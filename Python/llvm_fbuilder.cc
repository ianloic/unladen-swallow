#include "Python/llvm_fbuilder.h"

#include "Python.h"
#include "code.h"
#include "opcode.h"
#include "frameobject.h"

#include "Python/global_llvm_data.h"

#include "Util/EventTimer.h"
#include "Util/PyTypeBuilder.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Function.h"
#include "llvm/GlobalAlias.h"
#include "llvm/Instructions.h"
#include "llvm/Intrinsics.h"
#include "llvm/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Type.h"

#include <vector>

#ifndef DW_LANG_Python
// Python has an official ID number in the draft Dwarf4 spec.
#define DW_LANG_Python 0x0014
#endif

struct PyExcInfo;

namespace Intrinsic = llvm::Intrinsic;
using llvm::BasicBlock;
using llvm::CallInst;
using llvm::Constant;
using llvm::ConstantExpr;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::GlobalVariable;
using llvm::Module;
using llvm::Type;
using llvm::Value;
using llvm::array_endof;
using llvm::errs;

// Use like "this->GET_GLOBAL_VARIABLE(Type, variable)".
#define GET_GLOBAL_VARIABLE(TYPE, VARIABLE) \
    GetGlobalVariable<TYPE>(&VARIABLE, #VARIABLE)

#ifdef Py_WITH_INSTRUMENTATION
class CallFunctionStats {
public:
    ~CallFunctionStats() {
        errs() << "\nCALL_FUNCTION optimization:\n";
        errs() << "Total opcodes: " << this->total << "\n";
        errs() << "Optimized opcodes: " << this->optimized << "\n";
        errs() << "No opt: callsite kwargs: " << this->no_opt_kwargs << "\n";
        errs() << "No opt: function params: " << this->no_opt_params << "\n";
        errs() << "No opt: no data: " << this->no_opt_no_data << "\n";
        errs() << "No opt: polymorphic: " << this->no_opt_polymorphic << "\n";
    }

    // How many CALL_FUNCTION opcodes were compiled.
    unsigned total;
    // How many CALL_FUNCTION opcodes were successfully optimized;
    unsigned optimized;
    // We only optimize call sites without keyword, *args or **kwargs arguments.
    unsigned no_opt_kwargs;
    // We only optimize METH_O and METH_NOARGS functions so far.
    unsigned no_opt_params;
    // We only optimize callsites where we've collected data. Note that since
    // we record only PyCFunctions, any call to a Python function will show up
    // as having no data.
    unsigned no_opt_no_data;
    // We only optimize monomorphic callsites so far.
    unsigned no_opt_polymorphic;
};

static llvm::ManagedStatic<CallFunctionStats> call_function_stats;

class CondBranchStats {
public:
    ~CondBranchStats() {
        errs() << "\nConditional branch optimization:\n";
        errs() << "Total cond branches: " << this->total << "\n";
        errs() << "Optimized branches: " << this->optimized << "\n";
        errs() << "Insufficient data: " << this->not_enough_data << "\n";
        errs() << "Unpredictable branches: " << this->unpredictable << "\n";
    }

    // Total number of conditional branch opcodes compiled.
    unsigned total;
    // Number of predictable conditional branches we were able to optimize.
    unsigned optimized;
    // Number of single-direction branches we don't feel comfortable predicting.
    unsigned not_enough_data;
    // Number of unpredictable conditional branches (both directions
    // taken frequently; unable to be optimized).
    unsigned unpredictable;
};

static llvm::ManagedStatic<CondBranchStats> cond_branch_stats;

#define CF_INC_STATS(field) call_function_stats->field++
#define COND_BRANCH_INC_STATS(field) cond_branch_stats->field++
#else
#define CF_INC_STATS(field)
#define COND_BRANCH_INC_STATS(field)
#endif  /* Py_WITH_INSTRUMENTATION */

namespace py {

static std::string
pystring_to_std_string(PyObject *str)
{
    assert(PyString_Check(str));
    return std::string(PyString_AS_STRING(str), PyString_GET_SIZE(str));
}

static llvm::StringRef
pystring_to_stringref(const PyObject* str)
{
    assert(PyString_CheckExact(str));
    return llvm::StringRef(PyString_AS_STRING(str), PyString_GET_SIZE(str));
}

static const FunctionType *
get_function_type(Module *module)
{
    std::string function_type_name("__function_type");
    const FunctionType *result =
        llvm::cast_or_null<FunctionType>(
            module->getTypeByName(function_type_name));
    if (result != NULL)
        return result;

    result = PyTypeBuilder<PyObject*(PyFrameObject*)>::get(
        module->getContext());
    module->addTypeName(function_type_name, result);
    return result;
}

LlvmFunctionBuilder::LlvmFunctionBuilder(
    PyGlobalLlvmData *llvm_data, PyCodeObject *code_object)
    : uses_delete_fast(false),
      llvm_data_(llvm_data),
      code_object_(code_object),
      context_(this->llvm_data_->context()),
      module_(this->llvm_data_->module()),
      function_(Function::Create(
                    get_function_type(this->module_),
                    llvm::GlobalValue::ExternalLinkage,
                    // Prefix names with #u# to avoid collisions
                    // with runtime functions.
                    "#u#" + pystring_to_std_string(code_object->co_name),
                    this->module_)),
      builder_(this->context_),
      is_generator_(code_object->co_flags & CO_GENERATOR),
      debug_info_(llvm_data->DebugInfo()),
      debug_compile_unit_(this->debug_info_ == NULL ? llvm::DICompileUnit() :
                          this->debug_info_->CreateCompileUnit(
                              DW_LANG_Python,
                              pystring_to_std_string(code_object->co_filename),
                              "",  // Directory
                              "Unladen Swallow 2.6.1",
                              false, // Not main.
                              false, // Not optimized
                              "")),
      debug_subprogram_(this->debug_info_ == NULL ? llvm::DISubprogram() :
                        this->debug_info_->CreateSubprogram(
                            debug_compile_unit_,
                            function_->getName(),
                            function_->getName(),
                            function_->getName(),
                            debug_compile_unit_,
                            code_object->co_firstlineno,
                            llvm::DIType(),
                            false,  // Not local to unit.
                            true))  // Is definition.
{
    Function::arg_iterator args = this->function_->arg_begin();
    this->frame_ = args++;
    assert(args == this->function_->arg_end() &&
           "Unexpected number of arguments");
    this->frame_->setName("frame");

    this->uses_load_global_opt_ = false;

    BasicBlock *entry = this->CreateBasicBlock("entry");
    this->unreachable_block_ =
        this->CreateBasicBlock("unreachable");
    this->bail_to_interpreter_block_ =
        this->CreateBasicBlock("bail_to_interpreter");
    this->propagate_exception_block_ =
        this->CreateBasicBlock("propagate_exception");
    this->unwind_block_ = this->CreateBasicBlock("unwind_block");
    this->do_return_block_ = this->CreateBasicBlock("do_return");

    this->builder_.SetInsertPoint(entry);
    // CreateAllocaInEntryBlock will insert alloca's here, before
    // any other instructions in the 'entry' block.

    this->stack_pointer_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject**>::get(this->context_),
        NULL, "stack_pointer_addr");
    this->tmp_stack_pointer_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject**>::get(this->context_),
        NULL, "tmp_stack_pointer_addr");
    this->retval_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject*>::get(this->context_),
        NULL, "retval_addr");
    this->unwind_reason_addr_ = this->builder_.CreateAlloca(
        Type::getInt8Ty(this->context_), NULL, "unwind_reason_addr");
    this->unwind_target_index_addr_ = this->builder_.CreateAlloca(
        Type::getInt32Ty(this->context_), NULL, "unwind_target_index_addr");
    this->blockstack_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyTryBlock>::get(this->context_),
        ConstantInt::get(Type::getInt32Ty(this->context_), CO_MAXBLOCKS),
        "blockstack_addr");
    this->num_blocks_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<char>::get(this->context_), NULL, "num_blocks_addr");
    for (int i = 0; i < code_object->co_nlocals; ++i) {
        PyObject *local_name = PyTuple_GET_ITEM(code_object->co_varnames, i);
        this->locals_.push_back(
            this->builder_.CreateAlloca(
                PyTypeBuilder<PyObject*>::get(this->context_),
                NULL,
                "local_" + pystring_to_stringref(local_name)));
    }

    if (this->debug_info_ != NULL)
        this->debug_info_->InsertSubprogramStart(
            debug_subprogram_, this->builder_.GetInsertBlock());

    this->tstate_ = this->CreateCall(
        this->GetGlobalFunction<PyThreadState*()>(
            "_PyLlvm_WrapPyThreadState_GET"));
    this->stack_bottom_ = this->builder_.CreateLoad(
        FrameTy::f_valuestack(this->builder_, this->frame_),
        "stack_bottom");
    if (this->is_generator_) {
        // When we're re-entering a generator, we have to copy the stack
        // pointer, block stack and locals from the frame.
        this->CopyFromFrameObject();
    } else {
        // If this isn't a generator, the stack pointer always starts at
        // the bottom of the stack.
        this->builder_.CreateStore(this->stack_bottom_,
                                   this->stack_pointer_addr_);
        /* f_stacktop remains NULL unless yield suspends the frame. */
        this->builder_.CreateStore(
            Constant::getNullValue(
                    PyTypeBuilder<PyObject **>::get(this->context_)),
            FrameTy::f_stacktop(this->builder_, this->frame_));

        this->builder_.CreateStore(
            ConstantInt::get(PyTypeBuilder<char>::get(this->context_), 0),
            this->num_blocks_addr_);

        // If this isn't a generator, we only need to copy the locals.
        this->CopyLocalsFromFrameObject();
    }

    Value *use_tracing = this->builder_.CreateLoad(
        ThreadStateTy::use_tracing(this->builder_, this->tstate_),
        "use_tracing");
    BasicBlock *trace_enter_function =
        this->CreateBasicBlock("trace_enter_function");
    BasicBlock *continue_entry =
        this->CreateBasicBlock("continue_entry");
    this->builder_.CreateCondBr(this->IsNonZero(use_tracing),
                                trace_enter_function, continue_entry);

    this->builder_.SetInsertPoint(trace_enter_function);
    // Don't touch f_lasti since we just entered the function..
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<char>::get(this->context_),
                         _PYFRAME_TRACE_ON_ENTRY),
        FrameTy::f_bailed_from_llvm(this->builder_, this->frame_));
    this->builder_.CreateBr(this->bail_to_interpreter_block_);

    this->builder_.SetInsertPoint(continue_entry);
    Value *frame_code = this->builder_.CreateLoad(
        FrameTy::f_code(this->builder_, this->frame_),
        "frame->f_code");
    this->use_llvm_addr_ = CodeTy::co_use_llvm(this->builder_, frame_code);
#ifndef NDEBUG
    // Assert that the code object we pull out of the frame is the
    // same as the one passed into this object.
    Value *passed_in_code_object =
        ConstantInt::get(Type::getInt64Ty(this->context_),
                         reinterpret_cast<uintptr_t>(this->code_object_));
    this->Assert(this->builder_.CreateICmpEQ(
        this->builder_.CreatePtrToInt(frame_code,
                                      Type::getInt64Ty(this->context_)),
                     passed_in_code_object),
                 "Called with unexpected code object.");
#endif  // NDEBUG
    this->varnames_ = this->GetGlobalVariableFor(
        this->code_object_->co_varnames);

    Value *names_tuple = this->builder_.CreateBitCast(
        this->GetGlobalVariableFor(this->code_object_->co_names),
        PyTypeBuilder<PyTupleObject*>::get(this->context_),
        "names");
    // Get the address of the names_tuple's first item as well.
    this->names_ = this->GetTupleItemSlot(names_tuple, 0);

    // The next GEP-magic assigns &frame_[0].f_localsplus[0] to
    // this->fastlocals_.
    Value *localsplus = FrameTy::f_localsplus(this->builder_, this->frame_);
    this->fastlocals_ = this->builder_.CreateStructGEP(
        localsplus, 0, "fastlocals");
    Value *nlocals = ConstantInt::get(PyTypeBuilder<int>::get(this->context_),
                                      this->code_object_->co_nlocals);
    this->freevars_ =
        this->builder_.CreateGEP(this->fastlocals_, nlocals, "freevars");
    this->globals_ =
        this->builder_.CreateBitCast(
            this->builder_.CreateLoad(
                FrameTy::f_globals(this->builder_, this->frame_)),
            PyTypeBuilder<PyObject *>::get(this->context_));
    this->builtins_ =
        this->builder_.CreateBitCast(
            this->builder_.CreateLoad(
                FrameTy::f_builtins(this->builder_,this->frame_)),
            PyTypeBuilder<PyObject *>::get(this->context_));
    this->f_lineno_addr_ = FrameTy::f_lineno(this->builder_, this->frame_);
    this->f_lasti_addr_ = FrameTy::f_lasti(this->builder_, this->frame_);

    BasicBlock *start = this->CreateBasicBlock("body_start");
    if (this->is_generator_) {
      // Support generator.throw().  If frame->f_throwflag is set, the
      // caller has set an exception, and we're supposed to propagate
      // it.
      BasicBlock *propagate_generator_throw =
          this->CreateBasicBlock("propagate_generator_throw");
      BasicBlock *continue_generator_or_start_func =
          this->CreateBasicBlock("continue_generator_or_start_func");

      Value *throwflag = this->builder_.CreateLoad(
          FrameTy::f_throwflag(this->builder_, this->frame_),
          "f_throwflag");
      this->builder_.CreateCondBr(
          this->IsNonZero(throwflag),
          propagate_generator_throw, continue_generator_or_start_func);

      this->builder_.SetInsertPoint(propagate_generator_throw);
      PropagateException();

      this->builder_.SetInsertPoint(continue_generator_or_start_func);
      Value *resume_block = this->builder_.CreateLoad(
          this->f_lasti_addr_, "resume_block");
      // Each use of a YIELD_VALUE opcode will add a new case to this
      // switch.  eval.cc just assigns the new IP, allowing wild jumps,
      // but LLVM won't let us do that so we default to jumping to the
      // unreachable block.
      this->yield_resume_switch_ =
          this->builder_.CreateSwitch(resume_block, this->unreachable_block_);

      this->yield_resume_switch_->addCase(
          ConstantInt::getSigned(PyTypeBuilder<int>::get(this->context_), -1),
          start);
    } else {
      // This function is not a generator, so we just jump to the start.
      this->builder_.CreateBr(start);
    }

    this->builder_.SetInsertPoint(this->unreachable_block_);
#ifndef NDEBUG
    // In debug mode, die when we get to unreachable code.  In
    // optimized mode, let the LLVM optimizers get rid of it.
    this->Abort("Jumped to unreachable code.");
#endif  // NDEBUG
    this->builder_.CreateUnreachable();

    FillBailToInterpreterBlock();
    FillPropagateExceptionBlock();
    FillUnwindBlock();
    FillDoReturnBlock();

    this->builder_.SetInsertPoint(start);
#ifdef WITH_TSC
    this->LogTscEvent(CALL_ENTER_LLVM);
#endif
}

void
LlvmFunctionBuilder::FillPropagateExceptionBlock()
{
    this->builder_.SetInsertPoint(this->propagate_exception_block_);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_)),
        this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_EXCEPTION),
                               this->unwind_reason_addr_);
    this->CreateCall(
        this->GetGlobalFunction<int(PyFrameObject*)>("PyTraceBack_Here"),
        this->frame_);
    BasicBlock *call_exc_trace =
        this->CreateBasicBlock("call_exc_trace");
    Value *tracefunc = this->builder_.CreateLoad(
        ThreadStateTy::c_tracefunc(this->builder_, this->tstate_));
    this->builder_.CreateCondBr(this->IsNull(tracefunc),
                                this->unwind_block_, call_exc_trace);

    this->builder_.SetInsertPoint(call_exc_trace);
    this->CreateCall(
        this->GetGlobalFunction<void(PyThreadState *, PyFrameObject *)>(
            "_PyEval_CallExcTrace"),
        this->tstate_, this->frame_);
    this->builder_.CreateBr(this->unwind_block_);
}

