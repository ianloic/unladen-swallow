//===--- StmtSerialization.cpp - Serialization of Statements --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the type-specific methods for serializing statements
// and expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TypeTraits.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "llvm/Bitcode/Serialize.h"
#include "llvm/Bitcode/Deserialize.h"

using namespace clang;
using llvm::Serializer;
using llvm::Deserializer;

void Stmt::Emit(Serializer& S) const {
  S.FlushRecord();
  S.EmitInt(getStmtClass());
  EmitImpl(S);
  S.FlushRecord();
}  

Stmt* Stmt::Create(Deserializer& D, ASTContext& C) {
  StmtClass SC = static_cast<StmtClass>(D.ReadInt());
  
  switch (SC) {
    default:  
      assert (false && "Not implemented.");
      return NULL;
    
    case AddrLabelExprClass:
      return AddrLabelExpr::CreateImpl(D, C);
      
    case ArraySubscriptExprClass:
      return ArraySubscriptExpr::CreateImpl(D, C);
      
    case AsmStmtClass:
      return AsmStmt::CreateImpl(D, C);
      
    case BinaryOperatorClass:
      return BinaryOperator::CreateImpl(D, C);
      
    case BreakStmtClass:
      return BreakStmt::CreateImpl(D, C);
     
    case CallExprClass:
      return CallExpr::CreateImpl(D, C, CallExprClass);
      
    case CaseStmtClass:
      return CaseStmt::CreateImpl(D, C);
      
    case CharacterLiteralClass:
      return CharacterLiteral::CreateImpl(D, C);
      
    case ChooseExprClass:
      return ChooseExpr::CreateImpl(D, C);

    case CompoundAssignOperatorClass:
      return CompoundAssignOperator::CreateImpl(D, C);
      
    case CompoundLiteralExprClass:
      return CompoundLiteralExpr::CreateImpl(D, C);
      
    case CompoundStmtClass:
      return CompoundStmt::CreateImpl(D, C);
    
    case ConditionalOperatorClass:
      return ConditionalOperator::CreateImpl(D, C);
      
    case ContinueStmtClass:
      return ContinueStmt::CreateImpl(D, C);
      
    case DeclRefExprClass:
      return DeclRefExpr::CreateImpl(D, C);
      
    case DeclStmtClass:
      return DeclStmt::CreateImpl(D, C);
      
    case DefaultStmtClass:
      return DefaultStmt::CreateImpl(D, C);
    
    case DoStmtClass:
      return DoStmt::CreateImpl(D, C);
      
    case FloatingLiteralClass:
      return FloatingLiteral::CreateImpl(D, C);

    case ForStmtClass:
      return ForStmt::CreateImpl(D, C);
    
    case GNUNullExprClass:
      return GNUNullExpr::CreateImpl(D, C);

    case GotoStmtClass:
      return GotoStmt::CreateImpl(D, C);
      
    case IfStmtClass:
      return IfStmt::CreateImpl(D, C);
    
    case ImaginaryLiteralClass:
      return ImaginaryLiteral::CreateImpl(D, C);
      
    case ImplicitCastExprClass:
      return ImplicitCastExpr::CreateImpl(D, C);

    case CStyleCastExprClass:
      return CStyleCastExpr::CreateImpl(D, C);
      
    case IndirectGotoStmtClass:
      return IndirectGotoStmt::CreateImpl(D, C);
      
    case InitListExprClass:
      return InitListExpr::CreateImpl(D, C);
      
    case IntegerLiteralClass:
      return IntegerLiteral::CreateImpl(D, C);
      
    case LabelStmtClass:
      return LabelStmt::CreateImpl(D, C);
      
    case MemberExprClass:
      return MemberExpr::CreateImpl(D, C);
      
    case NullStmtClass:
      return NullStmt::CreateImpl(D, C);
      
    case ParenExprClass:
      return ParenExpr::CreateImpl(D, C);
      
    case PredefinedExprClass:
      return PredefinedExpr::CreateImpl(D, C);
      
    case ReturnStmtClass:
      return ReturnStmt::CreateImpl(D, C);
    
    case SizeOfAlignOfExprClass:
      return SizeOfAlignOfExpr::CreateImpl(D, C);
      
    case StmtExprClass:
      return StmtExpr::CreateImpl(D, C);
      
    case StringLiteralClass:
      return StringLiteral::CreateImpl(D, C);
      
    case SwitchStmtClass:
      return SwitchStmt::CreateImpl(D, C);
      
    case UnaryOperatorClass:
      return UnaryOperator::CreateImpl(D, C);
      
    case WhileStmtClass:
      return WhileStmt::CreateImpl(D, C);
      
    //==--------------------------------------==//
    //    Objective C
    //==--------------------------------------==//
    
    case ObjCAtCatchStmtClass:
      return ObjCAtCatchStmt::CreateImpl(D, C);
      
    case ObjCAtFinallyStmtClass:
      return ObjCAtFinallyStmt::CreateImpl(D, C);
    
    case ObjCAtSynchronizedStmtClass:
      return ObjCAtSynchronizedStmt::CreateImpl(D, C);
      
    case ObjCAtThrowStmtClass:
      return ObjCAtThrowStmt::CreateImpl(D, C);
      
    case ObjCAtTryStmtClass:
      return ObjCAtTryStmt::CreateImpl(D, C);
    
    case ObjCEncodeExprClass:
      return ObjCEncodeExpr::CreateImpl(D, C);
      
    case ObjCForCollectionStmtClass:
      return ObjCForCollectionStmt::CreateImpl(D, C);
      
    case ObjCIvarRefExprClass:
      return ObjCIvarRefExpr::CreateImpl(D, C);
      
    case ObjCMessageExprClass:
      return ObjCMessageExpr::CreateImpl(D, C);
      
    case ObjCSelectorExprClass:
      return ObjCSelectorExpr::CreateImpl(D, C);
      
    case ObjCStringLiteralClass:
      return ObjCStringLiteral::CreateImpl(D, C);
      
    case ObjCSuperExprClass:
      return ObjCSuperExpr::CreateImpl(D, C);

    //==--------------------------------------==//
    //    C++
    //==--------------------------------------==//
      
    case CXXOperatorCallExprClass:
      return CXXOperatorCallExpr::CreateImpl(D, C, CXXOperatorCallExprClass);

    case CXXDefaultArgExprClass:
      return CXXDefaultArgExpr::CreateImpl(D, C);      

    case CXXFunctionalCastExprClass:
      return CXXFunctionalCastExpr::CreateImpl(D, C);

    case CXXStaticCastExprClass:
      return CXXStaticCastExpr::CreateImpl(D, C, SC);

    case CXXDynamicCastExprClass:
      return CXXDynamicCastExpr::CreateImpl(D, C, SC);

    case CXXReinterpretCastExprClass:
      return CXXReinterpretCastExpr::CreateImpl(D, C, SC);

    case CXXConstCastExprClass:
      return CXXConstCastExpr::CreateImpl(D, C, SC);

    case CXXTypeidExprClass:
      return CXXTypeidExpr::CreateImpl(D, C);

    case CXXThisExprClass:
      return CXXThisExpr::CreateImpl(D, C);

    case CXXTemporaryObjectExprClass:
      return CXXTemporaryObjectExpr::CreateImpl(D, C);

    case CXXZeroInitValueExprClass:
      return CXXZeroInitValueExpr::CreateImpl(D, C);

    case CXXNewExprClass:
      return CXXNewExpr::CreateImpl(D, C);

    case CXXDeleteExprClass:
      return CXXDeleteExpr::CreateImpl(D, C);

    case CXXDependentNameExprClass:
      return CXXDependentNameExpr::CreateImpl(D, C);

    case CXXCatchStmtClass:
      return CXXCatchStmt::CreateImpl(D, C);

    case CXXTryStmtClass:
      return CXXTryStmt::CreateImpl(D, C);

    case QualifiedDeclRefExprClass:
      return QualifiedDeclRefExpr::CreateImpl(D, C);
  }
}

