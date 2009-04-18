/* _llvmfunction (llvm::Function wrapper) object interface */

#ifndef Py_LLVMFUNCTIONOBJECT_H
#define Py_LLVMFUNCTIONOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

/*
_llvmfunction represents an llvm::Function instance.  Only the
compiler can create these, but they also know how to prettyprint
themselves to LLVM assembly.
*/

typedef struct PyLlvmFunctionObject PyLlvmFunctionObject;

PyAPI_DATA(PyTypeObject) PyLlvmFunction_Type;

#define PyLlvmFunction_Check(op) (Py_TYPE(op) == &PyLlvmFunction_Type)

/* llvm_function must be an llvm::Function. */
PyAPI_FUNC(PyObject *) _PyLlvmFunction_FromPtr(void *llvm_function);

/* Returns the llvm::Function* this PyLlvmFunctionObject wraps. */
PyAPI_FUNC(void *) _PyLlvmFunction_GetFunction(
    PyLlvmFunctionObject *llvm_function);

/* JIT compiles and executes the llvm function. */
PyAPI_FUNC(PyObject *) _PyLlvmFunction_Eval(
    PyLlvmFunctionObject *llvm_function, struct _frame *frame);

#ifdef __cplusplus
}
#endif
#endif /* !Py_LLVMFUNCTIONOBJECT_H */
