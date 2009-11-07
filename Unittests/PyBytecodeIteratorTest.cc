#include "Python.h"
#include "opcode.h"

#include "Util/PyBytecodeIterator.h"
#include "llvm/ADT/STLExtras.h"

#include "gtest/gtest.h"

class PyBytecodeIteratorTest : public testing::Test {
protected:
    PyBytecodeIteratorTest()
    {
        Py_NoSiteFlag = true;
        Py_Initialize();
    }
    ~PyBytecodeIteratorTest()
    {
        Py_Finalize();
    }
};

TEST_F(PyBytecodeIteratorTest, SimpleIteration)
{
    // Nonsense bytecode, just to test iteration.
    const char bytecode_str[] = {(char)POP_TOP,
                                 (char)LOAD_FAST, (char)5, (char)0,
                                 (char)BINARY_ADD};
    PyObject *code = PyString_FromStringAndSize(
            bytecode_str, llvm::array_lengthof(bytecode_str));

    PyBytecodeIterator iter(code);

    ASSERT_FALSE(iter.Done());
    EXPECT_FALSE(iter.Error());
    EXPECT_EQ(POP_TOP, iter.Opcode());
    EXPECT_EQ(0u, iter.CurIndex());
    EXPECT_EQ(1u, iter.NextIndex());

    iter.Advance();
    ASSERT_FALSE(iter.Done());
    EXPECT_FALSE(iter.Error());
    EXPECT_EQ(LOAD_FAST, iter.Opcode());
    EXPECT_EQ(5, iter.Oparg());
    EXPECT_EQ(1u, iter.CurIndex());
    EXPECT_EQ(4u, iter.NextIndex());

    iter.Advance();
    ASSERT_FALSE(iter.Done());
    EXPECT_FALSE(iter.Error());
    EXPECT_EQ(BINARY_ADD, iter.Opcode());
    EXPECT_EQ(4u, iter.CurIndex());
    EXPECT_EQ(5u, iter.NextIndex());

    iter.Advance();
    ASSERT_TRUE(iter.Done());
    EXPECT_FALSE(iter.Error());

    Py_DECREF(code);
}

TEST_F(PyBytecodeIteratorTest, ExtendedArg)
{
    // Nonsense bytecode, just to test iteration.
    const char bytecode_str[] = {(char)EXTENDED_ARG, (char)5, (char)0,
                                 (char)LOAD_FAST, (char)7, (char)0};
    PyObject *code = PyString_FromStringAndSize(
            bytecode_str, llvm::array_lengthof(bytecode_str));

    PyBytecodeIterator iter(code);

    ASSERT_FALSE(iter.Done());
    EXPECT_FALSE(iter.Error());
    EXPECT_EQ(LOAD_FAST, iter.Opcode());
    EXPECT_EQ(327687, iter.Oparg());
    // See comments in BytecodeIterator::Advance() for why CurIndex() is 0.
    EXPECT_EQ(0u, iter.CurIndex());
    EXPECT_EQ(6u, iter.NextIndex());

    iter.Advance();
    ASSERT_TRUE(iter.Done());
    EXPECT_FALSE(iter.Error());

    Py_DECREF(code);
}
