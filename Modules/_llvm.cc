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
#include "Util/RuntimeFeedback_fwd.h"

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
    PyObject *obj;
    PyCodeObject *code;
    long opt_level;
    struct PyGlobalLlvmData *global_llvm_data;

    if (!PyArg_ParseTuple(args, "O!l:compile",
                          &PyCode_Type, &obj, &opt_level))
        return NULL;

    if (opt_level < -1 || opt_level > Py_MAX_LLVM_OPT_LEVEL) {
        PyErr_SetString(PyExc_ValueError, "invalid optimization level");
        return NULL;
    }

    code = (PyCodeObject *)obj;
    if (code->co_llvm_function)
        _LlvmFunction_Dealloc(code->co_llvm_function);
    code->co_llvm_function = _PyCode_ToLlvmIr(code);
    if (code->co_llvm_function == NULL)
        return NULL;
    global_llvm_data = PyThreadState_GET()->interp->global_llvm_data;
    if (code->co_optimization < opt_level &&
        PyGlobalLlvmData_Optimize(global_llvm_data,
                                  code->co_llvm_function, opt_level) < 0) {
        PyErr_Format(PyExc_ValueError,
                     "Failed to optimize to level %ld", opt_level);
        _LlvmFunction_Dealloc(code->co_llvm_function);
        return NULL;
    }

    return _PyLlvmFunction_FromCodeObject((PyObject *)code);
}

PyDoc_STRVAR(llvm_clear_feedback_doc,
"clear_feedback(func)\n\
\n\
Clear the runtime feedback collected for the given function.");

static PyObject *
llvm_clear_feedback(PyObject *self, PyObject *obj)
{
    PyFunctionObject *func;
    if (PyFunction_Check(obj)) {
        func = (PyFunctionObject *)obj;
    }
    else if (PyMethod_Check(obj)) {
        func = (PyFunctionObject *)((PyMethodObject *)obj)->im_func;
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "cannot clear feedback for %.100s objects",
                     Py_TYPE(obj)->tp_name);
        return NULL;
    }

    PyCodeObject *code = (PyCodeObject *)func->func_code;
    if (code->co_runtime_feedback)
        PyFeedbackMap_Clear(code->co_runtime_feedback);

    Py_RETURN_NONE;
}

static struct PyMethodDef llvm_methods[] = {
    {"set_debug", (PyCFunction)llvm_setdebug, METH_O, setdebug_doc},
    {"compile", llvm_compile, METH_VARARGS, llvm_compile_doc},
    {"clear_feedback", (PyCFunction)llvm_clear_feedback, METH_O,
     llvm_clear_feedback_doc},
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
