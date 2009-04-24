//===--- CodeGenFunction.cpp - Emit LLVM Code from ASTs for a Function ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This coordinates the per-function state used while generating code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "CGDebugInfo.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "llvm/Support/CFG.h"
using namespace clang;
using namespace CodeGen;

CodeGenFunction::CodeGenFunction(CodeGenModule &cgm) 
  : CGM(cgm), Target(CGM.getContext().Target), SwitchInsn(NULL), 
    CaseRangeBlock(NULL) {
    LLVMIntTy = ConvertType(getContext().IntTy);
    LLVMPointerWidth = Target.getPointerWidth(0);
}

ASTContext &CodeGenFunction::getContext() const {
  return CGM.getContext();
}


llvm::BasicBlock *CodeGenFunction::getBasicBlockForLabel(const LabelStmt *S) {
  llvm::BasicBlock *&BB = LabelMap[S];
  if (BB) return BB;
  
  // Create, but don't insert, the new block.
  return BB = createBasicBlock(S->getName());
}

llvm::Constant *
CodeGenFunction::GetAddrOfStaticLocalVar(const VarDecl *BVD) {
  return cast<llvm::Constant>(LocalDeclMap[BVD]);
}

llvm::Value *CodeGenFunction::GetAddrOfLocalVar(const VarDecl *VD)
{
  return LocalDeclMap[VD];
}

const llvm::Type *CodeGenFunction::ConvertType(QualType T) {
  return CGM.getTypes().ConvertType(T);
}

bool CodeGenFunction::isObjCPointerType(QualType T) {
  // All Objective-C types are pointers.
  return T->isObjCInterfaceType() ||
    T->isObjCQualifiedInterfaceType() || T->isObjCQualifiedIdType();
}

bool CodeGenFunction::hasAggregateLLVMType(QualType T) {
  // FIXME: Use positive checks instead of negative ones to be more
  // robust in the face of extension.
  return !isObjCPointerType(T) &&!T->isRealType() && !T->isPointerLikeType() &&
    !T->isVoidType() && !T->isVectorType() && !T->isFunctionType() && 
    !T->isBlockPointerType();
}

void CodeGenFunction::EmitReturnBlock() {
  // For cleanliness, we try to avoid emitting the return block for
  // simple cases.
  llvm::BasicBlock *CurBB = Builder.GetInsertBlock();

  if (CurBB) {
    assert(!CurBB->getTerminator() && "Unexpected terminated block.");

    // We have a valid insert point, reuse it if there are no explicit
    // jumps to the return block.
    if (ReturnBlock->use_empty())
      delete ReturnBlock;
    else
      EmitBlock(ReturnBlock);
    return;
  }

  // Otherwise, if the return block is the target of a single direct
  // branch then we can just put the code in that block instead. This
  // cleans up functions which started with a unified return block.
  if (ReturnBlock->hasOneUse()) {
    llvm::BranchInst *BI = 
      dyn_cast<llvm::BranchInst>(*ReturnBlock->use_begin());
    if (BI && BI->isUnconditional() && BI->getSuccessor(0) == ReturnBlock) {
      // Reset insertion point and delete the branch.
      Builder.SetInsertPoint(BI->getParent());
      BI->eraseFromParent();
      delete ReturnBlock;
      return;
    }
  }

  // FIXME: We are at an unreachable point, there is no reason to emit
  // the block unless it has uses. However, we still need a place to
  // put the debug region.end for now.

  EmitBlock(ReturnBlock);
}

void CodeGenFunction::FinishFunction(SourceLocation EndLoc) {
  // Finish emission of indirect switches.
  EmitIndirectSwitches();

  assert(BreakContinueStack.empty() &&
         "mismatched push/pop in break/continue stack!");

  // Emit function epilog (to return). 
  EmitReturnBlock();

  // Emit debug descriptor for function end.
  if (CGDebugInfo *DI = CGM.getDebugInfo()) {
    DI->setLocation(EndLoc);
    DI->EmitRegionEnd(CurFn, Builder);
  }

  EmitFunctionEpilog(*CurFnInfo, ReturnValue);

  // Remove the AllocaInsertPt instruction, which is just a convenience for us.
  AllocaInsertPt->eraseFromParent();
  AllocaInsertPt = 0;
}