//===----------------------------------------------------------------------===//
//   C Serialization
//===----------------------------------------------------------------------===//

void AddrLabelExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(AmpAmpLoc);
  S.Emit(LabelLoc);
  S.EmitPtr(Label);
}

AddrLabelExpr* AddrLabelExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  SourceLocation AALoc = SourceLocation::ReadVal(D);
  SourceLocation LLoc = SourceLocation::ReadVal(D);
  AddrLabelExpr* expr = new AddrLabelExpr(AALoc,LLoc,NULL,t);
  D.ReadPtr(expr->Label); // Pointer may be backpatched.
  return expr;
}

void ArraySubscriptExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(RBracketLoc);
  S.BatchEmitOwnedPtrs(getLHS(),getRHS());
}

ArraySubscriptExpr* ArraySubscriptExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  Expr *LHS, *RHS;
  D.BatchReadOwnedPtrs(LHS, RHS, C);
  return new ArraySubscriptExpr(LHS,RHS,t,L);  
}

void AsmStmt::EmitImpl(Serializer& S) const {
  S.Emit(AsmLoc);
  
  getAsmString()->EmitImpl(S);
  S.Emit(RParenLoc);  

  S.EmitBool(IsVolatile);
  S.EmitBool(IsSimple);
  S.EmitInt(NumOutputs);
  S.EmitInt(NumInputs);

  unsigned size = NumOutputs + NumInputs;

  for (unsigned i = 0; i < size; ++i)
    S.EmitCStr(Names[i].c_str());
  
  for (unsigned i = 0; i < size; ++i)
    Constraints[i]->EmitImpl(S);

  for (unsigned i = 0; i < size; ++i)
    S.EmitOwnedPtr(Exprs[i]);
  
  S.EmitInt(Clobbers.size());
  for (unsigned i = 0, e = Clobbers.size(); i != e; ++i)
    Clobbers[i]->EmitImpl(S);
}

AsmStmt* AsmStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation ALoc = SourceLocation::ReadVal(D);
  StringLiteral *AsmStr = StringLiteral::CreateImpl(D, C);
  SourceLocation PLoc = SourceLocation::ReadVal(D);
  
  bool IsVolatile = D.ReadBool();
  bool IsSimple = D.ReadBool();
  AsmStmt *Stmt = new AsmStmt(ALoc, IsSimple, IsVolatile, 0, 0, 0, 0, 0,  
                              AsmStr, 
                              0, 0, PLoc);  

  Stmt->NumOutputs = D.ReadInt();
  Stmt->NumInputs = D.ReadInt();
  
  unsigned size = Stmt->NumOutputs + Stmt->NumInputs;

  Stmt->Names.reserve(size);
  for (unsigned i = 0; i < size; ++i) {
    std::vector<char> data;
    D.ReadCStr(data, false);

    Stmt->Names.push_back(std::string(data.begin(), data.end()));
  }    

  Stmt->Constraints.reserve(size);
  for (unsigned i = 0; i < size; ++i)
    Stmt->Constraints.push_back(StringLiteral::CreateImpl(D, C));
  
  Stmt->Exprs.reserve(size);
  for (unsigned i = 0; i < size; ++i)
    Stmt->Exprs.push_back(D.ReadOwnedPtr<Expr>(C));
  
  unsigned NumClobbers = D.ReadInt();
  Stmt->Clobbers.reserve(NumClobbers);
  for (unsigned i = 0; i < NumClobbers; ++i)
    Stmt->Clobbers.push_back(StringLiteral::CreateImpl(D, C));
  
  return Stmt;
}

void BinaryOperator::EmitImpl(Serializer& S) const {
  S.EmitInt(Opc);
  S.Emit(OpLoc);;
  S.Emit(getType());
  S.BatchEmitOwnedPtrs(getLHS(),getRHS());
}

BinaryOperator* BinaryOperator::CreateImpl(Deserializer& D, ASTContext& C) {
  Opcode Opc = static_cast<Opcode>(D.ReadInt());
  SourceLocation OpLoc = SourceLocation::ReadVal(D);
  QualType Result = QualType::ReadVal(D);
  Expr *LHS, *RHS;
  D.BatchReadOwnedPtrs(LHS, RHS, C);

  return new BinaryOperator(LHS,RHS,Opc,Result,OpLoc);
}

void BreakStmt::EmitImpl(Serializer& S) const {
  S.Emit(BreakLoc);
}

BreakStmt* BreakStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  return new BreakStmt(Loc);
}

void CallExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(RParenLoc);
  S.EmitInt(NumArgs);
  S.BatchEmitOwnedPtrs(NumArgs+1, SubExprs);  
}

CallExpr* CallExpr::CreateImpl(Deserializer& D, ASTContext& C, StmtClass SC) {
  QualType t = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  unsigned NumArgs = D.ReadInt();
  Stmt** SubExprs = new Stmt*[NumArgs+1];
  D.BatchReadOwnedPtrs(NumArgs+1, SubExprs, C);

  return new CallExpr(SC, SubExprs,NumArgs,t,L);  
}

void CaseStmt::EmitImpl(Serializer& S) const {
  S.Emit(CaseLoc);
  S.EmitPtr(getNextSwitchCase());
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR,&SubExprs[0]);
}

CaseStmt* CaseStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation CaseLoc = SourceLocation::ReadVal(D);
  CaseStmt* stmt = new CaseStmt(NULL,NULL,NULL,CaseLoc);  
  D.ReadPtr(stmt->NextSwitchCase);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, &stmt->SubExprs[0], C);
  return stmt;
}

void CStyleCastExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(getTypeAsWritten());
  S.Emit(LPLoc);
  S.Emit(RPLoc);
  S.EmitOwnedPtr(getSubExpr());
}

CStyleCastExpr* CStyleCastExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  QualType writtenTy = QualType::ReadVal(D);
  SourceLocation LPLoc = SourceLocation::ReadVal(D);
  SourceLocation RPLoc = SourceLocation::ReadVal(D);
  Expr* Op = D.ReadOwnedPtr<Expr>(C);
  return new CStyleCastExpr(t,Op,writtenTy,LPLoc,RPLoc);
}

void CharacterLiteral::EmitImpl(Serializer& S) const {
  S.Emit(Value);
  S.Emit(Loc);
  S.EmitBool(IsWide);
  S.Emit(getType());
}

CharacterLiteral* CharacterLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  unsigned value = D.ReadInt();
  SourceLocation Loc = SourceLocation::ReadVal(D);
  bool iswide = D.ReadBool();
  QualType T = QualType::ReadVal(D);
  return new CharacterLiteral(value,iswide,T,Loc);
}

void CompoundAssignOperator::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(ComputationType);
  S.Emit(getOperatorLoc());
  S.EmitInt(getOpcode());
  S.BatchEmitOwnedPtrs(getLHS(),getRHS());
}

CompoundAssignOperator* 
CompoundAssignOperator::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  QualType c = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  Opcode Opc = static_cast<Opcode>(D.ReadInt());
  Expr* LHS, *RHS;
  D.BatchReadOwnedPtrs(LHS, RHS, C);
  
  return new CompoundAssignOperator(LHS,RHS,Opc,t,c,L);
}

void CompoundLiteralExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(getLParenLoc());
  S.EmitBool(isFileScope());
  S.EmitOwnedPtr(Init);
}

CompoundLiteralExpr* CompoundLiteralExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType Q = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  bool fileScope = D.ReadBool();
  Expr* Init = D.ReadOwnedPtr<Expr>(C);
  return new CompoundLiteralExpr(L, Q, Init, fileScope);
}

void CompoundStmt::EmitImpl(Serializer& S) const {
  S.Emit(LBracLoc);
  S.Emit(RBracLoc);
  S.Emit(Body.size());
  
  for (const_body_iterator I=body_begin(), E=body_end(); I!=E; ++I)
    S.EmitOwnedPtr(*I);
}

CompoundStmt* CompoundStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation LB = SourceLocation::ReadVal(D);
  SourceLocation RB = SourceLocation::ReadVal(D);
  unsigned size = D.ReadInt();
  
  CompoundStmt* stmt = new CompoundStmt(NULL,0,LB,RB);
  
  stmt->Body.reserve(size);
  
  for (unsigned i = 0; i < size; ++i)
    stmt->Body.push_back(D.ReadOwnedPtr<Stmt>(C));
  
  return stmt;
}

void ConditionalOperator::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR, SubExprs);
}

ConditionalOperator* ConditionalOperator::CreateImpl(Deserializer& D,
                                                     ASTContext& C) {

  QualType t = QualType::ReadVal(D);
  ConditionalOperator* c = new ConditionalOperator(NULL,NULL,NULL,t);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, c->SubExprs, C);
  return c;
}

void ContinueStmt::EmitImpl(Serializer& S) const {
  S.Emit(ContinueLoc);
}

ContinueStmt* ContinueStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  return new ContinueStmt(Loc);
}

void DeclStmt::EmitImpl(Serializer& S) const {
  S.Emit(StartLoc);
  S.Emit(EndLoc);
  S.Emit(DG);
}
    
DeclStmt* DeclStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation StartLoc = SourceLocation::ReadVal(D);
  SourceLocation EndLoc = SourceLocation::ReadVal(D); 
  DeclGroupOwningRef DG;
  return new DeclStmt(DG.Read(D, C), StartLoc, EndLoc);
}

void DeclRefExpr::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitPtr(getDecl());
}

DeclRefExpr* DeclRefExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);  
  DeclRefExpr *DRE = new DeclRefExpr(0, T, Loc);
  D.ReadPtr(DRE->D);
  return DRE;
}

void DefaultStmt::EmitImpl(Serializer& S) const {
  S.Emit(DefaultLoc);
  S.EmitOwnedPtr(getSubStmt());
  S.EmitPtr(getNextSwitchCase());
}

DefaultStmt* DefaultStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  Stmt* SubStmt = D.ReadOwnedPtr<Stmt>(C);
  
  DefaultStmt* stmt = new DefaultStmt(Loc,SubStmt);
  stmt->setNextSwitchCase(D.ReadPtr<SwitchCase>());
  
  return stmt;
}

void DoStmt::EmitImpl(Serializer& S) const {
  S.Emit(DoLoc);
  S.EmitOwnedPtr(getCond());
  S.EmitOwnedPtr(getBody());
}

DoStmt* DoStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation DoLoc = SourceLocation::ReadVal(D);
  Expr* Cond = D.ReadOwnedPtr<Expr>(C);
  Stmt* Body = D.ReadOwnedPtr<Stmt>(C);
  return new DoStmt(Body,Cond,DoLoc);
}

void FloatingLiteral::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitBool(isExact());
  S.Emit(Value);
}

FloatingLiteral* FloatingLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType t = QualType::ReadVal(D);
  bool isExact = D.ReadBool();
  llvm::APFloat Val = llvm::APFloat::ReadVal(D);
  FloatingLiteral* expr = new FloatingLiteral(Val,&isExact,t,Loc);
  return expr;
}

void ForStmt::EmitImpl(Serializer& S) const {
  S.Emit(ForLoc);
  S.EmitOwnedPtr(getInit());
  S.EmitOwnedPtr(getCond());
  S.EmitOwnedPtr(getInc());
  S.EmitOwnedPtr(getBody());
}

ForStmt* ForStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation ForLoc = SourceLocation::ReadVal(D);
  Stmt* Init = D.ReadOwnedPtr<Stmt>(C);
  Expr* Cond = D.ReadOwnedPtr<Expr>(C);
  Expr* Inc = D.ReadOwnedPtr<Expr>(C);
  Stmt* Body = D.ReadOwnedPtr<Stmt>(C);
  return new ForStmt(Init,Cond,Inc,Body,ForLoc);
}

void GotoStmt::EmitImpl(Serializer& S) const {
  S.Emit(GotoLoc);
  S.Emit(LabelLoc);
  S.EmitPtr(Label);
}

GotoStmt* GotoStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation GotoLoc = SourceLocation::ReadVal(D);
  SourceLocation LabelLoc = SourceLocation::ReadVal(D);
  GotoStmt* stmt = new GotoStmt(NULL,GotoLoc,LabelLoc);
  D.ReadPtr(stmt->Label); // This pointer may be backpatched later.
  return stmt;  
}

