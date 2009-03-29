#include "Python/ll_compile.h"

#include "Python.h"
#include "code.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"
#include "llvm/Module.h"
#include "llvm/Type.h"

#include <vector>

using llvm::BasicBlock;
using llvm::ConstantInt;
using llvm::Function;
using llvm::IntegerType;
using llvm::Module;
using llvm::PointerType;
using llvm::Type;
using llvm::Value;

namespace py {

static ConstantInt *
get_signed_constant_int(const Type *type, int64_t v)
{
    // This is an LLVM idiom. It expects an unsigned integer but does
    // different conversions internally depending on whether it was
    // originally signed or not.
    return ConstantInt::get(type, static_cast<uint64_t>(v), true /* signed */);
}

static const Type *
get_pyobject_type(Module *module)
{
    std::string pyobject_name("__pyobject");
    const Type *result = module->getTypeByName(pyobject_name);
    if (result != NULL)
        return result;

    // Keep this in sync with object.h.
    llvm::PATypeHolder object_ty = llvm::OpaqueType::get();
    Type *p_object_ty = PointerType::getUnqual(object_ty);
    llvm::StructType *temp_object_ty = llvm::StructType::get(
        // Fields from PyObject_HEAD.
#ifdef Py_TRACE_REFS
        // _ob_next, _ob_prev
        p_object_ty, p_object_ty,
#endif
        IntegerType::get(sizeof(ssize_t) * 8),
        p_object_ty,
        NULL);
    // Unifies the OpaqueType fields with the whole structure.  We
    // couldn't do that originally because the type's recursive.
    llvm::cast<llvm::OpaqueType>(object_ty.get())
        ->refineAbstractTypeTo(temp_object_ty);
    module->addTypeName(pyobject_name, object_ty.get());
    return object_ty.get();
}

enum ObjectFields {
#ifdef Py_TRACE_REFS
    OBJECT_FIELD_NEXT,
    OBJECT_FIELD_PREV,
#endif
    OBJECT_FIELD_REFCNT,
    OBJECT_FIELD_TYPE,
};

static const Type *
get_pytupleobject_type(Module *module)
{
    std::string pytupleobject_name("__pytupleobject");
    const Type *result = module->getTypeByName(pytupleobject_name);
    if (result != NULL)
        return result;

    // Keep this in sync with code.h.
    const Type *pyobject_type = get_pyobject_type(module);
    const Type *p_pyobject_type = PointerType::getUnqual(pyobject_type);
    result = llvm::StructType::get(
        // From PyObject_HEAD. In C these are directly nested
        // fields, but the layout should be the same when it's
        // represented as a nested struct.
        pyobject_type,
        // From PyObject_VAR_HEAD
        IntegerType::get(sizeof(ssize_t) * 8),
        // From PyTupleObject
        llvm::ArrayType::get(p_pyobject_type, 0),  // ob_item
        NULL);

    module->addTypeName(pytupleobject_name, result);
    return result;
};

enum TupleFields {
    TUPLE_FIELD_OBJECT,
    TUPLE_FIELD_SIZE,
    TUPLE_FIELD_ITEM,
};

static const Type *
get_pycodeobject_type(Module *module)
{
    std::string pycodeobject_name("__pycodeobject");
    const Type *result = module->getTypeByName(pycodeobject_name);
    if (result != NULL)
        return result;

    // Keep this in sync with code.h.
    const Type *pyobject_type = get_pyobject_type(module);
    const Type *p_pyobject_type = PointerType::getUnqual(pyobject_type);
    const Type *int_type = IntegerType::get(sizeof(int) * 8);
    result = llvm::StructType::get(
        // From PyObject_HEAD. In C these are directly nested
        // fields, but the layout should be the same when it's
        // represented as a nested struct.
        pyobject_type,
        // From PyCodeObject
        int_type,  // co_argcount
        int_type,  // co_nlocals
        int_type,  // co_stacksize
        int_type,  // co_flags
        p_pyobject_type,  // co_code
        p_pyobject_type,  // co_consts
        p_pyobject_type,  // co_names
        p_pyobject_type,  // co_varnames
        p_pyobject_type,  // co_freevars
        p_pyobject_type,  // co_cellvars
        //  Not bothering with defining the Inst struct.
        PointerType::getUnqual(Type::Int8Ty),  // co_tcode
        p_pyobject_type,  // co_filename
        p_pyobject_type,  // co_name
        int_type,  // co_firstlineno
        p_pyobject_type,  // co_lnotab
        PointerType::getUnqual(Type::Int8Ty),  //co_zombieframe
        p_pyobject_type,  // co_llvm_function
        NULL);

    module->addTypeName(pycodeobject_name, result);
    return result;
};

enum CodeFields {
    CODE_FIELD_OBJECT,
    CODE_FIELD_ARGCOUNT,
    CODE_FIELD_NLOCALS,
    CODE_FIELD_STACKSIZE,
    CODE_FIELD_FLAGS,
    CODE_FIELD_CODE,
    CODE_FIELD_CONSTS,
    CODE_FIELD_NAMES,
    CODE_FIELD_VARNAMES,
    CODE_FIELD_FREEVARS,
    CODE_FIELD_CELLVARS,
    CODE_FIELD_TCODE,
    CODE_FIELD_FILENAME,
    CODE_FIELD_NAME,
    CODE_FIELD_FIRSTLINENO,
    CODE_FIELD_LNOTAB,
    CODE_FIELD_ZOMBIEFRAME,
    CODE_FIELD_LLVM_FUNCTION,
};

static const Type *
get_pyframeobject_type(Module *module)
{
    std::string pyframeobject_name("__pyframeobject");
    const Type *result = module->getTypeByName(pyframeobject_name);
    if (result != NULL)
        return result;

    // Keep this in sync with frameobject.h.
    const Type *pyobject_type = get_pyobject_type(module);
    const Type *p_pyobject_type = PointerType::getUnqual(pyobject_type);
    const Type *int_type = IntegerType::get(sizeof(int) * 8);
    const Type *pytryblock_type = llvm::StructType::get(
        // b_type, b_handler, b_level
        int_type, int_type, int_type, NULL);
    result = llvm::StructType::get(
        // From PyObject_HEAD. In C these are directly nested
        // fields, but the layout should be the same when it's
        // represented as a nested struct.
        pyobject_type,
        // From PyObject_VAR_HEAD
        IntegerType::get(sizeof(ssize_t) * 8),
        // From struct _frame
        p_pyobject_type,  // f_back
        PointerType::getUnqual(get_pycodeobject_type(module)),  // f_code
        p_pyobject_type,  // f_builtins
        p_pyobject_type,  // f_globals
        p_pyobject_type,  // f_locals
        PointerType::getUnqual(p_pyobject_type),  // f_valuestack
        PointerType::getUnqual(p_pyobject_type),  // f_stacktop
        p_pyobject_type,  // f_trace
        p_pyobject_type,  // f_exc_type
        p_pyobject_type,  // f_exc_value
        p_pyobject_type,  // f_exc_traceback
        // f_tstate; punt on the type:
        PointerType::getUnqual(Type::Int8Ty),
        int_type,  // f_lasti
        int_type,  // f_lineno
        int_type,  // f_iblock
        // f_blockstack:
        llvm::ArrayType::get(pytryblock_type, CO_MAXBLOCKS),
        // f_localsplus, flexible array.
        llvm::ArrayType::get(p_pyobject_type, 0),
        NULL);

    module->addTypeName(pyframeobject_name, result);
    return result;
}

enum FrameFields {
    FRAME_FIELD_OBJECT_HEAD,
    FRAME_FIELD_OB_SIZE,
    FRAME_FIELD_BACK,
    FRAME_FIELD_CODE,
    FRAME_FIELD_BUILTINS,
    FRAME_FIELD_GLOBALS,
    FRAME_FIELD_LOCALS,
    FRAME_FIELD_VALUESTACK,
    FRAME_FIELD_STACKTOP,
    FRAME_FIELD_TRACE,
    FRAME_FIELD_EXC_TYPE,
    FRAME_FIELD_EXC_VALUE,
    FRAME_FIELD_EXC_TRACEBACK,
    FRAME_FIELD_TSTATE,
    FRAME_FIELD_LASTI,
    FRAME_FIELD_LINENO,
    FRAME_FIELD_IBLOCK,
    FRAME_FIELD_BLOCKSTACK,
    FRAME_FIELD_LOCALSPLUS,
};

static const llvm::FunctionType *
get_function_type(Module *module)
{
    std::string function_type_name("__function_type");
    const llvm::FunctionType *result =
        llvm::cast_or_null<llvm::FunctionType>(
            module->getTypeByName(function_type_name));
    if (result != NULL)
        return result;

    const Type *p_pyobject_type =
        PointerType::getUnqual(get_pyobject_type(module));
    std::vector<const Type *> params(4,p_pyobject_type);
    params[0] = PointerType::getUnqual(get_pyframeobject_type(module));
    result = llvm::FunctionType::get(p_pyobject_type, params, false);
    module->addTypeName(function_type_name, result);
    return result;
}

static llvm::GlobalVariable *
get_py_reftotal(Module *module)
{
    const std::string py_reftotal_name("_Py_RefTotal");
    llvm::GlobalVariable *result = module->getGlobalVariable(py_reftotal_name);
    if (result != NULL)
        return result;

    // The Module keeps ownership of the new GlobalVariable, and will
    // return it the next time we call getGlobalVariable().
    return new llvm::GlobalVariable(
        IntegerType::get(sizeof(Py_ssize_t) * 8),
        false,  // Not constant.
        llvm::GlobalValue::ExternalLinkage,
        // NULL intializer makes this a declaration, to be imported from the
        // main Python executable.
        NULL,
        py_reftotal_name,
        module);
}

static Function *
get_py_negativerefcount(Module *module)
{
    const std::string py_negativerefcount_name("_Py_NegativeRefcount");
    if (Function *result = module->getFunction(py_negativerefcount_name))
        return result;

    // The Module keeps ownership of the new Function, and will return
    // it the next time we call getFunction().
    std::vector<const Type*> params;
    params.push_back(PointerType::getUnqual(Type::Int8Ty));
    params.push_back(IntegerType::get(sizeof(int) * 8));
    params.push_back(PointerType::getUnqual(get_pyobject_type(module)));
    // Declare void _Py_NegativeRefcount(char*, int, PyObject*);
    return Function::Create(llvm::FunctionType::get(Type::VoidTy,
                                                    params,
                                                    false /* not vararg */),
                            llvm::GlobalValue::ExternalLinkage,
                            py_negativerefcount_name, module);
}

static Function *
get_py_dealloc(Module *module)
{
    const std::string py_dealloc_name("_Py_Dealloc");
    if (Function *result = module->getFunction(py_dealloc_name))
        return result;

    // The Module keeps ownership of the new Function, and will return
    // it the next time we call getFunction().
    std::vector<const Type*> params;
    params.push_back(PointerType::getUnqual(get_pyobject_type(module)));
    // Declare void _Py_Dealloc(PyObject*);
    return Function::Create(llvm::FunctionType::get(Type::VoidTy,
                                                    params,
                                                    false /* not vararg */),
                            llvm::GlobalValue::ExternalLinkage,
                            py_dealloc_name, module);
}

LlvmFunctionBuilder::LlvmFunctionBuilder(
    Module *module, const std::string& name)
    : module_(module),
      function_(Function::Create(
                    get_function_type(module),
                    llvm::GlobalValue::ExternalLinkage,
                    name,
                    module))
{
    Function::arg_iterator args = function()->arg_begin();
    this->frame_ = args++;
    this->self_ = args++;
    this->args_ = args++;
    this->kwargs_ = args++;
    assert(args == function()->arg_end() &&
           "Unexpected number of arguments");

    builder().SetInsertPoint(BasicBlock::Create("entry", function()));

    const Type *pyobject_type = get_pyobject_type(module);
    this->stack_pointer_addr_ = builder().CreateAlloca(
        PointerType::getUnqual(PointerType::getUnqual(pyobject_type)),
        0, "stack_pointer_addr");
    Value *initial_stack_pointer =
        builder().CreateLoad(
            builder().CreateStructGEP(this->frame_, FRAME_FIELD_STACKTOP),
            "initial_stack_pointer");
    builder().CreateStore(initial_stack_pointer, this->stack_pointer_addr_);

    Value *code = builder().CreateLoad(
        builder().CreateStructGEP(this->frame_, FRAME_FIELD_CODE), "co");
    this->consts_ = builder().CreateBitCast(
        builder().CreateLoad(
            builder().CreateStructGEP(code, CODE_FIELD_CONSTS)),
        PointerType::getUnqual(get_pytupleobject_type(module)),
        "consts");
}

void
LlvmFunctionBuilder::LOAD_CONST(int index)
{
    Value *indices[] = {
        ConstantInt::get(Type::Int32Ty, 0),
        ConstantInt::get(Type::Int32Ty, TUPLE_FIELD_ITEM),
        ConstantInt::get(Type::Int32Ty, index),
    };
    Value *const_ = builder().CreateLoad(
        builder().CreateGEP(this->consts_, indices, array_endof(indices)));
    IncRef(const_);
    Push(const_);
}

void
LlvmFunctionBuilder::RETURN_VALUE()
{
    Value *retval = Pop();
    builder().CreateRet(retval);
}


// Adds delta to *addr, and returns the new value.
static Value *
increment_and_get(llvm::IRBuilder<>& builder, Value *addr, int64_t delta)
{
    Value *orig = builder.CreateLoad(addr);
    Value *new_ = builder.CreateAdd(
        orig,
        get_signed_constant_int(orig->getType(), delta));
    builder.CreateStore(new_, addr);
    return new_;
}

void
LlvmFunctionBuilder::IncRef(Value *value)
{
#ifdef Py_REF_DEBUG
    // Increment the global reference count.
    Value *reftotal_addr = get_py_reftotal(this->module_);
    increment_and_get(builder(), reftotal_addr, 1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, PointerType::getUnqual(get_pyobject_type(this->module_)));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, OBJECT_FIELD_REFCNT);
    increment_and_get(builder(), refcnt_addr, 1);
}