void
LlvmFunctionBuilder::FillUnwindBlock()
{
    // Handles, roughly, the eval.cc JUMPTO macro.
    BasicBlock *goto_unwind_target_block =
        this->CreateBasicBlock("goto_unwind_target");
    this->builder_.SetInsertPoint(goto_unwind_target_block);
    Value *unwind_target_index =
        this->builder_.CreateLoad(this->unwind_target_index_addr_,
                                  "unwind_target_index");
    // Each call to AddUnwindTarget() will add a new case to this
    // switch.  eval.cc just assigns the new IP, allowing wild jumps,
    // but LLVM won't let us do that so we default to jumping to the
    // unreachable block.
    this->unwind_target_switch_ = this->builder_.CreateSwitch(
        unwind_target_index, this->unreachable_block_);

    // Code that needs to unwind the stack will jump here.
    // (e.g. returns, exceptions, breaks, and continues).
    this->builder_.SetInsertPoint(this->unwind_block_);
    Value *unwind_reason =
        this->builder_.CreateLoad(this->unwind_reason_addr_, "unwind_reason");

    BasicBlock *pop_remaining_objects =
        this->CreateBasicBlock("pop_remaining_objects");
    {  // Implements the fast_block_end loop toward the end of
       // PyEval_EvalFrame().  This pops blocks off the block-stack
       // and values off the value-stack until it finds a block that
       // wants to handle the current unwind reason.
        BasicBlock *unwind_loop_header =
            this->CreateBasicBlock("unwind_loop_header");
        BasicBlock *unwind_loop_body =
            this->CreateBasicBlock("unwind_loop_body");

        this->FallThroughTo(unwind_loop_header);
        // Continue looping if we still have blocks left on the blockstack.
        Value *blocks_left = this->builder_.CreateLoad(this->num_blocks_addr_);
        this->builder_.CreateCondBr(this->IsPositive(blocks_left),
                                    unwind_loop_body, pop_remaining_objects);

        this->builder_.SetInsertPoint(unwind_loop_body);
        Value *popped_block = this->CreateCall(
            this->GetGlobalFunction<PyTryBlock *(PyTryBlock *, char *)>(
                "_PyLlvm_Frame_BlockPop"),
            this->blockstack_addr_,
            this->num_blocks_addr_);
        Value *block_type = this->builder_.CreateLoad(
            PyTypeBuilder<PyTryBlock>::b_type(this->builder_, popped_block),
            "block_type");
        Value *block_handler = this->builder_.CreateLoad(
            PyTypeBuilder<PyTryBlock>::b_handler(this->builder_,
                                                     popped_block),
            "block_handler");
        Value *block_level = this->builder_.CreateLoad(
            PyTypeBuilder<PyTryBlock>::b_level(this->builder_,
                                                   popped_block),
            "block_level");

        // Handle SETUP_LOOP with UNWIND_CONTINUE.
        BasicBlock *not_continue =
            this->CreateBasicBlock("not_continue");
        BasicBlock *unwind_continue =
            this->CreateBasicBlock("unwind_continue");
        Value *is_setup_loop = this->builder_.CreateICmpEQ(
            block_type,
            ConstantInt::get(block_type->getType(), ::SETUP_LOOP),
            "is_setup_loop");
        Value *is_continue = this->builder_.CreateICmpEQ(
            unwind_reason,
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_CONTINUE),
            "is_continue");
        this->builder_.CreateCondBr(
            this->builder_.CreateAnd(is_setup_loop, is_continue),
            unwind_continue, not_continue);

        this->builder_.SetInsertPoint(unwind_continue);
        // Put the loop block back on the stack, clear the unwind reason,
        // then jump to the proper FOR_ITER.
        Value *args[] = {
            this->blockstack_addr_,
            this->num_blocks_addr_,
            block_type,
            block_handler,
            block_level
        };
        this->CreateCall(
            this->GetGlobalFunction<void(PyTryBlock *, char *, int, int, int)>(
                "_PyLlvm_Frame_BlockSetup"),
            args, array_endof(args));
        this->builder_.CreateStore(
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_NOUNWIND),
            this->unwind_reason_addr_);
        // Convert the return value to the unwind target. This is in keeping
        // with eval.cc. There's probably some LLVM magic that will allow
        // us to skip the boxing/unboxing, but this will work for now.
        Value *boxed_retval = this->builder_.CreateLoad(this->retval_addr_);
        Value *unbox_retval = this->builder_.CreateTrunc(
            this->CreateCall(
                this->GetGlobalFunction<long(PyObject *)>("PyInt_AsLong"),
                boxed_retval),
            Type::getInt32Ty(this->context_),
            "unboxed_retval");
        this->DecRef(boxed_retval);
        this->builder_.CreateStore(unbox_retval,
                                   this->unwind_target_index_addr_);
        this->builder_.CreateBr(goto_unwind_target_block);

        this->builder_.SetInsertPoint(not_continue);
        // Pop values back to where this block started.
        this->PopAndDecrefTo(
            this->builder_.CreateGEP(this->stack_bottom_, block_level));

        BasicBlock *handle_loop =
            this->CreateBasicBlock("handle_loop");
        BasicBlock *handle_except =
            this->CreateBasicBlock("handle_except");
        BasicBlock *handle_finally =
            this->CreateBasicBlock("handle_finally");
        BasicBlock *push_exception =
            this->CreateBasicBlock("push_exception");
        BasicBlock *goto_block_handler =
            this->CreateBasicBlock("goto_block_handler");

        llvm::SwitchInst *block_type_switch = this->builder_.CreateSwitch(
            block_type, this->unreachable_block_, 3);
        const llvm::IntegerType *block_type_type =
            llvm::cast<llvm::IntegerType>(block_type->getType());
        block_type_switch->addCase(
            ConstantInt::get(block_type_type, ::SETUP_LOOP),
            handle_loop);
        block_type_switch->addCase(
            ConstantInt::get(block_type_type, ::SETUP_EXCEPT),
            handle_except);
        block_type_switch->addCase(
            ConstantInt::get(block_type_type, ::SETUP_FINALLY),
            handle_finally);

        this->builder_.SetInsertPoint(handle_loop);
        Value *unwinding_break = this->builder_.CreateICmpEQ(
            unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                            UNWIND_BREAK),
            "currently_unwinding_break");
        this->builder_.CreateCondBr(unwinding_break,
                                    goto_block_handler, unwind_loop_header);

        this->builder_.SetInsertPoint(handle_except);
        // We only consider visiting except blocks when an exception
        // is being unwound.
        Value *unwinding_exception = this->builder_.CreateICmpEQ(
            unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                            UNWIND_EXCEPTION),
            "currently_unwinding_exception");
        this->builder_.CreateCondBr(unwinding_exception,
                                    push_exception, unwind_loop_header);

        this->builder_.SetInsertPoint(push_exception);
        // We need an alloca here so _PyLlvm_FastEnterExceptOrFinally
        // can return into it.  This alloca _won't_ be optimized by
        // mem2reg because its address is taken.
        Value *exc_info = this->CreateAllocaInEntryBlock(
            PyTypeBuilder<PyExcInfo>::get(this->context_), NULL, "exc_info");
        this->CreateCall(
            this->GetGlobalFunction<void(PyExcInfo*, int)>(
                "_PyLlvm_FastEnterExceptOrFinally"),
            exc_info,
            block_type);
        this->Push(this->builder_.CreateLoad(
                       this->builder_.CreateStructGEP(
                           exc_info, PyTypeBuilder<PyExcInfo>::FIELD_TB)));
        this->Push(this->builder_.CreateLoad(
                       this->builder_.CreateStructGEP(
                           exc_info,
                           PyTypeBuilder<PyExcInfo>::FIELD_VAL)));
        this->Push(this->builder_.CreateLoad(
                       this->builder_.CreateStructGEP(
                           exc_info,
                           PyTypeBuilder<PyExcInfo>::FIELD_EXC)));
        this->builder_.CreateBr(goto_block_handler);

        this->builder_.SetInsertPoint(handle_finally);
        // Jump to the finally block, with the stack prepared for
        // END_FINALLY to continue unwinding.

        BasicBlock *push_retval =
            this->CreateBasicBlock("push_retval");
        BasicBlock *handle_finally_end =
            this->CreateBasicBlock("handle_finally_end");
        llvm::SwitchInst *should_push_retval = this->builder_.CreateSwitch(
            unwind_reason, handle_finally_end, 2);
        // When unwinding for an exception, we have to save the
        // exception onto the stack.
        should_push_retval->addCase(
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_EXCEPTION),
            push_exception);
        // When unwinding for a return or continue, we have to save
        // the return value or continue target onto the stack.
        should_push_retval->addCase(
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_RETURN),
            push_retval);
        should_push_retval->addCase(
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_CONTINUE),
            push_retval);

        this->builder_.SetInsertPoint(push_retval);
        this->Push(this->builder_.CreateLoad(this->retval_addr_, "retval"));

        this->FallThroughTo(handle_finally_end);
        // END_FINALLY expects to find the unwind reason on the top of
        // the stack.  There's probably a way to do this that doesn't
        // involve allocating an int for every unwind through a
        // finally block, but imitating CPython is simpler.
        Value *unwind_reason_as_pyint = this->CreateCall(
            this->GetGlobalFunction<PyObject *(long)>("PyInt_FromLong"),
            this->builder_.CreateZExt(unwind_reason,
                                      PyTypeBuilder<long>::get(this->context_)),
            "unwind_reason_as_pyint");
        this->Push(unwind_reason_as_pyint);

        this->FallThroughTo(goto_block_handler);
        // Clear the unwind reason while running through the block's
        // handler.  mem2reg should never actually decide to use this
        // value, but having it here should make such forgotten stores
        // more obvious.
        this->builder_.CreateStore(
            ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_NOUNWIND),
            this->unwind_reason_addr_);
        // The block's handler field holds the index of the block
        // defining this finally or except, or the tail of the loop we
        // just broke out of.  Jump to it through the unwind switch
        // statement defined above.
        this->builder_.CreateStore(block_handler,
                                   this->unwind_target_index_addr_);
        this->builder_.CreateBr(goto_unwind_target_block);
    }  // End unwind loop.

    // If we fall off the end of the unwind loop, there are no blocks
    // left and it's time to pop the rest of the value stack and
    // return.
    this->builder_.SetInsertPoint(pop_remaining_objects);
    this->PopAndDecrefTo(this->stack_bottom_);

    // Unless we're returning (or yielding which comes into the
    // do_return_block_ through another path), the retval should be
    // NULL.
    BasicBlock *reset_retval =
        this->CreateBasicBlock("reset_retval");
    Value *unwinding_for_return =
        this->builder_.CreateICmpEQ(
            unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                            UNWIND_RETURN));
    this->builder_.CreateCondBr(unwinding_for_return,
                                this->do_return_block_, reset_retval);

    this->builder_.SetInsertPoint(reset_retval);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_)),
        this->retval_addr_);
    this->builder_.CreateBr(this->do_return_block_);
}

void
LlvmFunctionBuilder::FillDoReturnBlock()
{
    this->builder_.SetInsertPoint(this->do_return_block_);
    BasicBlock *check_frame_exception =
        this->CreateBasicBlock("check_frame_exception");
    BasicBlock *trace_leave_function =
        this->CreateBasicBlock("trace_leave_function");
    BasicBlock *tracer_raised =
        this->CreateBasicBlock("tracer_raised");

    // Trace exiting from this function, if tracing is turned on.
    Value *use_tracing = this->builder_.CreateLoad(
        ThreadStateTy::use_tracing(this->builder_, this->tstate_));
    this->builder_.CreateCondBr(this->IsNonZero(use_tracing),
                                trace_leave_function, check_frame_exception);

    this->builder_.SetInsertPoint(trace_leave_function);
    Value *unwind_reason =
        this->builder_.CreateLoad(this->unwind_reason_addr_);
    Value *is_return = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                        UNWIND_RETURN),
        "is_return");
    Value *is_yield = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                        UNWIND_YIELD),
        "is_yield");
    Value *is_exception = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::getInt8Ty(this->context_),
                                        UNWIND_EXCEPTION),
        "is_exception");
    Value *is_yield_or_return = this->builder_.CreateOr(is_return, is_yield);
    Value *traced_retval = this->builder_.CreateLoad(this->retval_addr_);
    Value *trace_args[] = {
        this->tstate_,
        this->frame_,
        traced_retval,
        this->builder_.CreateIntCast(
            is_yield_or_return, PyTypeBuilder<char>::get(this->context_),
            false /* unsigned */),
        this->builder_.CreateIntCast(
            is_exception, PyTypeBuilder<char>::get(this->context_),
            false /* unsigned */)
    };
    Value *trace_result = this->CreateCall(
        this->GetGlobalFunction<int(PyThreadState *, struct _frame *,
                                    PyObject *, char, char)>(
                                        "_PyEval_TraceLeaveFunction"),
        trace_args, array_endof(trace_args));
    this->builder_.CreateCondBr(this->IsNonZero(trace_result),
                                tracer_raised, check_frame_exception);

    this->builder_.SetInsertPoint(tracer_raised);
    this->XDecRef(traced_retval);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_)),
        this->retval_addr_);
    this->builder_.CreateBr(check_frame_exception);

    this->builder_.SetInsertPoint(check_frame_exception);
    // If this frame raised and caught an exception, it saved it into
    // sys.exc_info(). The calling frame may also be in the process of
    // handling an exception, in which case we don't want to clobber
    // its sys.exc_info().  See eval.cc's _PyEval_ResetExcInfo for
    // details.
    BasicBlock *have_frame_exception =
        this->CreateBasicBlock("have_frame_exception");
    BasicBlock *no_frame_exception =
        this->CreateBasicBlock("no_frame_exception");
    BasicBlock *finish_return =
        this->CreateBasicBlock("finish_return");
    Value *tstate_frame = this->builder_.CreateLoad(
        ThreadStateTy::frame(this->builder_, this->tstate_),
        "tstate->frame");
    Value *f_exc_type = this->builder_.CreateLoad(
        FrameTy::f_exc_type(this->builder_, tstate_frame),
        "tstate->frame->f_exc_type");
    this->builder_.CreateCondBr(this->IsNull(f_exc_type),
                                no_frame_exception, have_frame_exception);

    this->builder_.SetInsertPoint(have_frame_exception);
    // The frame did have an exception, so un-clobber the caller's exception.
    this->CreateCall(
        this->GetGlobalFunction<void(PyThreadState*)>("_PyEval_ResetExcInfo"),
        this->tstate_);
    this->builder_.CreateBr(finish_return);

    this->builder_.SetInsertPoint(no_frame_exception);
    // The frame did not have an exception.  In debug mode, check for
    // consistency.
#ifndef NDEBUG
    Value *f_exc_value = this->builder_.CreateLoad(
        FrameTy::f_exc_value(this->builder_, tstate_frame),
        "tstate->frame->f_exc_value");
    Value *f_exc_traceback = this->builder_.CreateLoad(
        FrameTy::f_exc_traceback(this->builder_, tstate_frame),
        "tstate->frame->f_exc_traceback");
    this->Assert(this->IsNull(f_exc_value),
                 "Frame's exc_type was null but exc_value wasn't");
    this->Assert(this->IsNull(f_exc_traceback),
                 "Frame's exc_type was null but exc_traceback wasn't");
#endif
    this->builder_.CreateBr(finish_return);

    this->builder_.SetInsertPoint(finish_return);
    // Grab the return value and return it.
    Value *retval = this->builder_.CreateLoad(this->retval_addr_, "retval");
    this->CreateRet(retval);
}

// Before jumping to this block, make sure frame->f_lasti points to
// the opcode index at which to resume.
void
LlvmFunctionBuilder::FillBailToInterpreterBlock()
{
    this->builder_.SetInsertPoint(this->bail_to_interpreter_block_);
    // Don't just immediately jump back to the JITted code.
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), 0),
        FrameTy::f_use_llvm(this->builder_, this->frame_));
    // Fill the frame object with any information that was in allocas here.
    this->CopyToFrameObject();

    // Tail-call back to the interpreter.  As of 2009-06-12 this isn't
    // codegen'ed as a tail call
    // (http://llvm.org/docs/CodeGenerator.html#tailcallopt), but that
    // should improve eventually.
    CallInst *bail = this->CreateCall(
        this->GetGlobalFunction<PyObject*(PyFrameObject*)>("PyEval_EvalFrame"),
        this->frame_);
    bail->setTailCall(true);
    this->CreateRet(bail);
}

