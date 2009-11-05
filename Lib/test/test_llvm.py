# Tests for our minimal LLVM wrappers
import __future__

from test import test_dynamic
from test import test_support

try:
    import _llvm
except ImportError:
    raise test_support.TestSkipped("not built against LLVM")

import __builtin__
import functools
import sys
import unittest


# Calculate default LLVM optimization level.
def _foo():
    pass
DEFAULT_OPT_LEVEL = _foo.__code__.co_optimization
del _foo


JIT_SPIN_COUNT = _llvm.get_hotness_threshold() / 10 + 1000
JIT_OPT_LEVEL = sys.flags.optimize if sys.flags.optimize > 2 else 2


def at_each_optimization_level(func):
    """Decorator for test functions, to run them at each optimization level."""
    levels = [None, -1, 0, 1, 2, 3]
    if DEFAULT_OPT_LEVEL != -1:
        levels = [level for level in levels if level >= DEFAULT_OPT_LEVEL]
    @functools.wraps(func)
    def result(self):
        for level in levels:
            func(self, level)
    return result


def compile_for_llvm(function_name, def_string, optimization_level=-1,
                     globals_dict=None):
    """Compiles function_name, defined in def_string to be run through LLVM.

    Compiles and runs def_string in a temporary namespace, pulls the
    function named 'function_name' out of that namespace, optimizes it
    at level 'optimization_level', -1 for the default optimization,
    and marks it to be JITted and run through LLVM.

    """
    namespace = {}
    if globals_dict is None:
        globals_dict = globals()
    exec def_string in globals_dict, namespace
    func = namespace[function_name]
    if optimization_level is not None:
        if optimization_level >= DEFAULT_OPT_LEVEL:
            func.__code__.co_optimization = optimization_level
        func.__code__.__use_llvm__ = True
    return func


class ExtraAssertsTestCase(unittest.TestCase):
    def assertRaisesWithArgs(self, expected_exception_type,
                             expected_args, f, *args, **kwargs):
        try:
            f(*args, **kwargs)
        except expected_exception_type, real_exception:
            pass
        else:
            self.fail("%r not raised" % expected_exception)
        self.assertEquals(real_exception.args, expected_args)

    def assertContains(self, obj, container):
        if obj not in container:
            self.fail("%r not found in %r" % (obj, container))


class LlvmTestCase(unittest.TestCase):

    """Common base class for LLVM-focused tests.

    Features provided:
        - Assert that the code doesn't bail to the interpreter.
    """

    def setUp(self):
        sys.setbailerror(True)

    def tearDown(self):
        sys.setbailerror(False)


