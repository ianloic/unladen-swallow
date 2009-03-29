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
	%__function_type = type %__pyobject* (%__pyframeobject*, %__pyobject*, %__pyobject*, %__pyobject*)
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

define %__pyobject* @foo(%__pyframeobject*, %__pyobject*, %__pyobject*, %__pyobject*) {
entry:
	%stack_pointer_addr = alloca %__pyobject**		; <%__pyobject***> [#uses=9]
	%4 = getelementptr %__pyframeobject* %0, i32 0, i32 8		; <%__pyobject***> [#uses=1]
	%initial_stack_pointer = load %__pyobject*** %4		; <%__pyobject**> [#uses=1]
	store %__pyobject** %initial_stack_pointer, %__pyobject*** %stack_pointer_addr
	%5 = getelementptr %__pyframeobject* %0, i32 0, i32 3		; <%__pycodeobject**> [#uses=1]
	%co = load %__pycodeobject** %5		; <%__pycodeobject*> [#uses=1]
	%6 = getelementptr %__pycodeobject* %co, i32 0, i32 6		; <%__pyobject**> [#uses=1]
	%7 = load %__pyobject** %6		; <%__pyobject*> [#uses=1]
	%consts = bitcast %__pyobject* %7 to %__pytupleobject*		; <%__pytupleobject*> [#uses=2]
	br label %L

L:		; preds = %entry
	%8 = getelementptr %__pytupleobject* %consts, i32 0, i32 2, i32 0		; <%__pyobject**> [#uses=1]
	%9 = load %__pyobject** %8		; <%__pyobject*> [#uses=2]
	%10 = load i32* @_Py_RefTotal		; <i32> [#uses=1]
	%11 = add i32 %10, 1		; <i32> [#uses=1]
	store i32 %11, i32* @_Py_RefTotal
	%12 = getelementptr %__pyobject* %9, i32 0, i32 2		; <i32*> [#uses=2]
	%13 = load i32* %12		; <i32> [#uses=1]
	%14 = add i32 %13, 1		; <i32> [#uses=1]
	store i32 %14, i32* %12
	%15 = load %__pyobject*** %stack_pointer_addr		; <%__pyobject**> [#uses=2]
	store %__pyobject* %9, %__pyobject** %15
	%16 = getelementptr %__pyobject** %15, i32 1		; <%__pyobject**> [#uses=1]
	store %__pyobject** %16, %__pyobject*** %stack_pointer_addr
	%17 = load %__pyobject*** %stack_pointer_addr		; <%__pyobject**> [#uses=1]
	%18 = getelementptr %__pyobject** %17, i32 -1		; <%__pyobject**> [#uses=2]
	%19 = load %__pyobject** %18		; <%__pyobject*> [#uses=1]
	store %__pyobject** %18, %__pyobject*** %stack_pointer_addr
	ret %__pyobject* %19

L1:		; No predecessors!
	br label %L2

L2:		; preds = %L1
	%20 = getelementptr %__pytupleobject* %consts, i32 0, i32 2, i32 0		; <%__pyobject**> [#uses=1]
	%21 = load %__pyobject** %20		; <%__pyobject*> [#uses=2]
	%22 = load i32* @_Py_RefTotal		; <i32> [#uses=1]
	%23 = add i32 %22, 1		; <i32> [#uses=1]
	store i32 %23, i32* @_Py_RefTotal
	%24 = getelementptr %__pyobject* %21, i32 0, i32 2		; <i32*> [#uses=2]
	%25 = load i32* %24		; <i32> [#uses=1]
	%26 = add i32 %25, 1		; <i32> [#uses=1]
	store i32 %26, i32* %24
	%27 = load %__pyobject*** %stack_pointer_addr		; <%__pyobject**> [#uses=2]
	store %__pyobject* %21, %__pyobject** %27
	%28 = getelementptr %__pyobject** %27, i32 1		; <%__pyobject**> [#uses=1]
	store %__pyobject** %28, %__pyobject*** %stack_pointer_addr
	%29 = load %__pyobject*** %stack_pointer_addr		; <%__pyobject**> [#uses=1]
	%30 = getelementptr %__pyobject** %29, i32 -1		; <%__pyobject**> [#uses=2]
	%31 = load %__pyobject** %30		; <%__pyobject*> [#uses=1]
	store %__pyobject** %30, %__pyobject*** %stack_pointer_addr
	ret %__pyobject* %31
}
""")


def test_main():
    run_unittest(LlvmTests)


if __name__ == "__main__":
    test_main()