void
LlvmFunctionBuilder::PopAndDecrefTo(Value *target_stack_pointer)
{
    BasicBlock *pop_loop = this->CreateBasicBlock("pop_loop");
    BasicBlock *pop_block = this->CreateBasicBlock("pop_stack");
    BasicBlock *pop_done = this->CreateBasicBlock("pop_done");

    this->FallThroughTo(pop_loop);
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *finished_popping = this->builder_.CreateICmpULE(
        stack_pointer, target_stack_pointer);
    this->builder_.CreateCondBr(finished_popping, pop_done, pop_block);

    this->builder_.SetInsertPoint(pop_block);
    this->XDecRef(this->Pop());
    this->builder_.CreateBr(pop_loop);

    this->builder_.SetInsertPoint(pop_done);
}

Value *
LlvmFunctionBuilder::CreateAllocaInEntryBlock(
    const Type *alloca_type, Value *array_size, const char *name="")
{
    // In order for LLVM to optimize alloca's, we should emit alloca
    // instructions in the function entry block. We can get at the
    // block with this->function_->begin(), but it will already have a
    // 'br' instruction at the end. Instantiating the AllocaInst class
    // directly, we pass it the begin() iterator of the entry block,
    // causing it to insert itself right before the first instruction
    // in the block.
    return new llvm::AllocaInst(alloca_type, array_size, name,
                                this->function_->begin()->begin());
}

void
LlvmFunctionBuilder::MemCpy(llvm::Value *target,
                            llvm::Value *array, llvm::Value *N)
{
    const Type *len_type[] = { Type::getInt64Ty(this->context_) };
    Value *memcpy = Intrinsic::getDeclaration(
        this->module_, Intrinsic::memcpy, len_type, 1);
    assert(target->getType() == array->getType() &&
           "memcpy's source and destination should have the same type.");
    // Calculate the length as int64_t(&array_type(NULL)[N]).
    Value *length = this->builder_.CreatePtrToInt(
        this->builder_.CreateGEP(Constant::getNullValue(array->getType()), N),
        Type::getInt64Ty(this->context_));
    const Type *char_star_type = PyTypeBuilder<char*>::get(this->context_);
    this->CreateCall(
        memcpy,
        this->builder_.CreateBitCast(target, char_star_type),
        this->builder_.CreateBitCast(array, char_star_type),
        length,
        // Unknown alignment.
        ConstantInt::get(Type::getInt32Ty(this->context_), 0));
}

void
LlvmFunctionBuilder::CopyToFrameObject()
{
    // Save the current stack pointer into the frame.
    // Note that locals are mirrored to the frame as they're modified.
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *f_stacktop = FrameTy::f_stacktop(this->builder_, this->frame_);
    this->builder_.CreateStore(stack_pointer, f_stacktop);
    Value *num_blocks = this->builder_.CreateLoad(this->num_blocks_addr_);
    this->builder_.CreateStore(num_blocks,
                               FrameTy::f_iblock(this->builder_, this->frame_));
    this->MemCpy(this->builder_.CreateStructGEP(
                     FrameTy::f_blockstack(this->builder_, this->frame_), 0),
                 this->blockstack_addr_, num_blocks);
}

void
LlvmFunctionBuilder::CopyFromFrameObject()
{
    Value *f_stacktop = FrameTy::f_stacktop(this->builder_, this->frame_);
    Value *stack_pointer =
        this->builder_.CreateLoad(f_stacktop,
                                  "stack_pointer_from_frame");
    this->builder_.CreateStore(stack_pointer, this->stack_pointer_addr_);
    /* f_stacktop remains NULL unless yield suspends the frame. */
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject **>::get(this->context_)),
        f_stacktop);

    Value *num_blocks = this->builder_.CreateLoad(
        FrameTy::f_iblock(this->builder_, this->frame_));
    this->builder_.CreateStore(num_blocks, this->num_blocks_addr_);
    this->MemCpy(this->blockstack_addr_,
                 this->builder_.CreateStructGEP(
                     FrameTy::f_blockstack(this->builder_, this->frame_), 0),
                 num_blocks);

    this->CopyLocalsFromFrameObject();
}

int
LlvmFunctionBuilder::GetParamCount() const
{
    int co_flags = this->code_object_->co_flags;
    return this->code_object_->co_argcount +
        bool(co_flags & CO_VARARGS) + bool(co_flags & CO_VARKEYWORDS);
}


// Rules for copying locals from the frame:
// - If this is a generator, copy everything from the frame.
// - If this is a regular function, only copy the function's parameters; these
//   can never be NULL. Set all other locals to NULL explicitly. This gives
//   LLVM's optimizers more information.
//
// TODO(collinwinter): when LLVM's metadata supports it, mark all parameters
// as "not-NULL" so that constant propagation can have more information to work
// with.
void
LlvmFunctionBuilder::CopyLocalsFromFrameObject()
{
    const Type *int_type = Type::getInt32Ty(this->context_);
    Value *locals =
        this->builder_.CreateStructGEP(
                 FrameTy::f_localsplus(this->builder_, this->frame_), 0);
    Value *null =
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_));

    // Figure out how many total parameters we have.
    int param_count = this->GetParamCount();

    for (int i = 0; i < this->code_object_->co_nlocals; ++i) {
        PyObject *pyname =
            PyTuple_GET_ITEM(this->code_object_->co_varnames, i);

        if (this->is_generator_ || i < param_count) {
            Value *local_slot = this->builder_.CreateLoad(
                this->builder_.CreateGEP(
                    locals, ConstantInt::get(int_type, i)),
                "local_" + std::string(PyString_AsString(pyname)));

            this->builder_.CreateStore(local_slot, this->locals_[i]);
        }
        else {
            this->builder_.CreateStore(null, this->locals_[i]);
        }
    }
}

void
LlvmFunctionBuilder::SetLasti(int current_instruction_index)
{
    this->f_lasti_ = current_instruction_index;
}

void
LlvmFunctionBuilder::SetLineNumber(int line)
{
    BasicBlock *this_line = this->CreateBasicBlock("line_start");

    this->builder_.CreateStore(
        this->GetSigned<int>(line),
        this->f_lineno_addr_);
    this->SetDebugStopPoint(line);

    this->MaybeCallLineTrace(this_line, _PYFRAME_LINE_TRACE);

    this->builder_.SetInsertPoint(this_line);
}

void
LlvmFunctionBuilder::FillBackedgeLanding(BasicBlock *backedge_landing,
                                         BasicBlock *target,
                                         bool to_start_of_line,
                                         int line_number)
{
    BasicBlock *continue_backedge = NULL;
    if (to_start_of_line) {
        continue_backedge = target;
    }
    else {
        continue_backedge = this->CreateBasicBlock(
                backedge_landing->getName() + ".cont");
    }

    this->builder_.SetInsertPoint(backedge_landing);
    this->CheckPyTicker(continue_backedge);

    if (!to_start_of_line) {
        continue_backedge->moveAfter(backedge_landing);
        this->builder_.SetInsertPoint(continue_backedge);
        // Record the new line number.  This is after _Py_Ticker, so
        // exceptions from signals will appear to come from the source of
        // the backedge.
        this->builder_.CreateStore(
            ConstantInt::getSigned(PyTypeBuilder<int>::get(this->context_),
                                   line_number),
            this->f_lineno_addr_);
        this->SetDebugStopPoint(line_number);

        // If tracing has been turned on, jump back to the interpreter.
        this->MaybeCallLineTrace(target, _PYFRAME_BACKEDGE_TRACE);
    }
}

void
LlvmFunctionBuilder::MaybeCallLineTrace(BasicBlock *fallthrough_block,
                                        char direction)
{
    BasicBlock *call_trace = this->CreateBasicBlock("call_trace");

    Value *tracing_possible = this->builder_.CreateLoad(
        this->GET_GLOBAL_VARIABLE(int, _Py_TracingPossible));
    this->builder_.CreateCondBr(this->IsNonZero(tracing_possible),
                                call_trace, fallthrough_block);

    this->builder_.SetInsertPoint(call_trace);
    this->CreateBailPoint(direction);
}

void
LlvmFunctionBuilder::BailIfProfiling(llvm::BasicBlock *fallthrough_block)
{
    BasicBlock *profiling = this->CreateBasicBlock("profiling");

    Value *profiling_possible = this->builder_.CreateLoad(
        this->GET_GLOBAL_VARIABLE(int, _Py_ProfilingPossible));
    this->builder_.CreateCondBr(this->IsNonZero(profiling_possible),
                                profiling, fallthrough_block);

    this->builder_.SetInsertPoint(profiling);
    this->CreateBailPoint(_PYFRAME_CALL_PROFILE);
}

void
LlvmFunctionBuilder::FallThroughTo(BasicBlock *next_block)
{
    if (this->builder_.GetInsertBlock()->getTerminator() == NULL) {
        // If the block doesn't already end with a branch or
        // return, branch to the next block.
        this->builder_.CreateBr(next_block);
    }
    this->builder_.SetInsertPoint(next_block);
}

ConstantInt *
LlvmFunctionBuilder::AddUnwindTarget(llvm::BasicBlock *target,
                                     int target_opindex)
{
    // The size of the switch instruction will give us a small unique
    // number for each target block.
    ConstantInt *target_index =
            ConstantInt::get(Type::getInt32Ty(this->context_), target_opindex);
    if (!this->existing_unwind_targets_.test(target_opindex)) {
        this->unwind_target_switch_->addCase(target_index, target);
        this->existing_unwind_targets_.set(target_opindex);
    }
    return target_index;
}

void
LlvmFunctionBuilder::Return(Value *retval)
{
    this->builder_.CreateStore(retval, this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_RETURN),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);
}

void
LlvmFunctionBuilder::PropagateException()
{
    this->builder_.CreateBr(this->propagate_exception_block_);
}

void
LlvmFunctionBuilder::SetDebugStopPoint(int line_number)
{
    if (this->debug_info_ != NULL)
        this->debug_info_->InsertStopPoint(this->debug_compile_unit_,
                                           line_number,
                                           0,
                                           this->builder_.GetInsertBlock());
}

void
LlvmFunctionBuilder::LOAD_CONST(int index)
{
    PyObject *co_consts = this->code_object_->co_consts;
    Value *const_ = this->builder_.CreateBitCast(
        this->GetGlobalVariableFor(PyTuple_GET_ITEM(co_consts, index)),
        PyTypeBuilder<PyObject*>::get(this->context_));
    this->IncRef(const_);
    this->Push(const_);
}

void
LlvmFunctionBuilder::LOAD_GLOBAL_safe(int name_index)
{
    BasicBlock *global_missing =
            this->CreateBasicBlock("LOAD_GLOBAL_global_missing");
    BasicBlock *global_success =
            this->CreateBasicBlock("LOAD_GLOBAL_global_success");
    BasicBlock *builtin_missing =
            this->CreateBasicBlock("LOAD_GLOBAL_builtin_missing");
    BasicBlock *builtin_success =
            this->CreateBasicBlock("LOAD_GLOBAL_builtin_success");
    BasicBlock *done = this->CreateBasicBlock("LOAD_GLOBAL_done");
#ifdef WITH_TSC
    this->LogTscEvent(LOAD_GLOBAL_ENTER_LLVM);
#endif
    Value *name = this->LookupName(name_index);
    Function *pydict_getitem = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *)>("PyDict_GetItem");
    Value *global = this->CreateCall(
        pydict_getitem, this->globals_, name, "global_variable");
    this->builder_.CreateCondBr(this->IsNull(global),
                                global_missing, global_success);

    this->builder_.SetInsertPoint(global_success);
    this->IncRef(global);
    this->Push(global);
    this->builder_.CreateBr(done);

    this->builder_.SetInsertPoint(global_missing);
    // This ignores any exception set by PyDict_GetItem (and similarly
    // for the builtins dict below,) but this is what ceval does too.
    Value *builtin = this->CreateCall(
        pydict_getitem, this->builtins_, name, "builtin_variable");
    this->builder_.CreateCondBr(this->IsNull(builtin),
                                builtin_missing, builtin_success);

    this->builder_.SetInsertPoint(builtin_missing);
    Function *do_raise = this->GetGlobalFunction<
        void(PyObject *)>("_PyEval_RaiseForGlobalNameError");
    this->CreateCall(do_raise, name);
    this->PropagateException();

    this->builder_.SetInsertPoint(builtin_success);
    this->IncRef(builtin);
    this->Push(builtin);
    this->builder_.CreateBr(done);

    this->builder_.SetInsertPoint(done);
#ifdef WITH_TSC
    this->LogTscEvent(LOAD_GLOBAL_EXIT_LLVM);
#endif
}

void
LlvmFunctionBuilder::LOAD_GLOBAL_fast(int name_index)
{
    PyCodeObject *code = this->code_object_;
    PyObject *name = PyTuple_GET_ITEM(code->co_names, name_index);
    PyObject *obj = PyDict_GetItem(code->co_assumed_globals, name);
    if (obj == NULL) {
        obj = PyDict_GetItem(code->co_assumed_builtins, name);
        if (obj == NULL) {
            /* This isn't necessarily an error: it's legal Python code to refer
               to globals that aren't yet defined at compilation time. Is it a
               bad idea? Almost certainly. Is it legal? Unfortunatley. */
            this->LOAD_GLOBAL_safe(name_index);
            return;
        }
    }
    this->uses_load_global_opt_ = true;

    BasicBlock *keep_going = this->CreateBasicBlock("LOAD_GLOBAL_keep_going");
    BasicBlock *invalid_assumptions =
        this->CreateBasicBlock("LOAD_GLOBAL_invalid_assumptions");

#ifdef WITH_TSC
    this->LogTscEvent(LOAD_GLOBAL_ENTER_LLVM);
#endif
    Value *use_llvm = this->builder_.CreateLoad(this->use_llvm_addr_,
                                                "co_use_llvm");
    this->builder_.CreateCondBr(this->IsNonZero(use_llvm),
                                keep_going,
                                invalid_assumptions);

    /* Our assumptions about the state of the globals/builtins no longer hold;
       bail back to the interpreter. */
    this->builder_.SetInsertPoint(invalid_assumptions);
    this->CreateBailPoint(_PYFRAME_FATAL_GUARD_FAIL);

    /* Our assumptions are still valid; encode the result of the lookups as an
       immediate in the IR. */
    this->builder_.SetInsertPoint(keep_going);
    Value *global = ConstantExpr::getIntToPtr(
        ConstantInt::get(
            Type::getInt64Ty(this->context_),
            reinterpret_cast<uintptr_t>(obj)),
        PyTypeBuilder<PyObject*>::get(this->context_));
    this->IncRef(global);
    this->Push(global);

#ifdef WITH_TSC
    this->LogTscEvent(LOAD_GLOBAL_EXIT_LLVM);
#endif
}

void
LlvmFunctionBuilder::LOAD_GLOBAL(int name_index)
{
    // A code object might not have CO_FDO_GLOBALS set if
    // a) it was compiled by setting co_optimization, or
    // b) we couldn't watch the globals/builtins dicts.
    if (this->code_object_->co_flags & CO_FDO_GLOBALS)
        this->LOAD_GLOBAL_fast(name_index);
    else
        this->LOAD_GLOBAL_safe(name_index);
}

void
LlvmFunctionBuilder::STORE_GLOBAL(int name_index)
{
    Value *name = this->LookupName(name_index);
    Value *value = this->Pop();
    Function *pydict_setitem = this->GetGlobalFunction<
        int(PyObject *, PyObject *, PyObject *)>("PyDict_SetItem");
    Value *result = this->CreateCall(
        pydict_setitem, this->globals_, name, value,
        "STORE_GLOBAL_result");
    this->DecRef(value);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::DELETE_GLOBAL(int name_index)
{
    BasicBlock *failure = this->CreateBasicBlock("DELETE_GLOBAL_failure");
    BasicBlock *success = this->CreateBasicBlock("DELETE_GLOBAL_success");
    Value *name = this->LookupName(name_index);
    Function *pydict_setitem = this->GetGlobalFunction<
        int(PyObject *, PyObject *)>("PyDict_DelItem");
    Value *result = this->CreateCall(
        pydict_setitem, this->globals_, name, "STORE_GLOBAL_result");
    this->builder_.CreateCondBr(this->IsNonZero(result), failure, success);

    this->builder_.SetInsertPoint(failure);
    Function *do_raise = this->GetGlobalFunction<
        void(PyObject *)>("_PyEval_RaiseForGlobalNameError");
    this->CreateCall(do_raise, name);
    this->PropagateException();

    this->builder_.SetInsertPoint(success);
}

void
LlvmFunctionBuilder::LOAD_NAME(int index)
{
    Value *result = this->CreateCall(
        this->GetGlobalFunction<PyObject *(PyFrameObject*, int)>(
            "_PyEval_LoadName"),
        this->frame_,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), index));
    PropagateExceptionOnNull(result);
    Push(result);
}

