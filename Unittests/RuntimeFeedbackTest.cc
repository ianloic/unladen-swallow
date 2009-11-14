#include "Python.h"
#include "Util/RuntimeFeedback.h"
#include "llvm/ADT/SmallVector.h"
#include "gtest/gtest.h"

using llvm::SmallVector;

class PyRuntimeFeedbackTest : public testing::Test {
protected:
    PyRuntimeFeedbackTest()
    {
        Py_NoSiteFlag = true;
        Py_Initialize();
        this->an_int_ = PyInt_FromLong(3);
        this->a_list_ = PyList_New(0);
        this->a_tuple_ = PyTuple_New(0);
        this->a_dict_ = PyDict_New();
        this->a_string_ = PyString_FromString("Hello");
        this->second_string_ = PyString_FromString("World");
    }
    ~PyRuntimeFeedbackTest()
    {
        Py_DECREF(this->an_int_);
        Py_DECREF(this->a_list_);
        Py_DECREF(this->a_tuple_);
        Py_DECREF(this->a_dict_);
        Py_DECREF(this->a_string_);
        Py_DECREF(this->second_string_);
        Py_Finalize();
    }

    PyObject *an_int_;
    PyObject *a_list_;
    PyObject *a_tuple_;
    PyObject *a_dict_;
    PyObject *a_string_;
    PyObject *second_string_;
};

class PyLimitedFeedbackTest : public PyRuntimeFeedbackTest {
protected:
    PyLimitedFeedback feedback_;
};

TEST_F(PyLimitedFeedbackTest, NoObjects)
{
    SmallVector<PyObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
    this->feedback_.GetSeenObjectsInto(seen);
    EXPECT_TRUE(seen.empty());
}

TEST_F(PyLimitedFeedbackTest, NullObject)
{
    this->feedback_.AddObjectSeen(NULL);
    SmallVector<PyObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(1U, seen.size());
    EXPECT_EQ(NULL, seen[0]);
}

TEST_F(PyLimitedFeedbackTest, DuplicateObjects)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);
    long list_start_refcnt = Py_REFCNT(this->a_list_);

    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    this->feedback_.AddObjectSeen(this->an_int_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(this->a_list_));

    SmallVector<PyObject*, 3> seen;
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
}

TEST_F(PyLimitedFeedbackTest, FewObjects)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);
    long list_start_refcnt = Py_REFCNT(this->a_list_);

    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(this->a_list_));

    SmallVector<PyObject*, 3> seen;
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
}

TEST_F(PyLimitedFeedbackTest, TooManyObjects)
{
    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_tuple_);
    this->feedback_.AddObjectSeen(this->a_dict_);
    SmallVector<PyObject*, 3> seen;
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_EQ(this->a_tuple_, seen[2]);
    EXPECT_TRUE(this->feedback_.ObjectsOverflowed());
}

TEST_F(PyLimitedFeedbackTest, ExactlyThreeObjects)
{
    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    this->feedback_.AddObjectSeen(this->a_tuple_);
    SmallVector<PyObject*, 3> seen;
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_EQ(this->a_tuple_, seen[2]);
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
}

TEST_F(PyLimitedFeedbackTest, DtorLowersRefcount)
{
    PyLimitedFeedback *feedback = new PyLimitedFeedback();
    long int_start_refcnt = Py_REFCNT(this->an_int_);
    long list_start_refcnt = Py_REFCNT(this->a_list_);

    feedback->AddObjectSeen(this->an_int_);
    feedback->AddObjectSeen(this->a_list_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(this->a_list_));

    delete feedback;
    EXPECT_EQ(int_start_refcnt, Py_REFCNT(this->an_int_));
    EXPECT_EQ(list_start_refcnt, Py_REFCNT(this->a_list_));
}

