// -*- C++ -*-
#ifndef UTIL_PYTYPEBUILDER_H
#define UTIL_PYTYPEBUILDER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python.h"
#include "code.h"
#include "frameobject.h"
#include "longintrepr.h"

#include "Python/global_llvm_data.h"

#include "llvm/Module.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/TypeBuilder.h"

struct PyExcInfo;

// llvm::TypeBuilder requires a boolean parameter specifying whether
// the type needs to be cross-compilable.  In Python, we don't need
// anything to be cross-compilable (the VM is by definition running on
// the platform it's generating code for), so we define PyTypeBuilder
// to hard-code that parameter to false.
template<typename T>
class PyTypeBuilder : public llvm::TypeBuilder<T, false> {};

// This function uses the JIT compiler's TargetData object to convert
// from a byte offset inside a type to a GEP index referring to the
// field of the type.  This should be called like
//   _PyTypeBuilder_GetFieldIndexFromOffset(
//       PyTypeBuilder<PySomethingType>::get(context),
//       offsetof(PySomethingType, field));
// It will only work if PySomethingType is a POD type.
PyAPI_FUNC(unsigned int)
_PyTypeBuilder_GetFieldIndexFromOffset(
    const llvm::StructType *type, size_t offset);

// Enter the LLVM namespace in order to add specializations of
// llvm::TypeBuilder.
namespace llvm {

// Defines a static member function FIELD_NAME(ir_builder, ptr) to
// access TYPE::FIELD_NAME inside ptr.  GetElementPtr instructions
// require the index of the field within the type, but padding makes
// it hard to predict that index from the list of fields in the type.
// Because the compiler building this file knows the byte offset of
// the field, we can use llvm::TargetData to compute the index.  This
// has the extra benefit that it's more resilient to changes in the
// set or order of fields in a type.
#define DEFINE_FIELD(TYPE, FIELD_NAME) \
    static unsigned FIELD_NAME##_index(LLVMContext &context) { \
        static const unsigned int index = \
            _PyTypeBuilder_GetFieldIndexFromOffset( \
                PyTypeBuilder<TYPE>::get(context), \
                offsetof(TYPE, FIELD_NAME)); \
        return index; \
    } \
    template<bool preserveNames, typename Folder> \
    static Value *FIELD_NAME(IRBuilder<preserveNames, Folder> &builder, \
                             Value *ptr) { \
        assert(ptr->getType() == PyTypeBuilder<TYPE*>::get(ptr->getContext()) \
               && "*ptr must be of type " #TYPE); \
        return builder.CreateStructGEP( \
            ptr, FIELD_NAME##_index(ptr->getContext()), #FIELD_NAME); \
    }

#ifdef Py_TRACE_REFS
#define DEFINE_OBJECT_HEAD_FIELDS(TYPE) \
    DEFINE_FIELD(TYPE, _ob_next) \
    DEFINE_FIELD(TYPE, _ob_prev) \
    DEFINE_FIELD(TYPE, ob_refcnt) \
    DEFINE_FIELD(TYPE, ob_type)
#else
#define DEFINE_OBJECT_HEAD_FIELDS(TYPE) \
    DEFINE_FIELD(TYPE, ob_refcnt) \
    DEFINE_FIELD(TYPE, ob_type)
#endif

template<> class TypeBuilder<PyObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyObject struct.
                "struct._object"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyObject)
};

template<> class TypeBuilder<PyVarObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyVarObject struct.
                "struct.PyVarObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyObject)
    DEFINE_FIELD(PyVarObject, ob_size)
};

template<> class TypeBuilder<PyStringObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyStringObject struct.
                "struct.PyStringObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyStringObject)
    DEFINE_FIELD(PyStringObject, ob_size)
    DEFINE_FIELD(PyStringObject, ob_shash)
    DEFINE_FIELD(PyStringObject, ob_sstate)
    DEFINE_FIELD(PyStringObject, ob_sval)
};

template<> class TypeBuilder<PyUnicodeObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyUnicodeObject struct.
                "struct.PyUnicodeObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyUnicodeObject)
    DEFINE_FIELD(PyUnicodeObject, length)
    DEFINE_FIELD(PyUnicodeObject, str)
    DEFINE_FIELD(PyUnicodeObject, hash)
    DEFINE_FIELD(PyUnicodeObject, defenc)
};

template<> class TypeBuilder<PyIntObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyIntObject struct.
                "struct.PyIntObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyIntObject)
    DEFINE_FIELD(PyIntObject, ob_ival)
};