void
LlvmFunctionBuilder::STORE_NAME(int index)
{
    Value *to_store = this->Pop();
    Value *err = this->CreateCall(
        this->GetGlobalFunction<int(PyFrameObject*, int, PyObject*)>(
            "_PyEval_StoreName"),
        this->frame_,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), index),
        to_store);
    PropagateExceptionOnNonZero(err);
}

void
LlvmFunctionBuilder::DELETE_NAME(int index)
{
    Value *err = this->CreateCall(
        this->GetGlobalFunction<int(PyFrameObject*, int)>(
            "_PyEval_DeleteName"),
        this->frame_,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), index));
    PropagateExceptionOnNonZero(err);
}

void
LlvmFunctionBuilder::LOAD_ATTR(int index)
{
    Value *attr = this->LookupName(index);
    Value *obj = this->Pop();
    Function *pyobj_getattr = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *)>("PyObject_GetAttr");
    Value *result = this->CreateCall(
        pyobj_getattr, obj, attr, "LOAD_ATTR_result");
    this->DecRef(obj);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::STORE_ATTR(int index)
{
    Value *attr = this->LookupName(index);
    Value *obj = this->Pop();
    Value *value = this->Pop();
    Function *pyobj_setattr = this->GetGlobalFunction<
        int(PyObject *, PyObject *, PyObject *)>("PyObject_SetAttr");
    Value *result = this->CreateCall(
        pyobj_setattr, obj, attr, value, "STORE_ATTR_result");
    this->DecRef(obj);
    this->DecRef(value);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::DELETE_ATTR(int index)
{
    Value *attr = this->LookupName(index);
    Value *obj = this->Pop();
    Value *value =
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_));
    Function *pyobj_setattr = this->GetGlobalFunction<
        int(PyObject *, PyObject *, PyObject *)>("PyObject_SetAttr");
    Value *result = this->CreateCall(
        pyobj_setattr, obj, attr, value, "STORE_ATTR_result");
    this->DecRef(obj);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::LOAD_FAST_fast(int index)
{
    Value *local = this->builder_.CreateLoad(
        this->locals_[index], "FAST_loaded");
#ifndef NDEBUG
    Value *frame_local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                            index));
    Value *frame_local = this->builder_.CreateLoad(frame_local_slot);
    Value *sane_locals = this->builder_.CreateICmpEQ(frame_local, local);
    this->Assert(sane_locals, "alloca locals do not match frame locals!");
#endif  /* NDEBUG */
    this->IncRef(local);
    this->Push(local);
}

void
LlvmFunctionBuilder::LOAD_FAST_safe(int index)
{
    BasicBlock *unbound_local =
        this->CreateBasicBlock("LOAD_FAST_unbound");
    BasicBlock *success =
        this->CreateBasicBlock("LOAD_FAST_success");

    Value *local = this->builder_.CreateLoad(
        this->locals_[index], "FAST_loaded");
#ifndef NDEBUG
    Value *frame_local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                            index));
    Value *frame_local = this->builder_.CreateLoad(frame_local_slot);
    Value *sane_locals = this->builder_.CreateICmpEQ(frame_local, local);
    this->Assert(sane_locals, "alloca locals do not match frame locals!");
#endif  /* NDEBUG */
    this->builder_.CreateCondBr(this->IsNull(local), unbound_local, success);

    this->builder_.SetInsertPoint(unbound_local);
    Function *do_raise =
        this->GetGlobalFunction<void(PyFrameObject*, int)>(
            "_PyEval_RaiseForUnboundLocal");
    this->CreateCall(do_raise, this->frame_, this->GetSigned<int>(index));
    this->PropagateException();

    this->builder_.SetInsertPoint(success);
    this->IncRef(local);
    this->Push(local);
}

// TODO(collinwinter): we'd like to implement this by simply marking the load
// as "cannot be NULL" and let LLVM's constant propgation optimizers remove the
// conditional branch for us. That is currently not supported, so we do this
// manually.
void
LlvmFunctionBuilder::LOAD_FAST(int index)
{
    // Simple check: if DELETE_FAST is never used, function parameters cannot
    // be NULL.
    if (!this->uses_delete_fast && index < this->GetParamCount())
        this->LOAD_FAST_fast(index);
    else
        this->LOAD_FAST_safe(index);
}

void
LlvmFunctionBuilder::WITH_CLEANUP()
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
       re-raising the exception. (But non-local gotos
       should still be resumed.)
    */

    Value *exc_type = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(this->context_),
        NULL, "WITH_CLEANUP_exc_type");
    Value *exc_value = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(this->context_),
        NULL, "WITH_CLEANUP_exc_value");
    Value *exc_traceback = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(this->context_),
        NULL, "WITH_CLEANUP_exc_traceback");
    Value *exit_func = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(this->context_),
        NULL, "WITH_CLEANUP_exit_func");

    BasicBlock *handle_none =
        this->CreateBasicBlock("WITH_CLEANUP_handle_none");
    BasicBlock *check_int =
        this->CreateBasicBlock("WITH_CLEANUP_check_int");
    BasicBlock *handle_int =
        this->CreateBasicBlock("WITH_CLEANUP_handle_int");
    BasicBlock *handle_ret_cont =
        this->CreateBasicBlock("WITH_CLEANUP_handle_ret_cont");
    BasicBlock *handle_default =
        this->CreateBasicBlock("WITH_CLEANUP_handle_default");
    BasicBlock *handle_else =
        this->CreateBasicBlock("WITH_CLEANUP_handle_else");
    BasicBlock *main_block =
        this->CreateBasicBlock("WITH_CLEANUP_main_block");

    Value *none = this->GET_GLOBAL_VARIABLE(PyObject, _Py_NoneStruct);
    this->builder_.CreateStore(this->Pop(), exc_type);

    Value *is_none = this->builder_.CreateICmpEQ(
        this->builder_.CreateLoad(exc_type), none,
        "reason_is_none");
    this->builder_.CreateCondBr(is_none, handle_none, check_int);

    this->builder_.SetInsertPoint(handle_none);
    this->builder_.CreateStore(this->Pop(), exit_func);
    this->Push(this->builder_.CreateLoad(exc_type));
    this->builder_.CreateStore(none, exc_value);
    this->builder_.CreateStore(none, exc_traceback);
    this->builder_.CreateBr(main_block);

    this->builder_.SetInsertPoint(check_int);
    Value *is_int = this->CreateCall(
        this->GetGlobalFunction<int(PyObject *)>("_PyLlvm_WrapIntCheck"),
        this->builder_.CreateLoad(exc_type),
        "WITH_CLEANUP_pyint_check");
    this->builder_.CreateCondBr(this->IsNonZero(is_int),
                                handle_int, handle_else);

    this->builder_.SetInsertPoint(handle_int);
    Value *unboxed = this->builder_.CreateTrunc(
        this->CreateCall(
            this->GetGlobalFunction<long(PyObject *)>("PyInt_AsLong"),
            this->builder_.CreateLoad(exc_type)),
        Type::getInt8Ty(this->context_),
        "unboxed_unwind_reason");
    // The LLVM equivalent of
    // switch (reason)
    //   case UNWIND_RETURN:
    //   case UNWIND_CONTINUE:
    //     ...
    //     break;
    //   default:
    //     break;
    llvm::SwitchInst *unwind_kind =
        this->builder_.CreateSwitch(unboxed, handle_default, 2);
    unwind_kind->addCase(ConstantInt::get(Type::getInt8Ty(this->context_),
                                          UNWIND_RETURN),
                         handle_ret_cont);
    unwind_kind->addCase(ConstantInt::get(Type::getInt8Ty(this->context_),
                                          UNWIND_CONTINUE),
                         handle_ret_cont);

    this->builder_.SetInsertPoint(handle_ret_cont);
    Value *retval = this->Pop();
    this->builder_.CreateStore(this->Pop(), exit_func);
    this->Push(retval);
    this->Push(this->builder_.CreateLoad(exc_type));
    this->builder_.CreateStore(none, exc_type);
    this->builder_.CreateStore(none, exc_value);
    this->builder_.CreateStore(none, exc_traceback);
    this->builder_.CreateBr(main_block);

    this->builder_.SetInsertPoint(handle_default);
    this->builder_.CreateStore(this->Pop(), exit_func);
    this->Push(this->builder_.CreateLoad(exc_type));
    this->builder_.CreateStore(none, exc_type);
    this->builder_.CreateStore(none, exc_value);
    this->builder_.CreateStore(none, exc_traceback);
    this->builder_.CreateBr(main_block);

    // This is the (TOP, SECOND, THIRD) = exc_info() case above.
    this->builder_.SetInsertPoint(handle_else);
    this->builder_.CreateStore(this->Pop(), exc_value);
    this->builder_.CreateStore(this->Pop(), exc_traceback);
    this->builder_.CreateStore(this->Pop(), exit_func);
    this->Push(this->builder_.CreateLoad(exc_traceback));
    this->Push(this->builder_.CreateLoad(exc_value));
    this->Push(this->builder_.CreateLoad(exc_type));
    this->builder_.CreateBr(main_block);

    this->builder_.SetInsertPoint(main_block);
    // Build a vector because there is no CreateCall5().
    // This is easier than building the tuple ourselves, but doing so would
    // probably be faster.
    std::vector<Value*> args;
    args.push_back(this->builder_.CreateLoad(exit_func));
    args.push_back(this->builder_.CreateLoad(exc_type));
    args.push_back(this->builder_.CreateLoad(exc_value));
    args.push_back(this->builder_.CreateLoad(exc_traceback));
    args.push_back(
        Constant::getNullValue(PyTypeBuilder<PyObject *>::get(this->context_)));
    Value *ret = this->CreateCall(
        this->GetGlobalFunction<PyObject *(PyObject *, ...)>(
            "PyObject_CallFunctionObjArgs"),
        args.begin(), args.end());
    this->DecRef(this->builder_.CreateLoad(exit_func));
    this->PropagateExceptionOnNull(ret);

    BasicBlock *check_silence =
        this->CreateBasicBlock("WITH_CLEANUP_check_silence");
    BasicBlock *no_silence =
        this->CreateBasicBlock("WITH_CLEANUP_no_silence");
    BasicBlock *cleanup =
        this->CreateBasicBlock("WITH_CLEANUP_cleanup");
    BasicBlock *next =
        this->CreateBasicBlock("WITH_CLEANUP_next");

    // Don't bother checking whether to silence the exception if there's
    // no exception to silence.
    this->builder_.CreateCondBr(
        this->builder_.CreateICmpEQ(this->builder_.CreateLoad(exc_type), none),
        no_silence, check_silence);

    this->builder_.SetInsertPoint(no_silence);
    this->DecRef(ret);
    this->builder_.CreateBr(next);

    this->builder_.SetInsertPoint(check_silence);
    this->builder_.CreateCondBr(this->IsPythonTrue(ret), cleanup, next);

    this->builder_.SetInsertPoint(cleanup);
    // There was an exception and a true return. Swallow the exception.
    this->Pop();
    this->Pop();
    this->Pop();
    this->IncRef(none);
    this->Push(none);
    this->DecRef(this->builder_.CreateLoad(exc_type));
    this->DecRef(this->builder_.CreateLoad(exc_value));
    this->DecRef(this->builder_.CreateLoad(exc_traceback));
    this->builder_.CreateBr(next);

    this->builder_.SetInsertPoint(next);
}

void
LlvmFunctionBuilder::LOAD_CLOSURE(int freevars_index)
{
    Value *cell = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            this->freevars_,
            ConstantInt::get(Type::getInt32Ty(this->context_),
                             freevars_index)));
    this->IncRef(cell);
    this->Push(cell);
}

void
LlvmFunctionBuilder::MAKE_CLOSURE(int num_defaults)
{
    Value *code_object = this->Pop();
    Function *pyfunction_new = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *)>("PyFunction_New");
    Value *func_object = this->CreateCall(
        pyfunction_new, code_object, this->globals_, "MAKE_CLOSURE_result");
    this->DecRef(code_object);
    this->PropagateExceptionOnNull(func_object);
    Value *closure = this->Pop();
    Function *pyfunction_setclosure = this->GetGlobalFunction<
        int(PyObject *, PyObject *)>("PyFunction_SetClosure");
    Value *setclosure_result = this->CreateCall(
        pyfunction_setclosure, func_object, closure,
        "MAKE_CLOSURE_setclosure_result");
    this->DecRef(closure);
    this->PropagateExceptionOnNonZero(setclosure_result);
    if (num_defaults > 0) {
        // Effectively inline BuildSequenceLiteral and
        // PropagateExceptionOnNull so we can DecRef func_object on error.
        BasicBlock *failure = this->CreateBasicBlock("MAKE_CLOSURE_failure");
        BasicBlock *success = this->CreateBasicBlock("MAKE_CLOSURE_success");

        Value *tupsize = ConstantInt::get(
            PyTypeBuilder<Py_ssize_t>::get(this->context_), num_defaults);
        Function *pytuple_new =
            this->GetGlobalFunction<PyObject *(Py_ssize_t)>("PyTuple_New");
        Value *defaults = this->CreateCall(pytuple_new, tupsize,
                                                    "MAKE_CLOSURE_defaults");
        this->builder_.CreateCondBr(this->IsNull(defaults),
                                    failure, success);

        this->builder_.SetInsertPoint(failure);
        this->DecRef(func_object);
        this->PropagateException();

        this->builder_.SetInsertPoint(success);
        // XXX(twouters): do this with a memcpy?
        while (--num_defaults >= 0) {
            Value *itemslot = this->GetTupleItemSlot(defaults, num_defaults);
            this->builder_.CreateStore(this->Pop(), itemslot);
        }
        // End of inlining.
        Function *pyfunction_setdefaults = this->GetGlobalFunction<
            int(PyObject *, PyObject *)>("PyFunction_SetDefaults");
        Value *setdefaults_result = this->CreateCall(
            pyfunction_setdefaults, func_object, defaults,
            "MAKE_CLOSURE_setdefaults_result");
        this->DecRef(defaults);
        this->PropagateExceptionOnNonZero(setdefaults_result);
    }
    this->Push(func_object);
}

#ifdef WITH_TSC
void
LlvmFunctionBuilder::LogTscEvent(_PyTscEventId event_id) {
    Function *timer_function = this->GetGlobalFunction<void (int)>(
            "_PyLog_TscEvent");
    // Int8Ty doesn't seem to work here, so we use Int32Ty instead.
    Value *enum_ir = ConstantInt::get(Type::getInt32Ty(this->context_),
                                      event_id);
    this->CreateCall(timer_function, enum_ir);
}
#endif

const PyRuntimeFeedback *
LlvmFunctionBuilder::GetFeedback(unsigned arg_index) const
{
    const PyFeedbackMap *map = this->code_object_->co_runtime_feedback;
    if (map == NULL)
        return NULL;
    return map->GetFeedbackEntry(this->f_lasti_, arg_index);
}