void CodeGenFunction::StartFunction(const Decl *D, QualType RetTy, 
                                    llvm::Function *Fn,
                                    const FunctionArgList &Args,
                                    SourceLocation StartLoc) {
  CurFuncDecl = D;
  FnRetTy = RetTy;
  CurFn = Fn;
  assert(CurFn->isDeclaration() && "Function already has body?");

  llvm::BasicBlock *EntryBB = createBasicBlock("entry", CurFn);

  // Create a marker to make it easy to insert allocas into the entryblock
  // later.  Don't create this with the builder, because we don't want it
  // folded.
  llvm::Value *Undef = llvm::UndefValue::get(llvm::Type::Int32Ty);
  AllocaInsertPt = new llvm::BitCastInst(Undef, llvm::Type::Int32Ty, "allocapt",
                                         EntryBB);

  ReturnBlock = createBasicBlock("return");
  ReturnValue = 0;
  if (!RetTy->isVoidType())
    ReturnValue = CreateTempAlloca(ConvertType(RetTy), "retval");
    
  Builder.SetInsertPoint(EntryBB);
  
  // Emit subprogram debug descriptor.
  // FIXME: The cast here is a huge hack.
  if (CGDebugInfo *DI = CGM.getDebugInfo()) {
    DI->setLocation(StartLoc);
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      DI->EmitFunctionStart(FD->getIdentifier()->getName(),
                            RetTy, CurFn, Builder);
    } else {
      // Just use LLVM function name.
      DI->EmitFunctionStart(Fn->getName().c_str(), 
                            RetTy, CurFn, Builder);
    }
  }

  // FIXME: Leaked.
  CurFnInfo = &CGM.getTypes().getFunctionInfo(FnRetTy, Args);
  EmitFunctionProlog(*CurFnInfo, CurFn, Args);
  
  // If any of the arguments have a variably modified type, make sure to
  // emit the type size.
  for (FunctionArgList::const_iterator i = Args.begin(), e = Args.end();
       i != e; ++i) {
    QualType Ty = i->second;

    if (Ty->isVariablyModifiedType())
      EmitVLASize(Ty);
  }
}

void CodeGenFunction::GenerateCode(const FunctionDecl *FD,
                                   llvm::Function *Fn) {
  FunctionArgList Args;
  if (FD->getNumParams()) {
    const FunctionTypeProto* FProto = FD->getType()->getAsFunctionTypeProto();
    assert(FProto && "Function def must have prototype!");

    for (unsigned i = 0, e = FD->getNumParams(); i != e; ++i)
      Args.push_back(std::make_pair(FD->getParamDecl(i), 
                                    FProto->getArgType(i)));
  }

  StartFunction(FD, FD->getResultType(), Fn, Args,
                cast<CompoundStmt>(FD->getBody())->getLBracLoc());

  EmitStmt(FD->getBody());
  
  const CompoundStmt *S = dyn_cast<CompoundStmt>(FD->getBody());
  if (S) {
    FinishFunction(S->getRBracLoc());
  } else {
    FinishFunction();
  }
}

/// ContainsLabel - Return true if the statement contains a label in it.  If
/// this statement is not executed normally, it not containing a label means
/// that we can just remove the code.
bool CodeGenFunction::ContainsLabel(const Stmt *S, bool IgnoreCaseStmts) {
  // Null statement, not a label!
  if (S == 0) return false;
  
  // If this is a label, we have to emit the code, consider something like:
  // if (0) {  ...  foo:  bar(); }  goto foo;
  if (isa<LabelStmt>(S))
    return true;
  
  // If this is a case/default statement, and we haven't seen a switch, we have
  // to emit the code.
  if (isa<SwitchCase>(S) && !IgnoreCaseStmts)
    return true;
  
  // If this is a switch statement, we want to ignore cases below it.
  if (isa<SwitchStmt>(S))
    IgnoreCaseStmts = true;
  
  // Scan subexpressions for verboten labels.
  for (Stmt::const_child_iterator I = S->child_begin(), E = S->child_end();
       I != E; ++I)
    if (ContainsLabel(*I, IgnoreCaseStmts))
      return true;
  
  return false;
}


