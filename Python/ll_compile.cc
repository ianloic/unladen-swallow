#include "Python/ll_compile.h"

#include "Python.h"
#include "code.h"
#include "frameobject.h"

#include "Util/TypeBuilder.h"

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
using llvm::Constant;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::IntegerType;
using llvm::Module;
using llvm::PointerType;
using llvm::Type;
using llvm::Value;
using llvm::array_endof;

namespace py {

static ConstantInt *
get_signed_constant_int(const Type *type, int64_t v)
{
    // This is an LLVM idiom. It expects an unsigned integer but does
    // different conversions internally depending on whether it was
    // originally signed or not.
    return ConstantInt::get(type, static_cast<uint64_t>(v), true /* signed */);
}

template<> class TypeBuilder<PyObject> {
public:
    static const Type *cache(Module *module) {
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
            TypeBuilder<ssize_t>::cache(module),
            p_object_ty,
            NULL);
	// Unifies the OpaqueType fields with the whole structure.  We
	// couldn't do that originally because the type's recursive.
        llvm::cast<llvm::OpaqueType>(object_ty.get())
            ->refineAbstractTypeTo(temp_object_ty);
        module->addTypeName(pyobject_name, object_ty.get());
        return object_ty.get();
    }

    enum Fields {
#ifdef Py_TRACE_REFS
        FIELD_NEXT,
        FIELD_PREV,
#endif
        FIELD_REFCNT,
        FIELD_TYPE,
    };
};
typedef TypeBuilder<PyObject> ObjectTy;

