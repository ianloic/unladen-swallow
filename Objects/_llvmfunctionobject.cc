// Definition of _llvmfunction, the llvm::Function wrapper.

#include "Python.h"
#include "_llvmfunctionobject.h"

#include "_llvmmoduleobject.h"
#include "structmember.h"

#include "llvm/Function.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

static llvm::Function *
get_function(PyLlvmFunctionObject *obj)
{
    return (llvm::Function *)obj->the_function;
}

PyObject *
_PyLlvmFunction_FromModuleAndPtr(PyObject *module, void *llvm_function)
{
    if (!PyLlvmModule_Check(module)) {
        PyErr_Format(PyExc_TypeError,
                     "Expected _llvmmodule. Got %s",
                     Py_TYPE(module)->tp_name);
        return NULL;
    }
    PyLlvmFunctionObject *result =
        PyObject_NEW(PyLlvmFunctionObject, &PyLlvmFunction_Type);
    if (result == NULL) {
        return NULL;
    }
    Py_INCREF(module);
    result->module = module;
    result->the_function = (llvm::Function *)llvm_function;
    return (PyObject *)result;
}

PyDoc_STRVAR(llvmfunction_doc,
"_llvmfunction()\n\
\n\
A wrapper around an llvm::Function object. Can only be created from\n\
existing _llvmmodule objects.");

static void
llvmfunction_dealloc(PyLlvmFunctionObject *functionobj)
{
    Py_XDECREF(functionobj->module);
}

static PyObject *
llvmfunction_str(PyLlvmFunctionObject *functionobj)
{
    llvm::Function *const function = get_function(functionobj);
    if (function == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    std::string result;
    llvm::raw_string_ostream wrapper(result);
    function->print(wrapper);
    wrapper.flush();

    return PyString_FromStringAndSize(result.data(), result.size());
}

#define OFF(x) offsetof(PyLlvmFunctionObject, x)

static PyMemberDef llvmfunction_memberlist[] = {
    {"module",	T_OBJECT,	OFF(module),	READONLY},
    {NULL}  // Sentinel
};

// PyType_Ready is called on this in _llvmmoduleobject.cc:_PyLlvm_Init().
PyTypeObject PyLlvmFunction_Type = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
	"_llvmfunction",
	sizeof(PyLlvmFunctionObject),
	0,
	(destructor)llvmfunction_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0, 				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	(reprfunc)llvmfunction_str,	/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,		/* tp_flags */
	llvmfunction_doc,		/* tp_doc */
	0,				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	0,				/* tp_iter */
	0,				/* tp_iternext */
	0,				/* tp_methods */
	llvmfunction_memberlist,	/* tp_members */
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
