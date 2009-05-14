#include "Python.h"
#include "llvm_compile.h"
#include "Python/llvm_fbuilder.h"
#include "_llvmfunctionobject.h"
#include "code.h"
#include "global_llvm_data.h"
#include "opcode.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/BasicBlock.h"


using llvm::BasicBlock;

static std::string
pystring_to_std_string(PyObject *str)
{
    assert(PyString_Check(str) && "code->co_name must be PyString");
    return std::string(PyString_AS_STRING(str), PyString_GET_SIZE(str));
}

class BytecodeWalker {
public:
    // Explicit create method instead of constructor so we don't have to
    // resort to C++ exceptions to propagate Python exceptions.
    static BytecodeWalker *Create(PyCodeObject *code);
    ~BytecodeWalker();

    // Calculate the current line number from the bytecode offset and the
    // code object's lnotab, and call LlvmFunctionBuilder::SetLineNumber.
    void SetLineNumber();
    // Decode two code bytes into a single integer opcode argument,
    // and returns it. Does not do bounds checking.
    int DecodeOparg();
    // Move the walker to the next opcode.
    // Return 0 for end of iteration, -1 for error, 1 otherwise.
    int NextOpcode();
    // Get the next opcode without touching cur_instr_, so that jumps do
    // not end up between the EXTENDED_ARG opcode and its companion. 
    // Returns -1 on error, 0 othwerise.
    int HandleExtendedArgument();

    // Get the right insert block for the given target, or create it if
    // necessary.  Because we'll only actually use blocks for
    // instructions later than the current insert point, it refuses to
    // insert new blocks "in the past" -- but using an existing one from
    // there is fine.  This restriction works out alright for everything
    // but FOR_ITER, which the caller specialcases (below).  If bytecode
    // ever changes in a way that requires jumps detected too late, we
    // should build a mapping from bytecode-offset to LLVM Instruction,
    // and use BasicBlock::splitBasicBlock to create the necessary new
    // blocks "in the past".
    BasicBlock *GetBlockForAbsoluteTarget(Py_ssize_t idx, const char *name);
    // A version of GetBlockForAbsoluteTarget, but for relative jumps.
    // In Python bytecode, relative jumps count from the instruction
    // *following* the jump instruction (i.e., this->next_instr_).
    BasicBlock *GetBlockForRelativeTarget(Py_ssize_t idx, const char *name);
    // Insert a new block at the start of the current instruction, and
    // select it as the current insertion point. The caller should not
    // emit any LLVM IR for the current instruction before calling this,
    // or jumps to this instruction will not be handled right.
    void InsertBlockHere(const char *name);

    py::LlvmFunctionBuilder *fbuilder;
    // Actual opcode we are currently handling, and its argument.
    int opcode;
    int oparg;

private:
    BytecodeWalker();
    PyCodeObject *code_;
    const unsigned char *codestring_;
    // Both the size of the codestring_ and the size of the blocks_ array.
    Py_ssize_t codesize_;
    BasicBlock **blocks_;
    // Index in codestring to the first byte that is part of the
    // current instruction (either the opcode or EXTENDED_ARG.)
    Py_ssize_t cur_instr_;
    // Index in codestring to the first byte of the next instruction.
    Py_ssize_t next_instr_;
    // For line-tracing, the upper bound instruction offset for
    // the current line.
    Py_ssize_t instr_upper_bound_;
};

BytecodeWalker::BytecodeWalker()
{
    this->blocks_ = NULL;
    this->fbuilder = NULL;
}

BytecodeWalker::~BytecodeWalker()
{
    if (this->blocks_)
        PyObject_Free(this->blocks_);
    if (this->fbuilder)
        delete this->fbuilder;
}

BytecodeWalker *
BytecodeWalker::Create(PyCodeObject *code)
{
    if (!PyString_Check(code->co_code)) {
        // TODO(twouters): use the buffer protocol for the codestring,
        // since it can be any valid ReadBuffer.
        PyErr_SetString(PyExc_SystemError,
                        "non-string codestring in code object");
        return NULL;
    }
    BytecodeWalker *walker = new BytecodeWalker();
    walker->code_ = code;
    walker->fbuilder = new py::LlvmFunctionBuilder(
        PyGlobalLlvmData::Get(), pystring_to_std_string(code->co_name));
    walker->codestring_ =
        (const unsigned char *)PyString_AS_STRING(code->co_code);
    walker->codesize_ = PyString_GET_SIZE(code->co_code);
    walker->blocks_ =
        (BasicBlock **)PyObject_Malloc(
            sizeof(BasicBlock *) * walker->codesize_);
    if (walker->blocks_ == NULL) {
        delete walker;
        return NULL;
    }
    memset((void *)walker->blocks_, 0,
           sizeof(BasicBlock *) * walker->codesize_);
    walker->cur_instr_ = 0;
    walker->next_instr_ = 0;
    walker->SetLineNumber();
    return walker;
}

