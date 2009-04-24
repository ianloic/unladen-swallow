//===--- CGStmt.cpp - Emit LLVM Code from Statements ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Stmt nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CGDebugInfo.h"
#include "CodeGenModule.h"
#include "CodeGenFunction.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/InlineAsm.h"
#include "llvm/Intrinsics.h"
#include "llvm/Target/TargetData.h"
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                              Statement Emission
//===----------------------------------------------------------------------===//

void CodeGenFunction::EmitStopPoint(const Stmt *S) {
  if (CGDebugInfo *DI = CGM.getDebugInfo()) {
    DI->setLocation(S->getLocStart());
    DI->EmitStopPoint(CurFn, Builder);
  }
}

void CodeGenFunction::EmitStmt(const Stmt *S) {
  assert(S && "Null statement?");

  // Check if we can handle this without bothering to generate an
  // insert point or debug info.
  if (EmitSimpleStmt(S))
    return;

  // If we happen to be at an unreachable point just create a dummy
  // basic block to hold the code. We could change parts of irgen to
  // simply not generate this code, but this situation is rare and
  // probably not worth the effort.
  // FIXME: Verify previous performance/effort claim.
  EnsureInsertPoint();
  
  // Generate a stoppoint if we are emitting debug info.
  EmitStopPoint(S);

  switch (S->getStmtClass()) {
  default:
    // Must be an expression in a stmt context.  Emit the value (to get
    // side-effects) and ignore the result.
    if (const Expr *E = dyn_cast<Expr>(S)) {
      if (!hasAggregateLLVMType(E->getType()))
        EmitScalarExpr(E);
      else if (E->getType()->isAnyComplexType())
        EmitComplexExpr(E);
      else
        EmitAggExpr(E, 0, false);
    } else {
      ErrorUnsupported(S, "statement");
    }
    break;
  case Stmt::IndirectGotoStmtClass:  
    EmitIndirectGotoStmt(cast<IndirectGotoStmt>(*S)); break;

  case Stmt::IfStmtClass:       EmitIfStmt(cast<IfStmt>(*S));             break;
  case Stmt::WhileStmtClass:    EmitWhileStmt(cast<WhileStmt>(*S));       break;
  case Stmt::DoStmtClass:       EmitDoStmt(cast<DoStmt>(*S));             break;
  case Stmt::ForStmtClass:      EmitForStmt(cast<ForStmt>(*S));           break;
    
  case Stmt::ReturnStmtClass:   EmitReturnStmt(cast<ReturnStmt>(*S));     break;
  case Stmt::DeclStmtClass:     EmitDeclStmt(cast<DeclStmt>(*S));         break;

  case Stmt::SwitchStmtClass:   EmitSwitchStmt(cast<SwitchStmt>(*S));     break;
  case Stmt::AsmStmtClass:      EmitAsmStmt(cast<AsmStmt>(*S));           break;

  case Stmt::ObjCAtTryStmtClass:
    EmitObjCAtTryStmt(cast<ObjCAtTryStmt>(*S));
    break;    
  case Stmt::ObjCAtCatchStmtClass:
    assert(0 && "@catch statements should be handled by EmitObjCAtTryStmt");
    break;
  case Stmt::ObjCAtFinallyStmtClass:
    assert(0 && "@finally statements should be handled by EmitObjCAtTryStmt");
    break;
  case Stmt::ObjCAtThrowStmtClass:
    EmitObjCAtThrowStmt(cast<ObjCAtThrowStmt>(*S));
    break;
  case Stmt::ObjCAtSynchronizedStmtClass:
    EmitObjCAtSynchronizedStmt(cast<ObjCAtSynchronizedStmt>(*S));
    break;
  case Stmt::ObjCForCollectionStmtClass: 
    EmitObjCForCollectionStmt(cast<ObjCForCollectionStmt>(*S));
    break;
  }
}

bool CodeGenFunction::EmitSimpleStmt(const Stmt *S) {
  switch (S->getStmtClass()) {
  default: return false;
  case Stmt::NullStmtClass: break;
  case Stmt::CompoundStmtClass: EmitCompoundStmt(cast<CompoundStmt>(*S)); break;
  case Stmt::LabelStmtClass:    EmitLabelStmt(cast<LabelStmt>(*S));       break;
  case Stmt::GotoStmtClass:     EmitGotoStmt(cast<GotoStmt>(*S));         break;
  case Stmt::BreakStmtClass:    EmitBreakStmt(cast<BreakStmt>(*S));       break;
  case Stmt::ContinueStmtClass: EmitContinueStmt(cast<ContinueStmt>(*S)); break;
  case Stmt::DefaultStmtClass:  EmitDefaultStmt(cast<DefaultStmt>(*S));   break;
  case Stmt::CaseStmtClass:     EmitCaseStmt(cast<CaseStmt>(*S));         break;
  }

  return true;
}

