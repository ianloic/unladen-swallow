#include "Python/llvm_fbuilder.h"

#include "Python.h"
#include "code.h"
#include "opcode.h"
#include "frameobject.h"

#include "Python/global_llvm_data.h"

#include "Util/EventTimer.h"
#include "Util/PyTypeBuilder.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/BasicBlock.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/GlobalAlias.h"
#include "llvm/Instructions.h"
#include "llvm/Intrinsics.h"
#include "llvm/Module.h"
#include "llvm/Type.h"

#include <vector>

struct PyExcInfo;

namespace Intrinsic = llvm::Intrinsic;
using llvm::BasicBlock;
using llvm::CallInst;
using llvm::Constant;
using llvm::ConstantExpr;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::Module;
using llvm::Type;
using llvm::Value;
using llvm::array_endof;

namespace py {

// These have the same meanings as the why_code enum in eval.cc.
enum UnwindReason {
    UNWIND_NOUNWIND,
    UNWIND_EXCEPTION,
    UNWIND_RETURN,
    UNWIND_BREAK,
    UNWIND_CONTINUE,
    UNWIND_YIELD
};

static std::string
pystring_to_std_string(PyObject *str)
{
    assert(PyString_Check(str) && "code->co_name must be PyString");
    return std::string(PyString_AS_STRING(str), PyString_GET_SIZE(str));
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

    result = PyTypeBuilder<PyObject*(PyFrameObject*)>::get();
    module->addTypeName(function_type_name, result);
    return result;
}

LlvmFunctionBuilder::LlvmFunctionBuilder(
    PyGlobalLlvmData *llvm_data, PyCodeObject *code_object)
    : llvm_data_(llvm_data),
      code_object_(code_object),
      module_(this->llvm_data_->module()),
      function_(Function::Create(
                    get_function_type(this->module_),
                    llvm::GlobalValue::ExternalLinkage,
                    // Prefix names with #u# to avoid collisions
                    // with runtime functions.
                    "#u#" + pystring_to_std_string(code_object->co_name),
                    this->module_)),
      builder_(llvm_data->context()),
      is_generator_(code_object->co_flags & CO_GENERATOR)
{
    Function::arg_iterator args = this->function_->arg_begin();
    this->frame_ = args++;
    assert(args == this->function_->arg_end() &&
           "Unexpected number of arguments");
    this->frame_->setName("frame");

    BasicBlock *entry = BasicBlock::Create("entry", this->function_);
    this->unreachable_block_ =
        BasicBlock::Create("unreachable", this->function_);
    this->bail_to_interpreter_block_ =
        BasicBlock::Create("bail_to_interpreter", this->function_);
    this->propagate_exception_block_ =
        BasicBlock::Create("propagate_exception", this->function_);
    this->unwind_block_ = BasicBlock::Create("unwind_block", this->function_);
    this->do_return_block_ = BasicBlock::Create("do_return", this->function_);

    this->builder_.SetInsertPoint(entry);
    // CreateAllocaInEntryBlock will insert alloca's here, before
    // any other instructions in the 'entry' block.

    this->stack_pointer_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject**>::get(),
        NULL, "stack_pointer_addr");
    this->tmp_stack_pointer_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject**>::get(),
        NULL, "tmp_stack_pointer_addr");
    this->retval_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyObject*>::get(),
        NULL, "retval_addr");
    this->unwind_reason_addr_ = this->builder_.CreateAlloca(
        Type::Int8Ty, NULL, "unwind_reason_addr");
    this->unwind_target_index_addr_ = this->builder_.CreateAlloca(
        Type::Int32Ty, NULL, "unwind_target_index_addr");
    this->blockstack_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<PyTryBlock>::get(),
        ConstantInt::get(Type::Int32Ty, CO_MAXBLOCKS),
        "blockstack_addr");
    this->num_blocks_addr_ = this->builder_.CreateAlloca(
        PyTypeBuilder<char>::get(), NULL, "num_blocks_addr");

    this->tstate_ = this->CreateCall(
        this->GetGlobalFunction<PyThreadState*()>(
            "_PyLlvm_WrapPyThreadState_GET"));
    this->stack_bottom_ = this->builder_.CreateLoad(
        FrameTy::f_valuestack(this->builder_, this->frame_),
        "stack_bottom");
    if (this->is_generator_) {
        // When we're re-entering a generator, we have to copy the stack
        // pointer and block stack from the frame.
        this->CopyFromFrameObject();
    } else {
        // If this isn't a generator, the stack pointer always starts at
        // the bottom of the stack.
        this->builder_.CreateStore(this->stack_bottom_,
                                   this->stack_pointer_addr_);
        /* f_stacktop remains NULL unless yield suspends the frame. */
        this->builder_.CreateStore(
            Constant::getNullValue(PyTypeBuilder<PyObject **>::get()),
            FrameTy::f_stacktop(this->builder_, this->frame_));

        this->builder_.CreateStore(
            ConstantInt::get(PyTypeBuilder<char>::get(), 0),
            this->num_blocks_addr_);
    }

    Value *use_tracing = this->builder_.CreateLoad(
        ThreadStateTy::use_tracing(this->builder_, this->tstate_),
        "use_tracing");
    BasicBlock *trace_enter_function =
        BasicBlock::Create("trace_enter_function", this->function_);
    BasicBlock *continue_entry =
        BasicBlock::Create("continue_entry", this->function_);
    this->builder_.CreateCondBr(this->IsNonZero(use_tracing),
                                trace_enter_function, continue_entry);

    this->builder_.SetInsertPoint(trace_enter_function);
    // Don't touch f_lasti since we just entered the function..
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<char>::get(), _PYFRAME_TRACE_ON_ENTRY),
        FrameTy::f_bailed_from_llvm(this->builder_, this->frame_));
    this->builder_.CreateBr(this->bail_to_interpreter_block_);

    this->builder_.SetInsertPoint(continue_entry);
    Value *code = this->builder_.CreateLoad(
        FrameTy::f_code(this->builder_, this->frame_),
        "co");
