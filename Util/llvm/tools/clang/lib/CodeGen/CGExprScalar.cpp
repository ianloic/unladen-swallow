//===--- CGExprScalar.cpp - Emit LLVM Code for Scalar Exprs ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes with scalar LLVM types as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Intrinsics.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/CFG.h"
#include <cstdarg>

using namespace clang;
using namespace CodeGen;
using llvm::Value;

//===----------------------------------------------------------------------===//
//                         Scalar Expression Emitter
//===----------------------------------------------------------------------===//

struct BinOpInfo {
  Value *LHS;
  Value *RHS;
  QualType Ty;  // Computation Type.
  const BinaryOperator *E;
};

namespace {
class VISIBILITY_HIDDEN ScalarExprEmitter
  : public StmtVisitor<ScalarExprEmitter, Value*> {
  CodeGenFunction &CGF;
  CGBuilderTy &Builder;

public:

  ScalarExprEmitter(CodeGenFunction &cgf) : CGF(cgf), 
    Builder(CGF.Builder) {
  }
  
  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  const llvm::Type *ConvertType(QualType T) { return CGF.ConvertType(T); }
  LValue EmitLValue(const Expr *E) { return CGF.EmitLValue(E); }

  Value *EmitLoadOfLValue(LValue LV, QualType T) {
    return CGF.EmitLoadOfLValue(LV, T).getScalarVal();
  }
    
  /// EmitLoadOfLValue - Given an expression with complex type that represents a
  /// value l-value, this method emits the address of the l-value, then loads
  /// and returns the result.
  Value *EmitLoadOfLValue(const Expr *E) {
    // FIXME: Volatile
    return EmitLoadOfLValue(EmitLValue(E), E->getType());
  }
    
  /// EmitConversionToBool - Convert the specified expression value to a
  /// boolean (i1) truth value.  This is equivalent to "Val != 0".
  Value *EmitConversionToBool(Value *Src, QualType DstTy);
    
  /// EmitScalarConversion - Emit a conversion from the specified type to the
  /// specified destination type, both of which are LLVM scalar types.
  Value *EmitScalarConversion(Value *Src, QualType SrcTy, QualType DstTy);

  /// EmitComplexToScalarConversion - Emit a conversion from the specified
  /// complex type to the specified destination type, where the destination
  /// type is an LLVM scalar type.
  Value *EmitComplexToScalarConversion(CodeGenFunction::ComplexPairTy Src,
                                       QualType SrcTy, QualType DstTy);
    
  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  Value *VisitStmt(Stmt *S) {
    S->dump(CGF.getContext().getSourceManager());
    assert(0 && "Stmt can't have complex result type!");
    return 0;
  }
  Value *VisitExpr(Expr *S);
  Value *VisitParenExpr(ParenExpr *PE) { return Visit(PE->getSubExpr()); }

  // Leaves.
  Value *VisitIntegerLiteral(const IntegerLiteral *E) {
    return llvm::ConstantInt::get(E->getValue());
  }
  Value *VisitFloatingLiteral(const FloatingLiteral *E) {
    return llvm::ConstantFP::get(E->getValue());
  }
  Value *VisitCharacterLiteral(const CharacterLiteral *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }
  Value *VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()), E->getValue());
  }
  Value *VisitCXXZeroInitValueExpr(const CXXZeroInitValueExpr *E) {
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  Value *VisitGNUNullExpr(const GNUNullExpr *E) {
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  Value *VisitTypesCompatibleExpr(const TypesCompatibleExpr *E) {
    return llvm::ConstantInt::get(ConvertType(E->getType()),
                                  CGF.getContext().typesAreCompatible(
                                    E->getArgType1(), E->getArgType2()));
  }
  Value *VisitSizeOfAlignOfExpr(const SizeOfAlignOfExpr *E);
  Value *VisitAddrLabelExpr(const AddrLabelExpr *E) {
    llvm::Value *V = 
      llvm::ConstantInt::get(llvm::Type::Int32Ty,
                             CGF.GetIDForAddrOfLabel(E->getLabel()));
    
    return Builder.CreateIntToPtr(V, ConvertType(E->getType()));
  }
    
  // l-values.
  Value *VisitDeclRefExpr(DeclRefExpr *E) {
    if (const EnumConstantDecl *EC = dyn_cast<EnumConstantDecl>(E->getDecl()))
      return llvm::ConstantInt::get(EC->getInitVal());
    return EmitLoadOfLValue(E);
  }
  Value *VisitObjCSelectorExpr(ObjCSelectorExpr *E) { 
    return CGF.EmitObjCSelectorExpr(E); 
  }
  Value *VisitObjCProtocolExpr(ObjCProtocolExpr *E) { 
    return CGF.EmitObjCProtocolExpr(E); 
  }
  Value *VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) { 
    return EmitLoadOfLValue(E);
  }
  Value *VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
    return EmitLoadOfLValue(E);
  }
  Value *VisitObjCKVCRefExpr(ObjCKVCRefExpr *E) {
    return EmitLoadOfLValue(E);
  }
  Value *VisitObjCMessageExpr(ObjCMessageExpr *E) {
    return CGF.EmitObjCMessageExpr(E).getScalarVal();
  }

  Value *VisitArraySubscriptExpr(ArraySubscriptExpr *E);
  Value *VisitShuffleVectorExpr(ShuffleVectorExpr *E);
  Value *VisitMemberExpr(Expr *E)           { return EmitLoadOfLValue(E); }
  Value *VisitExtVectorElementExpr(Expr *E) { return EmitLoadOfLValue(E); }
  Value *VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
    return EmitLoadOfLValue(E);
  }
  Value *VisitStringLiteral(Expr *E)  { return EmitLValue(E).getAddress(); }
  Value *VisitPredefinedExpr(Expr *E) { return EmitLValue(E).getAddress(); }

  Value *VisitInitListExpr(InitListExpr *E) {
    unsigned NumInitElements = E->getNumInits();
    
    if (E->hadArrayRangeDesignator()) {
      CGF.ErrorUnsupported(E, "GNU array range designator extension");
    }

    const llvm::VectorType *VType = 
      dyn_cast<llvm::VectorType>(ConvertType(E->getType()));
    
    // We have a scalar in braces. Just use the first element.
    if (!VType) 
      return Visit(E->getInit(0));
    
    unsigned NumVectorElements = VType->getNumElements();
    const llvm::Type *ElementType = VType->getElementType();

    // Emit individual vector element stores.
    llvm::Value *V = llvm::UndefValue::get(VType);
    
    // Emit initializers
    unsigned i;
    for (i = 0; i < NumInitElements; ++i) {
      Value *NewV = Visit(E->getInit(i));
      Value *Idx = llvm::ConstantInt::get(llvm::Type::Int32Ty, i);
      V = Builder.CreateInsertElement(V, NewV, Idx);
    }
    
    // Emit remaining default initializers
    for (/* Do not initialize i*/; i < NumVectorElements; ++i) {
      Value *Idx = llvm::ConstantInt::get(llvm::Type::Int32Ty, i);
      llvm::Value *NewV = llvm::Constant::getNullValue(ElementType);
      V = Builder.CreateInsertElement(V, NewV, Idx);
    }
    
    return V;
  }
  
  Value *VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E) {
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  }
  Value *VisitImplicitCastExpr(const ImplicitCastExpr *E);
  Value *VisitCastExpr(const CastExpr *E) { 
    return EmitCastExpr(E->getSubExpr(), E->getType());
  }
  Value *EmitCastExpr(const Expr *E, QualType T);

  Value *VisitCallExpr(const CallExpr *E) {
    return CGF.EmitCallExpr(E).getScalarVal();
  }

  Value *VisitStmtExpr(const StmtExpr *E);
  
  // Unary Operators.
  Value *VisitPrePostIncDec(const UnaryOperator *E, bool isInc, bool isPre);
  Value *VisitUnaryPostDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, false);
  }
  Value *VisitUnaryPostInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, false);
  }
  Value *VisitUnaryPreDec(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, false, true);
  }
  Value *VisitUnaryPreInc(const UnaryOperator *E) {
    return VisitPrePostIncDec(E, true, true);
  }
  Value *VisitUnaryAddrOf(const UnaryOperator *E) {
    return EmitLValue(E->getSubExpr()).getAddress();
  }
  Value *VisitUnaryDeref(const Expr *E) { return EmitLoadOfLValue(E); }
  Value *VisitUnaryPlus(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  Value *VisitUnaryMinus    (const UnaryOperator *E);
  Value *VisitUnaryNot      (const UnaryOperator *E);
  Value *VisitUnaryLNot     (const UnaryOperator *E);
  Value *VisitUnaryReal     (const UnaryOperator *E);
  Value *VisitUnaryImag     (const UnaryOperator *E);
  Value *VisitUnaryExtension(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  Value *VisitUnaryOffsetOf(const UnaryOperator *E);
  Value *VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE) {
    return Visit(DAE->getExpr());
  }
    
  // Binary Operators.
  Value *EmitMul(const BinOpInfo &Ops) {
    return Builder.CreateMul(Ops.LHS, Ops.RHS, "mul");
  }
  Value *EmitDiv(const BinOpInfo &Ops);
  Value *EmitRem(const BinOpInfo &Ops);
  Value *EmitAdd(const BinOpInfo &Ops);
  Value *EmitSub(const BinOpInfo &Ops);
  Value *EmitShl(const BinOpInfo &Ops);
  Value *EmitShr(const BinOpInfo &Ops);
  Value *EmitAnd(const BinOpInfo &Ops) {
    return Builder.CreateAnd(Ops.LHS, Ops.RHS, "and");
  }
  Value *EmitXor(const BinOpInfo &Ops) {
    return Builder.CreateXor(Ops.LHS, Ops.RHS, "xor");
  }
  Value *EmitOr (const BinOpInfo &Ops) {
    return Builder.CreateOr(Ops.LHS, Ops.RHS, "or");
  }

  BinOpInfo EmitBinOps(const BinaryOperator *E);
  Value *EmitCompoundAssign(const CompoundAssignOperator *E,
                            Value *(ScalarExprEmitter::*F)(const BinOpInfo &));

  // Binary operators and binary compound assignment operators.
#define HANDLEBINOP(OP) \
  Value *VisitBin ## OP(const BinaryOperator *E) {                         \
    return Emit ## OP(EmitBinOps(E));                                      \
  }                                                                        \
  Value *VisitBin ## OP ## Assign(const CompoundAssignOperator *E) {       \
    return EmitCompoundAssign(E, &ScalarExprEmitter::Emit ## OP);          \
  }
  HANDLEBINOP(Mul);
  HANDLEBINOP(Div);
  HANDLEBINOP(Rem);
  HANDLEBINOP(Add);
  HANDLEBINOP(Sub);
  HANDLEBINOP(Shl);
  HANDLEBINOP(Shr);
  HANDLEBINOP(And);
  HANDLEBINOP(Xor);
  HANDLEBINOP(Or);