/// EmitCompoundStmt - Emit a compound statement {..} node.  If GetLast is true,
/// this captures the expression result of the last sub-statement and returns it
/// (for use by the statement expression extension).
RValue CodeGenFunction::EmitCompoundStmt(const CompoundStmt &S, bool GetLast,
                                         llvm::Value *AggLoc, bool isAggVol) {
  // FIXME: handle vla's etc.
  CGDebugInfo *DI = CGM.getDebugInfo();
  if (DI) {
    EnsureInsertPoint();
    DI->setLocation(S.getLBracLoc());
    DI->EmitRegionStart(CurFn, Builder);
  }

  // Push a null stack save value.
  StackSaveValues.push_back(0);
  
  for (CompoundStmt::const_body_iterator I = S.body_begin(),
       E = S.body_end()-GetLast; I != E; ++I)
    EmitStmt(*I);

  if (DI) {
    EnsureInsertPoint();
    DI->setLocation(S.getRBracLoc());
    DI->EmitRegionEnd(CurFn, Builder);
  }

  RValue RV;
  if (!GetLast) 
    RV = RValue::get(0);
  else {
    // We have to special case labels here.  They are statements, but when put 
    // at the end of a statement expression, they yield the value of their
    // subexpression.  Handle this by walking through all labels we encounter,
    // emitting them before we evaluate the subexpr.
    const Stmt *LastStmt = S.body_back();
    while (const LabelStmt *LS = dyn_cast<LabelStmt>(LastStmt)) {
      EmitLabel(*LS);
      LastStmt = LS->getSubStmt();
    }
  
    EnsureInsertPoint();
    
    RV = EmitAnyExpr(cast<Expr>(LastStmt), AggLoc);
  }

  if (llvm::Value *V = StackSaveValues.pop_back_val()) {
    V = Builder.CreateLoad(V, "tmp");
    
    llvm::Value *F = CGM.getIntrinsic(llvm::Intrinsic::stackrestore);
    Builder.CreateCall(F, V);
  }
  
  return RV;
}

void CodeGenFunction::EmitBlock(llvm::BasicBlock *BB, bool IsFinished) {
  // Fall out of the current block (if necessary).
  EmitBranch(BB);

  if (IsFinished && BB->use_empty()) {
    delete BB;
    return;
  }

  CurFn->getBasicBlockList().push_back(BB);
  Builder.SetInsertPoint(BB);
}

void CodeGenFunction::EmitBranch(llvm::BasicBlock *Target) {
  // Emit a branch from the current block to the target one if this
  // was a real block.  If this was just a fall-through block after a
  // terminator, don't emit it.
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  if (!CurBB || CurBB->getTerminator()) {
    // If there is no insert point or the previous block is already
    // terminated, don't touch it.
  } else {
    // Otherwise, create a fall-through branch.
    Builder.CreateBr(Target);
  }

  Builder.ClearInsertionPoint();
}

void CodeGenFunction::EmitLabel(const LabelStmt &S) {
  llvm::BasicBlock *NextBB = getBasicBlockForLabel(&S);
  EmitBlock(NextBB);
}


void CodeGenFunction::EmitLabelStmt(const LabelStmt &S) {
  EmitLabel(S);
  EmitStmt(S.getSubStmt());
}

void CodeGenFunction::EmitGotoStmt(const GotoStmt &S) {
  // FIXME: Implement goto out in @try or @catch blocks.
  if (!ObjCEHStack.empty()) {
    CGM.ErrorUnsupported(&S, "goto inside an Obj-C exception block");
    return;
  }

  for (unsigned i = 0; i < StackSaveValues.size(); i++) {
    if (StackSaveValues[i]) {
      CGM.ErrorUnsupported(&S, "goto inside scope with VLA");
      return;
    }
  }
  
  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (HaveInsertPoint())
    EmitStopPoint(&S);
  EmitBranch(getBasicBlockForLabel(S.getLabel()));
}

void CodeGenFunction::EmitIndirectGotoStmt(const IndirectGotoStmt &S) {
  // FIXME: Implement indirect goto in @try or @catch blocks.
  if (!ObjCEHStack.empty()) {
    CGM.ErrorUnsupported(&S, "goto inside an Obj-C exception block");
    return;
  }

  // Emit initial switch which will be patched up later by
  // EmitIndirectSwitches(). We need a default dest, so we use the
  // current BB, but this is overwritten.
  llvm::Value *V = Builder.CreatePtrToInt(EmitScalarExpr(S.getTarget()),
                                          llvm::Type::Int32Ty, 
                                          "addr");
  llvm::SwitchInst *I = Builder.CreateSwitch(V, Builder.GetInsertBlock());
  IndirectSwitches.push_back(I);

  // Clear the insertion point to indicate we are in unreachable code.
  Builder.ClearInsertionPoint();
}