template<> class TypeBuilder<PyLongObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyLongObject struct.
                "struct._longobject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyLongObject)
    DEFINE_FIELD(PyLongObject, ob_digit)
};

template<> class TypeBuilder<PyFloatObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyFloatObject struct.
                "struct.PyFloatObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyFloatObject)
    DEFINE_FIELD(PyFloatObject, ob_fval)
};

template<> class TypeBuilder<PyComplexObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyFloatObject struct.
                "struct.PyComplexObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyComplexObject)
    DEFINE_FIELD(PyComplexObject, cval)
};

template<> class TypeBuilder<PyTupleObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTupleObject struct.
                "struct.PyTupleObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyTupleObject)
    DEFINE_FIELD(PyTupleObject, ob_size)
    DEFINE_FIELD(PyTupleObject, ob_item)
};

template<> class TypeBuilder<PyListObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyListObject struct.
                "struct.PyListObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyListObject)
    DEFINE_FIELD(PyListObject, ob_size)
    DEFINE_FIELD(PyListObject, ob_item)
    DEFINE_FIELD(PyListObject, allocated)
};

template<> class TypeBuilder<PyTypeObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct._typeobject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyTypeObject)
    DEFINE_FIELD(PyTypeObject, ob_size)
    DEFINE_FIELD(PyTypeObject, tp_name)
    DEFINE_FIELD(PyTypeObject, tp_basicsize)
    DEFINE_FIELD(PyTypeObject, tp_itemsize)
    DEFINE_FIELD(PyTypeObject, tp_dealloc)
    DEFINE_FIELD(PyTypeObject, tp_print)
    DEFINE_FIELD(PyTypeObject, tp_getattr)
    DEFINE_FIELD(PyTypeObject, tp_setattr)
    DEFINE_FIELD(PyTypeObject, tp_compare)
    DEFINE_FIELD(PyTypeObject, tp_repr)
    DEFINE_FIELD(PyTypeObject, tp_as_number)
    DEFINE_FIELD(PyTypeObject, tp_as_sequence)
    DEFINE_FIELD(PyTypeObject, tp_as_mapping)
    DEFINE_FIELD(PyTypeObject, tp_hash)
    DEFINE_FIELD(PyTypeObject, tp_call)
    DEFINE_FIELD(PyTypeObject, tp_str)
    DEFINE_FIELD(PyTypeObject, tp_getattro)
    DEFINE_FIELD(PyTypeObject, tp_setattro)
    DEFINE_FIELD(PyTypeObject, tp_as_buffer)
    DEFINE_FIELD(PyTypeObject, tp_flags)
    DEFINE_FIELD(PyTypeObject, tp_doc)
    DEFINE_FIELD(PyTypeObject, tp_traverse)
    DEFINE_FIELD(PyTypeObject, tp_clear)
    DEFINE_FIELD(PyTypeObject, tp_richcompare)
    DEFINE_FIELD(PyTypeObject, tp_weaklistoffset)
    DEFINE_FIELD(PyTypeObject, tp_iter)
    DEFINE_FIELD(PyTypeObject, tp_iternext)
    DEFINE_FIELD(PyTypeObject, tp_methods)
    DEFINE_FIELD(PyTypeObject, tp_members)
    DEFINE_FIELD(PyTypeObject, tp_getset)
    DEFINE_FIELD(PyTypeObject, tp_base)
    DEFINE_FIELD(PyTypeObject, tp_dict)
    DEFINE_FIELD(PyTypeObject, tp_descr_get)
    DEFINE_FIELD(PyTypeObject, tp_descr_set)
    DEFINE_FIELD(PyTypeObject, tp_dictoffset)
    DEFINE_FIELD(PyTypeObject, tp_init)
    DEFINE_FIELD(PyTypeObject, tp_alloc)
    DEFINE_FIELD(PyTypeObject, tp_new)
    DEFINE_FIELD(PyTypeObject, tp_free)
    DEFINE_FIELD(PyTypeObject, tp_is_gc)
    DEFINE_FIELD(PyTypeObject, tp_bases)
    DEFINE_FIELD(PyTypeObject, tp_mro)
    DEFINE_FIELD(PyTypeObject, tp_cache)
    DEFINE_FIELD(PyTypeObject, tp_subclasses)
    DEFINE_FIELD(PyTypeObject, tp_weaklist)
    DEFINE_FIELD(PyTypeObject, tp_del)
    DEFINE_FIELD(PyTypeObject, tp_version_tag)