void
LlvmFunctionBuilder::DecRef(Value *value)
{
#ifdef Py_REF_DEBUG
    // Decrement the global reference count.
    Value *reftotal_addr = get_py_reftotal(this->module_);
    increment_and_get(builder(), reftotal_addr, -1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, PointerType::getUnqual(get_pyobject_type(this->module_)));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, OBJECT_FIELD_REFCNT);
    Value *new_refcnt = increment_and_get(builder(), refcnt_addr, -1);

    // Check if we need to deallocate the object.
    BasicBlock *block_dealloc = BasicBlock::Create("dealloc", this->function_);
    BasicBlock *block_tail = BasicBlock::Create("decref_tail", this->function_);
    BasicBlock *block_ref_ne_zero = block_tail;
#ifdef Py_REF_DEBUG
    block_ref_ne_zero = BasicBlock::Create("check_refcnt", this->function_);
#endif

    Value *ne_zero = builder().CreateICmpNE(
        new_refcnt, llvm::Constant::getNullValue(new_refcnt->getType()));
    builder().CreateCondBr(ne_zero, block_ref_ne_zero, block_dealloc);

#ifdef Py_REF_DEBUG
    builder().SetInsertPoint(block_ref_ne_zero);
    Value *less_zero = builder().CreateICmpSLT(
        new_refcnt, llvm::Constant::getNullValue(new_refcnt->getType()));
    BasicBlock *block_ref_lt_zero = BasicBlock::Create("negative_refcount",
                                                 this->function_);
    builder().CreateCondBr(less_zero, block_ref_lt_zero, block_tail);

    builder().SetInsertPoint(block_ref_lt_zero);
    Value *neg_refcount = get_py_negativerefcount(this->module_);
    // TODO: Well that __FILE__ and __LINE__ are going to be useless!
    builder().CreateCall3(
        neg_refcount,
        llvm::ConstantArray::get(__FILE__),
        ConstantInt::get(IntegerType::get(sizeof(int) * 8), __LINE__),
        as_pyobject);
    builder().CreateBr(block_tail);
#endif

    builder().SetInsertPoint(block_dealloc);
    Value *dealloc = get_py_dealloc(this->module_);
    builder().CreateCall(dealloc, as_pyobject);

    builder().SetInsertPoint(block_tail);
}

void
LlvmFunctionBuilder::Push(Value *value)
{
    Value *stack_pointer = builder().CreateLoad(this->stack_pointer_addr_);
    builder().CreateStore(value, stack_pointer);
    Value *new_stack_pointer = builder().CreateGEP(
        stack_pointer, ConstantInt::get(Type::Int32Ty, 1));
    builder().CreateStore(new_stack_pointer, this->stack_pointer_addr_);
}

Value *
LlvmFunctionBuilder::Pop()
{
    Value *stack_pointer = builder().CreateLoad(this->stack_pointer_addr_);
    Value *new_stack_pointer = builder().CreateGEP(
        stack_pointer, get_signed_constant_int(Type::Int32Ty, -1));
    Value *former_top = builder().CreateLoad(new_stack_pointer);
    builder().CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    return former_top;
}

void
LlvmFunctionBuilder::InsertAbort()
{
    builder().CreateCall(llvm::Intrinsic::getDeclaration(
                             this->module_, llvm::Intrinsic::trap));
}


}  // namespace py