void CodeGenFunction::EmitIfStmt(const IfStmt &S) {
  // C99 6.8.4.1: The first substatement is executed if the expression compares
  // unequal to 0.  The condition must be a scalar type.
  
  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm of the if/else.
  if (int Cond = ConstantFoldsToSimpleInteger(S.getCond())) {
    // Figure out which block (then or else) is executed.
    const Stmt *Executed = S.getThen(), *Skipped  = S.getElse();
    if (Cond == -1)  // Condition false?
      std::swap(Executed, Skipped);
    
    // If the skipped block has no labels in it, just emit the executed block.
    // This avoids emitting dead code and simplifies the CFG substantially.
    if (!ContainsLabel(Skipped)) {
      if (Executed)
        EmitStmt(Executed);
      return;
    }
  }

  // Otherwise, the condition did not fold, or we couldn't elide it.  Just emit
  // the conditional branch.
  llvm::BasicBlock *ThenBlock = createBasicBlock("if.then");
  llvm::BasicBlock *ContBlock = createBasicBlock("if.end");
  llvm::BasicBlock *ElseBlock = ContBlock;
  if (S.getElse())
    ElseBlock = createBasicBlock("if.else");
  EmitBranchOnBoolExpr(S.getCond(), ThenBlock, ElseBlock);
  
  // Emit the 'then' code.
  EmitBlock(ThenBlock);
  EmitStmt(S.getThen());
  EmitBranch(ContBlock);
  
  // Emit the 'else' code if present.
  if (const Stmt *Else = S.getElse()) {
    EmitBlock(ElseBlock);
    EmitStmt(Else);
    EmitBranch(ContBlock);
  }
  
  // Emit the continuation block for code after the if.
  EmitBlock(ContBlock, true);
}

void CodeGenFunction::EmitWhileStmt(const WhileStmt &S) {
  // Emit the header for the loop, insert it, which will create an uncond br to
  // it.
  llvm::BasicBlock *LoopHeader = createBasicBlock("while.cond");
  EmitBlock(LoopHeader);
  
  // Evaluate the conditional in the while header.  C99 6.8.5.1: The evaluation
  // of the controlling expression takes place before each execution of the loop
  // body. 
  llvm::Value *BoolCondVal = EvaluateExprAsBool(S.getCond());

  // while(1) is common, avoid extra exit blocks.  Be sure
  // to correctly handle break/continue though.
  bool EmitBoolCondBranch = true;
  if (llvm::ConstantInt *C = dyn_cast<llvm::ConstantInt>(BoolCondVal)) 
    if (C->isOne())
      EmitBoolCondBranch = false;
  
  // Create an exit block for when the condition fails, create a block for the
  // body of the loop.
  llvm::BasicBlock *ExitBlock = createBasicBlock("while.end");
  llvm::BasicBlock *LoopBody  = createBasicBlock("while.body");
  
  // As long as the condition is true, go to the loop body.
  if (EmitBoolCondBranch)
    Builder.CreateCondBr(BoolCondVal, LoopBody, ExitBlock);
  
  // Store the blocks to use for break and continue.
  BreakContinueStack.push_back(BreakContinue(ExitBlock, LoopHeader, 
                                             ObjCEHStack.size()));
  
  // Emit the loop body.
  EmitBlock(LoopBody);
  EmitStmt(S.getBody());

  BreakContinueStack.pop_back();
  
  // Cycle to the condition.
  EmitBranch(LoopHeader);
  
  // Emit the exit block.
  EmitBlock(ExitBlock, true);

  // If LoopHeader is a simple forwarding block then eliminate it.
  if (!EmitBoolCondBranch 
      && &LoopHeader->front() == LoopHeader->getTerminator()) {
    LoopHeader->replaceAllUsesWith(LoopBody);
    LoopHeader->getTerminator()->eraseFromParent();
    LoopHeader->eraseFromParent();
  }
}