#ifdef COUNT_ALLOCS
    DEFINE_FIELD(PyTypeObject, tp_allocs)
    DEFINE_FIELD(PyTypeObject, tp_frees)
    DEFINE_FIELD(PyTypeObject, tp_maxalloc)
    DEFINE_FIELD(PyTypeObject, tp_prev)
    DEFINE_FIELD(PyTypeObject, tp_next)
#endif
};

template<> class TypeBuilder<PyNumberMethods, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct.PyNumberMethods"));
    }

    DEFINE_FIELD(PyNumberMethods, nb_add)
    DEFINE_FIELD(PyNumberMethods, nb_subtract)
    DEFINE_FIELD(PyNumberMethods, nb_multiply)
    DEFINE_FIELD(PyNumberMethods, nb_divide)
    DEFINE_FIELD(PyNumberMethods, nb_remainder)
    DEFINE_FIELD(PyNumberMethods, nb_divmod)
    DEFINE_FIELD(PyNumberMethods, nb_power)
    DEFINE_FIELD(PyNumberMethods, nb_negative)
    DEFINE_FIELD(PyNumberMethods, nb_positive)
    DEFINE_FIELD(PyNumberMethods, nb_absolute)
    DEFINE_FIELD(PyNumberMethods, nb_nonzero)
    DEFINE_FIELD(PyNumberMethods, nb_invert)
    DEFINE_FIELD(PyNumberMethods, nb_lshift)
    DEFINE_FIELD(PyNumberMethods, nb_rshift)
    DEFINE_FIELD(PyNumberMethods, nb_and)
    DEFINE_FIELD(PyNumberMethods, nb_xor)
    DEFINE_FIELD(PyNumberMethods, nb_or)
    DEFINE_FIELD(PyNumberMethods, nb_coerce)
    DEFINE_FIELD(PyNumberMethods, nb_int)
    DEFINE_FIELD(PyNumberMethods, nb_long)
    DEFINE_FIELD(PyNumberMethods, nb_float)
    DEFINE_FIELD(PyNumberMethods, nb_oct)
    DEFINE_FIELD(PyNumberMethods, nb_hex)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_add)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_subtract)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_multiply)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_divide)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_remainder)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_power)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_lshift)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_rshift)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_and)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_xor)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_or)
    DEFINE_FIELD(PyNumberMethods, nb_floor_divide)
    DEFINE_FIELD(PyNumberMethods, nb_true_divide)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_floor_divide)
    DEFINE_FIELD(PyNumberMethods, nb_inplace_true_divide)
    DEFINE_FIELD(PyNumberMethods, nb_index)
};

template<> class TypeBuilder<PySequenceMethods, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct.PySequenceMethods"));
    }

    DEFINE_FIELD(PySequenceMethods, sq_length)
    DEFINE_FIELD(PySequenceMethods, sq_concat)
    DEFINE_FIELD(PySequenceMethods, sq_repeat)
    DEFINE_FIELD(PySequenceMethods, sq_item)
    DEFINE_FIELD(PySequenceMethods, sq_slice)
    DEFINE_FIELD(PySequenceMethods, sq_ass_item)
    DEFINE_FIELD(PySequenceMethods, sq_ass_slice)
    DEFINE_FIELD(PySequenceMethods, sq_contains)
    DEFINE_FIELD(PySequenceMethods, sq_inplace_concat)
    DEFINE_FIELD(PySequenceMethods, sq_inplace_repeat)
};

template<> class TypeBuilder<PyMappingMethods, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct.PyMappingMethods"));
    }

    // No fields yet because we haven't needed them.
};

template<> class TypeBuilder<PyBufferProcs, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct.PyBufferProcs"));
    }

    // No fields yet because we haven't needed them.
};