#undef HANDLEBINOP

  // Comparisons.
  Value *EmitCompare(const BinaryOperator *E, unsigned UICmpOpc,
                     unsigned SICmpOpc, unsigned FCmpOpc);
#define VISITCOMP(CODE, UI, SI, FP) \
    Value *VisitBin##CODE(const BinaryOperator *E) { \
      return EmitCompare(E, llvm::ICmpInst::UI, llvm::ICmpInst::SI, \
                         llvm::FCmpInst::FP); }
  VISITCOMP(LT, ICMP_ULT, ICMP_SLT, FCMP_OLT);
  VISITCOMP(GT, ICMP_UGT, ICMP_SGT, FCMP_OGT);
  VISITCOMP(LE, ICMP_ULE, ICMP_SLE, FCMP_OLE);
  VISITCOMP(GE, ICMP_UGE, ICMP_SGE, FCMP_OGE);
  VISITCOMP(EQ, ICMP_EQ , ICMP_EQ , FCMP_OEQ);
  VISITCOMP(NE, ICMP_NE , ICMP_NE , FCMP_UNE);
#undef VISITCOMP
  
  Value *VisitBinAssign     (const BinaryOperator *E);

  Value *VisitBinLAnd       (const BinaryOperator *E);
  Value *VisitBinLOr        (const BinaryOperator *E);
  Value *VisitBinComma      (const BinaryOperator *E);

  // Other Operators.
  Value *VisitBlockExpr(const BlockExpr *BE) {
    CGF.ErrorUnsupported(BE, "block expression");
    return llvm::UndefValue::get(CGF.ConvertType(BE->getType()));
  }

  Value *VisitConditionalOperator(const ConditionalOperator *CO);
  Value *VisitChooseExpr(ChooseExpr *CE);
  Value *VisitOverloadExpr(OverloadExpr *OE);
  Value *VisitVAArgExpr(VAArgExpr *VE);
  Value *VisitObjCStringLiteral(const ObjCStringLiteral *E) {
    return CGF.EmitObjCStringLiteral(E);
  }
  Value *VisitObjCEncodeExpr(const ObjCEncodeExpr *E);
};
}  // end anonymous namespace.

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

/// EmitConversionToBool - Convert the specified expression value to a
/// boolean (i1) truth value.  This is equivalent to "Val != 0".
Value *ScalarExprEmitter::EmitConversionToBool(Value *Src, QualType SrcType) {
  assert(SrcType->isCanonical() && "EmitScalarConversion strips typedefs");
  
  if (SrcType->isRealFloatingType()) {
    // Compare against 0.0 for fp scalars.
    llvm::Value *Zero = llvm::Constant::getNullValue(Src->getType());
    return Builder.CreateFCmpUNE(Src, Zero, "tobool");
  }
  
  assert((SrcType->isIntegerType() || isa<llvm::PointerType>(Src->getType())) &&
         "Unknown scalar type to convert");
  
  // Because of the type rules of C, we often end up computing a logical value,
  // then zero extending it to int, then wanting it as a logical value again.
  // Optimize this common case.
  if (llvm::ZExtInst *ZI = dyn_cast<llvm::ZExtInst>(Src)) {
    if (ZI->getOperand(0)->getType() == llvm::Type::Int1Ty) {
      Value *Result = ZI->getOperand(0);
      // If there aren't any more uses, zap the instruction to save space.
      // Note that there can be more uses, for example if this
      // is the result of an assignment.
      if (ZI->use_empty())
        ZI->eraseFromParent();
      return Result;
    }
  }
  
  // Compare against an integer or pointer null.
  llvm::Value *Zero = llvm::Constant::getNullValue(Src->getType());
  return Builder.CreateICmpNE(Src, Zero, "tobool");
}