void
BytecodeWalker::SetLineNumber()
{
    PyAddrPair bounds;
    // TODO(twouters): refactor PyCode_CheckLineNumber so we can reuse
    // its logic without having it re-scan the lnotab at each call.
    int line = PyCode_CheckLineNumber(this->code_, this->cur_instr_, &bounds);
    if (line >= 0) {
        this->fbuilder->SetLineNumber(line);
    }
    this->instr_upper_bound_ = bounds.ap_upper;
}

int
BytecodeWalker::DecodeOparg()
{
    int new_oparg = (this->codestring_[this->next_instr_] |
                     this->codestring_[this->next_instr_ + 1] << 8);
    this->next_instr_ += 2;
    return new_oparg;
}

int
BytecodeWalker::NextOpcode()
{
    if (this->next_instr_ >= this->codesize_)
        return 0;
    this->cur_instr_ = this->next_instr_++;
    this->opcode = this->codestring_[this->cur_instr_];
    if (this->opcode >= HAVE_ARGUMENT) {
        if (this->next_instr_ + 1 >= this->codesize_) {
            PyErr_SetString(PyExc_SystemError,
                            "Unexpected end of bytecode");
            return -1;
        }
        this->oparg = this->DecodeOparg();
    }
    if (this->blocks_[this->cur_instr_] != NULL)
        this->fbuilder->FallThroughTo(this->blocks_[this->cur_instr_]);
    // Must call SetLineNumber *after* selecting the new insert block
    // (above), or the line-number-setting LLVM IR might get added after
    // a block terminator in the previous block.
    if (this->cur_instr_ >= this->instr_upper_bound_)
        this->SetLineNumber();
    return 1;
}

int
BytecodeWalker::HandleExtendedArgument()
{
    if (this->blocks_[this->next_instr_] != NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "invalid jump in bytecode (inbetween opcodes)");
        return -1;
    }
    if (this->next_instr_ + 2 >= this->codesize_) {
        PyErr_SetString(PyExc_SystemError, "Unexpected end of bytecode");
        return -1;
    }
    this->opcode = this->codestring_[this->next_instr_++];
    if (this->opcode < HAVE_ARGUMENT) {
        PyErr_SetString(PyExc_SystemError,
                        "invalid opcode after EXTENDED_ARG");
        return -1;
    }
    this->oparg = this->oparg << 16 | this->DecodeOparg();
    return 0;
}

BasicBlock *
BytecodeWalker::GetBlockForAbsoluteTarget(Py_ssize_t idx, const char *name)
{
    if (idx < 0 || idx >= this->codesize_) {
        PyErr_SetString(PyExc_SystemError,
                        "invalid jump in bytecode (past bytecode)");
        return NULL;
    }
    if (this->blocks_[idx] == NULL) {
        if (idx <= this->cur_instr_) {
            PyErr_SetString(PyExc_SystemError,
                            "invalid jump in bytecode (no jump target)");
            return NULL;
        }
        this->blocks_[idx] =
            BasicBlock::Create(name, this->fbuilder->function());
    }
    return this->blocks_[idx];
}

BasicBlock *
BytecodeWalker::GetBlockForRelativeTarget(Py_ssize_t idx, const char *name)
{
    return this->GetBlockForAbsoluteTarget(this->next_instr_ + idx, name);
}

void
BytecodeWalker::InsertBlockHere(const char *name)
{
    if (this->blocks_[this->cur_instr_] != NULL) {
        // NextOpcode will already have selected the current block for us.
        assert(this->fbuilder->builder().GetInsertBlock()
               == this->blocks_[this->cur_instr_]);
        return;
    }
    this->blocks_[this->cur_instr_] =
        BasicBlock::Create(name, this->fbuilder->function());
    this->fbuilder->FallThroughTo(this->blocks_[this->cur_instr_]);
}