void
LlvmFunctionBuilder::CALL_FUNCTION_fast(int oparg,
                                        const PyRuntimeFeedback *feedback)
{
    CF_INC_STATS(total);

    // Check for keyword arguments; we only optimize callsites with positional
    // arguments.
    if ((oparg >> 8) & 0xff) {
        CF_INC_STATS(no_opt_kwargs);
        this->CALL_FUNCTION_safe(oparg);
        return;
    }

    // Only optimize monomorphic callsites.
    llvm::SmallVector<FunctionRecord*, 3> fdo_data;
    feedback->GetSeenFuncsInto(fdo_data);
    if (fdo_data.size() != 1) {
#ifdef Py_WITH_INSTRUMENTATION
        if (fdo_data.size() == 0)
            CF_INC_STATS(no_opt_no_data);
        else
            CF_INC_STATS(no_opt_polymorphic);
#endif
        this->CALL_FUNCTION_safe(oparg);
        return;
    }

    FunctionRecord *func_record = fdo_data[0];

    // Only optimize calls to C functions with a fixed number of parameters,
    // where the number of arguments we have matches exactly.
    int flags = func_record->flags;
    int num_args = oparg & 0xff;
    if (!((flags & METH_NOARGS && num_args == 0) ||
          (flags & METH_O && num_args == 1))) {
        CF_INC_STATS(no_opt_params);
        this->CALL_FUNCTION_safe(oparg);
        return;
    }

    PyCFunction cfunc_ptr = func_record->func;

    // Expose the C function pointer to LLVM. This is what will actually get
    // called.
    Constant *llvm_func =
        this->llvm_data_->constant_mirror().GetGlobalForCFunction(
            cfunc_ptr,
            func_record->name);

    BasicBlock *not_profiling =
        this->CreateBasicBlock("CALL_FUNCTION_not_profiling");
    BasicBlock *check_is_same_func =
        this->CreateBasicBlock("CALL_FUNCTION_check_is_same_func");
    BasicBlock *invalid_assumptions =
        this->CreateBasicBlock("CALL_FUNCTION_invalid_assumptions");
    BasicBlock *all_assumptions_valid =
        this->CreateBasicBlock("CALL_FUNCTION_all_assumptions_valid");

    this->BailIfProfiling(not_profiling);

    // Handle bailing back to the interpreter if the assumptions below don't
    // hold.
    this->builder_.SetInsertPoint(invalid_assumptions);
    this->CreateBailPoint(_PYFRAME_GUARD_FAIL);

    this->builder_.SetInsertPoint(not_profiling);
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    // Retrieve the function to call from the Python stack.
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *actual_func = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            stack_pointer,
            ConstantInt::getSigned(
                Type::getInt64Ty(this->context_),
                -num_args - 1)));

    // Make sure it's a PyCFunction; if not, bail.
    Value *is_cfunction = this->CreateCall(
        this->GetGlobalFunction<int(PyObject *)>("_PyLlvm_WrapCFunctionCheck"),
        actual_func,
        "is_cfunction");
    Value *is_cfunction_guard = this->builder_.CreateICmpEQ(
        is_cfunction, ConstantInt::get(is_cfunction->getType(), 1),
        "is_cfunction_guard");
    this->builder_.CreateCondBr(is_cfunction_guard, check_is_same_func,
                                invalid_assumptions);

    // Make sure we got the same underlying function pointer; if not, bail.
    this->builder_.SetInsertPoint(check_is_same_func);
    Value *actual_as_pycfunc = this->builder_.CreateBitCast(
        actual_func, PyTypeBuilder<PyCFunctionObject *>::get(this->context_));
    Value *actual_method_def = this->builder_.CreateLoad(
        CFunctionTy::m_ml(this->builder_, actual_as_pycfunc),
        "CALL_FUNCTION_actual_method_def");
    Value *actual_func_ptr = this->builder_.CreateLoad(
        MethodDefTy::ml_meth(this->builder_, actual_method_def),
        "CALL_FUNCTION_actual_func_ptr");
    Value *is_same = this->builder_.CreateICmpEQ(
        // TODO(jyasskin): change this to "llvm_func" when
        // http://llvm.org/PR5126 is fixed.
        this->builder_.CreateIntToPtr(
            ConstantInt::get(Type::getInt64Ty(this->context_),
                             reinterpret_cast<intptr_t>(cfunc_ptr)),
            actual_func_ptr->getType()),
        actual_func_ptr);
    this->builder_.CreateCondBr(is_same,
        all_assumptions_valid, invalid_assumptions);

    // If all the assumptions are valid, we know we have a C function pointer
    // that takes two arguments: first the invocant, second an optional
    // PyObject *. If the function was tagged with METH_NOARGS, we use NULL for
    // the second argument. Because "the invocant" differs between built-in
    // functions like len() and C-level methods like list.append(), we pull
    // the invocant (called m_self) from the PyCFunction object we popped off
    // the stack. Once the function returns, we patch up the stack pointer.
    this->builder_.SetInsertPoint(all_assumptions_valid);
    Value *arg;
    if (num_args == 0) {
        arg = Constant::getNullValue(
            PyTypeBuilder<PyObject *>::get(this->context_));
    }
    else {
        assert(num_args == 1);
        arg = this->builder_.CreateLoad(
            this->builder_.CreateGEP(
                stack_pointer,
                ConstantInt::getSigned(Type::getInt64Ty(this->context_), -1)));
    }
    Value *self = this->builder_.CreateLoad(
        CFunctionTy::m_self(this->builder_, actual_as_pycfunc),
        "CALL_FUNCTION_actual_self");

#ifdef WITH_TSC
    this->LogTscEvent(CALL_ENTER_C);
#endif
    Value *result = this->CreateCall(llvm_func, self, arg);

    this->DecRef(actual_func);
    if (num_args == 1) {
        this->DecRef(arg);
    }
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer,
        ConstantInt::getSigned(
            Type::getInt64Ty(this->context_),
            -num_args - 1));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    this->PropagateExceptionOnNull(result);
    this->Push(result);

    // Check signals and maybe switch threads after each function call.
    this->CheckPyTicker();
    CF_INC_STATS(optimized);
}

void
LlvmFunctionBuilder::CALL_FUNCTION_safe(int oparg)
{
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    int num_args = oparg & 0xff;
    int num_kwargs = (oparg>>8) & 0xff;
    Function *call_function = this->GetGlobalFunction<
        PyObject *(PyObject **, int, int)>("_PyEval_CallFunction");
    Value *result = this->CreateCall(
        call_function,
        stack_pointer,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), num_args),
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), num_kwargs),
        "CALL_FUNCTION_result");
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer,
        ConstantInt::getSigned(Type::getInt64Ty(this->context_),
                               -num_args - 2*num_kwargs - 1));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    this->PropagateExceptionOnNull(result);
    this->Push(result);

    // Check signals and maybe switch threads after each function call.
    this->CheckPyTicker();
}

void
LlvmFunctionBuilder::CALL_FUNCTION(int oparg)
{
    const PyRuntimeFeedback *feedback = this->GetFeedback();
    if (feedback == NULL || feedback->FuncsOverflowed())
        this->CALL_FUNCTION_safe(oparg);
    else
        this->CALL_FUNCTION_fast(oparg, feedback);
}


// Keep this in sync with eval.cc
#define CALL_FLAG_VAR 1
#define CALL_FLAG_KW 2

void
LlvmFunctionBuilder::CallVarKwFunction(int oparg, int call_flag)
{
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    int num_args = oparg & 0xff;
    int num_kwargs = (oparg>>8) & 0xff;
    Function *call_function = this->GetGlobalFunction<
        PyObject *(PyObject **, int, int, int)>("_PyEval_CallFunctionVarKw");
    Value *result = this->CreateCall(
        call_function,
        stack_pointer,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), num_args),
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), num_kwargs),
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), call_flag),
        "CALL_FUNCTION_VAR_KW_result");
    int stack_items = num_args + 2 * num_kwargs + 1;
    if (call_flag & CALL_FLAG_VAR) {
        ++stack_items;
    }
    if (call_flag & CALL_FLAG_KW) {
        ++stack_items;
    }
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer,
        ConstantInt::getSigned(Type::getInt64Ty(this->context_), -stack_items));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    this->PropagateExceptionOnNull(result);
    this->Push(result);

    // Check signals and maybe switch threads after each function call.
    this->CheckPyTicker();
}

void
LlvmFunctionBuilder::CALL_FUNCTION_VAR(int oparg)
{
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    this->CallVarKwFunction(oparg, CALL_FLAG_VAR);
}

void
LlvmFunctionBuilder::CALL_FUNCTION_KW(int oparg)
{
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    this->CallVarKwFunction(oparg, CALL_FLAG_KW);
}

void
LlvmFunctionBuilder::CALL_FUNCTION_VAR_KW(int oparg)
{
#ifdef WITH_TSC
    this->LogTscEvent(CALL_START_LLVM);
#endif
    this->CallVarKwFunction(oparg, CALL_FLAG_KW | CALL_FLAG_VAR);
}

#undef CALL_FLAG_VAR
#undef CALL_FLAG_KW

void
LlvmFunctionBuilder::LOAD_DEREF(int index)
{
    BasicBlock *failed_load =
        this->CreateBasicBlock("LOAD_DEREF_failed_load");
    BasicBlock *unbound_local =
        this->CreateBasicBlock("LOAD_DEREF_unbound_local");
    BasicBlock *error =
        this->CreateBasicBlock("LOAD_DEREF_error");
    BasicBlock *success =
        this->CreateBasicBlock("LOAD_DEREF_success");

    Value *cell = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            this->freevars_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                              index)));
    Function *pycell_get = this->GetGlobalFunction<
        PyObject *(PyObject *)>("PyCell_Get");
    Value *value = this->CreateCall(
        pycell_get, cell, "LOAD_DEREF_cell_contents");
    this->builder_.CreateCondBr(this->IsNull(value), failed_load, success);

    this->builder_.SetInsertPoint(failed_load);
    Function *pyerr_occurred =
        this->GetGlobalFunction<PyObject *()>("PyErr_Occurred");
    Value *was_err =
        this->CreateCall(pyerr_occurred, "LOAD_DEREF_err_occurred");
    this->builder_.CreateCondBr(this->IsNull(was_err), unbound_local, error);

    this->builder_.SetInsertPoint(unbound_local);
    Function *do_raise =
        this->GetGlobalFunction<void(PyFrameObject*, int)>(
            "_PyEval_RaiseForUnboundFreeVar");
    this->CreateCall(
        do_raise, this->frame_,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), index));

    this->FallThroughTo(error);
    this->PropagateException();

    this->builder_.SetInsertPoint(success);
    this->Push(value);
}

void
LlvmFunctionBuilder::STORE_DEREF(int index)
{
    Value *value = this->Pop();
    Value *cell = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            this->freevars_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                              index)));
    Function *pycell_set = this->GetGlobalFunction<
        int(PyObject *, PyObject *)>("PyCell_Set");
    Value *result = this->CreateCall(
        pycell_set, cell, value, "STORE_DEREF_result");
    this->DecRef(value);
    // eval.cc doesn't actually check the return value of this, I guess
    // we are a little more likely to do things wrong.
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::JUMP_ABSOLUTE(llvm::BasicBlock *target,
                                   llvm::BasicBlock *fallthrough)
{
    this->builder_.CreateBr(target);
}

enum BranchInput {
    BranchInputFalse = -1,
    BranchInputUnpredictable = 0,
    BranchInputTrue = 1,
};

// If the branch was predictable, return the branch direction: return
// BranchInputTrue if the branch was always True, return BranchInputFalse
// if the branch was always False. If the branch was unpredictable or if we have
// no data, return 0.
static BranchInput
predict_branch_input(const PyRuntimeFeedback *feedback)
{
    if (feedback == NULL) {
        COND_BRANCH_INC_STATS(not_enough_data);
        return BranchInputUnpredictable;
    }

    uintptr_t was_true = feedback->GetCounter(PY_FDO_JUMP_TRUE);
    uintptr_t was_false = feedback->GetCounter(PY_FDO_JUMP_FALSE);

    // We want to be relatively sure of our prediction. 200 was chosen by
    // running the benchmarks and increasing this threshold until we stopped
    // making massively-bad predictions. Example: increasing the threshold from
    // 100 to 200 reduced bad predictions in 2to3 from 3900+ to 2. We currently
    // optimize only perfectly-predictable branches as a baseline; later work
    // should explore the tradeoffs between bail penalties and improved codegen
    // gained from omiting rarely-taken branches.
    if (was_true + was_false <= 200) {
        COND_BRANCH_INC_STATS(not_enough_data);
        return BranchInputUnpredictable;
    }

    BranchInput result = (BranchInput)(bool(was_true) - bool(was_false));
    if (result == BranchInputUnpredictable) {
        COND_BRANCH_INC_STATS(unpredictable);
    }
    return result;
}

void
LlvmFunctionBuilder::GetPyCondBranchBailBlock(unsigned true_idx,
                                              BasicBlock **true_block,
                                              unsigned false_idx,
                                              BasicBlock **false_block,
                                              unsigned *bail_idx,
                                              BasicBlock **bail_block)
{
    COND_BRANCH_INC_STATS(total);
    BranchInput branch_dir = predict_branch_input(this->GetFeedback());

    if (branch_dir == BranchInputFalse) {
        *bail_idx = false_idx;
        *false_block = *bail_block = this->CreateBasicBlock("FALSE_bail");
    }
    else if (branch_dir == BranchInputTrue) {
        *bail_idx = true_idx;
        *true_block = *bail_block = this->CreateBasicBlock("TRUE_bail");
    }
    else {
        *bail_idx = 0;
        *bail_block = NULL;
    }
}

void
LlvmFunctionBuilder::FillPyCondBranchBailBlock(BasicBlock *bail_to,
                                               unsigned bail_idx)
{
    COND_BRANCH_INC_STATS(optimized);
    BasicBlock *current = this->builder_.GetInsertBlock();

    this->builder_.SetInsertPoint(bail_to);
    this->CreateBailPoint(bail_idx, _PYFRAME_GUARD_FAIL);

    this->builder_.SetInsertPoint(current);
}

void
LlvmFunctionBuilder::POP_JUMP_IF_FALSE(unsigned target_idx,
                                       unsigned fallthrough_idx,
                                       BasicBlock *target,
                                       BasicBlock *fallthrough)
{
    unsigned bail_idx = 0;
    BasicBlock *bail_to = NULL;
    this->GetPyCondBranchBailBlock(/*on true: */ target_idx, &target,
                                   /*on false: */ fallthrough_idx, &fallthrough,
                                   &bail_idx, &bail_to);

    Value *test_value = this->Pop();
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, fallthrough, target);

    if (bail_to)
        this->FillPyCondBranchBailBlock(bail_to, bail_idx);
}

void
LlvmFunctionBuilder::POP_JUMP_IF_TRUE(unsigned target_idx,
                                      unsigned fallthrough_idx,
                                      BasicBlock *target,
                                      BasicBlock *fallthrough)
{
    unsigned bail_idx = 0;
    BasicBlock *bail_to = NULL;
    this->GetPyCondBranchBailBlock(/*on true: */ fallthrough_idx, &fallthrough,
                                   /*on false: */ target_idx, &target,
                                   &bail_idx, &bail_to);

    Value *test_value = this->Pop();
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, target, fallthrough);

    if (bail_to)
        this->FillPyCondBranchBailBlock(bail_to, bail_idx);
}

void
LlvmFunctionBuilder::JUMP_IF_FALSE_OR_POP(unsigned target_idx,
                                          unsigned fallthrough_idx,
                                          BasicBlock *target,
                                          BasicBlock *fallthrough)
{
    unsigned bail_idx = 0;
    BasicBlock *bail_to = NULL;
    this->GetPyCondBranchBailBlock(/*on true: */ target_idx, &target,
                                   /*on false: */ fallthrough_idx, &fallthrough,
                                   &bail_idx, &bail_to);

    BasicBlock *true_path =
        this->CreateBasicBlock("JUMP_IF_FALSE_OR_POP_pop");
    Value *test_value = this->Pop();
    this->Push(test_value);
    // IsPythonTrue() will steal the reference to test_value, so make sure
    // the stack owns one too.
    this->IncRef(test_value);
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, true_path, target);
    this->builder_.SetInsertPoint(true_path);
    test_value = this->Pop();
    this->DecRef(test_value);
    this->builder_.CreateBr(fallthrough);

    if (bail_to)
        this->FillPyCondBranchBailBlock(bail_to, bail_idx);
}