void CodeGenFunction::EmitDoStmt(const DoStmt &S) {
  // Emit the body for the loop, insert it, which will create an uncond br to
  // it.
  llvm::BasicBlock *LoopBody = createBasicBlock("do.body");
  llvm::BasicBlock *AfterDo = createBasicBlock("do.end");
  EmitBlock(LoopBody);

  llvm::BasicBlock *DoCond = createBasicBlock("do.cond");
  
  // Store the blocks to use for break and continue.
  BreakContinueStack.push_back(BreakContinue(AfterDo, DoCond, 
                                             ObjCEHStack.size()));
  
  // Emit the body of the loop into the block.
  EmitStmt(S.getBody());
  
  BreakContinueStack.pop_back();
  
  EmitBlock(DoCond);
  
  // C99 6.8.5.2: "The evaluation of the controlling expression takes place
  // after each execution of the loop body."
  
  // Evaluate the conditional in the while header.
  // C99 6.8.5p2/p4: The first substatement is executed if the expression
  // compares unequal to 0.  The condition must be a scalar type.
  llvm::Value *BoolCondVal = EvaluateExprAsBool(S.getCond());

  // "do {} while (0)" is common in macros, avoid extra blocks.  Be sure
  // to correctly handle break/continue though.
  bool EmitBoolCondBranch = true;
  if (llvm::ConstantInt *C = dyn_cast<llvm::ConstantInt>(BoolCondVal)) 
    if (C->isZero())
      EmitBoolCondBranch = false;

  // As long as the condition is true, iterate the loop.
  if (EmitBoolCondBranch)
    Builder.CreateCondBr(BoolCondVal, LoopBody, AfterDo);
  
  // Emit the exit block.
  EmitBlock(AfterDo, true);

  // If DoCond is a simple forwarding block then eliminate it.
  if (!EmitBoolCondBranch && &DoCond->front() == DoCond->getTerminator()) {
    DoCond->replaceAllUsesWith(AfterDo);
    DoCond->getTerminator()->eraseFromParent();
    DoCond->eraseFromParent();
  }
}

void CodeGenFunction::EmitForStmt(const ForStmt &S) {
  // FIXME: What do we do if the increment (f.e.) contains a stmt expression,
  // which contains a continue/break?

  // Evaluate the first part before the loop.
  if (S.getInit())
    EmitStmt(S.getInit());

  // Start the loop with a block that tests the condition.
  llvm::BasicBlock *CondBlock = createBasicBlock("for.cond");
  llvm::BasicBlock *AfterFor = createBasicBlock("for.end");

  EmitBlock(CondBlock);

  // Evaluate the condition if present.  If not, treat it as a non-zero-constant
  // according to 6.8.5.3p2, aka, true.
  if (S.getCond()) {
    // As long as the condition is true, iterate the loop.
    llvm::BasicBlock *ForBody = createBasicBlock("for.body");
    
    // C99 6.8.5p2/p4: The first substatement is executed if the expression
    // compares unequal to 0.  The condition must be a scalar type.
    EmitBranchOnBoolExpr(S.getCond(), ForBody, AfterFor);
    
    EmitBlock(ForBody);    
  } else {
    // Treat it as a non-zero constant.  Don't even create a new block for the
    // body, just fall into it.
  }

  // If the for loop doesn't have an increment we can just use the 
  // condition as the continue block.
  llvm::BasicBlock *ContinueBlock;
  if (S.getInc())
    ContinueBlock = createBasicBlock("for.inc");
  else
    ContinueBlock = CondBlock;  
  
  // Store the blocks to use for break and continue.
  BreakContinueStack.push_back(BreakContinue(AfterFor, ContinueBlock,
                                             ObjCEHStack.size()));
  
  // If the condition is true, execute the body of the for stmt.
  EmitStmt(S.getBody());

  BreakContinueStack.pop_back();
  
  // If there is an increment, emit it next.
  if (S.getInc()) {
    EmitBlock(ContinueBlock);
    EmitStmt(S.getInc());
  }
      
  // Finally, branch back up to the condition for the next iteration.
  EmitBranch(CondBlock);

  // Emit the fall-through block.
  EmitBlock(AfterFor, true);
}

void CodeGenFunction::EmitReturnOfRValue(RValue RV, QualType Ty) {
  if (RV.isScalar()) {
    Builder.CreateStore(RV.getScalarVal(), ReturnValue);
  } else if (RV.isAggregate()) {
    EmitAggregateCopy(ReturnValue, RV.getAggregateAddr(), Ty);
  } else {
    StoreComplexToAddr(RV.getComplexVal(), ReturnValue, false);
  }
  EmitBranch(ReturnBlock);
}

/// EmitReturnStmt - Note that due to GCC extensions, this can have an operand
/// if the function returns void, or may be missing one if the function returns
/// non-void.  Fun stuff :).
void CodeGenFunction::EmitReturnStmt(const ReturnStmt &S) {
  for (unsigned i = 0; i < StackSaveValues.size(); i++) {
    if (StackSaveValues[i]) {
      CGM.ErrorUnsupported(&S, "return inside scope with VLA");
      return;
    }
  }
  
  // Emit the result value, even if unused, to evalute the side effects.
  const Expr *RV = S.getRetValue();
  
  // FIXME: Clean this up by using an LValue for ReturnTemp,
  // EmitStoreThroughLValue, and EmitAnyExpr.
  if (!ReturnValue) {
    // Make sure not to return anything, but evaluate the expression
    // for side effects.
    if (RV)
      EmitAnyExpr(RV);
  } else if (RV == 0) {
    // Do nothing (return value is left uninitialized)
  } else if (!hasAggregateLLVMType(RV->getType())) {
    Builder.CreateStore(EmitScalarExpr(RV), ReturnValue);
  } else if (RV->getType()->isAnyComplexType()) {
    EmitComplexExprIntoAddr(RV, ReturnValue, false);
  } else {
    EmitAggExpr(RV, ReturnValue, false);
  }

  if (!ObjCEHStack.empty()) {
    for (ObjCEHStackType::reverse_iterator i = ObjCEHStack.rbegin(), 
           e = ObjCEHStack.rend(); i != e; ++i) {
      llvm::BasicBlock *ReturnPad = createBasicBlock("return.pad");
      EmitJumpThroughFinally(*i, ReturnPad);
      EmitBlock(ReturnPad);
    }
  } 

  EmitBranch(ReturnBlock);
}

