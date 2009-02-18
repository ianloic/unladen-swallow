#include "Python.h"
#include "instructionsobject.h"

static inline unsigned int inst_to_int(PyInst inst) {
    return inst.opcode_or_arg << 1 | inst.is_arg;
}

static int
insts_compare(PyInstructionsObject *l, PyInstructionsObject *r) {
    Py_ssize_t min_size = Py_SIZE(l) < Py_SIZE(r) ? Py_SIZE(l) : Py_SIZE(r);
    Py_ssize_t i;
    for (i = 0; i < min_size; i++) {
        unsigned int l_inst = inst_to_int(l->inst[i]);
        unsigned int r_inst = inst_to_int(r->inst[i]);
        if (l_inst < r_inst) return -1;
        if (l_inst > r_inst) return 1;
    }
    if (Py_SIZE(l) < Py_SIZE(r)) return -1;
    if (Py_SIZE(l) > Py_SIZE(r)) return 1;
    return 0;
}

static long
insts_hash(PyInstructionsObject *vec) {
    long result = Py_SIZE(vec);
    Py_ssize_t i;
    for (i = 0; i < Py_SIZE(vec); i++) {
        result *= 1000003;
        result ^= inst_to_int(vec->inst[i]);
    }
    return result;
}

PyInstructionsObject *
_PyInstructions_New(Py_ssize_t num_instructions)
{
    return PyObject_NEW_VAR(PyInstructionsObject, &PyInstructions_Type,
                            num_instructions);
}

/* This only works when it's passed the only copy of an Instructions
   vector.  Otherwise, it would delete the data out from other any
   other users.  On error, decrefs *vec and sets it to NULL. */
int
_PyInstructions_Resize(PyInstructionsObject **vec, Py_ssize_t new_size) {
    PyInstructionsObject *const old = *vec;
    if (!PyInstructions_Check(old) || Py_REFCNT(old) != 1 || new_size < 0) {
        *vec = NULL;
        Py_DECREF(old);
        PyErr_BadInternalCall();
        return -1;
    }
    _Py_DEC_REFTOTAL;
    _Py_ForgetReference((PyObject *)*vec);
    *vec = (PyInstructionsObject *)PyObject_REALLOC(
        old, sizeof(PyInstructionsObject) + new_size * sizeof(PyInst));
    if (*vec == NULL) {
        PyObject_Del(old);
        PyErr_NoMemory();
        return -1;
    }
    _Py_NewReference((PyObject *)*vec);
    Py_SIZE(*vec) = new_size;
    return 0;
}

PyObject *
PyInstructions_FromSequence(PyObject *seq) {
    Py_ssize_t codelen;
    PyInstructionsObject *code = NULL;
    PyObject *item = NULL;
    Py_ssize_t i;
    if (!PySequence_Check(seq)) {
        PyErr_SetString(
			PyExc_ValueError,
			"code: instructions must be a sequence of integral types.");
        return NULL;
    }
    if ((codelen = PySequence_Size(seq)) < 0 ||
        (code = _PyInstructions_New(codelen)) == NULL) {
        goto error;
    }
    for (i = 0; i < codelen; i++) {
        unsigned int value;
        item = PySequence_GetItem(seq, i);
        if (item == NULL) {
            PyErr_Format(PyExc_ValueError,
                         "code: Failed to extract %zdth element from"
                         " 'code' sequence.", i);
            goto error;
        }
        value = PyNumber_AsSsize_t(item, PyExc_OverflowError);
        if (value == -1 && PyErr_Occurred()) {
            PyErr_Format(PyExc_ValueError,
                         "code: %zdth element wasn't integral between"
                         " 0 and 2^32.", i);
            goto error;
        }
        code->inst[i].is_arg = value & 1;
        /* Not much checking here. The user can crash us in
           plenty of ways even with all valid opcodes. */
        code->inst[i].opcode_or_arg = value >> 1;
        Py_CLEAR(item);
    }

    return (PyObject *)code;

 error:
    Py_XDECREF(item);
    Py_XDECREF(code);
    return NULL;
}

Py_ssize_t
insts_length(PyObject *ob)
{
    return Py_SIZE(ob);
}

PyObject *
insts_item(PyInstructionsObject *ob, Py_ssize_t i)
{
    if (i < 0 || i >= Py_SIZE(ob)) {
        PyErr_SetString(PyExc_IndexError, "instruction index out of range");
        return NULL;
    }
    unsigned int value = inst_to_int(ob->inst[i]);
    return PyInt_FromSize_t(value);
}

PyDoc_STRVAR(insts_doc,
"instructions stores a sequence of integers, each of which represents either"
" an operation or an operation's argument.");

static PySequenceMethods instructions_as_sequence = {
    (lenfunc)insts_length,	/* sq_length */
    0,				/* sq_concat */
    0,				/* sq_repeat */
    (ssizeargfunc)insts_item,	/* sq_item */
    0,				/* sq_slice */
    0,				/* sq_ass_item */
    0,				/* sq_ass_slice */
    0,				/* sq_contains */
    0,				/* sq_inplace_concat */
    0,				/* sq_inplace_repeat */
};

PyTypeObject PyInstructions_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "instructions",			/* tp_name */
    sizeof(PyInstructionsObject),	/* tp_basicsize */
    sizeof(PyInst),		/* tp_itemsize */
    0,			 	/* tp_dealloc */
    0,				/* tp_print */
    0, 				/* tp_getattr */
    0,				/* tp_setattr */
    (cmpfunc)insts_compare,	/* tp_compare */
    0,				/* tp_repr */
    0,				/* tp_as_number */
    &instructions_as_sequence,	/* tp_as_sequence */
    0,				/* tp_as_mapping */
    (hashfunc)insts_hash,	/* tp_hash */
    0,				/* tp_call */
    0,				/* tp_str */
    PyObject_GenericGetAttr,	/* tp_getattro */
    0,				/* tp_setattro */
    0,				/* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,		/* tp_flags */
    insts_doc,			/* tp_doc */
    0,				/* tp_traverse */
    0,				/* tp_clear */
    0,				/* tp_richcompare */
    0,				/* tp_weaklistoffset */
    0,				/* tp_iter */
    0,				/* tp_iternext */
    0,				/* tp_methods */
    0,				/* tp_members */
    0,				/* tp_getset */
    0,				/* tp_base */
    0,				/* tp_dict */
    0,				/* tp_descr_get */
    0,				/* tp_descr_set */
    0,				/* tp_dictoffset */
    0,				/* tp_init */
    0,				/* tp_alloc */
    0,				/* tp_new */
};