#ifndef NDEBUG
    // Assert that the code object we pull out of the frame is the
    // same as the one passed into this object.  TODO(jyasskin):
    // Create an LLVM constant GlobalVariable to store the passed-in
    // code object instead of pulling it out of the frame.  We'll have
    // to check that there aren't any lifetime issues with the
    // GlobalVariable outliving the Function or codeobject.
    Value *passed_in_code_object = ConstantExpr::getIntToPtr(
        ConstantInt::get(Type::Int64Ty,
                         reinterpret_cast<uintptr_t>(this->code_object_)),
        PyTypeBuilder<PyCodeObject*>::get());
    this->Assert(this->builder_.CreateICmpEQ(code, passed_in_code_object),
                 "Called with unexpected code object.");
#endif  // NDEBUG
    this->varnames_ = this->builder_.CreateLoad(
        CodeTy::co_varnames(this->builder_, code),
        "varnames");

    Value *names_tuple = this->builder_.CreateBitCast(
        this->builder_.CreateLoad(
            CodeTy::co_names(this->builder_, code)),
        PyTypeBuilder<PyTupleObject*>::get(),
        "names");
    // Get the address of the names_tuple's first item as well.
    this->names_ = this->GetTupleItemSlot(names_tuple, 0);

    // The next GEP-magic assigns this->fastlocals_ to
    // &frame_[0].f_localsplus[0].
    Value *localsplus = FrameTy::f_localsplus(this->builder_, this->frame_);
    this->fastlocals_ = this->builder_.CreateStructGEP(
        localsplus, 0, "fastlocals");
    Value *nlocals = this->builder_.CreateLoad(
        CodeTy::co_nlocals(this->builder_, code), "nlocals");

    this->freevars_ =
        this->builder_.CreateGEP(this->fastlocals_, nlocals, "freevars");
    this->globals_ =
        this->builder_.CreateBitCast(
            this->builder_.CreateLoad(
                FrameTy::f_globals(this->builder_, this->frame_)),
            PyTypeBuilder<PyObject *>::get());
    this->builtins_ =
        this->builder_.CreateBitCast(
            this->builder_.CreateLoad(
                FrameTy::f_builtins(this->builder_,this->frame_)),
            PyTypeBuilder<PyObject *>::get());
    this->f_lineno_addr_ = FrameTy::f_lineno(this->builder_, this->frame_);
    this->f_lasti_addr_ = FrameTy::f_lasti(this->builder_, this->frame_);

    BasicBlock *start = BasicBlock::Create("body_start", this->function_);
    if (this->is_generator_) {
      // Support generator.throw().  If frame->f_throwflag is set, the
      // caller has set an exception, and we're supposed to propagate
      // it.
      BasicBlock *propagate_generator_throw =
          BasicBlock::Create("propagate_generator_throw", this->function_);
      BasicBlock *continue_generator_or_start_func =
          BasicBlock::Create("continue_generator_or_start_func",
                             this->function_);

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
          ConstantInt::getSigned(PyTypeBuilder<int>::get(), -1),
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
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get()),
        this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
                               this->unwind_reason_addr_);
    this->CreateCall(
        this->GetGlobalFunction<int(PyFrameObject*)>("PyTraceBack_Here"),
        this->frame_);
    BasicBlock *call_exc_trace =
        BasicBlock::Create("call_exc_trace", this->function_);
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
        BasicBlock::Create("goto_unwind_target", this->function_);
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
        BasicBlock::Create("pop_remaining_objects", this->function_);
    {  // Implements the fast_block_end loop toward the end of
       // PyEval_EvalFrame().  This pops blocks off the block-stack
       // and values off the value-stack until it finds a block that
       // wants to handle the current unwind reason.
        BasicBlock *unwind_loop_header =
            BasicBlock::Create("unwind_loop_header", this->function_);
        BasicBlock *unwind_loop_body =
            BasicBlock::Create("unwind_loop_body", this->function_);

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
            BasicBlock::Create("not_continue", function());
        BasicBlock *unwind_continue =
            BasicBlock::Create("unwind_continue", function());
        Value *is_setup_loop = this->builder_.CreateICmpEQ(
            block_type,
            ConstantInt::get(block_type->getType(), ::SETUP_LOOP),
            "is_setup_loop");
        Value *is_continue = this->builder_.CreateICmpEQ(
            unwind_reason,
            ConstantInt::get(Type::Int8Ty, UNWIND_CONTINUE),
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
            ConstantInt::get(Type::Int8Ty, UNWIND_NOUNWIND),
            this->unwind_reason_addr_);
        // Convert the return value to the unwind target. This is in keeping
        // with eval.cc. There's probably some LLVM magic that will allow
        // us to skip the boxing/unboxing, but this will work for now.
        Value *boxed_retval = this->builder_.CreateLoad(this->retval_addr_);
        Value *unbox_retval = this->builder_.CreateTrunc(
            this->CreateCall(
                this->GetGlobalFunction<long(PyObject *)>("PyInt_AsLong"),
                boxed_retval),
            Type::Int32Ty,
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
            BasicBlock::Create("handle_loop", this->function_);
        BasicBlock *handle_except =
            BasicBlock::Create("handle_except", this->function_);
        BasicBlock *handle_finally =
            BasicBlock::Create("handle_finally", this->function_);
        BasicBlock *push_exception =
            BasicBlock::Create("push_exception", this->function_);
        BasicBlock *goto_block_handler =
            BasicBlock::Create("goto_block_handler", this->function_);

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
            unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_BREAK),
            "currently_unwinding_break");
        this->builder_.CreateCondBr(unwinding_break,
                                    goto_block_handler, unwind_loop_header);

        this->builder_.SetInsertPoint(handle_except);
        // We only consider visiting except blocks when an exception
        // is being unwound.
        Value *unwinding_exception = this->builder_.CreateICmpEQ(
            unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
            "currently_unwinding_exception");
        this->builder_.CreateCondBr(unwinding_exception,
                                    push_exception, unwind_loop_header);

        this->builder_.SetInsertPoint(push_exception);
        // We need an alloca here so _PyLlvm_FastEnterExceptOrFinally
        // can return into it.  This alloca _won't_ be optimized by
        // mem2reg because its address is taken.
        Value *exc_info = this->CreateAllocaInEntryBlock(
            PyTypeBuilder<PyExcInfo>::get(), NULL, "exc_info");
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
            BasicBlock::Create("push_retval", this->function_);
        BasicBlock *handle_finally_end =
            BasicBlock::Create("handle_finally_end", this->function_);
        llvm::SwitchInst *should_push_retval = this->builder_.CreateSwitch(
            unwind_reason, handle_finally_end, 2);
        // When unwinding for an exception, we have to save the
        // exception onto the stack.
        should_push_retval->addCase(
            ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION), push_exception);
        // When unwinding for a return or continue, we have to save
        // the return value or continue target onto the stack.
        should_push_retval->addCase(
            ConstantInt::get(Type::Int8Ty, UNWIND_RETURN), push_retval);
        should_push_retval->addCase(
            ConstantInt::get(Type::Int8Ty, UNWIND_CONTINUE), push_retval);

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
                                      PyTypeBuilder<long>::get()),
            "unwind_reason_as_pyint");
        this->Push(unwind_reason_as_pyint);

        this->FallThroughTo(goto_block_handler);
        // Clear the unwind reason while running through the block's
        // handler.  mem2reg should never actually decide to use this
        // value, but having it here should make such forgotten stores
        // more obvious.
        this->builder_.CreateStore(
            ConstantInt::get(Type::Int8Ty, UNWIND_NOUNWIND),
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
        BasicBlock::Create("reset_retval", this->function_);
    Value *unwinding_for_return =
        this->builder_.CreateICmpEQ(
            unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_RETURN));
    this->builder_.CreateCondBr(unwinding_for_return,
                                this->do_return_block_, reset_retval);

    this->builder_.SetInsertPoint(reset_retval);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get()),
        this->retval_addr_);
    this->builder_.CreateBr(this->do_return_block_);
}

