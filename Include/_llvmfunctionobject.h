/* _llvmfunction (llvm::Function wrapper) object interface */

#ifndef Py_LLVMFUNCTIONOBJECT_H
#define Py_LLVMFUNCTIONOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef WITH_LLVM
/* We store an _LlvmFunction in the PyCodeObject; these can be created without
   changing any Python reference counts, which would show up as reference leaks
   in a regrtest.py -R:: run. When the code object's co_llvm attribute is
   accessed, the _LlvmFunction is wrapped in a PyLlvmFuntionObject so it can
   exposed to Python code.
   This is all done to allow us to embed C++ data structures inside C
   structures. */

/* Internal wrapper for llvm::Function*s. */
typedef struct _LlvmFunction _LlvmFunction;

/* Really takes an llvm::Function*, and wraps it into a new'ed
   _LlvmFunction. */
PyAPI_FUNC(_LlvmFunction *) _LlvmFunction_New(
    void *llvm_function);

/* JIT compiles the llvm function.  Note that once the function has
   been translated to machine code once, it will never be
   re-translated even if the underlying IR function changes. */
typedef PyObject *(*PyEvalFrameFunction)(struct _frame *);
PyAPI_FUNC(PyEvalFrameFunction) _LlvmFunction_Jit(
    _LlvmFunction *llvm_function);

// Forwards to global_data->Optimize(llvm_function->lf_function, level);
PyAPI_FUNC(int) _LlvmFunction_Optimize(struct PyGlobalLlvmData *global_data,
                                       _LlvmFunction *llvm_function,
                                       int level);

PyAPI_FUNC(void) _LlvmFunction_Dealloc(_LlvmFunction *functionobj);


/*
_llvmfunction exposes an llvm::Function instance to Python code.  Only the
compiler can create these, but they also know how to prettyprint
themselves to LLVM assembly.
*/
typedef struct PyLlvmFunctionObject PyLlvmFunctionObject;

PyAPI_DATA(PyTypeObject) PyLlvmFunction_Type;

#define PyLlvmFunction_Check(op) (Py_TYPE(op) == &PyLlvmFunction_Type)

PyAPI_DATA(PyObject *) _PyLlvmFunction_FromCodeObject(PyObject *);
#endif  /* WITH_LLVM */

#ifdef __cplusplus
}
#endif
#endif /* !Py_LLVMFUNCTIONOBJECT_H */
