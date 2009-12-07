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

int __attribute__((always_inline))
_PyLlvm_WrapCFunctionCheck(PyObject *obj)
{
    return PyCFunction_Check(obj);
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

/* Keep this in sync with _PyObject_GetDictPtr.  We need it inlined in order
 * for constant propagation to work.
 */
PyObject ** __attribute__((always_inline))
_PyLlvm_Object_GetDictPtr(PyObject *obj, PyTypeObject *tp, long dictoffset)
{
    if (dictoffset == 0)
        return NULL;
    if (dictoffset < 0) {
        Py_ssize_t tsize;
        size_t size;

        tsize = ((PyVarObject *)obj)->ob_size;
        if (tsize < 0)
            tsize = -tsize;
        size = _PyObject_VAR_SIZE(tp, tsize);

        dictoffset += (long)size;
        /* TODO(rnk): Put these back once we get NDEBUG properly
         * defined during clang compilation for release builds.
         *assert(dictoffset > 0);
         *assert(dictoffset % SIZEOF_VOID_P == 0);
         */
    }
    return (PyObject **) ((char *)obj + dictoffset);
}

/* Keep this in sync with PyObject_GenericGetAttr.  The reason we take so many
 * extra arguments is to allow LLVM optimizers to notice that all of these
 * things are constant.  By passing them as parameters and always inlining this
 * function, we ensure that they will benefit from constant propagation.
 */
PyObject * __attribute__((always_inline))
_PyLlvm_Object_GenericGetAttr(PyObject *obj, PyTypeObject *type,
                              PyObject *name, long dictoffset, PyObject *descr,
                              descrgetfunc descr_get, char is_data_descr)
{
    PyObject *res = NULL;
    PyObject **dictptr;
    PyObject *dict;

    /* If it's a data descriptor, that has the most precedence, so we just call
     * the getter.  */
    if (is_data_descr) {
        return descr_get(descr, obj, (PyObject *)type);
    }

    dictptr = _PyLlvm_Object_GetDictPtr(obj, type, dictoffset);
    dict = dictptr == NULL ? NULL : *dictptr;

    /* If the object has a dict, and the attribute is in it, return it.  */
    if (dict != NULL) {
        Py_INCREF(dict);
        res = PyDict_GetItem(dict, name);
        Py_DECREF(dict);
        if (res != NULL) {
            Py_INCREF(res);
            return res;
        }
    }

    /* Otherwise, try calling the descriptor getter.  */
    if (descr_get != NULL) {
        return descr_get(descr, obj, (PyObject *)type);
    }

    /* If the descriptor has no getter, it's probably a vanilla PyObject
     * hanging off the class, in which case we just return it.  */
    if (descr != NULL) {
        Py_INCREF(descr);
        return descr;
    }

    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object has no attribute '%.400s'",
                 type->tp_name, PyString_AS_STRING(name));
    return NULL;
}

/* Keep this in sync with PyObject_GenericSetAttr.  */
int __attribute__((always_inline))
_PyLlvm_Object_GenericSetAttr(PyObject *obj, PyObject *value,
                              PyTypeObject *type, PyObject *name,
                              long dictoffset, PyObject *descr,
                              descrsetfunc descr_set, char is_data_descr)
{
    int res = -1;
    PyObject **dictptr;
    PyObject *dict;

    /* If it's a data descriptor, that has the most precedence, so we just call
     * the setter.  */
    if (is_data_descr) {
        return descr_set(descr, obj, value);
    }

    dictptr = _PyLlvm_Object_GetDictPtr(obj, type, dictoffset);

    /* If the object has a dict slot, store it in there.  */
    if (dictptr != NULL) {
        PyObject *dict = *dictptr;
        if (dict == NULL && value != NULL) {
            dict = PyDict_New();
            if (dict == NULL)
                return -1;
            *dictptr = dict;
        }
        if (dict != NULL) {
            Py_INCREF(dict);
            if (value == NULL)
                res = PyDict_DelItem(dict, name);
            else
                res = PyDict_SetItem(dict, name, value);
            if (res < 0 && PyErr_ExceptionMatches(PyExc_KeyError))
                PyErr_SetObject(PyExc_AttributeError, name);
            Py_DECREF(dict);
            return res;
        }
    }

    /* Otherwise, try calling the descriptor setter.  */
    if (descr_set != NULL) {
        return descr_set(descr, obj, value);
    }

    if (descr == NULL) {
        PyErr_Format(PyExc_AttributeError,
                     "'%.100s' object has no attribute '%.200s'",
                     type->tp_name, PyString_AS_STRING(name));
        return -1;
    }

    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object attribute '%.400s' is read-only",
                 type->tp_name, PyString_AS_STRING(name));
    return -1;
}

/* Define a global using PyTupleObject so we can look it up from
   TypeBuilder<PyTupleObject>. */
PyTupleObject *_dummy_TupleObject;
/* Ditto for PyListObject, */
PyListObject *_dummy_ListObject;
/* PyStringObject, */
PyStringObject *_dummy_StringObject;
/* PyUnicodeObject, */
PyUnicodeObject *_dummy_UnicodeObject;
/* PyCFunctionObject, */
PyCFunctionObject *_dummy_CFunctionObject;
/* PyIntObject, */
PyIntObject *_dummy_IntObject;
/* PyLongObject, */
PyLongObject *_dummy_LongObject;
/* PyFloatObject, */
PyFloatObject *_dummy_FloatObject;
/* PyComplexObject, */
PyComplexObject *_dummy_ComplexObject;
/* and PyVarObject. */
PyVarObject *_dummy_PyVarObject;


/* Expose PyEllipsis to ConstantMirror. */
PyObject* objectEllipsis() { return Py_Ellipsis; }
