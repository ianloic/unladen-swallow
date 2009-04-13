#include "Python/ll_compile.h"

#include "Python.h"
#include "code.h"
#include "opcode.h"
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

        // Keep this in sync with tupleobject.h.
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

template<> class TypeBuilder<PyListObject> {
public:
    static const Type *cache(Module *module) {
        std::string pylistobject_name("__pylistobject");
        const Type *result = module->getTypeByName(pylistobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with listobject.h.
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From PyListObject
            TypeBuilder<PyObject**>::cache(module),  // ob_item
            TypeBuilder<Py_ssize_t>::cache(module),  // allocated
            NULL);

        module->addTypeName(pylistobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_ITEM,
        FIELD_ALLOCATED,
    };
};
typedef TypeBuilder<PyListObject> ListTy;

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
    // Get the address of the const_tuple's first item, for convenient
    // indexing of all its items.
    this->consts_ = GetTupleItemSlot(consts_tuple, 0);

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
LlvmFunctionBuilder::DELETE_FAST(int index)
{
    BasicBlock *failure =
        BasicBlock::Create("DELETE_FAST_failure", function());
    BasicBlock *success =
        BasicBlock::Create("DELETE_FAST_success", function());
    Value *local_slot = builder().CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::Int32Ty, index));
    Value *orig_value = builder().CreateLoad(
        local_slot, "DELETE_FAST_old_reference");
    builder().CreateCondBr(IsNull(orig_value), failure, success);

    builder().SetInsertPoint(failure);
    Function *do_raise = GetGlobalFunction<
        void(PyFrameObject *, int)>("_PyEval_RaiseForUnboundLocal");
    builder().CreateCall2(
        do_raise, this->frame_,
        ConstantInt::get(TypeBuilder<int>::cache(this->module_),
                         index, true /* signed */));
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(success);
    builder().CreateStore(
        Constant::getNullValue(TypeBuilder<PyObject *>::cache(this->module_)),
        local_slot);
    DecRef(orig_value);
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
    PropagateExceptionOnNull(iter);
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
    DecRef(iter);
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

void
LlvmFunctionBuilder::STORE_SUBSCR()
{
    // Performing obj[key] = val
    Value *key = Pop();
    Value *obj = Pop();
    Value *value = Pop();
    Function *setitem = GetGlobalFunction<
          int(PyObject *, PyObject *, PyObject *)>("PyObject_SetItem");
    Value *result = builder().CreateCall3(setitem, obj, key, value,
                                          "STORE_SUBSCR_result");
    DecRef(value);
    DecRef(obj);
    DecRef(key);
    PropagateExceptionOnNonZero(result);
}

// Common code for almost all binary operations
void
LlvmFunctionBuilder::GenericBinOp(const char *apifunc)
{
    Value *rhs = Pop();
    Value *lhs = Pop();
    Function *op =
        GetGlobalFunction<PyObject*(PyObject*, PyObject*)>(apifunc);
    Value *result = builder().CreateCall2(op, lhs, rhs, "binop_result");
    DecRef(lhs);
    DecRef(rhs);
    PropagateExceptionOnNull(result);
    Push(result);
}