void
LlvmFunctionBuilder::JUMP_IF_TRUE_OR_POP(unsigned target_idx,
                                         unsigned fallthrough_idx,
                                         BasicBlock *target,
                                         BasicBlock *fallthrough)
{
    unsigned bail_idx = 0;
    BasicBlock *bail_to = NULL;
    this->GetPyCondBranchBailBlock(/*on true: */ fallthrough_idx, &fallthrough,
                                   /*on false: */ target_idx, &target,
                                   &bail_idx, &bail_to);

    BasicBlock *false_path =
        this->CreateBasicBlock("JUMP_IF_TRUE_OR_POP_pop");
    Value *test_value = this->Pop();
    this->Push(test_value);
    // IsPythonTrue() will steal the reference to test_value, so make sure
    // the stack owns one too.
    this->IncRef(test_value);
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, target, false_path);
    this->builder_.SetInsertPoint(false_path);
    test_value = this->Pop();
    this->DecRef(test_value);
    this->builder_.CreateBr(fallthrough);

    if (bail_to)
        this->FillPyCondBranchBailBlock(bail_to, bail_idx);
}

void
LlvmFunctionBuilder::CreateBailPoint(unsigned bail_idx, char reason)
{
    this->builder_.CreateStore(
        // -1 so that next_instr gets set right in EvalFrame.
        this->GetSigned<int>(bail_idx - 1),
        this->f_lasti_addr_);
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<char>::get(this->context_), reason),
        FrameTy::f_bailed_from_llvm(this->builder_, this->frame_));
    this->builder_.CreateBr(this->bail_to_interpreter_block_);
}

void
LlvmFunctionBuilder::STORE_FAST(int index)
{
    this->SetLocal(index, this->Pop());
}

void
LlvmFunctionBuilder::DELETE_FAST(int index)
{
    BasicBlock *failure =
        this->CreateBasicBlock("DELETE_FAST_failure");
    BasicBlock *success =
        this->CreateBasicBlock("DELETE_FAST_success");
    Value *local_slot = this->locals_[index];
    Value *orig_value = this->builder_.CreateLoad(
        local_slot, "DELETE_FAST_old_reference");
    this->builder_.CreateCondBr(this->IsNull(orig_value), failure, success);

    this->builder_.SetInsertPoint(failure);
    Function *do_raise = this->GetGlobalFunction<
        void(PyFrameObject *, int)>("_PyEval_RaiseForUnboundLocal");
    this->CreateCall(
        do_raise, this->frame_,
        ConstantInt::getSigned(PyTypeBuilder<int>::get(this->context_), index));
    this->PropagateException();

    /* We clear both the LLVM-visible locals and the PyFrameObject's locals to
       make vars(), dir() and locals() happy. */
    this->builder_.SetInsertPoint(success);
    Value *frame_local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                            index));
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject *>::get(this->context_)),
        frame_local_slot);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject *>::get(this->context_)),
        local_slot);
    this->DecRef(orig_value);
}

void
LlvmFunctionBuilder::SETUP_LOOP(llvm::BasicBlock *target,
                                int target_opindex,
                                llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_LOOP, target, target_opindex);
}

void
LlvmFunctionBuilder::GET_ITER()
{
    Value *obj = this->Pop();
    Function *pyobject_getiter = this->GetGlobalFunction<PyObject*(PyObject*)>(
        "PyObject_GetIter");
    Value *iter = this->CreateCall(pyobject_getiter, obj);
    this->DecRef(obj);
    this->PropagateExceptionOnNull(iter);
    this->Push(iter);
}

void
LlvmFunctionBuilder::FOR_ITER(llvm::BasicBlock *target,
                              llvm::BasicBlock *fallthrough)
{
    Value *iter = this->Pop();
    Value *iter_tp = this->builder_.CreateBitCast(
        this->builder_.CreateLoad(
            ObjectTy::ob_type(this->builder_, iter)),
        PyTypeBuilder<PyTypeObject *>::get(this->context_),
        "iter_type");
    Value *iternext = this->builder_.CreateLoad(
        TypeTy::tp_iternext(this->builder_, iter_tp),
        "iternext");
    Value *next = this->CreateCall(iternext, iter, "next");
    BasicBlock *got_next = this->CreateBasicBlock("got_next");
    BasicBlock *next_null = this->CreateBasicBlock("next_null");
    this->builder_.CreateCondBr(this->IsNull(next), next_null, got_next);

    this->builder_.SetInsertPoint(next_null);
    Value *err_occurred = this->CreateCall(
        this->GetGlobalFunction<PyObject*()>("PyErr_Occurred"));
    BasicBlock *iter_ended = this->CreateBasicBlock("iter_ended");
    BasicBlock *exception = this->CreateBasicBlock("exception");
    this->builder_.CreateCondBr(this->IsNull(err_occurred),
                                iter_ended, exception);

    this->builder_.SetInsertPoint(exception);
    Value *exc_stopiteration = this->builder_.CreateLoad(
        this->GET_GLOBAL_VARIABLE(PyObject*, PyExc_StopIteration));
    Value *was_stopiteration = this->CreateCall(
        this->GetGlobalFunction<int(PyObject *)>("PyErr_ExceptionMatches"),
        exc_stopiteration);
    BasicBlock *clear_err = this->CreateBasicBlock("clear_err");
    BasicBlock *propagate = this->CreateBasicBlock("propagate");
    this->builder_.CreateCondBr(this->IsNonZero(was_stopiteration),
                                clear_err, propagate);

    this->builder_.SetInsertPoint(propagate);
    this->DecRef(iter);
    this->PropagateException();

    this->builder_.SetInsertPoint(clear_err);
    this->CreateCall(this->GetGlobalFunction<void()>("PyErr_Clear"));
    this->builder_.CreateBr(iter_ended);

    this->builder_.SetInsertPoint(iter_ended);
    this->DecRef(iter);
    this->builder_.CreateBr(target);

    this->builder_.SetInsertPoint(got_next);
    this->Push(iter);
    this->Push(next);
}

void
LlvmFunctionBuilder::POP_BLOCK()
{
    Value *block_info = this->CreateCall(
        this->GetGlobalFunction<PyTryBlock *(PyTryBlock *, char *)>(
            "_PyLlvm_Frame_BlockPop"),
        this->blockstack_addr_,
        this->num_blocks_addr_);
    Value *pop_to_level = this->builder_.CreateLoad(
        PyTypeBuilder<PyTryBlock>::b_level(this->builder_, block_info));
    Value *pop_to_addr =
        this->builder_.CreateGEP(this->stack_bottom_, pop_to_level);
    this->PopAndDecrefTo(pop_to_addr);
}

void
LlvmFunctionBuilder::SETUP_EXCEPT(llvm::BasicBlock *target,
                                  int target_opindex,
                                  llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_EXCEPT, target, target_opindex);
}

void
LlvmFunctionBuilder::SETUP_FINALLY(llvm::BasicBlock *target,
                                   int target_opindex,
                                   llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_FINALLY, target, target_opindex);
}

void
LlvmFunctionBuilder::END_FINALLY()
{
    Value *finally_discriminator = this->Pop();
    // END_FINALLY is fairly complicated. It decides what to do based
    // on the top value in the stack.  If that value is an int, it's
    // interpreted as one of the unwind reasons.  If it's an exception
    // type, the next two stack values are the rest of the exception,
    // and it's re-raised.  Otherwise, it's supposed to be None,
    // indicating that the finally was entered through normal control
    // flow.

    BasicBlock *unwind_code =
        this->CreateBasicBlock("unwind_code");
    BasicBlock *test_exception =
        this->CreateBasicBlock("test_exception");
    BasicBlock *reraise_exception =
        this->CreateBasicBlock("reraise_exception");
    BasicBlock *check_none = this->CreateBasicBlock("check_none");
    BasicBlock *not_none = this->CreateBasicBlock("not_none");
    BasicBlock *finally_fallthrough =
        this->CreateBasicBlock("finally_fallthrough");

    this->builder_.CreateCondBr(
        this->IsInstanceOfFlagClass(finally_discriminator,
                                    Py_TPFLAGS_INT_SUBCLASS),
        unwind_code, test_exception);

    this->builder_.SetInsertPoint(unwind_code);
    // The top of the stack was an int, interpreted as an unwind code.
    // If we're resuming a return or continue, the return value or
    // loop target (respectively) is now on top of the stack and needs
    // to be popped off.
    Value *unwind_reason = this->builder_.CreateTrunc(
        this->CreateCall(
            this->GetGlobalFunction<long(PyObject *)>("PyInt_AsLong"),
            finally_discriminator),
        Type::getInt8Ty(this->context_),
        "unwind_reason");
    this->DecRef(finally_discriminator);
    // Save the unwind reason for when we jump to the unwind block.
    this->builder_.CreateStore(unwind_reason, this->unwind_reason_addr_);
    // Check if we need to pop the return value or loop target.
    BasicBlock *pop_retval = this->CreateBasicBlock("pop_retval");
    llvm::SwitchInst *should_pop_retval =
        this->builder_.CreateSwitch(unwind_reason, this->unwind_block_, 2);
    should_pop_retval->addCase(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_RETURN),
                               pop_retval);
    should_pop_retval->addCase(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_CONTINUE),
                               pop_retval);

    this->builder_.SetInsertPoint(pop_retval);
    // We're continuing a return or continue.  Retrieve its argument.
    this->builder_.CreateStore(this->Pop(), this->retval_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    this->builder_.SetInsertPoint(test_exception);
    Value *is_exception_or_string = this->CreateCall(
        this->GetGlobalFunction<int(PyObject *)>(
            "_PyLlvm_WrapIsExceptionOrString"),
        finally_discriminator);
    this->builder_.CreateCondBr(this->IsNonZero(is_exception_or_string),
                                reraise_exception, check_none);

    this->builder_.SetInsertPoint(reraise_exception);
    Value *err_type = finally_discriminator;
    Value *err_value = this->Pop();
    Value *err_traceback = this->Pop();
    this->CreateCall(
        this->GetGlobalFunction<void(PyObject *, PyObject *, PyObject *)>(
            "PyErr_Restore"),
        err_type, err_value, err_traceback);
    // This is a "re-raise" rather than a new exception, so we don't
    // jump to the propagate_exception_block_.
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_)),
        this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_EXCEPTION),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    this->builder_.SetInsertPoint(check_none);
    // The contents of the try block push None onto the stack just
    // before falling through to the finally block.  If we didn't get
    // an unwind reason or an exception, we expect to fall through,
    // but for sanity we also double-check that the None is present.
    Value *is_none = this->builder_.CreateICmpEQ(
        finally_discriminator,
        this->GET_GLOBAL_VARIABLE(PyObject, _Py_NoneStruct));
    this->DecRef(finally_discriminator);
    this->builder_.CreateCondBr(is_none, finally_fallthrough, not_none);

    this->builder_.SetInsertPoint(not_none);
    // If we didn't get a None, raise a SystemError.
    Value *system_error = this->builder_.CreateLoad(
        this->GET_GLOBAL_VARIABLE(PyObject *, PyExc_SystemError));
    Value *err_msg =
        this->llvm_data_->GetGlobalStringPtr("'finally' pops bad exception");
    this->CreateCall(
        this->GetGlobalFunction<void(PyObject *, const char *)>(
            "PyErr_SetString"),
        system_error, err_msg);
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_EXCEPTION),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    // After falling through into a finally block, we also fall
    // through out of the block.  This has the nice side-effect of
    // avoiding jumps and switch instructions in the common case,
    // although returning out of a finally may still be slower than
    // ideal.
    this->builder_.SetInsertPoint(finally_fallthrough);
}

void
LlvmFunctionBuilder::CONTINUE_LOOP(llvm::BasicBlock *target,
                                   int target_opindex,
                                   llvm::BasicBlock *fallthrough)
{
    // Accept code after a continue statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = this->CreateBasicBlock("dead_code");
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_CONTINUE),
                               this->unwind_reason_addr_);
    Value *unwind_target = this->AddUnwindTarget(target, target_opindex);
    // Yes, store the unwind target in the return value slot. This is to
    // keep the translation from eval.cc as close as possible; deviation will
    // only introduce bugs. The UNWIND_CONTINUE cases in the unwind block
    // (see FillUnwindBlock()) will pick this up and deal with it.
    const Type *long_type = PyTypeBuilder<long>::get(this->context_);
    Value *pytarget = this->CreateCall(
            this->GetGlobalFunction<PyObject *(long)>("PyInt_FromLong"),
            this->builder_.CreateZExt(unwind_target, long_type));
    this->builder_.CreateStore(pytarget, this->retval_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::BREAK_LOOP()
{
    // Accept code after a break statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = this->CreateBasicBlock("dead_code");
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_BREAK),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::RETURN_VALUE()
{
    // Accept code after a return statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = this->CreateBasicBlock("dead_code");

    Value *retval = this->Pop();
    this->Return(retval);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::YIELD_VALUE()
{
    assert(this->is_generator_ && "yield in non-generator!");
    BasicBlock *yield_resume =
        this->CreateBasicBlock("yield_resume");
    // Save the current opcode index into f_lasti when we yield so
    // that, if tracing gets turned on while we're outside this
    // function we can jump back to the interpreter at the right
    // place.
    ConstantInt *yield_number =
        ConstantInt::getSigned(PyTypeBuilder<int>::get(this->context_),
                               this->f_lasti_);
    this->yield_resume_switch_->addCase(yield_number, yield_resume);

    Value *retval = this->Pop();

    // Save everything to the frame object so it'll be there when we
    // resume from the yield.
    this->CopyToFrameObject();

    // Save the right block to jump back to when we resume this generator.
    this->builder_.CreateStore(yield_number, this->f_lasti_addr_);

    // Yields return from the current function without unwinding the
    // stack.  They do trace the return and call _PyEval_ResetExcInfo
    // like everything else, so we jump to the common return block
    // instead of returning directly.
    this->builder_.CreateStore(retval, this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::getInt8Ty(this->context_),
                                                UNWIND_YIELD),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->do_return_block_);

    // Continue inserting code inside the resume block.
    this->builder_.SetInsertPoint(yield_resume);
    // Set frame->f_lasti back to negative so that exceptions are
    // generated with llvm-provided line numbers.
    this->builder_.CreateStore(
        ConstantInt::getSigned(PyTypeBuilder<int>::get(this->context_), -2),
        this->f_lasti_addr_);
}

void
LlvmFunctionBuilder::DoRaise(Value *exc_type, Value *exc_inst, Value *exc_tb)
{
    // Accept code after a raise statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = this->CreateBasicBlock("dead_code");

    // All raises set 'why' to UNWIND_EXCEPTION and the return value to NULL.
    // This is redundant with the propagate_exception_block_, but mem2reg will
    // remove the redundancy.
    this->builder_.CreateStore(
        ConstantInt::get(Type::getInt8Ty(this->context_), UNWIND_EXCEPTION),
        this->unwind_reason_addr_);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get(this->context_)),
        this->retval_addr_);

#ifdef WITH_TSC
    this->LogTscEvent(EXCEPT_RAISE_LLVM);
