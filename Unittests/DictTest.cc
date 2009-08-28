#include "Python.h"
#include "gtest/gtest.h"

class DictWatcherTest : public testing::Test {
protected:
    DictWatcherTest()
    {
        Py_NoSiteFlag = true;
        Py_Initialize();
        this->globals_ = PyDict_New();
        this->builtins_ = PyDict_New();
    }
    ~DictWatcherTest()
    {
        Py_DECREF(this->globals_);
        Py_DECREF(this->builtins_);
        Py_Finalize();
    }

    // Satisfying all the inputs to PyCode_New() is hard, so we fake it.
    // You will need to PyMem_DEL the result manually.
    PyCodeObject *FakeCodeObject()
    {
        PyCodeObject *code = PyMem_NEW(PyCodeObject, 1);
        assert(code != NULL);
        // We only initialize the fields related to dict watchers.
        code->co_assumed_globals = NULL;
        code->co_assumed_builtins = NULL;
        code->co_use_llvm = 0;
        code->co_fatalbailcount = 0;
        return code;
    }

    PyObject *globals_;
    PyObject *builtins_;
};

TEST_F(DictWatcherTest, AddWatcher)
{
    PyCodeObject *code = this->FakeCodeObject();

    _PyDict_AddWatcher(this->globals_, code);

    PyDictObject *dict = (PyDictObject *)this->globals_;
    EXPECT_EQ(dict->ma_watchers_used, 1);
    EXPECT_EQ(dict->ma_watchers_allocated, 64);

    // Drop the watcher to prevent the dict's dealloc from referencing freed
    // memory.
    _PyDict_DropWatcher(this->globals_, code);
    PyMem_DEL(code);
}

// _PyDict_DropWatcher() used to leave holes in the watcher array. This test
// verifies that DropWatcher() compacts the array.
TEST_F(DictWatcherTest, DropWatcherAddWatcherSequence)
{
    PyCodeObject *code1 = this->FakeCodeObject();
    PyCodeObject *code2 = this->FakeCodeObject();

    _PyDict_AddWatcher(this->globals_, code1);
    _PyDict_AddWatcher(this->globals_, code2);
    _PyDict_DropWatcher(this->globals_, code1);

    PyDictObject *dict = (PyDictObject *)this->globals_;
    EXPECT_EQ(dict->ma_watchers_used, 1);
    EXPECT_EQ(dict->ma_watchers[0], code2);

    _PyDict_DropWatcher(this->globals_, code2);
    PyMem_DEL(code1);
    PyMem_DEL(code2);
}

TEST_F(DictWatcherTest, DictDealloc)
{
    PyObject *globals = PyDict_New();
    PyObject *builtins = PyDict_New();
    PyCodeObject *code1 = this->FakeCodeObject();
    code1->co_use_llvm = 1;

    EXPECT_EQ(_PyCode_WatchGlobals(code1, globals, builtins), 0);
    Py_DECREF(globals);

    EXPECT_EQ(code1->co_use_llvm, 0);
    EXPECT_EQ(code1->co_assumed_globals, (PyObject *)NULL);
    EXPECT_EQ(code1->co_assumed_builtins, (PyObject *)NULL);

    PyDictObject *dict = (PyDictObject *)builtins;
    EXPECT_EQ(dict->ma_watchers_used, 0);

    Py_DECREF(builtins);
    PyMem_DEL(code1);
}

TEST_F(DictWatcherTest, NotifyWatcher)
{
    PyCodeObject *code1 = this->FakeCodeObject();
    code1->co_use_llvm = 1;

    EXPECT_EQ(_PyCode_WatchGlobals(code1, this->globals_, this->builtins_), 0);
    EXPECT_EQ(code1->co_use_llvm, 1);

    // This should notify the watchers.
    PyDict_SetItemString(this->globals_, "hello", Py_None);

    EXPECT_EQ(code1->co_use_llvm, 0);
    PyDictObject *globals_dict = (PyDictObject *)this->globals_;
    EXPECT_EQ(globals_dict->ma_watchers_used, 0);
    PyDictObject *builtins_dict = (PyDictObject *)this->builtins_;
    EXPECT_EQ(builtins_dict->ma_watchers_used, 0);

    EXPECT_EQ(code1->co_assumed_builtins, (PyObject *)NULL);
    EXPECT_EQ(code1->co_assumed_globals, (PyObject *)NULL);

    PyMem_DEL(code1);
}