#define BINOP_METH(OPCODE, APIFUNC) 		\
void						\
LlvmFunctionBuilder::OPCODE()			\
{						\
    GenericBinOp(#APIFUNC);			\
}

BINOP_METH(BINARY_ADD, PyNumber_Add)
BINOP_METH(BINARY_SUBTRACT, PyNumber_Subtract)
BINOP_METH(BINARY_MULTIPLY, PyNumber_Multiply)
BINOP_METH(BINARY_TRUE_DIVIDE, PyNumber_TrueDivide)
BINOP_METH(BINARY_DIVIDE, PyNumber_Divide)
BINOP_METH(BINARY_MODULO, PyNumber_Remainder)
BINOP_METH(BINARY_LSHIFT, PyNumber_Lshift)
BINOP_METH(BINARY_RSHIFT, PyNumber_Rshift)
BINOP_METH(BINARY_OR, PyNumber_Or)
BINOP_METH(BINARY_XOR, PyNumber_Xor)
BINOP_METH(BINARY_AND, PyNumber_And)
BINOP_METH(BINARY_FLOOR_DIVIDE, PyNumber_FloorDivide)
BINOP_METH(BINARY_SUBSCR, PyObject_GetItem)

BINOP_METH(INPLACE_ADD, PyNumber_InPlaceAdd)
BINOP_METH(INPLACE_SUBTRACT, PyNumber_InPlaceSubtract)
BINOP_METH(INPLACE_MULTIPLY, PyNumber_InPlaceMultiply)
BINOP_METH(INPLACE_TRUE_DIVIDE, PyNumber_InPlaceTrueDivide)
BINOP_METH(INPLACE_DIVIDE, PyNumber_InPlaceDivide)
BINOP_METH(INPLACE_MODULO, PyNumber_InPlaceRemainder)
BINOP_METH(INPLACE_LSHIFT, PyNumber_InPlaceLshift)
BINOP_METH(INPLACE_RSHIFT, PyNumber_InPlaceRshift)
BINOP_METH(INPLACE_OR, PyNumber_InPlaceOr)
BINOP_METH(INPLACE_XOR, PyNumber_InPlaceXor)
BINOP_METH(INPLACE_AND, PyNumber_InPlaceAnd)
BINOP_METH(INPLACE_FLOOR_DIVIDE, PyNumber_InPlaceFloorDivide)

#undef BINOP_METH

// PyNumber_Power() and PyNumber_InPlacePower() take three arguments, the
// third should be Py_None when calling from BINARY_POWER/INPLACE_POWER.
void
LlvmFunctionBuilder::GenericPowOp(const char *apifunc)
{
    Value *rhs = Pop();
    Value *lhs = Pop();
    Function *op = GetGlobalFunction<PyObject*(PyObject*, PyObject*,
        PyObject *)>(apifunc);
    Value *pynone = GetGlobalVariable<PyObject>("_Py_NoneStruct");
    Value *result = builder().CreateCall3(op, lhs, rhs, pynone,
                                          "powop_result");
    DecRef(lhs);
    DecRef(rhs);
    PropagateExceptionOnNull(result);
    Push(result);
}

void
LlvmFunctionBuilder::BINARY_POWER()
{
    GenericPowOp("PyNumber_Power");
}

void
LlvmFunctionBuilder::INPLACE_POWER()
{
    GenericPowOp("PyNumber_InPlacePower");
}

// Implementation of almost all unary operations
void
LlvmFunctionBuilder::GenericUnaryOp(const char *apifunc)
{
    Value *value = Pop();
    Function *op = GetGlobalFunction<PyObject*(PyObject*)>(apifunc);
    Value *result = builder().CreateCall(op, value, "unaryop_result");
    DecRef(value);
    PropagateExceptionOnNull(result);
    Push(result);
}

#define UNARYOP_METH(NAME, APIFUNC)			\
void							\
LlvmFunctionBuilder::NAME()				\
{							\
    GenericUnaryOp(#APIFUNC);				\
}

UNARYOP_METH(UNARY_CONVERT, PyObject_Repr)
UNARYOP_METH(UNARY_INVERT, PyNumber_Invert)
UNARYOP_METH(UNARY_POSITIVE, PyNumber_Positive)
UNARYOP_METH(UNARY_NEGATIVE, PyNumber_Negative)

#undef UNARYOP_METH

void
LlvmFunctionBuilder::UNARY_NOT()
{
    BasicBlock *success = BasicBlock::Create("UNARY_NOT_success", function());
    BasicBlock *failure = BasicBlock::Create("UNARY_NOT_failure", function());

    Value *value = Pop();
    Function *pyobject_istrue =
        GetGlobalFunction<int(PyObject *)>("PyObject_IsTrue");
    Value *result = builder().CreateCall(pyobject_istrue, value,
                                         "UNARY_NOT_obj_as_bool");
    DecRef(value);
    builder().CreateCondBr(IsNegative(result), failure, success);

    builder().SetInsertPoint(failure);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(success);
    Value *retval = builder().CreateSelect(
        IsNonZero(result),
        GetGlobalVariable<PyObject>("_Py_ZeroStruct"),
        GetGlobalVariable<PyObject>("_Py_TrueStruct"),
        "UNARY_NOT_result");
    IncRef(retval);
    Push(retval);
}

void
LlvmFunctionBuilder::DUP_TOP_TWO()
{
    Value *first = Pop();
    Value *second = Pop();
    IncRef(first);
    IncRef(second);
    Push(second);
    Push(first);
    Push(second);
    Push(first);
}

void
LlvmFunctionBuilder::ROT_THREE()
{
    Value *first = Pop();
    Value *second = Pop();
    Value *third = Pop();
    Push(first);
    Push(third);
    Push(second);
}

void
LlvmFunctionBuilder::RichCompare(Value *lhs, Value *rhs, int cmp_op)
{
    Function *pyobject_richcompare = GetGlobalFunction<
        PyObject *(PyObject *, PyObject *, int)>("PyObject_RichCompare");
    Value *result = builder().CreateCall3(
        pyobject_richcompare, lhs, rhs,
        ConstantInt::get(TypeBuilder<int>::cache(this->module_), cmp_op),
        "COMPARE_OP_RichCompare_result");
    DecRef(lhs);
    DecRef(rhs);
    PropagateExceptionOnNull(result);
    Push(result);
}

Value *
LlvmFunctionBuilder::ContainerContains(Value *container, Value *item)
{
    Function *contains =
        GetGlobalFunction<int(PyObject *, PyObject *)>("PySequence_Contains");
    Value *result = builder().CreateCall2(
        contains, container, item, "ContainerContains_result");
    DecRef(item);
    DecRef(container);
    PropagateExceptionOnNegative(result);
    Value *bool_result = builder().CreateICmpSGT(
        result,
        ConstantInt::get(result->getType(), 0, true /* signed */),
        "COMPARE_OP_IN_result");
    return bool_result;
}

// TODO(twouters): test this (used in exception handling.)
Value *
LlvmFunctionBuilder::ExceptionMatches(Value *exc, Value *exc_type)
{
    Function *exc_matches = GetGlobalFunction<
        int(PyObject *, PyObject *)>("_PyEval_CheckedExceptionMatches");
    Value *result = builder().CreateCall2(
        exc_matches, exc, exc_type, "ExceptionMatches_result");
    DecRef(exc_type);
    DecRef(exc);
    PropagateExceptionOnNegative(result);
    Value *bool_result = builder().CreateICmpSGT(
        result,
        ConstantInt::get(result->getType(), 0, true /* signed */),
        "COMPARE_OP_EXC_MATCH_result");
    return bool_result;
}

void
LlvmFunctionBuilder::COMPARE_OP(int cmp_op)
{
    Value *rhs = Pop();
    Value *lhs = Pop();
    Value *result;
    switch (cmp_op) {
    case PyCmp_IS:
        result = builder().CreateICmpEQ(lhs, rhs, "COMPARE_OP_is_same");
        DecRef(lhs);
        DecRef(rhs);
        break;
    case PyCmp_IS_NOT:
        result = builder().CreateICmpNE(lhs, rhs, "COMPARE_OP_is_not_same");
        DecRef(lhs);
        DecRef(rhs);
        break;
    case PyCmp_IN:
        // item in seq -> ContainerContains(seq, item)
        result = ContainerContains(rhs, lhs);
        break;
    case PyCmp_NOT_IN:
    {
        Value *inverted_result = ContainerContains(rhs, lhs);
        result = builder().CreateICmpEQ(
            inverted_result, ConstantInt::get(inverted_result->getType(), 0),
            "COMPARE_OP_not_in_result");
        break;
    }
    case PyCmp_EXC_MATCH:
        result = ExceptionMatches(lhs, rhs);
        break;
    case PyCmp_EQ:
    case PyCmp_NE:
    case PyCmp_LT:
    case PyCmp_LE:
    case PyCmp_GT:
    case PyCmp_GE:
        RichCompare(lhs, rhs, cmp_op);
        return;
    default:
        Py_FatalError("unknown COMPARE_OP oparg");;
    }
    Value *value = builder().CreateSelect(
        result,
        GetGlobalVariable<PyObject>("_Py_TrueStruct"),
        GetGlobalVariable<PyObject>("_Py_ZeroStruct"),
        "COMPARE_OP_result");
    IncRef(value);
    Push(value);
}

Value *
LlvmFunctionBuilder::GetListItemSlot(Value *lst, int idx)
{
    Value *listobj = builder().CreateBitCast(
        lst, TypeBuilder<PyListObject *>::cache(this->module_));
    // Load the target of the ob_item PyObject** into list_items.
    Value *list_items = builder().CreateLoad(
        builder().CreateStructGEP(listobj, ListTy::FIELD_ITEM));
    // GEP the list_items PyObject* up to the desired item
    return builder().CreateGEP(list_items,
                               ConstantInt::get(Type::Int32Ty, idx),
                               "list_item_slot");
}

Value *
LlvmFunctionBuilder::GetTupleItemSlot(Value *tup, int idx)
{
    Value *tupobj = builder().CreateBitCast(
        tup, TypeBuilder<PyTupleObject*>::cache(this->module_));
    // Make CreateGEP perform &tup_item_indices[0].ob_item[idx].
    Value *tup_item_indices[] = {
        ConstantInt::get(Type::Int32Ty, 0),
        ConstantInt::get(Type::Int32Ty, TupleTy::FIELD_ITEM),
        ConstantInt::get(Type::Int32Ty, idx),
    };
    return builder().CreateGEP(tupobj, tup_item_indices,
                               array_endof(tup_item_indices),
                               "tuple_item_slot");
}

void
LlvmFunctionBuilder::BuildSequenceLiteral(
    int size, const char *createname,
    Value *(LlvmFunctionBuilder::*getitemslot)(Value*, int))
{
    const Type *IntSsizeTy = TypeBuilder<Py_ssize_t>::cache(this->module_);
    Value *seqsize = ConstantInt::get(IntSsizeTy, size, true /* signed */);

    Function *create = GetGlobalFunction<PyObject *(Py_ssize_t)>(createname);
    Value *seq = builder().CreateCall(create, seqsize, "sequence_literal");
    PropagateExceptionOnNull(seq);

    // XXX(twouters): do this with a memcpy?
    while (--size >= 0) {
        Value *itemslot = (this->*getitemslot)(seq, size);
        builder().CreateStore(Pop(), itemslot);
    }
    Push(seq);
}

void
LlvmFunctionBuilder::BUILD_LIST(int size)
{
   BuildSequenceLiteral(size, "PyList_New",
                        &LlvmFunctionBuilder::GetListItemSlot);
}

void
LlvmFunctionBuilder::BUILD_TUPLE(int size)
{
   BuildSequenceLiteral(size, "PyTuple_New",
                        &LlvmFunctionBuilder::GetTupleItemSlot);
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

Value *
LlvmFunctionBuilder::IsNegative(Value *value)
{
    return builder().CreateICmpSLT(
        value, ConstantInt::get(value->getType(), 0, true /* signed */));
}

// TODO(twouters): do actual exception handling here, instead of blindly
// returning NULL from the current function.
void
LlvmFunctionBuilder::PropagateExceptionOnNull(Value *value)
{
    BasicBlock *propagate =
        BasicBlock::Create("PropagateExceptionOnNull_propagate", function());
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNull_pass", function());
    builder().CreateCondBr(IsNull(value), propagate, pass);

    builder().SetInsertPoint(propagate);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(pass);
}

// TODO(twouters): do actual exception handling here, instead of blindly
// returning NULL from the current function.
void
LlvmFunctionBuilder::PropagateExceptionOnNegative(Value *value)
{
    BasicBlock *propagate =
        BasicBlock::Create("PropagateExceptionOnNegative_propagate",
        function());
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNegative_pass", function());
    builder().CreateCondBr(IsNegative(value), propagate, pass);

    builder().SetInsertPoint(propagate);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(pass);
}

// TODO(twouters): do actual exception handling here, instead of blindly
// returning NULL from the current function.
void
LlvmFunctionBuilder::PropagateExceptionOnNonZero(Value *value)
{
    BasicBlock *propagate =
        BasicBlock::Create("PropagateExceptionOnNonZero_propagate",
        function());
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNonZero_pass", function());
    builder().CreateCondBr(IsNonZero(value), propagate, pass);

    builder().SetInsertPoint(propagate);
    Return(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(pass);
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