class GeneralCompilationTests(ExtraAssertsTestCase, LlvmTestCase):

    def test_uncreatable(self):
        # Functions can only be created by their static factories.
        self.assertRaises(TypeError, _llvm._function)

    def test_use_llvm(self):
        # Regression test: setting __use_llvm__ without setting an optimization
        # level used to segfault when the function was called.
        def foo():
            return 5
        foo.__code__.__use_llvm__ = True
        foo()

    @at_each_optimization_level
    def test_llvm_compile(self, level):
        # Makes no sense at level None
        if level is None:
            return
        def f(x):
            pass
        f = _llvm.compile(f.func_code, level)
        self.assertTrue(isinstance(f, _llvm._function))
        self.assertRaises(TypeError, _llvm.compile, f, level)

    @at_each_optimization_level
    def test_run_simple_function(self, level):
        foo = compile_for_llvm("foo", """
def foo():
    pass
""", level)
        self.assertEquals(None, foo())

    @at_each_optimization_level
    def test_constants(self, level):
        foo = compile_for_llvm("foo", """
def foo(x):
    if x: x[...]
    return [1, 2L, 3.2, 7j, (), "Hello", u"Hello", None]
""", level)
        self.assertEquals([1, 2L, 3.2, 7j, (), "Hello", u"Hello", None],
                          foo(False))

    def test_same_named_functions_coexist(self):
        foo1 = compile_for_llvm("foo", """
def foo(a):
    return a
""")
        foo2 = compile_for_llvm("foo", """
def foo():
    return 7
""")
        self.assertEquals("Hello", foo1("Hello"))
        self.assertEquals(7, foo2())

    def test_stack_pointer_optimized_to_register(self):
        def test_func():
            # We may have to add opcode uses to here as we find things
            # that break the stack pointer optimization.
            return sum(range(*[1, 10, 3]))
        # Run mem2reg.
        test_func.__code__.co_optimization = 2
        self.assertFalse("%stack_pointer_addr = alloca"
                         in str(test_func.__code__.co_llvm))

    # -j always will cause this test to always fail.
    if _llvm.get_jit_control() != "always":
        def test_fetch_unset_co_llvm(self):
            def test_func():
                pass
            test_func.__code__.__use_llvm__ = True
            # Just setting __use_llvm__ doesn't force code generation.
            self.assertEqual(str(test_func.__code__.co_llvm), "None")

    @at_each_optimization_level
    def test_return_arg(self, level):
        foo = compile_for_llvm("foo", """
def foo(a):
    return a
""", level)
        self.assertEquals(3, foo(3))
        self.assertEquals("Hello", foo("Hello"))

    @at_each_optimization_level
    def test_unbound_local(self, level):
        foo = compile_for_llvm("foo", """
def foo():
    a = a
""", level)
        try:
            foo()
        except UnboundLocalError as e:
            self.assertEquals(
                str(e), "local variable 'a' referenced before assignment")
        else:
            self.fail("Expected UnboundLocalError")

    @at_each_optimization_level
    def test_assign(self, level):
        foo = compile_for_llvm("foo", """
def foo(a):
    b = a
    return b
""", level)
        self.assertEquals(3, foo(3))
        self.assertEquals("Hello", foo("Hello"))

    @at_each_optimization_level
    def test_raising_getiter(self, level):
        class RaisingIter(object):
            def __iter__(self):
                raise RuntimeError
        loop = compile_for_llvm("loop", """
def loop(range):
    for i in range:
        pass
""", level)
        self.assertRaises(RuntimeError, loop, RaisingIter())

    @at_each_optimization_level
    def test_raising_next(self, level):
        class RaisingNext(object):
            def __iter__(self):
                return self
            def next(self):
                raise RuntimeError
        loop = compile_for_llvm("loop", """
def loop(range):
    for i in range:
        pass
""", level)
        self.assertRaises(RuntimeError, loop, RaisingNext())

    @at_each_optimization_level
    def test_loop(self, level):
        loop = compile_for_llvm("loop", """
def loop(range):
    for i in range:
        pass
""", level)
        r = iter(range(12))
        self.assertEquals(None, loop(r))
        self.assertRaises(StopIteration, next, r)

    @at_each_optimization_level
    def test_return_from_loop(self, level):
        loop = compile_for_llvm("loop", """
def loop(range):
    for i in range:
        return i
""", level)
        self.assertEquals(1, loop([1,2,3]))

    @at_each_optimization_level
    def test_finally(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup(obj):
    try:
        return 3
    finally:
        obj['x'] = 2
""", level)
        obj = {}
        self.assertEquals(3, cleanup(obj))
        self.assertEquals({'x': 2}, obj)

    @at_each_optimization_level
    def test_nested_finally(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup(obj):
    try:
        try:
            return 3
        finally:
            obj['x'] = 2
    finally:
        obj['y'] = 3
""", level)
        obj = {}
        self.assertEquals(3, cleanup(obj))
        self.assertEquals({'x': 2, 'y': 3}, obj)

    @at_each_optimization_level
    def test_finally_fallthrough(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup(obj):
    try:
        obj['y'] = 3
    finally:
        obj['x'] = 2
    return 3
""", level)
        obj = {}
        self.assertEquals(3, cleanup(obj))
        self.assertEquals({'x': 2, 'y': 3}, obj)

    @at_each_optimization_level
    def test_exception_out_of_finally(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup(obj):
    try:
        obj['x'] = 2
    finally:
        a = a
        obj['y'] = 3
    return 3
""", level)
        obj = {}
        self.assertRaises(UnboundLocalError, cleanup, obj)
        self.assertEquals({'x': 2}, obj)

    @at_each_optimization_level
    def test_exception_through_finally(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup(obj):
    try:
        a = a
    finally:
        obj['x'] = 2
""", level)
        obj = {}
        self.assertRaises(UnboundLocalError, cleanup, obj)
        self.assertEquals({'x': 2}, obj)

    @at_each_optimization_level
    def test_return_in_finally_overrides(self, level):
        cleanup = compile_for_llvm("cleanup", """
def cleanup():
    try:
        return 3
    finally:
        return 2
""", level)
        self.assertEquals(2, cleanup())

    @at_each_optimization_level
    def test_except(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        raise ZeroDivisionError
    except:
        obj["x"] = 2
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_filtered_except(self, level):
        catch = compile_for_llvm("catch", """
def catch(exc_type, obj):
    try:
        1 / 0
    except exc_type:
        obj["x"] = 2
""", level)
        obj = {}
        self.assertEquals(None, catch(ZeroDivisionError, obj))
        self.assertEquals({"x": 2}, obj)
        obj = {}
        self.assertRaises(ZeroDivisionError, catch, UnboundLocalError, obj)
        self.assertEquals({}, obj)

    @at_each_optimization_level
    def test_filtered_except_var(self, level):
        catch = compile_for_llvm("catch", """
def catch():
    try:
        1 / 0
    except ZeroDivisionError, exc:
        return exc
""", level)
        exc = catch()
        self.assertEquals(ZeroDivisionError, type(exc))
        self.assertEquals(('integer division or modulo by zero',), exc.args)

    @at_each_optimization_level
    def test_except_skipped_on_fallthrough(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        obj["x"] = 2
    except:
        obj["y"] = 3
    return 7
""", level)
        obj = {}
        self.assertEquals(7, catch(obj))
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_else_hit_on_fallthrough(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        obj["x"] = 2
    except:
        obj["y"] = 3
    else:
        obj["z"] = 4
    return 7
""", level)
        obj = {}
        self.assertEquals(7, catch(obj))
        self.assertEquals({"x": 2, "z": 4}, obj)

    @at_each_optimization_level
    def test_else_skipped_on_catch(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        a = a
    except:
        obj["y"] = 3
    else:
        obj["z"] = 4
    return 7
""", level)
        obj = {}
        self.assertEquals(7, catch(obj))
        self.assertEquals({"y": 3}, obj)

    @at_each_optimization_level
    def test_raise_from_except(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        raise ZeroDivisionError
    except:
        a = a
    obj["x"] = 2
""", level)
        obj = {}
        self.assertRaises(UnboundLocalError, catch, obj)
        self.assertEquals({}, obj)

    @at_each_optimization_level
    def test_nested_except(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            1 / 0
        except:
            obj["x"] = 2
            a = a
    except:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2, "y": 3}, obj)

    @at_each_optimization_level
    def test_nested_except_skipped_on_fallthrough(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            raise ZeroDivisionError
        except:
            obj["x"] = 2
    except:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_nested_finally_doesnt_block_catch(self, level):
        # Raise from the try.
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            1 / 0
        finally:
            obj["x"] = 2
    except:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2, "y": 3}, obj)

        # And raise from the finally.
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            obj["x"] = 2
        finally:
            raise ZeroDivisionError
    except:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2, "y": 3}, obj)

    @at_each_optimization_level
    def test_nested_except_goes_through_finally(self, level):
        # Raise from the try.
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            1 / 0
        except:
            obj["x"] = 2
    finally:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2, "y": 3}, obj)

        # And raise from the except.
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        try:
            raise UnboundLocalError
        except:
            1 / 0
    finally:
        obj["y"] = 3
""", level)
        obj = {}
        self.assertRaises(ZeroDivisionError, catch, obj)
        self.assertEquals({"y": 3}, obj)

    @at_each_optimization_level
    def test_finally_in_finally(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        raise ZeroDivisionError
    finally:
        try:
            a = a
        finally:
            obj["x"] = 2
""", level)
        obj = {}
        self.assertRaises(UnboundLocalError, catch, obj)
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_subexception_caught_in_finally(self, level):
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        1 / 0
    finally:
        try:
            a = a
        except:
            obj["x"] = 2
""", level)
        obj = {}
        self.assertRaises(ZeroDivisionError, catch, obj)
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_delete_fast(self, level):
        delit = compile_for_llvm('delit', """
def delit(x):
    y = 2
    z = 3
    del y
    del x
    return z
""", level)
        self.assertEquals(delit(1), 3)

        useit = compile_for_llvm('useit', """
def useit(x):
    del x
    return x
""", level)
        self.assertRaises(UnboundLocalError, useit, 1)

        misuseit = compile_for_llvm('misuseit', 'def misuseit(x): del y',
                                    level)
        self.assertRaises(UnboundLocalError, misuseit, 1)

        reuseit = compile_for_llvm('reuseit', """
def reuseit(x):
    del x
    x = 3
    return x
""", level)
        self.assertEquals(reuseit(1), 3)

    @at_each_optimization_level
    def test_call_function(self, level):
        f1 = compile_for_llvm("f1", "def f1(x): return x()", level)
        self.assertEquals(f1(lambda: 5), 5)
        def raise_exc():
            raise ValueError
        self.assertRaises(ValueError, f1, raise_exc)

        f2 = compile_for_llvm("f2", "def f2(x, y, z): return x(y, 2, z)",
                              level)
        self.assertEquals(f2(lambda *args: args, 1, 3), (1, 2, 3))

        f3 = compile_for_llvm("f3", "def f3(x, y, z): return x(y(z()))",
                              level)
        self.assertEquals(f3(lambda x: x+1, lambda x: x+2, lambda: 0), 3)

    @at_each_optimization_level
    def test_load_global(self, level):
        our_globals = dict(globals())  # Isolate the test's global effects.
        testvalue = 'test global value'
        loadglobal = compile_for_llvm('loadglobal',
                                      'def loadglobal(): return testvalue',
                                      level, our_globals)
        our_globals['testvalue'] = testvalue
        self.assertEquals(loadglobal(), testvalue)

        loadbuiltin = compile_for_llvm('loadbuiltin',
                                       'def loadbuiltin(): return str',
                                       level)
        self.assertEquals(loadbuiltin(), str)

        nosuchglobal = compile_for_llvm('nosuchglobal', '''
def nosuchglobal():
    return there_better_be_no_such_global
''', level)
        self.assertRaises(NameError, nosuchglobal)

    @at_each_optimization_level
    def test_store_global(self, level):
        our_globals = dict(globals())  # Isolate the test's global effects.
        setglobal = compile_for_llvm('setglobal', '''
def setglobal(x):
    global _test_global
    _test_global = x
''', level, our_globals)
        testvalue = "test global value"
        self.assertTrue('_test_global' not in our_globals)
        setglobal(testvalue)
        self.assertContains('_test_global', our_globals)
        self.assertEquals(our_globals['_test_global'], testvalue)

    @at_each_optimization_level
    def test_delete_global(self, level):
        our_globals = dict(globals())  # Isolate the test's global effects.
        delglobal = compile_for_llvm('delglobal', '''
def delglobal():
    global _test_global
    del _test_global
''', level, our_globals)
        our_globals['_test_global'] = 'test global value'
        self.assertContains('_test_global', our_globals)
        delglobal()
        self.assertTrue('_test_global' not in our_globals)

    @at_each_optimization_level
    def test_load_name(self, level):
        our_globals = dict(globals())  # Isolate the test's global effects.
        testvalue = 'test name value'
        loadlocal = compile_for_llvm('loadlocal', '''
def loadlocal():
    exec 'testvalue = "Hello"'
    return testvalue
''', level, our_globals)
        our_globals['testvalue'] = testvalue
        self.assertEquals(loadlocal(), 'Hello')

        our_globals = dict(globals())
        loadglobal = compile_for_llvm('loadglobal', '''
def loadglobal():
    exec ''
    return testvalue
''', level, our_globals)
        our_globals['testvalue'] = testvalue
        self.assertEquals(loadglobal(), testvalue)

        loadbuiltin = compile_for_llvm('loadbuiltin', '''
def loadbuiltin():
    exec ''
    return str
''', level)
        self.assertEquals(loadbuiltin(), str)

        nosuchname = compile_for_llvm('nosuchname', '''
def nosuchname():
    exec ''
    return there_better_be_no_such_name
''', level)
        self.assertRaises(NameError, nosuchname)

    @at_each_optimization_level
    def test_store_name(self, level):
        set_local = compile('a = 3', '<string>', 'exec')
        if level is not None:
            set_local.co_optimization = level
            set_local.__use_llvm__ = True
        exec set_local
        self.assertEquals(a, 3)

    @at_each_optimization_level
    def test_delete_name(self, level):
        do_del = compile('del a', '<string>', 'exec')
        if level is not None:
            do_del.co_optimization = level
            do_del.__use_llvm__ = True
        exec 'a = 3'
        self.assertEquals(a, 3)
        exec do_del
        try:
            a
        except NameError:
            pass
        else:
            self.fail('Expected "a" to be deleted')

        try:
            exec compile('del nonexistent', '<string>', 'exec')
        except NameError, e:
            self.assertEquals(e.args, ('name \'nonexistent\' is not defined',))
        else:
            self.fail('Expected not to find "nonexistent"')

    @at_each_optimization_level
    def test_simple_if_stmt(self, level):
        simple_if = compile_for_llvm("simple_if", """
def simple_if(x):
    if x:
        return "true"
""", level)
        self.assertEquals(simple_if(True), "true")
        self.assertEquals(simple_if(False), None)

        simple_if_else = compile_for_llvm("simple_if_else", """
def simple_if_else(x):
    if x:
        return "true"
    else:
        return "false"
""", level)
        self.assertEquals(simple_if_else("not false"), "true")
        self.assertEquals(simple_if_else(""), "false")

    @at_each_optimization_level
    def test_if_stmt_exceptions(self, level):
        if_exception = compile_for_llvm("if_exception", """
def if_exception(x):
    if x:
        return 1
""", level)
        class Unboolable(object):
            def __nonzero__(self):
                raise RuntimeError
        self.assertRaises(RuntimeError, if_exception, Unboolable())

    @at_each_optimization_level
    def test_complex_if(self, level):
        complex_if = compile_for_llvm("complex_if", """
def complex_if(x, y, z):
    if x:
        if y:
            return 1
        else:
            if z:
                return 2
            return 1 / 0
    else:
        return 3
""", level)
        self.assertEquals(complex_if(True, True, False), 1)
        self.assertEquals(complex_if(True, False, True), 2)
        self.assertEquals(complex_if(False, True, True), 3)
        self.assertRaises(ZeroDivisionError, complex_if, True, False, False)

    # Asserts aren't compiled when -O is passed.
    if sys.flags.optimize < 1:
        @at_each_optimization_level
        def test_assert(self, level):
            f = compile_for_llvm("f", "def f(x): assert x", level)
            self.assertEquals(f(1), None)
            self.assertRaises(AssertionError, f, 0)

    @at_each_optimization_level
    def test_and(self, level):
        and1 = compile_for_llvm("and1",
                                "def and1(x, y): return x and y", level)
        self.assertEquals(and1("x", "y"), "y")
        self.assertEquals(and1((), "y"), ())
        self.assertEquals(and1((), ""), ())

        and2 = compile_for_llvm("and2",
                                "def and2(x, y): return x+1 and y+1",
                                level)
        self.assertEquals(and2(-1, "5"), 0)
        self.assertRaises(TypeError, and2, "5", 5)
        self.assertRaises(TypeError, and2,  5, "5")

    @at_each_optimization_level
    def test_or(self, level):
        or1 = compile_for_llvm("or1", "def or1(x, y): return x or y", level)
        self.assertEquals(or1("x", "y"), "x")
        self.assertEquals(or1((), "y"), "y")
        self.assertEquals(or1((), ""), "")

        or2 = compile_for_llvm("or2",
                               "def or2(x, y): return x+1 or y+1", level)
        self.assertEquals(or2(5, "5"), 6)
        self.assertRaises(TypeError, or2, "5", 5)
        self.assertRaises(TypeError, or2, -1, "5")

    @at_each_optimization_level
    def test_complex_or(self, level):
        complex_or = compile_for_llvm('complex_or', '''
def complex_or(a, b, c):
    return a or b or c
''', level)
        self.assertEquals(complex_or(1, 2, 0), 1)
        self.assertEquals(complex_or(1, 2, 3), 1)
        self.assertEquals(complex_or(3, 0, 0), 3)
        self.assertEquals(complex_or(0, 3, 0), 3)
        self.assertEquals(complex_or(0, 0, 1), 1)
        self.assertEquals(complex_or(0, 0, 0), 0)

        complex_or_and = compile_for_llvm('complex_or_and', '''
def complex_or_and(a, b, c):
    return a or b and c
''', level)
        self.assertEquals(complex_or_and(3, 0, 0), 3)
        self.assertEquals(complex_or_and("", 3, 0), 0)
        self.assertEquals(complex_or_and("", 0, 1), 0)
        self.assertEquals(complex_or_and(0, 3, 1), 1)

    @at_each_optimization_level
    def test_complex_and(self, level):
        complex_and = compile_for_llvm('complex_and', '''
def complex_and(a, b, c):
    return a and b and c
''', level)
        self.assertEquals(complex_and(3, 0, ""), 0)
        self.assertEquals(complex_and(3, 2, 0), 0)
        self.assertEquals(complex_and(3, 2, 1), 1)
        self.assertEquals(complex_and(0, 3, 2), 0)
        self.assertEquals(complex_and(3, 0, 2), 0)

        complex_and_or = compile_for_llvm('complex_and_or', '''
def complex_and_or(a, b, c):
    return a and b or c
''', level)
        self.assertEquals(complex_and_or(3, "", 0), 0)
        self.assertEquals(complex_and_or(1, 3, 0), 3)
        self.assertEquals(complex_and_or(1, 3, 2), 3)
        self.assertEquals(complex_and_or(0, 3, 1), 1)

    @at_each_optimization_level
    def test_break(self, level):
        break_one = compile_for_llvm("break_one", """
def break_one(x):
    for y in [1, 2]:
        x["break"] = y
        break
        x["post break"] = y
    else:
        x["else"] = True
    return x
""", level)
        self.assertEqual(break_one({}), {"break": 1})

        nested = compile_for_llvm("nested", """
def nested(x):
    for y in [1, 2]:
        for z in [3, 4]:
            x["break"] = z
            break
            x["post break"] = z
        else:
            x["inner else"] = True
        x["outer"] = y
    else:
        x["else"] = True
    return x
""", level)
        self.assertEqual(nested({}), {"break": 3, "outer": 2, "else": True})

    @at_each_optimization_level
    def test_continue(self, level):
        # CONTINUE_LOOP is only used inside a try/except block. Otherwise,
        # the continue statement is lowered to a JUMP_ABSOLUTE.
        continue_one = compile_for_llvm("continue_one", """
def continue_one(x):
    for y in [1, 2]:
        if y:
            x["continue"] = y
            try:
                continue
            except:
                pass
            finally:
                x["finally"] = y
        1 / 0
    else:
        x["else"] = True
    return x
""", level)
        self.assertEqual(continue_one({}), {"continue": 2, "else": True,
                                            "finally": 2})

        nested = compile_for_llvm("nested", """
def nested(x):
    for y in [1, 2]:
        for z in [3, 4]:
            if z:
                x["continue"] = z
                try:
                    continue
                except:
                    pass
            1 / 0
        else:
            x["inner else"] = True
        x["outer"] = y
    else:
        x["else"] = True
    return x
""", level)
        self.assertEqual(nested({}), {"continue": 4, "outer": 2,
                                      "inner else": True, "else": True})

    @at_each_optimization_level
    def test_load_attr(self, level):
        load_attr = compile_for_llvm('load_attr',
                                     'def load_attr(o): return o.attr',
                                     level)
        load_attr.attr = 1
        self.assertEquals(load_attr(load_attr), 1)
        self.assertRaises(AttributeError, load_attr, object())

    @at_each_optimization_level
    def test_store_attr(self, level):
        store_attr = compile_for_llvm('store_attr',
                                      'def store_attr(o): o.attr = 2',
                                      level)
        store_attr(store_attr)
        self.assertEquals(store_attr.attr, 2)
        self.assertRaises(AttributeError, store_attr, object())

    @at_each_optimization_level
    def test_delete_attr(self, level):
        delete_attr = compile_for_llvm('delete_attr',
                                       'def delete_attr(o): del o.attr',
                                       level)
        delete_attr.attr = 3
        delete_attr(delete_attr)
        self.assertFalse(hasattr(delete_attr, 'attr'))
        self.assertRaises(AttributeError, delete_attr, object())

    @at_each_optimization_level
    def test_call_varargs(self, level):
        f1 = compile_for_llvm("f1", "def f1(x, args): return x(*args)",
                              level)
        def receiver1(a, b):
            return a, b
        self.assertEquals(f1(receiver1, (1, 2)), (1, 2))
        self.assertRaises(TypeError, f1, receiver1, None)
        self.assertRaises(TypeError, f1, None, (1, 2))

        f2 = compile_for_llvm("f2",
                              "def f2(x, args): return x(1, 2, *args)",
                              level)
        def receiver2(a, *args):
            return a, args
        self.assertEquals(f2(receiver2, (3, 4, 5)), (1, (2, 3, 4, 5)))

    @at_each_optimization_level
    def test_call_kwargs(self, level):
        f = compile_for_llvm("f",
                             "def f(x, kwargs): return x(a=1, **kwargs)",
                             level)
        def receiver(**kwargs):
            return kwargs
        self.assertEquals(f(receiver, {'b': 2, 'c': 3}),
                          {'a': 1, 'b': 2, 'c': 3})

    @at_each_optimization_level
    def test_call_args_kwargs(self, level):
        f = compile_for_llvm("f", """
def f(x, args, kwargs):
    return x(1, d=4, *args, **kwargs)
""", level)
        def receiver(*args, **kwargs):
            return args, kwargs
        self.assertEquals(f(receiver, (2, 3), {'e': 5, 'f': 6}),
                          ((1, 2, 3), {'d': 4, 'e': 5, 'f': 6}))

    @at_each_optimization_level
    def test_varargs_func(self, level):
        f = compile_for_llvm("f", """
def f(*args):
    return zip(*args)
""", level)
        self.assertEqual(f([1], [2]), [(1, 2)])

    @at_each_optimization_level
    def test_kwargs_func(self, level):
        f = compile_for_llvm("f", """
def f(**kwargs):
    return dict(**kwargs)
""", level)
        self.assertEqual(f(a=3), {"a": 3})

    @at_each_optimization_level
    def test_build_slice(self, level):
        class Sliceable(object):
            def __getitem__(self, item):
                return item
        # Test BUILD_SLICE_TWO; make sure we didn't swap arguments.
        slice_two = compile_for_llvm('slice_two',
                                      'def slice_two(o): return o[1:2:]',
                                      level)
        self.assertEquals(slice_two(Sliceable()), slice(1, 2, None))
        # Test BUILD_SLICE_THREE.
        slice_three = compile_for_llvm('slice_three',
                                     'def slice_three(o): return o[1:2:3]',
                                     level)
        self.assertEquals(slice_three(Sliceable()), slice(1, 2, 3))
        # No way to make BUILD_SLICE_* raise exceptions.

    @at_each_optimization_level
    def test_with(self, level):
        class SimpleCM(object):
            def __enter__(self):
                self.enter = True

            def __exit__(self, *exc_info):
                self.exit = True

        with_simple = compile_for_llvm('with_simple', '''
def with_simple(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
    self.assertEqual(x.__dict__, {"enter": True, "exit": True})
''', level)
        with_simple(self, SimpleCM())

        with_raise = compile_for_llvm('with_raise', '''
def with_raise(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
        raise ArithmeticError
''', level)
        x = SimpleCM()
        self.assertRaises(ArithmeticError, with_raise, self, x)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        with_return = compile_for_llvm('with_return', '''
def with_return(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
        return 55
''', level)
        x = SimpleCM()
        self.assertEqual(with_return(self, x), 55)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        class SwallowingCM(object):
            def __enter__(self):
                self.enter = True

            def __exit__(self, *exc_info):
                self.exit = True
                return True  # Swallow the raised exception

        with_swallow = compile_for_llvm('with_swallow', '''
def with_swallow(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
        raise ArithmeticError
    return 55
''', level)
        x = SwallowingCM()
        self.assertEqual(with_swallow(self, x), 55)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        class BustedExitCM(object):
            def __enter__(self):
                self.enter = True

            def __exit__(self, *exc_info):
                self.exit = True
                class A(object):
                    def __nonzero__(self):
                        raise ArithmeticError
                return A()  # Test error paths in WITH_CLEANUP

        with_ignored_bad_exit = compile_for_llvm('with_ignored_bad_exit', '''
def with_ignored_bad_exit(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
    return 55
''', level)
        x = BustedExitCM()
        self.assertEqual(with_ignored_bad_exit(self, x), 55)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        with_bad_exit = compile_for_llvm('with_bad_exit', '''
def with_bad_exit(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": True})
        raise KeyError
    return 55
''', level)
        x = BustedExitCM()
        self.assertRaises(ArithmeticError, with_bad_exit, self, x)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        class RaisingCM(object):
            def __enter__(self):
                self.enter = True

            def __exit__(self, *exc_info):
                self.exit = True
                raise ArithmeticError

        with_error = compile_for_llvm('with_error', '''
def with_error(self, x):
    with x:
        return 55
''', level)
        x = RaisingCM()
        self.assertRaises(ArithmeticError, with_error, self, x)
        self.assertEqual(x.__dict__, {'enter': True, 'exit': True})

        class NestedCM(object):
            def __init__(self):
                self.enter = 0
                self.exit = 0

            def __enter__(self):
                self.enter += 1

            def __exit__(self, *exc_info):
                self.exit += 1

        with_yield = compile_for_llvm('with_yield', '''
def with_yield(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": 1, "exit": 0})
        yield 7
        self.assertEqual(x.__dict__, {"enter": 1, "exit": 0})
        yield 8
    self.assertEqual(x.__dict__, {"enter": 1, "exit": 1})
''', level)
        x = NestedCM()
        self.assertEqual(list(with_yield(self, x)), [7, 8])

        with_nested = compile_for_llvm('with_nested', '''
def with_nested(self, x):
    with x:
        self.assertEqual(x.__dict__, {"enter": 1, "exit": 0})
        with x:
            self.assertEqual(x.__dict__, {"enter": 2, "exit": 0})
        self.assertEqual(x.__dict__, {"enter": 2, "exit": 1})
    self.assertEqual(x.__dict__, {"enter": 2, "exit": 2})
''', level)
        x = NestedCM()
        with_nested(self, x)

    @at_each_optimization_level
    def test_raise(self, level):
        raise_onearg = compile_for_llvm('raise_onearg', '''
def raise_onearg(x):
    raise x
''', level)
        self.assertRaises(OpExc, raise_onearg, OpExc);

        raise_twoargs = compile_for_llvm('raise_twoargs', '''
def raise_twoargs(x, y):
    raise x, y
''', level)
        self.assertRaisesWithArgs(OpExc, ('twoarg',),
                                  raise_twoargs, OpExc, OpExc('twoarg'));

    @at_each_optimization_level
    def test_reraise(self, level):
        raise_noargs = compile_for_llvm('raise_noargs', '''
def raise_noargs():
    raise
''', level)
        exc = OpExc('exc')
        def setup_traceback(e):
            raise e
        try:
            setup_traceback(exc)
        except OpExc:
            # orig_tb and exc re-used for raise_threeargs.
            orig_tb = sys.exc_info()[2]
            try:
                raise_noargs()
            except OpExc, e:
                new_tb = sys.exc_info()[2]
                # Test that we got the right exception and the right
                # traceback. Test both equality and identity for more
                # convenient error displays when things aren't as expected.
                self.assertEquals(e, exc)
                self.assertTrue(e is exc)
                self.assertEquals(new_tb.tb_next, orig_tb)
                self.assertTrue(new_tb.tb_next is orig_tb)
            else:
                self.fail('expected OpExc exception')
        else:
            self.fail('expected OpExc exception')

        raise_threeargs = compile_for_llvm('raise_threeargs', '''
def raise_threeargs(x, y, z):
    raise x, y, z
''', level)
        # Explicit version of the no-args raise.
        try:
            # Re-using exc and orig_tb from raise_noargs.
            raise_threeargs(OpExc, exc, orig_tb)
        except OpExc, e:
            new_tb = sys.exc_info()[2]
            self.assertEquals(e, exc)
            self.assertTrue(e is exc)
            self.assertEquals(new_tb.tb_next, orig_tb)
            self.assertTrue(new_tb.tb_next is orig_tb)
        else:
            self.fail('expected OpExc exception')

    @at_each_optimization_level
    def test_complex_reraise(self, level):
        reraise = compile_for_llvm('reraise', '''
def reraise(raiser, exctype):
    try:
        raiser()
    except:
        try:
            raise
        except exctype:
            return "inner"
        return "middle"
    return "outer"
''', level)
        def raiser():
            raise ZeroDivisionError
        self.assertEquals(reraise(raiser, ZeroDivisionError),
                          "inner")
        self.assertRaises(ZeroDivisionError, reraise, raiser, TypeError)

    @at_each_optimization_level
    def test_simple_yield(self, level):
        generator = compile_for_llvm("generator", """
def generator():
    yield 1
    yield 2
    yield 3
""", level)
        g = generator()
        self.assertEquals(1, g.next())
        self.assertEquals(2, g.next())
        self.assertEquals(3, g.next())
        self.assertRaises(StopIteration, g.next)

    @at_each_optimization_level
    def test_yield_in_loop(self, level):
        generator = compile_for_llvm("generator", """
def generator(x):
    for i in x:
        yield i
""", level)
        g = generator([1, 2, 3, 4])
        self.assertEquals([1, 2, 3, 4], list(g))

        cross_product = compile_for_llvm("cross_product", """
def cross_product(x, y):
    for i in x:
        for j in y:
            yield (i, j)
""", level)
        g = cross_product([1, 2], [3, 4])
        self.assertEquals([(1,3), (1,4), (2,3), (2,4)], list(g))

    @at_each_optimization_level
    def test_yield_saves_block_stack(self, level):
        generator = compile_for_llvm("generator", """
def generator(x):
    yield "starting"
    for i in x:
        try:
            try:
                1 / i
                yield ("survived", i)
            finally:
                yield ("finally", i)
        except ZeroDivisionError:
            yield "caught exception"
    yield "done looping"
""", level)
        self.assertEquals(list(generator([0, 1, 2])),
                          ["starting",
                           ("finally", 0),
                           "caught exception",
                           ("survived", 1),
                           ("finally", 1),
                           ("survived", 2),
                           ("finally", 2),
                           "done looping"])

    @at_each_optimization_level
    def test_generator_send(self, level):
        generator = compile_for_llvm("generator", """
def generator():
    yield (yield 1)
""", level)
        g = generator()
        self.assertEquals(1, g.next())
        self.assertEquals("Hello world", g.send("Hello world"))
        self.assertRaises(StopIteration, g.send, 3)

    @at_each_optimization_level
    def test_generator_throw(self, level):
        generator = compile_for_llvm("generator", """
def generator(obj):
    try:
        yield "starting"
    except ArithmeticError:
        obj["caught"] = 1
    finally:
        obj["finally"] = 1
    yield "done"
""", level)
        obj = {}
        g = generator(obj)
        self.assertEquals("starting", g.next())
        self.assertEquals("done", g.throw(ArithmeticError))
        self.assertEquals(None, g.close())
        self.assertEquals({"caught": 1, "finally": 1}, obj)

        obj = {}
        g = generator(obj)
        self.assertEquals("starting", g.next())
        self.assertRaises(UnboundLocalError, g.throw, UnboundLocalError)
        self.assertRaises(StopIteration, g.next)
        self.assertEquals({"finally": 1}, obj)

    # Getting this to work under -j always is a pain in the ass, and not worth
    # the effort IMHO.
    if _llvm.get_jit_control() != "always":
        def test_toggle_generator(self):
            # Toggling between native code and the interpreter between yields
            # used to cause crashes because f_lasti doesn't get translated
            # between the scheme used for LLVM and the scheme used for the
            # interpreter. For now these assignments are no-ops for the lifetime
            # of the generator object, but do take effect when a new generator
            # instance is created.
            def generator(obj):
                yield 1
                generator.func_code.__use_llvm__ = True
                yield 2
                # We iterate over the generator while the first instance is
                # still running. This is to test that the modification to the
                # shared code object above takes effect. We don't have any way
                # of checking whether LLVM is really being used, but the
                # important thing is that it doesn't crash.
                list(obj)
                generator.func_code.__use_llvm__ = False
                yield 3
            self.assertEqual(list(generator(generator([]))), [1, 2, 3])

    @at_each_optimization_level
    def test_closure(self, level):
        make_closure = compile_for_llvm('make_closure', '''
def make_closure(a, level):
    b = 5
    c = 3
    def inner(d, e=5):
        c = d + 1
        return a, b, c, d, e
    if level is not None:
        inner.__code__.__use_llvm__ = True
        inner.__code__.co_optimization = level
    b = 2
    return inner
''', level)
        inner = make_closure(1, level)
        self.assertEquals(inner(4), (1, 2, 5, 4, 5))
        self.assertRaises(TypeError, inner, "5")

    @at_each_optimization_level
    def test_closure_unbound_freevar(self, level):
        unbound_freevar = compile_for_llvm('unbound_freevar', '''
def unbound_freevar(level):
    if 0:
        b = 2
    def inner():
        return b
    if level is not None:
        inner.__code__.__use_llvm__ = True
        inner.__code__.co_optimization = level
    return inner
''', level)
        inner = unbound_freevar(level)
        self.assertRaisesWithArgs(NameError,
            ("free variable 'b' referenced before "
             "assignment in enclosing scope",), inner)

    @at_each_optimization_level
    def test_closure_unbound_local(self, level):
        unbound_local = compile_for_llvm('unbound_local', '''
def unbound_local(level):
    def inner():
        if 0:
            b = 3
        return b
    if level is not None:
        inner.__code__.__use_llvm__ = True
        inner.__code__.co_optimization = level
    return inner
''', level)
        inner = unbound_local(level)
        self.assertRaisesWithArgs(UnboundLocalError,
            ("local variable 'b' referenced before assignment",), inner)

    @at_each_optimization_level
    def test_ends_with_unconditional_jump(self, level):
        foo = compile_for_llvm('foo', '''
from opcode import opmap
from types import CodeType, FunctionType
foo_bytecode = [
    opmap["JUMP_FORWARD"],  4, 0,  #  0  JUMP_FORWARD   4 (to 7)
    opmap["LOAD_CONST"],    0, 0,  #  3  LOAD_CONST     0 (1)
    opmap["RETURN_VALUE"],         #  6  RETURN_VALUE
    opmap["JUMP_ABSOLUTE"], 3, 0,  #  7  JUMP_ABSOLUTE  3
]
foo_code = CodeType(0, 0, 1, 0, "".join(chr(x) for x in foo_bytecode),
                    (1,), (), (), "<string>", "foo", 1, "")
foo = FunctionType(foo_code, globals())
''', level)
        self.assertEquals(1, foo())