void CodeGenFunction::EmitDeclStmt(const DeclStmt &S) {
  for (DeclStmt::const_decl_iterator I = S.decl_begin(), E = S.decl_end();
       I != E; ++I)
    EmitDecl(**I);
}

void CodeGenFunction::EmitBreakStmt(const BreakStmt &S) {
  assert(!BreakContinueStack.empty() && "break stmt not in a loop or switch!");

  // FIXME: Implement break in @try or @catch blocks.
  if (ObjCEHStack.size() != BreakContinueStack.back().EHStackSize) {
    CGM.ErrorUnsupported(&S, "break inside an Obj-C exception block");
    return;
  }

  for (unsigned i = 0; i < StackSaveValues.size(); i++) {
    if (StackSaveValues[i]) {
      CGM.ErrorUnsupported(&S, "break inside scope with VLA");
      return;
    }
  }
  
  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (HaveInsertPoint())
    EmitStopPoint(&S);
  llvm::BasicBlock *Block = BreakContinueStack.back().BreakBlock;
  EmitBranch(Block);
}

void CodeGenFunction::EmitContinueStmt(const ContinueStmt &S) {
  assert(!BreakContinueStack.empty() && "continue stmt not in a loop!");

  // FIXME: Implement continue in @try or @catch blocks.
  if (ObjCEHStack.size() != BreakContinueStack.back().EHStackSize) {
    CGM.ErrorUnsupported(&S, "continue inside an Obj-C exception block");
    return;
  }

  for (unsigned i = 0; i < StackSaveValues.size(); i++) {
    if (StackSaveValues[i]) {
      CGM.ErrorUnsupported(&S, "continue inside scope with VLA");
      return;
    }
  }
  
  // If this code is reachable then emit a stop point (if generating
  // debug info). We have to do this ourselves because we are on the
  // "simple" statement path.
  if (HaveInsertPoint())
    EmitStopPoint(&S);
  llvm::BasicBlock *Block = BreakContinueStack.back().ContinueBlock;
  EmitBranch(Block);
}

/// EmitCaseStmtRange - If case statement range is not too big then
/// add multiple cases to switch instruction, one for each value within
/// the range. If range is too big then emit "if" condition check.
void CodeGenFunction::EmitCaseStmtRange(const CaseStmt &S) {
  assert(S.getRHS() && "Expected RHS value in CaseStmt");

  llvm::APSInt LHS = S.getLHS()->EvaluateAsInt(getContext());
  llvm::APSInt RHS = S.getRHS()->EvaluateAsInt(getContext());

  // Emit the code for this case. We do this first to make sure it is
  // properly chained from our predecessor before generating the
  // switch machinery to enter this block.
  EmitBlock(createBasicBlock("sw.bb"));
  llvm::BasicBlock *CaseDest = Builder.GetInsertBlock();
  EmitStmt(S.getSubStmt());

  // If range is empty, do nothing.
  if (LHS.isSigned() ? RHS.slt(LHS) : RHS.ult(LHS))
    return;

  llvm::APInt Range = RHS - LHS;
  // FIXME: parameters such as this should not be hardcoded.
  if (Range.ult(llvm::APInt(Range.getBitWidth(), 64))) {
    // Range is small enough to add multiple switch instruction cases.
    for (unsigned i = 0, e = Range.getZExtValue() + 1; i != e; ++i) {
      SwitchInsn->addCase(llvm::ConstantInt::get(LHS), CaseDest);
      LHS++;
    }
    return;
  } 
    
  // The range is too big. Emit "if" condition into a new block,
  // making sure to save and restore the current insertion point.
  llvm::BasicBlock *RestoreBB = Builder.GetInsertBlock();

  // Push this test onto the chain of range checks (which terminates
  // in the default basic block). The switch's default will be changed
  // to the top of this chain after switch emission is complete.
  llvm::BasicBlock *FalseDest = CaseRangeBlock;
  CaseRangeBlock = createBasicBlock("sw.caserange");

  CurFn->getBasicBlockList().push_back(CaseRangeBlock);
  Builder.SetInsertPoint(CaseRangeBlock);

  // Emit range check.
  llvm::Value *Diff = 
    Builder.CreateSub(SwitchInsn->getCondition(), llvm::ConstantInt::get(LHS), 
                      "tmp");
  llvm::Value *Cond = 
    Builder.CreateICmpULE(Diff, llvm::ConstantInt::get(Range), "tmp");
  Builder.CreateCondBr(Cond, CaseDest, FalseDest);

  // Restore the appropriate insertion point.
  if (RestoreBB)
    Builder.SetInsertPoint(RestoreBB);
  else
    Builder.ClearInsertionPoint();
}

