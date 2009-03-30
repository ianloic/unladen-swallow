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

    def test_module_data(self):
        # Certain types and constants get defined at the module level,
        # uniformly for any function.
        namespace = {}
        exec "def foo(): pass" in namespace
        disassembly = str(namespace['foo'].__code__.co_llvm.module)
        module_data = disassembly[:disassembly.find(
                'define %__pyobject* @"<module>"')]
        self.assertEquals(module_data,
                          """\
; ModuleID = '<string>'
	%__function_type = type %__pyobject* (%__pyframeobject*)
	%__pycodeobject = type { %__pyobject, i32, i32, i32, i32, %__pyobject*, %__pyobject*, %__pyobject*, %__pyobject*, %__pyobject*, %__pyobject*, i8*, %__pyobject*, %__pyobject*, i32, %__pyobject*, i8*, %__pyobject* }
	%__pyframeobject = type { %__pyobject, i32, %__pyobject*, %__pycodeobject*, %__pyobject*, %__pyobject*, %__pyobject*, %__pyobject**, %__pyobject**, %__pyobject*, %__pyobject*, %__pyobject*, %__pyobject*, i8*, i32, i32, i32, [20 x { i32, i32, i32 }], [0 x %__pyobject*] }
	%__pyobject = type { %__pyobject*, %__pyobject*, i32, %__pyobject* }
	%__pytupleobject = type { %__pyobject, i32, [0 x %__pyobject*] }
@_Py_RefTotal = external global i32		; <i32*> [#uses=6]

""")

    def test_simple_function_definition(self):
        namespace = {}
        exec "def foo(): return" in namespace
        self.assertEquals(str(namespace['foo'].__code__.co_llvm),
                          """\

define %__pyobject* @foo(%__pyframeobject* %frame) {
entry:
	%0 = getelementptr %__pyframeobject* %frame, i32 0, i32 8		; <%__pyobject***> [#uses=1]
	%initial_stack_pointer = load %__pyobject*** %0		; <%__pyobject**> [#uses=1]
	%1 = getelementptr %__pyframeobject* %frame, i32 0, i32 3		; <%__pycodeobject**> [#uses=1]
	%co = load %__pycodeobject** %1		; <%__pycodeobject*> [#uses=1]
	%2 = getelementptr %__pycodeobject* %co, i32 0, i32 6		; <%__pyobject**> [#uses=1]
	%3 = load %__pyobject** %2		; <%__pyobject*> [#uses=1]
	%4 = getelementptr %__pyobject* %3, i32 1, i32 1		; <%__pyobject**> [#uses=1]
	%5 = load %__pyobject** %4		; <%__pyobject*> [#uses=3]
	%6 = load i32* @_Py_RefTotal		; <i32> [#uses=1]
	%7 = add i32 %6, 1		; <i32> [#uses=1]
	store i32 %7, i32* @_Py_RefTotal
	%8 = getelementptr %__pyobject* %5, i32 0, i32 2		; <i32*> [#uses=2]
	%9 = load i32* %8		; <i32> [#uses=1]
	%10 = add i32 %9, 1		; <i32> [#uses=1]
	store i32 %10, i32* %8
	store %__pyobject* %5, %__pyobject** %initial_stack_pointer
	ret %__pyobject* %5
}
""")

    def test_run_simple_function(self):
        def foo():
            pass
        self.assertEquals(foo.__code__.__use_llvm__, False)
        foo.__code__.__use_llvm__ = True
        self.assertEquals(foo.__code__.__use_llvm__, True)
        self.assertEquals(None, foo())


def test_main():
    run_unittest(LlvmTests)


if __name__ == "__main__":
    test_main()
