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
        this->second_int_ = PyInt_FromLong(7);
        this->a_list_ = PyList_New(0);
        this->a_tuple_ = PyTuple_New(0);
        this->a_dict_ = PyDict_New();
        this->a_string_ = PyString_FromString("Hello");
        this->second_string_ = PyString_FromString("World");
    }
    ~PyRuntimeFeedbackTest()
    {
        Py_DECREF(this->an_int_);
        Py_DECREF(this->second_int_);
        Py_DECREF(this->a_list_);
        Py_DECREF(this->a_tuple_);
        Py_DECREF(this->a_dict_);
        Py_DECREF(this->a_string_);
        Py_DECREF(this->second_string_);
        Py_Finalize();
    }

    PyObject *an_int_;
    PyObject *second_int_;
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

TEST_F(PyLimitedFeedbackTest, NoTypes)
{
    SmallVector<PyTypeObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
    this->feedback_.GetSeenTypesInto(seen);
    EXPECT_TRUE(seen.empty());
}

TEST_F(PyLimitedFeedbackTest, NullObject)
{
    this->feedback_.AddTypeSeen(NULL);
    SmallVector<PyTypeObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(1U, seen.size());
    EXPECT_EQ(NULL, seen[0]);
}

TEST_F(PyLimitedFeedbackTest, DuplicateTypes)
{
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);
    long list_start_refcnt = Py_REFCNT(&PyList_Type);

    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    this->feedback_.AddTypeSeen(this->an_int_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(&PyList_Type));

    SmallVector<PyTypeObject*, 3> seen;
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
}

TEST_F(PyLimitedFeedbackTest, FewTypes)
{
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);
    long list_start_refcnt = Py_REFCNT(&PyList_Type);

    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(&PyList_Type));

    SmallVector<PyTypeObject*, 3> seen;
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
}

TEST_F(PyLimitedFeedbackTest, TooManyTypes)
{
    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    this->feedback_.AddTypeSeen(this->second_int_);
    this->feedback_.AddTypeSeen(this->a_tuple_);
    this->feedback_.AddTypeSeen(this->a_dict_);
    SmallVector<PyTypeObject*, 3> seen;
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_EQ(&PyTuple_Type, seen[2]);
    EXPECT_TRUE(this->feedback_.TypesOverflowed());
}

TEST_F(PyLimitedFeedbackTest, ExactlyThreeTypes)
{
    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    this->feedback_.AddTypeSeen(this->a_tuple_);
    SmallVector<PyTypeObject*, 3> seen;
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_EQ(&PyTuple_Type, seen[2]);
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
}

TEST_F(PyLimitedFeedbackTest, DtorLowersRefcount)
{
    PyLimitedFeedback *feedback = new PyLimitedFeedback();
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);
    long list_start_refcnt = Py_REFCNT(&PyList_Type);

    feedback->AddTypeSeen(this->an_int_);
    feedback->AddTypeSeen(this->a_list_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(list_start_refcnt + 1, Py_REFCNT(&PyList_Type));

    delete feedback;
    EXPECT_EQ(int_start_refcnt, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(list_start_refcnt, Py_REFCNT(&PyList_Type));
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
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);

    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    this->feedback_.AddTypeSeen(this->a_string_);
    this->feedback_.AddTypeSeen(this->a_tuple_);
    PyLimitedFeedback second = this->feedback_;
    EXPECT_TRUE(second.TypesOverflowed());

    SmallVector<PyTypeObject*, 3> seen;
    second.GetSeenTypesInto(seen);
    ASSERT_EQ(3U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_EQ(&PyString_Type, seen[2]);
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(&PyInt_Type));

    // Demonstrate that the copies are independent.
    second.Clear();
    second.GetSeenTypesInto(seen);
    ASSERT_EQ(0U, seen.size());
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(3U, seen.size());

    PyLimitedFeedback third;
    third.IncCounter(0);
    second = third;
    EXPECT_EQ(1U, second.GetCounter(0));
    EXPECT_EQ(0U, second.GetCounter(1));
    // second should release its reference to PyInt_Type.
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
}

