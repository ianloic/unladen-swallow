// -*- C++ -*-
#ifndef UTIL_CONSTANTMIRROR_H
#define UTIL_CONSTANTMIRROR_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "Python.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

struct PyGlobalLlvmData;

namespace llvm {
class Constant;
class ExecutionEngine;
class GlobalVariable;
class LLVMContext;
class StructType;
class TargetData;
class Type;
}

// This class helps mirror constants from the runtime into LLVM
// ConstantStructs and GlobalVariables.  For PyObject*s, it uses the
// ExecutionEngine's reverse mapping (getGlobalValueAtAddress()) to
// cache GlobalVariables we've already mirrored and holds a python
// reference to each one so that it doesn't wind up pointing to freed
// memory or holding an out-of-date value.  We don't cache
// non-PyObject*s.
class PyConstantMirror {
public:
    // Keeps a reference to llvm_data.
    // llvm_data->getExecutionEngine() has to be destroyed before this
    // object because it causes several CallbackVHs to self-destruct,
    // and they access this object.
    PyConstantMirror(PyGlobalLlvmData *llvm_data);

    // GetConstantFor(x) returns an llvm::Constant representing that
    // object.  It recursively mirrors objects the value points to
    // into GlobalVariables using GetGlobalVariableFor().
    llvm::Constant *GetConstantFor(PyObject *obj);
    llvm::Constant *GetConstantFor(PyCodeObject *obj);
    llvm::Constant *GetConstantFor(PyTypeObject *obj);
    llvm::Constant *GetConstantFor(PyTupleObject *obj);
    llvm::Constant *GetConstantFor(PyStringObject *obj);
    llvm::Constant *GetConstantFor(PyUnicodeObject *obj);
    llvm::Constant *GetConstantFor(PyIntObject *obj);
    llvm::Constant *GetConstantFor(PyLongObject *obj);
    llvm::Constant *GetConstantFor(PyFloatObject *obj);
    llvm::Constant *GetConstantFor(PyComplexObject *obj);
    llvm::Constant *GetConstantFor(PyNumberMethods *obj);
    llvm::Constant *GetConstantFor(PySequenceMethods *obj);
    llvm::Constant *GetConstantFor(PyMappingMethods *obj);
    llvm::Constant *GetConstantFor(PyBufferProcs *obj);

    // GetGlobalVariableFor(obj) returns an llvm::GlobalVariable
    // representing that object (or the null constant for obj==NULL),
    // tells the ExecutionEngine about the memory that backs it, and
    // attaches a Constant initializer to help optimizations.  It also
    // increfs the object and arranges (with a CallbackVH) to decref
    // it when the GlobalVariable is destroyed.  We use the
    // ExecutionEngine's address->GlobalValue mapping to avoid
    // creating two GlobalVariables for the same object.
    llvm::Constant *GetGlobalVariableFor(PyObject *obj);

    // Create an LLVM global variable that is backed by the given function
    // pointer. This currently only works for METH_FIXED functions.
    llvm::Constant *GetGlobalForCFunction(PyCFunction cfunc_ptr,
                                          int arity,
                                          const llvm::StringRef &name);

private:
    friend struct PyGlobalLlvmData;

    // GetGlobalVariableForOwned() increfs 'owner' in order to keep
    // 'ptr' alive until the GlobalVariable is destoryed.
    template<typename T>
    llvm::Constant *GetGlobalVariableForOwned(T *ptr, PyObject *owner);

    llvm::Constant *ConstantFromMemory(const llvm::Type *type,
                                       const void *memory) const;

    llvm::StructType *ResizeVarObjectType(const llvm::StructType *type,
                                          unsigned dynamic_size) const;

    // Attempted shorthand for getting the context.
    llvm::LLVMContext &context() const;

    class RemovePyObjFromGlobalMappingsVH;

    PyGlobalLlvmData &llvm_data_;
    llvm::ExecutionEngine &engine_;
    const llvm::TargetData &target_data_;
    /* When this is true, the RemovePyObjFromGlobalMappingsVH doesn't
       decref PyObjects since running their finalizers could run
       python code after all modules and the thread state are gone. */
    bool python_shutting_down_;
};

#endif  // UTIL_CONSTANTMIRROR_H
