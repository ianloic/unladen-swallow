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

class BytecodeIterator {
public:
    // Initializes the iterator to point to the first opcode in the
    // bytecode string.
    BytecodeIterator(PyObject *bytecode_string);
    // Allow the default copy operations.

    int Opcode() const { return this->opcode_; }
    int Oparg() const { return this->oparg_; }
    size_t CurIndex() const { return this->cur_index_; }
    size_t NextIndex() const { return this->next_index_; }
    bool Done() const { return this->cur_index_ == this->bytecode_size_; }
    bool Error() const { return this->error_; }

    // Advances the iterator by one opcode, including the effect of
    // any EXTENDED_ARG opcode in the way.  If there is an
    // EXTENDED_ARG, this->CurIndex() will point to it rather than the
    // actual opcode, since that's where jumps land.  If the bytecode
    // is malformed, this will set a Python error and cause
    // this->Error() to return true.
    void Advance();

private:
    int opcode_;
    int oparg_;
    size_t cur_index_;
    size_t next_index_;
    bool error_;
    const unsigned char *const bytecode_str_;
    const size_t bytecode_size_;
};

BytecodeIterator::BytecodeIterator(PyObject *bytecode_string)
    : error_(false),
      bytecode_str_((unsigned char *)PyString_AS_STRING(bytecode_string)),
      bytecode_size_(PyString_GET_SIZE(bytecode_string))
{
    assert(PyString_Check(bytecode_string) &&
           "Argument to BytecodeIterator() must be a Python string.");
    this->next_index_ = 0;
    // Take advantage of the implementation of Advance() to fill in
    // the other fields.
    this->Advance();
}

void
BytecodeIterator::Advance()
{
    this->cur_index_ = this->next_index_;
    if (this->Done()) {
        return;
    }
    this->opcode_ = this->bytecode_str_[this->cur_index_];
    this->next_index_++;
    if (HAS_ARG(this->opcode_)) {
        if (this->next_index_ + 1 >= this->bytecode_size_) {
            PyErr_SetString(PyExc_SystemError,
                            "Argument fell off the end of the bytecode");
            this->error_ = true;
            return;
        }
        this->oparg_ = (this->bytecode_str_[this->next_index_] |
                        this->bytecode_str_[this->next_index_ + 1] << 8);
        this->next_index_ += 2;
        if (this->opcode_ == EXTENDED_ARG) {
            if (this->next_index_ + 2 >= this->bytecode_size_) {
                PyErr_SetString(
                    PyExc_SystemError,
                    "EXTENDED_ARG fell off the end of the bytecode");
                this->error_ = true;
                return;
            }
            this->opcode_ = this->bytecode_str_[this->next_index_];
            if (!HAS_ARG(this->opcode_)) {
                PyErr_SetString(PyExc_SystemError,
                                "Opcode after EXTENDED_ARG must take argument");
                this->error_ = true;
                return;
            }
            this->oparg_ <<= 16;
            this->oparg_ |= (this->bytecode_str_[this->next_index_ + 1] |
                             this->bytecode_str_[this->next_index_ + 2] << 8);
            this->next_index_ += 3;
        }
    }
}

struct InstrInfo {
    InstrInfo() : line_number_(0), block_(NULL), backedge_block_(NULL) {}
    // The line this instruction falls on.
    int line_number_;
    // If this instruction starts a new basic block, this is the
    // LLVM block it starts.
    BasicBlock *block_;
    // If this instruction is the target of a backedge in the
    // control flow graph, this block implements the necessary
    // line tracing and then branches to the main block.
    BasicBlock *backedge_block_;
};

// Uses *code to fill line numbers into instr_info.  Assumes that
// instr_info[*].line_number was initialized to 0.  Returns -1 on
// error, or 0 on success.
static int
set_line_numbers(PyCodeObject *code, std::vector<InstrInfo>& instr_info)
{
    assert(PyString_Check(code->co_code));
    assert(instr_info.size() == (size_t)PyString_GET_SIZE(code->co_code) &&
           "instr_info indices must match bytecode indices.");
    // First, assign each address's "line number" to the change in the
    // line number that applies at that address.
    size_t addr = 0;
    const unsigned char *const lnotab_str =
        (unsigned char *)PyString_AS_STRING(code->co_lnotab);
    const int lnotab_size = PyString_GET_SIZE(code->co_lnotab);
    for (int i = 0; i + 1 < lnotab_size; i += 2) {
        addr += lnotab_str[i];
        if (addr >= instr_info.size()) {
            PyErr_Format(PyExc_SystemError,
                         "lnotab referred to addr %zu, which is outside of"
                         " bytecode string of length %zu.",
                         addr, instr_info.size());
            return -1;
        }
        // Use += instead of = to handle line number jumps of more than 255.
        instr_info[addr].line_number_ += lnotab_str[i + 1];
    }

    // Second, add up the line number deltas and store the total line
    // number back into instr_info.
    int line = code->co_firstlineno;
    for (size_t i = 0; i < instr_info.size(); ++i) {
        line += instr_info[i].line_number_;
        instr_info[i].line_number_ = line;
    }
    return 0;
}

