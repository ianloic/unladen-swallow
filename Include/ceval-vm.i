LABEL(NOP) /* NOP ( -- ) */
NAME("NOP")
{
DEF_CA
NEXT_P0;
{
}

NEXT_P1;
LABEL2(NOP)
NEXT_P2;
}

LABEL(LOAD_FAST) /* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
vm_Cell2i(IMM_ARG(IPTOS,305397760 ),i);
INC_IP(1);
stack_pointer += 1;
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
NEXT_P2;

}
Py_INCREF(a);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
LABEL2(LOAD_FAST)
NEXT_P2;
}

LABEL(LOAD_CONST) /* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
vm_Cell2i(IMM_ARG(IPTOS,305397761 ),i);
INC_IP(1);
stack_pointer += 1;
{
x = a = GETITEM(consts, i);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
LABEL2(LOAD_CONST)
NEXT_P2;
}

LABEL(STORE_FAST) /* STORE_FAST ( #i a -- ) */
NAME("STORE_FAST")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397762 ),i);
vm_Obj2a(stack_pointerTOS,a);
INC_IP(1);
stack_pointer += -1;
{
SETLOCAL(i, a);
}

NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_FAST)
NEXT_P2;
}

LABEL(POP_TOP) /* POP_TOP ( a -- dec:a ) */
NAME("POP_TOP")
{
DEF_CA
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a);
stack_pointer += -1;
{
}

NEXT_P1;
vm_a2decref(a,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(POP_TOP)
NEXT_P2;
}

LABEL(ROT_TWO) /* ROT_TWO ( a1 a2 -- a2 a1 ) */
NAME("ROT_TWO")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
{
}

NEXT_P1;
vm_a2Obj(a2,stack_pointer[-2]);
vm_a2Obj(a1,stack_pointerTOS);
LABEL2(ROT_TWO)
NEXT_P2;
}

LABEL(ROT_THREE) /* ROT_THREE ( a1 a2 a3 -- a3 a1 a2 ) */
NAME("ROT_THREE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
{
}

NEXT_P1;
vm_a2Obj(a3,stack_pointer[-3]);
vm_a2Obj(a1,stack_pointer[-2]);
vm_a2Obj(a2,stack_pointerTOS);
LABEL2(ROT_THREE)
NEXT_P2;
}

LABEL(ROT_FOUR) /* ROT_FOUR ( a1 a2 a3 a4 -- a4 a1 a2 a3 ) */
NAME("ROT_FOUR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
Obj a4;
NEXT_P0;
vm_Obj2a(stack_pointer[-4],a1);
vm_Obj2a(stack_pointer[-3],a2);
vm_Obj2a(stack_pointer[-2],a3);
vm_Obj2a(stack_pointerTOS,a4);
{
}

NEXT_P1;
vm_a2Obj(a4,stack_pointer[-4]);
vm_a2Obj(a1,stack_pointer[-3]);
vm_a2Obj(a2,stack_pointer[-2]);
vm_a2Obj(a3,stack_pointerTOS);
LABEL2(ROT_FOUR)
NEXT_P2;
}

LABEL(DUP_TOP) /* DUP_TOP ( a -- a a  inc:a ) */
NAME("DUP_TOP")
{
DEF_CA
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a);
stack_pointer += 1;
{
}

NEXT_P1;
IF_stack_pointerTOS(vm_a2Obj(a,stack_pointer[-2]););
vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
LABEL2(DUP_TOP)
NEXT_P2;
}

LABEL(DUP_TOP_TWO) /* DUP_TOP_TWO ( a1 a2 -- a1 a2 a1 a2  inc:a1 inc:a2 ) */
NAME("DUP_TOP_TWO")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += 2;
{
}

NEXT_P1;
IF_stack_pointerTOS(vm_a2Obj(a2,stack_pointer[-3]););
vm_a2Obj(a1,stack_pointer[-2]);
vm_a2Obj(a2,stack_pointerTOS);
vm_a2incref(a1,__none__[1]);
vm_a2incref(a2,__none__TOS);
LABEL2(DUP_TOP_TWO)
NEXT_P2;
}

LABEL(DUP_TOP_THREE) /* DUP_TOP_THREE ( a1 a2 a3 -- a1 a2 a3 a1 a2 a3  inc:a1 inc:a2 inc:a3 ) */
NAME("DUP_TOP_THREE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += 3;
{
}

NEXT_P1;
IF_stack_pointerTOS(vm_a2Obj(a3,stack_pointer[-4]););
vm_a2Obj(a1,stack_pointer[-3]);
vm_a2Obj(a2,stack_pointer[-2]);
vm_a2Obj(a3,stack_pointerTOS);
vm_a2incref(a1,__none__[2]);
vm_a2incref(a2,__none__[1]);
vm_a2incref(a3,__none__TOS);
LABEL2(DUP_TOP_THREE)
NEXT_P2;
}

LABEL(UNARY_POSITIVE) /* UNARY_POSITIVE ( a1 -- a2   dec:a1  next:a2 ) */
NAME("UNARY_POSITIVE")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
{
a2 = PyNumber_Positive(a1);
}

NEXT_P1;
vm_a2Obj(a2,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a2,__none__TOS);
LABEL2(UNARY_POSITIVE)
NEXT_P2;
}

LABEL(UNARY_NEGATIVE) /* UNARY_NEGATIVE ( a1 -- a2  dec:a1  next:a2 ) */
NAME("UNARY_NEGATIVE")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
{
a2 = PyNumber_Negative(a1);
}

NEXT_P1;
vm_a2Obj(a2,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a2,__none__TOS);
LABEL2(UNARY_NEGATIVE)
NEXT_P2;
}

LABEL(UNARY_NOT) /* UNARY_NOT ( -- ) */
NAME("UNARY_NOT")
{
DEF_CA
NEXT_P0;
{
a1 = TOP();
err = PyObject_IsTrue(a1);
Py_DECREF(a1);
if (err == 0) {
        Py_INCREF(Py_True);
        SET_TOP(Py_True);
        NEXT();
} else if (err > 0) {
        Py_INCREF(Py_False);
        SET_TOP(Py_False);
        err = 0;
        NEXT();
}
STACKADJ(-1);
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(UNARY_NOT)
NEXT_P2;
}

LABEL(UNARY_CONVERT) /* UNARY_CONVERT ( a1 -- a2  dec:a1  next:a2 ) */
NAME("UNARY_CONVERT")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
{
a2 = PyObject_Repr(a1);
}

NEXT_P1;
vm_a2Obj(a2,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a2,__none__TOS);
LABEL2(UNARY_CONVERT)
NEXT_P2;
}

LABEL(UNARY_INVERT) /* UNARY_INVERT ( a1 -- a2  dec:a1  next:a2 ) */
NAME("UNARY_INVERT")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
{
a2 = PyNumber_Invert(a1);
}

NEXT_P1;
vm_a2Obj(a2,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a2,__none__TOS);
LABEL2(UNARY_INVERT)
NEXT_P2;
}

LABEL(BINARY_POWER) /* BINARY_POWER ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_POWER")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Power(a1, a2, Py_None);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_POWER)
NEXT_P2;
}

LABEL(BINARY_MULTIPLY) /* BINARY_MULTIPLY ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_MULTIPLY")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Multiply(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_MULTIPLY)
NEXT_P2;
}

LABEL(BINARY_DIVIDE) /* BINARY_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (!_Py_QnewFlag)
        a = PyNumber_Divide(a1, a2);
else
        a = PyNumber_TrueDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_DIVIDE)
NEXT_P2;
}

LABEL(BINARY_TRUE_DIVIDE) /* BINARY_TRUE_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_TRUE_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_TrueDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_TRUE_DIVIDE)
NEXT_P2;
}

LABEL(BINARY_FLOOR_DIVIDE) /* BINARY_FLOOR_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_FLOOR_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_FloorDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_FLOOR_DIVIDE)
NEXT_P2;
}

LABEL(BINARY_MODULO) /* BINARY_MODULO ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_MODULO")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyString_CheckExact(a1))
	a = PyString_Format(a1, a2);
else
	a = PyNumber_Remainder(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_MODULO)
NEXT_P2;
}

LABEL(BINARY_ADD) /* BINARY_ADD ( a1 a2 -- a   next:a ) */
NAME("BINARY_ADD")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int + int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u + v;
        if ((i^u) < 0 && (i^v) < 0)
                a = PyNumber_Add(a1, a2);
        else
                a = PyInt_FromLong(i);
        Py_DECREF(a1);
} else if (PyString_CheckExact(a1) && PyString_CheckExact(a2)) {
        /* Look in the parallel PyInstructions object to find the
           symbolic opcode. */
        int opcode = PyInst_GET_OPCODE(
                &((PyInstructionsObject *)co->co_code)->inst[INSTR_OFFSET()]);
        a = string_concatenate(a1, a2, f, opcode, (next_instr+1)->oparg);
        /* string_concatenate consumed the ref to v */
} else {
        a = PyNumber_Add(a1, a2);
        Py_DECREF(a1);
}
Py_DECREF(a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_ADD)
NEXT_P2;
}

