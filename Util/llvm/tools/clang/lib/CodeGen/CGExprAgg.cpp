//===--- CGExprAgg.cpp - Emit LLVM Code from Aggregate Expressions --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Aggregate Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Intrinsics.h"
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                        Aggregate Expression Emitter
//===----------------------------------------------------------------------===//

namespace  {
class VISIBILITY_HIDDEN AggExprEmitter : public StmtVisitor<AggExprEmitter> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;
  llvm::Value *DestPtr;
  bool VolatileDest;
public:
  AggExprEmitter(CodeGenFunction &cgf, llvm::Value *destPtr, bool volatileDest)
    : CGF(cgf), Builder(CGF.Builder),
      DestPtr(destPtr), VolatileDest(volatileDest) {
  }

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  /// EmitAggLoadOfLValue - Given an expression with aggregate type that
  /// represents a value lvalue, this method emits the address of the lvalue,
  /// then loads the result into DestPtr.
  void EmitAggLoadOfLValue(const Expr *E);
  
  void EmitNonConstInit(InitListExpr *E);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//
  
  void VisitStmt(Stmt *S) {
    CGF.ErrorUnsupported(S, "aggregate expression");
  }
  void VisitParenExpr(ParenExpr *PE) { Visit(PE->getSubExpr()); }
  void VisitUnaryExtension(UnaryOperator *E) { Visit(E->getSubExpr()); }

  // l-values.
  void VisitDeclRefExpr(DeclRefExpr *DRE) { EmitAggLoadOfLValue(DRE); }
  void VisitMemberExpr(MemberExpr *ME) { EmitAggLoadOfLValue(ME); }
  void VisitUnaryDeref(UnaryOperator *E) { EmitAggLoadOfLValue(E); }
  void VisitStringLiteral(StringLiteral *E) { EmitAggLoadOfLValue(E); }
  void VisitCompoundLiteralExpr(CompoundLiteralExpr *E)
      { EmitAggLoadOfLValue(E); }

  void VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    EmitAggLoadOfLValue(E);
  }
  
  // Operators.
  //  case Expr::UnaryOperatorClass:
  //  case Expr::CastExprClass: 
  void VisitCStyleCastExpr(CStyleCastExpr *E);
  void VisitImplicitCastExpr(ImplicitCastExpr *E);
  void VisitCallExpr(const CallExpr *E);
  void VisitStmtExpr(const StmtExpr *E);
  void VisitBinaryOperator(const BinaryOperator *BO);
  void VisitBinAssign(const BinaryOperator *E);
  void VisitOverloadExpr(const OverloadExpr *E);
  void VisitBinComma(const BinaryOperator *E);

  void VisitObjCMessageExpr(ObjCMessageExpr *E);
  void VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
    EmitAggLoadOfLValue(E);
  }
  void VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E);
  void VisitObjCKVCRefExpr(ObjCKVCRefExpr *E);
  
  void VisitConditionalOperator(const ConditionalOperator *CO);
  void VisitInitListExpr(InitListExpr *E);
  void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE) {
    Visit(DAE->getExpr());
  }
  void VisitVAArgExpr(VAArgExpr *E);

  void EmitInitializationToLValue(Expr *E, LValue Address);
  void EmitNullInitializationToLValue(LValue Address, QualType T);
  //  case Expr::ChooseExprClass:

};
}  // end anonymous namespace.

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

/// EmitAggLoadOfLValue - Given an expression with aggregate type that
/// represents a value lvalue, this method emits the address of the lvalue,
/// then loads the result into DestPtr.
void AggExprEmitter::EmitAggLoadOfLValue(const Expr *E) {
  LValue LV = CGF.EmitLValue(E);
  assert(LV.isSimple() && "Can't have aggregate bitfield, vector, etc");
  llvm::Value *SrcPtr = LV.getAddress();
  
  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;

  CGF.EmitAggregateCopy(DestPtr, SrcPtr, E->getType());
}

//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

void AggExprEmitter::VisitCStyleCastExpr(CStyleCastExpr *E) {
  // GCC union extension
  if (E->getType()->isUnionType()) {
    RecordDecl *SD = E->getType()->getAsRecordType()->getDecl();
    LValue FieldLoc = CGF.EmitLValueForField(DestPtr, *SD->field_begin(), true, 0);
    EmitInitializationToLValue(E->getSubExpr(), FieldLoc);
    return;
  }

  Visit(E->getSubExpr());
}