TEST_F(PyLimitedFeedbackTest, SingleFunc)
{
    PyLimitedFeedback *feedback = new PyLimitedFeedback();

    PyObject *meth1 = PyObject_GetAttrString(this->a_string_, "join");
    long start_refcount = Py_REFCNT(meth1);
    feedback->AddFuncSeen(meth1);
    // This should not increase the reference count; we don't want to keep the
    // bound invocant alive longer than necessary.
    EXPECT_EQ(start_refcount, Py_REFCNT(meth1));

    SmallVector<FunctionRecord*, 3> seen;
    feedback->GetSeenFuncsInto(seen);
    ASSERT_EQ(1U, seen.size());
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth1), (void *)seen[0]->func);
    EXPECT_FALSE(feedback->FuncsOverflowed());

    delete feedback;
    Py_DECREF(meth1);
}

TEST_F(PyLimitedFeedbackTest, ThreeFuncs)
{
    PyLimitedFeedback *feedback = new PyLimitedFeedback();

    PyObject *meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *meth2 = PyObject_GetAttrString(this->a_string_, "split");
    PyObject *meth3 = PyObject_GetAttrString(this->a_string_, "lower");

    feedback->AddFuncSeen(meth1);
    feedback->AddFuncSeen(meth2);
    feedback->AddFuncSeen(meth3);

    SmallVector<FunctionRecord*, 3> seen;
    feedback->GetSeenFuncsInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth1), (void *)seen[0]->func);
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth2), (void *)seen[1]->func);
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth3), (void *)seen[2]->func);
    EXPECT_FALSE(feedback->FuncsOverflowed());

    delete feedback;
    Py_DECREF(meth1);
    Py_DECREF(meth2);
    Py_DECREF(meth3);
}

TEST_F(PyLimitedFeedbackTest, DuplicateFuncs)
{
    PyObject *meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *meth2 = PyObject_GetAttrString(this->a_string_, "split");

    this->feedback_.AddFuncSeen(meth1);
    this->feedback_.AddFuncSeen(meth2);
    this->feedback_.AddFuncSeen(meth1);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth1), (void *)seen[0]->func);
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth2), (void *)seen[1]->func);
    EXPECT_FALSE(this->feedback_.FuncsOverflowed());
}

TEST_F(PyLimitedFeedbackTest, SameMethodSameObject)
{
    PyObject *join_meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *join_meth2 = PyObject_GetAttrString(this->a_string_, "join");
    assert(join_meth1 && join_meth2);
    // The whole point is the the method objects are different, but really
    // represent the same method.
    assert(join_meth1 != join_meth2);

    this->feedback_.AddFuncSeen(join_meth1);
    this->feedback_.AddFuncSeen(join_meth2);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(1U, seen.size());

    Py_DECREF(join_meth1);
    Py_DECREF(join_meth2);
}

TEST_F(PyLimitedFeedbackTest, SameMethodSameTypeDifferentObjects)
{
    PyObject *join_meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *join_meth2 = PyObject_GetAttrString(this->second_string_, "join");
    assert(join_meth1 && join_meth2);
    // The whole point is the the method objects are different, but really
    // represent the same method, just with a different invocant.
    assert(join_meth1 != join_meth2);

    // join_meth2 should be recognized as a duplicate of join_meth1.
    this->feedback_.AddFuncSeen(join_meth1);
    this->feedback_.AddFuncSeen(join_meth2);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(1U, seen.size());

    Py_DECREF(join_meth1);
    Py_DECREF(join_meth2);
}

TEST_F(PyLimitedFeedbackTest, Counter)
{
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(1);
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(2);
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(1);
    EXPECT_EQ(3U, this->feedback_.GetCounter(0));
    EXPECT_EQ(2U, this->feedback_.GetCounter(1));
    EXPECT_EQ(1U, this->feedback_.GetCounter(2));
    // How to check that saturation works?
}

TEST_F(PyLimitedFeedbackTest, Copyable)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);

    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    this->feedback_.AddObjectSeen(this->a_string_);
    this->feedback_.AddObjectSeen(this->a_tuple_);
    PyLimitedFeedback second = this->feedback_;
    EXPECT_TRUE(second.ObjectsOverflowed());

    SmallVector<PyObject*, 3> seen;
    second.GetSeenObjectsInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_EQ(this->a_string_, seen[2]);
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(this->an_int_));

    // Demonstrate that the copies are independent.
    second.Clear();
    second.GetSeenObjectsInto(seen);
    ASSERT_EQ(0U, seen.size());
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(3U, seen.size());

    PyLimitedFeedback third;
    third.IncCounter(0);
    second = third;
    EXPECT_EQ(1U, second.GetCounter(0));
    EXPECT_EQ(0U, second.GetCounter(1));
    // second should release its reference to this->an_int_.
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
}