#endif
    Function *do_raise = this->GetGlobalFunction<
        int(PyObject*, PyObject *, PyObject *)>("_PyEval_DoRaise");
    // _PyEval_DoRaise eats references.
    Value *is_reraise = this->CreateCall(
        do_raise, exc_type, exc_inst, exc_tb, "raise_is_reraise");
    // If this is a "re-raise", we jump straight to the unwind block.
    // If it's a new raise, we call PyTraceBack_Here from the
    // propagate_exception_block_.
    this->builder_.CreateCondBr(
        this->builder_.CreateICmpEQ(
            is_reraise,
            ConstantInt::get(is_reraise->getType(), UNWIND_RERAISE)),
        this->unwind_block_, this->propagate_exception_block_);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_ZERO()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *exc_inst = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *exc_type = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_ONE()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *exc_inst = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *exc_type = this->Pop();
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_TWO()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *exc_inst = this->Pop();
    Value *exc_type = this->Pop();
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_THREE()
{
    Value *exc_tb = this->Pop();
    Value *exc_inst = this->Pop();
    Value *exc_type = this->Pop();
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::STORE_SUBSCR()
{
    // Performing obj[key] = val
    Value *key = this->Pop();
    Value *obj = this->Pop();
    Value *value = this->Pop();
    Function *setitem = this->GetGlobalFunction<
          int(PyObject *, PyObject *, PyObject *)>("PyObject_SetItem");
    Value *result = this->CreateCall(setitem, obj, key, value,
                                               "STORE_SUBSCR_result");
    this->DecRef(value);
    this->DecRef(obj);
    this->DecRef(key);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::DELETE_SUBSCR()
{
    Value *key = this->Pop();
    Value *obj = this->Pop();
    Function *delitem = this->GetGlobalFunction<
          int(PyObject *, PyObject *)>("PyObject_DelItem");
    Value *result = this->CreateCall(delitem, obj, key,
                                               "DELETE_SUBSCR_result");
    this->DecRef(obj);
    this->DecRef(key);
    this->PropagateExceptionOnNonZero(result);
}

// Common code for almost all binary operations
void
LlvmFunctionBuilder::GenericBinOp(const char *apifunc)
{
    Value *rhs = this->Pop();
    Value *lhs = this->Pop();
    Function *op =
        this->GetGlobalFunction<PyObject*(PyObject*, PyObject*)>(apifunc);
    Value *result = this->CreateCall(op, lhs, rhs, "binop_result");
    this->DecRef(lhs);
    this->DecRef(rhs);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

#define BINOP_METH(OPCODE, APIFUNC) 		\
void						\
LlvmFunctionBuilder::OPCODE()			\
{						\
    this->GenericBinOp(#APIFUNC);		\
}

BINOP_METH(BINARY_ADD, PyNumber_Add)
BINOP_METH(BINARY_SUBTRACT, PyNumber_Subtract)
BINOP_METH(BINARY_MULTIPLY, PyNumber_Multiply)
BINOP_METH(BINARY_TRUE_DIVIDE, PyNumber_TrueDivide)
BINOP_METH(BINARY_DIVIDE, PyNumber_Divide)
BINOP_METH(BINARY_MODULO, PyNumber_Remainder)
BINOP_METH(BINARY_LSHIFT, PyNumber_Lshift)
BINOP_METH(BINARY_RSHIFT, PyNumber_Rshift)
BINOP_METH(BINARY_OR, PyNumber_Or)
BINOP_METH(BINARY_XOR, PyNumber_Xor)
BINOP_METH(BINARY_AND, PyNumber_And)
BINOP_METH(BINARY_FLOOR_DIVIDE, PyNumber_FloorDivide)
BINOP_METH(BINARY_SUBSCR, PyObject_GetItem)

BINOP_METH(INPLACE_ADD, PyNumber_InPlaceAdd)
BINOP_METH(INPLACE_SUBTRACT, PyNumber_InPlaceSubtract)
BINOP_METH(INPLACE_MULTIPLY, PyNumber_InPlaceMultiply)
BINOP_METH(INPLACE_TRUE_DIVIDE, PyNumber_InPlaceTrueDivide)
BINOP_METH(INPLACE_DIVIDE, PyNumber_InPlaceDivide)
BINOP_METH(INPLACE_MODULO, PyNumber_InPlaceRemainder)
BINOP_METH(INPLACE_LSHIFT, PyNumber_InPlaceLshift)
BINOP_METH(INPLACE_RSHIFT, PyNumber_InPlaceRshift)
BINOP_METH(INPLACE_OR, PyNumber_InPlaceOr)
BINOP_METH(INPLACE_XOR, PyNumber_InPlaceXor)
BINOP_METH(INPLACE_AND, PyNumber_InPlaceAnd)
BINOP_METH(INPLACE_FLOOR_DIVIDE, PyNumber_InPlaceFloorDivide)

#undef BINOP_METH

// PyNumber_Power() and PyNumber_InPlacePower() take three arguments, the
// third should be Py_None when calling from BINARY_POWER/INPLACE_POWER.
void
LlvmFunctionBuilder::GenericPowOp(const char *apifunc)
{
    Value *rhs = this->Pop();
    Value *lhs = this->Pop();
    Function *op = this->GetGlobalFunction<PyObject*(PyObject*, PyObject*,
        PyObject *)>(apifunc);
    Value *pynone = this->GET_GLOBAL_VARIABLE(PyObject, _Py_NoneStruct);
    Value *result = this->CreateCall(op, lhs, rhs, pynone,
                                               "powop_result");
    this->DecRef(lhs);
    this->DecRef(rhs);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::BINARY_POWER()
{
    this->GenericPowOp("PyNumber_Power");
}

void
LlvmFunctionBuilder::INPLACE_POWER()
{
    this->GenericPowOp("PyNumber_InPlacePower");
}

// Implementation of almost all unary operations
void
LlvmFunctionBuilder::GenericUnaryOp(const char *apifunc)
{
    Value *value = this->Pop();
    Function *op = this->GetGlobalFunction<PyObject*(PyObject*)>(apifunc);
    Value *result = this->CreateCall(op, value, "unaryop_result");
    this->DecRef(value);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

#define UNARYOP_METH(NAME, APIFUNC)			\
void							\
LlvmFunctionBuilder::NAME()				\
{							\
    this->GenericUnaryOp(#APIFUNC);			\
}

UNARYOP_METH(UNARY_CONVERT, PyObject_Repr)
UNARYOP_METH(UNARY_INVERT, PyNumber_Invert)
UNARYOP_METH(UNARY_POSITIVE, PyNumber_Positive)
UNARYOP_METH(UNARY_NEGATIVE, PyNumber_Negative)

#undef UNARYOP_METH

void
LlvmFunctionBuilder::UNARY_NOT()
{
    Value *value = this->Pop();
    Value *retval = this->builder_.CreateSelect(
        this->IsPythonTrue(value),
        this->GET_GLOBAL_VARIABLE(PyObject, _Py_ZeroStruct),
        this->GET_GLOBAL_VARIABLE(PyObject, _Py_TrueStruct),
        "UNARY_NOT_result");
    this->IncRef(retval);
    this->Push(retval);
}

void
LlvmFunctionBuilder::POP_TOP()
{
    this->DecRef(this->Pop());
}

void
LlvmFunctionBuilder::DUP_TOP()
{
    Value *first = this->Pop();
    this->IncRef(first);
    this->Push(first);
    this->Push(first);
}

void
LlvmFunctionBuilder::DUP_TOP_TWO()
{
    Value *first = this->Pop();
    Value *second = this->Pop();
    this->IncRef(first);
    this->IncRef(second);
    this->Push(second);
    this->Push(first);
    this->Push(second);
    this->Push(first);
}

void
LlvmFunctionBuilder::DUP_TOP_THREE()
{
    Value *first = this->Pop();
    Value *second = this->Pop();
    Value *third = this->Pop();
    this->IncRef(first);
    this->IncRef(second);
    this->IncRef(third);
    this->Push(third);
    this->Push(second);
    this->Push(first);
    this->Push(third);
    this->Push(second);
    this->Push(first);
}

void
LlvmFunctionBuilder::ROT_TWO()
{
    Value *first = this->Pop();
    Value *second = this->Pop();
    this->Push(first);
    this->Push(second);
}

void
LlvmFunctionBuilder::ROT_THREE()
{
    Value *first = this->Pop();
    Value *second = this->Pop();
    Value *third = this->Pop();
    this->Push(first);
    this->Push(third);
    this->Push(second);
}

void
LlvmFunctionBuilder::ROT_FOUR()
{
    Value *first = this->Pop();
    Value *second = this->Pop();
    Value *third = this->Pop();
    Value *fourth = this->Pop();
    this->Push(first);
    this->Push(fourth);
    this->Push(third);
    this->Push(second);
}

void
LlvmFunctionBuilder::RichCompare(Value *lhs, Value *rhs, int cmp_op)
{
    Function *pyobject_richcompare = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *, int)>("PyObject_RichCompare");
    Value *result = this->CreateCall(
        pyobject_richcompare, lhs, rhs,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), cmp_op),
        "COMPARE_OP_RichCompare_result");
    this->DecRef(lhs);
    this->DecRef(rhs);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

Value *
LlvmFunctionBuilder::ContainerContains(Value *container, Value *item)
{
    Function *contains =
        this->GetGlobalFunction<int(PyObject *, PyObject *)>(
            "PySequence_Contains");
    Value *result = this->CreateCall(
        contains, container, item, "ContainerContains_result");
    this->DecRef(item);
    this->DecRef(container);
    this->PropagateExceptionOnNegative(result);
    return this->IsPositive(result);
}

// TODO(twouters): test this (used in exception handling.)
Value *
LlvmFunctionBuilder::ExceptionMatches(Value *exc, Value *exc_type)
{
    Function *exc_matches = this->GetGlobalFunction<
        int(PyObject *, PyObject *)>("_PyEval_CheckedExceptionMatches");
    Value *result = this->CreateCall(
        exc_matches, exc, exc_type, "ExceptionMatches_result");
    this->DecRef(exc_type);
    this->DecRef(exc);
    this->PropagateExceptionOnNegative(result);
    return this->IsPositive(result);
}

void
LlvmFunctionBuilder::COMPARE_OP(int cmp_op)
{
    Value *rhs = this->Pop();
    Value *lhs = this->Pop();
    Value *result;
    switch (cmp_op) {
    case PyCmp_IS:
        result = this->builder_.CreateICmpEQ(lhs, rhs, "COMPARE_OP_is_same");
        this->DecRef(lhs);
        this->DecRef(rhs);
        break;
    case PyCmp_IS_NOT:
        result = this->builder_.CreateICmpNE(lhs, rhs,
                                             "COMPARE_OP_is_not_same");
        this->DecRef(lhs);
        this->DecRef(rhs);
        break;
    case PyCmp_IN:
        // item in seq -> ContainerContains(seq, item)
        result = this->ContainerContains(rhs, lhs);
        break;
    case PyCmp_NOT_IN:
    {
        Value *inverted_result = this->ContainerContains(rhs, lhs);
        result = this->builder_.CreateICmpEQ(
            inverted_result, ConstantInt::get(inverted_result->getType(), 0),
            "COMPARE_OP_not_in_result");
        break;
    }
    case PyCmp_EXC_MATCH:
        result = this->ExceptionMatches(lhs, rhs);
        break;
    case PyCmp_EQ:
    case PyCmp_NE:
    case PyCmp_LT:
    case PyCmp_LE:
    case PyCmp_GT:
    case PyCmp_GE:
        this->RichCompare(lhs, rhs, cmp_op);
        return;
    default:
        Py_FatalError("unknown COMPARE_OP oparg");
        return;  // Not reached.
    }
    Value *value = this->builder_.CreateSelect(
        result,
        this->GET_GLOBAL_VARIABLE(PyObject, _Py_TrueStruct),
        this->GET_GLOBAL_VARIABLE(PyObject, _Py_ZeroStruct),
        "COMPARE_OP_result");
    this->IncRef(value);
    this->Push(value);
}

void
LlvmFunctionBuilder::LIST_APPEND()
{
    Value *item = this->Pop();
    Value *listobj = this->Pop();
    Function *list_append = this->GetGlobalFunction<
        int(PyObject *, PyObject *)>("PyList_Append");
    Value *result = this->CreateCall(list_append, listobj, item,
                                               "LIST_APPEND_result");
    this->DecRef(listobj);
    this->DecRef(item);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::STORE_MAP()
{
    Value *key = this->Pop();
    Value *value = this->Pop();
    Value *dict = this->Pop();
    this->Push(dict);
    Value *dict_type = this->builder_.CreateLoad(
        ObjectTy::ob_type(this->builder_, dict));
    Value *is_exact_dict = this->builder_.CreateICmpEQ(
        dict_type, this->GET_GLOBAL_VARIABLE(PyTypeObject, PyDict_Type));
    this->Assert(is_exact_dict,
                 "dict argument to STORE_MAP is not exactly a PyDict");
    Function *setitem = this->GetGlobalFunction<
        int(PyObject *, PyObject *, PyObject *)>("PyDict_SetItem");
    Value *result = this->CreateCall(setitem, dict, key, value,
                                               "STORE_MAP_result");
    this->DecRef(value);
    this->DecRef(key);
    this->PropagateExceptionOnNonZero(result);
}

Value *
LlvmFunctionBuilder::GetListItemSlot(Value *lst, int idx)
{
    Value *listobj = this->builder_.CreateBitCast(
        lst, PyTypeBuilder<PyListObject *>::get(this->context_));
    // Load the target of the ob_item PyObject** into list_items.
    Value *list_items = this->builder_.CreateLoad(
        ListTy::ob_item(this->builder_, listobj));
    // GEP the list_items PyObject* up to the desired item
    const Type *int_type = Type::getInt32Ty(this->context_);
    return this->builder_.CreateGEP(list_items,
                                    ConstantInt::get(int_type, idx),
                                    "list_item_slot");
}

Value *
LlvmFunctionBuilder::GetTupleItemSlot(Value *tup, int idx)
{
    Value *tupobj = this->builder_.CreateBitCast(
        tup, PyTypeBuilder<PyTupleObject*>::get(this->context_));
    // Make CreateGEP perform &tup_item_indices[0].ob_item[idx].
    Value *tuple_items = TupleTy::ob_item(this->builder_, tupobj);
    return this->builder_.CreateStructGEP(tuple_items, idx,
                                          "tuple_item_slot");
}

void
LlvmFunctionBuilder::BuildSequenceLiteral(
    int size, const char *createname,
    Value *(LlvmFunctionBuilder::*getitemslot)(Value*, int))
{
    const Type *IntSsizeTy = PyTypeBuilder<Py_ssize_t>::get(this->context_);
    Value *seqsize = ConstantInt::getSigned(IntSsizeTy, size);

    Function *create =
        this->GetGlobalFunction<PyObject *(Py_ssize_t)>(createname);
    Value *seq = this->CreateCall(create, seqsize, "sequence_literal");
    this->PropagateExceptionOnNull(seq);

    // XXX(twouters): do this with a memcpy?
    while (--size >= 0) {
        Value *itemslot = (this->*getitemslot)(seq, size);
        this->builder_.CreateStore(this->Pop(), itemslot);
    }
    this->Push(seq);
}

void
LlvmFunctionBuilder::BUILD_LIST(int size)
{
   this->BuildSequenceLiteral(size, "PyList_New",
                              &LlvmFunctionBuilder::GetListItemSlot);
}

void
LlvmFunctionBuilder::BUILD_TUPLE(int size)
{
   this->BuildSequenceLiteral(size, "PyTuple_New",
                              &LlvmFunctionBuilder::GetTupleItemSlot);
}

void
LlvmFunctionBuilder::BUILD_MAP(int size)
{
    Value *sizehint = ConstantInt::getSigned(
        PyTypeBuilder<Py_ssize_t>::get(this->context_), size);
    Function *create_dict = this->GetGlobalFunction<
        PyObject *(Py_ssize_t)>("_PyDict_NewPresized");
    Value *result = this->CreateCall(create_dict, sizehint,
                                              "BULD_MAP_result");
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::ApplySlice(Value *seq, Value *start, Value *stop)
{
    Function *build_slice = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *, PyObject *)>("_PyEval_ApplySlice");
    Value *result = this->CreateCall(
        build_slice, seq, start, stop, "ApplySlice_result");
    this->XDecRef(stop);
    this->XDecRef(start);
    this->DecRef(seq);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::SLICE_BOTH()
{
    Value *stop = this->Pop();
    Value *start = this->Pop();
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::SLICE_LEFT()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = this->Pop();
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::SLICE_RIGHT()
{
    Value *stop = this->Pop();
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::AssignSlice(Value *seq, Value *start, Value *stop,
                                 Value *source)
{
    Function *assign_slice = this->GetGlobalFunction<
        int (PyObject *, PyObject *, PyObject *, PyObject *)>(
            "_PyEval_AssignSlice");
    Value *result = this->CreateCall(
        assign_slice, seq, start, stop, source, "ApplySlice_result");
    this->XDecRef(source);
    this->XDecRef(stop);
    this->XDecRef(start);
    this->DecRef(seq);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::STORE_SLICE_BOTH()
{
    Value *stop = this->Pop();
    Value *start = this->Pop();
    Value *seq = this->Pop();
    Value *source = this->Pop();
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::STORE_SLICE_LEFT()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = this->Pop();
    Value *seq = this->Pop();
    Value *source = this->Pop();
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::STORE_SLICE_RIGHT()
{
    Value *stop = this->Pop();
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    Value *source = this->Pop();
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::STORE_SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    Value *source = this->Pop();
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_BOTH()
{
    Value *stop = this->Pop();
    Value *start = this->Pop();
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_LEFT()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = this->Pop();
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_RIGHT()
{
    Value *stop = this->Pop();
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::BUILD_SLICE_TWO()
{
    Value *step = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get(this->context_));
    Value *stop = this->Pop();
    Value *start = this->Pop();
    Function *build_slice = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *, PyObject *)>("PySlice_New");
    Value *result = this->CreateCall(
        build_slice, start, stop, step, "BUILD_SLICE_result");
    this->DecRef(start);
    this->DecRef(stop);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::BUILD_SLICE_THREE()
{
    Value *step = this->Pop();
    Value *stop = this->Pop();
    Value *start = this->Pop();
    Function *build_slice = this->GetGlobalFunction<
        PyObject *(PyObject *, PyObject *, PyObject *)>("PySlice_New");
    Value *result = this->CreateCall(
        build_slice, start, stop, step, "BUILD_SLICE_result");
    this->DecRef(start);
    this->DecRef(stop);
    this->DecRef(step);
    this->PropagateExceptionOnNull(result);
    this->Push(result);
}

void
LlvmFunctionBuilder::UNPACK_SEQUENCE(int size)
{
    // TODO(twouters): We could do even better by combining this opcode and the
    // STORE_* ones that follow into a single block of code circumventing the
    // stack altogether. And omitting the horrible external stack munging that
    // UnpackIterable does.
    Value *iterable = this->Pop();
    Function *unpack_iterable = this->GetGlobalFunction<
        int(PyObject *, int, PyObject **)>("_PyLlvm_FastUnpackIterable");
    Value *new_stack_pointer = this->builder_.CreateGEP(
        this->builder_.CreateLoad(this->stack_pointer_addr_),
        ConstantInt::getSigned(PyTypeBuilder<Py_ssize_t>::get(this->context_),
                               size));
    Value *result = this->CreateCall(
        unpack_iterable, iterable,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), size, true),
        // _PyLlvm_FastUnpackIterable really takes the *new* stack pointer as
        // an argument, because it builds the result stack in reverse.
        new_stack_pointer);
    this->DecRef(iterable);
    this->PropagateExceptionOnNonZero(result);
    // Not setting the new stackpointer on failure does mean that if
    // _PyLlvm_FastUnpackIterable failed after pushing some values onto the
    // stack, and it didn't clean up after itself, we lose references.  This
    // is what eval.cc does as well.
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
}

void
LlvmFunctionBuilder::IncRef(Value *value)
{
    Function *incref = this->GetGlobalFunction<void(PyObject*)>(
        "_PyLlvm_WrapIncref");
    this->CreateCall(incref, value);
}

void
LlvmFunctionBuilder::DecRef(Value *value)
{
    Function *decref = this->GetGlobalFunction<void(PyObject*)>(
        "_PyLlvm_WrapDecref");
    this->CreateCall(decref, value);
}

void
LlvmFunctionBuilder::XDecRef(Value *value)
{
    Function *xdecref = this->GetGlobalFunction<void(PyObject*)>(
        "_PyLlvm_WrapXDecref");
    this->CreateCall(xdecref, value);
}

void
LlvmFunctionBuilder::Push(Value *value)
{
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    this->builder_.CreateStore(value, stack_pointer);
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer, ConstantInt::get(Type::getInt32Ty(this->context_), 1));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
}

Value *
LlvmFunctionBuilder::Pop()
{
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer, ConstantInt::getSigned(Type::getInt32Ty(this->context_),
                                              -1));
    Value *former_top = this->builder_.CreateLoad(new_stack_pointer);
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    return former_top;
}

Value *
LlvmFunctionBuilder::GetStackLevel()
{
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *stack_pointer_int =
        this->builder_.CreatePtrToInt(stack_pointer,
                                      Type::getInt64Ty(this->context_));
    Value *stack_bottom_int =
        this->builder_.CreatePtrToInt(this->stack_bottom_,
                                      Type::getInt64Ty(this->context_));
    Value *difference =
        this->builder_.CreateSub(stack_pointer_int, stack_bottom_int);
    Value *level64 = this->builder_.CreateSDiv(
        difference,
        llvm::ConstantExpr::getSizeOf(
            PyTypeBuilder<PyObject*>::get(this->context_)));
    // The stack level is stored as an int, not an int64.
    return this->builder_.CreateTrunc(level64,
                                      PyTypeBuilder<int>::get(this->context_),
                                      "stack_level");
}

void
LlvmFunctionBuilder::SetLocal(int locals_index, llvm::Value *new_value)
{
    // We write changes twice: once to our LLVM-visible locals, and again to the
    // PyFrameObject. This makes vars(), locals() and dir() happy.
    Value *frame_local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                            locals_index));
    this->builder_.CreateStore(new_value, frame_local_slot);

    Value *llvm_local_slot = this->locals_[locals_index];
    Value *orig_value =
        this->builder_.CreateLoad(llvm_local_slot, "llvm_local_overwritten");
    this->builder_.CreateStore(new_value, llvm_local_slot);
    this->XDecRef(orig_value);
}

void
LlvmFunctionBuilder::CallBlockSetup(int block_type, llvm::BasicBlock *handler,
                                    int handler_opindex)
{
    Value *stack_level = this->GetStackLevel();
    Value *unwind_target_index =
        this->AddUnwindTarget(handler, handler_opindex);
    Function *blocksetup =
        this->GetGlobalFunction<void(PyTryBlock *, char *, int, int, int)>(
            "_PyLlvm_Frame_BlockSetup");
    Value *args[] = {
        this->blockstack_addr_, this->num_blocks_addr_,
        ConstantInt::get(PyTypeBuilder<int>::get(this->context_), block_type),
        unwind_target_index,
        stack_level
    };
    this->CreateCall(blocksetup, args, array_endof(args));
}

void
LlvmFunctionBuilder::CheckPyTicker(BasicBlock *next_block)
{
    if (next_block == NULL) {
        next_block = this->CreateBasicBlock("ticker_dec_end");
    }
    Value *pyticker_result = this->builder_.CreateCall(
        this->GetGlobalFunction<int(PyThreadState*)>(
            "_PyLlvm_DecAndCheckPyTicker"),
        this->tstate_);
    this->builder_.CreateCondBr(this->IsNegative(pyticker_result),
                                this->propagate_exception_block_,
                                next_block);
    this->builder_.SetInsertPoint(next_block);
}

void
LlvmFunctionBuilder::DieForUndefinedOpcode(const char *opcode_name)
{
    std::string message("Undefined opcode: ");
    message.append(opcode_name);
    this->Abort(message);
}

void
LlvmFunctionBuilder::Assert(llvm::Value *should_be_true,
                            const std::string &failure_message)
{
#ifndef NDEBUG
    BasicBlock *assert_passed =
            this->CreateBasicBlock(failure_message + "_assert_passed");
    BasicBlock *assert_failed =
            this->CreateBasicBlock(failure_message + "_assert_failed");
    this->builder_.CreateCondBr(should_be_true, assert_passed, assert_failed);

    this->builder_.SetInsertPoint(assert_failed);
    this->Abort(failure_message);
    this->builder_.CreateUnreachable();

    this->builder_.SetInsertPoint(assert_passed);
#endif
}

void
LlvmFunctionBuilder::Abort(const std::string &failure_message)
{
    this->CreateCall(
        GetGlobalFunction<int(const char*)>("puts"),
        this->llvm_data_->GetGlobalStringPtr(failure_message));
    this->CreateCall(GetGlobalFunction<void()>("abort"));
}

template<typename FunctionType> Function *
LlvmFunctionBuilder::GetGlobalFunction(const std::string &name)
{
    return llvm::cast<Function>(
        this->module_->getOrInsertFunction(
            name, PyTypeBuilder<FunctionType>::get(this->context_)));
}

template<typename VariableType> Constant *
LlvmFunctionBuilder::GetGlobalVariable(void *var_address,
                                       const std::string &name)
{
    const Type *expected_type =
        PyTypeBuilder<VariableType>::get(this->context_);
    if (GlobalVariable *global = this->module_->getNamedGlobal(name)) {
        assert (expected_type == global->getType()->getElementType());
        return global;
    }
    if (llvm::GlobalValue *global =
        const_cast<llvm::GlobalValue*>(this->llvm_data_->getExecutionEngine()->
                                       getGlobalValueAtAddress(var_address))) {
        assert (expected_type == global->getType()->getElementType());
        if (!global->hasName())
            global->setName(name);
        return global;
    }
    return new GlobalVariable(
        *this->module_, expected_type, /*isConstant=*/false,
        llvm::GlobalValue::ExternalLinkage, NULL, name);
}

Constant *
LlvmFunctionBuilder::GetGlobalVariableFor(PyObject *obj)
{
    return this->llvm_data_->constant_mirror().GetGlobalVariableFor(obj);
}

// For llvm::Functions, copy callee's calling convention and attributes to
// callsite; for non-Functions, leave the default calling convention and
// attributes in place (ie, do nothing). We require this for function pointers.
static llvm::CallInst *
TransferAttributes(llvm::CallInst *callsite, const llvm::Value* callee)
{
    if (const llvm::GlobalAlias *alias =
            llvm::dyn_cast<llvm::GlobalAlias>(callee))
        callee = alias->getAliasedGlobal();

    if (const llvm::Function *func = llvm::dyn_cast<llvm::Function>(callee)) {
        callsite->setCallingConv(func->getCallingConv());
        callsite->setAttributes(func->getAttributes());
    }
    return callsite;
}

llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall(callee, name);
    return TransferAttributes(call, callee);
}

llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, llvm::Value *arg1,
                                const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall(callee, arg1, name);
    return TransferAttributes(call, callee);
}

llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, llvm::Value *arg1,
                                llvm::Value *arg2, const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall2(callee, arg1, arg2, name);
    return TransferAttributes(call, callee);
}

llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, llvm::Value *arg1,
                                llvm::Value *arg2, llvm::Value *arg3,
                                const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall3(callee, arg1, arg2, arg3,
                                                      name);
    return TransferAttributes(call, callee);
}

llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, llvm::Value *arg1,
                                llvm::Value *arg2, llvm::Value *arg3,
                                llvm::Value *arg4, const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall4(callee, arg1, arg2, arg3,
                                                      arg4, name);
    return TransferAttributes(call, callee);
}

template<typename InputIterator> llvm::CallInst *
LlvmFunctionBuilder::CreateCall(llvm::Value *callee, InputIterator begin,
                                InputIterator end, const char *name)
{
    llvm::CallInst *call = this->builder_.CreateCall(callee, begin, end, name);
    return TransferAttributes(call, callee);
}

llvm::ReturnInst *
LlvmFunctionBuilder::CreateRet(llvm::Value *retval)
{
    if (this->debug_info_ != NULL)
        this->debug_info_->InsertRegionEnd(debug_subprogram_,
                                           this->builder_.GetInsertBlock());
    return this->builder_.CreateRet(retval);
}

llvm::BasicBlock *
LlvmFunctionBuilder::CreateBasicBlock(const llvm::Twine &name)
{
    return BasicBlock::Create(this->context_, name, this->function_);
}

Value *
LlvmFunctionBuilder::IsNull(Value *value)
{
    return this->builder_.CreateICmpEQ(
        value, Constant::getNullValue(value->getType()));
}

Value *
LlvmFunctionBuilder::IsNonZero(Value *value)
{
    return this->builder_.CreateICmpNE(
        value, Constant::getNullValue(value->getType()));
}

Value *
LlvmFunctionBuilder::IsNegative(Value *value)
{
    return this->builder_.CreateICmpSLT(
        value, ConstantInt::getSigned(value->getType(), 0));
}

Value *
LlvmFunctionBuilder::IsPositive(Value *value)
{
    return this->builder_.CreateICmpSGT(
        value, ConstantInt::getSigned(value->getType(), 0));
}

Value *
LlvmFunctionBuilder::IsInstanceOfFlagClass(llvm::Value *value, int flag)
{
    Value *type = this->builder_.CreateBitCast(
        this->builder_.CreateLoad(
            ObjectTy::ob_type(this->builder_, value),
            "type"),
        PyTypeBuilder<PyTypeObject *>::get(this->context_));
    Value *type_flags = this->builder_.CreateLoad(
        TypeTy::tp_flags(this->builder_, type),
        "type_flags");
    Value *is_instance = this->builder_.CreateAnd(
        type_flags,
        ConstantInt::get(type_flags->getType(), flag));
    return this->IsNonZero(is_instance);
}

void
LlvmFunctionBuilder::PropagateExceptionOnNull(Value *value)
{
    BasicBlock *propagate =
        this->CreateBasicBlock("PropagateExceptionOnNull_propagate");
    BasicBlock *pass =
        this->CreateBasicBlock("PropagateExceptionOnNull_pass");
    this->builder_.CreateCondBr(this->IsNull(value), propagate, pass);

    this->builder_.SetInsertPoint(propagate);
    this->PropagateException();

    this->builder_.SetInsertPoint(pass);
}

void
LlvmFunctionBuilder::PropagateExceptionOnNegative(Value *value)
{
    BasicBlock *propagate =
        this->CreateBasicBlock("PropagateExceptionOnNegative_propagate");
    BasicBlock *pass =
        this->CreateBasicBlock("PropagateExceptionOnNegative_pass");
    this->builder_.CreateCondBr(this->IsNegative(value), propagate, pass);

    this->builder_.SetInsertPoint(propagate);
    this->PropagateException();

    this->builder_.SetInsertPoint(pass);
}

void
LlvmFunctionBuilder::PropagateExceptionOnNonZero(Value *value)
{
    BasicBlock *propagate =
        this->CreateBasicBlock("PropagateExceptionOnNonZero_propagate");
    BasicBlock *pass =
        this->CreateBasicBlock("PropagateExceptionOnNonZero_pass");
    this->builder_.CreateCondBr(this->IsNonZero(value), propagate, pass);

    this->builder_.SetInsertPoint(propagate);
    this->PropagateException();

    this->builder_.SetInsertPoint(pass);
}

Value *
LlvmFunctionBuilder::LookupName(int name_index)
{
    Value *name = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            this->names_, ConstantInt::get(Type::getInt32Ty(this->context_),
                                           name_index),
            "constant_name"));
    return name;
}