/// EmitScalarConversion - Emit a conversion from the specified type to the
/// specified destination type, both of which are LLVM scalar types.
Value *ScalarExprEmitter::EmitScalarConversion(Value *Src, QualType SrcType,
                                               QualType DstType) {
  SrcType = CGF.getContext().getCanonicalType(SrcType);
  DstType = CGF.getContext().getCanonicalType(DstType);
  if (SrcType == DstType) return Src;
  
  if (DstType->isVoidType()) return 0;

  // Handle conversions to bool first, they are special: comparisons against 0.
  if (DstType->isBooleanType())
    return EmitConversionToBool(Src, SrcType);
  
  const llvm::Type *DstTy = ConvertType(DstType);

  // Ignore conversions like int -> uint.
  if (Src->getType() == DstTy)
    return Src;

  // Handle pointer conversions next: pointers can only be converted
  // to/from other pointers and integers. Check for pointer types in
  // terms of LLVM, as some native types (like Obj-C id) may map to a
  // pointer type.
  if (isa<llvm::PointerType>(DstTy)) {
    // The source value may be an integer, or a pointer.
    if (isa<llvm::PointerType>(Src->getType()))
      return Builder.CreateBitCast(Src, DstTy, "conv");
    assert(SrcType->isIntegerType() && "Not ptr->ptr or int->ptr conversion?");
    return Builder.CreateIntToPtr(Src, DstTy, "conv");
  }
  
  if (isa<llvm::PointerType>(Src->getType())) {
    // Must be an ptr to int cast.
    assert(isa<llvm::IntegerType>(DstTy) && "not ptr->int?");
    return Builder.CreatePtrToInt(Src, DstTy, "conv");
  }
  
  // A scalar can be splatted to an extended vector of the same element type
  if (DstType->isExtVectorType() && !isa<VectorType>(SrcType)) {
    // Cast the scalar to element type
    QualType EltTy = DstType->getAsExtVectorType()->getElementType();
    llvm::Value *Elt = EmitScalarConversion(Src, SrcType, EltTy);

    // Insert the element in element zero of an undef vector
    llvm::Value *UnV = llvm::UndefValue::get(DstTy);
    llvm::Value *Idx = llvm::ConstantInt::get(llvm::Type::Int32Ty, 0);
    UnV = Builder.CreateInsertElement(UnV, Elt, Idx, "tmp");

    // Splat the element across to all elements
    llvm::SmallVector<llvm::Constant*, 16> Args;
    unsigned NumElements = cast<llvm::VectorType>(DstTy)->getNumElements();
    for (unsigned i = 0; i < NumElements; i++)
      Args.push_back(llvm::ConstantInt::get(llvm::Type::Int32Ty, 0));
    
    llvm::Constant *Mask = llvm::ConstantVector::get(&Args[0], NumElements);
    llvm::Value *Yay = Builder.CreateShuffleVector(UnV, UnV, Mask, "splat");
    return Yay;
  }

  // Allow bitcast from vector to integer/fp of the same size.
  if (isa<llvm::VectorType>(Src->getType()) ||
      isa<llvm::VectorType>(DstTy))
    return Builder.CreateBitCast(Src, DstTy, "conv");
      
  // Finally, we have the arithmetic types: real int/float.
  if (isa<llvm::IntegerType>(Src->getType())) {
    bool InputSigned = SrcType->isSignedIntegerType();
    if (isa<llvm::IntegerType>(DstTy))
      return Builder.CreateIntCast(Src, DstTy, InputSigned, "conv");
    else if (InputSigned)
      return Builder.CreateSIToFP(Src, DstTy, "conv");
    else
      return Builder.CreateUIToFP(Src, DstTy, "conv");
  }
  
  assert(Src->getType()->isFloatingPoint() && "Unknown real conversion");
  if (isa<llvm::IntegerType>(DstTy)) {
    if (DstType->isSignedIntegerType())
      return Builder.CreateFPToSI(Src, DstTy, "conv");
    else
      return Builder.CreateFPToUI(Src, DstTy, "conv");
  }

  assert(DstTy->isFloatingPoint() && "Unknown real conversion");
  if (DstTy->getTypeID() < Src->getType()->getTypeID())
    return Builder.CreateFPTrunc(Src, DstTy, "conv");
  else
    return Builder.CreateFPExt(Src, DstTy, "conv");
}

/// EmitComplexToScalarConversion - Emit a conversion from the specified
/// complex type to the specified destination type, where the destination
/// type is an LLVM scalar type.
Value *ScalarExprEmitter::
EmitComplexToScalarConversion(CodeGenFunction::ComplexPairTy Src,
                              QualType SrcTy, QualType DstTy) {
  // Get the source element type.
  SrcTy = SrcTy->getAsComplexType()->getElementType();
  
  // Handle conversions to bool first, they are special: comparisons against 0.
  if (DstTy->isBooleanType()) {
    //  Complex != 0  -> (Real != 0) | (Imag != 0)
    Src.first  = EmitScalarConversion(Src.first, SrcTy, DstTy);
    Src.second = EmitScalarConversion(Src.second, SrcTy, DstTy);
    return Builder.CreateOr(Src.first, Src.second, "tobool");
  }
  
  // C99 6.3.1.7p2: "When a value of complex type is converted to a real type,
  // the imaginary part of the complex value is discarded and the value of the
  // real part is converted according to the conversion rules for the
  // corresponding real type. 
  return EmitScalarConversion(Src.first, SrcTy, DstTy);
}


//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

Value *ScalarExprEmitter::VisitExpr(Expr *E) {
  CGF.ErrorUnsupported(E, "scalar expression");
  if (E->getType()->isVoidType())
    return 0;
  return llvm::UndefValue::get(CGF.ConvertType(E->getType()));
}

Value *ScalarExprEmitter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  llvm::SmallVector<llvm::Constant*, 32> indices;
  for (unsigned i = 2; i < E->getNumSubExprs(); i++) {
    indices.push_back(cast<llvm::Constant>(CGF.EmitScalarExpr(E->getExpr(i))));
  }
  Value* V1 = CGF.EmitScalarExpr(E->getExpr(0));
  Value* V2 = CGF.EmitScalarExpr(E->getExpr(1));
  Value* SV = llvm::ConstantVector::get(indices.begin(), indices.size());
  return Builder.CreateShuffleVector(V1, V2, SV, "shuffle");
}

Value *ScalarExprEmitter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  // Emit subscript expressions in rvalue context's.  For most cases, this just
  // loads the lvalue formed by the subscript expr.  However, we have to be
  // careful, because the base of a vector subscript is occasionally an rvalue,
  // so we can't get it as an lvalue.
  if (!E->getBase()->getType()->isVectorType())
    return EmitLoadOfLValue(E);
  
  // Handle the vector case.  The base must be a vector, the index must be an
  // integer value.
  Value *Base = Visit(E->getBase());
  Value *Idx  = Visit(E->getIdx());
  
  // FIXME: Convert Idx to i32 type.
  return Builder.CreateExtractElement(Base, Idx, "vecext");
}

/// VisitImplicitCastExpr - Implicit casts are the same as normal casts, but
/// also handle things like function to pointer-to-function decay, and array to
/// pointer decay.
Value *ScalarExprEmitter::VisitImplicitCastExpr(const ImplicitCastExpr *E) {
  const Expr *Op = E->getSubExpr();
  
  // If this is due to array->pointer conversion, emit the array expression as
  // an l-value.
  if (Op->getType()->isArrayType()) {
    // FIXME: For now we assume that all source arrays map to LLVM arrays.  This
    // will not true when we add support for VLAs.
    Value *V = EmitLValue(Op).getAddress();  // Bitfields can't be arrays.

    if (!Op->getType()->isVariableArrayType()) {
      assert(isa<llvm::PointerType>(V->getType()) && "Expected pointer");
      assert(isa<llvm::ArrayType>(cast<llvm::PointerType>(V->getType())
                                 ->getElementType()) &&
             "Expected pointer to array");
      V = Builder.CreateStructGEP(V, 0, "arraydecay");
    }
    
    // The resultant pointer type can be implicitly casted to other pointer
    // types as well (e.g. void*) and can be implicitly converted to integer.
    const llvm::Type *DestTy = ConvertType(E->getType());
    if (V->getType() != DestTy) {
      if (isa<llvm::PointerType>(DestTy))
        V = Builder.CreateBitCast(V, DestTy, "ptrconv");
      else {
        assert(isa<llvm::IntegerType>(DestTy) && "Unknown array decay");
        V = Builder.CreatePtrToInt(V, DestTy, "ptrconv");
      }
    }
    return V;
    
  } else if (E->getType()->isReferenceType()) {
    return EmitLValue(Op).getAddress();
  }
  
  return EmitCastExpr(Op, E->getType());
}


