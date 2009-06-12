// -*- C++ -*-
#ifndef PYTHON_LLVM_FBUILDER_H
#define PYTHON_LLVM_FBUILDER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Support/IRBuilder.h"
#include <string>

struct PyCodeObject;
struct PyGlobalLlvmData;

namespace py {

/// Helps the compiler build LLVM functions corresponding to Python
/// functions.  This class maintains the IRBuilder and several Value*s
/// set up in the entry block.
class LlvmFunctionBuilder {
    LlvmFunctionBuilder(const LlvmFunctionBuilder &);  // Not implemented.
    void operator=(const LlvmFunctionBuilder &);  // Not implemented.

public:
    LlvmFunctionBuilder(PyGlobalLlvmData *global_data, PyCodeObject *code);

    llvm::Function *function() { return function_; }
    llvm::IRBuilder<>& builder() { return builder_; }
    llvm::BasicBlock *unreachable_block() { return unreachable_block_; }

    /// Sets the current instruction index.  This is only put into the
    /// frame object when tracing.
    void SetLasti(int current_instruction_index);

    /// Sets the current line number being executed.  This is used to
    /// make tracebacks correct and to get tracing to fire in the
    /// right places.
    void SetLineNumber(int line);

    /// Emits code into backedge_landing to call the line tracing
    /// function and then branch to target.  We need to re-set the
    /// line number in this function because the target may be in the
    /// middle of a different line than the source.  This function
    /// leaves the insert point in backedge_landing which is probably
    /// not where the caller wants it.
    void TraceBackedgeLanding(llvm::BasicBlock *backedge_landing,
                              llvm::BasicBlock *target,
                              int line_number);

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
    void WITH_CLEANUP();