class LoopExceptionInteractionTests(LlvmTestCase):
    @at_each_optimization_level
    def test_except_through_loop_caught(self, level):
        nested = compile_for_llvm('nested', '''
def nested(lst, obj):
    try:
        for x in lst:
            raise UnboundLocalError
    except:
        obj["x"] = 2
    # Make sure the block stack is ok.
    try:
        for x in lst:
            return x
    finally:
        obj["y"] = 3
''', level)
        obj = {}
        self.assertEquals(1, nested([1,2,3], obj))
        self.assertEquals({"x": 2, "y": 3}, obj)

    @at_each_optimization_level
    def test_except_in_loop(self, level):
        nested = compile_for_llvm('nested', '''
def nested(lst, obj):
    try:
        for x in lst:
            try:
                a = a
            except ZeroDivisionError:
                obj["x"] = 2
    except UnboundLocalError:
        obj["z"] = 4
    # Make sure the block stack is ok.
    try:
        for x in lst:
            return x
    finally:
        obj["y"] = 3
''', level)
        obj = {}
        self.assertEquals(1, nested([1,2,3], obj))
        self.assertEquals({"z": 4, "y": 3}, obj)

    @at_each_optimization_level
    def test_except_through_loop_finally(self, level):
        nested = compile_for_llvm('nested', '''
def nested(lst, obj):
    try:
        for x in lst:
            a = a
    finally:
        obj["x"] = 2
''', level)
        obj = {}
        self.assertRaises(UnboundLocalError, nested, [1,2,3], obj)
        self.assertEquals({"x": 2}, obj)

    @at_each_optimization_level
    def test_break_in_try(self, level):
        break_one = compile_for_llvm("break_one", """
def break_one(x):
    for y in [1, 2]:
        try:
            x["break"] = y
            break
            x["post break"] = y
        except ZeroDivisionError:
            x["except"] = 77
        finally:
            x["finally"] = y
    else:
        x["else"] = True

    # Make sure the block stack is ok.
    try:
        1 / 0
    except ZeroDivisionError:
        x["except"] = ZeroDivisionError
    return x
""", level)
        self.assertEqual(break_one({}), {"break": 1, "finally": 1,
                                         "except": ZeroDivisionError})