LABEL(BINARY_SUBTRACT) /* BINARY_SUBTRACT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_SUBTRACT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int - int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u - v;
        if ((i^u) < 0 && (i^~v) < 0)
                a = PyNumber_Subtract(a1, a2);
        else
                a = PyInt_FromLong(i);
} else
        a = PyNumber_Subtract(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_SUBTRACT)
NEXT_P2;
}

LABEL(BINARY_SUBSCR) /* BINARY_SUBSCR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_SUBSCR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyList_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: list[int] */
        Py_ssize_t i = PyInt_AsSsize_t(a2);
        if (i < 0)
                i += PyList_GET_SIZE(a1);
        if (i >= 0 && i < PyList_GET_SIZE(a1)) {
                a = PyList_GET_ITEM(a1, i);
                Py_INCREF(a);
        } else
                a = PyObject_GetItem(a1, a2);
} else
        a = PyObject_GetItem(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_SUBSCR)
NEXT_P2;
}

LABEL(BINARY_LSHIFT) /* BINARY_LSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_LSHIFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Lshift(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_LSHIFT)
NEXT_P2;
}

LABEL(BINARY_RSHIFT) /* BINARY_RSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_RSHIFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Rshift(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_RSHIFT)
NEXT_P2;
}

LABEL(BINARY_AND) /* BINARY_AND ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_AND")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_And(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_AND)
NEXT_P2;
}

LABEL(BINARY_XOR) /* BINARY_XOR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_XOR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Xor(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_XOR)
NEXT_P2;
}

LABEL(BINARY_OR) /* BINARY_OR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_OR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_Or(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BINARY_OR)
NEXT_P2;
}

LABEL(LIST_APPEND) /* LIST_APPEND ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("LIST_APPEND")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
err = PyList_Append(a1, a2);
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(LIST_APPEND)
NEXT_P2;
}

LABEL(INPLACE_POWER) /* INPLACE_POWER ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_POWER")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlacePower(a1, a2, Py_None);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_POWER)
NEXT_P2;
}

LABEL(INPLACE_MULTIPLY) /* INPLACE_MULTIPLY ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_MULTIPLY")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceMultiply(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_MULTIPLY)
NEXT_P2;
}

LABEL(INPLACE_DIVIDE) /* INPLACE_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (!_Py_QnewFlag)
        a = PyNumber_InPlaceDivide(a1, a2);
else
        a = PyNumber_InPlaceTrueDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_DIVIDE)
NEXT_P2;
}

LABEL(INPLACE_TRUE_DIVIDE) /* INPLACE_TRUE_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_TRUE_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceTrueDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_TRUE_DIVIDE)
NEXT_P2;
}

LABEL(INPLACE_FLOOR_DIVIDE) /* INPLACE_FLOOR_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_FLOOR_DIVIDE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceFloorDivide(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_FLOOR_DIVIDE)
NEXT_P2;
}

LABEL(INPLACE_MODULO) /* INPLACE_MODULO ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_MODULO")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceRemainder(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_MODULO)
NEXT_P2;
}

LABEL(INPLACE_ADD) /* INPLACE_ADD ( a1 a2 -- a   next:a ) */
NAME("INPLACE_ADD")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int + int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u + v;
        if ((i^u) < 0 && (i^v) < 0)
                a = PyNumber_InPlaceAdd(a1, a2);
        else
                a = PyInt_FromLong(i);
        Py_DECREF(a1);
} else if (PyString_CheckExact(a1) && PyString_CheckExact(a2)) {
        /* Look in the parallel PyInstructions object to find the
           symbolic opcode. */
        int opcode = PyInst_GET_OPCODE(
                &((PyInstructionsObject *)co->co_code)->inst[INSTR_OFFSET()]);
        a = string_concatenate(a1, a2, f, opcode, (next_instr+1)->oparg);
        /* string_concatenate consumed the ref to v */
} else {
        a = PyNumber_InPlaceAdd(a1, a2);
        Py_DECREF(a1);
}
Py_DECREF(a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_ADD)
NEXT_P2;
}

LABEL(INPLACE_SUBTRACT) /* INPLACE_SUBTRACT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_SUBTRACT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int - int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u - v;
        if ((i^u) < 0 && (i^~v) < 0)
                a = PyNumber_InPlaceSubtract(a1, a2);
        else
                a = PyInt_FromLong(i);
} else
        a = PyNumber_InPlaceSubtract(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_SUBTRACT)
NEXT_P2;
}

LABEL(INPLACE_LSHIFT) /* INPLACE_LSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_LSHIFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceLshift(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_LSHIFT)
NEXT_P2;
}

LABEL(INPLACE_RSHIFT) /* INPLACE_RSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_RSHIFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceRshift(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_RSHIFT)
NEXT_P2;
}

LABEL(INPLACE_AND) /* INPLACE_AND ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_AND")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceAnd(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_AND)
NEXT_P2;
}

LABEL(INPLACE_XOR) /* INPLACE_XOR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_XOR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceXor(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_XOR)
NEXT_P2;
}

LABEL(INPLACE_OR) /* INPLACE_OR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_OR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PyNumber_InPlaceOr(a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(INPLACE_OR)
NEXT_P2;
}

LABEL(SLICE_NONE) /* SLICE_NONE ( a1 -- a2  dec:a1  next:a2 ) */
NAME("SLICE_NONE")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
{
a2 = apply_slice(a1, NULL, NULL);
}

NEXT_P1;
vm_a2Obj(a2,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a2,__none__TOS);
LABEL2(SLICE_NONE)
NEXT_P2;
}

LABEL(SLICE_LEFT) /* SLICE_LEFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("SLICE_LEFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = apply_slice(a1, a2, NULL);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(SLICE_LEFT)
NEXT_P2;
}

LABEL(SLICE_RIGHT) /* SLICE_RIGHT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("SLICE_RIGHT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = apply_slice(a1, NULL, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(SLICE_RIGHT)
NEXT_P2;
}

LABEL(SLICE_BOTH) /* SLICE_BOTH ( a1 a2 a3 -- a  dec:a1 dec:a2 dec:a3  next:a ) */
NAME("SLICE_BOTH")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -2;
{
a = apply_slice(a1, a2, a3);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(SLICE_BOTH)
NEXT_P2;
}

LABEL(STORE_SLICE_NONE) /* STORE_SLICE_NONE ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("STORE_SLICE_NONE")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
err = assign_slice(a2, NULL, NULL, a1);
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_SLICE_NONE)
NEXT_P2;
}

LABEL(STORE_SLICE_LEFT) /* STORE_SLICE_LEFT ( a1 a2 a3 -- dec:a1 dec:a2 dec:a3  next:error ) */
NAME("STORE_SLICE_LEFT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -3;
{
err = assign_slice(a2, a3, NULL, a1);
}

NEXT_P1;
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_SLICE_LEFT)
NEXT_P2;
}

LABEL(STORE_SLICE_RIGHT) /* STORE_SLICE_RIGHT ( a1 a2 a3 -- dec:a1 dec:a2 dec:a3  next:error ) */
NAME("STORE_SLICE_RIGHT")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -3;
{
err = assign_slice(a2, NULL, a3, a1);
}

NEXT_P1;
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_SLICE_RIGHT)
NEXT_P2;
}

LABEL(STORE_SLICE_BOTH) /* STORE_SLICE_BOTH ( a1 a2 a3 a4 -- dec:a1 dec:a2 dec:a3 dec:a4  next:error ) */
NAME("STORE_SLICE_BOTH")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
Obj a4;
NEXT_P0;
vm_Obj2a(stack_pointer[-4],a1);
vm_Obj2a(stack_pointer[-3],a2);
vm_Obj2a(stack_pointer[-2],a3);
vm_Obj2a(stack_pointerTOS,a4);
stack_pointer += -4;
{
err = assign_slice(a2, a3, a4, a1); /* a2[a3:a4] = a1 */
}

NEXT_P1;
vm_a2decref(a1,__none__[3]);
vm_a2decref(a2,__none__[2]);
vm_a2decref(a3,__none__[1]);
vm_a2decref(a4,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_SLICE_BOTH)
NEXT_P2;
}

