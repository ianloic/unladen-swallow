//===--- ExprCXX.cpp - (C++) Expression AST Node Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Expr class declared in ExprCXX.h
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/IdentifierTable.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
using namespace clang;

void CXXConditionDeclExpr::Destroy(ASTContext& C) {
  // FIXME: Cannot destroy the decl here, because it is linked into the
  // DeclContext's chain.
  //getVarDecl()->Destroy(C);
  this->~CXXConditionDeclExpr();
  C.Deallocate(this);
}

//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// CXXTypeidExpr - has child iterators if the operand is an expression
Stmt::child_iterator CXXTypeidExpr::child_begin() {
  return isTypeOperand() ? child_iterator() : &Operand.Ex;
}
Stmt::child_iterator CXXTypeidExpr::child_end() {
  return isTypeOperand() ? child_iterator() : &Operand.Ex+1;
}

// CXXBoolLiteralExpr
Stmt::child_iterator CXXBoolLiteralExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator CXXBoolLiteralExpr::child_end() {
  return child_iterator();
}

// CXXNullPtrLiteralExpr
Stmt::child_iterator CXXNullPtrLiteralExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator CXXNullPtrLiteralExpr::child_end() {
  return child_iterator();
}

// CXXThisExpr
Stmt::child_iterator CXXThisExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator CXXThisExpr::child_end() { return child_iterator(); }

// CXXThrowExpr
Stmt::child_iterator CXXThrowExpr::child_begin() { return &Op; }
Stmt::child_iterator CXXThrowExpr::child_end() {
  // If Op is 0, we are processing throw; which has no children.
  return Op ? &Op+1 : &Op;
}

// CXXDefaultArgExpr
Stmt::child_iterator CXXDefaultArgExpr::child_begin() {
  return child_iterator();
}
Stmt::child_iterator CXXDefaultArgExpr::child_end() {
  return child_iterator();
}

// CXXZeroInitValueExpr
Stmt::child_iterator CXXZeroInitValueExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator CXXZeroInitValueExpr::child_end() {
  return child_iterator();
}

// CXXConditionDeclExpr
Stmt::child_iterator CXXConditionDeclExpr::child_begin() {
  return getVarDecl();
}
Stmt::child_iterator CXXConditionDeclExpr::child_end() {
  return child_iterator();
}

// CXXNewExpr
CXXNewExpr::CXXNewExpr(bool globalNew, FunctionDecl *operatorNew,
                       Expr **placementArgs, unsigned numPlaceArgs,
                       bool parenTypeId, Expr *arraySize,
                       CXXConstructorDecl *constructor, bool initializer,
                       Expr **constructorArgs, unsigned numConsArgs,
                       FunctionDecl *operatorDelete, QualType ty,
                       SourceLocation startLoc, SourceLocation endLoc)
  : Expr(CXXNewExprClass, ty, ty->isDependentType(), ty->isDependentType()),
    GlobalNew(globalNew), ParenTypeId(parenTypeId),
    Initializer(initializer), Array(arraySize), NumPlacementArgs(numPlaceArgs),
    NumConstructorArgs(numConsArgs), OperatorNew(operatorNew),
    OperatorDelete(operatorDelete), Constructor(constructor),
    StartLoc(startLoc), EndLoc(endLoc)
{
  unsigned TotalSize = Array + NumPlacementArgs + NumConstructorArgs;
  SubExprs = new Stmt*[TotalSize];
  unsigned i = 0;
  if (Array)
    SubExprs[i++] = arraySize;
  for (unsigned j = 0; j < NumPlacementArgs; ++j)
    SubExprs[i++] = placementArgs[j];
  for (unsigned j = 0; j < NumConstructorArgs; ++j)
    SubExprs[i++] = constructorArgs[j];
  assert(i == TotalSize);
}

Stmt::child_iterator CXXNewExpr::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator CXXNewExpr::child_end() {
  return &SubExprs[0] + Array + getNumPlacementArgs() + getNumConstructorArgs();
}

// CXXDeleteExpr
Stmt::child_iterator CXXDeleteExpr::child_begin() { return &Argument; }
Stmt::child_iterator CXXDeleteExpr::child_end() { return &Argument+1; }

// UnresolvedFunctionNameExpr
Stmt::child_iterator UnresolvedFunctionNameExpr::child_begin() { 
  return child_iterator(); 
}
Stmt::child_iterator UnresolvedFunctionNameExpr::child_end() {
  return child_iterator();
}

UnresolvedFunctionNameExpr* 
UnresolvedFunctionNameExpr::Clone(ASTContext &C) const {
  return new (C) UnresolvedFunctionNameExpr(Name, getType(), Loc);
}