def cause_bail_on_next_line():
    sys.settrace(lambda *args: None)


class BailoutTests(ExtraAssertsTestCase):
    @at_each_optimization_level
    def test_bail_inside_loop(self, level):
        # We had a bug where the block stack in the compiled version
        # of the below function used contiguous small integers to
        # identify block handlers.  When we bailed out to the
        # interpreter, it expected an handler's identifier to be the
        # index of the opcode that started the code for the handler.
        # This mismatch caused a crash.
        loop = compile_for_llvm("loop", """
def loop():
    for i in [1, 2]:
        # Use try/finally to get "break" to emit a BREAK_LOOP opcode
        # instead of just jumping out of the loop.
        try:
            cause_bail_on_next_line()
            break
        finally:
            pass
    return i
""", level)
        orig_trace = sys.gettrace()
        try:
            self.assertEquals(loop(), 1)
        finally:
            sys.settrace(orig_trace)

    def test_use_correct_unwind_reason_when_bailing(self):
        # Previously the LLVM-generated code and the eval loop were using
        # different integers to indicate why we're popping the Python stack. In
        # most cases these integers were invisible to the other side (eval loop
        # vs machine code), but if we bail while unwinding the stack, the
        # integers used by the machine code are exposed to the eval loop.
        #
        # Here's what's going on (use the dis module to follow along at home):
        # 1. try: compiles to a SETUP_FINALLY that registers "len([])" as
        #    the finally: handler.
        # 2. "cause_bail()" will flip the thread-local tracing flags.
        # 3. The "return" part of "return cause_bail()" sets the return value
        #    and stack unwinding reason (UNWIND_RETURN), then jumps to the
        #    unwinder.
        # 4. The stack unwinder does its thing: it pushes the return value and
        #    the unwinding reason (UNWIND_RETURN) onto the stack in that order,
        #    then jumps to the finally: handler that SETUP_FINALLY registered
        #    ("len([])").
        # 5. LLVM emits a per-line prologue that checks the thread-local
        #    tracing flag, and if that flag is true, bailing to the interpreter.
        # 6. At this point, the LLVM version of UNWIND_RETURN is on top of the
        #    Python stack. The eval loop will now continue where the native code
        #    left off, executing len([]).
        # 7. The END_FINALLY opcode that terminates the finally block kicks in
        #    and starts trying to handle pending exceptions or return values. It
        #    knows what to do by matching the top of the stack (the unwind
        #    reason) against its own list of reasons. When the eval loop and
        #    native code disagreed about the numerical values for these reasons,
        #    LLVM's UNWIND_RETURN was the eval loop's UNWIND_EXCEPTION. This
        #    would trigger a debugging assert in the eval loop, since
        #    PyErr_Occurred() indicated that indeed *no* exception was live.
        sys.setbailerror(False)
        def cause_bail():
            sys.settrace(lambda *args: None)

        foo = compile_for_llvm("foo", """
def foo():
    try:
        return cause_bail()
    finally:
        len([])  # This can be anything.
""", optimization_level=None, globals_dict={"cause_bail": cause_bail})
        foo.__code__.__use_llvm__ = True

        orig_func = sys.gettrace()
        try:
            foo()
        finally:
            sys.settrace(orig_func)
        # Even though we bailed, the machine code is still valid.
        self.assertTrue(foo.__code__.__use_llvm__)


# Tests for div/truediv won't work right if we enable true
# division in this test.
assert 1/2 == 0, "Do not run test_llvm with -Qnew"