LABEL(DELETE_SLICE_NONE) /* DELETE_SLICE_NONE ( a1 -- dec:a1  next:error ) */
NAME("DELETE_SLICE_NONE")
{
DEF_CA
Obj a1;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
stack_pointer += -1;
{
err = assign_slice(a1, NULL, NULL, (PyObject *) NULL);
}

NEXT_P1;
vm_a2decref(a1,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_SLICE_NONE)
NEXT_P2;
}

LABEL(DELETE_SLICE_LEFT) /* DELETE_SLICE_LEFT ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("DELETE_SLICE_LEFT")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
err = assign_slice(a1, a2, NULL, (PyObject *) NULL);
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_SLICE_LEFT)
NEXT_P2;
}

LABEL(DELETE_SLICE_RIGHT) /* DELETE_SLICE_RIGHT ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("DELETE_SLICE_RIGHT")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
err = assign_slice(a1, NULL, a2, (PyObject *) NULL);
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_SLICE_RIGHT)
NEXT_P2;
}

LABEL(DELETE_SLICE_BOTH) /* DELETE_SLICE_BOTH ( a1 a2 a3 -- dec:a1 dec:a2 dec:a3  next:error ) */
NAME("DELETE_SLICE_BOTH")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -3;
{
err = assign_slice(a1, a2, a3, (PyObject *) NULL); /* del a1[a2:a3] */
}

NEXT_P1;
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_SLICE_BOTH)
NEXT_P2;
}

LABEL(STORE_SUBSCR) /* STORE_SUBSCR ( a1 a2 a3 -- dec:a1 dec:a2 dec:a3  next:error ) */
NAME("STORE_SUBSCR")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -3;
{
err = PyObject_SetItem(a2, a3, a1);
}

NEXT_P1;
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_SUBSCR)
NEXT_P2;
}

LABEL(DELETE_SUBSCR) /* DELETE_SUBSCR ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("DELETE_SUBSCR")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
err = PyObject_DelItem(a1, a2);
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_SUBSCR)
NEXT_P2;
}

LABEL(RAISE_VARARGS_ZERO) /* RAISE_VARARGS_ZERO ( -- next:on_error ) */
NAME("RAISE_VARARGS_ZERO")
{
DEF_CA
NEXT_P0;
{
why = do_raise(NULL, NULL, NULL);
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
LABEL2(RAISE_VARARGS_ZERO)
NEXT_P2;
}

LABEL(RAISE_VARARGS_ONE) /* RAISE_VARARGS_ONE ( a1 -- next:on_error ) */
NAME("RAISE_VARARGS_ONE")
{
DEF_CA
Obj a1;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
stack_pointer += -1;
{
why = do_raise(a1, NULL, NULL);
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(RAISE_VARARGS_ONE)
NEXT_P2;
}

LABEL(RAISE_VARARGS_TWO) /* RAISE_VARARGS_TWO ( a1 a2 -- next:on_error ) */
NAME("RAISE_VARARGS_TWO")
{
DEF_CA
Obj a1;
Obj a2;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -2;
{
why = do_raise(a1, a2, NULL);
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(RAISE_VARARGS_TWO)
NEXT_P2;
}

LABEL(RAISE_VARARGS_THREE) /* RAISE_VARARGS_THREE ( a1 a2 a3 -- next:on_error ) */
NAME("RAISE_VARARGS_THREE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -3;
{
why = do_raise(a1, a2, a3);
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(RAISE_VARARGS_THREE)
NEXT_P2;
}

LABEL(LOAD_NAME) /* LOAD_NAME ( #i -- ) */
NAME("LOAD_NAME")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397763 ),i);
INC_IP(1);
{
a2 = GETITEM(names, i);
if ((a1 = f->f_locals) == NULL) {
        PyErr_Format(PyExc_SystemError,
                     "no locals when loading %s",
                     PyObject_REPR(a2));
        why = WHY_EXCEPTION;
        ERROR();
}
if (PyDict_CheckExact(a1)) {
        x = PyDict_GetItem(a1, a2);
        Py_XINCREF(x);
} else {
        x = PyObject_GetItem(a1, a2);
        if (x == NULL && PyErr_Occurred()) {
                if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
                        why = WHY_EXCEPTION;
                        ERROR();
                }
                PyErr_Clear();
        }
}
if (x == NULL) {
        x = PyDict_GetItem(f->f_globals, a2);
        if (x == NULL) {
                x = PyDict_GetItem(f->f_builtins, a2);
                if (x == NULL) {
                        format_exc_check_arg(PyExc_NameError, NAME_ERROR_MSG, a2);
                        why = WHY_EXCEPTION;
                        ERROR();
                }
        }
        Py_INCREF(x);
}
PUSH(x);
NEXT();
}

NEXT_P1;
LABEL2(LOAD_NAME)
NEXT_P2;
}

LABEL(LOAD_GLOBAL) /* LOAD_GLOBAL ( #i -- ) */
NAME("LOAD_GLOBAL")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397764 ),i);
INC_IP(1);
{
a1 = GETITEM(names, i);
if (PyString_CheckExact(a1)) {
        /* Inline the PyDict_GetItem() calls.
	   WARNING: this is an extreme speed hack.
	   Do not try this at home. */
        long hash = ((PyStringObject *)a1)->ob_shash;
        if (hash != -1) {
                PyDictObject *d = (PyDictObject *)(f->f_globals);
                PyDictEntry  *e = d->ma_lookup(d, a1, hash);
                if (e == NULL) {
                        why = WHY_EXCEPTION;
                        ERROR();
                }
                x = e->me_value;
                if (x != NULL) {
                        Py_INCREF(x);
                        PUSH(x);
                        NEXT();
                }
                d = (PyDictObject *)(f->f_builtins);
                e = d->ma_lookup(d, a1, hash);
                if (e == NULL) {
                        why = WHY_EXCEPTION;
                        ERROR();
                }
                x = e->me_value;
                if (x != NULL) {
                        Py_INCREF(x);
                        PUSH(x);
                        NEXT();
                }
                goto load_global_error;
        }
}
/* This is the un-inlined version of the code above */
x = PyDict_GetItem(f->f_globals, a1);
if (x == NULL) {
        x = PyDict_GetItem(f->f_builtins, a1);
        if (x == NULL) {
        load_global_error:
                _PyEval_RaiseForGlobalNameError(a1);
                why = WHY_EXCEPTION;
                ERROR();
        }
}
Py_INCREF(x);
PUSH(x);
NEXT();
}

NEXT_P1;
LABEL2(LOAD_GLOBAL)
NEXT_P2;
}

LABEL(LOAD_CLOSURE) /* LOAD_CLOSURE ( #i -- a  inc:a  next:a ) */
NAME("LOAD_CLOSURE")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
vm_Cell2i(IMM_ARG(IPTOS,305397765 ),i);
INC_IP(1);
stack_pointer += 1;
{
a = freevars[i];
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(LOAD_CLOSURE)
NEXT_P2;
}

LABEL(LOAD_DEREF) /* LOAD_DEREF ( #i -- ) */
NAME("LOAD_DEREF")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397766 ),i);
INC_IP(1);
{
x = freevars[i];
a2 = PyCell_Get(x);
if (a2 != NULL) {
        PUSH(a2);
        NEXT();
}
why = WHY_EXCEPTION;
/* Don't stomp existing exception */
if (PyErr_Occurred())
        ERROR();
if (i < PyTuple_GET_SIZE(co->co_cellvars)) {
        a1 = PyTuple_GET_ITEM(co->co_cellvars, i);
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                a1);
} else {
        a1 = PyTuple_GET_ITEM(
                co->co_freevars,
                i - PyTuple_GET_SIZE(co->co_cellvars));
        format_exc_check_arg(
                PyExc_NameError,
                UNBOUNDFREE_ERROR_MSG,
                a1);
}
ERROR();
}

NEXT_P1;
LABEL2(LOAD_DEREF)
NEXT_P2;
}

LABEL(LOAD_ATTR) /* LOAD_ATTR ( #i a1 -- a3  dec:a1  next:a3 ) */
NAME("LOAD_ATTR")
{
DEF_CA
Oparg i;
Obj a1;
Obj a3;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397767 ),i);
vm_Obj2a(stack_pointerTOS,a1);
INC_IP(1);
{
a2 = GETITEM(names, i);
a3 = PyObject_GetAttr(a1, a2);
}

NEXT_P1;
vm_a2Obj(a3,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a3,__none__TOS);
LABEL2(LOAD_ATTR)
NEXT_P2;
}

LABEL(RETURN_VALUE) /* RETURN_VALUE ( a -- next:fast_block_end ) */
NAME("RETURN_VALUE")
{
DEF_CA
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a);
stack_pointer += -1;
{
retval = a;
why = WHY_RETURN;
}

NEXT_P1;
vm_fast_block_end2next(fast_block_end,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(RETURN_VALUE)
NEXT_P2;
}