// VisitCastExpr - Emit code for an explicit or implicit cast.  Implicit casts
// have to handle a more broad range of conversions than explicit casts, as they
// handle things like function to ptr-to-function decay etc.
Value *ScalarExprEmitter::EmitCastExpr(const Expr *E, QualType DestTy) {
  // Handle cases where the source is an non-complex type.
  
  if (!CGF.hasAggregateLLVMType(E->getType())) {
    Value *Src = Visit(const_cast<Expr*>(E));

    // Use EmitScalarConversion to perform the conversion.
    return EmitScalarConversion(Src, E->getType(), DestTy);
  }
  
  if (E->getType()->isAnyComplexType()) {
    // Handle cases where the source is a complex type.
    return EmitComplexToScalarConversion(CGF.EmitComplexExpr(E), E->getType(),
                                         DestTy);
  }

  // Okay, this is a cast from an aggregate.  It must be a cast to void.  Just
  // evaluate the result and return.
  CGF.EmitAggExpr(E, 0, false);
  return 0;
}

Value *ScalarExprEmitter::VisitStmtExpr(const StmtExpr *E) {
  return CGF.EmitCompoundStmt(*E->getSubStmt(),
                              !E->getType()->isVoidType()).getScalarVal();
}


//===----------------------------------------------------------------------===//
//                             Unary Operators
//===----------------------------------------------------------------------===//

Value *ScalarExprEmitter::VisitPrePostIncDec(const UnaryOperator *E,
                                             bool isInc, bool isPre) {
  LValue LV = EmitLValue(E->getSubExpr());
  // FIXME: Handle volatile!
  Value *InVal = CGF.EmitLoadOfLValue(LV, // false
                                     E->getSubExpr()->getType()).getScalarVal();
  
  int AmountVal = isInc ? 1 : -1;
  
  Value *NextVal;
  if (isa<llvm::PointerType>(InVal->getType())) {
    // FIXME: This isn't right for VLAs.
    NextVal = llvm::ConstantInt::get(llvm::Type::Int32Ty, AmountVal);
    NextVal = Builder.CreateGEP(InVal, NextVal, "ptrincdec");
  } else {
    // Add the inc/dec to the real part.
    if (isa<llvm::IntegerType>(InVal->getType()))
      NextVal = llvm::ConstantInt::get(InVal->getType(), AmountVal);
    else if (InVal->getType() == llvm::Type::FloatTy)
      NextVal = 
        llvm::ConstantFP::get(llvm::APFloat(static_cast<float>(AmountVal)));
    else if (InVal->getType() == llvm::Type::DoubleTy)
      NextVal = 
        llvm::ConstantFP::get(llvm::APFloat(static_cast<double>(AmountVal)));
    else {
      llvm::APFloat F(static_cast<float>(AmountVal));
      bool ignored;
      F.convert(CGF.Target.getLongDoubleFormat(), llvm::APFloat::rmTowardZero,
                &ignored);
      NextVal = llvm::ConstantFP::get(F);
    }
    NextVal = Builder.CreateAdd(InVal, NextVal, isInc ? "inc" : "dec");
  }
  
  // Store the updated result through the lvalue.
  CGF.EmitStoreThroughLValue(RValue::get(NextVal), LV, 
                             E->getSubExpr()->getType());

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? NextVal : InVal;
}


Value *ScalarExprEmitter::VisitUnaryMinus(const UnaryOperator *E) {
  Value *Op = Visit(E->getSubExpr());
  return Builder.CreateNeg(Op, "neg");
}

Value *ScalarExprEmitter::VisitUnaryNot(const UnaryOperator *E) {
  Value *Op = Visit(E->getSubExpr());
  return Builder.CreateNot(Op, "neg");
}

Value *ScalarExprEmitter::VisitUnaryLNot(const UnaryOperator *E) {
  // Compare operand to zero.
  Value *BoolVal = CGF.EvaluateExprAsBool(E->getSubExpr());
  
  // Invert value.
  // TODO: Could dynamically modify easy computations here.  For example, if
  // the operand is an icmp ne, turn into icmp eq.
  BoolVal = Builder.CreateNot(BoolVal, "lnot");
  
  // ZExt result to int.
  return Builder.CreateZExt(BoolVal, CGF.LLVMIntTy, "lnot.ext");
}

/// VisitSizeOfAlignOfExpr - Return the size or alignment of the type of
/// argument of the sizeof expression as an integer.
Value *
ScalarExprEmitter::VisitSizeOfAlignOfExpr(const SizeOfAlignOfExpr *E) {
  QualType TypeToSize = E->getTypeOfArgument();
  if (E->isSizeOf()) {
    if (const VariableArrayType *VAT = 
          CGF.getContext().getAsVariableArrayType(TypeToSize)) {
      if (E->isArgumentType()) {
        // sizeof(type) - make sure to emit the VLA size.
        CGF.EmitVLASize(TypeToSize);
      }
      
      llvm::Value *VLASize = CGF.GetVLASize(VAT);
      return Builder.CreateIntCast(VLASize, ConvertType(E->getType()), 
                                   false, "conv");
    }
  }

  // If this isn't sizeof(vla), the result must be constant; use the
  // constant folding logic so we don't have to duplicate it here.
  Expr::EvalResult Result;
  E->Evaluate(Result, CGF.getContext());
  return llvm::ConstantInt::get(Result.Val.getInt());
}

Value *ScalarExprEmitter::VisitUnaryReal(const UnaryOperator *E) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType())
    return CGF.EmitComplexExpr(Op).first;
  return Visit(Op);
}
Value *ScalarExprEmitter::VisitUnaryImag(const UnaryOperator *E) {
  Expr *Op = E->getSubExpr();
  if (Op->getType()->isAnyComplexType())
    return CGF.EmitComplexExpr(Op).second;
  
  // __imag on a scalar returns zero.  Emit it the subexpr to ensure side
  // effects are evaluated.
  CGF.EmitScalarExpr(Op);
  return llvm::Constant::getNullValue(ConvertType(E->getType()));
}

