/* _llvm module */

/*
This module provides a way to get at the minimal LLVM wrapper
types. It's not intended to be a full LLVM interface. For that, use
LLVM-py.
*/

#include "Python.h"
#include "_llvmfunctionobject.h"
#include "_llvmmoduleobject.h"

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

    llvm::DebugFlag = on;
    Py_RETURN_NONE;
}

static struct PyMethodDef llvm_methods[] = {
    {"set_debug",	(PyCFunction)llvm_setdebug,	METH_O, setdebug_doc},
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

    Py_INCREF(&PyLlvmModule_Type);
    if (PyModule_AddObject(module, "_module",
                           (PyObject *)&PyLlvmModule_Type))
        return;
    Py_INCREF(&PyLlvmFunction_Type);
    if (PyModule_AddObject(module, "_function",
                           (PyObject *)&PyLlvmFunction_Type))
        return;
}