void AggExprEmitter::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  assert(CGF.getContext().typesAreCompatible(
                          E->getSubExpr()->getType().getUnqualifiedType(),
                          E->getType().getUnqualifiedType()) &&
         "Implicit cast types must be compatible");
  Visit(E->getSubExpr());
}

void AggExprEmitter::VisitCallExpr(const CallExpr *E) {
  RValue RV = CGF.EmitCallExpr(E);
  assert(RV.isAggregate() && "Return value must be aggregate value!");
  
  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;
  
  CGF.EmitAggregateCopy(DestPtr, RV.getAggregateAddr(), E->getType());
}

void AggExprEmitter::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  RValue RV = CGF.EmitObjCMessageExpr(E);
  assert(RV.isAggregate() && "Return value must be aggregate value!");

  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;
  
  CGF.EmitAggregateCopy(DestPtr, RV.getAggregateAddr(), E->getType());
}

void AggExprEmitter::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  RValue RV = CGF.EmitObjCPropertyGet(E);
  assert(RV.isAggregate() && "Return value must be aggregate value!");
  
  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;
  
  CGF.EmitAggregateCopy(DestPtr, RV.getAggregateAddr(), E->getType());
}

void AggExprEmitter::VisitObjCKVCRefExpr(ObjCKVCRefExpr *E) {
  RValue RV = CGF.EmitObjCPropertyGet(E);
  assert(RV.isAggregate() && "Return value must be aggregate value!");
  
  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;
  
  CGF.EmitAggregateCopy(DestPtr, RV.getAggregateAddr(), E->getType());
}

void AggExprEmitter::VisitOverloadExpr(const OverloadExpr *E) {
  RValue RV = CGF.EmitCallExpr(E->getFn(), E->arg_begin(),
                               E->arg_end(CGF.getContext()));
  
  assert(RV.isAggregate() && "Return value must be aggregate value!");
  
  // If the result is ignored, don't copy from the value.
  if (DestPtr == 0)
    // FIXME: If the source is volatile, we must read from it.
    return;
  
  CGF.EmitAggregateCopy(DestPtr, RV.getAggregateAddr(), E->getType());
}

void AggExprEmitter::VisitBinComma(const BinaryOperator *E) {
  CGF.EmitAnyExpr(E->getLHS());
  CGF.EmitAggExpr(E->getRHS(), DestPtr, false);
}

void AggExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  CGF.EmitCompoundStmt(*E->getSubStmt(), true, DestPtr, VolatileDest);
}

void AggExprEmitter::VisitBinaryOperator(const BinaryOperator *E) {
  CGF.ErrorUnsupported(E, "aggregate binary expression");
}

void AggExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  // For an assignment to work, the value on the right has
  // to be compatible with the value on the left.
  assert(CGF.getContext().typesAreCompatible(
             E->getLHS()->getType().getUnqualifiedType(),
             E->getRHS()->getType().getUnqualifiedType())
         && "Invalid assignment");
  LValue LHS = CGF.EmitLValue(E->getLHS());

  // We have to special case property setters, otherwise we must have
  // a simple lvalue (no aggregates inside vectors, bitfields).
  if (LHS.isPropertyRef()) {
    // FIXME: Volatility?
    llvm::Value *AggLoc = DestPtr;
    if (!AggLoc)
      AggLoc = CGF.CreateTempAlloca(CGF.ConvertType(E->getRHS()->getType()));
    CGF.EmitAggExpr(E->getRHS(), AggLoc, false);
    CGF.EmitObjCPropertySet(LHS.getPropertyRefExpr(), 
                            RValue::getAggregate(AggLoc));
  } 
  else if (LHS.isKVCRef()) {
    // FIXME: Volatility?
    llvm::Value *AggLoc = DestPtr;
    if (!AggLoc)
      AggLoc = CGF.CreateTempAlloca(CGF.ConvertType(E->getRHS()->getType()));
    CGF.EmitAggExpr(E->getRHS(), AggLoc, false);
    CGF.EmitObjCPropertySet(LHS.getKVCRefExpr(), 
                            RValue::getAggregate(AggLoc));
  } else {
    // Codegen the RHS so that it stores directly into the LHS.
    CGF.EmitAggExpr(E->getRHS(), LHS.getAddress(), false /*FIXME: VOLATILE LHS*/);
    
    if (DestPtr == 0)
      return;
    
    // If the result of the assignment is used, copy the RHS there also.
    CGF.EmitAggregateCopy(DestPtr, LHS.getAddress(), E->getType());
  }
}

