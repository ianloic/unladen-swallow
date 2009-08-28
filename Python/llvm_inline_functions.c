/* This file defines several functions that we want to be able to
   inline into the LLVM IR we generate.  We compile it with clang and
   llc to produce a C++ function that inserts these definitions into a
   module. 

   PyGlobalLlvmData::InstallInitialModule() will apply LLVM's fastcc calling
   convention to all functions defined in this module that start with
   _PyLlvm_Fast.
*/

#include "Python.h"
#include "frameobject.h"
#include "longintrepr.h"
#include "opcode.h"

#include "Util/EventTimer.h"

int __attribute__((always_inline))
_PyLlvm_WrapIntCheck(PyObject *obj)
{
    return PyInt_Check(obj);
}

void __attribute__((always_inline))
_PyLlvm_WrapIncref(PyObject *obj)
{
    Py_INCREF(obj);
}

void __attribute__((always_inline))
_PyLlvm_WrapDecref(PyObject *obj)
{
    Py_DECREF(obj);
}

void __attribute__((always_inline))
_PyLlvm_WrapXDecref(PyObject *obj)
{
    Py_XDECREF(obj);
}

int __attribute__((always_inline))
_PyLlvm_WrapIsExceptionOrString(PyObject *obj)
{
    return PyExceptionClass_Check(obj) || PyString_Check(obj);
}


/* TODO(collinwinter): move this special-casing into a common function that
   we can share with eval.cc. */
int
_PyLlvm_FastUnpackIterable(PyObject *iter, int argcount,
                           PyObject **stack_pointer) {
    /* TODO(collinwinter): we could reduce the amount of generated code by
       using &PyTuple_GET_ITEM(iter, 0) and &PyList_GET_ITEM(iter, 0) to find
       the beginning of the items lists, and then having a single loop to copy
       that into the stack. */
    if (PyTuple_Check(iter) && PyTuple_GET_SIZE(iter) == argcount) {
        int i;
        for (i = 0; i < argcount; i++) {
            PyObject *item = PyTuple_GET_ITEM(iter, i);
            Py_INCREF(item);
            *--stack_pointer = item;
        }
        return 0;
    }
    else if (PyList_Check(iter) && PyList_GET_SIZE(iter) == argcount) {
        int i;
        for (i = 0; i < argcount; i++) {
            PyObject *item = PyList_GET_ITEM(iter, i);
            Py_INCREF(item);
            *--stack_pointer = item;
        }
        return 0;
    }
    else {
        return _PyEval_UnpackIterable(iter, argcount, stack_pointer);
    }
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
_PyLlvm_FastEnterExceptOrFinally(struct PyExcInfo *exc_info, int block_type)
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
        PY_LOG_TSC_EVENT(EXCEPT_CATCH_LLVM);
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

int __attribute__((always_inline))
_PyLlvm_DecAndCheckPyTicker(PyThreadState *tstate)
{
    if (--_Py_Ticker < 0) {
        return _PyEval_HandlePyTickerExpired(tstate);
    }
    return 0;
}

PyThreadState * __attribute__((always_inline))
_PyLlvm_WrapPyThreadState_GET()
{
    return PyThreadState_GET();
}

/* Keep these in sync with the definitions of PyFrame_Block{Setup,Pop}
   in frameobject.c. */
void __attribute__((always_inline))
_PyLlvm_Frame_BlockSetup(PyTryBlock *blocks, char *num_blocks,
                         int type, int handler, int level)
{
    PyTryBlock *b;
    if (*num_blocks >= CO_MAXBLOCKS)
        Py_FatalError("XXX block stack overflow");
    b = &blocks[*num_blocks];
    b->b_type = type;
    b->b_level = level;
    b->b_handler = handler;
    ++*num_blocks;
}

PyTryBlock * __attribute__((always_inline))
_PyLlvm_Frame_BlockPop(PyTryBlock *blocks, char *num_blocks)
{
    PyTryBlock *b;
    if (*num_blocks <= 0)
        Py_FatalError("XXX block stack underflow");
    --*num_blocks;
    b = &blocks[*num_blocks];
    return b;
}

/* Define a global using PyTupleObject so we can look it up from
   TypeBuilder<PyTupleObject>. */
PyTupleObject *_dummy_TupleObject;
/* Ditto for PyStringObject, */
PyStringObject *_dummy_StringObject;
/* PyUnicodeObject, */
PyUnicodeObject *_dummy_UnicodeObject;
/* PyIntObject, */
PyIntObject *_dummy_IntObject;
/* PyLongObject, */
PyLongObject *_dummy_LongObject;
/* PyFloatObject, */
PyFloatObject *_dummy_FloatObject;
/* and PyComplexObject. */
PyComplexObject *_dummy_ComplexObject;

/* Expose PyEllipsis to ConstantMirror. */
PyObject* objectEllipsis() { return Py_Ellipsis; }