void CodeGenFunction::EmitCaseStmt(const CaseStmt &S) {
  if (S.getRHS()) {
    EmitCaseStmtRange(S);
    return;
  }
    
  EmitBlock(createBasicBlock("sw.bb"));
  llvm::BasicBlock *CaseDest = Builder.GetInsertBlock();
  llvm::APSInt CaseVal = S.getLHS()->EvaluateAsInt(getContext());
  SwitchInsn->addCase(llvm::ConstantInt::get(CaseVal), CaseDest);
  EmitStmt(S.getSubStmt());
}

void CodeGenFunction::EmitDefaultStmt(const DefaultStmt &S) {
  llvm::BasicBlock *DefaultBlock = SwitchInsn->getDefaultDest();
  assert(DefaultBlock->empty() && 
         "EmitDefaultStmt: Default block already defined?");
  EmitBlock(DefaultBlock);
  EmitStmt(S.getSubStmt());
}

void CodeGenFunction::EmitSwitchStmt(const SwitchStmt &S) {
  llvm::Value *CondV = EmitScalarExpr(S.getCond());

  // Handle nested switch statements.
  llvm::SwitchInst *SavedSwitchInsn = SwitchInsn;
  llvm::BasicBlock *SavedCRBlock = CaseRangeBlock;

  // Create basic block to hold stuff that comes after switch
  // statement. We also need to create a default block now so that
  // explicit case ranges tests can have a place to jump to on
  // failure.
  llvm::BasicBlock *NextBlock = createBasicBlock("sw.epilog");
  llvm::BasicBlock *DefaultBlock = createBasicBlock("sw.default");
  SwitchInsn = Builder.CreateSwitch(CondV, DefaultBlock);
  CaseRangeBlock = DefaultBlock;

  // Clear the insertion point to indicate we are in unreachable code.
  Builder.ClearInsertionPoint();

  // All break statements jump to NextBlock. If BreakContinueStack is non empty
  // then reuse last ContinueBlock.
  llvm::BasicBlock *ContinueBlock = NULL;
  if (!BreakContinueStack.empty())
    ContinueBlock = BreakContinueStack.back().ContinueBlock;
  BreakContinueStack.push_back(BreakContinue(NextBlock, ContinueBlock,
                                             ObjCEHStack.size()));

  // Emit switch body.
  EmitStmt(S.getBody());
  BreakContinueStack.pop_back();

  // Update the default block in case explicit case range tests have
  // been chained on top.
  SwitchInsn->setSuccessor(0, CaseRangeBlock);
  
  // If a default was never emitted then reroute any jumps to it and
  // discard.
  if (!DefaultBlock->getParent()) {
    DefaultBlock->replaceAllUsesWith(NextBlock);
    delete DefaultBlock;
  }

  // Emit continuation.
  EmitBlock(NextBlock, true);

  SwitchInsn = SavedSwitchInsn;
  CaseRangeBlock = SavedCRBlock;
}