/// ConstantFoldsToSimpleInteger - If the sepcified expression does not fold to
/// a constant, or if it does but contains a label, return 0.  If it constant
/// folds to 'true' and does not contain a label, return 1, if it constant folds
/// to 'false' and does not contain a label, return -1.
int CodeGenFunction::ConstantFoldsToSimpleInteger(const Expr *Cond) {
  // FIXME: Rename and handle conversion of other evaluatable things
  // to bool.
  Expr::EvalResult Result;
  if (!Cond->Evaluate(Result, getContext()) || !Result.Val.isInt() || 
      Result.HasSideEffects)
    return 0;  // Not foldable, not integer or not fully evaluatable.
  
  if (CodeGenFunction::ContainsLabel(Cond))
    return 0;  // Contains a label.
  
  return Result.Val.getInt().getBoolValue() ? 1 : -1;
}


/// EmitBranchOnBoolExpr - Emit a branch on a boolean condition (e.g. for an if
/// statement) to the specified blocks.  Based on the condition, this might try
/// to simplify the codegen of the conditional based on the branch.
///
void CodeGenFunction::EmitBranchOnBoolExpr(const Expr *Cond,
                                           llvm::BasicBlock *TrueBlock,
                                           llvm::BasicBlock *FalseBlock) {
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(Cond))
    return EmitBranchOnBoolExpr(PE->getSubExpr(), TrueBlock, FalseBlock);
  
  if (const BinaryOperator *CondBOp = dyn_cast<BinaryOperator>(Cond)) {
    // Handle X && Y in a condition.
    if (CondBOp->getOpcode() == BinaryOperator::LAnd) {
      // If we have "1 && X", simplify the code.  "0 && X" would have constant
      // folded if the case was simple enough.
      if (ConstantFoldsToSimpleInteger(CondBOp->getLHS()) == 1) {
        // br(1 && X) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock);
      }
      
      // If we have "X && 1", simplify the code to use an uncond branch.
      // "X && 0" would have been constant folded to 0.
      if (ConstantFoldsToSimpleInteger(CondBOp->getRHS()) == 1) {
        // br(X && 1) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock);
      }
      
      // Emit the LHS as a conditional.  If the LHS conditional is false, we
      // want to jump to the FalseBlock.
      llvm::BasicBlock *LHSTrue = createBasicBlock("land.lhs.true");
      EmitBranchOnBoolExpr(CondBOp->getLHS(), LHSTrue, FalseBlock);
      EmitBlock(LHSTrue);
      
      EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock);
      return;
    } else if (CondBOp->getOpcode() == BinaryOperator::LOr) {
      // If we have "0 || X", simplify the code.  "1 || X" would have constant
      // folded if the case was simple enough.
      if (ConstantFoldsToSimpleInteger(CondBOp->getLHS()) == -1) {
        // br(0 || X) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock);
      }
      
      // If we have "X || 0", simplify the code to use an uncond branch.
      // "X || 1" would have been constant folded to 1.
      if (ConstantFoldsToSimpleInteger(CondBOp->getRHS()) == -1) {
        // br(X || 0) -> br(X).
        return EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, FalseBlock);
      }
      
      // Emit the LHS as a conditional.  If the LHS conditional is true, we
      // want to jump to the TrueBlock.
      llvm::BasicBlock *LHSFalse = createBasicBlock("lor.lhs.false");
      EmitBranchOnBoolExpr(CondBOp->getLHS(), TrueBlock, LHSFalse);
      EmitBlock(LHSFalse);
      
      EmitBranchOnBoolExpr(CondBOp->getRHS(), TrueBlock, FalseBlock);
      return;
    }
  }
  
  if (const UnaryOperator *CondUOp = dyn_cast<UnaryOperator>(Cond)) {
    // br(!x, t, f) -> br(x, f, t)
    if (CondUOp->getOpcode() == UnaryOperator::LNot)
      return EmitBranchOnBoolExpr(CondUOp->getSubExpr(), FalseBlock, TrueBlock);
  }
  
  if (const ConditionalOperator *CondOp = dyn_cast<ConditionalOperator>(Cond)) {
    // Handle ?: operator.

    // Just ignore GNU ?: extension.
    if (CondOp->getLHS()) {
      // br(c ? x : y, t, f) -> br(c, br(x, t, f), br(y, t, f))
      llvm::BasicBlock *LHSBlock = createBasicBlock("cond.true");
      llvm::BasicBlock *RHSBlock = createBasicBlock("cond.false");
      EmitBranchOnBoolExpr(CondOp->getCond(), LHSBlock, RHSBlock);
      EmitBlock(LHSBlock);
      EmitBranchOnBoolExpr(CondOp->getLHS(), TrueBlock, FalseBlock);
      EmitBlock(RHSBlock);
      EmitBranchOnBoolExpr(CondOp->getRHS(), TrueBlock, FalseBlock);
      return;
    }
  }

  // Emit the code with the fully general case.
  llvm::Value *CondV = EvaluateExprAsBool(Cond);
  Builder.CreateCondBr(CondV, TrueBlock, FalseBlock);
}