template<> class TypeBuilder<PyCodeObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyCodeObject struct.
                "struct.PyCodeObject"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyCodeObject)
    DEFINE_FIELD(PyCodeObject, co_argcount)
    DEFINE_FIELD(PyCodeObject, co_nlocals)
    DEFINE_FIELD(PyCodeObject, co_stacksize)
    DEFINE_FIELD(PyCodeObject, co_flags)
    DEFINE_FIELD(PyCodeObject, co_code)
    DEFINE_FIELD(PyCodeObject, co_consts)
    DEFINE_FIELD(PyCodeObject, co_names)
    DEFINE_FIELD(PyCodeObject, co_varnames)
    DEFINE_FIELD(PyCodeObject, co_freevars)
    DEFINE_FIELD(PyCodeObject, co_cellvars)
    DEFINE_FIELD(PyCodeObject, co_filename)
    DEFINE_FIELD(PyCodeObject, co_name)
    DEFINE_FIELD(PyCodeObject, co_firstlineno)
    DEFINE_FIELD(PyCodeObject, co_lnotab)
    DEFINE_FIELD(PyCodeObject, co_zombieframe)
    DEFINE_FIELD(PyCodeObject, co_llvm_function)
    DEFINE_FIELD(PyCodeObject, co_native_function)
    DEFINE_FIELD(PyCodeObject, co_use_llvm)
    DEFINE_FIELD(PyCodeObject, co_optimization)
    DEFINE_FIELD(PyCodeObject, co_fatalbailcount)
    DEFINE_FIELD(PyCodeObject, co_hotness)
    DEFINE_FIELD(PyCodeObject, co_assumed_globals)
    DEFINE_FIELD(PyCodeObject, co_assumed_builtins)
};

template<> class TypeBuilder<PyTryBlock, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTryBlock struct.
                "struct.PyTryBlock"));
    }
    DEFINE_FIELD(PyTryBlock, b_type)
    DEFINE_FIELD(PyTryBlock, b_handler)
    DEFINE_FIELD(PyTryBlock, b_level)
};

template<> class TypeBuilder<PyFrameObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyFrameObject struct.
                "struct._frame"));
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyFrameObject)
    DEFINE_FIELD(PyFrameObject, ob_size)
    DEFINE_FIELD(PyFrameObject, f_back)
    DEFINE_FIELD(PyFrameObject, f_code)
    DEFINE_FIELD(PyFrameObject, f_builtins)
    DEFINE_FIELD(PyFrameObject, f_globals)
    DEFINE_FIELD(PyFrameObject, f_locals)
    DEFINE_FIELD(PyFrameObject, f_valuestack)
    DEFINE_FIELD(PyFrameObject, f_stacktop)
    DEFINE_FIELD(PyFrameObject, f_trace)
    DEFINE_FIELD(PyFrameObject, f_exc_type)
    DEFINE_FIELD(PyFrameObject, f_exc_value)
    DEFINE_FIELD(PyFrameObject, f_exc_traceback)
    DEFINE_FIELD(PyFrameObject, f_tstate)
    DEFINE_FIELD(PyFrameObject, f_lasti)
    DEFINE_FIELD(PyFrameObject, f_use_llvm)
    DEFINE_FIELD(PyFrameObject, f_lineno)
    DEFINE_FIELD(PyFrameObject, f_throwflag)
    DEFINE_FIELD(PyFrameObject, f_iblock)
    DEFINE_FIELD(PyFrameObject, f_bailed_from_llvm)
    DEFINE_FIELD(PyFrameObject, f_blockstack)
    DEFINE_FIELD(PyFrameObject, f_localsplus)
};

template<> class TypeBuilder<PyExcInfo, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyExcInfo struct
                // defined in llvm_inline_functions.c.
                "struct.PyExcInfo"));
    }

    // We use an enum here because PyExcInfo isn't defined in a header.
    enum Fields {
        FIELD_EXC,
        FIELD_VAL,
        FIELD_TB,
    };
};

template<> class TypeBuilder<PyThreadState, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        return cast<StructType>(
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyThreadState struct.
                "struct._ts"));
    }

    DEFINE_FIELD(PyThreadState, next)
    DEFINE_FIELD(PyThreadState, interp)
    DEFINE_FIELD(PyThreadState, frame)
    DEFINE_FIELD(PyThreadState, recursion_depth)
    DEFINE_FIELD(PyThreadState, tracing)
    DEFINE_FIELD(PyThreadState, use_tracing)
    DEFINE_FIELD(PyThreadState, c_profilefunc)
    DEFINE_FIELD(PyThreadState, c_tracefunc)
    DEFINE_FIELD(PyThreadState, c_profileobj)
    DEFINE_FIELD(PyThreadState, c_traceobj)
    DEFINE_FIELD(PyThreadState, curexc_type)
    DEFINE_FIELD(PyThreadState, curexc_value)
    DEFINE_FIELD(PyThreadState, curexc_traceback)
    DEFINE_FIELD(PyThreadState, exc_type)
    DEFINE_FIELD(PyThreadState, exc_value)
    DEFINE_FIELD(PyThreadState, exc_traceback)
    DEFINE_FIELD(PyThreadState, dict)
    DEFINE_FIELD(PyThreadState, tick_counter)
    DEFINE_FIELD(PyThreadState, gilstate_counter)
    DEFINE_FIELD(PyThreadState, async_exc)
    DEFINE_FIELD(PyThreadState, thread_id)
};