LABEL(YIELD_VALUE) /* YIELD_VALUE ( a -- next:fast_yield ) */
NAME("YIELD_VALUE")
{
DEF_CA
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a);
stack_pointer += -1;
{
retval = a;
f->f_stacktop = stack_pointer;
why = WHY_YIELD;
}

NEXT_P1;
vm_fast_yield2next(fast_yield,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(YIELD_VALUE)
NEXT_P2;
}

LABEL(POP_BLOCK) /* POP_BLOCK ( -- next:next_opcode ) */
NAME("POP_BLOCK")
{
DEF_CA
NEXT_P0;
{
PyTryBlock *b = PyFrame_BlockPop(f);
while (STACK_LEVEL() > b->b_level) {
        a1 = POP();
        Py_DECREF(a1);
}
}

NEXT_P1;
vm_next_opcode2next(next_opcode,__none__TOS);
LABEL2(POP_BLOCK)
NEXT_P2;
}

LABEL(END_FINALLY) /* END_FINALLY ( a1 -- next:on_error ) */
NAME("END_FINALLY")
{
DEF_CA
Obj a1;
NEXT_P0;
vm_Obj2a(stack_pointerTOS,a1);
stack_pointer += -1;
{
if (PyInt_Check(a1)) {
        why = (enum why_code) PyInt_AS_LONG(a1);
        assert(why != WHY_YIELD);
        if (why == WHY_RETURN || why == WHY_CONTINUE)
                retval = POP();
} else if (PyExceptionClass_Check(a1) || PyString_Check(a1)) {
        a2 = POP();
        a3 = POP();
        PyErr_Restore(a1, a2, a3);
        why = WHY_RERAISE;
        ERROR();
} else if (a1 != Py_None) {
        PyErr_SetString(PyExc_SystemError, "'finally' pops bad exception");
        why = WHY_EXCEPTION;
}
Py_DECREF(a1);
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(END_FINALLY)
NEXT_P2;
}

LABEL(BUILD_TUPLE) /* BUILD_TUPLE ( #i -- ) */
NAME("BUILD_TUPLE")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397768 ),i);
INC_IP(1);
{
x = PyTuple_New(i);
if (x != NULL) {
        for (; --i >= 0;) {
                a1 = POP();
                PyTuple_SET_ITEM(x, i, a1);
        }
        PUSH(x);
        NEXT();
}
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(BUILD_TUPLE)
NEXT_P2;
}

LABEL(BUILD_LIST) /* BUILD_LIST ( #i -- ) */
NAME("BUILD_LIST")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397769 ),i);
INC_IP(1);
{
x = PyList_New(i);
if (x != NULL) {
        for (; --i >= 0;) {
                a1 = POP();
                PyList_SET_ITEM(x, i, a1);
        }
        PUSH(x);
        NEXT();
}
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(BUILD_LIST)
NEXT_P2;
}

LABEL(BUILD_MAP) /* BUILD_MAP ( #i -- a  next:a ) */
NAME("BUILD_MAP")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
vm_Cell2i(IMM_ARG(IPTOS,305397770 ),i);
INC_IP(1);
stack_pointer += 1;
{
a = _PyDict_NewPresized(i);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2next(a,__none__TOS);
LABEL2(BUILD_MAP)
NEXT_P2;
}

LABEL(STORE_MAP) /* STORE_MAP ( a1 a2 a3 -- a1  dec:a2 dec:a3 next:error ) */
NAME("STORE_MAP")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -2;
{
/* a1 == dict, a2 == value, a3 == key */
assert (PyDict_CheckExact(a1));
err = PyDict_SetItem(a1, a3, a2);  /* a1[a3] = a2 */
}

NEXT_P1;
IF_stack_pointerTOS(vm_a2Obj(a1,stack_pointerTOS););
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
LABEL2(STORE_MAP)
NEXT_P2;
}

LABEL(BUILD_SLICE_TWO) /* BUILD_SLICE_TWO ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BUILD_SLICE_TWO")
{
DEF_CA
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
stack_pointer += -1;
{
a = PySlice_New(a1, a2, NULL);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BUILD_SLICE_TWO)
NEXT_P2;
}

LABEL(BUILD_SLICE_THREE) /* BUILD_SLICE_THREE ( a1 a2 a3 -- a  dec:a1 dec:a2 dec:a3  next:a ) */
NAME("BUILD_SLICE_THREE")
{
DEF_CA
Obj a1;
Obj a2;
Obj a3;
Obj a;
NEXT_P0;
vm_Obj2a(stack_pointer[-3],a1);
vm_Obj2a(stack_pointer[-2],a2);
vm_Obj2a(stack_pointerTOS,a3);
stack_pointer += -2;
{
a = PySlice_New(a1, a2, a3);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[2]);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(BUILD_SLICE_THREE)
NEXT_P2;
}

LABEL(STORE_NAME) /* STORE_NAME ( #i -- ) */
NAME("STORE_NAME")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397771 ),i);
INC_IP(1);
{
a2 = GETITEM(names, i);
a1 = POP();
if ((x = f->f_locals) != NULL) {
        if (PyDict_CheckExact(x))
                err = PyDict_SetItem(x, a2, a1);
        else
                err = PyObject_SetItem(x, a2, a1);
        Py_DECREF(a1);
        if (err == 0) NEXT();
        why = WHY_EXCEPTION;
        ERROR();
}
PyErr_Format(PyExc_SystemError,
             "no locals found when storing %s",
             PyObject_REPR(a2));
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(STORE_NAME)
NEXT_P2;
}

LABEL(STORE_ATTR) /* STORE_ATTR ( #i a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("STORE_ATTR")
{
DEF_CA
Oparg i;
Obj a1;
Obj a2;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397772 ),i);
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
INC_IP(1);
stack_pointer += -2;
{
a3 = GETITEM(names, i);
err = PyObject_SetAttr(a2, a3, a1); /* a2.a3 = a1 */
}

NEXT_P1;
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_ATTR)
NEXT_P2;
}

LABEL(STORE_GLOBAL) /* STORE_GLOBAL ( #i a1 -- dec:a1  next:error ) */
NAME("STORE_GLOBAL")
{
DEF_CA
Oparg i;
Obj a1;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397773 ),i);
vm_Obj2a(stack_pointerTOS,a1);
INC_IP(1);
stack_pointer += -1;
{
a2 = GETITEM(names, i);
err = PyDict_SetItem(f->f_globals, a2, a1);
}

NEXT_P1;
vm_a2decref(a1,__none__TOS);
vm_error2next(error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_GLOBAL)
NEXT_P2;
}

LABEL(STORE_DEREF) /* STORE_DEREF ( #i a -- dec:a  next:next_opcode ) */
NAME("STORE_DEREF")
{
DEF_CA
Oparg i;
Obj a;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397774 ),i);
vm_Obj2a(stack_pointerTOS,a);
INC_IP(1);
stack_pointer += -1;
{
x = freevars[i];
PyCell_Set(x, a);
}

NEXT_P1;
vm_a2decref(a,__none__TOS);
vm_next_opcode2next(next_opcode,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(STORE_DEREF)
NEXT_P2;
}

LABEL(DELETE_NAME) /* DELETE_NAME ( #i -- ) */
NAME("DELETE_NAME")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397775 ),i);
INC_IP(1);
{
a1 = GETITEM(names, i);
if ((x = f->f_locals) != NULL) {
        if ((err = PyObject_DelItem(x, a1)) != 0) {
                format_exc_check_arg(PyExc_NameError, NAME_ERROR_MSG, a1);
                why = WHY_EXCEPTION;
        }
        ERROR();
}
PyErr_Format(PyExc_SystemError,
             "no locals when deleting %s",
             PyObject_REPR(a1));
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(DELETE_NAME)
NEXT_P2;
}

LABEL(DELETE_ATTR) /* DELETE_ATTR ( #i a1 -- dec:a1  next:on_error ) */
NAME("DELETE_ATTR")
{
DEF_CA
Oparg i;
Obj a1;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397776 ),i);
vm_Obj2a(stack_pointerTOS,a1);
INC_IP(1);
stack_pointer += -1;
{
a2 = GETITEM(names, i);
if (0 != PyObject_SetAttr(a1, a2, (PyObject *) NULL) /* del a1.a2 */)
        why = WHY_EXCEPTION;
}

NEXT_P1;
vm_a2decref(a1,__none__TOS);
vm_on_error2next(on_error,__none__TOS);
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(DELETE_ATTR)
NEXT_P2;
}