extern "C" PyObject *
_PyCode_To_Llvm(PyCodeObject *code)
{
    llvm::OwningPtr<BytecodeWalker> walker(BytecodeWalker::Create(code));
    int is_err;
    if (walker.get() == NULL)
        return NULL;
    while ((is_err = walker->NextOpcode()) > 0) {
        BasicBlock *target, *fallthrough;

opcode_dispatch:
        switch(walker->opcode) {

        case NOP:
            break;

        case EXTENDED_ARG:
            walker->HandleExtendedArgument();
            goto opcode_dispatch;

        case FOR_ITER:
            // Because FOR_ITER is a jump target itself (for CONTINUE_LOOP)
            // we have to make sure we start a new block right here,
            // separately from the target/fallthrough blocks.
            walker->InsertBlockHere("FOR_ITER");
            target = walker->GetBlockForRelativeTarget(
                walker->oparg, "FOR_ITER_target");
            if (target == NULL)
                return NULL;
            fallthrough = walker->GetBlockForRelativeTarget(
                0, "FOR_ITER_fallthrough");
            if (fallthrough == NULL)
                return NULL;
            walker->fbuilder->FOR_ITER(target, fallthrough);
            break;

#define OPCODE(opname)					\
    case opname:					\
        walker->fbuilder->opname();			\
        break;

        OPCODE(POP_TOP)
        OPCODE(ROT_TWO)
        OPCODE(ROT_THREE)
        OPCODE(DUP_TOP)
        OPCODE(ROT_FOUR)
        OPCODE(UNARY_POSITIVE)
        OPCODE(UNARY_NEGATIVE)
        OPCODE(UNARY_NOT)
        OPCODE(UNARY_CONVERT)
        OPCODE(UNARY_INVERT)
        OPCODE(DUP_TOP_TWO)
        OPCODE(DUP_TOP_THREE)
        OPCODE(LIST_APPEND)
        OPCODE(BINARY_POWER)
        OPCODE(BINARY_MULTIPLY)
        OPCODE(BINARY_DIVIDE)
        OPCODE(BINARY_MODULO)
        OPCODE(BINARY_ADD)
        OPCODE(BINARY_SUBTRACT)
        OPCODE(BINARY_SUBSCR)
        OPCODE(BINARY_FLOOR_DIVIDE)
        OPCODE(BINARY_TRUE_DIVIDE)
        OPCODE(INPLACE_FLOOR_DIVIDE)
        OPCODE(INPLACE_TRUE_DIVIDE)
        OPCODE(SLICE_NONE)
        OPCODE(SLICE_LEFT)
        OPCODE(SLICE_RIGHT)
        OPCODE(SLICE_BOTH)
        OPCODE(RAISE_VARARGS_ZERO)
        OPCODE(RAISE_VARARGS_ONE)
        OPCODE(RAISE_VARARGS_TWO)
        OPCODE(RAISE_VARARGS_THREE)
        OPCODE(BUILD_SLICE_TWO)
        OPCODE(BUILD_SLICE_THREE)
        OPCODE(STORE_SLICE_NONE)
        OPCODE(STORE_SLICE_LEFT)
        OPCODE(STORE_SLICE_RIGHT)
        OPCODE(STORE_SLICE_BOTH)
        OPCODE(DELETE_SLICE_NONE)
        OPCODE(DELETE_SLICE_LEFT)
        OPCODE(DELETE_SLICE_RIGHT)
        OPCODE(DELETE_SLICE_BOTH)
        OPCODE(STORE_MAP)
        OPCODE(INPLACE_ADD)
        OPCODE(INPLACE_SUBTRACT)
        OPCODE(INPLACE_MULTIPLY)
        OPCODE(INPLACE_DIVIDE)
        OPCODE(INPLACE_MODULO)
        OPCODE(STORE_SUBSCR)
        OPCODE(DELETE_SUBSCR)
        OPCODE(BINARY_LSHIFT)
        OPCODE(BINARY_RSHIFT)
        OPCODE(BINARY_AND)
        OPCODE(BINARY_XOR)
        OPCODE(BINARY_OR)
        OPCODE(INPLACE_POWER)
        OPCODE(GET_ITER)
        OPCODE(INPLACE_LSHIFT)
        OPCODE(INPLACE_RSHIFT)
        OPCODE(INPLACE_AND)
        OPCODE(INPLACE_XOR)
        OPCODE(INPLACE_OR)
        OPCODE(BREAK_LOOP)
        OPCODE(WITH_CLEANUP)
        OPCODE(RETURN_VALUE)
        OPCODE(YIELD_VALUE)
        OPCODE(POP_BLOCK)
        OPCODE(END_FINALLY)
#undef OPCODE

#define OPCODE_WITH_ARG(opname)				\
    case opname:					\
        walker->fbuilder->opname(walker->oparg);	\
        break;

        OPCODE_WITH_ARG(STORE_NAME)
        OPCODE_WITH_ARG(DELETE_NAME)
        OPCODE_WITH_ARG(UNPACK_SEQUENCE)
        OPCODE_WITH_ARG(STORE_ATTR)
        OPCODE_WITH_ARG(DELETE_ATTR)
        OPCODE_WITH_ARG(STORE_GLOBAL)
        OPCODE_WITH_ARG(DELETE_GLOBAL)
        OPCODE_WITH_ARG(LOAD_CONST)
        OPCODE_WITH_ARG(LOAD_NAME)
        OPCODE_WITH_ARG(BUILD_TUPLE)
        OPCODE_WITH_ARG(BUILD_LIST)
        OPCODE_WITH_ARG(BUILD_MAP)
        OPCODE_WITH_ARG(LOAD_ATTR)
        OPCODE_WITH_ARG(COMPARE_OP)
        OPCODE_WITH_ARG(LOAD_GLOBAL)
        OPCODE_WITH_ARG(LOAD_FAST)
        OPCODE_WITH_ARG(STORE_FAST)
        OPCODE_WITH_ARG(DELETE_FAST)
        OPCODE_WITH_ARG(CALL_FUNCTION)
        OPCODE_WITH_ARG(MAKE_CLOSURE)
        OPCODE_WITH_ARG(LOAD_CLOSURE)
        OPCODE_WITH_ARG(LOAD_DEREF)
        OPCODE_WITH_ARG(STORE_DEREF)
        OPCODE_WITH_ARG(CALL_FUNCTION_VAR)
        OPCODE_WITH_ARG(CALL_FUNCTION_KW)
        OPCODE_WITH_ARG(CALL_FUNCTION_VAR_KW)
#undef OPCODE_WITH_ARG


// LLVM BasicBlocks can only have one terminator (jump or return) and
// only at the end of the block. This means we need to create two new
// blocks for any jump: one for the target instruction, and one for
// the instruction right after the jump. In either case, if a block
// for that instruction already exists, use the existing block.
#define OPCODE_JABS(opname)						\
    case opname:							\
        target = walker->GetBlockForAbsoluteTarget(			\
            walker->oparg, #opname "_target");				\
        if (target == NULL)						\
            return NULL;						\
        fallthrough = walker->GetBlockForRelativeTarget(		\
            0, #opname "_fallthrough");					\
        if (fallthrough == NULL)					\
            return NULL;						\
        walker->fbuilder->opname(target, fallthrough);			\
        break;

        OPCODE_JABS(JUMP_IF_FALSE_OR_POP)
        OPCODE_JABS(JUMP_IF_TRUE_OR_POP)
        OPCODE_JABS(JUMP_ABSOLUTE)
        OPCODE_JABS(POP_JUMP_IF_FALSE)
        OPCODE_JABS(POP_JUMP_IF_TRUE)
        OPCODE_JABS(CONTINUE_LOOP)
#undef OPCODE_JABS

#define OPCODE_JREL(opname)						\
    case opname:							\
        target = walker->GetBlockForRelativeTarget(			\
            walker->oparg, #opname "_target");				\
        if (target == NULL)						\
            return NULL;						\
        fallthrough = walker->GetBlockForRelativeTarget(		\
            0, #opname "_fallthrough"); 				\
        if (fallthrough == NULL)					\
            return NULL;						\
        walker->fbuilder->opname(target, fallthrough);			\
        break;

        OPCODE_JREL(JUMP_FORWARD)
        OPCODE_JREL(SETUP_LOOP)
        OPCODE_JREL(SETUP_EXCEPT)
        OPCODE_JREL(SETUP_FINALLY)
#undef OPCODE_JREL

        default:
            PyErr_Format(PyExc_SystemError,
                         "Invalid opcode %d in LLVM IR generation",
                         walker->opcode);
            return NULL;
        }
    }
    if (is_err < 0)
        return NULL;
    // Make sure the last block has a terminator, even though it should
    // be unreachable.
    walker->fbuilder->FallThroughTo(walker->fbuilder->unreachable_block());
    if (llvm::verifyFunction(*walker->fbuilder->function(),
                             llvm::PrintMessageAction)) {
        PyErr_SetString(PyExc_SystemError, "invalid LLVM IR produced");
        return NULL;
    }
    return _PyLlvmFunction_FromPtr(walker->fbuilder->function());
}
