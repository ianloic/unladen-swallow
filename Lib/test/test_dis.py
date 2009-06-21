# Minimal tests for dis module

from test.test_support import run_unittest
import unittest
import sys
import dis
import StringIO


def _f(a):
    print a
    return 1

dis_f = """\
 %-4d         0 LOAD_GLOBAL              0 (#@print_stmt)
              3 LOAD_FAST                0 (a)
              6 CALL_FUNCTION            1
              9 POP_TOP

 %-4d        10 LOAD_CONST               1 (1)
             13 RETURN_VALUE
"""%(_f.func_code.co_firstlineno + 1,
     _f.func_code.co_firstlineno + 2)


def bug708901():
    for res in range(1,
                     10):
        pass

dis_bug708901 = """\
 %-4d         0 LOAD_GLOBAL              0 (range)
              3 LOAD_CONST               1 (1)

 %-4d         6 LOAD_CONST               2 (10)
              9 CALL_FUNCTION            2
             12 GET_ITER
        >>   13 FOR_ITER                 6 (to 22)
             16 STORE_FAST               0 (res)

 %-4d        19 JUMP_ABSOLUTE           13
        >>   22 LOAD_CONST               0 (None)
             25 RETURN_VALUE
"""%(bug708901.func_code.co_firstlineno + 1,
     bug708901.func_code.co_firstlineno + 2,
     bug708901.func_code.co_firstlineno + 3)


def bug1333982(x=[]):
    assert 0, ([s for s in x] +
              1)
    pass

dis_bug1333982 = """\
 %-4d         0 LOAD_CONST               1 (0)
              3 POP_JUMP_IF_TRUE        44
              6 LOAD_GLOBAL              0 (AssertionError)
              9 BUILD_LIST               0
             12 DUP_TOP
             13 STORE_FAST               1 (_[1])
             16 LOAD_FAST                0 (x)
             19 GET_ITER
        >>   20 FOR_ITER                13 (to 36)
             23 STORE_FAST               2 (s)
             26 LOAD_FAST                1 (_[1])
             29 LOAD_FAST                2 (s)
             32 LIST_APPEND
             33 JUMP_ABSOLUTE           20
        >>   36 DELETE_FAST              1 (_[1])

 %-4d        39 LOAD_CONST               2 (1)
             42 BINARY_ADD
             43 RAISE_VARARGS_TWO

 %-4d   >>   44 LOAD_CONST               0 (None)
             47 RETURN_VALUE
"""%(bug1333982.func_code.co_firstlineno + 1,
     bug1333982.func_code.co_firstlineno + 2,
     bug1333982.func_code.co_firstlineno + 3)

_BIG_LINENO_FORMAT = """\
%3d           0 LOAD_GLOBAL              0 (spam)
              3 POP_TOP
              4 LOAD_CONST               0 (None)
              7 RETURN_VALUE
"""

class DisTests(unittest.TestCase):
    def do_disassembly_test(self, func, expected):
        s = StringIO.StringIO()
        save_stdout = sys.stdout
        sys.stdout = s
        dis.dis(func)
        sys.stdout = save_stdout
        got = s.getvalue()
        # Trim trailing blanks (if any).
        lines = got.split('\n')
        lines = [line.rstrip() for line in lines]
        expected = expected.split("\n")
        import difflib
        if expected != lines:
            self.fail(
                "events did not match expectation:\n" +
                "\n".join(difflib.ndiff(expected,
                                        lines)))

    def test_opmap(self):
        self.assertEqual(dis.opmap["STOP_CODE"], 0)
        self.assertEqual(dis.opmap["LOAD_CONST"] in dis.hasconst, True)
        self.assertEqual(dis.opmap["STORE_NAME"] in dis.hasname, True)

    def test_opname(self):
        self.assertEqual(dis.opname[dis.opmap["LOAD_FAST"]], "LOAD_FAST")

    def test_boundaries(self):
        self.assertEqual(dis.opmap["EXTENDED_ARG"], dis.EXTENDED_ARG)
        self.assertEqual(dis.opmap["STORE_NAME"], dis.HAVE_ARGUMENT)

    def test_dis(self):
        self.do_disassembly_test(_f, dis_f)

    def test_bug_708901(self):
        self.do_disassembly_test(bug708901, dis_bug708901)

    def test_bug_1333982(self):
        # This one is checking bytecodes generated for an `assert` statement,
        # so fails if the tests are run with -O.  Skip this test then.
        if __debug__:
            self.do_disassembly_test(bug1333982, dis_bug1333982)

    def test_big_linenos(self):
        def func(count):
            namespace = {}
            func = "def foo():\n " + "".join(["\n "] * count + ["spam\n"])
            exec func in namespace
            return namespace['foo']

        # Test all small ranges
        for i in xrange(1, 300):
            expected = _BIG_LINENO_FORMAT % (i + 2)
            self.do_disassembly_test(func(i), expected)

        # Test some larger ranges too
        for i in xrange(300, 5000, 10):
            expected = _BIG_LINENO_FORMAT % (i + 2)
            self.do_disassembly_test(func(i), expected)

def test_main():
    run_unittest(DisTests)


if __name__ == "__main__":
    test_main()
