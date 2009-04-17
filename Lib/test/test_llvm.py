# Tests for our minimal LLVM wrappers

from test.test_support import run_unittest, findfile
import _llvm
import functools
import sys
import unittest
import __future__


def at_each_optimization_level(func):
    """Decorator for test functions, to run them at each optimization level."""
    @functools.wraps(func)
    def result(self):
        for level in (None, -1, 0, 1, 2):
            func(self, level)
    return result


def compile_for_llvm(function_name, def_string, optimization_level=-1):
    """Compiles function_name, defined in def_string to be run through LLVM.

    Compiles and runs def_string in a temporary namespace, pulls the
    function named 'function_name' out of that namespace, optimizes it
    at level 'optimization_level', -1 for the default optimization,
    and marks it to be JITted and run through LLVM.

    """
    namespace = {}
    exec def_string in namespace
    func = namespace[function_name]
    if optimization_level is not None:
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


class LlvmTests(unittest.TestCase):
    def setUp(self):
        self.bitcode = open(findfile('arithmetic.bc')).read()
        self.module = _llvm._module.from_bitcode('arithmetic.bc', self.bitcode)

    def test_bitcode_to_assembly(self):
        self.assertEquals(str(self.module), """\
; ModuleID = 'arithmetic.bc'

define i32 @mul_add(i32 %x, i32 %y, i32 %z) {
entry:
	%tmp = mul i32 %x, %y		; <i32> [#uses=1]
	%tmp2 = add i32 %tmp, %z		; <i32> [#uses=1]
	ret i32 %tmp2
}

define i32 @shl_add(i32 %x, i32 %y, i32 %z) {
entry:
	%tmp = shl i32 %x, %y		; <i32> [#uses=1]
	%tmp2 = add i32 %tmp, %z		; <i32> [#uses=1]
	ret i32 %tmp2
}
""")

    def test_module_repr(self):
        # The above string isn't suitable to construct a new module,
        # so it shouldn't be the repr.
        self.assertNotEquals(str(self.module), repr(self.module))

    def test_iter_to_functions(self):
        functions = list(self.module.functions())
        self.assertEquals(2, len(functions))
        self.assertEquals(str(functions[0]), """\

define i32 @mul_add(i32 %x, i32 %y, i32 %z) {
entry:
	%tmp = mul i32 %x, %y		; <i32> [#uses=1]
	%tmp2 = add i32 %tmp, %z		; <i32> [#uses=1]
	ret i32 %tmp2
}
""")
        self.assertEquals(str(functions[1]), """\

define i32 @shl_add(i32 %x, i32 %y, i32 %z) {
entry:
	%tmp = shl i32 %x, %y		; <i32> [#uses=1]
	%tmp2 = add i32 %tmp, %z		; <i32> [#uses=1]
	ret i32 %tmp2
}
""")

    def test_function_repr(self):
        # The above strings aren't suitable to construct a new
        # function, so they shouldn't be the reprs.
        function = self.module.functions().next()
        self.assertNotEquals(str(function), repr(function))

    def test_uncreatable(self):
        # Modules and functions can only be created by their static factories.
        self.assertRaises(TypeError, _llvm._module)
        self.assertRaises(TypeError, _llvm._function)

    @at_each_optimization_level
    def test_run_simple_function(self, level):
        foo = compile_for_llvm("foo", """
def foo():
    pass
""", level)
        self.assertEquals(None, foo())

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
        1 / 0
    except:
        obj["x"] = 2
""", level)
        obj = {}
        self.assertEquals(None, catch(obj))
        self.assertEquals({"x": 2}, obj)

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
        1 / 0
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
            1 / 0
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
            1 / 0
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
            a = a
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
        # Raise from the try.
        catch = compile_for_llvm("catch", """
def catch(obj):
    try:
        1 / 0
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
        # Raise from the try.
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
        testvalue = 'test global value'
        loadglobal = compile_for_llvm('loadglobal',
                                      'def loadglobal(): return testvalue',
                                      level)
        loadglobal.func_globals['testvalue'] = testvalue
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
        setglobal = compile_for_llvm('setglobal', '''
def setglobal(x):
    global _test_global
    _test_global = x
''', level)
        testvalue = "test global value"
        self.assertTrue('_test_global' not in setglobal.func_globals)
        setglobal(testvalue)
        self.assertTrue('_test_global' in setglobal.func_globals)
        self.assertEquals(setglobal.func_globals['_test_global'], testvalue)

        delglobal = compile_for_llvm('delglobal', '''
def delglobal():
    global _test_global
    del _test_global
''', level)
        delglobal.func_globals['_test_global'] = 'test global value'
        self.assertTrue('_test_global' in delglobal.func_globals)
        delglobal()
        self.assertTrue('_test_global' not in delglobal.func_globals)

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

