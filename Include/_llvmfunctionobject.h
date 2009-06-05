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

/* JIT compiles the llvm function.  Note that once the function has
   been translated to machine code once, it will never be
   re-translated even if the underlying IR function changes. */
typedef PyObject *(*PyEvalFrameFunction)(struct _frame *);
PyAPI_FUNC(PyEvalFrameFunction) _PyLlvmFunction_Jit(
    PyLlvmFunctionObject *llvm_function);

#ifdef __cplusplus
}
#endif
#endif /* !Py_LLVMFUNCTIONOBJECT_H */