void
LlvmFunctionBuilder::FillDoReturnBlock()
{
    this->builder_.SetInsertPoint(this->do_return_block_);
    BasicBlock *check_frame_exception =
        BasicBlock::Create("check_frame_exception", this->function_);
    BasicBlock *trace_leave_function =
        BasicBlock::Create("trace_leave_function", this->function_);
    BasicBlock *tracer_raised =
        BasicBlock::Create("tracer_raised", this->function_);

    // Trace exiting from this function, if tracing is turned on.
    Value *use_tracing = this->builder_.CreateLoad(
        ThreadStateTy::use_tracing(this->builder_, this->tstate_));
    this->builder_.CreateCondBr(this->IsNonZero(use_tracing),
                                trace_leave_function, check_frame_exception);

    this->builder_.SetInsertPoint(trace_leave_function);
    Value *unwind_reason =
        this->builder_.CreateLoad(this->unwind_reason_addr_);
    Value *is_return = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_RETURN),
        "is_return");
    Value *is_yield = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_YIELD),
        "is_yield");
    Value *is_exception = this->builder_.CreateICmpEQ(
        unwind_reason, ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
        "is_exception");
    Value *is_yield_or_return = this->builder_.CreateOr(is_return, is_yield);
    Value *traced_retval = this->builder_.CreateLoad(this->retval_addr_);
    Value *trace_args[] = {
        this->tstate_,
        this->frame_,
        traced_retval,
        this->builder_.CreateIntCast(
            is_yield_or_return, PyTypeBuilder<char>::get(),
            false /* unsigned */),
        this->builder_.CreateIntCast(
            is_exception, PyTypeBuilder<char>::get(), false /* unsigned */)
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
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get()),
        this->retval_addr_);
    this->builder_.CreateBr(check_frame_exception);

    this->builder_.SetInsertPoint(check_frame_exception);
    // If this frame raised and caught an exception, it saved it into
    // sys.exc_info(). The calling frame may also be in the process of
    // handling an exception, in which case we don't want to clobber
    // its sys.exc_info().  See eval.cc's _PyEval_ResetExcInfo for
    // details.
    BasicBlock *have_frame_exception =
        BasicBlock::Create("have_frame_exception", this->function_);
    BasicBlock *no_frame_exception =
        BasicBlock::Create("no_frame_exception", this->function_);
    BasicBlock *finish_return =
        BasicBlock::Create("finish_return", this->function_);
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
    this->builder_.CreateRet(retval);
}

// Before jumping to this block, make sure frame->f_lasti points to
// the opcode index at which to resume.
void
LlvmFunctionBuilder::FillBailToInterpreterBlock()
{
    this->builder_.SetInsertPoint(this->bail_to_interpreter_block_);
    // Don't just immediately jump back to the JITted code.
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<int>::get(), 0),
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
    this->builder_.CreateRet(bail);
}

void
LlvmFunctionBuilder::PopAndDecrefTo(Value *target_stack_pointer)
{
    BasicBlock *pop_loop = BasicBlock::Create("pop_loop", this->function_);
    BasicBlock *pop_block = BasicBlock::Create("pop_stack", this->function_);
    BasicBlock *pop_done = BasicBlock::Create("pop_done", this->function_);

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
    const Type *len_type[] = { Type::Int64Ty };
    Value *memcpy = Intrinsic::getDeclaration(
        this->module_, Intrinsic::memcpy, len_type, 1);
    assert(target->getType() == array->getType() &&
           "memcpy's source and destination should have the same type.");
    // Calculate the length as int64_t(&array_type(NULL)[N]).
    Value *length = this->builder_.CreatePtrToInt(
        this->builder_.CreateGEP(Constant::getNullValue(array->getType()), N),
        Type::Int64Ty);
    this->CreateCall(
        memcpy,
        this->builder_.CreateBitCast(target, PyTypeBuilder<char*>::get()),
        this->builder_.CreateBitCast(array, PyTypeBuilder<char*>::get()),
        length,
        // Unknown alignment.
        ConstantInt::get(Type::Int32Ty, 0));
}