void IfStmt::EmitImpl(Serializer& S) const {
  S.Emit(IfLoc);
  S.EmitOwnedPtr(getCond());
  S.EmitOwnedPtr(getThen());
  S.EmitOwnedPtr(getElse());
}

IfStmt* IfStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  Expr* Cond = D.ReadOwnedPtr<Expr>(C);
  Stmt* Then = D.ReadOwnedPtr<Stmt>(C);
  Stmt* Else = D.ReadOwnedPtr<Stmt>(C);
  return new IfStmt(L,Cond,Then,Else);
}

void ImaginaryLiteral::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.EmitOwnedPtr(Val);    
}

ImaginaryLiteral* ImaginaryLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  Expr* expr = D.ReadOwnedPtr<Expr>(C);
  assert (isa<FloatingLiteral>(expr) || isa<IntegerLiteral>(expr));
  return new ImaginaryLiteral(expr,t);
}

void ImplicitCastExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.EmitOwnedPtr(getSubExpr());
  S.Emit(LvalueCast);
}

ImplicitCastExpr* ImplicitCastExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  Expr* Op = D.ReadOwnedPtr<Expr>(C);
  bool isLvalue = D.ReadBool();
  return new ImplicitCastExpr(t,Op,isLvalue);
}

void IndirectGotoStmt::EmitImpl(Serializer& S) const {
  S.EmitOwnedPtr(Target);  
}

IndirectGotoStmt* IndirectGotoStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  Expr* Target = D.ReadOwnedPtr<Expr>(C);
  return new IndirectGotoStmt(Target);
}

void InitListExpr::EmitImpl(Serializer& S) const {
  S.Emit(LBraceLoc);
  S.Emit(RBraceLoc);
  S.EmitInt(InitExprs.size());
  if (!InitExprs.empty()) S.BatchEmitOwnedPtrs(InitExprs.size(), &InitExprs[0]);
}

InitListExpr* InitListExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  InitListExpr* expr = new InitListExpr();
  expr->LBraceLoc = SourceLocation::ReadVal(D);
  expr->RBraceLoc = SourceLocation::ReadVal(D);
  unsigned size = D.ReadInt();
  assert(size);
  expr->InitExprs.reserve(size);
  for (unsigned i = 0 ; i < size; ++i) expr->InitExprs.push_back(0);

  D.BatchReadOwnedPtrs(size, &expr->InitExprs[0], C);
  return expr;
}

void IntegerLiteral::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.Emit(getValue());
}

IntegerLiteral* IntegerLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  
  // Create a dummy APInt because it is more efficient to deserialize
  // it in place with the deserialized IntegerLiteral. (fewer copies)
  llvm::APInt temp;  
  IntegerLiteral* expr = new IntegerLiteral(temp,T,Loc);
  D.Read(expr->Value);
  
  return expr;
}

void LabelStmt::EmitImpl(Serializer& S) const {
  S.EmitPtr(Label);
  S.Emit(IdentLoc);
  S.EmitOwnedPtr(SubStmt);
}

LabelStmt* LabelStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  IdentifierInfo* Label = D.ReadPtr<IdentifierInfo>();
  SourceLocation IdentLoc = SourceLocation::ReadVal(D);
  Stmt* SubStmt = D.ReadOwnedPtr<Stmt>(C);
  return new LabelStmt(IdentLoc,Label,SubStmt);
}

void MemberExpr::EmitImpl(Serializer& S) const {
  S.Emit(MemberLoc);
  S.EmitPtr(MemberDecl);
  S.EmitBool(IsArrow);
  S.Emit(getType());
  S.EmitOwnedPtr(Base);
}

MemberExpr* MemberExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  NamedDecl* MemberDecl = cast<NamedDecl>(D.ReadPtr<Decl>());
  bool IsArrow = D.ReadBool();
  QualType T = QualType::ReadVal(D);
  Expr* base = D.ReadOwnedPtr<Expr>(C);
  
  return new MemberExpr(base,IsArrow,MemberDecl,L,T); 
}

void NullStmt::EmitImpl(Serializer& S) const {
  S.Emit(SemiLoc);
}

NullStmt* NullStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation SemiLoc = SourceLocation::ReadVal(D);
  return new NullStmt(SemiLoc);
}

void ParenExpr::EmitImpl(Serializer& S) const {
  S.Emit(L);
  S.Emit(R);
  S.EmitOwnedPtr(Val);
}

ParenExpr* ParenExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  SourceLocation R = SourceLocation::ReadVal(D);
  Expr* val = D.ReadOwnedPtr<Expr>(C);
  return new ParenExpr(L,R,val);
}

void PredefinedExpr::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.EmitInt(getIdentType());
  S.Emit(getType());  
}

PredefinedExpr* PredefinedExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  IdentType it = static_cast<IdentType>(D.ReadInt());
  QualType Q = QualType::ReadVal(D);
  return new PredefinedExpr(Loc,Q,it);
}

void ReturnStmt::EmitImpl(Serializer& S) const {
  S.Emit(RetLoc);
  S.EmitOwnedPtr(RetExpr);
}

ReturnStmt* ReturnStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation RetLoc = SourceLocation::ReadVal(D);
  Expr* RetExpr = D.ReadOwnedPtr<Expr>(C);
  return new ReturnStmt(RetLoc,RetExpr);
}

void SizeOfAlignOfExpr::EmitImpl(Serializer& S) const {
  S.EmitBool(isSizeof);
  S.EmitBool(isType);
  if (isType)
    S.Emit(getArgumentType());
  else
    S.EmitOwnedPtr(getArgumentExpr());
  S.Emit(getType());
  S.Emit(OpLoc);
  S.Emit(RParenLoc);
}

SizeOfAlignOfExpr*
SizeOfAlignOfExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  bool isSizeof = D.ReadBool();
  bool isType = D.ReadBool();
  void *Argument;
  if (isType)
    Argument = QualType::ReadVal(D).getAsOpaquePtr();
  else
    Argument = D.ReadOwnedPtr<Expr>(C);
  QualType Res = QualType::ReadVal(D);
  SourceLocation OpLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  
  return new SizeOfAlignOfExpr(isSizeof, isType, Argument, Res,
                               OpLoc, RParenLoc);
}

void StmtExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(LParenLoc);
  S.Emit(RParenLoc);
  S.EmitOwnedPtr(SubStmt);
}

StmtExpr* StmtExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  SourceLocation R = SourceLocation::ReadVal(D);
  CompoundStmt* SubStmt = cast<CompoundStmt>(D.ReadOwnedPtr<Stmt>(C));
  return new StmtExpr(SubStmt,t,L,R);
}

void TypesCompatibleExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(BuiltinLoc);
  S.Emit(RParenLoc);
  S.Emit(Type1);
  S.Emit(Type2);
}

TypesCompatibleExpr* TypesCompatibleExpr::CreateImpl(llvm::Deserializer& D, 
                                                     ASTContext& C) {
  QualType RT = QualType::ReadVal(D);
  SourceLocation BL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  QualType T1 = QualType::ReadVal(D);
  QualType T2 = QualType::ReadVal(D);
  return new TypesCompatibleExpr(RT, BL, T1, T2, RP);
}

void ShuffleVectorExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(BuiltinLoc);
  S.Emit(RParenLoc);
  S.EmitInt(NumExprs);
  S.BatchEmitOwnedPtrs(NumExprs, &SubExprs[0]);
}

ShuffleVectorExpr* ShuffleVectorExpr::CreateImpl(llvm::Deserializer& D, 
                                                 ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  SourceLocation BL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  unsigned NumExprs = D.ReadInt();
  // FIXME: Avoid extra allocation.
  llvm::SmallVector<Expr*, 4> Exprs(NumExprs);
  D.BatchReadOwnedPtrs(NumExprs, Exprs.begin(), C);
  return new ShuffleVectorExpr(Exprs.begin(), NumExprs, T, BL, RP);
}

void ChooseExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(BuiltinLoc);
  S.Emit(RParenLoc);
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR, &SubExprs[0]);
}

ChooseExpr* ChooseExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  SourceLocation BL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  ChooseExpr *CE = new ChooseExpr(BL, 0, 0, 0, T, RP);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, &CE->SubExprs[0], C);
  return CE;
}

void GNUNullExpr::EmitImpl(llvm::Serializer &S) const {
  S.Emit(getType());
  S.Emit(TokenLoc);
}

GNUNullExpr *GNUNullExpr::CreateImpl(llvm::Deserializer &D, ASTContext &C) {
  QualType T = QualType::ReadVal(D);
  SourceLocation TL = SourceLocation::ReadVal(D);
  return new GNUNullExpr(T, TL);
}

void OverloadExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(BuiltinLoc);
  S.Emit(RParenLoc);
  S.EmitInt(FnIndex);
  S.EmitInt(NumExprs);
  S.BatchEmitOwnedPtrs(NumExprs, &SubExprs[0]);
}

OverloadExpr* OverloadExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  SourceLocation BL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  unsigned FnIndex = D.ReadInt();
  unsigned NumExprs = D.ReadInt();
  // FIXME: Avoid extra allocation.
  llvm::SmallVector<Expr*, 4> Exprs(NumExprs);
  D.BatchReadOwnedPtrs(NumExprs, Exprs.begin(), C);
  return new OverloadExpr(Exprs.begin(), NumExprs, FnIndex, T, BL, RP);
}

void VAArgExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(BuiltinLoc);
  S.Emit(RParenLoc);
  S.EmitOwnedPtr(getSubExpr());
}

VAArgExpr* VAArgExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  SourceLocation BL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  Expr *E = D.ReadOwnedPtr<Expr>(C);
  return new VAArgExpr(BL, E, T, RP);
}

void StringLiteral::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(firstTokLoc);
  S.Emit(lastTokLoc);
  S.EmitBool(isWide());
  S.Emit(getByteLength());

  for (unsigned i = 0 ; i < ByteLength; ++i)
    S.EmitInt(StrData[i]);
}

StringLiteral* StringLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  SourceLocation firstTokLoc = SourceLocation::ReadVal(D);
  SourceLocation lastTokLoc = SourceLocation::ReadVal(D);
  bool isWide = D.ReadBool();
  unsigned ByteLength = D.ReadInt();
  
  StringLiteral* sl = new StringLiteral(NULL,0,isWide,t,firstTokLoc,lastTokLoc);

  char* StrData = new char[ByteLength];
  for (unsigned i = 0; i < ByteLength; ++i)
    StrData[i] = (char) D.ReadInt();

  sl->ByteLength = ByteLength;
  sl->StrData = StrData;
  
  return sl;
}

void SwitchStmt::EmitImpl(Serializer& S) const {
  S.Emit(SwitchLoc);
  S.EmitOwnedPtr(getCond());
  S.EmitOwnedPtr(getBody());
  S.EmitPtr(FirstCase);  
}

SwitchStmt* SwitchStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  Stmt* Cond = D.ReadOwnedPtr<Stmt>(C);
  Stmt* Body = D.ReadOwnedPtr<Stmt>(C);
  SwitchCase* FirstCase = cast<SwitchCase>(D.ReadPtr<Stmt>());
  
  SwitchStmt* stmt = new SwitchStmt(cast<Expr>(Cond));
  stmt->setBody(Body,Loc);
  stmt->FirstCase = FirstCase;
  
  return stmt;
}

void UnaryOperator::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(Loc);
  S.EmitInt(Opc);
  S.EmitOwnedPtr(Val);
}

UnaryOperator* UnaryOperator::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType t = QualType::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  Opcode Opc = static_cast<Opcode>(D.ReadInt());
  Expr* Val = D.ReadOwnedPtr<Expr>(C);
  return new UnaryOperator(Val,Opc,t,L);
}

void WhileStmt::EmitImpl(Serializer& S) const {
  S.Emit(WhileLoc);
  S.EmitOwnedPtr(getCond());
  S.EmitOwnedPtr(getBody());
}

WhileStmt* WhileStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation WhileLoc = SourceLocation::ReadVal(D);
  Expr* Cond = D.ReadOwnedPtr<Expr>(C);
  Stmt* Body = D.ReadOwnedPtr<Stmt>(C);
  return new WhileStmt(Cond,Body,WhileLoc);
}

//===----------------------------------------------------------------------===//
//   Objective C Serialization
//===----------------------------------------------------------------------===//

void ObjCAtCatchStmt::EmitImpl(Serializer& S) const {
  S.Emit(AtCatchLoc);
  S.Emit(RParenLoc);
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR, &SubExprs[0]);
}

ObjCAtCatchStmt* ObjCAtCatchStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation AtCatchLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  
  ObjCAtCatchStmt* stmt = new ObjCAtCatchStmt(AtCatchLoc,RParenLoc);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, &stmt->SubExprs[0], C);

  return stmt;
}

void ObjCAtFinallyStmt::EmitImpl(Serializer& S) const {
  S.Emit(AtFinallyLoc);
  S.EmitOwnedPtr(AtFinallyStmt);
}

ObjCAtFinallyStmt* ObjCAtFinallyStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  Stmt* AtFinallyStmt = D.ReadOwnedPtr<Stmt>(C);
  return new ObjCAtFinallyStmt(Loc,AtFinallyStmt);  
}