Value *ScalarExprEmitter::VisitUnaryOffsetOf(const UnaryOperator *E)
{
  const Expr* SubExpr = E->getSubExpr();
  const llvm::Type* ResultType = ConvertType(E->getType());
  llvm::Value* Result = llvm::Constant::getNullValue(ResultType);
  while (!isa<CompoundLiteralExpr>(SubExpr)) {
    if (const MemberExpr *ME = dyn_cast<MemberExpr>(SubExpr)) {
      SubExpr = ME->getBase();
      QualType Ty = SubExpr->getType();

      RecordDecl *RD = Ty->getAsRecordType()->getDecl();
      const ASTRecordLayout &RL = CGF.getContext().getASTRecordLayout(RD);
      FieldDecl *FD = cast<FieldDecl>(ME->getMemberDecl());

      // FIXME: This is linear time. And the fact that we're indexing
      // into the layout by position in the record means that we're
      // either stuck numbering the fields in the AST or we have to keep
      // the linear search (yuck and yuck).
      unsigned i = 0;
      for (RecordDecl::field_iterator Field = RD->field_begin(),
                                   FieldEnd = RD->field_end();
           Field != FieldEnd; (void)++Field, ++i) {
        if (*Field == FD)
          break;
      }

      llvm::Value* Offset =
          llvm::ConstantInt::get(ResultType, RL.getFieldOffset(i) / 8);
      Result = Builder.CreateAdd(Result, Offset);
    } else if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(SubExpr)) {
      SubExpr = ASE->getBase();
      int64_t size = CGF.getContext().getTypeSize(ASE->getType()) / 8;
      llvm::Value* ElemSize = llvm::ConstantInt::get(ResultType, size);
      llvm::Value* ElemIndex = CGF.EmitScalarExpr(ASE->getIdx());
      bool IndexSigned = ASE->getIdx()->getType()->isSignedIntegerType();
      ElemIndex = Builder.CreateIntCast(ElemIndex, ResultType, IndexSigned);
      llvm::Value* Offset = Builder.CreateMul(ElemSize, ElemIndex);
      Result = Builder.CreateAdd(Result, Offset);
    } else {
      assert(0 && "This should be impossible!");
    }
  }
  return Result;
}

//===----------------------------------------------------------------------===//
//                           Binary Operators
//===----------------------------------------------------------------------===//

BinOpInfo ScalarExprEmitter::EmitBinOps(const BinaryOperator *E) {
  BinOpInfo Result;
  Result.LHS = Visit(E->getLHS());
  Result.RHS = Visit(E->getRHS());
  Result.Ty  = E->getType();
  Result.E = E;
  return Result;
}

Value *ScalarExprEmitter::EmitCompoundAssign(const CompoundAssignOperator *E,
                      Value *(ScalarExprEmitter::*Func)(const BinOpInfo &)) {
  QualType LHSTy = E->getLHS()->getType(), RHSTy = E->getRHS()->getType();

  BinOpInfo OpInfo;

  // Load the LHS and RHS operands.
  LValue LHSLV = EmitLValue(E->getLHS());
  OpInfo.LHS = EmitLoadOfLValue(LHSLV, LHSTy);

  // Determine the computation type.  If the RHS is complex, then this is one of
  // the add/sub/mul/div operators.  All of these operators can be computed in
  // with just their real component even though the computation domain really is
  // complex.
  QualType ComputeType = E->getComputationType();
  
  // If the computation type is complex, then the RHS is complex.  Emit the RHS.
  if (const ComplexType *CT = ComputeType->getAsComplexType()) {
    ComputeType = CT->getElementType();
    
    // Emit the RHS, only keeping the real component.
    OpInfo.RHS = CGF.EmitComplexExpr(E->getRHS()).first;
    RHSTy = RHSTy->getAsComplexType()->getElementType();
  } else {
    // Otherwise the RHS is a simple scalar value.
    OpInfo.RHS = Visit(E->getRHS());
  }
  
  QualType LComputeTy, RComputeTy, ResultTy;

  // Compound assignment does not contain enough information about all
  // the types involved for pointer arithmetic cases. Figure it out
  // here for now.
  if (E->getLHS()->getType()->isPointerType()) {
    // Pointer arithmetic cases: ptr +=,-= int and ptr -= ptr, 
    assert((E->getOpcode() == BinaryOperator::AddAssign ||
            E->getOpcode() == BinaryOperator::SubAssign) &&
           "Invalid compound assignment operator on pointer type.");
    LComputeTy = E->getLHS()->getType();
    
    if (E->getRHS()->getType()->isPointerType()) {    
      // Degenerate case of (ptr -= ptr) allowed by GCC implicit cast
      // extension, the conversion from the pointer difference back to
      // the LHS type is handled at the end.
      assert(E->getOpcode() == BinaryOperator::SubAssign &&
             "Invalid compound assignment operator on pointer type.");
      RComputeTy = E->getLHS()->getType();
      ResultTy = CGF.getContext().getPointerDiffType();
    } else {
      RComputeTy = E->getRHS()->getType();
      ResultTy = LComputeTy;
    }
  } else if (E->getRHS()->getType()->isPointerType()) {
    // Degenerate case of (int += ptr) allowed by GCC implicit cast
    // extension.
    assert(E->getOpcode() == BinaryOperator::AddAssign &&
           "Invalid compound assignment operator on pointer type.");
    LComputeTy = E->getLHS()->getType();
    RComputeTy = E->getRHS()->getType();
    ResultTy = RComputeTy;
  } else {
    LComputeTy = RComputeTy = ResultTy = ComputeType;
  }

  // Convert the LHS/RHS values to the computation type.
  OpInfo.LHS = EmitScalarConversion(OpInfo.LHS, LHSTy, LComputeTy);
  OpInfo.RHS = EmitScalarConversion(OpInfo.RHS, RHSTy, RComputeTy);
  OpInfo.Ty = ResultTy;
  OpInfo.E = E;
  
  // Expand the binary operator.
  Value *Result = (this->*Func)(OpInfo);
  
  // Convert the result back to the LHS type.
  Result = EmitScalarConversion(Result, ResultTy, LHSTy);
  
  // Store the result value into the LHS lvalue. Bit-fields are
  // handled specially because the result is altered by the store,
  // i.e., [C99 6.5.16p1] 'An assignment expression has the value of
  // the left operand after the assignment...'.
  if (LHSLV.isBitfield())
    CGF.EmitStoreThroughBitfieldLValue(RValue::get(Result), LHSLV, LHSTy,
                                       &Result);
  else
    CGF.EmitStoreThroughLValue(RValue::get(Result), LHSLV, LHSTy);
  
  return Result;
}


Value *ScalarExprEmitter::EmitDiv(const BinOpInfo &Ops) {
  if (Ops.LHS->getType()->isFPOrFPVector())
    return Builder.CreateFDiv(Ops.LHS, Ops.RHS, "div");
  else if (Ops.Ty->isUnsignedIntegerType())
    return Builder.CreateUDiv(Ops.LHS, Ops.RHS, "div");
  else
    return Builder.CreateSDiv(Ops.LHS, Ops.RHS, "div");
}

Value *ScalarExprEmitter::EmitRem(const BinOpInfo &Ops) {
  // Rem in C can't be a floating point type: C99 6.5.5p2.
  if (Ops.Ty->isUnsignedIntegerType())
    return Builder.CreateURem(Ops.LHS, Ops.RHS, "rem");
  else
    return Builder.CreateSRem(Ops.LHS, Ops.RHS, "rem");
}


Value *ScalarExprEmitter::EmitAdd(const BinOpInfo &Ops) {
  if (!Ops.Ty->isPointerType())
    return Builder.CreateAdd(Ops.LHS, Ops.RHS, "add");
  
  // FIXME: What about a pointer to a VLA?
  Value *Ptr, *Idx;
  Expr *IdxExp;
  const PointerType *PT;
  if ((PT = Ops.E->getLHS()->getType()->getAsPointerType())) {
    Ptr = Ops.LHS;
    Idx = Ops.RHS;
    IdxExp = Ops.E->getRHS();
  } else {                                           // int + pointer
    PT = Ops.E->getRHS()->getType()->getAsPointerType();
    assert(PT && "Invalid add expr");
    Ptr = Ops.RHS;
    Idx = Ops.LHS;
    IdxExp = Ops.E->getLHS();
  }

  unsigned Width = cast<llvm::IntegerType>(Idx->getType())->getBitWidth();
  if (Width < CGF.LLVMPointerWidth) {
    // Zero or sign extend the pointer value based on whether the index is
    // signed or not.
    const llvm::Type *IdxType = llvm::IntegerType::get(CGF.LLVMPointerWidth);
    if (IdxExp->getType()->isSignedIntegerType())
      Idx = Builder.CreateSExt(Idx, IdxType, "idx.ext");
    else
      Idx = Builder.CreateZExt(Idx, IdxType, "idx.ext");
  }

  // Explicitly handle GNU void* and function pointer arithmetic
  // extensions. The GNU void* casts amount to no-ops since our void*
  // type is i8*, but this is future proof.
  const QualType ElementType = PT->getPointeeType();
  if (ElementType->isVoidType() || ElementType->isFunctionType()) {
    const llvm::Type *i8Ty = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
    Value *Casted = Builder.CreateBitCast(Ptr, i8Ty);
    Value *Res = Builder.CreateGEP(Casted, Idx, "sub.ptr");
    return Builder.CreateBitCast(Res, Ptr->getType());
  } 
  
  return Builder.CreateGEP(Ptr, Idx, "add.ptr");
}