    void JUMP_FORWARD(llvm::BasicBlock *target, llvm::BasicBlock *fallthrough) {
        this->JUMP_ABSOLUTE(target, fallthrough);
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
    void CONTINUE_LOOP(llvm::BasicBlock *target,
                       llvm::BasicBlock *fallthrough);

    void BREAK_LOOP();
    void RETURN_VALUE();
    void YIELD_VALUE();

    void POP_TOP();
    void DUP_TOP();
    void DUP_TOP_TWO();
    void DUP_TOP_THREE();
    void ROT_TWO();
    void ROT_THREE();
    void ROT_FOUR();

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
    void CALL_FUNCTION_VAR(int num_args);
    void CALL_FUNCTION_KW(int num_args);
    void CALL_FUNCTION_VAR_KW(int num_args);

    void BUILD_TUPLE(int size);
    void BUILD_LIST(int size);
    void BUILD_MAP(int size);
    void BUILD_SLICE_TWO();
    void BUILD_SLICE_THREE();
    void UNPACK_SEQUENCE(int size);

    void LOAD_GLOBAL(int index);
    void STORE_GLOBAL(int index);
    void DELETE_GLOBAL(int index);

    void LOAD_NAME(int index);
    void STORE_NAME(int index);
    void DELETE_NAME(int index);

    void LOAD_ATTR(int index);
    void STORE_ATTR(int index);
    void DELETE_ATTR(int index);

    void LOAD_CLOSURE(int freevar_index);
    void MAKE_CLOSURE(int num_defaults);
    void LOAD_DEREF(int index);
    void STORE_DEREF(int index);

    void RAISE_VARARGS_ZERO();
    void RAISE_VARARGS_ONE();
    void RAISE_VARARGS_TWO();
    void RAISE_VARARGS_THREE();

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
    /// program when it's reached.
    void DieForUndefinedOpcode(const char *opcode_name);

    /// Implements something like the C assert statement.  If
    /// should_be_true (an i1) is false, prints failure_message (with
    /// puts) and aborts.  Compiles to nothing in optimized mode.
    void Assert(llvm::Value *should_be_true,
                const std::string &failure_message);

    /// Prints failure_message (with puts) and aborts.
    void Abort(const std::string &failure_message);

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

    // Only for use in the constructor: Fills in the block that
    // handles bailing out of JITted code back to the interpreter
    // loop.  Code jumping to this block must first:
    //  1) Set frame->f_lasti to the current opcode index.
    //  2) Set frame->f_bailed_from_llvm to a reason.
    void FillBailToInterpreterBlock();
    // Only for use in the constructor: Fills in the block that starts
    // propagating an exception.  Jump to this block when you want to
    // add a traceback entry for the current line.  Don't jump to this
    // block (and just set retval_addr_ and unwind_reason_addr_
    // directly) when you're re-raising an exception and you want to
    // use its traceback.
    void FillPropagateExceptionBlock();
    // Only for use in the constructor: Fills in the unwind block.
    void FillUnwindBlock();
    // Only for use in the constructor: Fills in the block that
    // actually handles returning from the function.
    void FillDoReturnBlock();

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

    // Helper method for CALL_FUNCTION_(VAR|KW|VAR_KW).  If TSC is enabled,
    // this method builds LLVM code to call a C function that logs and times
    // the CALL_FUNCTION* event.
    void LogCallStart();

    // Helper method for CALL_FUNCTION_(VAR|KW|VAR_KW); calls
    // _PyEval_CallFunctionVarKw() with the given flags and the current
    // stack pointer.
    void CallVarKwFunction(int num_args, int call_flag);

    /// Emits code to conditionally bail out to the interpreter loop
    /// if a line tracing function is installed.  If the line tracing
    /// function is not installed, execution will continue at
    /// fallthrough_block.  direction should be _PYFRAME_LINE_TRACE or
    /// _PYFRAME_BACKEDGE_TRACE.
    void MaybeCallLineTrace(llvm::BasicBlock *fallthrough_block,
                            char direction);

    PyGlobalLlvmData *const llvm_data_;
    // The code object is used for looking up peripheral information
    // about the function.  It's not used to examine the bytecode
    // string.
    PyCodeObject *const code_object_;
    llvm::Module *const module_;
    llvm::Function *const function_;
    llvm::IRBuilder<> builder_;
    const bool is_generator_;

    // The most recent index we've started emitting an instruction for.
    int f_lasti_;

    // The following pointers hold values created in the function's
    // entry block. They're constant after construction.
    llvm::Value *frame_;

    llvm::Value *tstate_;
    llvm::Value *stack_bottom_;
    llvm::Value *stack_pointer_addr_;
    // The tmp_stack_pointer is used when we need to have another
    // function update the stack pointer.  Passing the stack pointer
    // directly would prevent mem2reg from working on it, so we copy
    // it to and from the tmp_stack_pointer around the call.
    llvm::Value *tmp_stack_pointer_addr_;
    llvm::Value *varnames_;
    llvm::Value *names_;
    llvm::Value *globals_;
    llvm::Value *builtins_;
    llvm::Value *consts_;
    llvm::Value *fastlocals_;
    llvm::Value *freevars_;
    llvm::Value *f_lineno_addr_;
    llvm::Value *f_lasti_addr_;

    llvm::BasicBlock *unreachable_block_;

    // In generators, we use this switch to jump back to the most
    // recently executed yield instruction.
    llvm::SwitchInst *yield_resume_switch_;

    llvm::BasicBlock *bail_to_interpreter_block_;

    llvm::BasicBlock *propagate_exception_block_;
    llvm::BasicBlock *unwind_block_;
    llvm::Value *unwind_target_index_addr_;
    llvm::SwitchInst *unwind_target_switch_;
    // Stores one of the UNWIND_XXX constants defined at the top of
    // llvm_fbuilder.cc
    llvm::Value *unwind_reason_addr_;
    llvm::BasicBlock *do_return_block_;
    llvm::Value *retval_addr_;
};

}  // namespace py

#endif  // PYTHON_LLVM_FBUILDER_H