void
LlvmFunctionBuilder::CopyToFrameObject()
{
    // Save the current stack pointer into the frame.
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
        Constant::getNullValue(PyTypeBuilder<PyObject **>::get()),
        f_stacktop);

    Value *num_blocks = this->builder_.CreateLoad(
        FrameTy::f_iblock(this->builder_, this->frame_));
    this->builder_.CreateStore(num_blocks, this->num_blocks_addr_);
    this->MemCpy(this->blockstack_addr_,
                 this->builder_.CreateStructGEP(
                     FrameTy::f_blockstack(this->builder_, this->frame_), 0),
                 num_blocks);
}

void
LlvmFunctionBuilder::SetLasti(int current_instruction_index)
{
    this->f_lasti_ = current_instruction_index;
}

void
LlvmFunctionBuilder::SetLineNumber(int line)
{
    BasicBlock *this_line = BasicBlock::Create("line_start", this->function_);

    this->builder_.CreateStore(
        ConstantInt::getSigned(PyTypeBuilder<int>::get(), line),
        this->f_lineno_addr_);

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
        continue_backedge = BasicBlock::Create(
            backedge_landing->getName() + ".cont", this->function_);
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
            ConstantInt::getSigned(PyTypeBuilder<int>::get(), line_number),
            this->f_lineno_addr_);

        // If tracing has been turned on, jump back to the interpreter.
        this->MaybeCallLineTrace(target, _PYFRAME_BACKEDGE_TRACE);
    }
}

void
LlvmFunctionBuilder::MaybeCallLineTrace(BasicBlock *fallthrough_block,
                                        char direction)
{
    BasicBlock *call_trace = BasicBlock::Create("call_trace", this->function_);

    Value *tracing_possible = this->builder_.CreateLoad(
        this->GetGlobalVariable<int>("_Py_TracingPossible"));
    this->builder_.CreateCondBr(this->IsNonZero(tracing_possible),
                                call_trace, fallthrough_block);

    this->builder_.SetInsertPoint(call_trace);
    this->builder_.CreateStore(
        // -1 so that next_instr gets set right in EvalFrame.
        ConstantInt::getSigned(PyTypeBuilder<int>::get(), this->f_lasti_ - 1),
        this->f_lasti_addr_);
    this->builder_.CreateStore(
        ConstantInt::get(PyTypeBuilder<char>::get(), direction),
        FrameTy::f_bailed_from_llvm(this->builder_, this->frame_));
    this->builder_.CreateBr(this->bail_to_interpreter_block_);
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
LlvmFunctionBuilder::AddUnwindTarget(llvm::BasicBlock *target)
{
    // The size of the switch instruction will give us a small unique
    // number for each target block.
    ConstantInt *target_index =
        ConstantInt::get(Type::Int32Ty,
                         this->unwind_target_switch_->getNumCases());
    this->unwind_target_switch_->addCase(target_index, target);
    return target_index;
}

void
LlvmFunctionBuilder::Return(Value *retval)
{
    this->builder_.CreateStore(retval, this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_RETURN),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);
}

void
LlvmFunctionBuilder::PropagateException()
{
    this->builder_.CreateBr(this->propagate_exception_block_);
}

void
LlvmFunctionBuilder::LOAD_CONST(int index)
{
    PyObject *co_consts = this->code_object_->co_consts;
    // TODO(jyasskin): we'll eventually want to represent these with
    // llvm::ConstantStructs so that LLVM has more information about them.
    // Casting the pointers to ints makes the structs opaque, which hinders
    // constant propagation, etc.
    Value *const_ = ConstantExpr::getIntToPtr(
        ConstantInt::get(
            Type::Int64Ty,
            reinterpret_cast<uintptr_t>(PyTuple_GET_ITEM(co_consts, index))),
        PyTypeBuilder<PyObject*>::get());
    this->IncRef(const_);
    this->Push(const_);
}

void
LlvmFunctionBuilder::LOAD_GLOBAL(int name_index)
{
    BasicBlock *global_missing = BasicBlock::Create(
        "LOAD_GLOBAL_global_missing", this->function_);
    BasicBlock *global_success = BasicBlock::Create(
        "LOAD_GLOBAL_global_success", this->function_);
    BasicBlock *builtin_missing = BasicBlock::Create(
        "LOAD_GLOBAL_builtin_missing", this->function_);
    BasicBlock *builtin_success = BasicBlock::Create(
        "LOAD_GLOBAL_builtin_success", this->function_);
    BasicBlock *done = BasicBlock::Create("LOAD_GLOBAL_done", this->function_);
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
    BasicBlock *failure = BasicBlock::Create("DELETE_GLOBAL_failure",
                                             this->function_);
    BasicBlock *success = BasicBlock::Create("DELETE_GLOBAL_success",
                                             this->function_);
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
        ConstantInt::get(PyTypeBuilder<int>::get(), index));
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
        ConstantInt::get(PyTypeBuilder<int>::get(), index),
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
        ConstantInt::get(PyTypeBuilder<int>::get(), index));
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
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get());
    Function *pyobj_setattr = this->GetGlobalFunction<
        int(PyObject *, PyObject *, PyObject *)>("PyObject_SetAttr");
    Value *result = this->CreateCall(
        pyobj_setattr, obj, attr, value, "STORE_ATTR_result");
    this->DecRef(obj);
    this->PropagateExceptionOnNonZero(result);
}

