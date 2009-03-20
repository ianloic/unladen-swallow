/* _llvmmodule (llvm::Module wrapper) object interface */

#ifndef Py_LLVMMODULEOBJECT_H
#define Py_LLVMMODULEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

/*
_llvmmodule represents an llvm::Module instance.  It can load one of
these from a bitcode string or prettyprint itself as LLVM assembly.
*/

typedef struct {
    PyObject_HEAD
    /* This has type llvm::Module*, but we can't say that in a C header. */
    void *the_module;
} PyLlvmModuleObject;

PyAPI_DATA(PyTypeObject) PyLlvmModule_Type;

#define PyLlvmModule_Check(op) (Py_TYPE(op) == &PyLlvmModule_Type)

/* Loads an llvm::Module object from a bitcode string. Expects two
   string objects. */
PyAPI_FUNC(PyObject *) PyLlvmModule_FromBitcode(PyObject *name,
                                                PyObject *bitcode);

/* Initializes all LLVM wrapper types, not just the module wrapper. */
PyAPI_FUNC(int) _PyLlvm_Init(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_LLVMMODULEOBJECT_H */