Value *ScalarExprEmitter::EmitSub(const BinOpInfo &Ops) {
  if (!isa<llvm::PointerType>(Ops.LHS->getType()))
    return Builder.CreateSub(Ops.LHS, Ops.RHS, "sub");

  const QualType LHSType = Ops.E->getLHS()->getType();
  const QualType LHSElementType = LHSType->getAsPointerType()->getPointeeType();
  if (!isa<llvm::PointerType>(Ops.RHS->getType())) {
    // pointer - int
    Value *Idx = Ops.RHS;
    unsigned Width = cast<llvm::IntegerType>(Idx->getType())->getBitWidth();
    if (Width < CGF.LLVMPointerWidth) {
      // Zero or sign extend the pointer value based on whether the index is
      // signed or not.
      const llvm::Type *IdxType = llvm::IntegerType::get(CGF.LLVMPointerWidth);
      if (Ops.E->getRHS()->getType()->isSignedIntegerType())
        Idx = Builder.CreateSExt(Idx, IdxType, "idx.ext");
      else
        Idx = Builder.CreateZExt(Idx, IdxType, "idx.ext");
    }
    Idx = Builder.CreateNeg(Idx, "sub.ptr.neg");
    
    // FIXME: The pointer could point to a VLA.

    // Explicitly handle GNU void* and function pointer arithmetic
    // extensions. The GNU void* casts amount to no-ops since our
    // void* type is i8*, but this is future proof.
    if (LHSElementType->isVoidType() || LHSElementType->isFunctionType()) {
      const llvm::Type *i8Ty = llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
      Value *LHSCasted = Builder.CreateBitCast(Ops.LHS, i8Ty);
      Value *Res = Builder.CreateGEP(LHSCasted, Idx, "sub.ptr");
      return Builder.CreateBitCast(Res, Ops.LHS->getType());
    } 
      
    return Builder.CreateGEP(Ops.LHS, Idx, "sub.ptr");
  } else {
    // pointer - pointer
    Value *LHS = Ops.LHS;
    Value *RHS = Ops.RHS;
  
    uint64_t ElementSize;

    // Handle GCC extension for pointer arithmetic on void* types.
    if (LHSElementType->isVoidType()) {
      ElementSize = 1;
    } else {
      ElementSize = CGF.getContext().getTypeSize(LHSElementType) / 8;
    }
    
    const llvm::Type *ResultType = ConvertType(Ops.Ty);
    LHS = Builder.CreatePtrToInt(LHS, ResultType, "sub.ptr.lhs.cast");
    RHS = Builder.CreatePtrToInt(RHS, ResultType, "sub.ptr.rhs.cast");
    Value *BytesBetween = Builder.CreateSub(LHS, RHS, "sub.ptr.sub");
    
    // HACK: LLVM doesn't have an divide instruction that 'knows' there is no
    // remainder.  As such, we handle common power-of-two cases here to generate
    // better code. See PR2247.
    if (llvm::isPowerOf2_64(ElementSize)) {
      Value *ShAmt =
        llvm::ConstantInt::get(ResultType, llvm::Log2_64(ElementSize));
      return Builder.CreateAShr(BytesBetween, ShAmt, "sub.ptr.shr");
    }
    
    // Otherwise, do a full sdiv.
    Value *BytesPerElt = llvm::ConstantInt::get(ResultType, ElementSize);
    return Builder.CreateSDiv(BytesBetween, BytesPerElt, "sub.ptr.div");
  }
}

Value *ScalarExprEmitter::EmitShl(const BinOpInfo &Ops) {
  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");
  
  return Builder.CreateShl(Ops.LHS, RHS, "shl");
}

Value *ScalarExprEmitter::EmitShr(const BinOpInfo &Ops) {
  // LLVM requires the LHS and RHS to be the same type: promote or truncate the
  // RHS to the same size as the LHS.
  Value *RHS = Ops.RHS;
  if (Ops.LHS->getType() != RHS->getType())
    RHS = Builder.CreateIntCast(RHS, Ops.LHS->getType(), false, "sh_prom");
  
  if (Ops.Ty->isUnsignedIntegerType())
    return Builder.CreateLShr(Ops.LHS, RHS, "shr");
  return Builder.CreateAShr(Ops.LHS, RHS, "shr");
}

