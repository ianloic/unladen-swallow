//===--- Stmt.cpp - Statement AST Node Implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Stmt class and statement subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Stmt.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Type.h"
using namespace clang;

static struct StmtClassNameTable {
  const char *Name;
  unsigned Counter;
  unsigned Size;
} StmtClassInfo[Stmt::lastExprConstant+1];

static StmtClassNameTable &getStmtInfoTableEntry(Stmt::StmtClass E) {
  static bool Initialized = false;
  if (Initialized)
    return StmtClassInfo[E];

  // Intialize the table on the first use.
  Initialized = true;
#define STMT(CLASS, PARENT) \
  StmtClassInfo[(unsigned)Stmt::CLASS##Class].Name = #CLASS;    \
  StmtClassInfo[(unsigned)Stmt::CLASS##Class].Size = sizeof(CLASS);
#include "clang/AST/StmtNodes.def"

  return StmtClassInfo[E];
}

const char *Stmt::getStmtClassName() const {
  return getStmtInfoTableEntry(sClass).Name;
}

void Stmt::DestroyChildren(ASTContext& C) {
  for (child_iterator I = child_begin(), E = child_end(); I !=E; ) {
    if (Stmt* Child = *I++) Child->Destroy(C);
  }
}

void Stmt::Destroy(ASTContext& C) {
  DestroyChildren(C);
  // FIXME: Eventually all Stmts should be allocated with the allocator
  //  in ASTContext, just like with Decls.
  // this->~Stmt();
  delete this;
}

void DeclStmt::Destroy(ASTContext& C) {
  DG.Destroy(C);
  delete this;
}

void Stmt::PrintStats() {
  // Ensure the table is primed.
  getStmtInfoTableEntry(Stmt::NullStmtClass);

  unsigned sum = 0;
  fprintf(stderr, "*** Stmt/Expr Stats:\n");
  for (int i = 0; i != Stmt::lastExprConstant+1; i++) {
    if (StmtClassInfo[i].Name == 0) continue;
    sum += StmtClassInfo[i].Counter;
  }
  fprintf(stderr, "  %d stmts/exprs total.\n", sum);
  sum = 0;
  for (int i = 0; i != Stmt::lastExprConstant+1; i++) {
    if (StmtClassInfo[i].Name == 0) continue;
    fprintf(stderr, "    %d %s, %d each (%d bytes)\n",
            StmtClassInfo[i].Counter, StmtClassInfo[i].Name,
            StmtClassInfo[i].Size,
            StmtClassInfo[i].Counter*StmtClassInfo[i].Size);
    sum += StmtClassInfo[i].Counter*StmtClassInfo[i].Size;
  }
  fprintf(stderr, "Total bytes = %d\n", sum);
}

void Stmt::addStmtClass(StmtClass s) {
  ++getStmtInfoTableEntry(s).Counter;
}

static bool StatSwitch = false;

bool Stmt::CollectingStats(bool enable) {
  if (enable) StatSwitch = true;
  return StatSwitch;
}


const char *LabelStmt::getName() const {
  return getID()->getName();
}

// This is defined here to avoid polluting Stmt.h with importing Expr.h
SourceRange ReturnStmt::getSourceRange() const {
  if (RetExpr)
    return SourceRange(RetLoc, RetExpr->getLocEnd());
  else
    return SourceRange(RetLoc);
}

bool Stmt::hasImplicitControlFlow() const {
  switch (sClass) {
    default:
      return false;

    case CallExprClass:
    case ConditionalOperatorClass:
    case ChooseExprClass:
    case StmtExprClass:
    case DeclStmtClass:
      return true;

    case Stmt::BinaryOperatorClass: {
      const BinaryOperator* B = cast<BinaryOperator>(this);
      if (B->isLogicalOp() || B->getOpcode() == BinaryOperator::Comma)
        return true;
      else
        return false;
    }
  }
}

const Expr* AsmStmt::getOutputExpr(unsigned i) const {
  return cast<Expr>(Exprs[i]);
}
Expr* AsmStmt::getOutputExpr(unsigned i) {
  return cast<Expr>(Exprs[i]);
}
Expr* AsmStmt::getInputExpr(unsigned i) {
  return cast<Expr>(Exprs[i + NumOutputs]);
}
const Expr* AsmStmt::getInputExpr(unsigned i) const {
  return cast<Expr>(Exprs[i + NumOutputs]);
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

AsmStmt::AsmStmt(SourceLocation asmloc, bool issimple, bool isvolatile,
                 unsigned numoutputs, unsigned numinputs,
                 std::string *names, StringLiteral **constraints,
                 Expr **exprs, StringLiteral *asmstr, unsigned numclobbers,
                 StringLiteral **clobbers, SourceLocation rparenloc)
  : Stmt(AsmStmtClass), AsmLoc(asmloc), RParenLoc(rparenloc), AsmStr(asmstr)
  , IsSimple(issimple), IsVolatile(isvolatile)
  , NumOutputs(numoutputs), NumInputs(numinputs) {
  for (unsigned i = 0, e = numinputs + numoutputs; i != e; i++) {
    Names.push_back(names[i]);
    Exprs.push_back(exprs[i]);
    Constraints.push_back(constraints[i]);
  }

  for (unsigned i = 0; i != numclobbers; i++)
    Clobbers.push_back(clobbers[i]);
}

ObjCForCollectionStmt::ObjCForCollectionStmt(Stmt *Elem, Expr *Collect,
                                             Stmt *Body,  SourceLocation FCL,
                                             SourceLocation RPL)
: Stmt(ObjCForCollectionStmtClass) {
  SubExprs[ELEM] = Elem;
  SubExprs[COLLECTION] = reinterpret_cast<Stmt*>(Collect);
  SubExprs[BODY] = Body;
  ForLoc = FCL;
  RParenLoc = RPL;
}


ObjCAtCatchStmt::ObjCAtCatchStmt(SourceLocation atCatchLoc,
                                 SourceLocation rparenloc,
                                 Stmt *catchVarStmtDecl, Stmt *atCatchStmt,
                                 Stmt *atCatchList)
: Stmt(ObjCAtCatchStmtClass) {
  SubExprs[SELECTOR] = catchVarStmtDecl;
  SubExprs[BODY] = atCatchStmt;
  SubExprs[NEXT_CATCH] = NULL;
  if (atCatchList) {
    ObjCAtCatchStmt *AtCatchList = static_cast<ObjCAtCatchStmt*>(atCatchList);

    while (ObjCAtCatchStmt* NextCatch = AtCatchList->getNextCatchStmt())
      AtCatchList = NextCatch;

    AtCatchList->SubExprs[NEXT_CATCH] = this;
  }
  AtCatchLoc = atCatchLoc;
  RParenLoc = rparenloc;
}


//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// DeclStmt
Stmt::child_iterator DeclStmt::child_begin() {
  return StmtIterator(DG.begin(), DG.end());
}

Stmt::child_iterator DeclStmt::child_end() {
  return StmtIterator(DG.end(), DG.end());
}

// NullStmt
Stmt::child_iterator NullStmt::child_begin() { return child_iterator(); }
Stmt::child_iterator NullStmt::child_end() { return child_iterator(); }

// CompoundStmt
Stmt::child_iterator CompoundStmt::child_begin() { return &Body[0]; }
Stmt::child_iterator CompoundStmt::child_end() { return &Body[0]+Body.size(); }

// CaseStmt
Stmt::child_iterator CaseStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator CaseStmt::child_end() { return &SubExprs[END_EXPR]; }

// DefaultStmt
Stmt::child_iterator DefaultStmt::child_begin() { return &SubStmt; }
Stmt::child_iterator DefaultStmt::child_end() { return &SubStmt+1; }

// LabelStmt
Stmt::child_iterator LabelStmt::child_begin() { return &SubStmt; }
Stmt::child_iterator LabelStmt::child_end() { return &SubStmt+1; }

// IfStmt
Stmt::child_iterator IfStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator IfStmt::child_end() { return &SubExprs[0]+END_EXPR; }

// SwitchStmt
Stmt::child_iterator SwitchStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator SwitchStmt::child_end() { return &SubExprs[0]+END_EXPR; }

// WhileStmt
Stmt::child_iterator WhileStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator WhileStmt::child_end() { return &SubExprs[0]+END_EXPR; }

// DoStmt
Stmt::child_iterator DoStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator DoStmt::child_end() { return &SubExprs[0]+END_EXPR; }

// ForStmt
Stmt::child_iterator ForStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator ForStmt::child_end() { return &SubExprs[0]+END_EXPR; }

// ObjCForCollectionStmt
Stmt::child_iterator ObjCForCollectionStmt::child_begin() {
  return &SubExprs[0];
}
Stmt::child_iterator ObjCForCollectionStmt::child_end() {
  return &SubExprs[0]+END_EXPR;
}

// GotoStmt
Stmt::child_iterator GotoStmt::child_begin() { return child_iterator(); }
Stmt::child_iterator GotoStmt::child_end() { return child_iterator(); }

// IndirectGotoStmt
Expr* IndirectGotoStmt::getTarget() { return cast<Expr>(Target); }
const Expr* IndirectGotoStmt::getTarget() const { return cast<Expr>(Target); }

Stmt::child_iterator IndirectGotoStmt::child_begin() { return &Target; }
Stmt::child_iterator IndirectGotoStmt::child_end() { return &Target+1; }

// ContinueStmt
Stmt::child_iterator ContinueStmt::child_begin() { return child_iterator(); }
Stmt::child_iterator ContinueStmt::child_end() { return child_iterator(); }

// BreakStmt
Stmt::child_iterator BreakStmt::child_begin() { return child_iterator(); }
Stmt::child_iterator BreakStmt::child_end() { return child_iterator(); }

// ReturnStmt
const Expr* ReturnStmt::getRetValue() const {
  return cast_or_null<Expr>(RetExpr);
}
Expr* ReturnStmt::getRetValue() {
  return cast_or_null<Expr>(RetExpr);
}

Stmt::child_iterator ReturnStmt::child_begin() {
  return &RetExpr;
}
Stmt::child_iterator ReturnStmt::child_end() {
  return RetExpr ? &RetExpr+1 : &RetExpr;
}

// AsmStmt
Stmt::child_iterator AsmStmt::child_begin() { 
  return Exprs.empty() ? 0 : &Exprs[0];
}
Stmt::child_iterator AsmStmt::child_end() {
  return Exprs.empty() ? 0 : &Exprs[0] + Exprs.size();
}

// ObjCAtCatchStmt
Stmt::child_iterator ObjCAtCatchStmt::child_begin() { return &SubExprs[0]; }
Stmt::child_iterator ObjCAtCatchStmt::child_end() {
  return &SubExprs[0]+END_EXPR;
}

// ObjCAtFinallyStmt
Stmt::child_iterator ObjCAtFinallyStmt::child_begin() { return &AtFinallyStmt; }
Stmt::child_iterator ObjCAtFinallyStmt::child_end() { return &AtFinallyStmt+1; }

// ObjCAtTryStmt
Stmt::child_iterator ObjCAtTryStmt::child_begin() { return &SubStmts[0]; }
Stmt::child_iterator ObjCAtTryStmt::child_end()   {
  return &SubStmts[0]+END_EXPR;
}

// ObjCAtThrowStmt
Stmt::child_iterator ObjCAtThrowStmt::child_begin() {
  return &Throw;
}

Stmt::child_iterator ObjCAtThrowStmt::child_end() {
  return &Throw+1;
}

// ObjCAtSynchronizedStmt
Stmt::child_iterator ObjCAtSynchronizedStmt::child_begin() {
  return &SubStmts[0];
}

Stmt::child_iterator ObjCAtSynchronizedStmt::child_end() {
  return &SubStmts[0]+END_EXPR;
}

// CXXCatchStmt
Stmt::child_iterator CXXCatchStmt::child_begin() {
  return &HandlerBlock;
}

Stmt::child_iterator CXXCatchStmt::child_end() {
  return &HandlerBlock + 1;
}

QualType CXXCatchStmt::getCaughtType() {
  if (ExceptionDecl)
    return llvm::cast<VarDecl>(ExceptionDecl)->getType();
  return QualType();
}

void CXXCatchStmt::Destroy(ASTContext& C) {
  if (ExceptionDecl)
    ExceptionDecl->Destroy(C);
  Stmt::Destroy(C);
}

// CXXTryStmt
Stmt::child_iterator CXXTryStmt::child_begin() { return &Stmts[0]; }
Stmt::child_iterator CXXTryStmt::child_end() { return &Stmts[0]+Stmts.size(); }

CXXTryStmt::CXXTryStmt(SourceLocation tryLoc, Stmt *tryBlock,
                       Stmt **handlers, unsigned numHandlers)
  : Stmt(CXXTryStmtClass), TryLoc(tryLoc) {
  Stmts.push_back(tryBlock);
  Stmts.insert(Stmts.end(), handlers, handlers + numHandlers);
}