void
LlvmFunctionBuilder::LOAD_FAST(int index)
{
    BasicBlock *unbound_local =
        BasicBlock::Create("LOAD_FAST_unbound", this->function_);
    BasicBlock *success =
        BasicBlock::Create("LOAD_FAST_success", this->function_);

    Value *local = this->builder_.CreateLoad(
        this->builder_.CreateGEP(this->fastlocals_,
                                 ConstantInt::get(Type::Int32Ty, index)),
        "FAST_loaded");
    this->builder_.CreateCondBr(this->IsNull(local), unbound_local, success);

    this->builder_.SetInsertPoint(unbound_local);
    Function *do_raise =
        this->GetGlobalFunction<void(PyFrameObject*, int)>(
            "_PyEval_RaiseForUnboundLocal");
    this->CreateCall(
        do_raise, this->frame_,
        ConstantInt::getSigned(PyTypeBuilder<int>::get(),
                               index));
    this->PropagateException();

    this->builder_.SetInsertPoint(success);
    this->IncRef(local);
    this->Push(local);
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
        PyTypeBuilder<PyObject*>::get(),
        NULL, "WITH_CLEANUP_exc_type");
    Value *exc_value = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(),
        NULL, "WITH_CLEANUP_exc_value");
    Value *exc_traceback = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(),
        NULL, "WITH_CLEANUP_exc_traceback");
    Value *exit_func = this->CreateAllocaInEntryBlock(
        PyTypeBuilder<PyObject*>::get(),
        NULL, "WITH_CLEANUP_exit_func");

    BasicBlock *handle_none =
        BasicBlock::Create("WITH_CLEANUP_handle_none", this->function_);
    BasicBlock *check_int =
        BasicBlock::Create("WITH_CLEANUP_check_int", this->function_);
    BasicBlock *handle_int =
        BasicBlock::Create("WITH_CLEANUP_handle_int", this->function_);
    BasicBlock *handle_ret_cont =
        BasicBlock::Create("WITH_CLEANUP_handle_ret_cont", this->function_);
    BasicBlock *handle_default =
        BasicBlock::Create("WITH_CLEANUP_handle_default", this->function_);
    BasicBlock *handle_else =
        BasicBlock::Create("WITH_CLEANUP_handle_else", this->function_);
    BasicBlock *main_block =
        BasicBlock::Create("WITH_CLEANUP_main_block", this->function_);

    Value *none = this->GetGlobalVariable<PyObject>("_Py_NoneStruct");
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
        Type::Int8Ty,
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
    unwind_kind->addCase(ConstantInt::get(Type::Int8Ty, UNWIND_RETURN),
                         handle_ret_cont);
    unwind_kind->addCase(ConstantInt::get(Type::Int8Ty, UNWIND_CONTINUE),
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
        Constant::getNullValue(PyTypeBuilder<PyObject *>::get()));
    Value *ret = this->CreateCall(
        this->GetGlobalFunction<PyObject *(PyObject *, ...)>(
            "PyObject_CallFunctionObjArgs"),
        args.begin(), args.end());
    this->DecRef(this->builder_.CreateLoad(exit_func));
    this->PropagateExceptionOnNull(ret);

    BasicBlock *check_silence =
        BasicBlock::Create("WITH_CLEANUP_check_silence", this->function_);
    BasicBlock *no_silence =
        BasicBlock::Create("WITH_CLEANUP_no_silence", this->function_);
    BasicBlock *cleanup =
        BasicBlock::Create("WITH_CLEANUP_cleanup", this->function_);
    BasicBlock *next =
        BasicBlock::Create("WITH_CLEANUP_next", this->function_);

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
            ConstantInt::get(Type::Int32Ty, freevars_index)));
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
        BasicBlock *failure = BasicBlock::Create("MAKE_CLOSURE_failure",
                                                 this->function_);
        BasicBlock *success = BasicBlock::Create("MAKE_CLOSURE_success",
                                                 this->function_);

        Value *tupsize = ConstantInt::get(
            PyTypeBuilder<Py_ssize_t>::get(), num_defaults);
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
    Value *enum_ir = ConstantInt::get(Type::Int32Ty, event_id);
    this->CreateCall(timer_function, enum_ir);
}
#endif

void
LlvmFunctionBuilder::CALL_FUNCTION(int oparg)
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
        ConstantInt::get(PyTypeBuilder<int>::get(), num_args),
        ConstantInt::get(PyTypeBuilder<int>::get(), num_kwargs),
        "CALL_FUNCTION_result");
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer,
        ConstantInt::getSigned(Type::Int64Ty,
                               -num_args - 2*num_kwargs - 1));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    this->PropagateExceptionOnNull(result);
    this->Push(result);

    // Check signals and maybe switch threads after each function call.
    this->CheckPyTicker();
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
        ConstantInt::get(PyTypeBuilder<int>::get(), num_args),
        ConstantInt::get(PyTypeBuilder<int>::get(), num_kwargs),
        ConstantInt::get(PyTypeBuilder<int>::get(), call_flag),
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
        ConstantInt::getSigned(Type::Int64Ty, -stack_items));
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
        BasicBlock::Create("LOAD_DEREF_failed_load", this->function_);
    BasicBlock *unbound_local =
        BasicBlock::Create("LOAD_DEREF_unbound_local", this->function_);
    BasicBlock *error =
        BasicBlock::Create("LOAD_DEREF_error", this->function_);
    BasicBlock *success =
        BasicBlock::Create("LOAD_DEREF_success", this->function_);

    Value *cell = this->builder_.CreateLoad(
        this->builder_.CreateGEP(
            this->freevars_, ConstantInt::get(Type::Int32Ty, index)));
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
        ConstantInt::get(PyTypeBuilder<int>::get(), index));

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
            this->freevars_, ConstantInt::get(Type::Int32Ty, index)));
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

void
LlvmFunctionBuilder::POP_JUMP_IF_FALSE(llvm::BasicBlock *target,
                                       llvm::BasicBlock *fallthrough)
{
    Value *test_value = this->Pop();
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, fallthrough, target);
}

void
LlvmFunctionBuilder::POP_JUMP_IF_TRUE(llvm::BasicBlock *target,
                                      llvm::BasicBlock *fallthrough)
{
    Value *test_value = this->Pop();
    Value *is_true = this->IsPythonTrue(test_value);
    this->builder_.CreateCondBr(is_true, target, fallthrough);
}

void
LlvmFunctionBuilder::JUMP_IF_FALSE_OR_POP(llvm::BasicBlock *target,
                                          llvm::BasicBlock *fallthrough)
{
    BasicBlock *true_path =
        BasicBlock::Create("JUMP_IF_FALSE_OR_POP_pop", this->function_);
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

}

