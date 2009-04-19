// -*- C++ -*-
#ifndef PYTHON_LL_COMPILE_H
#define PYTHON_LL_COMPILE_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Support/IRBuilder.h"
#include <string>

struct PyGlobalLlvmData;

namespace py {

/// Helps the compiler build LLVM functions corresponding to Python
/// functions.  This class maintains the IRBuilder and several Value*s
/// set up in the entry block.
class LlvmFunctionBuilder {
    LlvmFunctionBuilder(const LlvmFunctionBuilder &);  // Not implemented.
    void operator=(const LlvmFunctionBuilder &);  // Not implemented.

public:
    LlvmFunctionBuilder(PyGlobalLlvmData *global_data, const std::string& name);

    llvm::Function *function() { return function_; }
    llvm::IRBuilder<>& builder() { return builder_; }

    /// Sets the insert point to next_block, inserting an
    /// unconditional branch to there if the current block doesn't yet
    /// have a terminator instruction.
    void FallThroughTo(llvm::BasicBlock *next_block);

    /// The following methods operate like the opcodes with the same
    /// name.
    void LOAD_CONST(int index);
    void LOAD_FAST(int index);
    void STORE_FAST(int index);
    void DELETE_FAST(int index);

    void SETUP_LOOP(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough);
    void GET_ITER();
    void FOR_ITER(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough);
    void POP_BLOCK();

    void SETUP_EXCEPT(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough);
    void SETUP_FINALLY(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough);
    void END_FINALLY();

    void JUMP_FORWARD(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough) {
        JUMP_ABSOLUTE(target, fallthrough);
    }
    void JUMP_ABSOLUTE(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough);

    void POP_JUMP_IF_FALSE(llvm::BasicBlock *target,
                           llvm::BasicBlock *fallthrough);
    void POP_JUMP_IF_TRUE(llvm::BasicBlock *target,
                          llvm::BasicBlock *fallthrough);
    void JUMP_IF_FALSE_OR_POP(llvm::BasicBlock *target,
                             llvm::BasicBlock *fallthrough);
    void JUMP_IF_TRUE_OR_POP(llvm::BasicBlock *target,
                             llvm::BasicBlock *fallthrough);

    void BREAK_LOOP();
    void RETURN_VALUE();

    void POP_TOP();
    void DUP_TOP();
    void DUP_TOP_TWO();
    void ROT_THREE();

    void BINARY_ADD();
    void BINARY_SUBTRACT();
    void BINARY_MULTIPLY();
    void BINARY_TRUE_DIVIDE();
    void BINARY_DIVIDE();
    void BINARY_MODULO();
    void BINARY_POWER();
    void BINARY_LSHIFT();
    void BINARY_RSHIFT();
    void BINARY_OR();
    void BINARY_XOR();
    void BINARY_AND();
    void BINARY_FLOOR_DIVIDE();
    void BINARY_SUBSCR();

    void INPLACE_ADD();
    void INPLACE_SUBTRACT();
    void INPLACE_MULTIPLY();
    void INPLACE_TRUE_DIVIDE();
    void INPLACE_DIVIDE();
    void INPLACE_MODULO();
    void INPLACE_POWER();
    void INPLACE_LSHIFT();
    void INPLACE_RSHIFT();
    void INPLACE_OR();
    void INPLACE_XOR();
    void INPLACE_AND();
    void INPLACE_FLOOR_DIVIDE();

    void UNARY_CONVERT();
    void UNARY_INVERT();
    void UNARY_POSITIVE();
    void UNARY_NEGATIVE();
    void UNARY_NOT();

    void SLICE_NONE();
    void SLICE_LEFT();
    void SLICE_RIGHT();
    void SLICE_BOTH();
    void STORE_SLICE_NONE();
    void STORE_SLICE_LEFT();
    void STORE_SLICE_RIGHT();
    void STORE_SLICE_BOTH();
    void DELETE_SLICE_NONE();
    void DELETE_SLICE_LEFT();
    void DELETE_SLICE_RIGHT();
    void DELETE_SLICE_BOTH();
    void STORE_SUBSCR();
    void DELETE_SUBSCR();
    void STORE_MAP();
    void LIST_APPEND();

    void COMPARE_OP(int cmp_op);
    void CALL_FUNCTION(int num_args);
    void CALL_FUNCTION_VAR_KW(int num_args);

    void BUILD_TUPLE(int size);
    void BUILD_LIST(int size);
    void BUILD_MAP(int size);
    void BUILD_SLICE_TWO();
    void BUILD_SLICE_THREE();

    void LOAD_GLOBAL(int index);
    void STORE_GLOBAL(int index);
    void DELETE_GLOBAL(int index);

    void LOAD_ATTR(int index);
    void STORE_ATTR(int index);
    void DELETE_ATTR(int index);