void AggExprEmitter::VisitConditionalOperator(const ConditionalOperator *E) {
  llvm::BasicBlock *LHSBlock = CGF.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("cond.end");
  
  llvm::Value *Cond = CGF.EvaluateExprAsBool(E->getCond());
  Builder.CreateCondBr(Cond, LHSBlock, RHSBlock);
  
  CGF.EmitBlock(LHSBlock);
  
  // Handle the GNU extension for missing LHS.
  assert(E->getLHS() && "Must have LHS for aggregate value");

  Visit(E->getLHS());
  CGF.EmitBranch(ContBlock);
  
  CGF.EmitBlock(RHSBlock);
  
  Visit(E->getRHS());
  CGF.EmitBranch(ContBlock);
  
  CGF.EmitBlock(ContBlock);
}

void AggExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  llvm::Value *ArgValue = CGF.EmitLValue(VE->getSubExpr()).getAddress();
  llvm::Value *ArgPtr = CGF.EmitVAArg(ArgValue, VE->getType());

  if (!ArgPtr) {
    CGF.ErrorUnsupported(VE, "aggregate va_arg expression");
    return;
  }

  if (DestPtr)
    // FIXME: volatility
    CGF.EmitAggregateCopy(DestPtr, ArgPtr, VE->getType());
}

void AggExprEmitter::EmitNonConstInit(InitListExpr *E) {
  const llvm::PointerType *APType =
    cast<llvm::PointerType>(DestPtr->getType());
  const llvm::Type *DestType = APType->getElementType();

  if (E->hadArrayRangeDesignator()) {
    CGF.ErrorUnsupported(E, "GNU array range designator extension");
  }

  if (const llvm::ArrayType *AType = dyn_cast<llvm::ArrayType>(DestType)) {
    unsigned NumInitElements = E->getNumInits();

    unsigned i;
    for (i = 0; i != NumInitElements; ++i) {
      llvm::Value *NextVal = Builder.CreateStructGEP(DestPtr, i, ".array");
      Expr *Init = E->getInit(i);
      if (isa<InitListExpr>(Init))
        CGF.EmitAggExpr(Init, NextVal, VolatileDest);
      else
        // FIXME: volatility
        Builder.CreateStore(CGF.EmitScalarExpr(Init), NextVal);
    }

    // Emit remaining default initializers
    unsigned NumArrayElements = AType->getNumElements();
    QualType QType = E->getInit(0)->getType();
    const llvm::Type *EType = AType->getElementType();
    for (/*Do not initialize i*/; i < NumArrayElements; ++i) {
      llvm::Value *NextVal = Builder.CreateStructGEP(DestPtr, i, ".array");
      if (EType->isSingleValueType())
        // FIXME: volatility
        Builder.CreateStore(llvm::Constant::getNullValue(EType), NextVal);
      else
        CGF.EmitAggregateClear(NextVal, QType);
    }
  } else
    assert(false && "Invalid initializer");
}

void AggExprEmitter::EmitInitializationToLValue(Expr* E, LValue LV) {
  // FIXME: Are initializers affected by volatile?
  if (isa<ImplicitValueInitExpr>(E)) {
    EmitNullInitializationToLValue(LV, E->getType());
  } else if (E->getType()->isComplexType()) {
    CGF.EmitComplexExprIntoAddr(E, LV.getAddress(), false);
  } else if (CGF.hasAggregateLLVMType(E->getType())) {
    CGF.EmitAnyExpr(E, LV.getAddress(), false);
  } else {
    CGF.EmitStoreThroughLValue(CGF.EmitAnyExpr(E), LV, E->getType());
  }
}