class Operand(object):
    """Helper class for testing operations."""
    # Regular binary arithmetic operations.
    def __add__(self, other):
        return ('add', other)
    def __sub__(self, other):
        return ('sub', other)
    def __mul__(self, other):
        return ('mul', other)
    def __div__(self, other):
        return ('div', other)
    def __truediv__(self, other):
        return ('truediv', other)
    def __floordiv__(self, other):
        return ('floordiv', other)
    def __mod__(self, other):
        return ('mod', other)
    def __pow__(self, other):
        return ('pow', other)
    def __lshift__(self, other):
        return ('lshift', other)
    def __rshift__(self, other):
        return ('rshift', other)
    def __and__(self, other):
        return ('and', other)
    def __or__(self, other):
        return ('or', other)
    def __xor__(self, other):
        return ('xor', other)

    # Unary operations.
    def __invert__(self):
        return ('invert')
    def __pos__(self):
        return ('pos')
    def __neg__(self):
        return ('neg')
    def __repr__(self):
        return ('repr')

    # Right-hand binary arithmetic operations.
    def __radd__(self, other):
        return ('radd', other)
    def __rsub__(self, other):
        return ('rsub', other)
    def __rmul__(self, other):
        return ('rmul', other)
    def __rdiv__(self, other):
        return ('rdiv', other)
    def __rtruediv__(self, other):
        return ('rtruediv', other)
    def __rfloordiv__(self, other):
        return ('rfloordiv', other)
    def __rmod__(self, other):
        return ('rmod', other)
    def __rpow__(self, other):
        return ('rpow', other)
    def __rlshift__(self, other):
        return ('rlshift', other)
    def __rrshift__(self, other):
        return ('rrshift', other)
    def __rand__(self, other):
        return ('rand', other)
    def __ror__(self, other):
        return ('ror', other)
    def __rxor__(self, other):
        return ('rxor', other)

    # In-place binary arithmetic operations.
    def __iadd__(self, other):
        return ('iadd', other)
    def __isub__(self, other):
        return ('isub', other)
    def __imul__(self, other):
        return ('imul', other)
    def __idiv__(self, other):
        return ('idiv', other)
    def __itruediv__(self, other):
        return ('itruediv', other)
    def __ifloordiv__(self, other):
        return ('ifloordiv', other)
    def __imod__(self, other):
        return ('imod', other)
    def __ipow__(self, other):
        return ('ipow', other)
    def __ilshift__(self, other):
        return ('ilshift', other)
    def __irshift__(self, other):
        return ('irshift', other)
    def __iand__(self, other):
        return ('iand', other)
    def __ior__(self, other):
        return ('ior', other)
    def __ixor__(self, other):
        return ('ixor', other)

    # Comparisons.
    def __cmp__(self, other):
        return ('cmp', other)
    def __eq__(self, other):
        return ('eq', other)
    def __ne__(self, other):
        return ('ne', other)
    def __lt__(self, other):
        return ('lt', other)
    def __le__(self, other):
        return ('le', other)
    def __gt__(self, other):
        return ('gt', other)
    def __ge__(self, other):
        return ('ge', other)

    # Misc operations.
    def __getitem__(self, item):
        return ('getitem', item)
    def __getslice__(self, start, stop):
        return ('getslice', start, stop)


class RecordingOperand(object):
    """Helper class for testing operations that can't return messages"""
    def __init__(self, value=None):
        self._ops = []
        self._value = value
    def __add__(self, other):
        self._ops.append(('add', other))
        return ('add', other)
    def __contains__(self, other):
        self._ops.append(('contains', other))
        return other in self._value
    def __getitem__(self, index):
        self._ops.append(('getitem', index))
        return self._value
    def __setitem__(self, item, value):
        self._ops.append(('setitem', item, value))
    def __delitem__(self, item):
        self._ops.append(('delitem', item))
    def __nonzero__(self):
        self._ops.append('nonzero')
        return bool(self._value)
    def __getslice__(self, start, stop):
        operation = ('getslice', start, stop)
        self._ops.append(operation)
        return operation
    def __setslice__(self, start, stop, seq):
        self._ops.append(('setslice', start, stop, seq))
    def __delslice__(self, start, stop):
        self._ops.append(('delslice', start, stop))


class OpExc(Exception):
    pass


class RaisingOperand(object):
    # Regular binary arithmetic operations.
    def __add__(self, other):
        raise OpExc('add', other)
    def __sub__(self, other):
        raise OpExc('sub', other)
    def __mul__(self, other):
        raise OpExc('mul', other)
    def __div__(self, other):
        raise OpExc('div', other)
    def __truediv__(self, other):
        raise OpExc('truediv', other)
    def __floordiv__(self, other):
        raise OpExc('floordiv', other)
    def __mod__(self, other):
        raise OpExc('mod', other)
    def __pow__(self, other):
        raise OpExc('pow', other)
    def __lshift__(self, other):
        raise OpExc('lshift', other)
    def __rshift__(self, other):
        raise OpExc('rshift', other)
    def __and__(self, other):
        raise OpExc('and', other)
    def __or__(self, other):
        raise OpExc('or', other)
    def __xor__(self, other):
        raise OpExc('xor', other)

    # Unary operations,
    def __nonzero__(self):
        raise OpExc('nonzero')
    def __invert__(self):
        raise OpExc('invert')
    def __pos__(self):
        raise OpExc('pos')
    def __neg__(self):
        raise OpExc('neg')
    def __repr__(self):
        raise OpExc('repr')

    # right-hand binary arithmetic operations.
    def __radd__(self, other):
        raise OpExc('radd', other)
    def __rsub__(self, other):
        raise OpExc('rsub', other)
    def __rmul__(self, other):
        raise OpExc('rmul', other)
    def __rdiv__(self, other):
        raise OpExc('rdiv', other)
    def __rtruediv__(self, other):
        raise OpExc('rtruediv', other)
    def __rfloordiv__(self, other):
        raise OpExc('rfloordiv', other)
    def __rmod__(self, other):
        raise OpExc('rmod', other)
    def __rpow__(self, other):
        raise OpExc('rpow', other)
    def __rlshift__(self, other):
        raise OpExc('rlshift', other)
    def __rrshift__(self, other):
        raise OpExc('rrshift', other)
    def __rand__(self, other):
        raise OpExc('rand', other)
    def __ror__(self, other):
        raise OpExc('ror', other)
    def __rxor__(self, other):
        raise OpExc('rxor', other)

    # In-place binary arithmetic operations.
    def __iadd__(self, other):
        raise OpExc('iadd', other)
    def __isub__(self, other):
        raise OpExc('isub', other)
    def __imul__(self, other):
        raise OpExc('imul', other)
    def __idiv__(self, other):
        raise OpExc('idiv', other)
    def __itruediv__(self, other):
        raise OpExc('itruediv', other)
    def __ifloordiv__(self, other):
        raise OpExc('ifloordiv', other)
    def __imod__(self, other):
        raise OpExc('imod', other)
    def __ipow__(self, other):
        raise OpExc('ipow', other)
    def __ilshift__(self, other):
        raise OpExc('ilshift', other)
    def __irshift__(self, other):
        raise OpExc('irshift', other)
    def __iand__(self, other):
        raise OpExc('iand', other)
    def __ior__(self, other):
        raise OpExc('ior', other)
    def __ixor__(self, other):
        raise OpExc('ixor', other)

    # Comparison.
    def __cmp__(self, other):
        raise OpExc('cmp', other)
    def __eq__(self, other):
        raise OpExc('eq', other)
    def __ne__(self, other):
        raise OpExc('ne', other)
    def __lt__(self, other):
        raise OpExc('lt', other)
    def __le__(self, other):
        raise OpExc('le', other)
    def __gt__(self,other):
        raise OpExc('gt', other)
    def __ge__(self, other):
        raise OpExc('ge', other)
    def __contains__(self, other):
        raise OpExc('contains', other)

    # Indexing.
    def __getitem__(self, item):
        raise OpExc('getitem', item)
    def __setitem__(self, item, value):
        raise OpExc('setitem', item, value)
    def __delitem__(self, item):
        raise OpExc('delitem', item)

    # Classic slices
    def __getslice__(self, start, stop):
        raise OpExc('getslice', start, stop)
    def __setslice__(self, start, stop, seq):
        raise OpExc('setslice', start, stop, seq)
    def __delslice__(self, start, stop):
        raise OpExc('delslice', start, stop)