/// getCGRecordLayout - Return record layout info.
const CGRecordLayout *CodeGenFunction::getCGRecordLayout(CodeGenTypes &CGT,
                                                         QualType Ty) {
  const RecordType *RTy = Ty->getAsRecordType();
  assert (RTy && "Unexpected type. RecordType expected here.");

  return CGT.getCGRecordLayout(RTy->getDecl());
}

/// ErrorUnsupported - Print out an error that codegen doesn't support the
/// specified stmt yet.
void CodeGenFunction::ErrorUnsupported(const Stmt *S, const char *Type,
                                       bool OmitOnError) {
  CGM.ErrorUnsupported(S, Type, OmitOnError);
}

unsigned CodeGenFunction::GetIDForAddrOfLabel(const LabelStmt *L) {
  // Use LabelIDs.size() as the new ID if one hasn't been assigned.
  return LabelIDs.insert(std::make_pair(L, LabelIDs.size())).first->second;
}

void CodeGenFunction::EmitMemSetToZero(llvm::Value *DestPtr, QualType Ty)
{
  const llvm::Type *BP = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  if (DestPtr->getType() != BP)
    DestPtr = Builder.CreateBitCast(DestPtr, BP, "tmp");

  // Get size and alignment info for this aggregate.
  std::pair<uint64_t, unsigned> TypeInfo = getContext().getTypeInfo(Ty);

  // FIXME: Handle variable sized types.
  const llvm::Type *IntPtr = llvm::IntegerType::get(LLVMPointerWidth);

  Builder.CreateCall4(CGM.getMemSetFn(), DestPtr,
                      llvm::ConstantInt::getNullValue(llvm::Type::Int8Ty),
                      // TypeInfo.first describes size in bits.
                      llvm::ConstantInt::get(IntPtr, TypeInfo.first/8),
                      llvm::ConstantInt::get(llvm::Type::Int32Ty, 
                                             TypeInfo.second/8));
}

