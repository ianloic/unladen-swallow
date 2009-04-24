//==- DeadStores.cpp - Check for stores to dead variables --------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a DeadStores, a flow-sensitive checker that looks for
//  stores to variables that are no longer live.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/Visitors/CFGRecStmtVisitor.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMap.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

namespace {

class VISIBILITY_HIDDEN DeadStoreObs : public LiveVariables::ObserverTy {
  ASTContext &Ctx;
  BugReporter& BR;
  ParentMap& Parents;
  
  enum DeadStoreKind { Standard, Enclosing, DeadIncrement, DeadInit };
    
public:
  DeadStoreObs(ASTContext &ctx, BugReporter& br, ParentMap& parents)
    : Ctx(ctx), BR(br), Parents(parents) {}
  
  virtual ~DeadStoreObs() {}
  
  bool isConsumedExpr(Expr* E) const;
  
  
  void Report(VarDecl* V, DeadStoreKind dsk, SourceLocation L, SourceRange R) {

    std::string name = V->getNameAsString();
    
    const char* BugType = 0;
    std::string msg;
    
    switch (dsk) {
      default:
        assert(false && "Impossible dead store type.");
        
      case DeadInit:
        BugType = "dead initialization";
        msg = "Value stored to '" + name +
          "' during its initialization is never read";
        break;
        
      case DeadIncrement:
        BugType = "dead increment";
      case Standard:
        if (!BugType) BugType = "dead assignment";
        msg = "Value stored to '" + name + "' is never read";
        break;
        
      case Enclosing:
        BugType = "dead nested assignment";
        msg = "Although the value stored to '" + name +
          "' is used in the enclosing expression, the value is never actually"
          " read from '" + name + "'";
        break;
    }
      
    BR.EmitBasicReport(BugType, "Dead Store", msg.c_str(), L, R);      
  }
  
  void CheckVarDecl(VarDecl* VD, Expr* Ex, Expr* Val,
                    DeadStoreKind dsk,
                    const LiveVariables::AnalysisDataTy& AD,
                    const LiveVariables::ValTy& Live) {

    if (VD->hasLocalStorage() && !Live(VD, AD) && !VD->getAttr<UnusedAttr>())
      Report(VD, dsk, Ex->getSourceRange().getBegin(),
             Val->getSourceRange());      
  }
  
  void CheckDeclRef(DeclRefExpr* DR, Expr* Val, DeadStoreKind dsk,
                    const LiveVariables::AnalysisDataTy& AD,
                    const LiveVariables::ValTy& Live) {
    
    if (VarDecl* VD = dyn_cast<VarDecl>(DR->getDecl()))
      CheckVarDecl(VD, DR, Val, dsk, AD, Live);
  }
  
  bool isIncrement(VarDecl* VD, BinaryOperator* B) {
    if (B->isCompoundAssignmentOp())
      return true;
    
    Expr* RHS = B->getRHS()->IgnoreParenCasts();
    BinaryOperator* BRHS = dyn_cast<BinaryOperator>(RHS);
    
    if (!BRHS)
      return false;
    
    DeclRefExpr *DR;
    
    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getLHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;
    
    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getRHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;
    
    return false;
  }
  