llvm::Value *
LlvmFunctionBuilder::IsPythonTrue(Value *value)
{
    BasicBlock *not_py_true =
        this->CreateBasicBlock("IsPythonTrue_is_not_PyTrue");
    BasicBlock *not_py_false =
        this->CreateBasicBlock("IsPythonTrue_is_not_PyFalse");
    BasicBlock *decref_value =
        this->CreateBasicBlock("IsPythonTrue_decref_value");
    BasicBlock *done =
        this->CreateBasicBlock("IsPythonTrue_done");

    Value *result_addr = this->CreateAllocaInEntryBlock(
        Type::getInt1Ty(this->context_), NULL, "IsPythonTrue_result");
    Value *py_false = this->GET_GLOBAL_VARIABLE(PyObject, _Py_ZeroStruct);
    Value *py_true = this->GET_GLOBAL_VARIABLE(PyObject, _Py_TrueStruct);

    Value *is_PyTrue = this->builder_.CreateICmpEQ(
        py_true, value, "IsPythonTrue_is_PyTrue");
    this->builder_.CreateStore(is_PyTrue, result_addr);
    this->builder_.CreateCondBr(is_PyTrue, decref_value, not_py_true);

    this->builder_.SetInsertPoint(not_py_true);
    Value *is_not_PyFalse = this->builder_.CreateICmpNE(
        py_false, value, "IsPythonTrue_is_PyFalse");
    this->builder_.CreateStore(is_not_PyFalse, result_addr);
    this->builder_.CreateCondBr(is_not_PyFalse, not_py_false, decref_value);

    this->builder_.SetInsertPoint(not_py_false);
    Function *pyobject_istrue =
        this->GetGlobalFunction<int(PyObject *)>("PyObject_IsTrue");
    Value *istrue_result = this->CreateCall(
        pyobject_istrue, value, "PyObject_IsTrue_result");
    this->DecRef(value);
    this->PropagateExceptionOnNegative(istrue_result);
    this->builder_.CreateStore(
        this->IsPositive(istrue_result),
        result_addr);
    this->builder_.CreateBr(done);

    this->builder_.SetInsertPoint(decref_value);
    this->DecRef(value);
    this->builder_.CreateBr(done);

    this->builder_.SetInsertPoint(done);
    return this->builder_.CreateLoad(result_addr);
}

}  // namespace py