class OperatorTests(ExtraAssertsTestCase, LlvmTestCase):
    @at_each_optimization_level
    def test_basic_arithmetic(self, level):
        operators = {
            '+': 'add',
            '-': 'sub',
            '*': 'mul',
            '/': 'div',
            '//': 'floordiv',
            '%': 'mod',
            '**': 'pow',
            '<<': 'lshift',
            '>>': 'rshift',
            '&': 'and',
            '|': 'or',
            '^': 'xor'}
        for op, method in operators.items():
            normal = compile_for_llvm('normal', '''
def normal(x):
    return x %s 1
''' % op, level)
            self.assertEquals(normal(Operand()), (method, 1))
            self.assertRaisesWithArgs(OpExc, (method, 1),
                                      normal, RaisingOperand())

            righthand = compile_for_llvm('righthand', '''
def righthand(x):
    return 2 %s x
''' % op, level)
            self.assertEquals(righthand(Operand()), ('r' + method, 2))
            self.assertRaisesWithArgs(OpExc, ('r' + method, 2),
                                      righthand, RaisingOperand())

            inplace = compile_for_llvm('inplace', '''
def inplace(x):
    x %s= 3
    return x
''' % op, level)
            self.assertEquals(inplace(Operand()), ('i' + method, 3))
            self.assertRaisesWithArgs(OpExc, ('i' + method, 3),
                                      inplace, RaisingOperand())

    @at_each_optimization_level
    def test_truediv(self, level):
        div_code = compile('''
def div(x):
    return x / 1
''', 'div_code', 'exec', flags=__future__.division.compiler_flag)
        div = compile_for_llvm('div', div_code, level)
        self.assertEquals(div(Operand()), ('truediv', 1))
        self.assertRaisesWithArgs(OpExc, ('truediv', 1),
                                  div, RaisingOperand())

        rdiv_code = compile('''
def rdiv(x):
    return 2 / x
''', 'rdiv_code', 'exec', flags=__future__.division.compiler_flag)
        rdiv = compile_for_llvm('rdiv', rdiv_code, level)
        self.assertEquals(rdiv(Operand()), ('rtruediv', 2))
        self.assertRaisesWithArgs(OpExc, ('rtruediv', 2),
                                  rdiv, RaisingOperand())

        idiv_code = compile('''
def idiv(x):
    x /= 3;
    return x
''', 'idiv_code', 'exec', flags=__future__.division.compiler_flag)
        idiv = compile_for_llvm('idiv', idiv_code, level)
        self.assertEquals(idiv(Operand()), ('itruediv', 3))
        self.assertRaisesWithArgs(OpExc, ('itruediv', 3),
                                  idiv, RaisingOperand())

    @at_each_optimization_level
    def test_subscr(self, level):
        subscr = compile_for_llvm('subscr',
                                  'def subscr(x): return x["item"]',
                                  level)
        self.assertEquals(subscr(Operand()), ('getitem', 'item'))
        self.assertRaisesWithArgs(OpExc, ('getitem', 'item'),
                                  subscr, RaisingOperand())

    @at_each_optimization_level
    def test_store_subscr(self, level):
        store_subscr = compile_for_llvm('store_subscr', '''
def store_subscr(x):
    x['item'] = 4
    return x
''', level)
        self.assertEquals(store_subscr(RecordingOperand())._ops,
                          [('setitem', 'item', 4)])
        self.assertRaisesWithArgs(OpExc, ('setitem', 'item', 4),
                                  store_subscr, RaisingOperand())

    @at_each_optimization_level
    def test_subscr_augassign(self, level):
        subscr_augassign = compile_for_llvm('subscr_augassign', '''
def subscr_augassign(x):
    x[0] += 2
    return x
''', level)
        self.assertEquals(subscr_augassign(RecordingOperand(3))._ops,
                          [('getitem', 0), ('setitem', 0, 5)])
        # Test getitem raising an exception
        self.assertRaisesWithArgs(OpExc, ('getitem', 0),
                                  subscr_augassign, RaisingOperand())
        # Test iadd raising an exception.
        self.assertRaisesWithArgs(OpExc, ('iadd', 2),
                                  subscr_augassign, [RaisingOperand()])
        # Test setitem raising an exception
        class SetitemRaisingOperand(RaisingOperand):
            def __getitem__(self, item):
                return 5
        self.assertRaisesWithArgs(OpExc, ('setitem', 0, 7),
                                  subscr_augassign, SetitemRaisingOperand())

    @at_each_optimization_level
    def test_invert(self, level):
        invert = compile_for_llvm('invert',
                                  'def invert(x): return ~x', level)
        self.assertEquals(invert(Operand()), 'invert')
        self.assertRaisesWithArgs(OpExc, ('invert',),
                                  invert, RaisingOperand())

    @at_each_optimization_level
    def test_pos(self, level):
        pos = compile_for_llvm('pos', 'def pos(x): return +x', level)
        self.assertEquals(pos(Operand()), 'pos')
        self.assertRaisesWithArgs(OpExc, ('pos',),
                                  pos, RaisingOperand())

    @at_each_optimization_level
    def test_neg(self, level):
        neg = compile_for_llvm('neg', 'def neg(x): return -x', level)
        self.assertEquals(neg(Operand()), 'neg')
        self.assertRaisesWithArgs(OpExc, ('neg',),
                                  neg, RaisingOperand())

    @at_each_optimization_level
    def test_convert(self, level):
        convert = compile_for_llvm('convert',
                                  'def convert(x): return `x`', level)
        self.assertEquals(convert(Operand()), 'repr')
        self.assertRaisesWithArgs(OpExc, ('repr',),
                                  convert, RaisingOperand())

    @at_each_optimization_level
    def test_not(self, level):
        not_ = compile_for_llvm('not_', '''
def not_(x):
    y = not x
    return x
''', level)
        self.assertEquals(not_(RecordingOperand())._ops, ['nonzero'])
        self.assertRaisesWithArgs(OpExc, ('nonzero',),
                                  not_, RaisingOperand())

    @at_each_optimization_level
    def test_slice_none(self, level):
        getslice_none = compile_for_llvm('getslice_none',
                                         'def getslice_none(x): return x[:]',
                                         level)
        self.assertEquals(getslice_none(Operand()),
                          ('getslice', 0, sys.maxint))
        self.assertRaisesWithArgs(OpExc, ('getslice', 0, sys.maxint),
                                  getslice_none, RaisingOperand())

        setslice_none = compile_for_llvm('setslice_none', '''
def setslice_none(x):
    x[:] = [0]
    return x
''', level)
        self.assertEquals(setslice_none(RecordingOperand())._ops,
                          [('setslice', 0, sys.maxint, [0])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 0, sys.maxint, [0]),
                                  setslice_none, RaisingOperand())

        delslice_none = compile_for_llvm('delslice_none', '''
def delslice_none(x):
    del x[:]
    return x
''', level)
        self.assertEquals(delslice_none(RecordingOperand())._ops,
                          [('delslice', 0, sys.maxint)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 0, sys.maxint),
                                  delslice_none, RaisingOperand())

        augassign_none = compile_for_llvm('augassign_none', '''
def augassign_none(x):
    x[:] += (0,)
    return x
''', level)
        self.assertEquals(augassign_none(RecordingOperand())._ops, [
            # The result of op.__getslice__(0, sys.maxint), and ..
            ('getslice', 0, sys.maxint),
            # ... the result of op.__setslice__(0, sys.maxint, seq) ..
            ('setslice', 0, sys.maxint,
             # .. with seq being op.__getslice__(0, sys.maxint) + (0,)
             ('getslice', 0, sys.maxint, 0))])

    @at_each_optimization_level
    def test_slice_left(self, level):
        getslice_left = compile_for_llvm('getslice_left', '''
def getslice_left(x, y):
    return x[y:]
''', level)
        self.assertEquals(getslice_left(Operand(), 5),
                          ('getslice', 5, sys.maxint))
        self.assertRaisesWithArgs(OpExc, ('getslice', 5, sys.maxint),
                                  getslice_left, RaisingOperand(), 5)

        setslice_left = compile_for_llvm('setslice_left', '''
def setslice_left(x, y):
    x[y:] = [1]
    return x
''', level)
        self.assertEquals(setslice_left(RecordingOperand(), 5)._ops,
                          [('setslice', 5, sys.maxint, [1])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 5, sys.maxint, [1]),
                                  setslice_left, RaisingOperand(), 5)

        delslice_left = compile_for_llvm('delslice_left', '''
def delslice_left(x, y):
    del x[y:]
    return x
''', level)
        self.assertEquals(delslice_left(RecordingOperand(), 5)._ops,
                          [('delslice', 5, sys.maxint)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 5, sys.maxint),
                                  delslice_left, RaisingOperand(), 5)

        augassign_left = compile_for_llvm('augassign_left', '''
def augassign_left(x, y):
    x[y:] += (1,)
    return x
''', level)
        self.assertEquals(augassign_left(RecordingOperand(), 2)._ops, [
            # The result of op.__getslice__(2, sys.maxint), and ..
            ('getslice', 2, sys.maxint),
            # ... the result of op.__setslice__(2, sys.maxint, seq) ..
            ('setslice', 2, sys.maxint,
             # .. with seq being op.__getslice__(2, sys.maxint) + (1,)
             ('getslice', 2, sys.maxint, 1))])

    @at_each_optimization_level
    def test_slice_right(self, level):
        getslice_right = compile_for_llvm('getslice_right', '''
def getslice_right(x, y):
    return x[:y]
''', level)
        self.assertEquals(getslice_right(Operand(), 10),
                          ('getslice', 0, 10))
        self.assertRaisesWithArgs(OpExc, ('getslice', 0, 10),
                                  getslice_right, RaisingOperand(), 10)

        setslice_right = compile_for_llvm('setslice_right', '''
def setslice_right(x, y):
    x[:y] = [2]
    return x
''', level)
        self.assertEquals(setslice_right(RecordingOperand(), 10)._ops,
                          [('setslice', 0, 10, [2])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 0, 10, [2]),
                                  setslice_right, RaisingOperand(), 10)

        delslice_right = compile_for_llvm('delslice_right', '''
def delslice_right(x, y):
    del x[:y]
    return x
''', level)
        self.assertEquals(delslice_right(RecordingOperand(), 10)._ops,
                          [('delslice', 0, 10)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 0, 10),
                                  delslice_right, RaisingOperand(), 10)

        augassign_right = compile_for_llvm('augassign_right', '''
def augassign_right(x, y):
    x[:y] += (2,)
    return x
''', level)
        self.assertEquals(augassign_right(RecordingOperand(), 1)._ops, [
            # The result of op.__getslice__(0, 1), and ..
            ('getslice', 0, 1),
            # ... the result of op.__setslice__(0, 1, seq) ..
            ('setslice', 0, 1,
             # .. with seq being op.__getslice__(0, 1) + (2,)
             ('getslice', 0, 1, 2))])

    @at_each_optimization_level
    def test_slice_both(self, level):
        getslice_both = compile_for_llvm('getslice_both', '''
def getslice_both(x, y, z):
    return x[y:z]
''', level)
        self.assertEquals(getslice_both(Operand(), 4, -6),
                          ('getslice', 4, -6))
        self.assertRaisesWithArgs(OpExc, ('getslice', 4, -6),
                                  getslice_both, RaisingOperand(), 4, -6)

        setslice_both = compile_for_llvm('setslice_both', '''
def setslice_both(x, y, z):
    x[y:z] = [3]
    return x
''', level)
        self.assertEquals(setslice_both(RecordingOperand(), 4, -6)._ops,
                          [('setslice', 4, -6, [3])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 4, -6, [3]),
                                  setslice_both, RaisingOperand(), 4, -6)

        delslice_both = compile_for_llvm('delslice_both', '''
def delslice_both(x, y, z):
    del x[y:z]
    return x
''', level)
        self.assertEquals(delslice_both(RecordingOperand(), 4, -6)._ops,
                          [('delslice', 4, -6)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 4, -6),
                                  delslice_both, RaisingOperand(), 4, -6)

        augassign_both = compile_for_llvm('augassign_both', '''
def augassign_both(x, y, z):
    x[y:z] += (3,)
    return x
''', level)
        self.assertEquals(augassign_both(RecordingOperand(), 1, 2)._ops, [
            # The result of op.__getslice__(1, 2), and ..
            ('getslice', 1, 2),
            # ... the result of op.__setslice__(1, 2, seq) ..
            ('setslice', 1, 2,
             # .. with seq being op.__getslice__(1, 2) + (3,)
             ('getslice', 1, 2, 3))])

    @at_each_optimization_level
    def test_is(self, level):
        is_ = compile_for_llvm('is_', 'def is_(x, y): return x is y', level)
        # Don't rely on Python making separate literal 1's the same object.
        one = 1
        self.assertTrue(is_(one, one))
        self.assertFalse(is_(2, 3))

    @at_each_optimization_level
    def test_is_not(self, level):
        is_not = compile_for_llvm('is_not',
                                  'def is_not(x, y): return x is not y',
                                  level)
        # Don't rely on Python making separate literal 1's the same object.
        one = 1
        self.assertFalse(is_not(one, one))
        self.assertTrue(is_not(2, 3))

    @at_each_optimization_level
    def test_eq(self, level):
        eq = compile_for_llvm('eq', 'def eq(x, y): return x == y', level)
        self.assertEquals(eq(Operand(), 6), ('eq', 6))
        self.assertEquals(eq(7, Operand()), ('eq', 7))
        self.assertRaisesWithArgs(OpExc, ('eq', 1), eq, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('eq', 1), eq, 1, RaisingOperand())

    @at_each_optimization_level
    def test_ne(self, level):
        ne = compile_for_llvm('ne', 'def ne(x, y): return x != y', level)
        self.assertEquals(ne(Operand(), 6), ('ne', 6))
        self.assertEquals(ne(7, Operand()), ('ne', 7))
        self.assertRaisesWithArgs(OpExc, ('ne', 1), ne, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('ne', 1), ne, 1, RaisingOperand())

    @at_each_optimization_level
    def test_lt(self, level):
        lt = compile_for_llvm('lt', 'def lt(x, y): return x < y', level)
        self.assertEquals(lt(Operand(), 6), ('lt', 6))
        self.assertEquals(lt(7, Operand()), ('gt', 7))
        self.assertRaisesWithArgs(OpExc, ('lt', 1), lt, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('gt', 1), lt, 1, RaisingOperand())

    @at_each_optimization_level
    def test_le(self, level):
        le = compile_for_llvm('le', 'def le(x, y): return x <= y', level)
        self.assertEquals(le(Operand(), 6), ('le', 6))
        self.assertEquals(le(7, Operand()), ('ge', 7))
        self.assertRaisesWithArgs(OpExc, ('le', 1), le, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('ge', 1), le, 1, RaisingOperand())

    @at_each_optimization_level
    def test_gt(self, level):
        gt = compile_for_llvm('gt', 'def gt(x, y): return x > y', level)
        self.assertEquals(gt(Operand(), 6), ('gt', 6))
        self.assertEquals(gt(7, Operand()), ('lt', 7))
        self.assertRaisesWithArgs(OpExc, ('gt', 1), gt, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('lt', 1), gt, 1, RaisingOperand())

    @at_each_optimization_level
    def test_ge(self, level):
        ge = compile_for_llvm('ge', 'def ge(x, y): return x >= y', level)
        self.assertEquals(ge(Operand(), 6), ('ge', 6))
        self.assertEquals(ge(7, Operand()), ('le', 7))
        self.assertRaisesWithArgs(OpExc, ('ge', 1), ge, RaisingOperand(), 1)
        self.assertRaisesWithArgs(OpExc, ('le', 1), ge, 1, RaisingOperand())

    @at_each_optimization_level
    def test_in(self, level):
        in_ = compile_for_llvm('in_', 'def in_(x, y): return x in y', level)
        self.assertTrue(in_(1, [1, 2]))
        self.assertFalse(in_(1, [0, 2]))
        op = RecordingOperand([1])
        self.assertTrue(in_(1, op))
        self.assertEquals(op._ops, [('contains', 1)])
        self.assertRaisesWithArgs(OpExc, ('contains', 1),
                                     in_, 1, RaisingOperand())

    @at_each_optimization_level
    def test_not_in(self, level):
        not_in = compile_for_llvm('not_in',
                                  'def not_in(x, y): return x not in y',
                                  level)
        self.assertFalse(not_in(1, [1, 2]))
        self.assertTrue(not_in(1, [0, 2]))
        op = RecordingOperand([])
        self.assertTrue(not_in(1, op))
        self.assertEquals(op._ops, [('contains', 1)])
        self.assertRaisesWithArgs(OpExc, ('contains', 1),
                                   not_in, 1, RaisingOperand())

    @at_each_optimization_level
    def test_listcomp(self, level):
        listcomp = compile_for_llvm('listcomp', '''
def listcomp(x):
    return [ item + 1 for item in x ]
''', level)
        self.assertEquals(listcomp([1, 2, 3]), [2, 3, 4])

        listcomp_exc = compile_for_llvm('listcomp_exc', '''
def listcomp_exc(x):
    return [ item + 5 for item in x ]
''', level)
        op = RecordingOperand()
        self.assertRaisesWithArgs(OpExc, ('add', 5),
                                  listcomp_exc, [op, RaisingOperand(), op])
        # Test that the last Operand wasn't touched, and we didn't
        # leak references.
        self.assertEquals(op._ops, [('add', 5)])