static std::string ConvertAsmString(const AsmStmt& S, bool &Failed)
{
  // FIXME: No need to create new std::string here, we could just make sure
  // that we don't read past the end of the string data.
  std::string str(S.getAsmString()->getStrData(),
                  S.getAsmString()->getByteLength());
  const char *Start = str.c_str();
  
  unsigned NumOperands = S.getNumOutputs() + S.getNumInputs();
  bool IsSimple = S.isSimple();
  Failed = false;

  static unsigned AsmCounter = 0;
  AsmCounter++;
  std::string Result;
  if (IsSimple) {
    while (*Start) {
      switch (*Start) {
      default:
        Result += *Start;
        break;
      case '$':
        Result += "$$";
        break;
      }
      Start++;
    }
    
    return Result;
  }
  
  while (*Start) {
    switch (*Start) {
    default:
      Result += *Start;
      break;
    case '$':
      Result += "$$";
      break;
    case '%':
      // Escaped character
      Start++;
      if (!*Start) {
        // FIXME: This should be caught during Sema.
        assert(0 && "Trailing '%' in asm string.");
      }
      
      char EscapedChar = *Start;
      if (EscapedChar == '%') {
        // Escaped percentage sign.
        Result += '%';
      } else if (EscapedChar == '=') {
        // Generate an unique ID.
        Result += llvm::utostr(AsmCounter);
      } else if (isdigit(EscapedChar)) {
        // %n - Assembler operand n
        char *End;
        unsigned long n = strtoul(Start, &End, 10);
        if (Start == End) {
          // FIXME: This should be caught during Sema.
          assert(0 && "Missing operand!");
        } else if (n >= NumOperands) {
          // FIXME: This should be caught during Sema.
          assert(0 && "Operand number out of range!");
        }
        
        Result += '$' + llvm::utostr(n);
        Start = End - 1;
      } else if (isalpha(EscapedChar)) {
        char *End;
        
        unsigned long n = strtoul(Start + 1, &End, 10);
        if (Start == End) {
          // FIXME: This should be caught during Sema.
          assert(0 && "Missing operand!");
        } else if (n >= NumOperands) {
          // FIXME: This should be caught during Sema.
          assert(0 && "Operand number out of range!");
        }
        
        Result += "${" + llvm::utostr(n) + ':' + EscapedChar + '}';
        Start = End - 1;
      } else if (EscapedChar == '[') {
        std::string SymbolicName;
        
        Start++;
        
        while (*Start && *Start != ']') {
          SymbolicName += *Start;
          
          Start++;
        }
        
        if (!Start) {
          // FIXME: Should be caught by sema.
          assert(0 && "Could not parse symbolic name");
        }
        
        assert(*Start == ']' && "Error parsing symbolic name");
        
        int Index = -1;
        
        // Check if this is an output operand.
        for (unsigned i = 0; i < S.getNumOutputs(); i++) {
          if (S.getOutputName(i) == SymbolicName) {
            Index = i;
            break;
          }
        }
        
        if (Index == -1) {
          for (unsigned i = 0; i < S.getNumInputs(); i++) {
            if (S.getInputName(i) == SymbolicName) {
              Index = S.getNumOutputs() + i;
            }
          }
        }
        
        assert(Index != -1 && "Did not find right operand!");
       
        Result += '$' + llvm::utostr(Index);

      } else {
        Failed = true;
        return "";
      }
    }
    Start++;
  }
  
  return Result;
}

static std::string SimplifyConstraint(const char* Constraint,
                                      TargetInfo &Target,
                                      const std::string *OutputNamesBegin = 0,
                                      const std::string *OutputNamesEnd = 0)
{
  std::string Result;
  
  while (*Constraint) {
    switch (*Constraint) {
    default:
      Result += Target.convertConstraint(*Constraint);
      break;
    // Ignore these
    case '*':
    case '?':
    case '!':
      break;
    case 'g':
      Result += "imr";
      break;
    case '[': {
      assert(OutputNamesBegin && OutputNamesEnd &&
             "Must pass output names to constraints with a symbolic name");
      unsigned Index;
      bool result = Target.resolveSymbolicName(Constraint, 
                                               OutputNamesBegin,
                                               OutputNamesEnd, Index);
      assert(result && "Could not resolve symbolic name"); result=result;
      Result += llvm::utostr(Index);
      break;
    }
    }
    
    Constraint++;
  }
  
  return Result;
}

llvm::Value* CodeGenFunction::EmitAsmInput(const AsmStmt &S,
                                           TargetInfo::ConstraintInfo Info, 
                                           const Expr *InputExpr,
                                           std::string &ConstraintStr)
{
  llvm::Value *Arg;
  if ((Info & TargetInfo::CI_AllowsRegister) ||
      !(Info & TargetInfo::CI_AllowsMemory)) { 
    const llvm::Type *Ty = ConvertType(InputExpr->getType());
    
    if (Ty->isSingleValueType()) {
      Arg = EmitScalarExpr(InputExpr);
    } else {
      LValue Dest = EmitLValue(InputExpr);

      uint64_t Size = CGM.getTargetData().getTypeSizeInBits(Ty);
      if (Size <= 64 && llvm::isPowerOf2_64(Size)) {
        Ty = llvm::IntegerType::get(Size);
        Ty = llvm::PointerType::getUnqual(Ty);
        
        Arg = Builder.CreateLoad(Builder.CreateBitCast(Dest.getAddress(), Ty));
      } else {
        Arg = Dest.getAddress();
        ConstraintStr += '*';
      }
    }
  } else {
    LValue Dest = EmitLValue(InputExpr);
    Arg = Dest.getAddress();
    ConstraintStr += '*';
  }
  
  return Arg;
}