template<> class TypeBuilder<PyTupleObject> {
public:
    static const Type *cache(Module *module) {
        std::string pytupleobject_name("__pytupleobject");
        const Type *result = module->getTypeByName(pytupleobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with code.h.
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From PyTupleObject
            TypeBuilder<PyObject*[]>::cache(module),  // ob_item
            NULL);

        module->addTypeName(pytupleobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_ITEM,
    };
};
typedef TypeBuilder<PyTupleObject> TupleTy;

template<> class TypeBuilder<PyTypeObject> {
public:
    static const Type *cache(Module *module) {
        std::string pytypeobject_name("__pytypeobject");
        const Type *result = module->getTypeByName(pytypeobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with code.h.
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From PyTYPEObject
            TypeBuilder<const char *>::cache(module),  // tp_name
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_basicsize
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_itemsize
            TypeBuilder<destructor>::cache(module),  // tp_dealloc
            // tp_print
            TypeBuilder<int (*)(PyObject*, char*, int)>::cache(module),
            TypeBuilder<getattrfunc>::cache(module),  // tp_getattr
            TypeBuilder<setattrfunc>::cache(module),  // tp_setattr
            TypeBuilder<cmpfunc>::cache(module),  // tp_compare
            TypeBuilder<reprfunc>::cache(module),  // tp_repr
            TypeBuilder<char *>::cache(module),  // tp_as_number
            TypeBuilder<char *>::cache(module),  // tp_as_sequence
            TypeBuilder<char *>::cache(module),  // tp_as_mapping
            TypeBuilder<hashfunc>::cache(module),  // tp_hash
            TypeBuilder<ternaryfunc>::cache(module),  // tp_call
            TypeBuilder<reprfunc>::cache(module),  // tp_str
            TypeBuilder<getattrofunc>::cache(module),  // tp_getattro
            TypeBuilder<setattrofunc>::cache(module),  // tp_setattro
            TypeBuilder<char *>::cache(module),  // tp_as_buffer
            TypeBuilder<long>::cache(module),  // tp_flags
            TypeBuilder<const char *>::cache(module),  // tp_doc
            TypeBuilder<traverseproc>::cache(module),  // tp_traverse
            TypeBuilder<inquiry>::cache(module),  // tp_clear
            TypeBuilder<richcmpfunc>::cache(module),  // tp_richcompare
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_weaklistoffset
            TypeBuilder<getiterfunc>::cache(module),  // tp_iter
            TypeBuilder<iternextfunc>::cache(module),  // tp_iternext
            TypeBuilder<char *>::cache(module),  // tp_methods
            TypeBuilder<char *>::cache(module),  // tp_members
            TypeBuilder<char *>::cache(module),  // tp_getset
            TypeBuilder<PyObject *>::cache(module),  // tp_base
            TypeBuilder<PyObject *>::cache(module),  // tp_dict
            TypeBuilder<descrgetfunc>::cache(module),  // tp_descr_get
            TypeBuilder<descrsetfunc>::cache(module),  // tp_descr_set
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_dictoffset
            TypeBuilder<initproc>::cache(module),  // tp_init
            // Can't use newfunc or allocfunc because they refer to
            // PyTypeObject.
            TypeBuilder<PyObject *(*)(PyObject *,
                                      Py_ssize_t)>::cache(module),  // tp_alloc
            TypeBuilder<PyObject *(*)(PyObject *, PyObject *,
                                      PyObject *)>::cache(module),  // tp_new
            TypeBuilder<freefunc>::cache(module),  // tp_free
            TypeBuilder<inquiry>::cache(module),  // tp_is_gc
            TypeBuilder<PyObject *>::cache(module),  // tp_bases
            TypeBuilder<PyObject *>::cache(module),  // tp_mro
            TypeBuilder<PyObject *>::cache(module),  // tp_cache
            TypeBuilder<PyObject *>::cache(module),  // tp_subclasses
            TypeBuilder<PyObject *>::cache(module),  // tp_weaklist
            TypeBuilder<destructor>::cache(module),  // tp_del
            TypeBuilder<unsigned int>::cache(module),  // tp_version_tag
#ifdef COUNT_ALLOCS
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_allocs
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_frees
            TypeBuilder<Py_ssize_t>::cache(module),  // tp_maxalloc
            TypeBuilder<PyObject *>::cache(module),  // tp_prev
            TypeBuilder<PyObject *>::cache(module),  // tp_next
#endif
            NULL);

        module->addTypeName(pytypeobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_NAME,
        FIELD_BASICSIZE,
        FIELD_ITEMSIZE,
        FIELD_DEALLOC,
        FIELD_PRINT,
        FIELD_GETATTR,
        FIELD_SETATTR,
        FIELD_COMPARE,
        FIELD_REPR,
        FIELD_AS_NUMBER,
        FIELD_AS_SEQUENCE,
        FIELD_AS_MAPPING,
        FIELD_HASH,
        FIELD_CALL,
        FIELD_STR,
        FIELD_GETATTRO,
        FIELD_SETATTRO,
        FIELD_AS_BUFFER,
        FIELD_FLAGS,
        FIELD_DOC,
        FIELD_TRAVERSE,
        FIELD_CLEAR,
        FIELD_RICHCOMPARE,
        FIELD_WEAKLISTOFFSET,
        FIELD_ITER,
        FIELD_ITERNEXT,
        FIELD_METHODS,
        FIELD_MEMBERS,
        FIELD_GETSET,
        FIELD_BASE,
        FIELD_DICT,
        FIELD_DESCR_GET,
        FIELD_DESCR_SET,
        FIELD_DICTOFFSET,
        FIELD_INIT,
        FIELD_ALLOC,
        FIELD_NEW,
        FIELD_FREE,
        FIELD_IS_GC,
        FIELD_BASES,
        FIELD_MRO,
        FIELD_CACHE,
        FIELD_SUBCLASSES,
        FIELD_WEAKLIST,
        FIELD_DEL,
        FIELD_TP_VERSION_TAG,
#ifdef COUNT_ALLOCS
        FIELD_ALLOCS,
        FIELD_FREES,
        FIELD_MAXALLOC,
        FIELD_PREV,
        FIELD_NEXT,
#endif
    };
};
typedef TypeBuilder<PyTypeObject> TypeTy;

template<> class TypeBuilder<PyCodeObject> {
public:
    static const Type *cache(Module *module) {
        std::string pycodeobject_name("__pycodeobject");
        const Type *result = module->getTypeByName(pycodeobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with code.h.
        const Type *p_pyobject_type = TypeBuilder<PyObject*>::cache(module);
        const Type *int_type = TypeBuilder<int>::cache(module);
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
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
            TypeBuilder<char*>::cache(module),  // co_tcode
            p_pyobject_type,  // co_filename
            p_pyobject_type,  // co_name
            int_type,  // co_firstlineno
            p_pyobject_type,  // co_lnotab
            TypeBuilder<char*>::cache(module),  //co_zombieframe
            p_pyobject_type,  // co_llvm_function
            NULL);

        module->addTypeName(pycodeobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_ARGCOUNT,
        FIELD_NLOCALS,
        FIELD_STACKSIZE,
        FIELD_FLAGS,
        FIELD_CODE,
        FIELD_CONSTS,
        FIELD_NAMES,
        FIELD_VARNAMES,
        FIELD_FREEVARS,
        FIELD_CELLVARS,
        FIELD_TCODE,
        FIELD_FILENAME,
        FIELD_NAME,
        FIELD_FIRSTLINENO,
        FIELD_LNOTAB,
        FIELD_ZOMBIEFRAME,
        FIELD_LLVM_FUNCTION,
    };
};
typedef TypeBuilder<PyCodeObject> CodeTy;

template<> class TypeBuilder<PyTryBlock> {
public:
    static const Type *cache(Module *module) {
        const Type *int_type = TypeBuilder<int>::cache(module);
        return llvm::StructType::get(
            // b_type, b_handler, b_level
            int_type, int_type, int_type, NULL);
    }
    enum Fields {
        FIELD_TYPE,
        FIELD_HANDLER,
        FIELD_LEVEL,
    };
};

template<> class TypeBuilder<PyFrameObject> {
public:
    static const Type *cache(Module *module) {
        std::string pyframeobject_name("__pyframeobject");
        const Type *result = module->getTypeByName(pyframeobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with frameobject.h.
        const Type *p_pyobject_type = TypeBuilder<PyObject*>::cache(module);
        const Type *int_type = TypeBuilder<int>::cache(module);
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            ObjectTy::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From struct _frame
            p_pyobject_type,  // f_back
            TypeBuilder<PyCodeObject*>::cache(module),  // f_code
            p_pyobject_type,  // f_builtins
            p_pyobject_type,  // f_globals
            p_pyobject_type,  // f_locals
            TypeBuilder<PyObject**>::cache(module),  // f_valuestack
            TypeBuilder<PyObject**>::cache(module),  // f_stacktop
            p_pyobject_type,  // f_trace
            p_pyobject_type,  // f_exc_type
            p_pyobject_type,  // f_exc_value
            p_pyobject_type,  // f_exc_traceback
            // f_tstate; punt on the type:
            TypeBuilder<char*>::cache(module),
            int_type,  // f_lasti
            int_type,  // f_lineno
            int_type,  // f_iblock
            // f_blockstack:
            TypeBuilder<PyTryBlock[CO_MAXBLOCKS]>::cache(module),
            // f_localsplus, flexible array.
            TypeBuilder<PyObject*[]>::cache(module),
            NULL);

        module->addTypeName(pyframeobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT_HEAD,
        FIELD_OB_SIZE,
        FIELD_BACK,
        FIELD_CODE,
        FIELD_BUILTINS,
        FIELD_GLOBALS,
        FIELD_LOCALS,
        FIELD_VALUESTACK,
        FIELD_STACKTOP,
        FIELD_TRACE,
        FIELD_EXC_TYPE,
        FIELD_EXC_VALUE,
        FIELD_EXC_TRACEBACK,
        FIELD_TSTATE,
        FIELD_LASTI,
        FIELD_LINENO,
        FIELD_IBLOCK,
        FIELD_BLOCKSTACK,
        FIELD_LOCALSPLUS,
    };
};
typedef TypeBuilder<PyFrameObject> FrameTy;

static const FunctionType *
get_function_type(Module *module)
{
    std::string function_type_name("__function_type");
    const FunctionType *result =
        llvm::cast_or_null<FunctionType>(
            module->getTypeByName(function_type_name));
    if (result != NULL)
        return result;

    result = TypeBuilder<PyObject*(PyFrameObject*)>::cache(module);
    module->addTypeName(function_type_name, result);
    return result;
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
    assert(args == function()->arg_end() &&
           "Unexpected number of arguments");
    this->frame_->setName("frame");

    builder().SetInsertPoint(BasicBlock::Create("entry", function()));
    this->return_block_ = BasicBlock::Create("return_block", function());

    this->stack_pointer_addr_ = builder().CreateAlloca(
        TypeBuilder<PyObject**>::cache(module),
        0, "stack_pointer_addr");
    this->retval_addr_ = builder().CreateAlloca(
        TypeBuilder<PyObject*>::cache(module),
        0, "retval_addr_");

    Value *initial_stack_pointer =
        builder().CreateLoad(
            builder().CreateStructGEP(this->frame_, FrameTy::FIELD_STACKTOP),
            "initial_stack_pointer");
    builder().CreateStore(initial_stack_pointer, this->stack_pointer_addr_);

    Value *code = builder().CreateLoad(
        builder().CreateStructGEP(this->frame_, FrameTy::FIELD_CODE), "co");
    this->varnames_ = builder().CreateLoad(
        builder().CreateStructGEP(code, CodeTy::FIELD_VARNAMES),
        "varnames");
    this->names_ = builder().CreateBitCast(
        builder().CreateLoad(
            builder().CreateStructGEP(code, CodeTy::FIELD_NAMES)),
        TypeBuilder<PyTupleObject*>::cache(module),
        "names");
    Value *consts_tuple =  // (PyTupleObject*)code->co_consts
        builder().CreateBitCast(
            builder().CreateLoad(
                builder().CreateStructGEP(code, CodeTy::FIELD_CONSTS)),
            TypeBuilder<PyTupleObject*>::cache(module));
    // The next GEP-magic assigns this->consts_ to &consts_tuple[0].ob_item[0].
    Value *consts_item_indices[] = {
        ConstantInt::get(Type::Int32Ty, 0),
        ConstantInt::get(Type::Int32Ty, TupleTy::FIELD_ITEM),
        ConstantInt::get(Type::Int32Ty, 0),
    };
    this->consts_ =
        builder().CreateGEP(
            consts_tuple,
            consts_item_indices, array_endof(consts_item_indices),
            "consts");

    // The next GEP-magic assigns this->fastlocals_ to
    // &frame_[0].f_localsplus[0].
    Value* fastlocals_indices[] = {
        ConstantInt::get(Type::Int32Ty, 0),
        ConstantInt::get(Type::Int32Ty, FrameTy::FIELD_LOCALSPLUS),
        ConstantInt::get(Type::Int32Ty, 0),
    };
    this->fastlocals_ =
        builder().CreateGEP(this->frame_,
                            fastlocals_indices, array_endof(fastlocals_indices),
                            "fastlocals");
    Value *nlocals = builder().CreateLoad(
        builder().CreateStructGEP(code, CodeTy::FIELD_NLOCALS), "nlocals");

    this->freevars_ =
        builder().CreateGEP(this->fastlocals_, nlocals, "freevars");

    FillReturnBlock(this->return_block_);
}

void
LlvmFunctionBuilder::FillReturnBlock(BasicBlock *return_block)
{
    BasicBlock *const orig_block = builder().GetInsertBlock();
    builder().SetInsertPoint(this->return_block_);
    Value *stack_bottom = builder().CreateLoad(
        builder().CreateStructGEP(this->frame_, FrameTy::FIELD_VALUESTACK),
        "stack_bottom");

    BasicBlock *pop_loop = BasicBlock::Create("pop_loop", function());
    BasicBlock *pop_block = BasicBlock::Create("pop_stack", function());
    BasicBlock *do_return = BasicBlock::Create("do_return", function());

    FallThroughTo(pop_loop);
    Value *stack_pointer = builder().CreateLoad(this->stack_pointer_addr_);
    Value *finished_popping = builder().CreateICmpULE(
        stack_pointer, stack_bottom);
    builder().CreateCondBr(finished_popping, do_return, pop_block);

    builder().SetInsertPoint(pop_block);
    XDecRef(Pop());
    builder().CreateBr(pop_loop);

    builder().SetInsertPoint(do_return);
    Value *retval = builder().CreateLoad(this->retval_addr_, "retval");
    builder().CreateRet(retval);

    builder().SetInsertPoint(orig_block);
}

void
LlvmFunctionBuilder::FallThroughTo(BasicBlock *next_block)
{
    if (builder().GetInsertBlock()->getTerminator() == NULL) {
        // If the block doesn't already end with a branch or
        // return, branch to the next block.
        builder().CreateBr(next_block);
    }
    builder().SetInsertPoint(next_block);
}

void
LlvmFunctionBuilder::Return(Value *retval)
{
    builder().CreateStore(retval, this->retval_addr_);
    builder().CreateBr(this->return_block_);
}

void
LlvmFunctionBuilder::LOAD_CONST(int index)
{
    Value *const_ = builder().CreateLoad(
        builder().CreateGEP(this->consts_,
                            ConstantInt::get(Type::Int32Ty, index)));
    IncRef(const_);
    Push(const_);
}

void
LlvmFunctionBuilder::LOAD_FAST(int index)
{
    BasicBlock *unbound_local =
        BasicBlock::Create("LOAD_FAST_unbound", function());
    BasicBlock *success =
        BasicBlock::Create("LOAD_FAST_success", function());

    Value *local = builder().CreateLoad(
        builder().CreateGEP(this->fastlocals_,
                            ConstantInt::get(Type::Int32Ty, index)),
        "FAST_loaded");
    builder().CreateCondBr(IsNull(local), unbound_local, success);

    builder().SetInsertPoint(unbound_local);
    Function *do_raise =
        GetGlobalFunction<void(PyFrameObject*, int)>(
            "_PyEval_RaiseForUnboundLocal");
    builder().CreateCall2(
        do_raise, this->frame_,
        ConstantInt::get(TypeBuilder<int>::cache(this->module_),
                         index, true /* signed */));
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(success);
    IncRef(local);
    Push(local);
}

void
LlvmFunctionBuilder::JUMP_ABSOLUTE(llvm::BasicBlock *target,
                                   llvm::BasicBlock *fallthrough)
{
    builder().CreateBr(target);
}

void
LlvmFunctionBuilder::STORE_FAST(int index)
{
    SetLocal(index, Pop());
}

void
LlvmFunctionBuilder::SETUP_LOOP(llvm::BasicBlock *target,
                                llvm::BasicBlock *fallthrough)
{
    // TODO: I think we can ignore this until we have an exception story.
    //InsertAbort("SETUP_LOOP");
}

void
LlvmFunctionBuilder::GET_ITER()
{
    Value *obj = Pop();
    Function *pyobject_getiter = GetGlobalFunction<PyObject*(PyObject*)>(
        "PyObject_GetIter");
    Value *iter = builder().CreateCall(pyobject_getiter, obj);
    DecRef(obj);
    BasicBlock *got_iter = BasicBlock::Create("got_iter", function());
    BasicBlock *was_exception = BasicBlock::Create("was_exception", function());
    builder().CreateCondBr(IsNull(iter), was_exception, got_iter);

    builder().SetInsertPoint(was_exception);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(got_iter);
    Push(iter);
}

void
LlvmFunctionBuilder::FOR_ITER(llvm::BasicBlock *target,
                              llvm::BasicBlock *fallthrough)
{
    Value *iter = Pop();
    Value *iter_tp = builder().CreateBitCast(
        builder().CreateLoad(
            builder().CreateStructGEP(iter, ObjectTy::FIELD_TYPE)),
        TypeBuilder<PyTypeObject *>::cache(this->module_),
        "iter_type");
    Value *iternext = builder().CreateLoad(
        builder().CreateStructGEP(iter_tp, TypeTy::FIELD_ITERNEXT),
        "iternext");
    Value *next = builder().CreateCall(iternext, iter, "next");
    BasicBlock *got_next = BasicBlock::Create("got_next", function());
    BasicBlock *next_null = BasicBlock::Create("next_null", function());
    builder().CreateCondBr(IsNull(next), next_null, got_next);

    builder().SetInsertPoint(next_null);
    Value *err_occurred = builder().CreateCall(
        GetGlobalFunction<PyObject*()>("PyErr_Occurred"));
    BasicBlock *iter_ended = BasicBlock::Create("iter_ended", function());
    BasicBlock *exception = BasicBlock::Create("exception", function());
    builder().CreateCondBr(IsNull(err_occurred), iter_ended, exception);

    builder().SetInsertPoint(exception);
    Value *exc_stopiteration = builder().CreateLoad(
        GetGlobalVariable<PyObject*>("PyExc_StopIteration"));
    Value *was_stopiteration = builder().CreateCall(
        GetGlobalFunction<int(PyObject *)>("PyErr_ExceptionMatches"),
        exc_stopiteration);
    BasicBlock *clear_err = BasicBlock::Create("clear_err", function());
    BasicBlock *propagate = BasicBlock::Create("propagate", function());
    builder().CreateCondBr(IsNonZero(was_stopiteration), clear_err, propagate);

    builder().SetInsertPoint(propagate);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(clear_err);
    builder().CreateCall(GetGlobalFunction<void()>("PyErr_Clear"));
    builder().CreateBr(iter_ended);

    builder().SetInsertPoint(iter_ended);
    DecRef(iter);
    builder().CreateBr(target);

    builder().SetInsertPoint(got_next);
    Push(iter);
    Push(next);
}

void
LlvmFunctionBuilder::POP_BLOCK()
{
    // TODO: I think we can ignore this until we have an exception story.
    //InsertAbort("POP_BLOCK");
}


void
LlvmFunctionBuilder::RETURN_VALUE()
{
    Value *retval = Pop();
    Return(retval);
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
    Value *reftotal_addr = GetGlobalVariable<Py_ssize_t>("_Py_RefTotal");
    increment_and_get(builder(), reftotal_addr, 1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, TypeBuilder<PyObject*>::cache(this->module_));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, ObjectTy::FIELD_REFCNT);
    increment_and_get(builder(), refcnt_addr, 1);
}

void
LlvmFunctionBuilder::DecRef(Value *value)
{
#ifdef Py_REF_DEBUG
    // Decrement the global reference count.
    Value *reftotal_addr = GetGlobalVariable<Py_ssize_t>("_Py_RefTotal");
    increment_and_get(builder(), reftotal_addr, -1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, TypeBuilder<PyObject*>::cache(this->module_));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, ObjectTy::FIELD_REFCNT);
    Value *new_refcnt = increment_and_get(builder(), refcnt_addr, -1);

    // Check if we need to deallocate the object.
    BasicBlock *block_dealloc = BasicBlock::Create("dealloc", this->function_);
    BasicBlock *block_tail = BasicBlock::Create("decref_tail", this->function_);
    BasicBlock *block_ref_ne_zero = block_tail;
#ifdef Py_REF_DEBUG
    block_ref_ne_zero = BasicBlock::Create("check_refcnt", this->function_);
#endif

    builder().CreateCondBr(IsNonZero(new_refcnt),
                           block_ref_ne_zero, block_dealloc);

#ifdef Py_REF_DEBUG
    builder().SetInsertPoint(block_ref_ne_zero);
    Value *less_zero = builder().CreateICmpSLT(
        new_refcnt, Constant::getNullValue(new_refcnt->getType()));
    BasicBlock *block_ref_lt_zero = BasicBlock::Create("negative_refcount",
                                                 this->function_);
    builder().CreateCondBr(less_zero, block_ref_lt_zero, block_tail);

    builder().SetInsertPoint(block_ref_lt_zero);
    Value *neg_refcount = GetGlobalFunction<void(const char*, int, PyObject*)>(
        "_Py_NegativeRefcount");
    // TODO: Well that __FILE__ and __LINE__ are going to be useless!
    builder().CreateCall3(
        neg_refcount,
        builder().CreateGlobalStringPtr(__FILE__, __FILE__),
        ConstantInt::get(TypeBuilder<int>::cache(this->module_), __LINE__),
        as_pyobject);
    builder().CreateBr(block_tail);
#endif

    builder().SetInsertPoint(block_dealloc);
    Value *dealloc = GetGlobalFunction<void(PyObject *)>("_PyLlvm_WrapDealloc");
    builder().CreateCall(dealloc, as_pyobject);
    builder().CreateBr(block_tail);

    builder().SetInsertPoint(block_tail);
}

void
LlvmFunctionBuilder::XDecRef(Value *value)
{
    BasicBlock *do_decref = BasicBlock::Create("decref", function());
    BasicBlock *decref_end = BasicBlock::Create("decref_end", function());
    builder().CreateCondBr(IsNull(value), decref_end, do_decref);

    builder().SetInsertPoint(do_decref);
    DecRef(value);
    builder().CreateBr(decref_end);

    builder().SetInsertPoint(decref_end);
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
LlvmFunctionBuilder::SetLocal(int locals_index, llvm::Value *new_value)
{
    Value *local_slot = builder().CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::Int32Ty, locals_index));
    Value *orig_value = builder().CreateLoad(local_slot, "local_overwritten");
    builder().CreateStore(new_value, local_slot);
    XDecRef(orig_value);
}


void
LlvmFunctionBuilder::InsertAbort(const char *opcode_name)
{
    std::string message("Undefined opcode: ");
    message.append(opcode_name);
    builder().CreateCall(GetGlobalFunction<int(const char*)>("puts"),
                         builder().CreateGlobalStringPtr(message.c_str(),
                                                         message.c_str()));
    builder().CreateCall(GetGlobalFunction<void()>("abort"));
}

template<typename FunctionType> Function *
LlvmFunctionBuilder::GetGlobalFunction(const std::string &name)
{
    return llvm::cast<Function>(
        this->module_->getOrInsertFunction(
            name, TypeBuilder<FunctionType>::cache(this->module_)));
}

template<typename VariableType> Constant *
LlvmFunctionBuilder::GetGlobalVariable(const std::string &name)
{
    return this->module_->getOrInsertGlobal(
        name, TypeBuilder<VariableType>::cache(this->module_));
}

Value *
LlvmFunctionBuilder::IsNull(Value *value)
{
    return builder().CreateICmpEQ(
        value, Constant::getNullValue(value->getType()));
}

Value *
LlvmFunctionBuilder::IsNonZero(Value *value)
{
    return builder().CreateICmpNE(
        value, Constant::getNullValue(value->getType()));
}

}  // namespace py


// Helper functions for the LLVM IR. These exist for
// non-speed-critical code that's easier to write in C, or for calls
// that are functions in pydebug mode and macros otherwise.
extern "C" {

void
_PyLlvm_WrapDealloc(PyObject *obj)
{
    _Py_Dealloc(obj);
}

}
