# Tests for our minimal LLVM wrappers

from test.test_support import run_unittest, findfile
import unittest
import _llvm


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

    def test_run_simple_function(self):
        def foo():
            pass
        foo.__code__.__use_llvm__ = True
        self.assertEquals(None, foo())

    def test_return_arg(self):
        def foo(a):
            return a
        foo.__code__.__use_llvm__ = True
        self.assertEquals(3, foo(3))
        self.assertEquals("Hello", foo("Hello"))

    def test_unbound_local(self):
        def foo():
            a = a
        foo.__code__.__use_llvm__ = True
        try:
            foo()
        except UnboundLocalError as e:
            self.assertEquals(
                str(e), "local variable 'a' referenced before assignment")
        else:
            self.fail("Expected UnboundLocalError")

    def test_assign(self):
        def foo(a):
            b = a
            return b
        foo.__code__.__use_llvm__ = True
        self.assertEquals(3, foo(3))
        self.assertEquals("Hello", foo("Hello"))

    def test_raising_getiter(self):
        class RaisingIter(object):
            def __iter__(self):
                raise RuntimeError
        def loop(range):
            for i in range:
                pass
        loop.__code__.__use_llvm__ = True
        self.assertRaises(RuntimeError, loop, RaisingIter())

    def test_raising_next(self):
        class RaisingNext(object):
            def __iter__(self):
                return self
            def next(self):
                raise RuntimeError
        def loop(range):
            for i in range:
                pass
        loop.__code__.__use_llvm__ = True
        self.assertRaises(RuntimeError, loop, RaisingNext())

    def test_loop(self):
        def loop(range):
            for i in range:
                pass
        loop.__code__.__use_llvm__ = True
        r = iter(range(12))
        self.assertEquals(None, loop(r))
        self.assertRaises(StopIteration, next, r)

    def test_return_from_loop(self):
        def loop(range):
            for i in range:
                return i
        loop.__code__.__use_llvm__ = True
        self.assertEquals(1, loop([1,2,3]))


def test_main():
    run_unittest(LlvmTests)


if __name__ == "__main__":
    test_main()