// UnaryTypeTraitExpr
Stmt::child_iterator UnaryTypeTraitExpr::child_begin() {
  return child_iterator();
}
Stmt::child_iterator UnaryTypeTraitExpr::child_end() {
  return child_iterator();
}

// UnresolvedDeclRefExpr
StmtIterator UnresolvedDeclRefExpr::child_begin() {
  return child_iterator();
}

StmtIterator UnresolvedDeclRefExpr::child_end() {
  return child_iterator();
}

TemplateIdRefExpr::TemplateIdRefExpr(QualType T,
                                     NestedNameSpecifier *Qualifier, 
                                     SourceRange QualifierRange,
                                     TemplateName Template, 
                                     SourceLocation TemplateNameLoc,
                                     SourceLocation LAngleLoc, 
                                     const TemplateArgument *TemplateArgs,
                                     unsigned NumTemplateArgs,
                                     SourceLocation RAngleLoc)
  : Expr(TemplateIdRefExprClass, T,
         (Template.isDependent() || 
          TemplateSpecializationType::anyDependentTemplateArguments(
                                              TemplateArgs, NumTemplateArgs)),
         (Template.isDependent() ||
          TemplateSpecializationType::anyDependentTemplateArguments(
                                              TemplateArgs, NumTemplateArgs))),
    Qualifier(Qualifier), QualifierRange(QualifierRange), Template(Template),
    TemplateNameLoc(TemplateNameLoc), LAngleLoc(LAngleLoc),
    RAngleLoc(RAngleLoc), NumTemplateArgs(NumTemplateArgs)
    
{ 
  TemplateArgument *StoredTemplateArgs 
    = reinterpret_cast<TemplateArgument *> (this+1);
  for (unsigned I = 0; I != NumTemplateArgs; ++I)
    new (StoredTemplateArgs + I) TemplateArgument(TemplateArgs[I]);
}

TemplateIdRefExpr *
TemplateIdRefExpr::Create(ASTContext &Context, QualType T,
                          NestedNameSpecifier *Qualifier, 
                          SourceRange QualifierRange,
                          TemplateName Template, SourceLocation TemplateNameLoc,
                          SourceLocation LAngleLoc, 
                          const TemplateArgument *TemplateArgs,
                          unsigned NumTemplateArgs, SourceLocation RAngleLoc) {
  void *Mem = Context.Allocate(sizeof(TemplateIdRefExpr) +
                               sizeof(TemplateArgument) * NumTemplateArgs);
  return new (Mem) TemplateIdRefExpr(T, Qualifier, QualifierRange, Template,
                                     TemplateNameLoc, LAngleLoc, TemplateArgs,
                                     NumTemplateArgs, RAngleLoc);
}

void TemplateIdRefExpr::Destroy(ASTContext &Context) {
  const TemplateArgument *TemplateArgs = getTemplateArgs();
  for (unsigned I = 0; I != NumTemplateArgs; ++I)
    if (Expr *E = TemplateArgs[I].getAsExpr())
      E->Destroy(Context);
}

Stmt::child_iterator TemplateIdRefExpr::child_begin() {
  // FIXME: Walk the expressions in the template arguments (?)
  return Stmt::child_iterator();
}

Stmt::child_iterator TemplateIdRefExpr::child_end() {
  // FIXME: Walk the expressions in the template arguments (?)
  return Stmt::child_iterator();
}

bool UnaryTypeTraitExpr::EvaluateTrait() const {
  switch(UTT) {
  default: assert(false && "Unknown type trait or not implemented");
  case UTT_IsPOD: return QueriedType->isPODType();
  case UTT_IsClass: // Fallthrough
  case UTT_IsUnion:
    if (const RecordType *Record = QueriedType->getAsRecordType()) {
      bool Union = Record->getDecl()->isUnion();
      return UTT == UTT_IsUnion ? Union : !Union;
    }
    return false;
  case UTT_IsEnum: return QueriedType->isEnumeralType();
  case UTT_IsPolymorphic:
    if (const RecordType *Record = QueriedType->getAsRecordType()) {
      // Type traits are only parsed in C++, so we've got CXXRecords.
      return cast<CXXRecordDecl>(Record->getDecl())->isPolymorphic();
    }
    return false;
  case UTT_IsAbstract:
    if (const RecordType *RT = QueriedType->getAsRecordType())
      return cast<CXXRecordDecl>(RT->getDecl())->isAbstract();
    return false;
  case UTT_HasTrivialConstructor:
    if (const RecordType *RT = QueriedType->getAsRecordType())
      return cast<CXXRecordDecl>(RT->getDecl())->hasTrivialConstructor();
    return false;
  case UTT_HasTrivialDestructor:
    if (const RecordType *RT = QueriedType->getAsRecordType())
      return cast<CXXRecordDecl>(RT->getDecl())->hasTrivialDestructor();
    return false;
  }
}