void CodeGenFunction::EmitAsmStmt(const AsmStmt &S) {
  bool Failed;
  std::string AsmString = 
    ConvertAsmString(S, Failed);

  if (Failed) {
    ErrorUnsupported(&S, "asm string");
    return;
  }
  
  std::string Constraints;
  
  llvm::Value *ResultAddr = 0;
  const llvm::Type *ResultType = llvm::Type::VoidTy;
  
  std::vector<const llvm::Type*> ArgTypes;
  std::vector<llvm::Value*> Args;

  // Keep track of inout constraints.
  std::string InOutConstraints;
  std::vector<llvm::Value*> InOutArgs;
  std::vector<const llvm::Type*> InOutArgTypes;

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;

  for (unsigned i = 0, e = S.getNumOutputs(); i != e; i++) {    
    std::string OutputConstraint(S.getOutputConstraint(i)->getStrData(),
                                 S.getOutputConstraint(i)->getByteLength());
    
    TargetInfo::ConstraintInfo Info;
    bool result = Target.validateOutputConstraint(OutputConstraint.c_str(), 
                                                  Info);
    assert(result && "Failed to parse output constraint"); result=result;
    
    OutputConstraintInfos.push_back(Info);

    // Simplify the output constraint.
    OutputConstraint = SimplifyConstraint(OutputConstraint.c_str() + 1, Target);
    
    LValue Dest = EmitLValue(S.getOutputExpr(i));
    const llvm::Type *DestValueType = 
      cast<llvm::PointerType>(Dest.getAddress()->getType())->getElementType();
    
    // If the first output operand is not a memory dest, we'll
    // make it the return value.
    if (i == 0 && !(Info & TargetInfo::CI_AllowsMemory) &&
        DestValueType->isSingleValueType()) {
      ResultAddr = Dest.getAddress();
      ResultType = DestValueType;
      Constraints += "=" + OutputConstraint;
    } else {
      ArgTypes.push_back(Dest.getAddress()->getType());
      Args.push_back(Dest.getAddress());
      if (i != 0)
        Constraints += ',';
      Constraints += "=*";
      Constraints += OutputConstraint;
    }
    
    if (Info & TargetInfo::CI_ReadWrite) {
      InOutConstraints += ',';

      const Expr *InputExpr = S.getOutputExpr(i);
      llvm::Value *Arg = EmitAsmInput(S, Info, InputExpr, InOutConstraints);
      
      if (Info & TargetInfo::CI_AllowsRegister)
        InOutConstraints += llvm::utostr(i);
      else
        InOutConstraints += OutputConstraint;

      InOutArgTypes.push_back(Arg->getType());
      InOutArgs.push_back(Arg);
    }
  }
  
  unsigned NumConstraints = S.getNumOutputs() + S.getNumInputs();
  
  for (unsigned i = 0, e = S.getNumInputs(); i != e; i++) {
    const Expr *InputExpr = S.getInputExpr(i);

    std::string InputConstraint(S.getInputConstraint(i)->getStrData(),
                                S.getInputConstraint(i)->getByteLength());
    
    TargetInfo::ConstraintInfo Info;
    bool result = Target.validateInputConstraint(InputConstraint.c_str(),
                                                 S.begin_output_names(),
                                                 S.end_output_names(),
                                                 &OutputConstraintInfos[0],
                                                 Info); result=result;
    assert(result && "Failed to parse input constraint");
    
    if (i != 0 || S.getNumOutputs() > 0)
      Constraints += ',';
    
    // Simplify the input constraint.
    InputConstraint = SimplifyConstraint(InputConstraint.c_str(), Target,
                                         S.begin_output_names(),
                                         S.end_output_names());

    llvm::Value *Arg = EmitAsmInput(S, Info, InputExpr, Constraints);
    
    ArgTypes.push_back(Arg->getType());
    Args.push_back(Arg);
    Constraints += InputConstraint;
  }
  
  // Append the "input" part of inout constraints last.
  for (unsigned i = 0, e = InOutArgs.size(); i != e; i++) {
    ArgTypes.push_back(InOutArgTypes[i]);
    Args.push_back(InOutArgs[i]);
  }
  Constraints += InOutConstraints;
  
  // Clobbers
  for (unsigned i = 0, e = S.getNumClobbers(); i != e; i++) {
    std::string Clobber(S.getClobber(i)->getStrData(),
                        S.getClobber(i)->getByteLength());

    Clobber = Target.getNormalizedGCCRegisterName(Clobber.c_str());
    
    if (i != 0 || NumConstraints != 0)
      Constraints += ',';
    
    Constraints += "~{";
    Constraints += Clobber;
    Constraints += '}';
  }
  
  // Add machine specific clobbers
  std::string MachineClobbers = Target.getClobbers();
  if (!MachineClobbers.empty()) {
    if (!Constraints.empty())
      Constraints += ',';
    Constraints += MachineClobbers;
  }
    
  const llvm::FunctionType *FTy = 
    llvm::FunctionType::get(ResultType, ArgTypes, false);
  
  llvm::InlineAsm *IA = 
    llvm::InlineAsm::get(FTy, AsmString, Constraints, 
                         S.isVolatile() || S.getNumOutputs() == 0);
  llvm::Value *Result = Builder.CreateCall(IA, Args.begin(), Args.end(), "");
  if (ResultAddr) // FIXME: volatility
    Builder.CreateStore(Result, ResultAddr);
}