void
LlvmFunctionBuilder::JUMP_IF_TRUE_OR_POP(llvm::BasicBlock *target,
                                         llvm::BasicBlock *fallthrough)
{
    BasicBlock *false_path =
        BasicBlock::Create("JUMP_IF_TRUE_OR_POP_pop", this->function_);
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
        BasicBlock::Create("DELETE_FAST_failure", this->function_);
    BasicBlock *success =
        BasicBlock::Create("DELETE_FAST_success", this->function_);
    Value *local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::Int32Ty, index));
    Value *orig_value = this->builder_.CreateLoad(
        local_slot, "DELETE_FAST_old_reference");
    this->builder_.CreateCondBr(this->IsNull(orig_value), failure, success);

    this->builder_.SetInsertPoint(failure);
    Function *do_raise = this->GetGlobalFunction<
        void(PyFrameObject *, int)>("_PyEval_RaiseForUnboundLocal");
    this->CreateCall(
        do_raise, this->frame_,
        ConstantInt::getSigned(PyTypeBuilder<int>::get(),
                               index));
    this->PropagateException();

    this->builder_.SetInsertPoint(success);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject *>::get()),
        local_slot);
    this->DecRef(orig_value);
}

void
LlvmFunctionBuilder::SETUP_LOOP(llvm::BasicBlock *target,
                                llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_LOOP, target);
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
        PyTypeBuilder<PyTypeObject *>::get(),
        "iter_type");
    Value *iternext = this->builder_.CreateLoad(
        TypeTy::tp_iternext(this->builder_, iter_tp),
        "iternext");
    Value *next = this->CreateCall(iternext, iter, "next");
    BasicBlock *got_next = BasicBlock::Create("got_next", this->function_);
    BasicBlock *next_null = BasicBlock::Create("next_null", this->function_);
    this->builder_.CreateCondBr(this->IsNull(next), next_null, got_next);

    this->builder_.SetInsertPoint(next_null);
    Value *err_occurred = this->CreateCall(
        this->GetGlobalFunction<PyObject*()>("PyErr_Occurred"));
    BasicBlock *iter_ended = BasicBlock::Create("iter_ended", this->function_);
    BasicBlock *exception = BasicBlock::Create("exception", this->function_);
    this->builder_.CreateCondBr(this->IsNull(err_occurred),
                                iter_ended, exception);

    this->builder_.SetInsertPoint(exception);
    Value *exc_stopiteration = this->builder_.CreateLoad(
        this->GetGlobalVariable<PyObject*>("PyExc_StopIteration"));
    Value *was_stopiteration = this->CreateCall(
        this->GetGlobalFunction<int(PyObject *)>("PyErr_ExceptionMatches"),
        exc_stopiteration);
    BasicBlock *clear_err = BasicBlock::Create("clear_err", this->function_);
    BasicBlock *propagate = BasicBlock::Create("propagate", this->function_);
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
                                   llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_EXCEPT, target);
}

void
LlvmFunctionBuilder::SETUP_FINALLY(llvm::BasicBlock *target,
                                   llvm::BasicBlock *fallthrough)
{
    this->CallBlockSetup(::SETUP_FINALLY, target);
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
        BasicBlock::Create("unwind_code", this->function_);
    BasicBlock *test_exception =
        BasicBlock::Create("test_exception", this->function_);
    BasicBlock *reraise_exception =
        BasicBlock::Create("reraise_exception", this->function_);
    BasicBlock *check_none = BasicBlock::Create("check_none", this->function_);
    BasicBlock *not_none = BasicBlock::Create("not_none", this->function_);
    BasicBlock *finally_fallthrough =
        BasicBlock::Create("finally_fallthrough", this->function_);

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
        Type::Int8Ty,
        "unwind_reason");
    this->DecRef(finally_discriminator);
    // Save the unwind reason for when we jump to the unwind block.
    this->builder_.CreateStore(unwind_reason, this->unwind_reason_addr_);
    // Check if we need to pop the return value or loop target.
    BasicBlock *pop_retval = BasicBlock::Create("pop_retval", this->function_);
    llvm::SwitchInst *should_pop_retval =
        this->builder_.CreateSwitch(unwind_reason, this->unwind_block_, 2);
    should_pop_retval->addCase(ConstantInt::get(Type::Int8Ty, UNWIND_RETURN),
                               pop_retval);
    should_pop_retval->addCase(ConstantInt::get(Type::Int8Ty, UNWIND_CONTINUE),
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
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get()),
        this->retval_addr_);
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->unwind_block_);

    this->builder_.SetInsertPoint(check_none);
    // The contents of the try block push None onto the stack just
    // before falling through to the finally block.  If we didn't get
    // an unwind reason or an exception, we expect to fall through,
    // but for sanity we also double-check that the None is present.
    Value *is_none = this->builder_.CreateICmpEQ(
        finally_discriminator,
        this->GetGlobalVariable<PyObject>("_Py_NoneStruct"));
    this->DecRef(finally_discriminator);
    this->builder_.CreateCondBr(is_none, finally_fallthrough, not_none);

    this->builder_.SetInsertPoint(not_none);
    // If we didn't get a None, raise a SystemError.
    Value *system_error = this->builder_.CreateLoad(
        this->GetGlobalVariable<PyObject *>(
            "PyExc_SystemError"));
    Value *err_msg =
        this->llvm_data_->GetGlobalStringPtr("'finally' pops bad exception");
    this->CreateCall(
        this->GetGlobalFunction<void(PyObject *, const char *)>(
            "PyErr_SetString"),
        system_error, err_msg);
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
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
                                   llvm::BasicBlock *fallthrough)
{
    // Accept code after a continue statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = BasicBlock::Create("dead_code", function());
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_CONTINUE),
                               this->unwind_reason_addr_);
    Value *unwind_target = this->AddUnwindTarget(target);
    // Yes, store the unwind target in the return value slot. This is to
    // keep the translation from eval.cc as close as possible; deviation will
    // only introduce bugs. The UNWIND_CONTINUE cases in the unwind block
    // (see FillUnwindBlock()) will pick this up and deal with it.
    Value *pytarget = this->CreateCall(
            this->GetGlobalFunction<PyObject *(long)>("PyInt_FromLong"),
            this->builder_.CreateZExt(unwind_target,
                                      PyTypeBuilder<long>::get()));
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
    BasicBlock *dead_code = BasicBlock::Create("dead_code", this->function_);
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_BREAK),
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
    BasicBlock *dead_code = BasicBlock::Create("dead_code", this->function_);

    Value *retval = this->Pop();
    this->Return(retval);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::YIELD_VALUE()
{
    assert(this->is_generator_ && "yield in non-generator!");
    BasicBlock *yield_resume =
        BasicBlock::Create("yield_resume", this->function_);
    // Save the current opcode index into f_lasti when we yield so
    // that, if tracing gets turned on while we're outside this
    // function we can jump back to the interpreter at the right
    // place.
    ConstantInt *yield_number =
        ConstantInt::getSigned(PyTypeBuilder<int>::get(), this->f_lasti_);
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
    this->builder_.CreateStore(ConstantInt::get(Type::Int8Ty, UNWIND_YIELD),
                               this->unwind_reason_addr_);
    this->builder_.CreateBr(this->do_return_block_);

    // Continue inserting code inside the resume block.
    this->builder_.SetInsertPoint(yield_resume);
    // Set frame->f_lasti back to negative so that exceptions are
    // generated with llvm-provided line numbers.
    this->builder_.CreateStore(
        ConstantInt::getSigned(PyTypeBuilder<int>::get(), -2),
        this->f_lasti_addr_);
}