void ObjCAtSynchronizedStmt::EmitImpl(Serializer& S) const {
  S.Emit(AtSynchronizedLoc);
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR,&SubStmts[0]);
 }

ObjCAtSynchronizedStmt* ObjCAtSynchronizedStmt::CreateImpl(Deserializer& D,
                                                           ASTContext& C) {

  SourceLocation L = SourceLocation::ReadVal(D);
  ObjCAtSynchronizedStmt* stmt = new ObjCAtSynchronizedStmt(L,0,0);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, &stmt->SubStmts[0], C);
  return stmt;
}

void ObjCAtThrowStmt::EmitImpl(Serializer& S) const {
  S.Emit(AtThrowLoc);
  S.EmitOwnedPtr(Throw);
}

ObjCAtThrowStmt* ObjCAtThrowStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  Stmt* Throw = D.ReadOwnedPtr<Stmt>(C);
  return new ObjCAtThrowStmt(L,Throw);  
}
  
void ObjCAtTryStmt::EmitImpl(Serializer& S) const {
  S.Emit(AtTryLoc);
  S.BatchEmitOwnedPtrs((unsigned) END_EXPR, &SubStmts[0]);
}

ObjCAtTryStmt* ObjCAtTryStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  ObjCAtTryStmt* stmt = new ObjCAtTryStmt(L,NULL,NULL,NULL);
  D.BatchReadOwnedPtrs((unsigned) END_EXPR, &stmt->SubStmts[0], C);
  return stmt;
}

void ObjCEncodeExpr::EmitImpl(Serializer& S) const {
  S.Emit(AtLoc);
  S.Emit(RParenLoc);
  S.Emit(getType());
  S.Emit(EncType);
}

ObjCEncodeExpr* ObjCEncodeExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation AtLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);  
  QualType T = QualType::ReadVal(D);
  QualType ET = QualType::ReadVal(D);
  return new ObjCEncodeExpr(T,ET,AtLoc,RParenLoc);
}

void ObjCForCollectionStmt::EmitImpl(Serializer& S) const {
  S.Emit(ForLoc);
  S.Emit(RParenLoc);
  S.BatchEmitOwnedPtrs(getElement(),getCollection(),getBody());
}

ObjCForCollectionStmt* ObjCForCollectionStmt::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation ForLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  Stmt* Element;
  Expr* Collection;
  Stmt* Body;
  D.BatchReadOwnedPtrs(Element, Collection, Body, C);
  return new ObjCForCollectionStmt(Element,Collection,Body,ForLoc, RParenLoc);
}

void ObjCProtocolExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.EmitPtr(Protocol);
  S.Emit(AtLoc);
  S.Emit(RParenLoc);
}

ObjCProtocolExpr* ObjCProtocolExpr::CreateImpl(llvm::Deserializer& D, 
                                               ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  ObjCProtocolDecl *PD = D.ReadPtr<ObjCProtocolDecl>();
  SourceLocation AL = SourceLocation::ReadVal(D);
  SourceLocation RP = SourceLocation::ReadVal(D);
  return new ObjCProtocolExpr(T, PD, AL, RP);
}

void ObjCIvarRefExpr::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitPtr(getDecl());
}
  
ObjCIvarRefExpr* ObjCIvarRefExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  ObjCIvarRefExpr* dr = new ObjCIvarRefExpr(NULL,T,Loc);
  D.ReadPtr(dr->D,false);  
  return dr;
}

void ObjCPropertyRefExpr::EmitImpl(Serializer& S) const {
  S.Emit(IdLoc);
  S.Emit(getType());
  S.EmitPtr(getProperty());
}

void ObjCKVCRefExpr::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitPtr(getGetterMethod());
  S.EmitPtr(getSetterMethod());
}
  
ObjCPropertyRefExpr* ObjCPropertyRefExpr::CreateImpl(Deserializer& D, 
                                                     ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  ObjCPropertyRefExpr* dr = new ObjCPropertyRefExpr(NULL,T,Loc,0);
  D.ReadPtr(dr->AsProperty,false);
  return dr;
}

ObjCKVCRefExpr* ObjCKVCRefExpr::CreateImpl(Deserializer& D, 
                                           ASTContext& C) {
  SourceLocation Loc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  ObjCKVCRefExpr* dr = new ObjCKVCRefExpr(NULL,T,NULL,Loc,0);
  D.ReadPtr(dr->Setter,false);
  D.ReadPtr(dr->Getter,false);
  return dr;
}

void ObjCMessageExpr::EmitImpl(Serializer& S) const {
  S.EmitInt(getFlag());
  S.Emit(getType());
  S.Emit(SelName);
  S.Emit(LBracloc);
  S.Emit(RBracloc);
  S.EmitInt(NumArgs);  
  S.EmitPtr(MethodProto);
  
  if (getReceiver())
    S.BatchEmitOwnedPtrs(NumArgs+1, SubExprs);
  else {
    ClassInfo Info = getClassInfo();

    if (Info.first) S.EmitPtr(Info.first);
    else S.EmitPtr(Info.second);

    S.BatchEmitOwnedPtrs(NumArgs, &SubExprs[ARGS_START]);
  }
}

ObjCMessageExpr* ObjCMessageExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  unsigned flags = D.ReadInt();
  QualType t = QualType::ReadVal(D);
  Selector S = Selector::ReadVal(D);
  SourceLocation L = SourceLocation::ReadVal(D);
  SourceLocation R = SourceLocation::ReadVal(D);
    
  // Construct an array for the subexpressions.
  unsigned NumArgs = D.ReadInt();
  Stmt** SubExprs = new Stmt*[NumArgs+1];
  
  // Construct the ObjCMessageExpr object using the special ctor.
  ObjCMessageExpr* ME = new ObjCMessageExpr(S, t, L, R, SubExprs, NumArgs);
  
  // Read in the MethodProto.  Read the instance variable directly
  // allows it to be backpatched.
  D.ReadPtr(ME->MethodProto);
  
  // Now read in the arguments.
  
  if ((flags & Flags) == IsInstMeth)
    D.BatchReadOwnedPtrs(NumArgs+1, SubExprs, C);
  else {
    // Read the pointer for Cls/ClassName.  The Deserializer will handle the
    // bit-mangling automatically.
    SubExprs[RECEIVER] = (Stmt*) ((uintptr_t) flags);
    D.ReadUIntPtr((uintptr_t&) SubExprs[RECEIVER]);
    
    // Read the arguments.
    D.BatchReadOwnedPtrs(NumArgs, &SubExprs[ARGS_START], C);
  }
  
  return ME;
}

void ObjCSelectorExpr::EmitImpl(Serializer& S) const {
  S.Emit(AtLoc);
  S.Emit(RParenLoc);
  S.Emit(getType());
  S.Emit(SelName);
}