SourceRange CXXOperatorCallExpr::getSourceRange() const {
  OverloadedOperatorKind Kind = getOperator();
  if (Kind == OO_PlusPlus || Kind == OO_MinusMinus) {
    if (getNumArgs() == 1)
      // Prefix operator
      return SourceRange(getOperatorLoc(), 
                         getArg(0)->getSourceRange().getEnd());
    else
      // Postfix operator
      return SourceRange(getArg(0)->getSourceRange().getEnd(),
                         getOperatorLoc());
  } else if (Kind == OO_Call) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (Kind == OO_Subscript) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (getNumArgs() == 1) {
    return SourceRange(getOperatorLoc(), getArg(0)->getSourceRange().getEnd());
  } else if (getNumArgs() == 2) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(),
                       getArg(1)->getSourceRange().getEnd());
  } else {
    return SourceRange();
  }
}

Expr *CXXMemberCallExpr::getImplicitObjectArgument() {
  if (MemberExpr *MemExpr = dyn_cast<MemberExpr>(getCallee()->IgnoreParens()))
    return MemExpr->getBase();

  // FIXME: Will eventually need to cope with member pointers.
  return 0;
}

//===----------------------------------------------------------------------===//
//  Named casts
//===----------------------------------------------------------------------===//

/// getCastName - Get the name of the C++ cast being used, e.g.,
/// "static_cast", "dynamic_cast", "reinterpret_cast", or
/// "const_cast". The returned pointer must not be freed.
const char *CXXNamedCastExpr::getCastName() const {
  switch (getStmtClass()) {
  case CXXStaticCastExprClass:      return "static_cast";
  case CXXDynamicCastExprClass:     return "dynamic_cast";
  case CXXReinterpretCastExprClass: return "reinterpret_cast";
  case CXXConstCastExprClass:       return "const_cast";
  default:                          return "<invalid cast>";
  }
}

CXXTemporary *CXXTemporary::Create(ASTContext &C, 
                                   const CXXDestructorDecl *Destructor) {
  return new (C) CXXTemporary(Destructor);
}

void CXXTemporary::Destroy(ASTContext &C) {
  this->~CXXTemporary();
  C.Deallocate(this);
}

CXXBindTemporaryExpr *CXXBindTemporaryExpr::Create(ASTContext &C, 
                                                   CXXTemporary *Temp,
                                                   Expr* SubExpr) {
  assert(SubExpr->getType()->isRecordType() && 
         "Expression bound to a temporary must have record type!");

  return new (C) CXXBindTemporaryExpr(Temp, SubExpr);
}

void CXXBindTemporaryExpr::Destroy(ASTContext &C) {
  Temp->Destroy(C);
  this->~CXXBindTemporaryExpr();
  C.Deallocate(this);
}

CXXTemporaryObjectExpr::CXXTemporaryObjectExpr(ASTContext &C,
                                               CXXConstructorDecl *Cons,
                                               QualType writtenTy,
                                               SourceLocation tyBeginLoc, 
                                               Expr **Args,
                                               unsigned NumArgs, 
                                               SourceLocation rParenLoc)
  : CXXConstructExpr(C, CXXTemporaryObjectExprClass, writtenTy, Cons, 
                     false, Args, NumArgs), 
  TyBeginLoc(tyBeginLoc), RParenLoc(rParenLoc) {
}

CXXConstructExpr *CXXConstructExpr::Create(ASTContext &C, QualType T, 
                                           CXXConstructorDecl *D, bool Elidable,
                                           Expr **Args, unsigned NumArgs) {
  return new (C) CXXConstructExpr(C, CXXConstructExprClass, T, D, Elidable, 
                                  Args, NumArgs);
}

CXXConstructExpr::CXXConstructExpr(ASTContext &C, StmtClass SC, QualType T, 
                                   CXXConstructorDecl *D, bool elidable,
                                   Expr **args, unsigned numargs) 
: Expr(SC, T,
       T->isDependentType(),
       (T->isDependentType() ||
        CallExpr::hasAnyValueDependentArguments(args, numargs))),
  Constructor(D), Elidable(elidable), Args(0), NumArgs(numargs) {
    if (NumArgs > 0) {
      Args = new (C) Stmt*[NumArgs];
      for (unsigned i = 0; i < NumArgs; ++i)
        Args[i] = args[i];
    }
}

void CXXConstructExpr::Destroy(ASTContext &C) {
  DestroyChildren(C);
  if (Args)
    C.Deallocate(Args);
  this->~CXXConstructExpr();
  C.Deallocate(this);
}

CXXExprWithTemporaries::CXXExprWithTemporaries(Expr *subexpr, 
                                               CXXTemporary **temps, 
                                               unsigned numtemps,
                                               bool shoulddestroytemps)