class LiteralsTests(LlvmTestCase):
    @at_each_optimization_level
    def test_build_tuple(self, level):
        t1 = compile_for_llvm('t1', 'def t1(): return (1, 2, 3)', level)
        self.assertEquals(t1(), (1, 2, 3))
        t2 = compile_for_llvm('t2', 'def t2(x): return (1, x + 1, 3)', level)
        self.assertEquals(t2(1), (1, 2, 3))
        self.assertRaises(TypeError, t2, "1")
        t3 = compile_for_llvm('t3',
                              'def t3(x): return ([1], x, (3, x + 1), 2, 1)',
                              level)
        self.assertEquals(t3(2), ([1], 2, (3, 3), 2, 1))
        self.assertRaises(TypeError, t3, "2")

    @at_each_optimization_level
    def test_unpack_tuple(self, level):
        unpack = compile_for_llvm('unpack', '''
def unpack(x):
    a, b, (c, d) = x
    return (a, b, c, d)
''', level)
        self.assertEquals(unpack((1, 2, (3, 4))), (1, 2, 3, 4))
        self.assertRaises(TypeError, unpack, None)
        self.assertRaises(ValueError, unpack, (1, 2, (3, 4, 5)))
        self.assertRaises(ValueError, unpack, (1, 2))

    @at_each_optimization_level
    def test_build_list(self, level):
        l1 = compile_for_llvm('l1', 'def l1(): return [1, 2, 3]', level)
        self.assertEquals(l1(), [1, 2, 3])
        l2 = compile_for_llvm('l2', 'def l2(x): return [1, x + 1, 3]', level)
        self.assertEquals(l2(1), [1, 2, 3])
        self.assertRaises(TypeError, l2, "1")
        l3 = compile_for_llvm('l3',
                              'def l3(x): return [(1,), x, [3, x + 1], 2, 1]',
                              level)
        self.assertEquals(l3(2), [(1,), 2, [3, 3], 2, 1])
        self.assertRaises(TypeError, l3, "2")

    @at_each_optimization_level
    def test_build_map(self, level):
        f1 = compile_for_llvm('f1', 'def f1(x): return {1: x, x + 1: 4}',
                              level)
        self.assertEquals(f1(2), {1: 2, 3: 4})
        self.assertRaises(TypeError, f1, '2')
        f2 = compile_for_llvm('f2', 'def f2(x): return {1: {x: 3}, x: 5}',
                              level)
        self.assertEquals(f2(2), {1: {2: 3}, 2: 5})
        self.assertRaises(TypeError, f2, {})