TEST_F(PyLimitedFeedbackTest, CopyableFuncs)
{
    PyObject *join_meth = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *split_meth = PyObject_GetAttrString(this->a_string_, "split");
    PyObject *count_meth = PyObject_GetAttrString(this->a_string_, "count");

    this->feedback_.AddFuncSeen(join_meth);
    this->feedback_.AddFuncSeen(split_meth);
    PyLimitedFeedback second = this->feedback_;
    SmallVector<FunctionRecord*, 3> seen;
    second.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());

    // Demonstrate that the copies are independent.
    second.AddFuncSeen(count_meth);
    second.GetSeenFuncsInto(seen);
    ASSERT_EQ(3U, seen.size());
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());
}

TEST_F(PyLimitedFeedbackTest, Assignment)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);
    long str_start_refcnt = Py_REFCNT(this->a_string_);
    PyLimitedFeedback second;

    this->feedback_.AddObjectSeen(this->an_int_);
    second.AddObjectSeen(this->a_string_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
    EXPECT_EQ(str_start_refcnt + 1, Py_REFCNT(this->a_string_));

    second = this->feedback_;
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(this->an_int_));
    EXPECT_EQ(str_start_refcnt, Py_REFCNT(this->a_string_));
}

class PyFullFeedbackTest : public PyRuntimeFeedbackTest {
protected:
    PyFullFeedback feedback_;
};

TEST_F(PyFullFeedbackTest, NoObjects)
{
    SmallVector<PyObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
    this->feedback_.GetSeenObjectsInto(seen);
    EXPECT_TRUE(seen.empty());
}

TEST_F(PyFullFeedbackTest, NullObject)
{
    this->feedback_.AddObjectSeen(NULL);
    SmallVector<PyObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
    this->feedback_.GetSeenObjectsInto(seen);
    EXPECT_EQ(1U, seen.size());
    EXPECT_EQ(NULL, seen[0]);
}

TEST_F(PyFullFeedbackTest, FiveObjects)
{
    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_tuple_);
    this->feedback_.AddObjectSeen(this->a_dict_);
    this->feedback_.AddObjectSeen(this->a_string_);
    SmallVector<PyObject*, 3> seen;
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(5U, seen.size());
    // These may not be in order, since PyFullFeedback uses a set to
    // store its contents.
    using std::find;
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), this->an_int_));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), this->a_list_));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), this->a_tuple_));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), this->a_dict_));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), this->a_string_));
    EXPECT_FALSE(this->feedback_.ObjectsOverflowed());
}

TEST_F(PyFullFeedbackTest, Refcounts)
{
    PyFullFeedback *feedback = new PyFullFeedback();
    long int_start_refcnt = Py_REFCNT(this->an_int_);

    feedback->AddObjectSeen(this->an_int_);
    feedback->AddObjectSeen(this->an_int_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));

    delete feedback;
    EXPECT_EQ(int_start_refcnt, Py_REFCNT(this->an_int_));
}

TEST_F(PyFullFeedbackTest, Counter)
{
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(1);
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(2);
    this->feedback_.IncCounter(0);
    this->feedback_.IncCounter(1);
    EXPECT_EQ(3U, this->feedback_.GetCounter(0));
    EXPECT_EQ(2U, this->feedback_.GetCounter(1));
    EXPECT_EQ(1U, this->feedback_.GetCounter(2));
    // How to check that saturation works?
}