void CodeGenFunction::EmitIndirectSwitches() {
  llvm::BasicBlock *Default;
  
  if (IndirectSwitches.empty())
    return;
  
  if (!LabelIDs.empty()) {
    Default = getBasicBlockForLabel(LabelIDs.begin()->first);
  } else {
    // No possible targets for indirect goto, just emit an infinite
    // loop.
    Default = createBasicBlock("indirectgoto.loop", CurFn);
    llvm::BranchInst::Create(Default, Default);
  }

  for (std::vector<llvm::SwitchInst*>::iterator i = IndirectSwitches.begin(),
         e = IndirectSwitches.end(); i != e; ++i) {
    llvm::SwitchInst *I = *i;
    
    I->setSuccessor(0, Default);
    for (std::map<const LabelStmt*,unsigned>::iterator LI = LabelIDs.begin(), 
           LE = LabelIDs.end(); LI != LE; ++LI) {
      I->addCase(llvm::ConstantInt::get(llvm::Type::Int32Ty,
                                        LI->second), 
                 getBasicBlockForLabel(LI->first));
    }
  }         
}

llvm::Value *CodeGenFunction::EmitVAArg(llvm::Value *VAListAddr, QualType Ty)
{
  // FIXME: This entire method is hardcoded for 32-bit X86.
  
  const char *TargetPrefix = getContext().Target.getTargetPrefix();
  
  if (strcmp(TargetPrefix, "x86") != 0 ||
      getContext().Target.getPointerWidth(0) != 32)
    return 0;
  
  const llvm::Type *BP = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  const llvm::Type *BPP = llvm::PointerType::getUnqual(BP);

  llvm::Value *VAListAddrAsBPP = Builder.CreateBitCast(VAListAddr, BPP, 
                                                       "ap");
  llvm::Value *Addr = Builder.CreateLoad(VAListAddrAsBPP, "ap.cur");
  llvm::Value *AddrTyped = 
    Builder.CreateBitCast(Addr, 
                          llvm::PointerType::getUnqual(ConvertType(Ty)));
  
  uint64_t SizeInBytes = getContext().getTypeSize(Ty) / 8;
  const unsigned ArgumentSizeInBytes = 4;
  if (SizeInBytes < ArgumentSizeInBytes)
    SizeInBytes = ArgumentSizeInBytes;

  llvm::Value *NextAddr = 
    Builder.CreateGEP(Addr, 
                      llvm::ConstantInt::get(llvm::Type::Int32Ty, SizeInBytes),
                      "ap.next");
  Builder.CreateStore(NextAddr, VAListAddrAsBPP);

  return AddrTyped;
}


llvm::Value *CodeGenFunction::GetVLASize(const VariableArrayType *VAT)
{
  llvm::Value *&SizeEntry = VLASizeMap[VAT];
  
  assert(SizeEntry && "Did not emit size for type");
  return SizeEntry;
}

llvm::Value *CodeGenFunction::EmitVLASize(QualType Ty)
{
  assert(Ty->isVariablyModifiedType() &&
         "Must pass variably modified type to EmitVLASizes!");
  
  if (const VariableArrayType *VAT = getContext().getAsVariableArrayType(Ty)) {
    llvm::Value *&SizeEntry = VLASizeMap[VAT];
    
    if (!SizeEntry) {
      // Get the element size;
      llvm::Value *ElemSize;
    
      QualType ElemTy = VAT->getElementType();
    
      if (ElemTy->isVariableArrayType())
        ElemSize = EmitVLASize(ElemTy);
      else {
        // FIXME: We use Int32Ty here because the alloca instruction takes a
        // 32-bit integer. What should we do about overflow?
        ElemSize = llvm::ConstantInt::get(llvm::Type::Int32Ty, 
                                          getContext().getTypeSize(ElemTy) / 8);
      }
    
      llvm::Value *NumElements = EmitScalarExpr(VAT->getSizeExpr());
    
      SizeEntry = Builder.CreateMul(ElemSize, NumElements);
    }
    
    return SizeEntry;
  } else if (const PointerType *PT = Ty->getAsPointerType())
    EmitVLASize(PT->getPointeeType());
  else {
    assert(0 && "unknown VM type!");
  }
  
  return 0;
}

llvm::Value* CodeGenFunction::EmitVAListRef(const Expr* E) {
  if (CGM.getContext().getBuiltinVaListType()->isArrayType()) {
    return EmitScalarExpr(E);
  }
  return EmitLValue(E).getAddress();
}