void AggExprEmitter::EmitNullInitializationToLValue(LValue LV, QualType T) {
  if (!CGF.hasAggregateLLVMType(T)) {
    // For non-aggregates, we can store zero
    llvm::Value *Null = llvm::Constant::getNullValue(CGF.ConvertType(T));
    CGF.EmitStoreThroughLValue(RValue::get(Null), LV, T);
  } else {
    // Otherwise, just memset the whole thing to zero.  This is legal
    // because in LLVM, all default initializers are guaranteed to have a
    // bit pattern of all zeros.
    // There's a potential optimization opportunity in combining
    // memsets; that would be easy for arrays, but relatively
    // difficult for structures with the current code.
    const llvm::Type *SizeTy = llvm::Type::Int64Ty;
    llvm::Value *MemSet = CGF.CGM.getIntrinsic(llvm::Intrinsic::memset,
                                               &SizeTy, 1);
    uint64_t Size = CGF.getContext().getTypeSize(T);
    
    const llvm::Type *BP = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
    llvm::Value* DestPtr = Builder.CreateBitCast(LV.getAddress(), BP, "tmp");
    Builder.CreateCall4(MemSet, DestPtr, 
                        llvm::ConstantInt::get(llvm::Type::Int8Ty, 0),
                        llvm::ConstantInt::get(SizeTy, Size/8),
                        llvm::ConstantInt::get(llvm::Type::Int32Ty, 0));
  }
}

void AggExprEmitter::VisitInitListExpr(InitListExpr *E) {
#if 0
  // FIXME: Disabled while we figure out what to do about 
  // test/CodeGen/bitfield.c
  //
  // If we can, prefer a copy from a global; this is a lot less
  // code for long globals, and it's easier for the current optimizers
  // to analyze.
  // FIXME: Should we really be doing this? Should we try to avoid
  // cases where we emit a global with a lot of zeros?  Should
  // we try to avoid short globals? 
  if (E->isConstantInitializer(CGF.getContext(), 0)) {
    llvm::Constant* C = CGF.CGM.EmitConstantExpr(E, &CGF);
    llvm::GlobalVariable* GV =
    new llvm::GlobalVariable(C->getType(), true,
                             llvm::GlobalValue::InternalLinkage,
                             C, "", &CGF.CGM.getModule(), 0);
    CGF.EmitAggregateCopy(DestPtr, GV, E->getType());
    return;
  }
#endif
  if (E->hadArrayRangeDesignator()) {
    CGF.ErrorUnsupported(E, "GNU array range designator extension");
  }

  // Handle initialization of an array.
  if (E->getType()->isArrayType()) {
    const llvm::PointerType *APType =
      cast<llvm::PointerType>(DestPtr->getType());
    const llvm::ArrayType *AType =
      cast<llvm::ArrayType>(APType->getElementType());
    
    uint64_t NumInitElements = E->getNumInits();

    if (E->getNumInits() > 0) {
      QualType T1 = E->getType();
      QualType T2 = E->getInit(0)->getType();
      if (CGF.getContext().getCanonicalType(T1).getUnqualifiedType() == 
          CGF.getContext().getCanonicalType(T2).getUnqualifiedType()) {
        EmitAggLoadOfLValue(E->getInit(0));
        return;
      }
    }

    uint64_t NumArrayElements = AType->getNumElements();
    QualType ElementType = CGF.getContext().getCanonicalType(E->getType());
    ElementType = CGF.getContext().getAsArrayType(ElementType)->getElementType();
    
    unsigned CVRqualifier = ElementType.getCVRQualifiers();

    for (uint64_t i = 0; i != NumArrayElements; ++i) {
      llvm::Value *NextVal = Builder.CreateStructGEP(DestPtr, i, ".array");
      if (i < NumInitElements)
        EmitInitializationToLValue(E->getInit(i),
                                   LValue::MakeAddr(NextVal, CVRqualifier));
      else
        EmitNullInitializationToLValue(LValue::MakeAddr(NextVal, CVRqualifier),
                                       ElementType);
    }
    return;
  }
  
  assert(E->getType()->isRecordType() && "Only support structs/unions here!");
  
  // Do struct initialization; this code just sets each individual member
  // to the approprate value.  This makes bitfield support automatic;
  // the disadvantage is that the generated code is more difficult for
  // the optimizer, especially with bitfields.
  unsigned NumInitElements = E->getNumInits();
  RecordDecl *SD = E->getType()->getAsRecordType()->getDecl();
  unsigned CurInitVal = 0;

  if (E->getType()->isUnionType()) {
    // Only initialize one field of a union. The field itself is
    // specified by the initializer list.
    if (!E->getInitializedFieldInUnion()) {
      // Empty union; we have nothing to do.
      
#ifndef NDEBUG
      // Make sure that it's really an empty and not a failure of
      // semantic analysis.
      for (RecordDecl::field_iterator Field = SD->field_begin(),
                                   FieldEnd = SD->field_end();
           Field != FieldEnd; ++Field)
        assert(Field->isUnnamedBitfield() && "Only unnamed bitfields allowed");
#endif
      return;
    }

    // FIXME: volatility
    FieldDecl *Field = E->getInitializedFieldInUnion();
    LValue FieldLoc = CGF.EmitLValueForField(DestPtr, Field, true, 0);

    if (NumInitElements) {
      // Store the initializer into the field
      EmitInitializationToLValue(E->getInit(0), FieldLoc);
    } else {
      // Default-initialize to null
      EmitNullInitializationToLValue(FieldLoc, Field->getType());
    }

    return;
  }
  
  // Here we iterate over the fields; this makes it simpler to both
  // default-initialize fields and skip over unnamed fields.
  for (RecordDecl::field_iterator Field = SD->field_begin(),
                               FieldEnd = SD->field_end();
       Field != FieldEnd; ++Field) {
    // We're done once we hit the flexible array member
    if (Field->getType()->isIncompleteArrayType())
      break;

    if (Field->isUnnamedBitfield())
      continue;

    // FIXME: volatility
    LValue FieldLoc = CGF.EmitLValueForField(DestPtr, *Field, false, 0);
    if (CurInitVal < NumInitElements) {
      // Store the initializer into the field
      EmitInitializationToLValue(E->getInit(CurInitVal++), FieldLoc);
    } else {
      // We're out of initalizers; default-initialize to null
      EmitNullInitializationToLValue(FieldLoc, Field->getType());
    }
  }
}