Value *ScalarExprEmitter::EmitCompare(const BinaryOperator *E,unsigned UICmpOpc,
                                      unsigned SICmpOpc, unsigned FCmpOpc) {
  Value *Result;
  QualType LHSTy = E->getLHS()->getType();
  if (!LHSTy->isAnyComplexType() && !LHSTy->isVectorType()) {
    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    
    if (LHS->getType()->isFloatingPoint()) {
      Result = Builder.CreateFCmp((llvm::CmpInst::Predicate)FCmpOpc,
                                  LHS, RHS, "cmp");
    } else if (LHSTy->isSignedIntegerType()) {
      Result = Builder.CreateICmp((llvm::ICmpInst::Predicate)SICmpOpc,
                                  LHS, RHS, "cmp");
    } else {
      // Unsigned integers and pointers.
      Result = Builder.CreateICmp((llvm::ICmpInst::Predicate)UICmpOpc,
                                  LHS, RHS, "cmp");
    }
  } else if (LHSTy->isVectorType()) {
    Value *LHS = Visit(E->getLHS());
    Value *RHS = Visit(E->getRHS());
    
    if (LHS->getType()->isFPOrFPVector()) {
      Result = Builder.CreateVFCmp((llvm::CmpInst::Predicate)FCmpOpc,
                                  LHS, RHS, "cmp");
    } else if (LHSTy->isUnsignedIntegerType()) {
      Result = Builder.CreateVICmp((llvm::CmpInst::Predicate)UICmpOpc,
                                  LHS, RHS, "cmp");
    } else {
      // Signed integers and pointers.
      Result = Builder.CreateVICmp((llvm::CmpInst::Predicate)SICmpOpc,
                                  LHS, RHS, "cmp");
    }
    return Result;
  } else {
    // Complex Comparison: can only be an equality comparison.
    CodeGenFunction::ComplexPairTy LHS = CGF.EmitComplexExpr(E->getLHS());
    CodeGenFunction::ComplexPairTy RHS = CGF.EmitComplexExpr(E->getRHS());
    
    QualType CETy = LHSTy->getAsComplexType()->getElementType();
    
    Value *ResultR, *ResultI;
    if (CETy->isRealFloatingType()) {
      ResultR = Builder.CreateFCmp((llvm::FCmpInst::Predicate)FCmpOpc,
                                   LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateFCmp((llvm::FCmpInst::Predicate)FCmpOpc,
                                   LHS.second, RHS.second, "cmp.i");
    } else {
      // Complex comparisons can only be equality comparisons.  As such, signed
      // and unsigned opcodes are the same.
      ResultR = Builder.CreateICmp((llvm::ICmpInst::Predicate)UICmpOpc,
                                   LHS.first, RHS.first, "cmp.r");
      ResultI = Builder.CreateICmp((llvm::ICmpInst::Predicate)UICmpOpc,
                                   LHS.second, RHS.second, "cmp.i");
    }
    
    if (E->getOpcode() == BinaryOperator::EQ) {
      Result = Builder.CreateAnd(ResultR, ResultI, "and.ri");
    } else {
      assert(E->getOpcode() == BinaryOperator::NE &&
             "Complex comparison other than == or != ?");
      Result = Builder.CreateOr(ResultR, ResultI, "or.ri");
    }
  }

  return EmitScalarConversion(Result, CGF.getContext().BoolTy, E->getType());
}

Value *ScalarExprEmitter::VisitBinAssign(const BinaryOperator *E) {
  LValue LHS = EmitLValue(E->getLHS());
  Value *RHS = Visit(E->getRHS());
  
  // Store the value into the LHS.  Bit-fields are handled specially
  // because the result is altered by the store, i.e., [C99 6.5.16p1]
  // 'An assignment expression has the value of the left operand after
  // the assignment...'.  
  // FIXME: Volatility!
  if (LHS.isBitfield())
    CGF.EmitStoreThroughBitfieldLValue(RValue::get(RHS), LHS, E->getType(),
                                       &RHS);
  else
    CGF.EmitStoreThroughLValue(RValue::get(RHS), LHS, E->getType());

  // Return the RHS.
  return RHS;
}

Value *ScalarExprEmitter::VisitBinLAnd(const BinaryOperator *E) {
  // If we have 0 && RHS, see if we can elide RHS, if so, just return 0.
  // If we have 1 && X, just emit X without inserting the control flow.
  if (int Cond = CGF.ConstantFoldsToSimpleInteger(E->getLHS())) {
    if (Cond == 1) { // If we have 1 && X, just emit X.
      Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());
      // ZExt result to int.
      return Builder.CreateZExt(RHSCond, CGF.LLVMIntTy, "land.ext");
    }
    
    // 0 && RHS: If it is safe, just elide the RHS, and return 0.
    if (!CGF.ContainsLabel(E->getRHS()))
      return llvm::Constant::getNullValue(CGF.LLVMIntTy);
  }
  
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("land.end");
  llvm::BasicBlock *RHSBlock  = CGF.createBasicBlock("land.rhs");

  // Branch on the LHS first.  If it is false, go to the failure (cont) block.
  CGF.EmitBranchOnBoolExpr(E->getLHS(), RHSBlock, ContBlock);

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be false.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN = llvm::PHINode::Create(llvm::Type::Int1Ty, "", ContBlock);
  PN->reserveOperandSpace(2);  // Normal case, two inputs.
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getFalse(), *PI);
  
  CGF.EmitBlock(RHSBlock);
  Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());
  
  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();

  // Emit an unconditional branch from this block to ContBlock.  Insert an entry
  // into the phi node for the edge with the value of RHSCond.
  CGF.EmitBlock(ContBlock);
  PN->addIncoming(RHSCond, RHSBlock);
  
  // ZExt result to int.
  return Builder.CreateZExt(PN, CGF.LLVMIntTy, "land.ext");
}

Value *ScalarExprEmitter::VisitBinLOr(const BinaryOperator *E) {
  // If we have 1 || RHS, see if we can elide RHS, if so, just return 1.
  // If we have 0 || X, just emit X without inserting the control flow.
  if (int Cond = CGF.ConstantFoldsToSimpleInteger(E->getLHS())) {
    if (Cond == -1) { // If we have 0 || X, just emit X.
      Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());
      // ZExt result to int.
      return Builder.CreateZExt(RHSCond, CGF.LLVMIntTy, "lor.ext");
    }
    
    // 1 || RHS: If it is safe, just elide the RHS, and return 1.
    if (!CGF.ContainsLabel(E->getRHS()))
      return llvm::ConstantInt::get(CGF.LLVMIntTy, 1);
  }
  
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("lor.end");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("lor.rhs");
  
  // Branch on the LHS first.  If it is true, go to the success (cont) block.
  CGF.EmitBranchOnBoolExpr(E->getLHS(), ContBlock, RHSBlock);

  // Any edges into the ContBlock are now from an (indeterminate number of)
  // edges from this first condition.  All of these values will be true.  Start
  // setting up the PHI node in the Cont Block for this.
  llvm::PHINode *PN = llvm::PHINode::Create(llvm::Type::Int1Ty, "", ContBlock);
  PN->reserveOperandSpace(2);  // Normal case, two inputs.
  for (llvm::pred_iterator PI = pred_begin(ContBlock), PE = pred_end(ContBlock);
       PI != PE; ++PI)
    PN->addIncoming(llvm::ConstantInt::getTrue(), *PI);

  // Emit the RHS condition as a bool value.
  CGF.EmitBlock(RHSBlock);
  Value *RHSCond = CGF.EvaluateExprAsBool(E->getRHS());
  
  // Reaquire the RHS block, as there may be subblocks inserted.
  RHSBlock = Builder.GetInsertBlock();
  
  // Emit an unconditional branch from this block to ContBlock.  Insert an entry
  // into the phi node for the edge with the value of RHSCond.
  CGF.EmitBlock(ContBlock);
  PN->addIncoming(RHSCond, RHSBlock);
  
  // ZExt result to int.
  return Builder.CreateZExt(PN, CGF.LLVMIntTy, "lor.ext");
}

Value *ScalarExprEmitter::VisitBinComma(const BinaryOperator *E) {
  CGF.EmitStmt(E->getLHS());
  CGF.EnsureInsertPoint();
  return Visit(E->getRHS());
}

//===----------------------------------------------------------------------===//
//                             Other Operators
//===----------------------------------------------------------------------===//

/// isCheapEnoughToEvaluateUnconditionally - Return true if the specified
/// expression is cheap enough and side-effect-free enough to evaluate
/// unconditionally instead of conditionally.  This is used to convert control
/// flow into selects in some cases.
static bool isCheapEnoughToEvaluateUnconditionally(const Expr *E) {
  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E))
    return isCheapEnoughToEvaluateUnconditionally(PE->getSubExpr());
  
  // TODO: Allow anything we can constant fold to an integer or fp constant.
  if (isa<IntegerLiteral>(E) || isa<CharacterLiteral>(E) ||
      isa<FloatingLiteral>(E))
    return true;
  
  // Non-volatile automatic variables too, to get "cond ? X : Y" where
  // X and Y are local variables.
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (VD->hasLocalStorage() && !VD->getType().isVolatileQualified())
        return true;
  
  return false;
}


