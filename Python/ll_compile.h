// -*- C++ -*-
#ifndef PYTHON_LL_COMPILE_H
#define PYTHON_LL_COMPILE_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Support/IRBuilder.h"

namespace py {

/// Helps the compiler build LLVM functions corresponding to Python
/// functions.  This class maintains the IRBuilder and several Value*s
/// set up in the entry block.
class LlvmFunctionBuilder {
public:
    LlvmFunctionBuilder(llvm::Module *module, const std::string& name);

    llvm::Function *function() { return function_; }
    llvm::IRBuilder<>& builder() { return builder_; }

    /// Sets the insert point to next_block, inserting an
    /// unconditional branch to there if the current block doesn't yet
    /// have a terminator instruction.
    void FallThroughTo(llvm::BasicBlock *next_block);

    /// The following methods operate like the opcodes with the same
    /// name.
    void LOAD_CONST(int index);
    void LOAD_ATTR(int index) {
        InsertAbort();
    }
    void LOAD_GLOBAL(int index) {
        InsertAbort();
    }
    void LOAD_FAST(int index);
    void STORE_FAST(int index) {
        InsertAbort();
    }
    void DELETE_FAST(int index) {
        InsertAbort();
    }

    void RETURN_VALUE();

private:
    /// These two functions increment or decrement the reference count
    /// of a PyObject*. The behavior is undefined if the Value's type
    /// isn't PyObject* or a subclass.
    void IncRef(llvm::Value *value);
    void DecRef(llvm::Value *value);
    /// These two push or pop a value onto or off of the stack. The
    /// behavior is undefined if the Value's type isn't PyObject* or a
    /// subclass.
    void Push(llvm::Value *value);
    llvm::Value *Pop();

    /// Inserts a call that will abort the program when it's
    /// reached. This is useful for not-yet-defined instructions.
    void InsertAbort();

    llvm::Module *const module_;
    llvm::Function *const function_;
    llvm::IRBuilder<> builder_;

    // The following pointers hold values created in the function's
    // entry block. They're constant after construction.
    llvm::Value *frame_;

    llvm::Value *stack_pointer_addr_;
    llvm::Value *varnames_;
    llvm::Value *names_;
    llvm::Value *consts_;
    llvm::Value *fastlocals_;
    llvm::Value *freevars_;
};

}  // namespace py

#endif  // PYTHON_LL_COMPILE_H