LABEL(DELETE_GLOBAL) /* DELETE_GLOBAL ( #i -- next:on_error ) */
NAME("DELETE_GLOBAL")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397777 ),i);
INC_IP(1);
{
a1 = GETITEM(names, i);
if ((err = PyDict_DelItem(f->f_globals, a1)) != 0) {
        _PyEval_RaiseForGlobalNameError(a1);
        why = WHY_EXCEPTION;
}
}

NEXT_P1;
vm_on_error2next(on_error,__none__TOS);
LABEL2(DELETE_GLOBAL)
NEXT_P2;
}

LABEL(DELETE_FAST) /* DELETE_FAST ( #i -- ) */
NAME("DELETE_FAST")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397778 ),i);
INC_IP(1);
{
x = GETLOCAL(i);
if (x != NULL) {
        SETLOCAL(i, NULL);
        NEXT();
}
format_exc_check_arg(
        PyExc_UnboundLocalError,
        UNBOUNDLOCAL_ERROR_MSG,
        PyTuple_GetItem(co->co_varnames, i));
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(DELETE_FAST)
NEXT_P2;
}

LABEL(UNPACK_SEQUENCE) /* UNPACK_SEQUENCE ( #i a1 -- ) */
NAME("UNPACK_SEQUENCE")
{
DEF_CA
Oparg i;
Obj a1;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397779 ),i);
vm_Obj2a(stack_pointerTOS,a1);
INC_IP(1);
stack_pointer += -1;
{
if (PyTuple_CheckExact(a1) && PyTuple_GET_SIZE(a1) == i) {
        PyObject **items = ((PyTupleObject *)a1)->ob_item;
        while (i--) {
                a2 = items[i];
                Py_INCREF(a2);
                PUSH(a2);
        }
        Py_DECREF(a1);
        NEXT();
} else if (PyList_CheckExact(a1) && PyList_GET_SIZE(a1) == i) {
        PyObject **items = ((PyListObject *)a1)->ob_item;
        while (i--) {
                a2 = items[i];
                Py_INCREF(a2);
                PUSH(a2);
        }
} else if (unpack_iterable(a1, i, stack_pointer + i)) {
        stack_pointer += i;
} else {
        /* unpack_iterable() raised an exception */
        why = WHY_EXCEPTION;
}
Py_DECREF(a1);
ERROR();
}

NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(UNPACK_SEQUENCE)
NEXT_P2;
}

LABEL(COMPARE_OP) /* COMPARE_OP ( #i a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("COMPARE_OP")
{
DEF_CA
Oparg i;
Obj a1;
Obj a2;
Obj a;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397780 ),i);
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
INC_IP(1);
stack_pointer += -1;
{
if (PyInt_CheckExact(a2) && PyInt_CheckExact(a1)) {
        /* INLINE: cmp(int, int) */
        register long u, v;
        register int res;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        switch (i) {
        case PyCmp_LT: res = u <  v; break;
        case PyCmp_LE: res = u <= v; break;
        case PyCmp_EQ: res = u == v; break;
        case PyCmp_NE: res = u != v; break;
        case PyCmp_GT: res = u >  v; break;
        case PyCmp_GE: res = u >= v; break;
        case PyCmp_IS: res = a1 == a2; break;
        case PyCmp_IS_NOT: res = a1 != a2; break;
        default: res = -1;
        }
        if (res < 0)
                a = cmp_outcome(i, a1, a2);
        else {
                a = res ? Py_True : Py_False;
                Py_INCREF(a);
        }
} else
        a = cmp_outcome(i, a1, a2);
}

NEXT_P1;
vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
LABEL2(COMPARE_OP)
NEXT_P2;
}

LABEL(JUMP_FORWARD) /* JUMP_FORWARD ( #i -- ) */
NAME("JUMP_FORWARD")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397781 ),i);
INC_IP(1);
{
JUMPBY(i);
}

NEXT_P1;
LABEL2(JUMP_FORWARD)
NEXT_P2;
}

LABEL(POP_JUMP_IF_FALSE) /* POP_JUMP_IF_FALSE ( #i -- ) */
NAME("POP_JUMP_IF_FALSE")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397782 ),i);
INC_IP(1);
{
a1 = POP();
if (a1 == Py_True)
        ;
else if (a1 == Py_False)
        JUMPTO(i);
else {
        err = PyObject_IsTrue(a1);
        Py_DECREF(a1);
        if (err < 0) {
                why = WHY_EXCEPTION;
                ERROR();
        }
        else if (err == 0)
                JUMPTO(i);
        NEXT();
}
Py_DECREF(a1);
}

NEXT_P1;
LABEL2(POP_JUMP_IF_FALSE)
NEXT_P2;
}

LABEL(POP_JUMP_IF_TRUE) /* POP_JUMP_IF_TRUE ( #i -- ) */
NAME("POP_JUMP_IF_TRUE")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397783 ),i);
INC_IP(1);
{
a1 = POP();
if (a1 == Py_False)
        ;
else if (a1 == Py_True)
        JUMPTO(i);
else {
        err = PyObject_IsTrue(a1);
        Py_DECREF(a1);
        if (err < 0) {
                why = WHY_EXCEPTION;
                ERROR();
        }
        else if (err > 0)
                JUMPTO(i);
        NEXT();
}
Py_DECREF(a1);
}

NEXT_P1;
LABEL2(POP_JUMP_IF_TRUE)
NEXT_P2;
}

LABEL(JUMP_IF_FALSE_OR_POP) /* JUMP_IF_FALSE_OR_POP ( #i -- ) */
NAME("JUMP_IF_FALSE_OR_POP")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397784 ),i);
INC_IP(1);
{
a1 = TOP();
if (a1 == Py_True) {
        STACKADJ(-1);
        Py_DECREF(a1);
}
else if (a1 == Py_False)
        JUMPTO(i);
else {
        err = PyObject_IsTrue(a1);
        if (err < 0) {
                why = WHY_EXCEPTION;
                ERROR();
        }
        else if (err == 0)
                JUMPTO(i);
        else {
                STACKADJ(-1);
                Py_DECREF(a1);
        }
        NEXT();
}
}

NEXT_P1;
LABEL2(JUMP_IF_FALSE_OR_POP)
NEXT_P2;
}

LABEL(JUMP_IF_TRUE_OR_POP) /* JUMP_IF_TRUE_OR_POP ( #i -- ) */
NAME("JUMP_IF_TRUE_OR_POP")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397785 ),i);
INC_IP(1);
{
a1 = TOP();
if (a1 == Py_False) {
        STACKADJ(-1);
        Py_DECREF(a1);
}
else if (a1 == Py_True)
        JUMPTO(i);
else {
        err = PyObject_IsTrue(a1);
        if (err < 0) {
                why = WHY_EXCEPTION;
                ERROR();
        }
        else if (err > 0)
                JUMPTO(i);
        else {
                STACKADJ(-1);
                Py_DECREF(a1);
        }
        NEXT();
}
}

NEXT_P1;
LABEL2(JUMP_IF_TRUE_OR_POP)
NEXT_P2;
}

LABEL(JUMP_ABSOLUTE) /* JUMP_ABSOLUTE ( #i -- next:next_opcode ) */
NAME("JUMP_ABSOLUTE")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397786 ),i);
INC_IP(1);
{
JUMPTO(i);
}

NEXT_P1;
vm_next_opcode2next(next_opcode,__none__TOS);
LABEL2(JUMP_ABSOLUTE)
NEXT_P2;
}

LABEL(GET_ITER) /* GET_ITER ( -- ) */
NAME("GET_ITER")
{
DEF_CA
NEXT_P0;
{
/* before: [obj]; after [getiter(obj)] */
a1 = TOP();
x = PyObject_GetIter(a1);
Py_DECREF(a1);
if (x != NULL) {
        SET_TOP(x);
        NEXT();
}
STACKADJ(-1);
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(GET_ITER)
NEXT_P2;
}

LABEL(FOR_ITER) /* FOR_ITER ( #i -- ) */
NAME("FOR_ITER")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397787 ),i);
INC_IP(1);
{
/* before: [iter]; after: [iter, iter()] *or* [] */
a1 = TOP();
x = (*a1->ob_type->tp_iternext)(a1);
if (x != NULL) {
        PUSH(x);
        NEXT();
}
if (PyErr_Occurred()) {
        if (!PyErr_ExceptionMatches(PyExc_StopIteration)) {
                why = WHY_EXCEPTION;
                ERROR();
        }
        PyErr_Clear();
}
/* iterator ended normally */
a1 = POP();
Py_DECREF(a1);
JUMPBY(i);
NEXT();
}

NEXT_P1;
LABEL2(FOR_ITER)
NEXT_P2;
}