template<> class TypeBuilder<PyCFunctionObject, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyCFunctionObject
                                 // struct.
                                 "struct.PyCFunctionObject"));
        return result;
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyCFunctionObject)
    DEFINE_FIELD(PyCFunctionObject, m_ml)
    DEFINE_FIELD(PyCFunctionObject, m_self)
    DEFINE_FIELD(PyCFunctionObject, m_module)
};

template<> class TypeBuilder<PyMethodDef, false> {
public:
    static const StructType *get(llvm::LLVMContext &context) {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyMethodDef struct.
                                 "struct.PyMethodDef"));
        return result;
    }

    DEFINE_FIELD(PyMethodDef, ml_name)
    DEFINE_FIELD(PyMethodDef, ml_meth)
    DEFINE_FIELD(PyMethodDef, ml_flags)
    DEFINE_FIELD(PyMethodDef, ml_doc)
};

// We happen to have two functions with these types, so we must define type
// builder specializations for them.
template<typename R, typename A1, typename A2, typename A3, typename A4,
         typename A5, typename A6, typename A7, bool cross>
class TypeBuilder<R(A1, A2, A3, A4, A5, A6, A7), cross> {
public:
    static const FunctionType *get(llvm::LLVMContext &Context) {
        std::vector<const Type*> params;
        params.reserve(7);
        params.push_back(TypeBuilder<A1, cross>::get(Context));
        params.push_back(TypeBuilder<A2, cross>::get(Context));
        params.push_back(TypeBuilder<A3, cross>::get(Context));
        params.push_back(TypeBuilder<A4, cross>::get(Context));
        params.push_back(TypeBuilder<A5, cross>::get(Context));
        params.push_back(TypeBuilder<A6, cross>::get(Context));
        params.push_back(TypeBuilder<A7, cross>::get(Context));
        return FunctionType::get(TypeBuilder<R, cross>::get(Context),
                                 params, false);
    }
};

template<typename R, typename A1, typename A2, typename A3, typename A4,
         typename A5, typename A6, typename A7, typename A8, bool cross>
class TypeBuilder<R(A1, A2, A3, A4, A5, A6, A7, A8), cross> {
public:
    static const FunctionType *get(llvm::LLVMContext &Context) {
        std::vector<const Type*> params;
        params.reserve(8);
        params.push_back(TypeBuilder<A1, cross>::get(Context));
        params.push_back(TypeBuilder<A2, cross>::get(Context));
        params.push_back(TypeBuilder<A3, cross>::get(Context));
        params.push_back(TypeBuilder<A4, cross>::get(Context));
        params.push_back(TypeBuilder<A5, cross>::get(Context));
        params.push_back(TypeBuilder<A6, cross>::get(Context));
        params.push_back(TypeBuilder<A7, cross>::get(Context));
        params.push_back(TypeBuilder<A8, cross>::get(Context));
        return FunctionType::get(TypeBuilder<R, cross>::get(Context),
                                 params, false);
    }
};

#undef DEFINE_OBJECT_HEAD_FIELDS
#undef DEFINE_FIELD

}  // namespace llvm

namespace py {
typedef PyTypeBuilder<PyObject> ObjectTy;
typedef PyTypeBuilder<PyVarObject> VarObjectTy;
typedef PyTypeBuilder<PyStringObject> StringTy;
typedef PyTypeBuilder<PyIntObject> IntTy;
typedef PyTypeBuilder<PyTupleObject> TupleTy;
typedef PyTypeBuilder<PyListObject> ListTy;
typedef PyTypeBuilder<PyTypeObject> TypeTy;
typedef PyTypeBuilder<PyCodeObject> CodeTy;
typedef PyTypeBuilder<PyFrameObject> FrameTy;
typedef PyTypeBuilder<PyThreadState> ThreadStateTy;
typedef PyTypeBuilder<PyCFunctionObject> CFunctionTy;
typedef PyTypeBuilder<PyMethodDef> MethodDefTy;
}  // namespace py

#endif  // UTIL_PYTYPEBUILDER_H