//===----------------------------------------------------------------------===//
//                        Entry Points into this File
//===----------------------------------------------------------------------===//

/// EmitAggExpr - Emit the computation of the specified expression of
/// aggregate type.  The result is computed into DestPtr.  Note that if
/// DestPtr is null, the value of the aggregate expression is not needed.
void CodeGenFunction::EmitAggExpr(const Expr *E, llvm::Value *DestPtr,
                                  bool VolatileDest) {
  assert(E && hasAggregateLLVMType(E->getType()) &&
         "Invalid aggregate expression to emit");
  
  AggExprEmitter(*this, DestPtr, VolatileDest).Visit(const_cast<Expr*>(E));
}

void CodeGenFunction::EmitAggregateClear(llvm::Value *DestPtr, QualType Ty) {
  assert(!Ty->isAnyComplexType() && "Shouldn't happen for complex");

  EmitMemSetToZero(DestPtr, Ty);
}

void CodeGenFunction::EmitAggregateCopy(llvm::Value *DestPtr,
                                        llvm::Value *SrcPtr, QualType Ty) {
  assert(!Ty->isAnyComplexType() && "Shouldn't happen for complex");
  
  // Aggregate assignment turns into llvm.memmove.
  const llvm::Type *BP = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
  if (DestPtr->getType() != BP)
    DestPtr = Builder.CreateBitCast(DestPtr, BP, "tmp");
  if (SrcPtr->getType() != BP)
    SrcPtr = Builder.CreateBitCast(SrcPtr, BP, "tmp");
  
  // Get size and alignment info for this aggregate.
  std::pair<uint64_t, unsigned> TypeInfo = getContext().getTypeInfo(Ty);
  
  // FIXME: Handle variable sized types.
  const llvm::Type *IntPtr = llvm::IntegerType::get(LLVMPointerWidth);
  
  Builder.CreateCall4(CGM.getMemMoveFn(),
                      DestPtr, SrcPtr,
                      // TypeInfo.first describes size in bits.
                      llvm::ConstantInt::get(IntPtr, TypeInfo.first/8),
                      llvm::ConstantInt::get(llvm::Type::Int32Ty, 
                                             TypeInfo.second/8));
}