Value *ScalarExprEmitter::
VisitConditionalOperator(const ConditionalOperator *E) {
  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm.
  if (int Cond = CGF.ConstantFoldsToSimpleInteger(E->getCond())){
    Expr *Live = E->getLHS(), *Dead = E->getRHS();
    if (Cond == -1)
      std::swap(Live, Dead);
    
    // If the dead side doesn't have labels we need, and if the Live side isn't
    // the gnu missing ?: extension (which we could handle, but don't bother
    // to), just emit the Live part.
    if ((!Dead || !CGF.ContainsLabel(Dead)) &&  // No labels in dead part
        Live)                                   // Live part isn't missing.
      return Visit(Live);
  }
  
  
  // If this is a really simple expression (like x ? 4 : 5), emit this as a
  // select instead of as control flow.  We can only do this if it is cheap and
  // safe to evaluate the LHS and RHS unconditionally.
  if (E->getLHS() && isCheapEnoughToEvaluateUnconditionally(E->getLHS()) &&
      isCheapEnoughToEvaluateUnconditionally(E->getRHS())) {
    llvm::Value *CondV = CGF.EvaluateExprAsBool(E->getCond());
    llvm::Value *LHS = Visit(E->getLHS());
    llvm::Value *RHS = Visit(E->getRHS());
    return Builder.CreateSelect(CondV, LHS, RHS, "cond");
  }
  
  
  llvm::BasicBlock *LHSBlock = CGF.createBasicBlock("cond.true");
  llvm::BasicBlock *RHSBlock = CGF.createBasicBlock("cond.false");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("cond.end");
  Value *CondVal = 0;

  // If we have the GNU missing condition extension, evaluate the conditional
  // and then convert it to bool the hard way.  We do this explicitly
  // because we need the unconverted value for the missing middle value of
  // the ?:.
  if (E->getLHS() == 0) {
    CondVal = CGF.EmitScalarExpr(E->getCond());
    Value *CondBoolVal =
      CGF.EmitScalarConversion(CondVal, E->getCond()->getType(),
                               CGF.getContext().BoolTy);
    Builder.CreateCondBr(CondBoolVal, LHSBlock, RHSBlock);
  } else {
    // Otherwise, just use EmitBranchOnBoolExpr to get small and simple code for
    // the branch on bool.
    CGF.EmitBranchOnBoolExpr(E->getCond(), LHSBlock, RHSBlock);
  }
  
  CGF.EmitBlock(LHSBlock);
  
  // Handle the GNU extension for missing LHS.
  Value *LHS;
  if (E->getLHS())
    LHS = Visit(E->getLHS());
  else    // Perform promotions, to handle cases like "short ?: int"
    LHS = EmitScalarConversion(CondVal, E->getCond()->getType(), E->getType());
  
  LHSBlock = Builder.GetInsertBlock();
  CGF.EmitBranch(ContBlock);
  
  CGF.EmitBlock(RHSBlock);
  
  Value *RHS = Visit(E->getRHS());
  RHSBlock = Builder.GetInsertBlock();
  CGF.EmitBranch(ContBlock);
  
  CGF.EmitBlock(ContBlock);
  
  if (!LHS || !RHS) {
    assert(E->getType()->isVoidType() && "Non-void value should have a value");
    return 0;
  }
  
  // Create a PHI node for the real part.
  llvm::PHINode *PN = Builder.CreatePHI(LHS->getType(), "cond");
  PN->reserveOperandSpace(2);
  PN->addIncoming(LHS, LHSBlock);
  PN->addIncoming(RHS, RHSBlock);
  return PN;
}

Value *ScalarExprEmitter::VisitChooseExpr(ChooseExpr *E) {
  // Emit the LHS or RHS as appropriate.
  return
    Visit(E->isConditionTrue(CGF.getContext()) ? E->getLHS() : E->getRHS());
}

Value *ScalarExprEmitter::VisitOverloadExpr(OverloadExpr *E) {
  return CGF.EmitCallExpr(E->getFn(), E->arg_begin(),
                          E->arg_end(CGF.getContext())).getScalarVal();
}

Value *ScalarExprEmitter::VisitVAArgExpr(VAArgExpr *VE) {
  llvm::Value *ArgValue = CGF.EmitVAListRef(VE->getSubExpr());

  llvm::Value *ArgPtr = CGF.EmitVAArg(ArgValue, VE->getType());

  // If EmitVAArg fails, we fall back to the LLVM instruction.
  if (!ArgPtr) 
    return Builder.CreateVAArg(ArgValue, ConvertType(VE->getType()));

  // FIXME: volatile?
  return Builder.CreateLoad(ArgPtr);
}

Value *ScalarExprEmitter::VisitObjCEncodeExpr(const ObjCEncodeExpr *E) {
  std::string str;
  CGF.getContext().getObjCEncodingForType(E->getEncodedType(), str);
  
  llvm::Constant *C = llvm::ConstantArray::get(str);
  C = new llvm::GlobalVariable(C->getType(), true, 
                               llvm::GlobalValue::InternalLinkage,
                               C, ".str", &CGF.CGM.getModule());
  llvm::Constant *Zero = llvm::Constant::getNullValue(llvm::Type::Int32Ty);
  llvm::Constant *Zeros[] = { Zero, Zero };
  C = llvm::ConstantExpr::getGetElementPtr(C, Zeros, 2);
  
  return C;
}

//===----------------------------------------------------------------------===//
//                         Entry Point into this File
//===----------------------------------------------------------------------===//

/// EmitComplexExpr - Emit the computation of the specified expression of
/// complex type, ignoring the result.
Value *CodeGenFunction::EmitScalarExpr(const Expr *E) {
  assert(E && !hasAggregateLLVMType(E->getType()) &&
         "Invalid scalar expression to emit");
  
  return ScalarExprEmitter(*this).Visit(const_cast<Expr*>(E));
}

/// EmitScalarConversion - Emit a conversion from the specified type to the
/// specified destination type, both of which are LLVM scalar types.
Value *CodeGenFunction::EmitScalarConversion(Value *Src, QualType SrcTy,
                                             QualType DstTy) {
  assert(!hasAggregateLLVMType(SrcTy) && !hasAggregateLLVMType(DstTy) &&
         "Invalid scalar expression to emit");
  return ScalarExprEmitter(*this).EmitScalarConversion(Src, SrcTy, DstTy);
}

/// EmitComplexToScalarConversion - Emit a conversion from the specified
/// complex type to the specified destination type, where the destination
/// type is an LLVM scalar type.
Value *CodeGenFunction::EmitComplexToScalarConversion(ComplexPairTy Src,
                                                      QualType SrcTy,
                                                      QualType DstTy) {
  assert(SrcTy->isAnyComplexType() && !hasAggregateLLVMType(DstTy) &&
         "Invalid complex -> scalar conversion");
  return ScalarExprEmitter(*this).EmitComplexToScalarConversion(Src, SrcTy,
                                                                DstTy);
}

Value *CodeGenFunction::EmitShuffleVector(Value* V1, Value *V2, ...) {
  assert(V1->getType() == V2->getType() &&
         "Vector operands must be of the same type");
  unsigned NumElements = 
    cast<llvm::VectorType>(V1->getType())->getNumElements();
  
  va_list va;
  va_start(va, V2);
  
  llvm::SmallVector<llvm::Constant*, 16> Args;
  for (unsigned i = 0; i < NumElements; i++) {
    int n = va_arg(va, int);
    assert(n >= 0 && n < (int)NumElements * 2 && 
           "Vector shuffle index out of bounds!");
    Args.push_back(llvm::ConstantInt::get(llvm::Type::Int32Ty, n));
  }
  
  const char *Name = va_arg(va, const char *);
  va_end(va);
  
  llvm::Constant *Mask = llvm::ConstantVector::get(&Args[0], NumElements);
  
  return Builder.CreateShuffleVector(V1, V2, Mask, Name);
}

llvm::Value *CodeGenFunction::EmitVector(llvm::Value * const *Vals, 
                                         unsigned NumVals, bool isSplat) {
  llvm::Value *Vec
    = llvm::UndefValue::get(llvm::VectorType::get(Vals[0]->getType(), NumVals));
  
  for (unsigned i = 0, e = NumVals; i != e; ++i) {
    llvm::Value *Val = isSplat ? Vals[0] : Vals[i];
    llvm::Value *Idx = llvm::ConstantInt::get(llvm::Type::Int32Ty, i);
    Vec = Builder.CreateInsertElement(Vec, Val, Idx, "tmp");
  }
  
  return Vec;  
}