: Expr(CXXExprWithTemporariesClass, subexpr->getType(),
       subexpr->isTypeDependent(), subexpr->isValueDependent()), 
  SubExpr(subexpr), Temps(0), NumTemps(numtemps), 
  ShouldDestroyTemps(shoulddestroytemps) {
  if (NumTemps > 0) {
    Temps = new CXXTemporary*[NumTemps];
    for (unsigned i = 0; i < NumTemps; ++i)
      Temps[i] = temps[i];
  }
}

CXXExprWithTemporaries *CXXExprWithTemporaries::Create(ASTContext &C, 
                                                       Expr *SubExpr,
                                                       CXXTemporary **Temps, 
                                                       unsigned NumTemps,
                                                       bool ShouldDestroyTemps){
  return new (C) CXXExprWithTemporaries(SubExpr, Temps, NumTemps, 
                                        ShouldDestroyTemps);
}

void CXXExprWithTemporaries::Destroy(ASTContext &C) {
  DestroyChildren(C);
  this->~CXXExprWithTemporaries();
  C.Deallocate(this);
}

CXXExprWithTemporaries::~CXXExprWithTemporaries() {
  delete[] Temps;
}

// CXXBindTemporaryExpr
Stmt::child_iterator CXXBindTemporaryExpr::child_begin() {
  return &SubExpr;
}

Stmt::child_iterator CXXBindTemporaryExpr::child_end() { 
  return &SubExpr + 1;
}

// CXXConstructExpr
Stmt::child_iterator CXXConstructExpr::child_begin() {
  return &Args[0];
}
Stmt::child_iterator CXXConstructExpr::child_end() {
  return &Args[0]+NumArgs;
}

// CXXExprWithTemporaries
Stmt::child_iterator CXXExprWithTemporaries::child_begin() {
  return &SubExpr;
}

Stmt::child_iterator CXXExprWithTemporaries::child_end() { 
  return &SubExpr + 1;
}

CXXUnresolvedConstructExpr::CXXUnresolvedConstructExpr(
                                                 SourceLocation TyBeginLoc,
                                                 QualType T,
                                                 SourceLocation LParenLoc,
                                                 Expr **Args,
                                                 unsigned NumArgs,
                                                 SourceLocation RParenLoc)
  : Expr(CXXUnresolvedConstructExprClass, T.getNonReferenceType(),
         T->isDependentType(), true),
    TyBeginLoc(TyBeginLoc),
    Type(T),
    LParenLoc(LParenLoc),
    RParenLoc(RParenLoc),
    NumArgs(NumArgs) {
  Stmt **StoredArgs = reinterpret_cast<Stmt **>(this + 1);
  memcpy(StoredArgs, Args, sizeof(Expr *) * NumArgs);
}

CXXUnresolvedConstructExpr *
CXXUnresolvedConstructExpr::Create(ASTContext &C, 
                                   SourceLocation TyBegin,
                                   QualType T,
                                   SourceLocation LParenLoc,
                                   Expr **Args,
                                   unsigned NumArgs,
                                   SourceLocation RParenLoc) {
  void *Mem = C.Allocate(sizeof(CXXUnresolvedConstructExpr) +
                         sizeof(Expr *) * NumArgs);
  return new (Mem) CXXUnresolvedConstructExpr(TyBegin, T, LParenLoc,
                                              Args, NumArgs, RParenLoc);
}

Stmt::child_iterator CXXUnresolvedConstructExpr::child_begin() {
  return child_iterator(reinterpret_cast<Stmt **>(this + 1));
}

Stmt::child_iterator CXXUnresolvedConstructExpr::child_end() {
  return child_iterator(reinterpret_cast<Stmt **>(this + 1) + NumArgs);
}

Stmt::child_iterator CXXUnresolvedMemberExpr::child_begin() {
  return child_iterator(&Base);
}

Stmt::child_iterator CXXUnresolvedMemberExpr::child_end() {
  return child_iterator(&Base + 1);
}

//===----------------------------------------------------------------------===//
//  Cloners
//===----------------------------------------------------------------------===//

CXXBoolLiteralExpr* CXXBoolLiteralExpr::Clone(ASTContext &C) const {
  return new (C) CXXBoolLiteralExpr(Value, getType(), Loc);
}

CXXNullPtrLiteralExpr* CXXNullPtrLiteralExpr::Clone(ASTContext &C) const {
  return new (C) CXXNullPtrLiteralExpr(getType(), Loc);
}

CXXZeroInitValueExpr* CXXZeroInitValueExpr::Clone(ASTContext &C) const {
  return new (C) CXXZeroInitValueExpr(getType(), TyBeginLoc, RParenLoc);
}