# TODO(twouters): assert needs LOAD_GLOBAL and RAISE_VARARGS
##     @at_each_optimization_level
##     def test_assert(self, level):
##         f = comile_for_llvm("f", "def f(x): assert x", level)
##         self.assertEquals(f(1), None)
##         self.assertRaises(AssertionError, f, 0)

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


class LoopExceptionInteractionTests(unittest.TestCase):
    @at_each_optimization_level
    def test_except_through_loop_caught(self, level):
        nested = compile_for_llvm('nested', '''
def nested(lst, obj):
    try:
        for x in lst:
            a = a
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


# dont_inherit will unfortunately not turn off true division when
# running with -Qnew, so we can't test classic division in
# test_basic_arithmetic when running with -Qnew.
# Make sure we aren't running with -Qnew. A __future__
# statement in this module should not affect things.
_co = compile('1 / 2', 'truediv_check', 'eval',
             flags=0, dont_inherit=True)
assert eval(_co) == 0, "Do not run test_llvm with -Qnew"
del _co

class OpRecorder(object):
    # regular binary arithmetic operations
    def __init__(self):
        self.ops = []
    def __cmp__(self, other):
        return cmp(self.ops, other)
    def __add__(self, other):
        self.ops.append('add')
        return 1
    def __sub__(self, other):
        self.ops.append('sub')
        return 2
    def __mul__(self, other):
        self.ops.append('mul')
        return 3
    def __div__(self, other):
        self.ops.append('div')
        return 4
    def __truediv__(self, other):
        self.ops.append('truediv')
        return 5
    def __floordiv__(self, other):
        self.ops.append('floordiv')
        return 6
    def __mod__(self, other):
        self.ops.append('mod')
        return 7
    def __pow__(self, other):
        self.ops.append('pow')
        return 8
    def __lshift__(self, other):
        self.ops.append('lshift')
        return 9
    def __rshift__(self, other):
        self.ops.append('rshift')
        return 10
    def __and__(self, other):
        self.ops.append('and')
        return 11
    def __or__(self, other):
        self.ops.append('or')
        return 12
    def __xor__(self, other):
        self.ops.append('xor')
        return 13

    # Unary operations
    def __nonzero__(self):
        self.ops.append('nonzero')
        return False
    def __invert__(self):
        self.ops.append('invert')
        return 14
    def __pos__(self):
        self.ops.append('pos')
        return 15
    def __neg__(self):
        self.ops.append('neg')
        return 16
    def __repr__(self):
        self.ops.append('repr')
        return '<OpRecorder 17>'

    # right-hand binary arithmetic operations
    def __radd__(self, other):
        self.ops.append('radd')
        return 101
    def __rsub__(self, other):
        self.ops.append('rsub')
        return 102
    def __rmul__(self, other):
        self.ops.append('rmul')
        return 103
    def __rdiv__(self, other):
        self.ops.append('rdiv')
        return 104
    def __rtruediv__(self, other):
        self.ops.append('rtruediv')
        return 105
    def __rfloordiv__(self, other):
        self.ops.append('rfloordiv')
        return 106
    def __rmod__(self, other):
        self.ops.append('rmod')
        return 107
    def __rpow__(self, other):
        self.ops.append('rpow')
        return 108
    def __rlshift__(self, other):
        self.ops.append('rlshift')
        return 109
    def __rrshift__(self, other):
        self.ops.append('rrshift')
        return 110
    def __rand__(self, other):
        self.ops.append('rand')
        return 111
    def __ror__(self, other):
        self.ops.append('ror')
        return 112
    def __rxor__(self, other):
        self.ops.append('rxor')
        return 113

    # In-place binary arithmetic operations
    def __iadd__(self, other):
        self.ops.append('iadd')
        return 1001
    def __isub__(self, other):
        self.ops.append('isub')
        return 1002
    def __imul__(self, other):
        self.ops.append('imul')
        return 1003
    def __idiv__(self, other):
        self.ops.append('idiv')
        return 1004
    def __itruediv__(self, other):
        self.ops.append('itruediv')
        return 1005
    def __ifloordiv__(self, other):
        self.ops.append('ifloordiv')
        return 1006
    def __imod__(self, other):
        self.ops.append('imod')
        return 1007
    def __ipow__(self, other):
        self.ops.append('ipow')
        return 1008
    def __ilshift__(self, other):
        self.ops.append('ilshift')
        return 1009
    def __irshift__(self, other):
        self.ops.append('irshift')
        return 1010
    def __iand__(self, other):
        self.ops.append('iand')
        return 1011
    def __ior__(self, other):
        self.ops.append('ior')
        return 1012
    def __ixor__(self, other):
        self.ops.append('ixor')
        return 1013

    # Indexing
    def __getitem__(self, item):
        self.ops.append(('getitem', item))
        return 1014
    def __setitem__(self, item, value):
        self.ops.append(('setitem', item, value))
    def __delitem__(self, item):
        self.ops.append(('delitem', item))

    # Slicing
    def __getslice__(self, start, stop):
        self.ops.append(('getslice', start, stop))
        return ('getslice', start, stop)
    def __setslice__(self, start, stop, seq):
        self.ops.append(('setslice', start, stop, seq))
    def __delslice__(self, start, stop):
        self.ops.append(('delslice', start, stop))

class OperatorTests(ExtraAssertsTestCase):
    def run_and_compare(self, testfunc, optimization_level,
                        expected_num_ops, expected_num_results):
        non_llvm_results = {}
        non_llvm_recorder = OpRecorder()
        testfunc(non_llvm_recorder, non_llvm_results)
        self.assertEquals(len(non_llvm_recorder.ops), expected_num_ops)
        self.assertEquals(len(non_llvm_results), expected_num_results)
        self.assertEquals(len(set(non_llvm_results.values())),
                          len(non_llvm_results))

        if optimization_level is not None:
            testfunc.__code__.co_optimization = optimization_level
            testfunc.__code__.__use_llvm__ = True
        llvm_results = {}
        llvm_recorder = OpRecorder()
        testfunc(llvm_recorder, llvm_results)

        self.assertEquals(non_llvm_results, llvm_results)
        self.assertEquals(non_llvm_recorder.ops, llvm_recorder.ops)

    @at_each_optimization_level
    def test_basic_arithmetic(self, level):
        operators = ('+', '-', '*', '/', '//', '%', '**',
                     '<<', '>>', '&', '|', '^')
        num_ops = len(operators) * 3
        parts = []
        for op in operators:
            parts.extend([
                'results["regular %s"] = x %s 1' % (op, op),
                'results["reverse %s"] = 1 %s x' % (op, op),
                'y = x;y %s= 1; results["in-place %s"] = y' % (op, op),
            ])
        testcode = '\n'.join(['def test(x, results):',
                              '  ' + '\n  '.join(parts)])
        co = compile(testcode, 'basic_arithmetic', 'exec',
                     flags=0, dont_inherit=True)
        namespace = {}
        exec co in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace['test'],
                             level,
                             expected_num_ops=num_ops,
                             expected_num_results=num_ops)

    @at_each_optimization_level
    def test_truediv(self, level):
        truedivcode = '''def test(x, results):
                             results["regular div"] = x / 1
                             results["reverse div"] = 1 / x
                             x /= 1; results["in-place div"] = x'''
        co = compile(truedivcode, 'truediv_arithmetic', 'exec',
                     flags=__future__.division.compiler_flag,
                     dont_inherit=True)
        namespace = {}
        exec co in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace['test'], level,
                             expected_num_ops=3, expected_num_results=3)

    @at_each_optimization_level
    def test_subscr(self, level):
        namespace = {}
        exec """
def testfunc(x, results):
    results['idx'] = x['item']
    x['item'] = 1
    del x['item']
""" in namespace
        self.run_and_compare(namespace['testfunc'], level,
                             expected_num_ops=3, expected_num_results=1)

    @at_each_optimization_level
    def test_unary(self, level):
        namespace = {}
        exec """
def testfunc(x, results):
    results['not'] = not x
    results['invert'] = ~x
    results['pos'] = +x
    results['neg'] = -x
    results['convert'] = `x`
""" in namespace
        self.run_and_compare(namespace['testfunc'], level,
                             expected_num_ops=5, expected_num_results=5)

    @at_each_optimization_level
    def test_subscr_augassign(self, level):
        namespace = {}
        exec """
def testfunc(x, results):
    results['item'] = x
    results['item'] += 1
    x['item'] += 1
""" in namespace
        # expect __iadd__, __getitem__ and __setitem__ on x,
        # and one item in results.
        self.run_and_compare(namespace['testfunc'], level,
                             expected_num_ops=3, expected_num_results=1)

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
        recorders = [OpRecorder(), OpRecorder(), OpRaiser(), OpRecorder()]
        self.assertRaises(OpExc, listcomp_exc, recorders)
        # Test that the last OpRecorder wasn't touched, and we didn't
        # leak references.
        self.assertEquals([o.ops for o in recorders],
                          [['add'], ['add'], ['add'], []])


    @at_each_optimization_level
    def test_slice_none(self, level):
        getslice_none = compile_for_llvm('getslice_none',
                                         'def getslice_none(x): return x[:]',
                                         level)
        self.assertEquals(getslice_none(OpRecorder()),
                          ('getslice', 0, sys.maxint))
        self.assertRaisesWithArgs(OpExc, ('getslice', 0, sys.maxint),
                                  getslice_none, OpRaiser())

        setslice_none = compile_for_llvm('setslice_none',
                                         'def setslice_none(x): x[:] = [0]',
                                         level)
        op = OpRecorder()
        setslice_none(op)
        self.assertEquals(op.ops, [('setslice', 0, sys.maxint, [0])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 0, sys.maxint, [0]),
                                  setslice_none, OpRaiser())

        delslice_none = compile_for_llvm('delslice_none',
                                         'def delslice_none(x): del x[:]',
                                         level)
        op = OpRecorder()
        delslice_none(op)
        self.assertEquals(op.ops, [('delslice', 0, sys.maxint)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 0, sys.maxint),
                                  delslice_none, OpRaiser())

    @at_each_optimization_level
    def test_slice_left(self, level):
        getslice_left = compile_for_llvm('getslice_left', '''
def getslice_left(x, y):
    return x[y:]
''', level)
        self.assertEquals(getslice_left(OpRecorder(), 5),
                          ('getslice', 5, sys.maxint))
        self.assertRaisesWithArgs(OpExc, ('getslice', 5, sys.maxint),
                                  getslice_left, OpRaiser(), 5)

        setslice_left = compile_for_llvm('setslice_left', '''
def setslice_left(x, y):
    x[y:] = [1]
''', level)
        op = OpRecorder()
        setslice_left(op, 5)
        self.assertEquals(op.ops, [('setslice', 5, sys.maxint, [1])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 5, sys.maxint, [1]),
                                  setslice_left, OpRaiser(), 5)

        delslice_left = compile_for_llvm('delslice_left', '''
def delslice_left(x, y):
    del x[y:]
''', level)
        op = OpRecorder()
        delslice_left(op, 5)
        self.assertEquals(op.ops, [('delslice', 5, sys.maxint)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 5, sys.maxint),
                                  delslice_left, OpRaiser(), 5)

    @at_each_optimization_level
    def test_slice_right(self, level):
        getslice_right = compile_for_llvm('getslice_right', '''
def getslice_right(x, y):
    return x[:y]
''', level)
        self.assertEquals(getslice_right(OpRecorder(), 10),
                          ('getslice', 0, 10))
        self.assertRaisesWithArgs(OpExc, ('getslice', 0, 10),
                                  getslice_right, OpRaiser(), 10)

        setslice_right = compile_for_llvm('setslice_right', '''
def setslice_right(x, y):
    x[:y] = [2]
''', level)
        op = OpRecorder()
        setslice_right(op, 10)
        self.assertEquals(op.ops, [('setslice', 0, 10, [2])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 0, 10, [2]),
                                  setslice_right, OpRaiser(), 10)

        delslice_right = compile_for_llvm('delslice_right', '''
def delslice_right(x, y):
    del x[:y]
''', level)
        op = OpRecorder()
        delslice_right(op, 10)
        self.assertEquals(op.ops, [('delslice', 0, 10)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 0, 10),
                                  delslice_right, OpRaiser(), 10)

    @at_each_optimization_level
    def test_slice_both(self, level):
        getslice_both = compile_for_llvm('getslice_both', '''
def getslice_both(x, y, z):
    return x[y:z]
''', level)
        self.assertEquals(getslice_both(OpRecorder(), 4, -6),
                          ('getslice', 4, -6))
        self.assertRaisesWithArgs(OpExc, ('getslice', 4, -6),
                                  getslice_both, OpRaiser(), 4, -6)

        setslice_both = compile_for_llvm('setslice_both', '''
def setslice_both(x, y, z):
    x[y:z] = [3]
''', level)
        op = OpRecorder()
        setslice_both(op, 4, -6)
        self.assertEquals(op.ops, [('setslice', 4, -6, [3])])
        self.assertRaisesWithArgs(OpExc, ('setslice', 4, -6, [3]),
                                  setslice_both, OpRaiser(), 4, -6)

        delslice_both = compile_for_llvm('delslice_both', '''
def delslice_both(x, y, z):
    del x[y:z]
''', level)
        op = OpRecorder()
        delslice_both(op, 4, -6)
        self.assertEquals(op.ops, [('delslice', 4, -6)])
        self.assertRaisesWithArgs(OpExc, ('delslice', 4, -6),
                                  delslice_both, OpRaiser(), 4, -6)

class OpExc(Exception):
    def __cmp__(self, other):
        return cmp(self.args, other.args)
    def __hash__(self):
        return hash(self.args)

class OpRaiser(object):
    # regular binary arithmetic operations
    def __init__(self):
        self.ops = []
        self.recording = True
    def __cmp__(self, other):
        return cmp(self.ops, other)
    def __add__(self, other):
        self.ops.append('add')
        raise OpExc(1)
    def __sub__(self, other):
        self.ops.append('sub')
        raise OpExc(2)
    def __mul__(self, other):
        self.ops.append('mul')
        raise OpExc(3)
    def __div__(self, other):
        self.ops.append('div')
        raise OpExc(4)
    def __truediv__(self, other):
        self.ops.append('truediv')
        raise OpExc(5)
    def __floordiv__(self, other):
        self.ops.append('floordiv')
        raise OpExc(6)
    def __mod__(self, other):
        self.ops.append('mod')
        raise OpExc(7)
    def __pow__(self, other):
        self.ops.append('pow')
        raise OpExc(8)
    def __lshift__(self, other):
        self.ops.append('lshift')
        raise OpExc(9)
    def __rshift__(self, other):
        self.ops.append('rshift')
        raise OpExc(10)
    def __and__(self, other):
        self.ops.append('and')
        raise OpExc(11)
    def __or__(self, other):
        self.ops.append('or')
        raise OpExc(12)
    def __xor__(self, other):
        self.ops.append('xor')
        raise OpExc(13)

    # Unary operations
    def __nonzero__(self):
        self.ops.append('nonzero')
        raise OpExc(False)
    def __invert__(self):
        self.ops.append('invert')
        raise OpExc(14)
    def __pos__(self):
        self.ops.append('pos')
        raise OpExc(15)
    def __neg__(self):
        self.ops.append('neg')
        raise OpExc(16)
    def __repr__(self):
        if not self.recording:
            return '<OpRecorder %r>' % self.ops
        self.ops.append('repr')
        raise OpExc('<OpRecorder 17>')

    # right-hand binary arithmetic operations
    def __radd__(self, other):
        self.ops.append('radd')
        raise OpExc(101)
    def __rsub__(self, other):
        self.ops.append('rsub')
        raise OpExc(102)
    def __rmul__(self, other):
        self.ops.append('rmul')
        raise OpExc(103)
    def __rdiv__(self, other):
        self.ops.append('rdiv')
        raise OpExc(104)
    def __rtruediv__(self, other):
        self.ops.append('rtruediv')
        raise OpExc(105)
    def __rfloordiv__(self, other):
        self.ops.append('rfloordiv')
        raise OpExc(106)
    def __rmod__(self, other):
        self.ops.append('rmod')
        raise OpExc(107)
    def __rpow__(self, other):
        self.ops.append('rpow')
        raise OpExc(108)
    def __rlshift__(self, other):
        self.ops.append('rlshift')
        raise OpExc(109)
    def __rrshift__(self, other):
        self.ops.append('rrshift')
        raise OpExc(110)
    def __rand__(self, other):
        self.ops.append('rand')
        raise OpExc(111)
    def __ror__(self, other):
        self.ops.append('ror')
        raise OpExc(112)
    def __rxor__(self, other):
        self.ops.append('rxor')
        raise OpExc(113)

    # In-place binary arithmetic operations
    def __iadd__(self, other):
        self.ops.append('iadd')
        raise OpExc(1001)
    def __isub__(self, other):
        self.ops.append('isub')
        raise OpExc(1002)
    def __imul__(self, other):
        self.ops.append('imul')
        raise OpExc(1003)
    def __idiv__(self, other):
        self.ops.append('idiv')
        raise OpExc(1004)
    def __itruediv__(self, other):
        self.ops.append('itruediv')
        raise OpExc(1005)
    def __ifloordiv__(self, other):
        self.ops.append('ifloordiv')
        raise OpExc(1006)
    def __imod__(self, other):
        self.ops.append('imod')
        raise OpExc(1007)
    def __ipow__(self, other):
        self.ops.append('ipow')
        raise OpExc(1008)
    def __ilshift__(self, other):
        self.ops.append('ilshift')
        raise OpExc(1009)
    def __irshift__(self, other):
        self.ops.append('irshift')
        raise OpExc(1010)
    def __iand__(self, other):
        self.ops.append('iand')
        raise OpExc(1011)
    def __ior__(self, other):
        self.ops.append('ior')
        raise OpExc(1012)
    def __ixor__(self, other):
        self.ops.append('ixor')
        raise OpExc(1013)

    # Indexing
    def __getitem__(self, item):
        self.ops.append(('getitem', item))
        raise OpExc(1014)
    def __setitem__(self, item, value):
        self.ops.append(('setitem', item, value))
        raise OpExc(1015)
    def __delitem__(self, item):
        self.ops.append(('delitem', item))
        raise OpExc(1016)

    # Classic slices
    def __getslice__(self, start, stop):
        raise OpExc('getslice', start, stop)
    def __setslice__(self, start, stop, seq):
        raise OpExc('setslice', start, stop, seq)
    def __delslice__(self, start, stop):
        raise OpExc('delslice', start, stop)

class OperatorRaisingTests(unittest.TestCase):
    def run_and_compare(self, namespace, optimization_level,
                        argument_factory=OpRaiser):
        non_llvm_results = []
        non_llvm_raiser = argument_factory()
        funcs = namespace.items()
        funcs.sort()
        for fname, func in funcs:
            try:
                func(non_llvm_raiser)
            except OpExc, e:
                non_llvm_results.append(e)
        non_llvm_raiser.recording = False

        self.assertEquals(len(non_llvm_raiser.ops), len(funcs))
        self.assertEquals(len(non_llvm_results), len(funcs))
        self.assertEquals(len(set(non_llvm_results)),
                          len(non_llvm_results))

        llvm_results = []
        llvm_raiser = argument_factory()
        for fname, func in funcs:
            if optimization_level is not None:
                func.__code__.co_optimization = optimization_level
                func.__code__.__use_llvm__ = True
            try:
                func(llvm_raiser)
            except OpExc, e:
                llvm_results.append(e)
        llvm_raiser.recording = False

        self.assertEquals(non_llvm_results, llvm_results)
        self.assertEquals(non_llvm_raiser.ops, llvm_raiser.ops)

    @at_each_optimization_level
    def test_basic_arithmetic(self, level):
        operators = ('+', '-', '*', '/', '//', '%', '**',
                     '<<', '>>', '&', '|', '^')
        parts = []
        for idx, op in enumerate(operators):
            parts.extend([
                'def regular_%s(x): x %s 1' % (idx, op),
                'def reverse_%s(x): 1 %s x' % (idx, op),
                'def inplace_%s(x): x %s= 1' % (idx, op),
            ])
        # Compile in a single codeblock to avoid (current) LLVM
        # exec overhead.
        testcode = '\n'.join(parts)
        co = compile(testcode, 'basic_arithmetic', 'exec',
                     flags=0, dont_inherit=True)
        namespace = {}
        exec co in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace, level)

    @at_each_optimization_level
    def test_truediv(self, level):
        truedivcode = '\n'.join(['def regular(x): x / 1',
                                 'def reverse(x): 1 / x',
                                 'def inplace(x): x /= 1',
        ])
        co = compile(truedivcode, 'truediv_arithmetic', 'exec',
                     flags=__future__.division.compiler_flag,
                     dont_inherit=True)
        namespace = {}
        exec co in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace, level)

    @at_each_optimization_level
    def test_unary(self, level):
        namespace = {}
        exec """
def not_(x): return not x
def invert(x): return ~x
def pos(x): return +x
def neg(x): return -x
def convert(x): return `x`
""" in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace, level)

    @at_each_optimization_level
    def test_subscr(self, level):
        namespace = {}
        exec """
def getitem(x): x['item']
def setitem(x): x['item'] = 1
def delitem(x): del x['item']
""" in namespace
        del namespace['__builtins__']
        self.run_and_compare(namespace, level)

    @at_each_optimization_level
    def test_subscr_augassign_(self, level):
        # We can't reuse setitem for the different tests because
        # running it will increase the optimization level from -1 to 0.
        def make_setitem():
            namespace = {}
            exec """
def setitem(x):
    x['item'] += 1
""" in namespace
            del namespace['__builtins__']
            return namespace
        # Test x.__getitem__ raising an exception.
        self.run_and_compare(make_setitem(), level)
        # Test x.__setitem__ raising an exception.
        class HalfOpRaiser(OpRaiser):
            def __getitem__(self, item):
                # Not recording this operation, we care about __setitem__.
                return 1
        self.run_and_compare(make_setitem(), level,
                             argument_factory=HalfOpRaiser)
        # Test item.__iadd__(1) raising an exception, between the
        # x.__getitem__ and x.__setitem__ calls.
        class OpRaiserProvider(OpRaiser):
            def __init__(self):
                OpRaiser.__init__(self)
                self.opraiser = None
            def __cmp__(self, other):
                return cmp((self.ops, self.opraiser),
                           (other.ops, other.opraiser))
            def __getitem__(self, item):
                self.ops.append('getitem')
                self.opraiser = OpRaiser()
                return self.opraiser
        self.run_and_compare(make_setitem(), level,
                             argument_factory=OpRaiserProvider)


class ComparisonReporter(object):
    def __cmp__(self, other):
        return 'cmp'
    def __eq__(self, other):
        return 'eq'
    def __ne__(self, other):
        return 'ne'
    def __lt__(self, other):
        return 'lt'
    def __le__(self, other):
        return 'le'
    def __gt__(self, other):
        return 'gt'
    def __ge__(self, other):
        return 'ge'
    def __contains__(self, other):
        return True


class ComparisonRaiser(object):
    def __cmp__(self, other):
        raise RuntimeError, 'cmp should not be called'
    def __eq__(self, other):
        raise OpExc('eq')
    def __ne__(self, other):
        raise OpExc('ne')
    def __lt__(self, other):
        raise OpExc('lt')
    def __le__(self, other):
        raise OpExc('le')
    def __gt__(self, other):
        raise OpExc('gt')
    def __ge__(self, other):
        raise OpExc('ge')
    def __contains__(self, other):
        raise OpExc('contains')


class ComparisonTests(ExtraAssertsTestCase):
    @at_each_optimization_level
    def test_is(self, level):
        is_ = compile_for_llvm('is_', 'def is_(x, y): return x is y', level)
        # Don't rely on Python making separate literal 1's the same object.
        one = 1
        reporter = ComparisonReporter()
        self.assertTrue(is_(one, one))
        self.assertFalse(is_(2, 3))
        self.assertFalse(is_([], []))
        self.assertTrue(is_(reporter, reporter))
        self.assertFalse(is_(7, reporter))

    @at_each_optimization_level
    def test_is_not(self, level):
        is_not = compile_for_llvm('is_not',
                                  'def is_not(x, y): return x is not y',
                                  level)
        # Don't rely on Python making separate literal 1's the same object.
        one = 1
        reporter = ComparisonReporter()
        self.assertFalse(is_not(one, one))
        self.assertTrue(is_not(2, 3))
        self.assertTrue(is_not([], []))
        self.assertFalse(is_not(reporter, reporter))
        self.assertTrue(is_not(7, reporter))

    @at_each_optimization_level
    def test_eq(self, level):
        eq = compile_for_llvm('eq', 'def eq(x, y): return x == y', level)
        self.assertTrue(eq(1, 1))
        self.assertFalse(eq(2, 3))
        self.assertTrue(eq([], []))
        self.assertEquals(eq(ComparisonReporter(), 6), 'eq')
        self.assertEquals(eq(7, ComparisonReporter()), 'eq')
        self.assertRaisesWithArgs(OpExc, ('eq',), eq, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('eq',), eq, 1, ComparisonRaiser())

    @at_each_optimization_level
    def test_ne(self, level):
        ne = compile_for_llvm('ne', 'def ne(x, y): return x != y', level)
        self.assertFalse(ne(1, 1))
        self.assertTrue(ne(2, 3))
        self.assertFalse(ne([], []))
        self.assertEquals(ne(ComparisonReporter(), 6), 'ne')
        self.assertEquals(ne(7, ComparisonReporter()), 'ne')
        self.assertRaisesWithArgs(OpExc, ('ne',), ne, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('ne',), ne, 1, ComparisonRaiser())

    @at_each_optimization_level
    def test_lt(self, level):
        lt = compile_for_llvm('lt', 'def lt(x, y): return x < y', level)
        self.assertFalse(lt(1, 1))
        self.assertTrue(lt(2, 3))
        self.assertFalse(lt(5, 4))
        self.assertFalse(lt([], []))
        self.assertEquals(lt(ComparisonReporter(), 6), 'lt')
        self.assertEquals(lt(7, ComparisonReporter()), 'gt')
        self.assertRaisesWithArgs(OpExc, ('lt',), lt, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('gt',), lt, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ('no ordering relation is defined for complex numbers',),
            lt, 1, 1j)

    @at_each_optimization_level
    def test_le(self, level):
        le = compile_for_llvm('le', 'def le(x, y): return x <= y', level)
        self.assertTrue(le(1, 1))
        self.assertTrue(le(2, 3))
        self.assertFalse(le(5, 4))
        self.assertTrue(le([], []))
        self.assertEquals(le(ComparisonReporter(), 6), 'le')
        self.assertEquals(le(7, ComparisonReporter()), 'ge')
        self.assertRaisesWithArgs(OpExc, ('le',), le, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('ge',), le, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ('no ordering relation is defined for complex numbers',),
            le, 1, 1j)

    @at_each_optimization_level
    def test_gt(self, level):
        gt = compile_for_llvm('gt', 'def gt(x, y): return x > y', level)
        self.assertFalse(gt(1, 1))
        self.assertFalse(gt(2, 3))
        self.assertTrue(gt(5, 4))
        self.assertFalse(gt([], []))
        self.assertEquals(gt(ComparisonReporter(), 6), 'gt')
        self.assertEquals(gt(7, ComparisonReporter()), 'lt')
        self.assertRaisesWithArgs(OpExc, ('gt',), gt, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('lt',), gt, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ('no ordering relation is defined for complex numbers',),
            gt, 1, 1j)

    @at_each_optimization_level
    def test_ge(self, level):
        ge = compile_for_llvm('ge', 'def ge(x, y): return x >= y', level)
        self.assertTrue(ge(1, 1))
        self.assertFalse(ge(2, 3))
        self.assertTrue(ge(5, 4))
        self.assertTrue(ge([], []))
        self.assertEquals(ge(ComparisonReporter(), 6), 'ge')
        self.assertEquals(ge(7, ComparisonReporter()), 'le')
        self.assertRaisesWithArgs(OpExc, ('ge',), ge, ComparisonRaiser(), 1)
        self.assertRaisesWithArgs(OpExc, ('le',), ge, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ('no ordering relation is defined for complex numbers',),
            ge, 1, 1j)

    @at_each_optimization_level
    def test_in(self, level):
        in_ = compile_for_llvm('in_', 'def in_(x, y): return x in y', level)
        self.assertTrue(in_(1, [1, 2]))
        self.assertFalse(in_(1, [0, 2]))
        self.assertTrue(in_(1, ComparisonReporter()))
        self.assertRaisesWithArgs(OpExc, ('contains',),
                                     in_, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ("argument of type 'int' is not iterable",),
            in_, 1, 1)

    @at_each_optimization_level
    def test_not_in(self, level):
        not_in = compile_for_llvm('not_in',
                                  'def not_in(x, y): return x not in y',
                                  level)
        self.assertFalse(not_in(1, [1, 2]))
        self.assertTrue(not_in(1, [0, 2]))
        self.assertFalse(not_in(1, ComparisonReporter()))
        self.assertRaisesWithArgs(OpExc, ('contains',),
                                   not_in, 1, ComparisonRaiser())
        self.assertRaisesWithArgs(TypeError,
            ("argument of type 'int' is not iterable",),
            not_in, 1, 1)


class LiteralsTests(unittest.TestCase):
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


def test_main():
    run_unittest(LoopExceptionInteractionTests, LlvmTests, OperatorTests,
                 OperatorRaisingTests, ComparisonTests, LiteralsTests)


if __name__ == "__main__":
    test_main()