  virtual void ObserveStmt(Stmt* S,
                           const LiveVariables::AnalysisDataTy& AD,
                           const LiveVariables::ValTy& Live) {
    
    // Skip statements in macros.
    if (S->getLocStart().isMacroID())
      return;
    
    if (BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {    
      if (!B->isAssignmentOp()) return; // Skip non-assignments.
      
      if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(B->getLHS()))
        if (VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
          Expr* RHS = B->getRHS()->IgnoreParenCasts();
          
          // Special case: check for assigning null to a pointer.
          //  This is a common form of defensive programming.          
          if (VD->getType()->isPointerType()) {
            if (IntegerLiteral* L = dyn_cast<IntegerLiteral>(RHS))
              // FIXME: Probably should have an Expr::isNullPointerConstant.              
              if (L->getValue() == 0)
                return;
          }
          // Special case: self-assignments.  These are often used to shut up
          //  "unused variable" compiler warnings.
          if (DeclRefExpr* RhsDR = dyn_cast<DeclRefExpr>(RHS))
            if (VD == dyn_cast<VarDecl>(RhsDR->getDecl()))
              return;
            
          // Otherwise, issue a warning.
          DeadStoreKind dsk = isConsumedExpr(B)
                              ? Enclosing 
                              : (isIncrement(VD,B) ? DeadIncrement : Standard);
          
          CheckVarDecl(VD, DR, B->getRHS(), dsk, AD, Live);
        }              
    }
    else if (UnaryOperator* U = dyn_cast<UnaryOperator>(S)) {
      if (!U->isIncrementOp())
        return;
      
      // Handle: ++x within a subexpression.  The solution is not warn
      //  about preincrements to dead variables when the preincrement occurs
      //  as a subexpression.  This can lead to false negatives, e.g. "(++x);"
      //  A generalized dead code checker should find such issues.
      if (U->isPrefix() && isConsumedExpr(U))
        return;

      Expr *Ex = U->getSubExpr()->IgnoreParenCasts();
      
      if (DeclRefExpr* DR = dyn_cast<DeclRefExpr>(Ex))
        CheckDeclRef(DR, U, DeadIncrement, AD, Live);
    }    
    else if (DeclStmt* DS = dyn_cast<DeclStmt>(S))
      // Iterate through the decls.  Warn if any initializers are complex
      // expressions that are not live (never used).
      for (DeclStmt::decl_iterator DI=DS->decl_begin(), DE=DS->decl_end();
           DI != DE; ++DI) {
        
        VarDecl* V = dyn_cast<VarDecl>(*DI);

        if (!V)
          continue;
        
        if (V->hasLocalStorage())
          if (Expr* E = V->getInit()) {
            // A dead initialization is a variable that is dead after it
            // is initialized.  We don't flag warnings for those variables
            // marked 'unused'.
            if (!Live(V, AD) && V->getAttr<UnusedAttr>() == 0) {
              // Special case: check for initializations with constants.
              //
              //  e.g. : int x = 0;
              //
              // If x is EVER assigned a new value later, don't issue
              // a warning.  This is because such initialization can be
              // due to defensive programming.
              if (!E->isConstantInitializer(Ctx))
                Report(V, DeadInit, V->getLocation(), E->getSourceRange());
            }
          }
      }
  }
};
  
} // end anonymous namespace

bool DeadStoreObs::isConsumedExpr(Expr* E) const {
  Stmt *P = Parents.getParent(E);
  Stmt *DirectChild = E;
  
  // Ignore parents that are parentheses or casts.
  while (P && (isa<ParenExpr>(E) || isa<CastExpr>(E))) {
    DirectChild = P;
    P = Parents.getParent(P);
  }
  
  if (!P)
    return false;
  
  switch (P->getStmtClass()) {
    default:
      return isa<Expr>(P);
    case Stmt::BinaryOperatorClass: {
      BinaryOperator *BE = cast<BinaryOperator>(P);
      return BE->getOpcode()==BinaryOperator::Comma && DirectChild==BE->getLHS();
    }
    case Stmt::ForStmtClass:
      return DirectChild == cast<ForStmt>(P)->getCond();
    case Stmt::WhileStmtClass:
      return DirectChild == cast<WhileStmt>(P)->getCond();      
    case Stmt::DoStmtClass:
      return DirectChild == cast<DoStmt>(P)->getCond();
    case Stmt::IfStmtClass:
      return DirectChild == cast<IfStmt>(P)->getCond();
    case Stmt::IndirectGotoStmtClass:
      return DirectChild == cast<IndirectGotoStmt>(P)->getTarget();
    case Stmt::SwitchStmtClass:
      return DirectChild == cast<SwitchStmt>(P)->getCond();
    case Stmt::ReturnStmtClass:
      return true;
  }
}

//===----------------------------------------------------------------------===//
// Driver function to invoke the Dead-Stores checker on a CFG.
//===----------------------------------------------------------------------===//

void clang::CheckDeadStores(LiveVariables& L, BugReporter& BR) {  
  DeadStoreObs A(BR.getContext(), BR, BR.getParentMap());
  L.runOnAllBlocks(*BR.getCFG(), &A);
}
