// Definition of _llvmmodule, the llvm::Module wrapper.

#include "Python.h"
#include "_llvmmoduleobject.h"

#include "Python/global_llvm_data.h"
#include "_llvmfunctionobject.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <sstream>

static llvm::Module *
get_module(PyLlvmModuleObject *obj)
{
    return (llvm::Module *)obj->the_module;
}

PyObject *
PyLlvmModule_New(const char *module_name)
{
    PyLlvmModuleObject *result =
        PyObject_NEW(PyLlvmModuleObject, &PyLlvmModule_Type);
    if (result == NULL) {
        return NULL;
    }
    llvm::Module *module = new llvm::Module(module_name);
    result->the_module = module;
    llvm::ModuleProvider *provider = new llvm::ExistingModuleProvider(module);
    result->module_provider = provider;
    PyInterpreterState *interp = PyThreadState_Get()->interp;
    // Hands ownership of the module and provider to the
    // ExecutionEngine.  We'll tell the ExecutionEngine to delete them
    // when our refcount goes to zero.
    interp->global_llvm_data->getExecutionEngine()->addModuleProvider(provider);
    return (PyObject *)result;
}

PyObject *
PyLlvmModule_FromBitcode(PyObject *module_name_obj, PyObject *bitcode_str)
{
    if (!PyString_Check(module_name_obj)) {
        PyErr_Format(PyExc_TypeError,
                     "Param 1: expected string containing module name. Got %s",
                     Py_TYPE(module_name_obj)->tp_name);
        return NULL;
    }
    if (!PyString_Check(bitcode_str)) {
        PyErr_Format(PyExc_TypeError,
                     "Param 2: expected string containing LLVM bitcode. Got %s",
                     Py_TYPE(bitcode_str)->tp_name);
        return NULL;
    }
    const char *bitcode_data = PyString_AS_STRING(bitcode_str);
    Py_ssize_t bitcode_size = PyString_GET_SIZE(bitcode_str);
    const std::auto_ptr<llvm::MemoryBuffer> buffer(
        llvm::MemoryBuffer::getMemBuffer(
            bitcode_data, bitcode_data + bitcode_size,
            // This parameter provides the name of the module.
            PyString_AS_STRING(module_name_obj)));

    std::string error;
    std::auto_ptr<llvm::Module> module(
        llvm::ParseBitcodeFile(buffer.get(), &error));
    if (module.get() == NULL) {
        if (!error.empty()) {
            PyErr_Format(PyExc_ValueError,
                         "%s", error.c_str());
        }
        else {
            PyErr_Format(PyExc_ValueError,
                         "bitcode didn't read correctly");
        }
        return NULL;
    }

    PyLlvmModuleObject *result =
        PyObject_NEW(PyLlvmModuleObject, &PyLlvmModule_Type);
    if (result == NULL) {
        return NULL;
    }
    result->the_module = module.release();
    return (PyObject *)result;
}

PyDoc_STRVAR(llvmmodule_from_bitcode_doc,
"_llvmmodule.from_bitcode(module_name, bitcode_str)\n\
\n\
Create an _llvmmodule object from an LLVM bitcode string.\n\
llvm-dis uses the input filename as the module name.");

static PyObject *
llvmmodule_from_bitcode(PyTypeObject *type, PyObject *args)
{
    PyObject *module_name;
    PyObject *bitcode_str;
    if (!PyArg_ParseTuple(args, "SS", &module_name, &bitcode_str))
        return NULL;

    return PyLlvmModule_FromBitcode(module_name, bitcode_str);
}

static void
llvmmodule_dealloc(PyLlvmModuleObject *moduleobj)
{
    PyInterpreterState *interp = PyThreadState_Get()->interp;
    interp->global_llvm_data->getExecutionEngine()->deleteModuleProvider(
        (llvm::ModuleProvider *)moduleobj->module_provider);
}

