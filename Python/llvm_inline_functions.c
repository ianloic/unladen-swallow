/* This file defines several functions that we want to be able to
   inline into the LLVM IR we generate.  We compile it with clang and
   llc to produce a C++ function that inserts these definitions into a
   module. */

#include "Python.h"
#include "frameobject.h"
#include "opcode.h"

int
_PyLlvm_WrapIntCheck(PyObject *obj)
{
    return PyInt_Check(obj);
}

void
_PyLlvm_WrapIncref(PyObject *obj)
{
    Py_INCREF(obj);
}

void
_PyLlvm_WrapDecref(PyObject *obj)
{
    Py_DECREF(obj);
}

void
_PyLlvm_WrapXDecref(PyObject *obj)
{
    Py_XDECREF(obj);
}

int
_PyLlvm_WrapIsExceptionOrString(PyObject *obj)
{
    return PyExceptionClass_Check(obj) || PyString_Check(obj);
}

/* This type collects the set of three values that constitute an
   exception.  So far, it's only used for
   _PyLlvm_WrapEnterExceptOrFinally().  If we use it for more, we
   should move it to pyerrors.h. */
struct PyExcInfo {
    PyObject *exc;
    PyObject *val;
    PyObject *tb;
};

/* Copied from the SETUP_FINALLY && WHY_EXCEPTION block in
   fast_block_end in PyEval_EvalFrame(). */
void
_PyLlvm_WrapEnterExceptOrFinally(struct PyExcInfo *exc_info, int block_type)
{
    PyThreadState *tstate = PyThreadState_GET();
    PyErr_Fetch(&exc_info->exc, &exc_info->val, &exc_info->tb);
    if (exc_info->val == NULL) {
        exc_info->val = Py_None;
        Py_INCREF(exc_info->val);
    }
    /* Make the raw exception data
       available to the handler,
       so a program can emulate the
       Python main loop.  Don't do
       this for 'finally'. */
    if (block_type == SETUP_EXCEPT) {
        PyErr_NormalizeException(
            &exc_info->exc, &exc_info->val, &exc_info->tb);
        _PyEval_SetExcInfo(tstate,
                           exc_info->exc, exc_info->val, exc_info->tb);
    }
    if (exc_info->tb == NULL) {
        Py_INCREF(Py_None);
        exc_info->tb = Py_None;
    }
    /* Within the except or finally block,
       PyErr_Occurred() should be false.
       END_FINALLY will restore the
       exception if necessary. */
    PyErr_Clear();
}

PyThreadState *
_PyLlvm_WrapPyThreadState_GET()
{
    return PyThreadState_GET();
}