    void RAISE_VARARGS_ZERO();
    void RAISE_VARARGS_ONE();
    void RAISE_VARARGS_TWO();
    void RAISE_VARARGS_THREE();

#define UNIMPLEMENTED(NAME) \
    void NAME() { \
        InsertAbort(#NAME); \
    }
#define UNIMPLEMENTED_I(NAME) \
    void NAME(int index) { \
        InsertAbort(#NAME); \
    }
#define UNIMPLEMENTED_J(NAME) \
    void NAME(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough) { \
        InsertAbort(#NAME); \
    }

    UNIMPLEMENTED(DUP_TOP_THREE)
    UNIMPLEMENTED(ROT_TWO)
    UNIMPLEMENTED(ROT_FOUR)
    UNIMPLEMENTED(WITH_CLEANUP)
    UNIMPLEMENTED(YIELD_VALUE)

    UNIMPLEMENTED_I(LOAD_DEREF);
    UNIMPLEMENTED_I(STORE_DEREF);
    UNIMPLEMENTED_I(LOAD_NAME);
    UNIMPLEMENTED_I(STORE_NAME);
    UNIMPLEMENTED_I(DELETE_NAME);
    UNIMPLEMENTED_I(LOAD_CLOSURE)
    UNIMPLEMENTED_I(MAKE_CLOSURE)
    UNIMPLEMENTED_I(UNPACK_SEQUENCE)

    UNIMPLEMENTED_J(CONTINUE_LOOP);

#undef UNIMPLEMENTED
#undef UNIMPLEMENTED_I
#undef UNIMPLEMENTED_UNCOND_J
#undef UNIMPLEMENTED_COND_J

private:
    /// These two functions increment or decrement the reference count
    /// of a PyObject*. The behavior is undefined if the Value's type
    /// isn't PyObject* or a subclass.
    void IncRef(llvm::Value *value);
    void DecRef(llvm::Value *value);
    void XDecRef(llvm::Value *value);

    /// These two push or pop a value onto or off of the stack. The
    /// behavior is undefined if the Value's type isn't PyObject* or a
    /// subclass.  These do no refcount operations, which means that
    /// Push() consumes a reference and gives ownership of it to the
    /// new value on the stack, and Pop() returns a pointer that owns
    /// a reference (which it got from the stack).
    void Push(llvm::Value *value);
    llvm::Value *Pop();

    /// Takes a target stack pointer and pops values off the stack
    /// until it gets there, decref'ing as it goes.
    void PopAndDecrefTo(llvm::Value *target_stack_pointer);

    /// Returns the difference between the current stack pointer and
    /// the base of the stack.
    llvm::Value *GetStackLevel();

    // Replaces a local variable with the PyObject* stored in
    // new_value.  Decrements the original value's refcount after
    // replacing it.
    void SetLocal(int locals_index, llvm::Value *new_value);

    // Adds handler to the switch for unwind targets and then sets up
    // a call to PyFrame_BlockSetup() with the block type, handler
    // index, and current stack level.
    void CallBlockSetup(int block_type, llvm::BasicBlock *handler);

    // Look up a name in the function's names list, returning the
    // PyStringObject for the name_index.
    llvm::Value *LookupName(int name_index);

    /// Inserts a call that will print opcode_name and abort the
    /// program when it's reached. This is useful for not-yet-defined
    /// instructions.
    void InsertAbort(const char *opcode_name);

    // Returns the global variable with type T and name 'name'. The
    // variable will be looked up in Python's C runtime.
    template<typename T>
    llvm::Constant *GetGlobalVariable(const std::string &name);
    // Returns the global function with type T and name 'name'. The
    // function will be looked up in Python's C runtime.
    template<typename T>
    llvm::Function *GetGlobalFunction(const std::string &name);

    // Returns an i1, true if value represents a NULL pointer.
    llvm::Value *IsNull(llvm::Value *value);
    // Returns an i1, true if value is a negative integer.
    llvm::Value *IsNegative(llvm::Value *value);
    // Returns an i1, true if value is a non-zero integer.
    llvm::Value *IsNonZero(llvm::Value *value);
    // Returns an i1, true if value is a positive (>0) integer.
    llvm::Value *IsPositive(llvm::Value *value);
    // Returns an i1, true if value is a PyObject considered true.
    // Steals the reference to value.
    llvm::Value *IsPythonTrue(llvm::Value *value);
    // Returns an i1, true if value is an instance of the class
    // represented by the flag argument.  flag should be something
    // like Py_TPFLAGS_INT_SUBCLASS.
    llvm::Value *IsInstanceOfFlagClass(llvm::Value *value, int flag);

    /// During stack unwinding it may be necessary to jump back into
    /// the function to handle a finally or except block.  Since LLVM
    /// doesn't allow us to directly store labels as data, we instead
    /// add the label to a switch instruction and return the i32 that
    /// will jump there.
    llvm::ConstantInt *AddUnwindTarget(llvm::BasicBlock *target);

    // Inserts a jump to the return block, returning retval.  You
    // should _never_ call CreateRet directly from one of the opcode
    // handlers, since doing so would fail to unwind the stack.
    void Return(llvm::Value *retval);

    // Propagates an exception by jumping to the unwind block with an
    // appropriate unwind reason set.
    void PropagateException();

    // Only for use in the constructor: Fills in the unwind block. Has
    // no effect on the IRBuilder's current insertion block.
    void FillUnwindBlock();

    // Create an alloca in the entry block, so that LLVM can optimize
    // it more easily, and return the resulting address. The signature
    // matches IRBuilder.CreateAlloca()'s.
    llvm::Value *CreateAllocaInEntryBlock(
        const llvm::Type *alloca_type,
        llvm::Value *array_size,
        const char *name);

    // Set exception information and jump to exception handling. The
    // arguments can be Value*'s representing NULL to implement the
    // four forms of the 'raise' statement. Steals all references.
    void DoRaise(llvm::Value *exc_type, llvm::Value *exc_inst,
                 llvm::Value *exc_tb);

    // Helper methods for binary and unary operators, passing the name
    // of the Python/C API function that implements the operation.
    // GenericBinOp's apifunc is "PyObject *(*)(PyObject *, PyObject *)"
    void GenericBinOp(const char *apifunc);
    // GenericPowOp's is "PyObject *(*)(PyObject *, PyObject *, PyObject *)"
    void GenericPowOp(const char *apifunc);
    // GenericUnaryOp's is "PyObject *(*)(PyObject *)"
    void GenericUnaryOp(const char *apifunc);

    // Call PyObject_RichCompare(lhs, rhs, cmp_op), pushing the result
    // onto the stack. cmp_op is one of Py_EQ, Py_NE, Py_LT, Py_LE, Py_GT
    // or Py_GE as defined in Python/object.h. Steals both references.
    void RichCompare(llvm::Value *lhs, llvm::Value *rhs, int cmp_op);
    // Call PySequence_Contains(seq, item), returning the result as an i1.
    // Steals both references.
    llvm::Value *ContainerContains(llvm::Value *seq, llvm::Value *item);
    // Check whether exc (a thrown exception) matches exc_type
    // (a class or tuple of classes) for the purpose of catching
    // exc in an except clause. Returns an i1. Steals both references.
    llvm::Value *ExceptionMatches(llvm::Value *exc, llvm::Value *exc_type);

    // If 'value' represents NULL, propagates the exception.
    // Otherwise, falls through.
    void PropagateExceptionOnNull(llvm::Value *value);
    // If 'value' represents a negative integer, propagates the exception.
    // Otherwise, falls through.
    void PropagateExceptionOnNegative(llvm::Value *value);
    // If 'value' represents a non-zero integer, propagates the exception.
    // Otherwise, falls through.
    void PropagateExceptionOnNonZero(llvm::Value *value);

    // Get the address of the idx'th item in a list or tuple object.
    llvm::Value *GetListItemSlot(llvm::Value *lst, int idx);
    llvm::Value *GetTupleItemSlot(llvm::Value *tup, int idx);
    // Helper method for building a new sequence from items on the stack.
    // 'size' is the number of items to build, 'createname' the Python/C API
    // function to call to create the sequence, and 'getitemslot' is called
    // to get each item's address (GetListItemSlot or GetTupleItemSlot.)
    void BuildSequenceLiteral(
        int size, const char *createname,
        llvm::Value *(LlvmFunctionBuilder::*getitemslot)(llvm::Value *, int));

    // Apply a classic slice to a sequence, pushing the result onto the
    // stack.  'start' and 'stop' can be Value*'s representing NULL to
    // indicate missing arguments, and all references are stolen.
    void ApplySlice(llvm::Value *seq, llvm::Value *start, llvm::Value *stop);
    // Assign to or delete a slice of a sequence. 'start' and 'stop' can be
    // Value*'s representing NULL to indicate missing arguments, and
    // 'source' can be a Value* representing NULL to indicate slice
    // deletion. All references are stolen.
    void AssignSlice(llvm::Value *seq, llvm::Value *start, llvm::Value *stop,
                     llvm::Value *source);

    PyGlobalLlvmData *const llvm_data_;
    llvm::Module *const module_;
    llvm::Function *const function_;
    llvm::IRBuilder<> builder_;

    // The following pointers hold values created in the function's
    // entry block. They're constant after construction.
    llvm::Value *frame_;

    llvm::Value *stack_bottom_;
    llvm::Value *stack_pointer_addr_;
    llvm::Value *varnames_;
    llvm::Value *names_;
    llvm::Value *globals_;
    llvm::Value *builtins_;
    llvm::Value *consts_;
    llvm::Value *fastlocals_;
    llvm::Value *freevars_;

    llvm::BasicBlock *unwind_block_;
    llvm::Value *unwind_target_index_addr_;
    llvm::SwitchInst *unwind_target_switch_;
    // Stores one of the UNWIND_XXX constants defined at the top of
    // ll_compile.cc
    llvm::Value *unwind_reason_addr_;
    llvm::Value *retval_addr_;
};

}  // namespace py

#endif  // PYTHON_LL_COMPILE_H