void
LlvmFunctionBuilder::DoRaise(Value *exc_type, Value *exc_inst, Value *exc_tb)
{
    // Accept code after a raise statement, even though it's never executed.
    // Otherwise, CPython's willingness to insert code after block
    // terminators causes problems.
    BasicBlock *dead_code = BasicBlock::Create("dead_code", this->function_);

    // All raises set 'why' to UNWIND_EXCEPTION and the return value to NULL.
    // This is redundant with the propagate_exception_block_, but mem2reg will
    // remove the redundancy.
    this->builder_.CreateStore(
        ConstantInt::get(Type::Int8Ty, UNWIND_EXCEPTION),
        this->unwind_reason_addr_);
    this->builder_.CreateStore(
        Constant::getNullValue(PyTypeBuilder<PyObject*>::get()),
        this->retval_addr_);

    Function *do_raise = this->GetGlobalFunction<
        int(PyObject*, PyObject *, PyObject *)>("_PyEval_DoRaise");
    // _PyEval_DoRaise eats references.
    Value *is_reraise = this->CreateCall(
        do_raise, exc_type, exc_inst, exc_tb, "raise_is_reraise");
    // If this is a "re-raise", we jump straight to the unwind block.
    // If it's a new raise, we call PyTraceBack_Here from the
    // propagate_exception_block_.
    this->builder_.CreateCondBr(
        this->IsNonZero(is_reraise),
        this->unwind_block_, this->propagate_exception_block_);

    this->builder_.SetInsertPoint(dead_code);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_ZERO()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *exc_inst = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *exc_type = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_ONE()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *exc_inst = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *exc_type = this->Pop();
    this->DoRaise(exc_type, exc_inst, exc_tb);
}

void
LlvmFunctionBuilder::RAISE_VARARGS_TWO()
{
    Value *exc_tb = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
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
    Value *pynone = this->GetGlobalVariable<PyObject>("_Py_NoneStruct");
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
        this->GetGlobalVariable<PyObject>("_Py_ZeroStruct"),
        this->GetGlobalVariable<PyObject>("_Py_TrueStruct"),
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
        ConstantInt::get(PyTypeBuilder<int>::get(), cmp_op),
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
        this->GetGlobalVariable<PyObject>("_Py_TrueStruct"),
        this->GetGlobalVariable<PyObject>("_Py_ZeroStruct"),
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
        dict_type, GetGlobalVariable<PyTypeObject>("PyDict_Type"));
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
        lst, PyTypeBuilder<PyListObject *>::get());
    // Load the target of the ob_item PyObject** into list_items.
    Value *list_items = this->builder_.CreateLoad(
        ListTy::ob_item(this->builder_, listobj));
    // GEP the list_items PyObject* up to the desired item
    return this->builder_.CreateGEP(list_items,
                                    ConstantInt::get(Type::Int32Ty, idx),
                                    "list_item_slot");
}

Value *
LlvmFunctionBuilder::GetTupleItemSlot(Value *tup, int idx)
{
    Value *tupobj = this->builder_.CreateBitCast(
        tup, PyTypeBuilder<PyTupleObject*>::get());
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
    const Type *IntSsizeTy = PyTypeBuilder<Py_ssize_t>::get();
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
        PyTypeBuilder<Py_ssize_t>::get(), size);
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
        PyTypeBuilder<PyObject *>::get());
    Value *start = this->Pop();
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::SLICE_RIGHT()
{
    Value *stop = this->Pop();
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *seq = this->Pop();
    this->ApplySlice(seq, start, stop);
}

void
LlvmFunctionBuilder::SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
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
        PyTypeBuilder<PyObject *>::get());
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
        PyTypeBuilder<PyObject *>::get());
    Value *seq = this->Pop();
    Value *source = this->Pop();
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::STORE_SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
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
        PyTypeBuilder<PyObject *>::get());
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_LEFT()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *start = this->Pop();
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_RIGHT()
{
    Value *stop = this->Pop();
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::DELETE_SLICE_NONE()
{
    Value *stop = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *start = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    Value *seq = this->Pop();
    Value *source = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
    this->AssignSlice(seq, start, stop, source);
}

void
LlvmFunctionBuilder::BUILD_SLICE_TWO()
{
    Value *step = Constant::getNullValue(
        PyTypeBuilder<PyObject *>::get());
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
        ConstantInt::getSigned(PyTypeBuilder<Py_ssize_t>::get(),
                               size));
    Value *result = this->CreateCall(
        unpack_iterable, iterable,
        ConstantInt::get(PyTypeBuilder<int>::get(), size, true),
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
        stack_pointer, ConstantInt::get(Type::Int32Ty, 1));
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
}