LABEL(BREAK_LOOP) /* BREAK_LOOP ( -- next:fast_block_end ) */
NAME("BREAK_LOOP")
{
DEF_CA
NEXT_P0;
{
why = WHY_BREAK;
}

NEXT_P1;
vm_fast_block_end2next(fast_block_end,__none__TOS);
LABEL2(BREAK_LOOP)
NEXT_P2;
}

LABEL(CONTINUE_LOOP) /* CONTINUE_LOOP ( #i -- ) */
NAME("CONTINUE_LOOP")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397788 ),i);
INC_IP(1);
{
retval = PyInt_FromLong(i);
if (!retval) {
        why = WHY_EXCEPTION;
        ERROR();
}
why = WHY_CONTINUE;
goto fast_block_end;
}

NEXT_P1;
LABEL2(CONTINUE_LOOP)
NEXT_P2;
}

LABEL(SETUP_LOOP) /* SETUP_LOOP ( #i -- next:next_opcode ) */
NAME("SETUP_LOOP")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397789 ),i);
INC_IP(1);
{
PyFrame_BlockSetup(f, SETUP_LOOP, INSTR_OFFSET() + i, STACK_LEVEL());
}

NEXT_P1;
vm_next_opcode2next(next_opcode,__none__TOS);
LABEL2(SETUP_LOOP)
NEXT_P2;
}

LABEL(SETUP_EXCEPT) /* SETUP_EXCEPT ( #i -- next:next_opcode ) */
NAME("SETUP_EXCEPT")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397790 ),i);
INC_IP(1);
{
PyFrame_BlockSetup(f, SETUP_EXCEPT, INSTR_OFFSET() + i, STACK_LEVEL());
}

NEXT_P1;
vm_next_opcode2next(next_opcode,__none__TOS);
LABEL2(SETUP_EXCEPT)
NEXT_P2;
}

LABEL(SETUP_FINALLY) /* SETUP_FINALLY ( #i -- next:next_opcode ) */
NAME("SETUP_FINALLY")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397791 ),i);
INC_IP(1);
{
PyFrame_BlockSetup(f, SETUP_FINALLY, INSTR_OFFSET() + i, STACK_LEVEL());
}

NEXT_P1;
vm_next_opcode2next(next_opcode,__none__TOS);
LABEL2(SETUP_FINALLY)
NEXT_P2;
}

LABEL(WITH_CLEANUP) /* WITH_CLEANUP ( -- ) */
NAME("WITH_CLEANUP")
{
DEF_CA
NEXT_P0;
{
{
        /* At the top of the stack are 1-3 values indicating
           how/why we entered the finally clause:
           - TOP = None
           - (TOP, SECOND) = (WHY_{RETURN,CONTINUE}), retval
           - TOP = WHY_*; no retval below it
           - (TOP, SECOND, THIRD) = exc_info()
           Below them is EXIT, the context.__exit__ bound method.
           In the last case, we must call
             EXIT(TOP, SECOND, THIRD)
           otherwise we must call
             EXIT(None, None, None)

           In all cases, we remove EXIT from the stack, leaving
           the rest in the same order.

           In addition, if the stack represents an exception,
           *and* the function call returns a 'true' value, we
           "zap" this information, to prevent END_FINALLY from
           re-raising the exception.  (But non-local gotos
           should still be resumed.)
        */

        PyObject *exit_func;

        a1 = POP();
        if (a1 == Py_None) {
                exit_func = TOP();
                SET_TOP(a1);
                a2 = a3 = Py_None;
        }
        else if (PyInt_Check(a1)) {
                switch(PyInt_AS_LONG(a1)) {
                case WHY_RETURN:
                case WHY_CONTINUE:
                        /* Retval in TOP. */
                        exit_func = SECOND();
                        SET_SECOND(TOP());
                        SET_TOP(a1);
                        break;
                default:
                        exit_func = TOP();
                        SET_TOP(a1);
                        break;
                }
                a1 = a2 = a3 = Py_None;
        }
        else {
                a2 = TOP();
                a3 = SECOND();
                exit_func = THIRD();
                SET_TOP(a1);
                SET_SECOND(a2);
                SET_THIRD(a3);
        }
        /* XXX Not the fastest way to call it... */
        x = PyObject_CallFunctionObjArgs(exit_func, a1, a2, a3,
                                         NULL);
        Py_DECREF(exit_func);
        if (x == NULL) {
                why = WHY_EXCEPTION;
                ERROR(); /* Go to error exit */
        }
        if (a1 != Py_None)
                err = PyObject_IsTrue(x);
        else
                err = 0;
        Py_DECREF(x);
        if (err < 0) {
                why = WHY_EXCEPTION;
                ERROR(); /* Go to error exit */
        }
        else if (err > 0) {
                /* There was an exception and a true return */
                STACKADJ(-2);
                Py_INCREF(Py_None);
                SET_TOP(Py_None);
                Py_DECREF(a1);
                Py_DECREF(a2);
                Py_DECREF(a3);
        } else {
                /* The stack was rearranged to remove EXIT
                   above. Let END_FINALLY do its thing */
        }
        ERROR();
}
}

NEXT_P1;
LABEL2(WITH_CLEANUP)
NEXT_P2;
}

LABEL(CALL_FUNCTION) /* CALL_FUNCTION ( #i -- ) */
NAME("CALL_FUNCTION")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397792 ),i);
INC_IP(1);
{
PyObject **sp = stack_pointer;
#ifdef WITH_TSC
x = _PyEval_CallFunction(&sp, i, &intr0, &intr1);
#else
x = _PyEval_CallFunction(&sp, i);
#endif
stack_pointer = sp;
PUSH(x);
if (x != NULL) NEXT();
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(CALL_FUNCTION)
NEXT_P2;
}

LABEL(CALL_FUNCTION_VAR_KW) /* CALL_FUNCTION_VAR_KW ( #i -- ) */
NAME("CALL_FUNCTION_VAR_KW")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397793 ),i);
INC_IP(1);
{
int var_kw = i & 0x0000FFFF;
int oparg  = i>>16;
int na     = oparg & 0xff;
int nk     = (oparg>>8) & 0xff;
int flags  = var_kw & 3;
int n      = na + 2 * nk;
PyObject **pfunc, *func, **sp;
if (flags & CALL_FLAG_VAR)
        n++;
if (flags & CALL_FLAG_KW)
        n++;
pfunc = stack_pointer - n - 1;
func  = *pfunc;
if (PyMethod_Check(func) && PyMethod_GET_SELF(func) != NULL) {
        PyObject *self = PyMethod_GET_SELF(func);
        Py_INCREF(self);
        func = PyMethod_GET_FUNCTION(func);
        Py_INCREF(func);
        Py_DECREF(*pfunc);
        *pfunc = self;
        na++;
        n++;
} else
        Py_INCREF(func);
sp = stack_pointer;
x = ext_do_call(func, &sp, flags, na, nk);
stack_pointer = sp;
Py_DECREF(func);
while (stack_pointer > pfunc) {
        a1 = POP();
        Py_DECREF(a1);
}
PUSH(x);
if (x != NULL) NEXT();
why = WHY_EXCEPTION;
ERROR();
}

NEXT_P1;
LABEL2(CALL_FUNCTION_VAR_KW)
NEXT_P2;
}

LABEL(MAKE_CLOSURE) /* MAKE_CLOSURE ( #i -- ) */
NAME("MAKE_CLOSURE")
{
DEF_CA
Oparg i;
NEXT_P0;
vm_Cell2i(IMM_ARG(IPTOS,305397794 ),i);
INC_IP(1);
{
a1 = POP(); /* code object */
if ((x = PyFunction_New(a1, f->f_globals)) == NULL)
        why = WHY_EXCEPTION;
Py_DECREF(a1);
if (x != NULL) {
        a1 = POP();
        if (PyFunction_SetClosure(x, a1) != 0) {
                /* Can't happen unless bytecode is corrupt. */
                why = WHY_EXCEPTION;
        }
        Py_DECREF(a1);
}
if (x != NULL && i > 0) {
        a1 = PyTuple_New(i);
        if (a1 == NULL) {
                Py_DECREF(x);
                why = WHY_EXCEPTION;
                ERROR();
        }
        while (--i >= 0) {
                a2 = POP();
                PyTuple_SET_ITEM(a1, i, a2);
        }
        if (PyFunction_SetDefaults(x, a1) != 0) {
                /* Can't happen unless
                   PyFunction_SetDefaults changes. */
                why = WHY_EXCEPTION;
        }
        Py_DECREF(a1);
}
PUSH(x);
ERROR();
}

NEXT_P1;
LABEL2(MAKE_CLOSURE)
NEXT_P2;
}

LABEL(CC)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397795 ),i);
stack_pointer += 2;
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointer[-2]);
vm_a2incref(a,__none__[1]);
}
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397796 ),i);
INC_IP(2);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
}
NEXT_P1;
LABEL2(CC)
NEXT_P2;
}