ObjCSelectorExpr* ObjCSelectorExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation AtLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  Selector SelName = Selector::ReadVal(D);
  
  return new ObjCSelectorExpr(T,SelName,AtLoc,RParenLoc);
}

void ObjCStringLiteral::EmitImpl(Serializer& S) const {
  S.Emit(AtLoc);
  S.Emit(getType());
  S.EmitOwnedPtr(String);
}

ObjCStringLiteral* ObjCStringLiteral::CreateImpl(Deserializer& D, ASTContext& C) {
  SourceLocation L = SourceLocation::ReadVal(D);
  QualType T = QualType::ReadVal(D);
  StringLiteral* String = cast<StringLiteral>(D.ReadOwnedPtr<Stmt>(C));
  return new ObjCStringLiteral(String,T,L);
}

void ObjCSuperExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(Loc);
}

ObjCSuperExpr* ObjCSuperExpr::CreateImpl(llvm::Deserializer& D, ASTContext&) {
  QualType Ty = QualType::ReadVal(D);
  SourceLocation Loc = SourceLocation::ReadVal(D);
  return new ObjCSuperExpr(Loc, Ty);
}

//===----------------------------------------------------------------------===//
//   Serialization for Clang Extensions.
//===----------------------------------------------------------------------===//

void ExtVectorElementExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.EmitOwnedPtr(getBase());
  S.EmitPtr(&Accessor);
  S.Emit(AccessorLoc);
}

ExtVectorElementExpr* CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  Expr *B = D.ReadOwnedPtr<Expr>(C);
  IdentifierInfo *A = D.ReadPtr<IdentifierInfo>();
  SourceLocation AL = SourceLocation::ReadVal(D);
  return new ExtVectorElementExpr(T, B, *A, AL);
}

void BlockExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.EmitOwnedPtr(TheBlock);
}

BlockExpr* BlockExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  return new BlockExpr(cast<BlockDecl>(D.ReadOwnedPtr<Decl>(C)),T);
}

void BlockDeclRefExpr::EmitImpl(Serializer& S) const {
  S.Emit(Loc);
  S.Emit(getType());
  S.EmitBool(false);
  S.EmitPtr(getDecl());
}

BlockDeclRefExpr* BlockDeclRefExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  assert(0 && "Cannot deserialize BlockDeclRefExpr yet");
  return 0;
}

//===----------------------------------------------------------------------===//
//   C++ Serialization
//===----------------------------------------------------------------------===//
void CXXDefaultArgExpr::EmitImpl(Serializer& S) const {
  S.EmitPtr(Param);
}

CXXDefaultArgExpr *CXXDefaultArgExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  ParmVarDecl* Param = 0;
  D.ReadPtr(Param, false);
  return new CXXDefaultArgExpr(Param);
}

void CXXFunctionalCastExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(getTypeAsWritten());
  S.Emit(TyBeginLoc);
  S.Emit(RParenLoc);
  S.EmitOwnedPtr(getSubExpr());
}

CXXFunctionalCastExpr *
CXXFunctionalCastExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType Ty = QualType::ReadVal(D);
  QualType WrittenTy = QualType::ReadVal(D);
  SourceLocation TyBeginLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  Expr* SubExpr = D.ReadOwnedPtr<Expr>(C);
  return new CXXFunctionalCastExpr(Ty, WrittenTy, TyBeginLoc, SubExpr, RParenLoc);
}

void CXXNamedCastExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(getTypeAsWritten());
  S.Emit(Loc);
  S.EmitOwnedPtr(getSubExpr());
}

CXXNamedCastExpr *
CXXNamedCastExpr::CreateImpl(Deserializer& D, ASTContext& C, StmtClass SC) {
  QualType Ty = QualType::ReadVal(D);
  QualType WrittenTy = QualType::ReadVal(D);
  SourceLocation Loc = SourceLocation::ReadVal(D);
  Expr* SubExpr = D.ReadOwnedPtr<Expr>(C);
  switch (SC) {
  case CXXStaticCastExprClass: 
    return new CXXStaticCastExpr(Ty, SubExpr, WrittenTy, Loc);
  case CXXDynamicCastExprClass: 
    return new CXXDynamicCastExpr(Ty, SubExpr, WrittenTy, Loc);
  case CXXReinterpretCastExprClass: 
    return new CXXReinterpretCastExpr(Ty, SubExpr, WrittenTy, Loc);
  case CXXConstCastExprClass: 
    return new CXXConstCastExpr(Ty, SubExpr, WrittenTy, Loc);
  default:
    assert(false && "Unknown cast type!");
    return 0;
  }
}

void CXXTypeidExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.EmitBool(isTypeOperand());
  if (isTypeOperand()) {
    S.Emit(getTypeOperand());
  } else {
    S.EmitOwnedPtr(getExprOperand());
  }
  S.Emit(Range);
}

CXXTypeidExpr*
CXXTypeidExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType Ty = QualType::ReadVal(D);
  bool isTypeOp = D.ReadBool();
  void *Operand;
  if (isTypeOp) {
    Operand = QualType::ReadVal(D).getAsOpaquePtr();
  } else {
    Operand = D.ReadOwnedPtr<Expr>(C);
  }
  SourceRange Range = SourceRange::ReadVal(D);
  return new CXXTypeidExpr(isTypeOp, Operand, Ty, Range);
}

void CXXThisExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(Loc);
}

CXXThisExpr* CXXThisExpr::CreateImpl(llvm::Deserializer& D, ASTContext&) {
  QualType Ty = QualType::ReadVal(D);
  SourceLocation Loc = SourceLocation::ReadVal(D);
  return new CXXThisExpr(Loc, Ty);
}

void CXXTemporaryObjectExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.Emit(TyBeginLoc);
  S.Emit(RParenLoc);
  S.EmitPtr(cast<Decl>(Constructor));
  S.EmitInt(NumArgs);
  if (NumArgs > 0)
    S.BatchEmitOwnedPtrs(NumArgs, Args);
}

CXXTemporaryObjectExpr *
CXXTemporaryObjectExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType writtenTy = QualType::ReadVal(D);
  SourceLocation tyBeginLoc = SourceLocation::ReadVal(D);
  SourceLocation rParenLoc = SourceLocation::ReadVal(D);
  CXXConstructorDecl * Cons = cast_or_null<CXXConstructorDecl>(D.ReadPtr<Decl>());
  unsigned NumArgs = D.ReadInt();
  Stmt** Args = 0;
  if (NumArgs > 0) {
    Args = new Stmt*[NumArgs];
    D.BatchReadOwnedPtrs(NumArgs, Args, C);
  }

  CXXTemporaryObjectExpr * Result 
    = new CXXTemporaryObjectExpr(Cons, writtenTy, tyBeginLoc, 
                                 (Expr**)Args, NumArgs, rParenLoc);

  if (NumArgs > 0)
    delete [] Args;

  return Result;
}


void CXXZeroInitValueExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.Emit(TyBeginLoc);
  S.Emit(RParenLoc);
}

CXXZeroInitValueExpr *
CXXZeroInitValueExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType Ty = QualType::ReadVal(D);
  SourceLocation TyBeginLoc = SourceLocation::ReadVal(D);
  SourceLocation RParenLoc = SourceLocation::ReadVal(D);
  return new CXXZeroInitValueExpr(Ty, TyBeginLoc, RParenLoc);
}

void CXXNewExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.EmitBool(GlobalNew);
  S.EmitBool(ParenTypeId);
  S.EmitBool(Initializer);
  S.EmitBool(Array);
  S.EmitInt(NumPlacementArgs);
  S.EmitInt(NumConstructorArgs);
  S.BatchEmitOwnedPtrs(NumPlacementArgs + NumConstructorArgs, SubExprs);
  assert((OperatorNew == 0 || S.isRegistered(OperatorNew)) &&
         (OperatorDelete == 0 || S.isRegistered(OperatorDelete)) &&
         (Constructor == 0 || S.isRegistered(Constructor)) &&
         "CXXNewExpr cannot own declarations");
  S.EmitPtr(OperatorNew);
  S.EmitPtr(OperatorDelete);
  S.EmitPtr(Constructor);
  S.Emit(StartLoc);
  S.Emit(EndLoc);
}

CXXNewExpr *
CXXNewExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType T = QualType::ReadVal(D);
  bool GlobalNew = D.ReadBool();
  bool ParenTypeId = D.ReadBool();
  bool Initializer = D.ReadBool();
  bool Array = D.ReadBool();
  unsigned NumPlacementArgs = D.ReadInt();
  unsigned NumConstructorArgs = D.ReadInt();
  unsigned TotalExprs = Array + NumPlacementArgs + NumConstructorArgs;
  Stmt** SubExprs = new Stmt*[TotalExprs];
  D.BatchReadOwnedPtrs(TotalExprs, SubExprs, C);
  FunctionDecl *OperatorNew = D.ReadPtr<FunctionDecl>();
  FunctionDecl *OperatorDelete = D.ReadPtr<FunctionDecl>();
  CXXConstructorDecl *Constructor = D.ReadPtr<CXXConstructorDecl>();
  SourceLocation StartLoc = SourceLocation::ReadVal(D);
  SourceLocation EndLoc = SourceLocation::ReadVal(D);

  return new CXXNewExpr(T, GlobalNew, ParenTypeId, Initializer, Array,
                        NumPlacementArgs, NumConstructorArgs, SubExprs,
                        OperatorNew, OperatorDelete, Constructor, StartLoc,
                        EndLoc);
}

void CXXDeleteExpr::EmitImpl(Serializer& S) const {
  S.Emit(getType());
  S.EmitBool(GlobalDelete);
  S.EmitBool(ArrayForm);
  S.EmitPtr(OperatorDelete);
  S.EmitOwnedPtr(Argument);
  S.Emit(Loc);
}

CXXDeleteExpr *
CXXDeleteExpr::CreateImpl(Deserializer& D, ASTContext& C) {
  QualType Ty = QualType::ReadVal(D);
  bool GlobalDelete = D.ReadBool();
  bool ArrayForm = D.ReadBool();
  FunctionDecl *OperatorDelete = D.ReadPtr<FunctionDecl>();
  Stmt *Argument = D.ReadOwnedPtr<Stmt>(C);
  SourceLocation Loc = SourceLocation::ReadVal(D);
  return new CXXDeleteExpr(Ty, GlobalDelete, ArrayForm, OperatorDelete,
                           cast<Expr>(Argument), Loc);
}

void CXXDependentNameExpr::EmitImpl(llvm::Serializer& S) const {
  S.Emit(getType());
  S.EmitPtr(Name);
  S.Emit(Loc);
}

CXXDependentNameExpr *
CXXDependentNameExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  QualType Ty = QualType::ReadVal(D);
  IdentifierInfo *N = D.ReadPtr<IdentifierInfo>();
  SourceLocation L = SourceLocation::ReadVal(D);
  return new CXXDependentNameExpr(N, Ty, L);
}

void UnaryTypeTraitExpr::EmitImpl(llvm::Serializer& S) const {
  S.EmitInt(UTT);
  S.Emit(Loc);
  S.Emit(RParen);
  S.Emit(QueriedType);
  S.Emit(getType());
}

UnaryTypeTraitExpr *
UnaryTypeTraitExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  UnaryTypeTrait UTT = static_cast<UnaryTypeTrait>(D.ReadInt());
  SourceLocation Loc = SourceLocation::ReadVal(D);
  SourceLocation RParen = SourceLocation::ReadVal(D);
  QualType QueriedType = QualType::ReadVal(D);
  QualType Ty = QualType::ReadVal(D);
  return new UnaryTypeTraitExpr(Loc, UTT, QueriedType, RParen, Ty);
}

void CXXCatchStmt::EmitImpl(llvm::Serializer& S) const {
  S.Emit(CatchLoc);
  S.EmitOwnedPtr(ExceptionDecl);
  S.EmitOwnedPtr(HandlerBlock);
}

CXXCatchStmt *
CXXCatchStmt::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  SourceLocation CatchLoc = SourceLocation::ReadVal(D);
  Decl *ExDecl = D.ReadOwnedPtr<Decl>(C);
  Stmt *HandlerBlock = D.ReadOwnedPtr<Stmt>(C);
  return new CXXCatchStmt(CatchLoc, ExDecl, HandlerBlock);
}

void CXXTryStmt::EmitImpl(llvm::Serializer& S) const {
  S.Emit(TryLoc);
  S.EmitInt(Stmts.size());
  S.BatchEmitOwnedPtrs(Stmts.size(), &Stmts[0]);
}

CXXTryStmt *
CXXTryStmt::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  SourceLocation TryLoc = SourceLocation::ReadVal(D);
  unsigned size = D.ReadInt();
  llvm::SmallVector<Stmt*, 4> Stmts(size);
  D.BatchReadOwnedPtrs<Stmt>(size, &Stmts[0], C);

  return new CXXTryStmt(TryLoc, Stmts[0], &Stmts[1], size - 1);
}

void QualifiedDeclRefExpr::EmitImpl(llvm::Serializer& S) const {
  DeclRefExpr::EmitImpl(S);
  S.Emit(NestedNameLoc);
}

QualifiedDeclRefExpr* 
QualifiedDeclRefExpr::CreateImpl(llvm::Deserializer& D, ASTContext& C) {
  assert(false && "Cannot deserialize qualified decl references");
  return 0;
}