TEST_F(PyFullFeedbackTest, Copyable)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);

    this->feedback_.AddObjectSeen(this->an_int_);
    this->feedback_.AddObjectSeen(this->a_list_);
    PyFullFeedback second = this->feedback_;
    SmallVector<PyObject*, 3> seen;
    second.GetSeenObjectsInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(this->an_int_, seen[0]);
    EXPECT_EQ(this->a_list_, seen[1]);
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(this->an_int_));

    // Demonstrate that the copies are independent.
    second.AddObjectSeen(this->a_string_);
    second.GetSeenObjectsInto(seen);
    ASSERT_EQ(3U, seen.size());
    this->feedback_.GetSeenObjectsInto(seen);
    ASSERT_EQ(2U, seen.size());

    PyFullFeedback third;
    third.IncCounter(0);
    second = third;
    EXPECT_EQ(1U, second.GetCounter(0));
    EXPECT_EQ(0U, second.GetCounter(1));
    // second should release its reference to this->an_int_.
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
}

TEST_F(PyFullFeedbackTest, CopyableFuncs)
{
    PyObject *join_meth = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *split_meth = PyObject_GetAttrString(this->a_string_, "split");
    PyObject *count_meth = PyObject_GetAttrString(this->a_string_, "count");

    this->feedback_.AddFuncSeen(join_meth);
    this->feedback_.AddFuncSeen(split_meth);
    PyFullFeedback second = this->feedback_;
    SmallVector<FunctionRecord*, 3> seen;
    second.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());

    // Demonstrate that the copies are independent.
    second.AddFuncSeen(count_meth);
    second.GetSeenFuncsInto(seen);
    ASSERT_EQ(3U, seen.size());
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());
}

TEST_F(PyFullFeedbackTest, Assignment)
{
    long int_start_refcnt = Py_REFCNT(this->an_int_);
    long str_start_refcnt = Py_REFCNT(this->a_string_);
    PyFullFeedback second;

    this->feedback_.AddObjectSeen(this->an_int_);
    second.AddObjectSeen(this->a_string_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(this->an_int_));
    EXPECT_EQ(str_start_refcnt + 1, Py_REFCNT(this->a_string_));

    second = this->feedback_;
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(this->an_int_));
    EXPECT_EQ(str_start_refcnt, Py_REFCNT(this->a_string_));
}

TEST_F(PyFullFeedbackTest, DuplicateFuncs)
{
    PyObject *meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *meth2 = PyObject_GetAttrString(this->a_string_, "split");

    this->feedback_.AddFuncSeen(meth1);
    this->feedback_.AddFuncSeen(meth2);
    this->feedback_.AddFuncSeen(meth1);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth1), (void *)seen[0]->func);
    EXPECT_EQ((void *)PyCFunction_GET_FUNCTION(meth2), (void *)seen[1]->func);
    EXPECT_FALSE(this->feedback_.FuncsOverflowed());
}

TEST_F(PyFullFeedbackTest, SameMethodSameObject)
{
    PyObject *join_meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *join_meth2 = PyObject_GetAttrString(this->a_string_, "join");
    assert(join_meth1 && join_meth2);
    // The whole point is the the method objects are different, but really
    // represent the same method.
    assert(join_meth1 != join_meth2);

    this->feedback_.AddFuncSeen(join_meth1);
    this->feedback_.AddFuncSeen(join_meth2);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(1U, seen.size());

    Py_DECREF(join_meth1);
    Py_DECREF(join_meth2);
}

TEST_F(PyFullFeedbackTest, SameMethodSameTypeDifferentObjects)
{
    PyObject *join_meth1 = PyObject_GetAttrString(this->a_string_, "join");
    PyObject *join_meth2 = PyObject_GetAttrString(this->second_string_, "join");
    assert(join_meth1 && join_meth2);
    // The whole point is the the method objects are different, but really
    // represent the same method, just with a different invocant.
    assert(join_meth1 != join_meth2);

    // join_meth2 should be recognized as a duplicate of join_meth1.
    this->feedback_.AddFuncSeen(join_meth1);
    this->feedback_.AddFuncSeen(join_meth2);

    SmallVector<FunctionRecord*, 3> seen;
    this->feedback_.GetSeenFuncsInto(seen);
    ASSERT_EQ(1U, seen.size());

    Py_DECREF(join_meth1);
    Py_DECREF(join_meth2);
}