LABEL(CF)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397797 ),i);
stack_pointer += 2;
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointer[-2]);
vm_a2incref(a,__none__TOS);
}
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397798 ),i);
INC_IP(2);
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointerTOS);
}
NEXT_P1;
LABEL2(CF)
NEXT_P2;
}

LABEL(FC)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397799 ),i);
stack_pointer += 2;
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;
        INC_IP(1);
stack_pointer += -1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointer[-2]);
}
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397800 ),i);
INC_IP(2);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
}
NEXT_P1;
LABEL2(FC)
NEXT_P2;
}

LABEL(FF)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397801 ),i);
stack_pointer += 2;
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;
        INC_IP(1);
stack_pointer += -1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointer[-2]);
}
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397802 ),i);
INC_IP(2);
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointerTOS);
}
NEXT_P1;
LABEL2(FF)
NEXT_P2;
}

LABEL(FA)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397803 ),i);
stack_pointer += 1;
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;
        INC_IP(1);

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,_stack_pointer0);
}
/* LOAD_ATTR ( #i a1 -- a3  dec:a1  next:a3 ) */
NAME("LOAD_ATTR")
{
Oparg i;
Obj a1;
Obj a3;
vm_Cell2i(IMM_ARG(IP[1],305397804 ),i);
vm_Obj2a(_stack_pointer0,a1);
INC_IP(2);
{
a2 = GETITEM(names, i);
a3 = PyObject_GetAttr(a1, a2);
}

vm_a2Obj(a3,stack_pointerTOS);
vm_a2decref(a1,__none__TOS);
vm_a2next(a3,__none__TOS);
}
NEXT_P1;
LABEL2(FA)
NEXT_P2;
}

LABEL(CBINARY_POWER)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397805 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_POWER ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_POWER")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Power(a1, a2, Py_None);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_POWER)
NEXT_P2;
}

LABEL(CBINARY_MULTIPLY)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397806 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_MULTIPLY ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_MULTIPLY")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Multiply(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_MULTIPLY)
NEXT_P2;
}

LABEL(CBINARY_DIVIDE)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397807 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_DIVIDE")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (!_Py_QnewFlag)
        a = PyNumber_Divide(a1, a2);
else
        a = PyNumber_TrueDivide(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_DIVIDE)
NEXT_P2;
}

LABEL(CBINARY_TRUE_DIVIDE)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397808 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_TRUE_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_TRUE_DIVIDE")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_TrueDivide(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_TRUE_DIVIDE)
NEXT_P2;
}

LABEL(CBINARY_FLOOR_DIVIDE)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397809 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_FLOOR_DIVIDE ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_FLOOR_DIVIDE")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_FloorDivide(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_FLOOR_DIVIDE)
NEXT_P2;
}

LABEL(CBINARY_MODULO)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397810 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_MODULO ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_MODULO")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyString_CheckExact(a1))
	a = PyString_Format(a1, a2);
else
	a = PyNumber_Remainder(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_MODULO)
NEXT_P2;
}

LABEL(CBINARY_ADD)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397811 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_ADD ( a1 a2 -- a   next:a ) */
NAME("BINARY_ADD")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int + int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u + v;
        if ((i^u) < 0 && (i^v) < 0)
                a = PyNumber_Add(a1, a2);
        else
                a = PyInt_FromLong(i);
        Py_DECREF(a1);
} else if (PyString_CheckExact(a1) && PyString_CheckExact(a2)) {
        /* Look in the parallel PyInstructions object to find the
           symbolic opcode. */
        int opcode = PyInst_GET_OPCODE(
                &((PyInstructionsObject *)co->co_code)->inst[INSTR_OFFSET()]);
        a = string_concatenate(a1, a2, f, opcode, (next_instr+1)->oparg);
        /* string_concatenate consumed the ref to v */
} else {
        a = PyNumber_Add(a1, a2);
        Py_DECREF(a1);
}
Py_DECREF(a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_ADD)
NEXT_P2;
}

LABEL(CBINARY_SUBTRACT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397812 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_SUBTRACT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_SUBTRACT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int - int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u - v;
        if ((i^u) < 0 && (i^~v) < 0)
                a = PyNumber_Subtract(a1, a2);
        else
                a = PyInt_FromLong(i);
} else
        a = PyNumber_Subtract(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_SUBTRACT)
NEXT_P2;
}

LABEL(CBINARY_SUBSCR)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397813 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_SUBSCR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_SUBSCR")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyList_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: list[int] */
        Py_ssize_t i = PyInt_AsSsize_t(a2);
        if (i < 0)
                i += PyList_GET_SIZE(a1);
        if (i >= 0 && i < PyList_GET_SIZE(a1)) {
                a = PyList_GET_ITEM(a1, i);
                Py_INCREF(a);
        } else
                a = PyObject_GetItem(a1, a2);
} else
        a = PyObject_GetItem(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_SUBSCR)
NEXT_P2;
}

LABEL(CBINARY_LSHIFT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397814 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_LSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_LSHIFT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Lshift(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_LSHIFT)
NEXT_P2;
}

LABEL(CBINARY_RSHIFT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397815 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_RSHIFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_RSHIFT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Rshift(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_RSHIFT)
NEXT_P2;
}

LABEL(CBINARY_AND)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397816 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_AND ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_AND")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_And(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_AND)
NEXT_P2;
}

LABEL(CBINARY_XOR)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397817 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_XOR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_XOR")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Xor(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_XOR)
NEXT_P2;
}

LABEL(CBINARY_OR)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397818 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* BINARY_OR ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("BINARY_OR")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_Or(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CBINARY_OR)
NEXT_P2;
}

LABEL(CLIST_APPEND)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397819 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* LIST_APPEND ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("LIST_APPEND")
{
Obj a1;
Obj a2;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
stack_pointer += -1;
{
err = PyList_Append(a1, a2);
}

vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
}
NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(CLIST_APPEND)
NEXT_P2;
}

LABEL(CINPLACE_ADD)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397820 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* INPLACE_ADD ( a1 a2 -- a   next:a ) */
NAME("INPLACE_ADD")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int + int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u + v;
        if ((i^u) < 0 && (i^v) < 0)
                a = PyNumber_InPlaceAdd(a1, a2);
        else
                a = PyInt_FromLong(i);
        Py_DECREF(a1);
} else if (PyString_CheckExact(a1) && PyString_CheckExact(a2)) {
        /* Look in the parallel PyInstructions object to find the
           symbolic opcode. */
        int opcode = PyInst_GET_OPCODE(
                &((PyInstructionsObject *)co->co_code)->inst[INSTR_OFFSET()]);
        a = string_concatenate(a1, a2, f, opcode, (next_instr+1)->oparg);
        /* string_concatenate consumed the ref to v */
} else {
        a = PyNumber_InPlaceAdd(a1, a2);
        Py_DECREF(a1);
}
Py_DECREF(a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CINPLACE_ADD)
NEXT_P2;
}

LABEL(CINPLACE_SUBTRACT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397821 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* INPLACE_SUBTRACT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_SUBTRACT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
if (PyInt_CheckExact(a1) && PyInt_CheckExact(a2)) {
        /* INLINE: int - int */
        register long u, v, i;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        i = u - v;
        if ((i^u) < 0 && (i^~v) < 0)
                a = PyNumber_InPlaceSubtract(a1, a2);
        else
                a = PyInt_FromLong(i);
} else
        a = PyNumber_InPlaceSubtract(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CINPLACE_SUBTRACT)
NEXT_P2;
}

LABEL(CINPLACE_AND)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397822 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* INPLACE_AND ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("INPLACE_AND")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = PyNumber_InPlaceAnd(a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CINPLACE_AND)
NEXT_P2;
}