Value *
LlvmFunctionBuilder::Pop()
{
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *new_stack_pointer = this->builder_.CreateGEP(
        stack_pointer, ConstantInt::getSigned(Type::Int32Ty, -1));
    Value *former_top = this->builder_.CreateLoad(new_stack_pointer);
    this->builder_.CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    return former_top;
}

Value *
LlvmFunctionBuilder::GetStackLevel()
{
    Value *stack_pointer = this->builder_.CreateLoad(this->stack_pointer_addr_);
    Value *stack_pointer_int =
        this->builder_.CreatePtrToInt(stack_pointer, Type::Int64Ty);
    Value *stack_bottom_int =
        this->builder_.CreatePtrToInt(this->stack_bottom_, Type::Int64Ty);
    Value *difference =
        this->builder_.CreateSub(stack_pointer_int, stack_bottom_int);
    Value *level64 = this->builder_.CreateSDiv(
        difference,
        llvm::ConstantExpr::getSizeOf(
            PyTypeBuilder<PyObject*>::get()));
    // The stack level is stored as an int, not an int64.
    return this->builder_.CreateTrunc(level64,
                                      PyTypeBuilder<int>::get(),
                                      "stack_level");
}

void
LlvmFunctionBuilder::SetLocal(int locals_index, llvm::Value *new_value)
{
    Value *local_slot = this->builder_.CreateGEP(
        this->fastlocals_, ConstantInt::get(Type::Int32Ty, locals_index));
    Value *orig_value =
        this->builder_.CreateLoad(local_slot, "local_overwritten");
    this->builder_.CreateStore(new_value, local_slot);
    this->XDecRef(orig_value);
}

void
LlvmFunctionBuilder::CallBlockSetup(int block_type, llvm::BasicBlock *handler)
{
    Value *stack_level = this->GetStackLevel();
    Value *unwind_target_index = this->AddUnwindTarget(handler);
    Function *blocksetup =
        this->GetGlobalFunction<void(PyTryBlock *, char *, int, int, int)>(
            "_PyLlvm_Frame_BlockSetup");
    Value *args[] = {
        this->blockstack_addr_, this->num_blocks_addr_,
        ConstantInt::get(PyTypeBuilder<int>::get(), block_type),
        unwind_target_index,
        stack_level
    };
    this->CreateCall(blocksetup, args, array_endof(args));
}

void
LlvmFunctionBuilder::CheckPyTicker(BasicBlock *next_block)
{
    if (next_block == NULL) {
        next_block = BasicBlock::Create("ticker_dec_end", this->function_);
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
    BasicBlock *assert_passed = BasicBlock::Create(
        failure_message + "_assert_passed", this->function_);
    BasicBlock *assert_failed = BasicBlock::Create(
        failure_message + "_assert_failed", this->function_);
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
            name, PyTypeBuilder<FunctionType>::get()));
}

template<typename VariableType> Constant *
LlvmFunctionBuilder::GetGlobalVariable(const std::string &name)
{
    return this->module_->getOrInsertGlobal(
        name, PyTypeBuilder<VariableType>::get());
}

// For llvm::Functions, copy callee's calling convention and attributes to
// callsite; for non-Functions, leave the default calling convention and
// attributes in place (ie, do nothing). We require this for function pointers.
static llvm::CallInst *
TransferAttributes(llvm::CallInst *callsite, const llvm::Value* callee) {
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
        PyTypeBuilder<PyTypeObject *>::get());
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
        BasicBlock::Create("PropagateExceptionOnNull_propagate",
                           this->function_);
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNull_pass", this->function_);
    this->builder_.CreateCondBr(this->IsNull(value), propagate, pass);

    this->builder_.SetInsertPoint(propagate);
    this->PropagateException();

    this->builder_.SetInsertPoint(pass);
}

void
LlvmFunctionBuilder::PropagateExceptionOnNegative(Value *value)
{
    BasicBlock *propagate =
        BasicBlock::Create("PropagateExceptionOnNegative_propagate",
        this->function_);
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNegative_pass",
                           this->function_);
    this->builder_.CreateCondBr(this->IsNegative(value), propagate, pass);

    this->builder_.SetInsertPoint(propagate);
    this->PropagateException();

    this->builder_.SetInsertPoint(pass);
}

void
LlvmFunctionBuilder::PropagateExceptionOnNonZero(Value *value)
{
    BasicBlock *propagate =
        BasicBlock::Create("PropagateExceptionOnNonZero_propagate",
        this->function_);
    BasicBlock *pass =
        BasicBlock::Create("PropagateExceptionOnNonZero_pass", this->function_);
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
            this->names_, ConstantInt::get(Type::Int32Ty, name_index),
            "constant_name"));
    return name;
}

llvm::Value *
LlvmFunctionBuilder::IsPythonTrue(Value *value)
{
    BasicBlock *not_py_true =
        BasicBlock::Create("IsPythonTrue_is_not_PyTrue", this->function_);
    BasicBlock *not_py_false =
        BasicBlock::Create("IsPythonTrue_is_not_PyFalse", this->function_);
    BasicBlock *decref_value =
        BasicBlock::Create("IsPythonTrue_decref_value", this->function_);
    BasicBlock *done =
        BasicBlock::Create("IsPythonTrue_done", this->function_);

    Value *result_addr = this->CreateAllocaInEntryBlock(
        Type::Int1Ty, NULL, "IsPythonTrue_result");
    Value *py_false = this->GetGlobalVariable<PyObject>("_Py_ZeroStruct");
    Value *py_true = this->GetGlobalVariable<PyObject>("_Py_TrueStruct");

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