// Uses the jump instructions in the bytecode string to identify basic
// blocks and backedges, and creates new llvm::BasicBlocks inside
// *function accordingly into instr_info.  Returns -1 on error, or 0
// on success.
static int
find_basic_blocks(PyObject *bytecode, py::LlvmFunctionBuilder &fbuilder,
                  std::vector<InstrInfo>& instr_info)
{
    assert(PyString_Check(bytecode) && "Expected bytecode string");
    assert(instr_info.size() == (size_t)PyString_GET_SIZE(bytecode) &&
           "instr_info indices must match bytecode indices.");
    BytecodeIterator iter(bytecode);
    for (; !iter.Done() && !iter.Error(); iter.Advance()) {
        size_t target_index;
        const char *target_name;
        const char *fallthrough_name;
        const char *backedge_name;
        switch (iter.Opcode()) {
#define OPCODE_JABS(opname) \
            case opname: \
                target_index = iter.Oparg(); \
                target_name = #opname "_target"; \
                fallthrough_name = #opname "_fallthrough"; \
                backedge_name = #opname "_backedge"; \
                break;

        OPCODE_JABS(JUMP_IF_FALSE_OR_POP)
        OPCODE_JABS(JUMP_IF_TRUE_OR_POP)
        OPCODE_JABS(JUMP_ABSOLUTE)
        OPCODE_JABS(POP_JUMP_IF_FALSE)
        OPCODE_JABS(POP_JUMP_IF_TRUE)
        OPCODE_JABS(CONTINUE_LOOP)
#undef OPCODE_JABS

#define OPCODE_JREL(opname) \
            case opname: \
                target_index = iter.NextIndex() + iter.Oparg(); \
                target_name = #opname "_target"; \
                fallthrough_name = #opname "_fallthrough"; \
                backedge_name = #opname "_backedge"; \
                break;

        OPCODE_JREL(FOR_ITER)
        OPCODE_JREL(JUMP_FORWARD)
        OPCODE_JREL(SETUP_LOOP)
        OPCODE_JREL(SETUP_EXCEPT)
        OPCODE_JREL(SETUP_FINALLY)
#undef OPCODE_JREL

        // Disable an optimization to LOAD_FAST if DELETE_FAST is ever used.
        // This isn't a jump, and isn't necessary for basic block creation, but
        // doing this check here saves us having to iterate over the opcodes
        // again.
        case DELETE_FAST:
            fbuilder.uses_delete_fast = true;
            continue;

        default:
            // This isn't a jump, so we don't need any new blocks for it.
            continue;
        }

        // LLVM BasicBlocks can only have one terminator (jump or return) and
        // only at the end of the block. This means we need to create two new
        // blocks for any jump: one for the target instruction, and one for
        // the instruction right after the jump. In either case, if a block
        // for that instruction already exists, use the existing block.
        if (iter.NextIndex() >= instr_info.size()) {
            PyErr_SetString(PyExc_SystemError, "Fell through out of bytecode.");
            return -1;
        }
        if (instr_info[iter.NextIndex()].block_ == NULL) {
            instr_info[iter.NextIndex()].block_ =
                fbuilder.CreateBasicBlock(fallthrough_name);
        }
        if (target_index >= instr_info.size()) {
            PyErr_Format(PyExc_SystemError,
                         "Jumped to index %zu, which is outside of the"
                         " bytecode string of length %zu.",
                         target_index, instr_info.size());
            return -1;
        }
        if (instr_info[target_index].block_ == NULL) {
            instr_info[target_index].block_ =
                fbuilder.CreateBasicBlock(target_name);
        }
        if (target_index < iter.NextIndex() &&  // This is a backedge.
            instr_info[target_index].backedge_block_ == NULL) {
            instr_info[target_index].backedge_block_ =
                fbuilder.CreateBasicBlock(backedge_name);
        }
    }
    if (iter.Error()) {
        return -1;
    }
    return 0;
}