LABEL(CSLICE_LEFT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397823 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* SLICE_LEFT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("SLICE_LEFT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = apply_slice(a1, a2, NULL);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CSLICE_LEFT)
NEXT_P2;
}

LABEL(CSLICE_RIGHT)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397824 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* SLICE_RIGHT ( a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("SLICE_RIGHT")
{
Obj a1;
Obj a2;
Obj a;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
{
a = apply_slice(a1, NULL, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CSLICE_RIGHT)
NEXT_P2;
}

LABEL(CDELETE_SUBSCR)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397825 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* DELETE_SUBSCR ( a1 a2 -- dec:a1 dec:a2  next:error ) */
NAME("DELETE_SUBSCR")
{
Obj a1;
Obj a2;
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
stack_pointer += -1;
{
err = PyObject_DelItem(a1, a2);
}

vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_error2next(error,__none__TOS);
}
NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(CDELETE_SUBSCR)
NEXT_P2;
}

LABEL(CCOMPARE_OP)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397826 ),i);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer1);
vm_a2incref(a,__none__TOS);
}
/* COMPARE_OP ( #i a1 a2 -- a  dec:a1 dec:a2  next:a ) */
NAME("COMPARE_OP")
{
Oparg i;
Obj a1;
Obj a2;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397827 ),i);
vm_Obj2a(stack_pointerTOS,a1);
vm_Obj2a(_stack_pointer1,a2);
INC_IP(2);
{
if (PyInt_CheckExact(a2) && PyInt_CheckExact(a1)) {
        /* INLINE: cmp(int, int) */
        register long u, v;
        register int res;
        u = PyInt_AS_LONG(a1);
        v = PyInt_AS_LONG(a2);
        switch (i) {
        case PyCmp_LT: res = u <  v; break;
        case PyCmp_LE: res = u <= v; break;
        case PyCmp_EQ: res = u == v; break;
        case PyCmp_NE: res = u != v; break;
        case PyCmp_GT: res = u >  v; break;
        case PyCmp_GE: res = u >= v; break;
        case PyCmp_IS: res = a1 == a2; break;
        case PyCmp_IS_NOT: res = a1 != a2; break;
        default: res = -1;
        }
        if (res < 0)
                a = cmp_outcome(i, a1, a2);
        else {
                a = res ? Py_True : Py_False;
                Py_INCREF(a);
        }
} else
        a = cmp_outcome(i, a1, a2);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2decref(a1,__none__[1]);
vm_a2decref(a2,__none__TOS);
vm_a2next(a,__none__TOS);
}
NEXT_P1;
LABEL2(CCOMPARE_OP)
NEXT_P2;
}

LABEL(STORE_LOAD_FAST)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
/* STORE_FAST ( #i a -- ) */
NAME("STORE_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397828 ),i);
vm_Obj2a(stack_pointerTOS,a);
{
SETLOCAL(i, a);
}

}
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397829 ),i);
INC_IP(2);
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointerTOS);
}
NEXT_P1;
LABEL2(STORE_LOAD_FAST)
NEXT_P2;
}

LABEL(POP_LOAD_FAST)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
/* POP_TOP ( a -- dec:a ) */
NAME("POP_TOP")
{
Obj a;
vm_Obj2a(stack_pointerTOS,a);
{
}

vm_a2decref(a,__none__TOS);
}
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397830 ),i);
INC_IP(1);
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointerTOS);
}
NEXT_P1;
LABEL2(POP_LOAD_FAST)
NEXT_P2;
}

LABEL(C_CALL_FUNCTION)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397831 ),i);
stack_pointer += 1;
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
}
/* CALL_FUNCTION ( #i -- ) */
NAME("CALL_FUNCTION")
{
Oparg i;
vm_Cell2i(IMM_ARG(IP[1],305397832 ),i);
INC_IP(2);
{
PyObject **sp = stack_pointer;
#ifdef WITH_TSC
x = _PyEval_CallFunction(&sp, i, &intr0, &intr1);
#else
x = _PyEval_CallFunction(&sp, i);
#endif
stack_pointer = sp;
PUSH(x);
if (x != NULL) NEXT();
why = WHY_EXCEPTION;
ERROR();
}

}
NEXT_P1;
LABEL2(C_CALL_FUNCTION)
NEXT_P2;
}

LABEL(F_CALL_FUNCTION)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_FAST ( #i -- a ) */
NAME("LOAD_FAST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397833 ),i);
stack_pointer += 1;
{
x = a = GETLOCAL(i);
if (a == NULL) {
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                PyTuple_GetItem(co->co_varnames, i));
        why = WHY_EXCEPTION;
        /* On exception, make sure the stack is valid. */
        have_error = 1;
        INC_IP(1);

vm_a2Obj(a,stack_pointerTOS);
NEXT_P1;
NEXT_P2;

}
Py_INCREF(a);
}

vm_a2Obj(a,stack_pointerTOS);
}
/* CALL_FUNCTION ( #i -- ) */
NAME("CALL_FUNCTION")
{
Oparg i;
vm_Cell2i(IMM_ARG(IP[1],305397834 ),i);
INC_IP(2);
{
PyObject **sp = stack_pointer;
#ifdef WITH_TSC
x = _PyEval_CallFunction(&sp, i, &intr0, &intr1);
#else
x = _PyEval_CallFunction(&sp, i);
#endif
stack_pointer = sp;
PUSH(x);
if (x != NULL) NEXT();
why = WHY_EXCEPTION;
ERROR();
}

}
NEXT_P1;
LABEL2(F_CALL_FUNCTION)
NEXT_P2;
}

LABEL(CC_CALL_FUNCTION)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Cell MAYBE_UNUSED _IP1;
Cell MAYBE_UNUSED _IP2;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
NEXT_P0;
IF_stack_pointerTOS(stack_pointer[-1] = stack_pointerTOS);
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397835 ),i);
stack_pointer += 2;
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointer[-2]);
vm_a2incref(a,__none__[1]);
}
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IP[1],305397836 ),i);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,stack_pointerTOS);
vm_a2incref(a,__none__TOS);
}
/* CALL_FUNCTION ( #i -- ) */
NAME("CALL_FUNCTION")
{
Oparg i;
vm_Cell2i(IMM_ARG(IP[2],305397837 ),i);
INC_IP(3);
{
PyObject **sp = stack_pointer;
#ifdef WITH_TSC
x = _PyEval_CallFunction(&sp, i, &intr0, &intr1);
#else
x = _PyEval_CallFunction(&sp, i);
#endif
stack_pointer = sp;
PUSH(x);
if (x != NULL) NEXT();
why = WHY_EXCEPTION;
ERROR();
}

}
NEXT_P1;
LABEL2(CC_CALL_FUNCTION)
NEXT_P2;
}

LABEL(C_STORE_MAP)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
Obj MAYBE_UNUSED _stack_pointer1;
Obj MAYBE_UNUSED _stack_pointer2;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397838 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer2);
vm_a2incref(a,__none__TOS);
}
/* STORE_MAP ( a1 a2 a3 -- a1  dec:a2 dec:a3 next:error ) */
NAME("STORE_MAP")
{
Obj a1;
Obj a2;
Obj a3;
vm_Obj2a(stack_pointer[-2],a1);
vm_Obj2a(stack_pointerTOS,a2);
vm_Obj2a(_stack_pointer2,a3);
stack_pointer += -1;
{
/* a1 == dict, a2 == value, a3 == key */
assert (PyDict_CheckExact(a1));
err = PyDict_SetItem(a1, a3, a2);  /* a1[a3] = a2 */
}

vm_a2Obj(a1,stack_pointerTOS);
vm_a2decref(a2,__none__[1]);
vm_a2decref(a3,__none__TOS);
vm_error2next(error,__none__TOS);
}
NEXT_P1;
LABEL2(C_STORE_MAP)
NEXT_P2;
}

LABEL(RETURN_CONST)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
/* LOAD_CONST ( #i -- a  inc:a ) */
NAME("LOAD_CONST")
{
Oparg i;
Obj a;
vm_Cell2i(IMM_ARG(IPTOS,305397839 ),i);
INC_IP(1);
{
x = a = GETITEM(consts, i);
}

vm_a2Obj(a,_stack_pointer0);
vm_a2incref(a,__none__TOS);
}
/* RETURN_VALUE ( a -- next:fast_block_end ) */
NAME("RETURN_VALUE")
{
Obj a;
vm_Obj2a(_stack_pointer0,a);
{
retval = a;
why = WHY_RETURN;
}

vm_fast_block_end2next(fast_block_end,__none__TOS);
}
NEXT_P1;
LABEL2(RETURN_CONST)
NEXT_P2;
}

LABEL(POP_JUMP_ABSOLUTE)
{
DEF_CA
Cell MAYBE_UNUSED _IP0;
Obj MAYBE_UNUSED _stack_pointer0;
NEXT_P0;
/* POP_TOP ( a -- dec:a ) */
NAME("POP_TOP")
{
Obj a;
vm_Obj2a(stack_pointerTOS,a);
stack_pointer += -1;
{
}

vm_a2decref(a,__none__TOS);
}
/* JUMP_ABSOLUTE ( #i -- next:next_opcode ) */
NAME("JUMP_ABSOLUTE")
{
Oparg i;
vm_Cell2i(IMM_ARG(IPTOS,305397840 ),i);
INC_IP(1);
{
JUMPTO(i);
}

vm_next_opcode2next(next_opcode,__none__TOS);
}
NEXT_P1;
IF_stack_pointerTOS(stack_pointerTOS = stack_pointer[-1]);
LABEL2(POP_JUMP_ABSOLUTE)
NEXT_P2;
}

