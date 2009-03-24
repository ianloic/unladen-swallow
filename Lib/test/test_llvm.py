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

    def test_simple_function_definition(self):
        # The LLVM compiler can't yet compile anything; it just
        # attaches a function declaration to each code object.
        namespace = {}
        exec "def foo(a): return a + 3" in namespace
        self.assertEquals(str(namespace['foo'].__code__.co_llvm),
                          """\

declare %__pyobject @foo(%__pyobject, %__pyobject, %__pyobject, %__pyobject)
""")
        self.assertEquals(str(namespace['foo'].__code__.co_llvm.module),
                          """\
; ModuleID = '<string>'
	%__function_type = type %__pyobject (%__pyobject, %__pyobject, %__pyobject, %__pyobject)
	%__pyobject = type { %__pyobject*, %__pyobject*, i32, %__pyobject* }

declare %__pyobject @"<module>"(%__pyobject, %__pyobject, %__pyobject, %__pyobject)

declare %__pyobject @foo(%__pyobject, %__pyobject, %__pyobject, %__pyobject)
""")


def test_main():
    run_unittest(LlvmTests)


if __name__ == "__main__":
    test_main()
