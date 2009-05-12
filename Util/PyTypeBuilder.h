// -*- C++ -*-
#ifndef UTIL_PYTYPEBUILDER_H
#define UTIL_PYTYPEBUILDER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python.h"
#include "code.h"

#include "Python/global_llvm_data.h"

#include "llvm/Support/TypeBuilder.h"
#include "llvm/Module.h"

struct PyExcInfo;
typedef struct _frame PyFrameObject;

// llvm::TypeBuilder requires a boolean parameter specifying whether
// the type needs to be cross-compilable.  In Python, we don't need
// anything to be cross-compilable (the VM is by definition running on
// the platform it's generating code for), so we define PyTypeBuilder
// to hard-code that parameter to false.
template<typename T>
class PyTypeBuilder : public llvm::TypeBuilder<T, false> {};

// Enter the LLVM namespace in order to add specializations of
// llvm::TypeBuilder.
namespace llvm {

#ifdef Py_TRACE_REFS
#define OBJECT_HEAD_FIELDS \
        FIELD_NEXT, \
        FIELD_PREV, \
        FIELD_REFCNT, \
        FIELD_TYPE,
#else
#define OBJECT_HEAD_FIELDS \
        FIELD_REFCNT, \
        FIELD_TYPE,
#endif

template<> class TypeBuilder<PyObject, false> {
public:
    static const Type *get() {
        static const Type *const result =
            // Clang's name for the PyObject struct.
            PyGlobalLlvmData::Get()->module()->getTypeByName("struct._object");
        return result;
    }

    enum Fields {
        OBJECT_HEAD_FIELDS
    };
};

template<> class TypeBuilder<PyTupleObject, false> {
public:
    static const Type *get() {
        static const Type *const result = Create();
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_ITEM,
    };

private:
    static const Type *Create() {
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
    static const Type *get() {
        static const Type *const result = Create();
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_ITEM,
        FIELD_ALLOCATED,
    };

private:
    static const Type *Create() {
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
    static const Type *get() {
        static const Type *const result =
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyTypeObject struct.
                "struct._typeobject");
        return result;
    }

    enum Fields {
        OBJECT_HEAD_FIELDS
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

template<> class TypeBuilder<PyCodeObject, false> {
public:
    static const Type *get() {
        static const Type *const result = Create();
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

private:
    static const Type *Create() {
        // Keep this in sync with code.h.
        const Type *p_pyobject_type =
            PyTypeBuilder<PyObject*>::get();
        const Type *int_type = PyTypeBuilder<int>::get();
        return StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            PyTypeBuilder<PyObject>::get(),
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
            PyTypeBuilder<char*>::get(),  // co_tcode
            p_pyobject_type,  // co_filename
            p_pyobject_type,  // co_name
            int_type,  // co_firstlineno
            p_pyobject_type,  // co_lnotab
            PyTypeBuilder<char*>::get(),  //co_zombieframe
            p_pyobject_type,  // co_llvm_function
            NULL);
    }
};

template<> class TypeBuilder<PyTryBlock, false> {
public:
    static const Type *get() {
        static const Type *const result = Create();
        return result;
    }
    enum Fields {
        FIELD_TYPE,
        FIELD_HANDLER,
        FIELD_LEVEL,
    };

private:
    static const Type *Create() {
        const Type *int_type = PyTypeBuilder<int>::get();
        return StructType::get(
            // b_type, b_handler, b_level
            int_type, int_type, int_type, NULL);
    }
};

template<> class TypeBuilder<PyFrameObject, false> {
public:
    static const Type *get() {
        static const Type *const result = Create();
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
        FIELD_THROWFLAG,
        FIELD_IBLOCK,
        FIELD_BLOCKSTACK,
        FIELD_LOCALSPLUS,
    };

private:
    static const Type *Create() {
        // Keep this in sync with frameobject.h.
        const Type *p_pyobject_type = PyTypeBuilder<PyObject*>::get();
        const Type *int_type = PyTypeBuilder<int>::get();
        return StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            PyTypeBuilder<PyObject>::get(),
            // From PyObject_VAR_HEAD
            PyTypeBuilder<ssize_t>::get(),
            // From PyFrameObject
            p_pyobject_type,  // f_back
            PyTypeBuilder<PyCodeObject*>::get(),  // f_code
            p_pyobject_type,  // f_builtins
            p_pyobject_type,  // f_globals
            p_pyobject_type,  // f_locals
            PyTypeBuilder<PyObject**>::get(),  // f_valuestack
            PyTypeBuilder<PyObject**>::get(),  // f_stacktop
            p_pyobject_type,  // f_trace
            p_pyobject_type,  // f_exc_type
            p_pyobject_type,  // f_exc_value
            p_pyobject_type,  // f_exc_traceback
            // f_tstate; punt on the type:
            PyTypeBuilder<char*>::get(),
            int_type,  // f_lasti
            int_type,  // f_lineno
            PyTypeBuilder<unsigned char>::get(),  // f_throwflag
            PyTypeBuilder<unsigned char>::get(),  // f_iblock
            // f_blockstack:
            PyTypeBuilder<PyTryBlock[CO_MAXBLOCKS]>::get(),
            // f_localsplus, flexible array.
            PyTypeBuilder<PyObject*[]>::get(),
            NULL);
    }
};

template<> class TypeBuilder<PyExcInfo, false> {
public:
    static const Type *get() {
        static const Type *const result =
            PyGlobalLlvmData::Get()->module()->getTypeByName(
                // Clang's name for the PyExcInfo struct defined in
                // llvm_inline_functions.c.
                "struct.PyExcInfo");
        return result;
    }
    enum Fields {
        FIELD_EXC,
        FIELD_VAL,
        FIELD_TB,
    };
};

#undef OBJECT_HEAD_FIELDS

}  // namespace llvm

namespace py {
typedef PyTypeBuilder<PyObject> ObjectTy;
typedef PyTypeBuilder<PyTupleObject> TupleTy;
typedef PyTypeBuilder<PyListObject> ListTy;
typedef PyTypeBuilder<PyTypeObject> TypeTy;
typedef PyTypeBuilder<PyCodeObject> CodeTy;
typedef PyTypeBuilder<PyFrameObject> FrameTy;
}  // namespace py

#endif  // UTIL_PYTYPEBUILDER_H
