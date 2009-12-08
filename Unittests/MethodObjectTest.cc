#include "Python.h"
#include "gtest/gtest.h"

class PyCFunctionTest : public testing::Test {
protected:
    PyCFunctionTest()
    {
        Py_NoSiteFlag = true;
        Py_Initialize();
    }
    ~PyCFunctionTest()
    {
        Py_Finalize();
    }
};

// Most PyMethodDef structs are allocated globally, and the compiler takes care
// of zeroing out uninitialized fields. Not so with malloc(), which causes
// problems for some embedding applications that don't memset() their memory.
// This has been observed in applications that heap-allocate PyMethodDefs and
// then set METH_NOARGS, which previously was #defined to METH_ARG_RANGE.
// This is arguably a problem in the application, but if it works with Python
// 2.6, it needs to work with Unladen Swallow.
TEST_F(PyCFunctionTest, HeapAllocatedMethNoArgs)
{
    PyMethodDef *method_def = PyMem_New(PyMethodDef, 1);
    ASSERT_TRUE(method_def != NULL);
    method_def->ml_min_arity = 55;  // Dirty some memory.

    // From here on, pretend that method_def was free()d, then we did another
    // method_def = PyMem_New(PyMethodDef, 1); that returned the same memory,
    // but uninitialized. This is the behaviour observed in the wild.
    method_def->ml_flags = METH_NOARGS;                // Important.
    method_def->ml_meth = (PyCFunction)PyList_Append;  // Dummy.

    // Dummy objects; their type/contents are unimportant.
    PyObject *self = PyList_New(1);
    PyObject *module = PyList_New(1);
    ASSERT_TRUE(self != NULL);
    ASSERT_TRUE(module != NULL);

    // This PyCFunction_NewEx call used to trigger a PyErr_BadInternalCall.
    PyErr_Clear();
    PyObject *my_function = PyCFunction_NewEx(method_def, self, module);
    EXPECT_TRUE(my_function != NULL);
    EXPECT_FALSE(PyErr_Occurred());

    PyErr_Clear();
    PyMem_Free(method_def);
    Py_CLEAR(self);
    Py_CLEAR(module);
}
