// -*- C++ -*-
#ifndef UTIL_PYTYPEBUILDER_H
#define UTIL_PYTYPEBUILDER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python.h"
#include "code.h"
#include "frameobject.h"

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
//       PyTypeBuilder<PySomethingType>::get(),
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
    static Value *FIELD_NAME(IRBuilder<> &builder, Value *ptr) { \
        assert(ptr->getType() == PyTypeBuilder<TYPE*>::get() && \
               "*ptr must be of type " #TYPE); \
        static const unsigned int index = \
            _PyTypeBuilder_GetFieldIndexFromOffset( \
                PyTypeBuilder<TYPE>::get(), \
                offsetof(TYPE, FIELD_NAME)); \
        return builder.CreateStructGEP(ptr, index, #FIELD_NAME); \
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
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyObject struct.
                                 "struct._object"));
        return result;
    }

    DEFINE_OBJECT_HEAD_FIELDS(PyObject)
};

template<> class TypeBuilder<PyTupleObject, false> {
public:
    static const StructType *get() {
        static const StructType *const result = Create();
        return result;
    }

    DEFINE_FIELD(PyTupleObject, ob_size)
    DEFINE_FIELD(PyTupleObject, ob_item)

private:
    static const StructType *Create() {
        // Keep this in sync with tupleobject.h.
        return StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            PyTypeBuilder<PyObject>::get(),
            // From PyObject_VAR_HEAD
            PyTypeBuilder<ssize_t>::get(),
            // From PyTupleObject
            PyTypeBuilder<PyObject*[]>::get(),  // ob_item
            NULL);
    }
};

template<> class TypeBuilder<PyListObject, false> {
public:
    static const StructType *get() {
        static const StructType *const result = Create();
        return result;
    }

    DEFINE_FIELD(PyListObject, ob_size)
    DEFINE_FIELD(PyListObject, ob_item)
    DEFINE_FIELD(PyListObject, allocated)

private:
    static const StructType *Create() {
        // Keep this in sync with listobject.h.
        return StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            PyTypeBuilder<PyObject>::get(),
            // From PyObject_VAR_HEAD
            PyTypeBuilder<ssize_t>::get(),
            // From PyListObject
            PyTypeBuilder<PyObject**>::get(),  // ob_item
            PyTypeBuilder<Py_ssize_t>::get(),  // allocated
            NULL);
    }
};

template<> class TypeBuilder<PyTypeObject, false> {
public:
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyTypeObject struct.
                                 "struct._typeobject"));
        return result;
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

template<> class TypeBuilder<PyCodeObject, false> {
public:
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyCodeObject struct.
                                 "struct.PyCodeObject"));
        return result;
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
    DEFINE_FIELD(PyCodeObject, co_callcount)
};

template<> class TypeBuilder<PyTryBlock, false> {
public:
    static const StructType *get() {
        static const StructType *const result = Create();
        return result;
    }
    DEFINE_FIELD(PyTryBlock, b_type)
    DEFINE_FIELD(PyTryBlock, b_handler)
    DEFINE_FIELD(PyTryBlock, b_level)

private:
    static const StructType *Create() {
        const Type *int_type = PyTypeBuilder<int>::get();
        return StructType::get(
            // b_type, b_handler, b_level
            int_type, int_type, int_type, NULL);
    }
};

template<> class TypeBuilder<PyFrameObject, false> {
public:
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyFrameObject struct.
                                 "struct._frame"));
        return result;
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
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyExcInfo struct
                                 // defined in llvm_inline_functions.c.
                                 "struct.PyExcInfo"));
        return result;
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
    static const StructType *get() {
        static const StructType *const result =
            cast<StructType>(PyGlobalLlvmData::Get()->module()->getTypeByName(
                                 // Clang's name for the PyThreadState struct.
                                 "struct._ts"));
        return result;
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

#undef DEFINE_OBJECT_HEAD_FIELDS
#undef DEFINE_FIELD

}  // namespace llvm

namespace py {
typedef PyTypeBuilder<PyObject> ObjectTy;
typedef PyTypeBuilder<PyTupleObject> TupleTy;
typedef PyTypeBuilder<PyListObject> ListTy;
typedef PyTypeBuilder<PyTypeObject> TypeTy;
typedef PyTypeBuilder<PyCodeObject> CodeTy;
typedef PyTypeBuilder<PyFrameObject> FrameTy;
typedef PyTypeBuilder<PyThreadState> ThreadStateTy;
}  // namespace py

#endif  // UTIL_PYTYPEBUILDER_H