static PyObject *
llvmmodule_str(PyLlvmModuleObject *moduleobj)
{
    llvm::Module *const module = get_module(moduleobj);
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

struct llvmmodule_functioniter {
    PyObject_HEAD
    PyObject *module;  // Holds a reference; not used.
    llvm::Module::iterator current;
    llvm::Module::iterator end;
};

extern PyTypeObject PyLlvmModule_FunctionIter_Type;

static PyObject *
llvmmodule_functioniter_new(PyLlvmModuleObject *moduleobj)
{
    llvmmodule_functioniter *fi =
        PyObject_New(llvmmodule_functioniter, &PyLlvmModule_FunctionIter_Type);
    if (fi == NULL)
        return NULL;

    // Call the constructor explicitly, to initialize the iterators.
    new(fi) llvmmodule_functioniter();

    Py_INCREF(moduleobj);
    fi->module = (PyObject *)moduleobj;
    llvm::Module *module = get_module(moduleobj);
    fi->current = module->begin();
    fi->end = module->end();
    return (PyObject *)fi;
}

static PyObject *
llvmmodule_functioniter_iternext(llvmmodule_functioniter *fi)
{
    if (fi->module == NULL || fi->current == fi->end) {
        // Finished. This will raise StopIteration.
        return NULL;
    }
    llvm::Function *next_func = fi->current;
    PyObject *result = _PyLlvmFunction_FromModuleAndPtr(fi->module, next_func);
    if (result == NULL)
        return NULL;
    ++fi->current;  // Only change state on success.
    return result;
}

static void
llvmmodule_functioniter_dealloc(llvmmodule_functioniter *fi)
{
	Py_XDECREF(fi->module);
        fi->~llvmmodule_functioniter();
	PyObject_Del(fi);
}

PyTypeObject PyLlvmModule_FunctionIter_Type = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
	"llvmmodule-functioniterator",		/* tp_name */
	sizeof(llvmmodule_functioniter),	/* tp_basicsize */
	0,					/* tp_itemsize */
	/* methods */
	(destructor)llvmmodule_functioniter_dealloc,	/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	0,					/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,			/* tp_flags */
 	0,					/* tp_doc */
 	0,					/* tp_traverse */
 	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	PyObject_SelfIter,			/* tp_iter */
	(iternextfunc)llvmmodule_functioniter_iternext,	/* tp_iternext */
	0,					/* tp_methods */
	0,
};

PyDoc_STRVAR(llvmmodule_functions__doc__,
"M.functions() -> an iterator over the functions defined in M");

static PyMethodDef llvmmodule_methods[] = {
	{"functions", (PyCFunction)llvmmodule_functioniter_new,	METH_NOARGS,
	 llvmmodule_functions__doc__},
	{"from_bitcode", (PyCFunction)llvmmodule_from_bitcode,
	 METH_VARARGS|METH_STATIC, llvmmodule_from_bitcode_doc},
	{NULL,		NULL}	/* sentinel */
};

PyDoc_STRVAR(llvmmodule_doc,
"_llvmmodule(bitcode_str)\n\
\n\
Create an _llvmmodule object from an LLVM bitcode string.");

PyTypeObject PyLlvmModule_Type = {
	PyVarObject_HEAD_INIT(&PyType_Type, 0)
	"_llvmmodule",
	sizeof(PyLlvmModuleObject),
	0,
	(destructor)llvmmodule_dealloc,	/* tp_dealloc */
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
	(reprfunc)llvmmodule_str,	/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,		/* tp_flags */
	llvmmodule_doc,			/* tp_doc */
	0,				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	0,				/* tp_iter */
	0,				/* tp_iternext */
	llvmmodule_methods,		/* tp_methods */
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

int
_PyLlvm_Init()
{
    if (PyType_Ready(&PyLlvmModule_Type) < 0)
        return 0;
    if (PyType_Ready(&PyLlvmFunction_Type) < 0)
        return 0;

    return 1;
}