# These tests are skipped when -j never or -j always is passed to Python.
class OptimizationTests(LlvmTestCase, ExtraAssertsTestCase):

    def test_manual_optimization(self):
        foo = compile_for_llvm("foo", "def foo(): return 5",
                               optimization_level=None)
        foo.__code__.co_optimization = 2
        self.assertContains("getelementptr", str(foo.__code__.co_llvm))
        self.assertEqual(foo(), 5)

    def test_hotness(self):
        foo = compile_for_llvm("foo", "def foo(): pass",
                               optimization_level=None)
        iterations = JIT_SPIN_COUNT
        l = [foo() for _ in xrange(iterations)]
        self.assertEqual(foo.__code__.co_hotness, iterations * 10)
        self.assertEqual(foo.__code__.__use_llvm__, True)
        self.assertEqual(foo.__code__.co_optimization, JIT_OPT_LEVEL)
        self.assertContains("getelementptr", str(foo.__code__.co_llvm))

    def test_loop_hotness(self):
        # Test that long-running loops count toward the hotness metric. A
        # function doing 1e6 inner loop iterations per call should be worthy
        # of optimization.
        foo = compile_for_llvm("foo", """
def foo():
    for x in xrange(1000):
        for y in xrange(1000):
            pass
""", optimization_level=None)
        self.assertFalse(foo.__code__.__use_llvm__)
        foo()
        foo()  # Hot-or-not calculations are done on function-entry.
        self.assertTrue(foo.__code__.__use_llvm__)

    def test_generator_hotness(self):
        foo = compile_for_llvm("foo", """
def foo():
    yield 5
    yield 6
""", optimization_level=None)
        iterations = JIT_SPIN_COUNT
        l = [foo() for _ in xrange(iterations)]
        self.assertEqual(foo.__code__.co_hotness, iterations * 10)

        l = map(list, l)
        self.assertEqual(foo.__code__.co_hotness, iterations * 10)
        self.assertEqual(foo.__code__.__use_llvm__, True)
        self.assertEqual(foo.__code__.co_optimization, JIT_OPT_LEVEL)

    def test_fast_load_global(self):
        # Make sure that hot functions use the optimized LOAD_GLOBAL
        # implementation. We do this by asserting that if their assumptions
        # about globals/builtins no longer hold, they bail.
        with test_support.swap_attr(__builtin__, "len", len):
            iterations = JIT_SPIN_COUNT
            foo = compile_for_llvm("foo", """
def foo(x, callback):
    callback()
    return len(x)
""", optimization_level=None)

            for _ in xrange(iterations):
                foo([], lambda: None)
            self.assertEqual(foo.__code__.__use_llvm__, True)

            def change_builtins():
                self.assertEqual(foo.__code__.__use_llvm__, True)
                __builtin__.len = lambda x: 7

            def run_test():
                sys.setbailerror(True)
                self.assertEqual(foo.__code__.__use_llvm__, True)
                self.assertEqual(foo.__code__.co_fatalbailcount, 0)
                try:
                    self.assertRaises(RuntimeError, foo, [], change_builtins)
                finally:
                    sys.setbailerror(False)
                self.assertEqual(foo.__code__.co_fatalbailcount, 1)
                self.assertEqual(foo.__code__.__use_llvm__, False)
            run_test()

    def test_get_correct_globals(self):
        # Extracted from test_math.MathTests.testFsum. Trigger the compilation
        # of a hot function from another module; at one point in the
        # optimization of LOAD_GLOBAL, this caused errors because we were using
        # the wrong globals dictionary. We included it here as a simple
        # regression test.
        if "random" in sys.modules:
            del sys.modules["random"]
        from random import gauss  # A pure-Python function in another module.
        def foo():
            for _ in xrange(1000 * 200):
                v = gauss(0, 0.7) ** 7
        foo()
        self.assertEquals(gauss.__code__.__use_llvm__, True)

    def test_global_name_unknown_at_compilation_time(self):
        # Extracted from Sympy: the global `match` is unknown when foo() becomes
        # hot, but will be known by the time `trigger` has items. This used
        # to raise a NameError while compiling foo() to LLVM IR.
        #
        # The code-under-test is written like this to avoid conditional jumps,
        # which may contain their own guards.
        foo = compile_for_llvm("foo", """
def foo(trigger):
    for x in trigger:
        return match
    return 5
""", optimization_level=None)
        for _ in xrange(JIT_SPIN_COUNT):  # Spin foo() until it becomes hot.
            foo([])
        self.assertEquals(foo.__code__.__use_llvm__, True)
        self.assertEquals(foo([]), 5)

        # Set `match` so that we can run foo(True) and have it work correctly.
        global match
        match = 7
        self.assertEquals(foo([1]), 7)
        # Looking up `match` doesn't depend on any pointers cached in the IR,
        # so changing the globals didn't invalidate the code.
        self.assertEquals(foo.__code__.__use_llvm__, True)
        self.assertEquals(foo.__code__.co_fatalbailcount, 0)
        del match

    def test_setprofile_in_leaf_function(self):
        # Make sure that the fast version of CALL_FUNCTION supports profiling.
        data = []
        def record_profile(*args):
            data.append(args)

        def profiling_leaf():
            sys.setprofile(record_profile)

        def outer(leaf):
            [leaf(), len([])]

        # This will specialize the len() call in outer, but not the leaf() call,
        # since lambdas are created anew each time through the loop.
        for _ in xrange(JIT_SPIN_COUNT):
            outer(lambda: None)
        self.assertTrue(outer.__code__.__use_llvm__)
        sys.setbailerror(False)
        outer(profiling_leaf)
        sys.setprofile(None)

        len_event = data[1]
        # Slice off the frame object.
        self.assertEqual(len_event[1:], ("c_call", len))

    def test_fastcalls_bail_on_unknown_function(self):
        # If the function is different than the one that we've assumed, we
        # need to bail to the interpreter.
        def foo(f):
            return f([])

        for _ in xrange(JIT_SPIN_COUNT):
            foo(len)
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertRaises(RuntimeError, foo, lambda x: 7)

        # Make sure bailing does the right thing.
        self.assertTrue(foo.__code__.__use_llvm__)
        sys.setbailerror(False)
        self.assertEqual(foo(lambda x: 7), 7)

    def test_guard_failure_blocks_native_code(self):
        # Until we can recompile things, failing a guard should force use of the
        # eval loop forever after. Even once we can recompile things, we should
        # limit how often we're willing to recompile highly-dynamic functions.
        # test_mutants has a good example of this.

        # Compile like this so we get a new code object every time.
        foo = compile_for_llvm("foo", "def foo(): return len([])",
                               optimization_level=None)
        for _ in xrange(JIT_SPIN_COUNT):
            foo()
        self.assertEqual(foo.__code__.__use_llvm__, True)
        self.assertEqual(foo.__code__.co_fatalbailcount, 0)
        self.assertEqual(foo(), 0)

        with test_support.swap_attr(__builtin__, "len", lambda x: 7):
            self.assertEqual(foo.__code__.__use_llvm__, False)
            self.assertEqual(foo.__code__.co_fatalbailcount, 1)
            # Since we can't recompile things yet, __use_llvm__ should be left
            # at False and execution should use the eval loop.
            for _ in xrange(JIT_SPIN_COUNT):
                foo()
            self.assertEqual(foo.__code__.__use_llvm__, False)
            self.assertEqual(foo(), 7)

    def test_fast_calls_method(self):
        # This used to crash at one point while developing CALL_FUNCTION's
        # FDO-ified machine code. We include it here as a simple regression
        # test.
        d = dict.fromkeys(range(12000))
        foo = compile_for_llvm('foo', 'def foo(x): return x()',
                               optimization_level=None)
        for _ in xrange(JIT_SPIN_COUNT):
            foo(d.popitem)
        self.assertTrue(foo.__code__.__use_llvm__)

        k, v = foo(d.popitem)
        self.assertTrue(k < 12000, k)
        self.assertEqual(v, None)

    def test_fast_calls_same_method_different_invocant(self):
        # For all strings, x.join will resolve to the same C function, so
        # it should use the fast version of CALL_FUNCTION that calls the
        # function pointer directly.
        foo = compile_for_llvm('foo', 'def foo(x): return x.join(["c", "d"])',
                               optimization_level=None)
        for _ in xrange(JIT_SPIN_COUNT):
            foo("a")
            foo("c")
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo("a"), "cad")

        # A new, unknown-to-the-feedback-system instance should reuse the
        # same function, just with a different invocant.
        self.assertEqual(foo("b"), "cbd")

    @at_each_optimization_level
    def test_access_frame_locals_via_vars(self, level):
        # We need to be able to call vars() inside an LLVM-compiled function
        # and have it still work. This complicates some LLVM-side optimizations.
        foo = compile_for_llvm("foo", """
def foo(x):
    y = 7
    return vars()
""", optimization_level=level)

        got_vars = foo(8)
        self.assertEqual(got_vars, {"x": 8, "y": 7})

    @at_each_optimization_level
    def test_access_frame_locals_via_dir(self, level):
        # We need to be able to call dir() inside an LLVM-compiled function
        # and have it still work. This complicates some LLVM-side optimizations.
        foo = compile_for_llvm("foo", """
def foo(x):
    y = 7
    return dir()
""", optimization_level=level)

        got_dir = foo(8)
        self.assertEqual(set(got_dir), set(["x", "y"]))

    @at_each_optimization_level
    def test_access_frame_locals_via_locals(self, level):
        # We need to be able to call locals() inside an LLVM-compiled function
        # and have it still work. This complicates some LLVM-side optimizations.
        foo = compile_for_llvm("foo", """
def foo(x):
    z = 9
    y = 7
    del z
    return locals()
""", optimization_level=level)

        got_locals = foo(8)
        self.assertEqual(got_locals, {"x": 8, "y": 7})

    @at_each_optimization_level
    def test_access_frame_locals_via_traceback(self, level):
        # Some production code, like Django's fancy debugging pages, rely on
        # being able to pull locals out of frame objects. This complicates some
        # LLVM-side optimizations.
        foo = compile_for_llvm("foo", """
def foo(x):
    y = 7
    raise ZeroDivisionError
""", optimization_level=level)

        try:
            foo(8)
        except ZeroDivisionError:
            tb = sys.exc_info()[2]
        else:
            self.fail("Failed to raise ZeroDivisionError")

        # Sanity check to make sure we're getting the right frame.
        self.assertEqual(tb.tb_next.tb_frame.f_code.co_name, "foo")
        self.assertEqual(tb.tb_next.tb_frame.f_locals, {"y": 7, "x": 8})

    @at_each_optimization_level
    def test_access_frame_locals_in_finally_via_traceback(self, level):
        # Some production code, like Django's fancy debugging pages, rely on
        # being able to pull locals out of frame objects. This complicates some
        # LLVM-side optimizations. This particular case is a strange corner
        # case Jeffrey Yasskin thought up.
        foo = compile_for_llvm("foo", """
def foo(x):
    y = 7
    try:
        raise ZeroDivisionError
    finally:
        z = 9
""", optimization_level=level)

        try:
            foo(8)
        except ZeroDivisionError:
            tb = sys.exc_info()[2]
        else:
            self.fail("Failed to raise ZeroDivisionError")

        # Sanity check to make sure we're getting the right frame.
        self.assertEqual(tb.tb_next.tb_frame.f_code.co_name, "foo")
        self.assertEqual(tb.tb_next.tb_frame.f_locals, {"y": 7, "x": 8, "z": 9})

    def test_POP_JUMP_IF_FALSE_training_consistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches to a) reduce code size, b) give the optimizers less input.
        # We only do this if the branch is consistent (always 100%
        # taken/not-taken).
        foo = compile_for_llvm("foo", """
def foo(x):
    if x:
        return 7
    len([])
    return 8
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), 7)
        self.assertRaises(RuntimeError, foo, False)
        self.assertEqual(foo.__code__.co_fatalbailcount, 0)

        sys.setbailerror(False)
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), 8)

        # Make sure we didn't actually compile the untaken branch to LLVM IR.
        self.assertTrue("@len" not in str(foo.__code__.co_llvm))

    def test_POP_JUMP_IF_FALSE_training_inconsistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches. We can't do this, though, if the branch is inconsistent.
        foo = compile_for_llvm("foo", """
def foo(x):
    if x:
        return 7
    len([])
    return 8
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), 7)
        self.assertEqual(foo(False), 8)  # Does not raise RuntimeError

        # Make sure we compiled both branches to LLVM IR.
        self.assertContains("@len", str(foo.__code__.co_llvm))

    def test_POP_JUMP_IF_TRUE_training_consistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches to a) reduce code size, b) give the optimizers less input.
        # We only do this if the branch is consistent (always 100%
        # taken/not-taken).
        foo = compile_for_llvm("foo", """
def foo(x):
    if not x:
        return 7
    len([])
    return 8
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), 7)
        self.assertRaises(RuntimeError, foo, True)
        self.assertEqual(foo.__code__.co_fatalbailcount, 0)

        sys.setbailerror(False)
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), 8)

        # Make sure we didn't actually compile the untaken branch to LLVM IR.
        self.assertTrue("@len" not in str(foo.__code__.co_llvm))

    def test_POP_JUMP_IF_TRUE_training_inconsistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches. We can't do this, though, if the branch is inconsistent.
        foo = compile_for_llvm("foo", """
def foo(x):
    if not x:
        return 7
    len([])
    return 8
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), 7)
        self.assertEqual(foo(True), 8)  # Does not raise RuntimeError

        # Make sure we compiled both branches to LLVM IR.
        self.assertContains("@len", str(foo.__code__.co_llvm))

    def test_JUMP_IF_FALSE_OR_POP_training_consistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches to a) reduce code size, b) give the optimizers less input.
        # We only do this if the branch is consistent (always 100%
        # taken/not-taken).
        foo = compile_for_llvm("foo", """
def foo(x):
    return x and len([1, 2])
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), False)
        self.assertRaises(RuntimeError, foo, True)
        self.assertEqual(foo.__code__.co_fatalbailcount, 0)

        sys.setbailerror(False)
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), 2)

        # Make sure we didn't actually compile the untaken branch to LLVM IR.
        self.assertTrue("@len" not in str(foo.__code__.co_llvm))

    def test_JUMP_IF_FALSE_OR_POP_training_inconsistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches. We can't do this, though, if the branch is inconsistent.
        foo = compile_for_llvm("foo", """
def foo(x):
    return x and len([1, 2])
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), False)
        self.assertEqual(foo(True), 2)  # Does not raise RuntimeError

        # Make sure we compiled both branches to LLVM IR.
        self.assertContains("@len", str(foo.__code__.co_llvm))

    def test_JUMP_IF_TRUE_OR_POP_training_consistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches to a) reduce code size, b) give the optimizers less input.
        # We only do this if the branch is consistent (always 100%
        # taken/not-taken).
        foo = compile_for_llvm("foo", """
def foo(x):
    return x or len([1, 2])
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), True)
        self.assertRaises(RuntimeError, foo, False)
        self.assertEqual(foo.__code__.co_fatalbailcount, 0)

        sys.setbailerror(False)
        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(False), 2)

        # Make sure we didn't actually compile the untaken branch to LLVM IR.
        self.assertTrue("@len" not in str(foo.__code__.co_llvm))

    def test_JUMP_IF_TRUE_OR_POP_training_inconsistent(self):
        # If we have runtime feedback, we'd like to be able to omit untaken
        # branches. We can't do this, though, if the branch is inconsistent.
        foo = compile_for_llvm("foo", """
def foo(x):
    return x or len([1, 2])
""", optimization_level=None)

        for _ in xrange(JIT_SPIN_COUNT):
            foo(True)
            foo(False)

        self.assertTrue(foo.__code__.__use_llvm__)
        self.assertEqual(foo(True), True)
        self.assertEqual(foo(False), 2)  # Does not raise RuntimeError

        # Make sure we compiled both branches to LLVM IR.
        self.assertContains("@len", str(foo.__code__.co_llvm))

    def test_import_does_not_bail(self):
        # Regression test: this simple import (which hits sys.modules!) used
        # to cause invalidation due to no-op assignments to the globals dict
        # done deep within the import machinery.
        foo = compile_for_llvm("foo", """
def foo():
    import os
    return len([])
""", optimization_level=None)
        import os  # Make sure this is in sys.modules.
        for _ in xrange(JIT_SPIN_COUNT):
            foo()

        # If we get here, we haven't bailed, but double-check to be sure.
        self.assertTrue(foo.__code__.__use_llvm__)


class LlvmRebindBuiltinsTests(test_dynamic.RebindBuiltinsTests):

    def configure_func(self, func, *args):
        # Spin the function until it triggers as hot. Setting co_optimization
        # doesn't trigger the full range of optimizations.
        for _ in xrange(JIT_SPIN_COUNT):
            func(*args)

    def test_changing_globals_invalidates_function(self):
        foo = compile_for_llvm("foo", "def foo(): return len(range(3))",
                               optimization_level=None)
        self.configure_func(foo)
        self.assertEqual(foo.__code__.__use_llvm__, True)

        with test_support.swap_item(globals(), "len", lambda x: 7):
            self.assertEqual(foo.__code__.__use_llvm__, False)

    def test_changing_builtins_invalidates_function(self):
        foo = compile_for_llvm("foo", "def foo(): return len(range(3))",
                               optimization_level=None)
        self.configure_func(foo)
        self.assertEqual(foo.__code__.__use_llvm__, True)

        with test_support.swap_attr(__builtin__, "len", lambda x: 7):
            self.assertEqual(foo.__code__.__use_llvm__, False)

    def test_nondict_builtins_class(self):
        # Regression test: this used to trigger a fatal assertion when trying
        # to watch an instance of D; assertions from pure-Python code are a
        # no-no.
        class D(dict):
            pass

        foo = compile_for_llvm("foo", "def foo(): return len",
                               optimization_level=None,
                               globals_dict=D(globals()))
        foo.__code__.__use_llvm__ = True
        foo()


def test_main():
    tests = [LoopExceptionInteractionTests, GeneralCompilationTests,
             OperatorTests, LiteralsTests, BailoutTests]
    if sys.flags.optimize >= 1:
        print >>sys.stderr, "test_llvm -- skipping some tests due to -O flag."
        sys.stderr.flush()
    if _llvm.get_jit_control() != "whenhot":
        print >>sys.stderr, "test_llvm -- skipping some tests due to -j flag."
        sys.stderr.flush()
    else:
        tests.extend([OptimizationTests, LlvmRebindBuiltinsTests])

    test_support.run_unittest(*tests)


if __name__ == "__main__":
    test_main()
