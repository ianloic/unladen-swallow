/* _llvm module */

/*
This module provides a way to get at the minimal LLVM wrapper
types. It's not intended to be a full LLVM interface. For that, use
LLVM-py.
*/

#include "Python.h"
#include "_llvmfunctionobject.h"
#include "llvm_compile.h"
#include "Python/global_llvm_data_fwd.h"

#include "llvm/Support/Debug.h"

PyDoc_STRVAR(llvm_module_doc,
"Defines thin wrappers around fundamental LLVM types.");

PyDoc_STRVAR(setdebug_doc,
             "set_debug(bool).  Sets LLVM debug output on or off.");
static PyObject *
llvm_setdebug(PyObject *self, PyObject *on_obj)
{
    int on = PyObject_IsTrue(on_obj);
    if (on == -1)  // Error.
        return NULL;

#ifdef NDEBUG
    if (on) {
        PyErr_SetString(PyExc_ValueError, "llvm debugging not available");
        return NULL;
    }
#else
    llvm::DebugFlag = on;
#endif
    Py_RETURN_NONE;
}

PyDoc_STRVAR(llvm_compile_doc,
"compile(code, optimization_level) -> llvm_function\n\
\n\
Compile a code object to an llvm_function object at the given\n\
optimization level.");

static PyObject *
llvm_compile(PyObject *self, PyObject *args)
{
    PyObject *code;
    long opt_level;
    long i;
    struct PyGlobalLlvmData *global_llvm_data;
    PyObject *llvm_function;

    if (!PyArg_ParseTuple(args, "O!l:compile",
                          &PyCode_Type, &code, &opt_level))
        return NULL;

    if (opt_level < -1 || opt_level > 2) {
        PyErr_SetString(PyExc_ValueError, "invalid optimization level");
        return NULL;
    }

    llvm_function = _PyCode_To_Llvm((PyCodeObject *)code);
    if (llvm_function == NULL)
        return NULL;
    global_llvm_data = PyThreadState_GET()->interp->global_llvm_data;
    for (i = 0; i <= opt_level; i++) {
        if (PyGlobalLlvmData_Optimize(global_llvm_data,
                                      llvm_function, i) < 0) {
            PyErr_Format(PyExc_ValueError,
                         "Failed to optimize to level %ld", i);
            Py_DECREF(llvm_function);
            return NULL;
        }
    }
    return llvm_function;
}

static struct PyMethodDef llvm_methods[] = {
    {"set_debug",	(PyCFunction)llvm_setdebug,	METH_O, setdebug_doc},
    {"compile",		llvm_compile,	METH_VARARGS, llvm_compile_doc},
    { NULL, NULL }
};

PyMODINIT_FUNC
init_llvm(void)
{
    PyObject *module;

    /* Create the module and add the functions */
    module = Py_InitModule3("_llvm", llvm_methods, llvm_module_doc);
    if (module == NULL)
        return;

    Py_INCREF(&PyLlvmFunction_Type);
    if (PyModule_AddObject(module, "_function",
                           (PyObject *)&PyLlvmFunction_Type))
        return;
}