TEST_F(PyLimitedFeedbackTest, Assignment)
{
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);
    long str_start_refcnt = Py_REFCNT(&PyString_Type);
    PyLimitedFeedback second;

    this->feedback_.AddTypeSeen(this->an_int_);
    second.AddTypeSeen(this->a_string_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(str_start_refcnt + 1, Py_REFCNT(&PyString_Type));

    second = this->feedback_;
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(str_start_refcnt, Py_REFCNT(&PyString_Type));
}

class PyFullFeedbackTest : public PyRuntimeFeedbackTest {
protected:
    PyFullFeedback feedback_;
};

TEST_F(PyFullFeedbackTest, NoTypes)
{
    SmallVector<PyTypeObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
    this->feedback_.GetSeenTypesInto(seen);
    EXPECT_TRUE(seen.empty());
}

TEST_F(PyFullFeedbackTest, NullObject)
{
    this->feedback_.AddTypeSeen(NULL);
    SmallVector<PyTypeObject*, 3> seen;
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
    this->feedback_.GetSeenTypesInto(seen);
    EXPECT_EQ(1U, seen.size());
    EXPECT_EQ(NULL, seen[0]);
}

TEST_F(PyFullFeedbackTest, FiveTypes)
{
    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    this->feedback_.AddTypeSeen(this->second_int_);
    this->feedback_.AddTypeSeen(this->a_tuple_);
    this->feedback_.AddTypeSeen(this->a_dict_);
    this->feedback_.AddTypeSeen(this->a_string_);
    SmallVector<PyTypeObject*, 3> seen;
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(5U, seen.size());
    // These may not be in order, since PyFullFeedback uses a set to
    // store its contents.
    using std::find;
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), &PyInt_Type));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), &PyList_Type));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), &PyTuple_Type));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), &PyDict_Type));
    EXPECT_TRUE(seen.end() != find(seen.begin(), seen.end(), &PyString_Type));
    EXPECT_FALSE(this->feedback_.TypesOverflowed());
}

TEST_F(PyFullFeedbackTest, Refcounts)
{
    PyFullFeedback *feedback = new PyFullFeedback();
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);

    feedback->AddTypeSeen(this->an_int_);
    feedback->AddTypeSeen(this->an_int_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));

    delete feedback;
    EXPECT_EQ(int_start_refcnt, Py_REFCNT(&PyInt_Type));
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
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);

    this->feedback_.AddTypeSeen(this->an_int_);
    this->feedback_.AddTypeSeen(this->a_list_);
    PyFullFeedback second = this->feedback_;
    SmallVector<PyTypeObject*, 3> seen;
    second.GetSeenTypesInto(seen);
    ASSERT_EQ(2U, seen.size());
    EXPECT_EQ(&PyInt_Type, seen[0]);
    EXPECT_EQ(&PyList_Type, seen[1]);
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(&PyInt_Type));

    // Demonstrate that the copies are independent.
    second.AddTypeSeen(this->a_string_);
    second.GetSeenTypesInto(seen);
    ASSERT_EQ(3U, seen.size());
    this->feedback_.GetSeenTypesInto(seen);
    ASSERT_EQ(2U, seen.size());

    PyFullFeedback third;
    third.IncCounter(0);
    second = third;
    EXPECT_EQ(1U, second.GetCounter(0));
    EXPECT_EQ(0U, second.GetCounter(1));
    // second should release its reference to PyInt_Type.
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
}

TEST_F(PyFullFeedbackTest, Assignment)
{
    long int_start_refcnt = Py_REFCNT(&PyInt_Type);
    long str_start_refcnt = Py_REFCNT(&PyString_Type);
    PyFullFeedback second;

    this->feedback_.AddTypeSeen(this->an_int_);
    second.AddTypeSeen(this->a_string_);
    EXPECT_EQ(int_start_refcnt + 1, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(str_start_refcnt + 1, Py_REFCNT(&PyString_Type));

    second = this->feedback_;
    EXPECT_EQ(int_start_refcnt + 2, Py_REFCNT(&PyInt_Type));
    EXPECT_EQ(str_start_refcnt, Py_REFCNT(&PyString_Type));
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
