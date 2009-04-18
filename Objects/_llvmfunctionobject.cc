// Definition of _llvmfunction, the llvm::Function wrapper.

#include "Python.h"
#include "_llvmfunctionobject.h"

#include "frameobject.h"
#include "structmember.h"
#include "Python/global_llvm_data.h"

#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

struct PyLlvmFunctionObject {
public:
    PyObject_HEAD
    // TODO(jyasskin): Make this a WeakVH when we import llvm's
    // Support/ValueHandle.h.
    llvm::Function *the_function;
};

static llvm::Function *
get_function(PyLlvmFunctionObject *obj)
{
    return (llvm::Function *)obj->the_function;
}

PyObject *
_PyLlvmFunction_FromPtr(void *llvm_function)
{
    PyLlvmFunctionObject *result =
        PyObject_NEW(PyLlvmFunctionObject, &PyLlvmFunction_Type);
    if (result == NULL) {
        return NULL;
    }
    llvm::Function *function = (llvm::Function *)llvm_function;
    result->the_function = function;

    // Make sure the function survives global optimizations.
    function->setLinkage(llvm::GlobalValue::ExternalLinkage);

    return (PyObject *)result;
}

void *
_PyLlvmFunction_GetFunction(PyLlvmFunctionObject *llvm_function)
{
    return llvm_function->the_function;
}

PyObject *
_PyLlvmFunction_Eval(PyLlvmFunctionObject *function_obj, PyFrameObject *frame)
{
    if (!PyLlvmFunction_Check(function_obj)) {
        PyErr_Format(PyExc_TypeError,
                     "Expected PyLlvmFunctionObject; got %s",
                     Py_TYPE(function_obj)->tp_name);
        return NULL;
    }
    llvm::Function *function = function_obj->the_function;
    PyGlobalLlvmData *global_llvm_data =
        PyThreadState_GET()->interp->global_llvm_data;
    typedef PyObject *(*NativeFunction)(PyFrameObject *);
    llvm::ExecutionEngine *engine = global_llvm_data->getExecutionEngine();
    NativeFunction native =
        (NativeFunction)engine->getPointerToFunction(function);
    return native(frame);
}

PyDoc_STRVAR(llvmfunction_doc,
"_llvmfunction()\n\
\n\
A wrapper around an llvm::Function object. Can only be created from\n\
existing _llvmmodule objects.");

static void
llvmfunction_dealloc(PyLlvmFunctionObject *functionobj)
{
    llvm::Function *function = functionobj->the_function;
    // Allow global optimizations to destroy the function.
    function->setLinkage(llvm::GlobalValue::InternalLinkage);
    if (function->use_empty()) {
        // Delete the function if it's already unused.
        function->eraseFromParent();
    }
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

static PyObject *
func_get_module(PyLlvmFunctionObject *op)
{
    llvm::Module *module = op->the_function->getParent();
    if (module == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    std::string result;
    llvm::raw_string_ostream wrapper(result);
    module->print(wrapper, NULL /* No extra annotations in the output */);
    wrapper.flush();

    return PyString_FromStringAndSize(result.data(),
                                      result.size());
}

static PyGetSetDef llvmfunction_getsetlist[] = {
    {"module", (getter)func_get_module, NULL},
    NULL
};

// PyType_Ready is called on this in global_llvm_data.cc:_PyLlvm_Init().
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
	0,				/* tp_members */
	llvmfunction_getsetlist,	/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	0,				/* tp_init */
	0,				/* tp_alloc */
	0,				/* tp_new */
};