extern "C" _LlvmFunction *
_PyCode_ToLlvmIr(PyCodeObject *code)
{
    if (!PyCode_Check(code)) {
        PyErr_Format(PyExc_TypeError, "Expected code object, not '%.500s'",
                     code->ob_type->tp_name);
        return NULL;
    }
    if (!PyString_Check(code->co_code)) {
        PyErr_SetString(PyExc_SystemError,
                        "non-string codestring in code object");
        return NULL;
    }

    py::LlvmFunctionBuilder fbuilder(PyGlobalLlvmData::Get(), code);
    std::vector<InstrInfo> instr_info(PyString_GET_SIZE(code->co_code));
    if (-1 == set_line_numbers(code, instr_info)) {
        return NULL;
    }
    if (-1 == find_basic_blocks(code->co_code, fbuilder, instr_info)) {
        return NULL;
    }

    BytecodeIterator iter(code->co_code);
    for (; !iter.Done() && !iter.Error(); iter.Advance()) {
        fbuilder.SetLasti(iter.CurIndex());
        if (instr_info[iter.CurIndex()].block_ != NULL) {
            fbuilder.FallThroughTo(instr_info[iter.CurIndex()].block_);
        }
        // Must call SetLineNumber *after* selecting the new insert block
        // (above), or the line-number-setting LLVM IR might get added after
        // a block terminator in the previous block.
        if (iter.CurIndex() == 0 ||
            instr_info[iter.CurIndex()].line_number_ !=
            instr_info[iter.CurIndex() - 1].line_number_) {
            fbuilder.SetLineNumber(instr_info[iter.CurIndex()].line_number_);
        }

        BasicBlock *target, *fallthrough;
        int target_opindex;
        switch(iter.Opcode()) {
        case NOP:
            break;

#define OPCODE(opname)					\
    case opname:					\
        fbuilder.opname();				\
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
        fbuilder.opname(iter.Oparg());			\
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

#define ABS iter.Oparg()
#define REL iter.NextIndex() + iter.Oparg()
#define NO_OPINDEX target
#define NEED_OPINDEX target, target_opindex
#define OPCODE_J(opname, OPINDEX_EXPR, TARGET_PARAM) \
    case opname: \
        target_opindex = OPINDEX_EXPR; \
        if ((size_t)target_opindex < iter.NextIndex()) { \
            target = instr_info[target_opindex].backedge_block_; \
        } else { \
            target = instr_info[target_opindex].block_; \
        } \
        fallthrough = instr_info[iter.NextIndex()].block_; \
        assert(target != NULL && "Missing target block"); \
        assert(fallthrough != NULL && "Missing fallthrough block"); \
        fbuilder.opname(TARGET_PARAM, fallthrough); \
        break;

        OPCODE_J(JUMP_IF_FALSE_OR_POP, ABS, NO_OPINDEX)
        OPCODE_J(JUMP_IF_TRUE_OR_POP, ABS, NO_OPINDEX)
        OPCODE_J(JUMP_ABSOLUTE, ABS, NO_OPINDEX)
        OPCODE_J(POP_JUMP_IF_FALSE, ABS, NO_OPINDEX)
        OPCODE_J(POP_JUMP_IF_TRUE, ABS, NO_OPINDEX)
        OPCODE_J(CONTINUE_LOOP, ABS, NEED_OPINDEX)
        OPCODE_J(JUMP_FORWARD, REL, NO_OPINDEX)
        OPCODE_J(FOR_ITER, REL, NO_OPINDEX)
        OPCODE_J(SETUP_LOOP, REL, NEED_OPINDEX)
        OPCODE_J(SETUP_EXCEPT, REL, NEED_OPINDEX)
        OPCODE_J(SETUP_FINALLY, REL, NEED_OPINDEX)
#undef OPCODE_J
#undef ABS
#undef REL
#undef NO_OPINDEX
#undef NEED_OPINDEX

        case EXTENDED_ARG:
            // Already handled by the iterator.
        default:
            PyErr_Format(PyExc_SystemError,
                         "Invalid opcode %d in LLVM IR generation",
                         iter.Opcode());
            return NULL;
        }
    }
    if (iter.Error()) {
        return NULL;
    }
    // Make sure the last block has a terminator, even though it should
    // be unreachable.
    fbuilder.FallThroughTo(fbuilder.unreachable_block());

    for (size_t i = 0; i < instr_info.size(); ++i) {
        const InstrInfo &info = instr_info[i];
        if (info.backedge_block_ != NULL) {
            assert(info.block_ != NULL &&
                   "We expect that any backedge is to the beginning"
                   " of a basic block.");
            fbuilder.SetLasti(i);
            bool backedge_is_to_start_of_line =
                i == 0 || instr_info[i - 1].line_number_ != info.line_number_;
            fbuilder.FillBackedgeLanding(info.backedge_block_, info.block_,
                                         backedge_is_to_start_of_line,
                                         info.line_number_);
        }
    }

    if (llvm::verifyFunction(*fbuilder.function(), llvm::PrintMessageAction)) {
        PyErr_SetString(PyExc_SystemError, "invalid LLVM IR produced");
        return NULL;
    }

    /* If the code object doesn't need the LOAD_GLOBAL optimization, it should
       not care whether the globals/builtins change. */
    if (!fbuilder.UsesLoadGlobalOpt() && code->co_assumed_globals) {
        code->co_flags &= ~CO_FDO_GLOBALS;
        _PyDict_DropWatcher(code->co_assumed_globals, code);
        _PyDict_DropWatcher(code->co_assumed_builtins, code);
        code->co_assumed_globals = NULL;
        code->co_assumed_builtins = NULL;
    }

    // Make sure the function survives global optimizations.
    fbuilder.function()->setLinkage(llvm::GlobalValue::ExternalLinkage);

    _LlvmFunction *wrapper = new _LlvmFunction();
    wrapper->lf_function = fbuilder.function();
    return wrapper;
}
