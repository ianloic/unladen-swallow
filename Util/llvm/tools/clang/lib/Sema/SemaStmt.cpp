//===--- SemaStmt.cpp - Semantic Analysis for Statements ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for statements.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
using namespace clang;

Sema::OwningStmtResult Sema::ActOnExprStmt(ExprArg expr) {
  Expr *E = static_cast<Expr*>(expr.release());
  assert(E && "ActOnExprStmt(): missing expression");

  // C99 6.8.3p2: The expression in an expression statement is evaluated as a
  // void expression for its side effects.  Conversion to void allows any
  // operand, even incomplete types.

  // Same thing in for stmt first clause (when expr) and third clause.
  return Owned(static_cast<Stmt*>(E));
}


Sema::OwningStmtResult Sema::ActOnNullStmt(SourceLocation SemiLoc) {
  return Owned(new NullStmt(SemiLoc));
}

Sema::OwningStmtResult Sema::ActOnDeclStmt(DeclTy *decl,
                                           SourceLocation StartLoc,
                                           SourceLocation EndLoc) {
  if (decl == 0)
    return StmtError();

  Decl *D = static_cast<Decl *>(decl);

  // This is a temporary hack until we are always passing around
  // DeclGroupRefs.
  llvm::SmallVector<Decl*, 10> decls;
  while (D) { 
    Decl* d = D;
    D = D->getNextDeclarator();
    d->setNextDeclarator(0);
    decls.push_back(d);
  }

  assert (!decls.empty());

  if (decls.size() == 1) {
    DeclGroupOwningRef DG(*decls.begin());                      
    return Owned(new DeclStmt(DG, StartLoc, EndLoc));
  }
  else {
    DeclGroupOwningRef DG(DeclGroup::Create(Context, decls.size(), &decls[0]));
    return Owned(new DeclStmt(DG, StartLoc, EndLoc));
  }
}

Action::OwningStmtResult
Sema::ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                        MultiStmtArg elts, bool isStmtExpr) {
  unsigned NumElts = elts.size();
  Stmt **Elts = reinterpret_cast<Stmt**>(elts.release());
  // If we're in C89 mode, check that we don't have any decls after stmts.  If
  // so, emit an extension diagnostic.
  if (!getLangOptions().C99 && !getLangOptions().CPlusPlus) {
    // Note that __extension__ can be around a decl.
    unsigned i = 0;
    // Skip over all declarations.
    for (; i != NumElts && isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;

    // We found the end of the list or a statement.  Scan for another declstmt.
    for (; i != NumElts && !isa<DeclStmt>(Elts[i]); ++i)
      /*empty*/;
    
    if (i != NumElts) {
      Decl *D = *cast<DeclStmt>(Elts[i])->decl_begin();
      Diag(D->getLocation(), diag::ext_mixed_decls_code);
    }
  }
  // Warn about unused expressions in statements.
  for (unsigned i = 0; i != NumElts; ++i) {
    Expr *E = dyn_cast<Expr>(Elts[i]);
    if (!E) continue;
    
    // Warn about expressions with unused results.
    if (E->hasLocalSideEffect() || E->getType()->isVoidType())
      continue;
    
    // The last expr in a stmt expr really is used.
    if (isStmtExpr && i == NumElts-1)
      continue;
    
    /// DiagnoseDeadExpr - This expression is side-effect free and evaluated in
    /// a context where the result is unused.  Emit a diagnostic to warn about
    /// this.
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E))
      Diag(BO->getOperatorLoc(), diag::warn_unused_expr)
        << BO->getLHS()->getSourceRange() << BO->getRHS()->getSourceRange();
    else if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E))
      Diag(UO->getOperatorLoc(), diag::warn_unused_expr)
        << UO->getSubExpr()->getSourceRange();
    else
      Diag(E->getExprLoc(), diag::warn_unused_expr) << E->getSourceRange();
  }

  return Owned(new CompoundStmt(Elts, NumElts, L, R));
}

Action::OwningStmtResult
Sema::ActOnCaseStmt(SourceLocation CaseLoc, ExprArg lhsval,
                    SourceLocation DotDotDotLoc, ExprArg rhsval,
                    SourceLocation ColonLoc, StmtArg subStmt) {
  Stmt *SubStmt = static_cast<Stmt*>(subStmt.release());
  assert((lhsval.get() != 0) && "missing expression in case statement");

  // C99 6.8.4.2p3: The expression shall be an integer constant.
  // However, GCC allows any evaluatable integer expression. 

  Expr *LHSVal = static_cast<Expr*>(lhsval.get());
  if (VerifyIntegerConstantExpression(LHSVal))
    return Owned(SubStmt);

  // GCC extension: The expression shall be an integer constant.

  Expr *RHSVal = static_cast<Expr*>(rhsval.get());
  if (RHSVal && VerifyIntegerConstantExpression(RHSVal)) {
    RHSVal = 0;  // Recover by just forgetting about it.
    rhsval = 0;
  }

  if (SwitchStack.empty()) {
    Diag(CaseLoc, diag::err_case_not_in_switch);
    return Owned(SubStmt);
  }

  // Only now release the smart pointers.
  lhsval.release();
  rhsval.release();
  CaseStmt *CS = new CaseStmt(LHSVal, RHSVal, SubStmt, CaseLoc);
  SwitchStack.back()->addSwitchCase(CS);
  return Owned(CS);
}

Action::OwningStmtResult
Sema::ActOnDefaultStmt(SourceLocation DefaultLoc, SourceLocation ColonLoc, 
                       StmtArg subStmt, Scope *CurScope) {
  Stmt *SubStmt = static_cast<Stmt*>(subStmt.release());

  if (SwitchStack.empty()) {
    Diag(DefaultLoc, diag::err_default_not_in_switch);
    return Owned(SubStmt);
  }

  DefaultStmt *DS = new DefaultStmt(DefaultLoc, SubStmt);
  SwitchStack.back()->addSwitchCase(DS);
  return Owned(DS);
}

Action::OwningStmtResult
Sema::ActOnLabelStmt(SourceLocation IdentLoc, IdentifierInfo *II,
                     SourceLocation ColonLoc, StmtArg subStmt) {
  Stmt *SubStmt = static_cast<Stmt*>(subStmt.release());
  // Look up the record for this label identifier.
  LabelStmt *&LabelDecl = LabelMap[II];

  // If not forward referenced or defined already, just create a new LabelStmt.
  if (LabelDecl == 0)
    return Owned(LabelDecl = new LabelStmt(IdentLoc, II, SubStmt));

  assert(LabelDecl->getID() == II && "Label mismatch!");

  // Otherwise, this label was either forward reference or multiply defined.  If
  // multiply defined, reject it now.
  if (LabelDecl->getSubStmt()) {
    Diag(IdentLoc, diag::err_redefinition_of_label) << LabelDecl->getID();
    Diag(LabelDecl->getIdentLoc(), diag::note_previous_definition);
    return Owned(SubStmt);
  }

  // Otherwise, this label was forward declared, and we just found its real
  // definition.  Fill in the forward definition and return it.
  LabelDecl->setIdentLoc(IdentLoc);
  LabelDecl->setSubStmt(SubStmt);
  return Owned(LabelDecl);
}

Action::OwningStmtResult
Sema::ActOnIfStmt(SourceLocation IfLoc, ExprArg CondVal,
                  StmtArg ThenVal, SourceLocation ElseLoc,
                  StmtArg ElseVal) {
  Expr *condExpr = (Expr *)CondVal.release();

  assert(condExpr && "ActOnIfStmt(): missing expression");

  DefaultFunctionArrayConversion(condExpr);
  // Take ownership again until we're past the error checking.
  CondVal = condExpr;
  QualType condType = condExpr->getType();

  if (getLangOptions().CPlusPlus) {
    if (CheckCXXBooleanCondition(condExpr)) // C++ 6.4p4
      return StmtError();
  } else if (!condType->isScalarType()) // C99 6.8.4.1p1
    return StmtError(Diag(IfLoc, diag::err_typecheck_statement_requires_scalar)
      << condType << condExpr->getSourceRange());

  Stmt *thenStmt = (Stmt *)ThenVal.release();

  // Warn if the if block has a null body without an else value.
  // this helps prevent bugs due to typos, such as
  // if (condition);
  //   do_stuff();
  if (!ElseVal.get()) { 
    if (NullStmt* stmt = dyn_cast<NullStmt>(thenStmt))
      Diag(stmt->getSemiLoc(), diag::warn_empty_if_body);
  }

  CondVal.release();
  return Owned(new IfStmt(IfLoc, condExpr, thenStmt, (Stmt*)ElseVal.release()));
}

Action::OwningStmtResult
Sema::ActOnStartOfSwitchStmt(ExprArg cond) {
  Expr *Cond = static_cast<Expr*>(cond.release());

  if (getLangOptions().CPlusPlus) {
    // C++ 6.4.2.p2:
    // The condition shall be of integral type, enumeration type, or of a class
    // type for which a single conversion function to integral or enumeration
    // type exists (12.3). If the condition is of class type, the condition is
    // converted by calling that conversion function, and the result of the
    // conversion is used in place of the original condition for the remainder
    // of this section. Integral promotions are performed.

    QualType Ty = Cond->getType();

    // FIXME: Handle class types.

    // If the type is wrong a diagnostic will be emitted later at
    // ActOnFinishSwitchStmt.
    if (Ty->isIntegralType() || Ty->isEnumeralType()) {
      // Integral promotions are performed.
      // FIXME: Integral promotions for C++ are not complete.
      UsualUnaryConversions(Cond);
    }
  } else {
    // C99 6.8.4.2p5 - Integer promotions are performed on the controlling expr.
    UsualUnaryConversions(Cond);
  }

  SwitchStmt *SS = new SwitchStmt(Cond);
  SwitchStack.push_back(SS);
  return Owned(SS);
}

/// ConvertIntegerToTypeWarnOnOverflow - Convert the specified APInt to have
/// the specified width and sign.  If an overflow occurs, detect it and emit
/// the specified diagnostic.
void Sema::ConvertIntegerToTypeWarnOnOverflow(llvm::APSInt &Val,
                                              unsigned NewWidth, bool NewSign,
                                              SourceLocation Loc, 
                                              unsigned DiagID) {
  // Perform a conversion to the promoted condition type if needed.
  if (NewWidth > Val.getBitWidth()) {
    // If this is an extension, just do it.
    llvm::APSInt OldVal(Val);
    Val.extend(NewWidth);
    
    // If the input was signed and negative and the output is unsigned,
    // warn.
    if (!NewSign && OldVal.isSigned() && OldVal.isNegative())
      Diag(Loc, DiagID) << OldVal.toString(10) << Val.toString(10);
    
    Val.setIsSigned(NewSign);
  } else if (NewWidth < Val.getBitWidth()) {
    // If this is a truncation, check for overflow.
    llvm::APSInt ConvVal(Val);
    ConvVal.trunc(NewWidth);
    ConvVal.setIsSigned(NewSign);
    ConvVal.extend(Val.getBitWidth());
    ConvVal.setIsSigned(Val.isSigned());
    if (ConvVal != Val)
      Diag(Loc, DiagID) << Val.toString(10) << ConvVal.toString(10);
    
    // Regardless of whether a diagnostic was emitted, really do the
    // truncation.
    Val.trunc(NewWidth);
    Val.setIsSigned(NewSign);
  } else if (NewSign != Val.isSigned()) {
    // Convert the sign to match the sign of the condition.  This can cause
    // overflow as well: unsigned(INTMIN)
    llvm::APSInt OldVal(Val);
    Val.setIsSigned(NewSign);
    
    if (Val.isNegative())  // Sign bit changes meaning.
      Diag(Loc, DiagID) << OldVal.toString(10) << Val.toString(10);
  }
}

namespace {
  struct CaseCompareFunctor {
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const llvm::APSInt &RHS) {
      return LHS.first < RHS;
    }
    bool operator()(const std::pair<llvm::APSInt, CaseStmt*> &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS.first < RHS.first;
    }
    bool operator()(const llvm::APSInt &LHS,
                    const std::pair<llvm::APSInt, CaseStmt*> &RHS) {
      return LHS < RHS.first;
    }
  };
}

/// CmpCaseVals - Comparison predicate for sorting case values.
///
static bool CmpCaseVals(const std::pair<llvm::APSInt, CaseStmt*>& lhs,
                        const std::pair<llvm::APSInt, CaseStmt*>& rhs) {
  if (lhs.first < rhs.first)
    return true;

  if (lhs.first == rhs.first &&
      lhs.second->getCaseLoc().getRawEncoding()
       < rhs.second->getCaseLoc().getRawEncoding())
    return true;
  return false;
}

Action::OwningStmtResult
Sema::ActOnFinishSwitchStmt(SourceLocation SwitchLoc, StmtArg Switch,
                            StmtArg Body) {
  Stmt *BodyStmt = (Stmt*)Body.release();

  SwitchStmt *SS = SwitchStack.back();
  assert(SS == (SwitchStmt*)Switch.get() && "switch stack missing push/pop!");

  SS->setBody(BodyStmt, SwitchLoc);
  SwitchStack.pop_back(); 

  Expr *CondExpr = SS->getCond();
  QualType CondType = CondExpr->getType();

  if (!CondType->isIntegerType()) { // C99 6.8.4.2p1
    Diag(SwitchLoc, diag::err_typecheck_statement_requires_integer)
      << CondType << CondExpr->getSourceRange();
    return StmtError();
  }

  // Get the bitwidth of the switched-on value before promotions.  We must
  // convert the integer case values to this width before comparison.
  unsigned CondWidth = static_cast<unsigned>(Context.getTypeSize(CondType));
  bool CondIsSigned = CondType->isSignedIntegerType();
  
  // Accumulate all of the case values in a vector so that we can sort them
  // and detect duplicates.  This vector contains the APInt for the case after
  // it has been converted to the condition type.
  typedef llvm::SmallVector<std::pair<llvm::APSInt, CaseStmt*>, 64> CaseValsTy;
  CaseValsTy CaseVals;
  
  // Keep track of any GNU case ranges we see.  The APSInt is the low value.
  std::vector<std::pair<llvm::APSInt, CaseStmt*> > CaseRanges;
  
  DefaultStmt *TheDefaultStmt = 0;
  
  bool CaseListIsErroneous = false;
  
  for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase()) {
    
    if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC)) {
      if (TheDefaultStmt) {
        Diag(DS->getDefaultLoc(), diag::err_multiple_default_labels_defined);
        Diag(TheDefaultStmt->getDefaultLoc(), diag::note_duplicate_case_prev);

        // FIXME: Remove the default statement from the switch block so that
        // we'll return a valid AST.  This requires recursing down the
        // AST and finding it, not something we are set up to do right now.  For
        // now, just lop the entire switch stmt out of the AST.
        CaseListIsErroneous = true;
      }
      TheDefaultStmt = DS;
      
    } else {
      CaseStmt *CS = cast<CaseStmt>(SC);
      
      // We already verified that the expression has a i-c-e value (C99
      // 6.8.4.2p3) - get that value now.
      Expr *Lo = CS->getLHS();
      llvm::APSInt LoVal = Lo->EvaluateAsInt(Context);
      
      // Convert the value to the same width/sign as the condition.
      ConvertIntegerToTypeWarnOnOverflow(LoVal, CondWidth, CondIsSigned,
                                         CS->getLHS()->getLocStart(),
                                         diag::warn_case_value_overflow);

      // If the LHS is not the same type as the condition, insert an implicit
      // cast.
      ImpCastExprToType(Lo, CondType);
      CS->setLHS(Lo);
      
      // If this is a case range, remember it in CaseRanges, otherwise CaseVals.
      if (CS->getRHS())
        CaseRanges.push_back(std::make_pair(LoVal, CS));
      else 
        CaseVals.push_back(std::make_pair(LoVal, CS));
    }
  }
  
  // Sort all the scalar case values so we can easily detect duplicates.
  std::stable_sort(CaseVals.begin(), CaseVals.end(), CmpCaseVals);
  
  if (!CaseVals.empty()) {
    for (unsigned i = 0, e = CaseVals.size()-1; i != e; ++i) {
      if (CaseVals[i].first == CaseVals[i+1].first) {
        // If we have a duplicate, report it.
        Diag(CaseVals[i+1].second->getLHS()->getLocStart(),
             diag::err_duplicate_case) << CaseVals[i].first.toString(10);
        Diag(CaseVals[i].second->getLHS()->getLocStart(), 
             diag::note_duplicate_case_prev);
        // FIXME: We really want to remove the bogus case stmt from the substmt,
        // but we have no way to do this right now.
        CaseListIsErroneous = true;
      }
    }
  }
  
  // Detect duplicate case ranges, which usually don't exist at all in the first
  // place.
  if (!CaseRanges.empty()) {
    // Sort all the case ranges by their low value so we can easily detect
    // overlaps between ranges.
    std::stable_sort(CaseRanges.begin(), CaseRanges.end());
    
    // Scan the ranges, computing the high values and removing empty ranges.
    std::vector<llvm::APSInt> HiVals;
    for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
      CaseStmt *CR = CaseRanges[i].second;
      Expr *Hi = CR->getRHS();
      llvm::APSInt HiVal = Hi->EvaluateAsInt(Context);

      // Convert the value to the same width/sign as the condition.
      ConvertIntegerToTypeWarnOnOverflow(HiVal, CondWidth, CondIsSigned,
                                         CR->getRHS()->getLocStart(),
                                         diag::warn_case_value_overflow);
      
      // If the LHS is not the same type as the condition, insert an implicit
      // cast.
      ImpCastExprToType(Hi, CondType);
      CR->setRHS(Hi);
      
      // If the low value is bigger than the high value, the case is empty.
      if (CaseRanges[i].first > HiVal) {
        Diag(CR->getLHS()->getLocStart(), diag::warn_case_empty_range)
          << SourceRange(CR->getLHS()->getLocStart(),
                         CR->getRHS()->getLocEnd());
        CaseRanges.erase(CaseRanges.begin()+i);
        --i, --e;
        continue;
      }
      HiVals.push_back(HiVal);
    }

    // Rescan the ranges, looking for overlap with singleton values and other
    // ranges.  Since the range list is sorted, we only need to compare case
    // ranges with their neighbors.
    for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
      llvm::APSInt &CRLo = CaseRanges[i].first;
      llvm::APSInt &CRHi = HiVals[i];
      CaseStmt *CR = CaseRanges[i].second;
      
      // Check to see whether the case range overlaps with any singleton cases.
      CaseStmt *OverlapStmt = 0;
      llvm::APSInt OverlapVal(32);
      
      // Find the smallest value >= the lower bound.  If I is in the case range,
      // then we have overlap.
      CaseValsTy::iterator I = std::lower_bound(CaseVals.begin(),
                                                CaseVals.end(), CRLo,
                                                CaseCompareFunctor());
      if (I != CaseVals.end() && I->first < CRHi) {
        OverlapVal  = I->first;   // Found overlap with scalar.
        OverlapStmt = I->second;
      }

      // Find the smallest value bigger than the upper bound.
      I = std::upper_bound(I, CaseVals.end(), CRHi, CaseCompareFunctor());
      if (I != CaseVals.begin() && (I-1)->first >= CRLo) {
        OverlapVal  = (I-1)->first;      // Found overlap with scalar.
        OverlapStmt = (I-1)->second;
      }

      // Check to see if this case stmt overlaps with the subsequent case range.
      if (i && CRLo <= HiVals[i-1]) {
        OverlapVal  = HiVals[i-1];       // Found overlap with range.
        OverlapStmt = CaseRanges[i-1].second;
      }
      
      if (OverlapStmt) {
        // If we have a duplicate, report it.
        Diag(CR->getLHS()->getLocStart(), diag::err_duplicate_case)
          << OverlapVal.toString(10);
        Diag(OverlapStmt->getLHS()->getLocStart(), 
             diag::note_duplicate_case_prev);
        // FIXME: We really want to remove the bogus case stmt from the substmt,
        // but we have no way to do this right now.
        CaseListIsErroneous = true;
      }
    }
  }
  
  // FIXME: If the case list was broken is some way, we don't have a good system
  // to patch it up.  Instead, just return the whole substmt as broken.
  if (CaseListIsErroneous)
    return StmtError();

  Switch.release();
  return Owned(SS);
}

Action::OwningStmtResult
Sema::ActOnWhileStmt(SourceLocation WhileLoc, ExprArg Cond, StmtArg Body) {
  Expr *condExpr = (Expr *)Cond.release();
  assert(condExpr && "ActOnWhileStmt(): missing expression");

  DefaultFunctionArrayConversion(condExpr);
  Cond = condExpr;
  QualType condType = condExpr->getType();

  if (getLangOptions().CPlusPlus) {
    if (CheckCXXBooleanCondition(condExpr)) // C++ 6.4p4
      return StmtError();
  } else if (!condType->isScalarType()) // C99 6.8.5p2
    return StmtError(Diag(WhileLoc,
      diag::err_typecheck_statement_requires_scalar)
      << condType << condExpr->getSourceRange());

  Cond.release();
  return Owned(new WhileStmt(condExpr, (Stmt*)Body.release(), WhileLoc));
}

Action::OwningStmtResult
Sema::ActOnDoStmt(SourceLocation DoLoc, StmtArg Body,
                  SourceLocation WhileLoc, ExprArg Cond) {
  Expr *condExpr = (Expr *)Cond.release();
  assert(condExpr && "ActOnDoStmt(): missing expression");

  DefaultFunctionArrayConversion(condExpr);
  Cond = condExpr;
  QualType condType = condExpr->getType();

  if (getLangOptions().CPlusPlus) {
    if (CheckCXXBooleanCondition(condExpr)) // C++ 6.4p4
      return StmtError();
  } else if (!condType->isScalarType()) // C99 6.8.5p2
    return StmtError(Diag(DoLoc, diag::err_typecheck_statement_requires_scalar)
      << condType << condExpr->getSourceRange());

  Cond.release();
  return Owned(new DoStmt((Stmt*)Body.release(), condExpr, DoLoc));
}

Action::OwningStmtResult
Sema::ActOnForStmt(SourceLocation ForLoc, SourceLocation LParenLoc,
                   StmtArg first, ExprArg second, ExprArg third,
                   SourceLocation RParenLoc, StmtArg body) {
  Stmt *First  = static_cast<Stmt*>(first.get());
  Expr *Second = static_cast<Expr*>(second.get());
  Expr *Third  = static_cast<Expr*>(third.get());
  Stmt *Body  = static_cast<Stmt*>(body.get());

  if (!getLangOptions().CPlusPlus) {
    if (DeclStmt *DS = dyn_cast_or_null<DeclStmt>(First)) {
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      for (DeclStmt::decl_iterator DI=DS->decl_begin(), DE=DS->decl_end();
           DI!=DE; ++DI) {
        VarDecl *VD = dyn_cast<VarDecl>(*DI);
        if (VD && VD->isBlockVarDecl() && !VD->hasLocalStorage())
          VD = 0;
        if (VD == 0)
          Diag((*DI)->getLocation(), diag::err_non_variable_decl_in_for);
        // FIXME: mark decl erroneous!
      }
    }
  }
  if (Second) {
    DefaultFunctionArrayConversion(Second);
    QualType SecondType = Second->getType();

    if (getLangOptions().CPlusPlus) {
      if (CheckCXXBooleanCondition(Second)) // C++ 6.4p4
        return StmtError();
    } else if (!SecondType->isScalarType()) // C99 6.8.5p2
      return StmtError(Diag(ForLoc,
                            diag::err_typecheck_statement_requires_scalar)
        << SecondType << Second->getSourceRange());
  }
  first.release();
  second.release();
  third.release();
  body.release();
  return Owned(new ForStmt(First, Second, Third, Body, ForLoc));
}

Action::OwningStmtResult
Sema::ActOnObjCForCollectionStmt(SourceLocation ForLoc,
                                 SourceLocation LParenLoc,
                                 StmtArg first, ExprArg second,
                                 SourceLocation RParenLoc, StmtArg body) {
  Stmt *First  = static_cast<Stmt*>(first.get());
  Expr *Second = static_cast<Expr*>(second.get());
  Stmt *Body  = static_cast<Stmt*>(body.get());
  if (First) {
    QualType FirstType;
    if (DeclStmt *DS = dyn_cast<DeclStmt>(First)) {
      if (!DS->hasSolitaryDecl())
        return StmtError(Diag((*DS->decl_begin())->getLocation(),
                         diag::err_toomany_element_decls));

      Decl *D = DS->getSolitaryDecl();
      FirstType = cast<ValueDecl>(D)->getType();
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      VarDecl *VD = cast<VarDecl>(D);
      if (VD->isBlockVarDecl() && !VD->hasLocalStorage())
        return StmtError(Diag(VD->getLocation(),
                              diag::err_non_variable_decl_in_for));
    } else {
      Expr::isLvalueResult lval = cast<Expr>(First)->isLvalue(Context);

      if (lval != Expr::LV_Valid)
        return StmtError(Diag(First->getLocStart(),
                   diag::err_selector_element_not_lvalue)
          << First->getSourceRange());

      FirstType = static_cast<Expr*>(First)->getType();        
    }
    if (!Context.isObjCObjectPointerType(FirstType))
        Diag(ForLoc, diag::err_selector_element_type)
          << FirstType << First->getSourceRange();
  }
  if (Second) {
    DefaultFunctionArrayConversion(Second);
    QualType SecondType = Second->getType();
    if (!Context.isObjCObjectPointerType(SecondType))
      Diag(ForLoc, diag::err_collection_expr_type)
        << SecondType << Second->getSourceRange();
  }
  first.release();
  second.release();
  body.release();
  return Owned(new ObjCForCollectionStmt(First, Second, Body,
                                         ForLoc, RParenLoc));
}

Action::OwningStmtResult
Sema::ActOnGotoStmt(SourceLocation GotoLoc, SourceLocation LabelLoc,
                    IdentifierInfo *LabelII) {
  // If we are in a block, reject all gotos for now.
  if (CurBlock)
    return StmtError(Diag(GotoLoc, diag::err_goto_in_block));

  // Look up the record for this label identifier.
  LabelStmt *&LabelDecl = LabelMap[LabelII];

  // If we haven't seen this label yet, create a forward reference.
  if (LabelDecl == 0)
    LabelDecl = new LabelStmt(LabelLoc, LabelII, 0);

  return Owned(new GotoStmt(LabelDecl, GotoLoc, LabelLoc));
}

Action::OwningStmtResult
Sema::ActOnIndirectGotoStmt(SourceLocation GotoLoc,SourceLocation StarLoc,
                            ExprArg DestExp) {
  // FIXME: Verify that the operand is convertible to void*.

  return Owned(new IndirectGotoStmt((Expr*)DestExp.release()));
}

Action::OwningStmtResult
Sema::ActOnContinueStmt(SourceLocation ContinueLoc, Scope *CurScope) {
  Scope *S = CurScope->getContinueParent();
  if (!S) {
    // C99 6.8.6.2p1: A break shall appear only in or as a loop body.
    return StmtError(Diag(ContinueLoc, diag::err_continue_not_in_loop));
  }

  return Owned(new ContinueStmt(ContinueLoc));
}

Action::OwningStmtResult
Sema::ActOnBreakStmt(SourceLocation BreakLoc, Scope *CurScope) {
  Scope *S = CurScope->getBreakParent();
  if (!S) {
    // C99 6.8.6.3p1: A break shall appear only in or as a switch/loop body.
    return StmtError(Diag(BreakLoc, diag::err_break_not_in_loop_or_switch));
  }

  return Owned(new BreakStmt(BreakLoc));
}

/// ActOnBlockReturnStmt - Utility routine to figure out block's return type.
///
Action::OwningStmtResult
Sema::ActOnBlockReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp) {

  // If this is the first return we've seen in the block, infer the type of
  // the block from it.
  if (CurBlock->ReturnType == 0) {
    if (RetValExp) {
      // Don't call UsualUnaryConversions(), since we don't want to do
      // integer promotions here.
      DefaultFunctionArrayConversion(RetValExp);
      CurBlock->ReturnType = RetValExp->getType().getTypePtr();
    } else
      CurBlock->ReturnType = Context.VoidTy.getTypePtr();
    return Owned(new ReturnStmt(ReturnLoc, RetValExp));
  }

  // Otherwise, verify that this result type matches the previous one.  We are
  // pickier with blocks than for normal functions because we don't have GCC
  // compatibility to worry about here.
  if (CurBlock->ReturnType->isVoidType()) {
    if (RetValExp) {
      Diag(ReturnLoc, diag::err_return_block_has_expr);
      delete RetValExp;
      RetValExp = 0;
    }
    return Owned(new ReturnStmt(ReturnLoc, RetValExp));
  }

  if (!RetValExp)
    return StmtError(Diag(ReturnLoc, diag::err_block_return_missing_expr));

  // we have a non-void block with an expression, continue checking
  QualType RetValType = RetValExp->getType();

  // For now, restrict multiple return statements in a block to have 
  // strict compatible types only.
  QualType BlockQT = QualType(CurBlock->ReturnType, 0);
  if (Context.getCanonicalType(BlockQT).getTypePtr() 
      != Context.getCanonicalType(RetValType).getTypePtr()) {
    // FIXME: non-localizable string in diagnostic
    DiagnoseAssignmentResult(Incompatible, ReturnLoc, BlockQT,
                             RetValType, RetValExp, "returning");
    return StmtError();
  }

  if (RetValExp) CheckReturnStackAddr(RetValExp, BlockQT, ReturnLoc);

  return Owned(new ReturnStmt(ReturnLoc, RetValExp));
}

Action::OwningStmtResult
Sema::ActOnReturnStmt(SourceLocation ReturnLoc, ExprArg rex) {
  Expr *RetValExp = static_cast<Expr *>(rex.release());
  if (CurBlock)
    return ActOnBlockReturnStmt(ReturnLoc, RetValExp);

  QualType FnRetType;
  if (FunctionDecl *FD = getCurFunctionDecl())
    FnRetType = FD->getResultType();
  else
    FnRetType = getCurMethodDecl()->getResultType();

  if (FnRetType->isVoidType()) {
    if (RetValExp) {// C99 6.8.6.4p1 (ext_ since GCC warns)
      unsigned D = diag::ext_return_has_expr;
      if (RetValExp->getType()->isVoidType())
        D = diag::ext_return_has_void_expr;

      // return (some void expression); is legal in C++.
      if (D != diag::ext_return_has_void_expr ||
          !getLangOptions().CPlusPlus) {
        NamedDecl *CurDecl = getCurFunctionOrMethodDecl();
        Diag(ReturnLoc, D)
          << CurDecl->getDeclName() << isa<ObjCMethodDecl>(CurDecl)
          << RetValExp->getSourceRange();
      }
    }
    return Owned(new ReturnStmt(ReturnLoc, RetValExp));
  }

  if (!RetValExp) {
    unsigned DiagID = diag::warn_return_missing_expr;  // C90 6.6.6.4p4
    // C99 6.8.6.4p1 (ext_ since GCC warns)
    if (getLangOptions().C99) DiagID = diag::ext_return_missing_expr;

    if (FunctionDecl *FD = getCurFunctionDecl())
      Diag(ReturnLoc, DiagID) << FD->getIdentifier() << 0/*fn*/;
    else
      Diag(ReturnLoc, DiagID) << getCurMethodDecl()->getDeclName() << 1/*meth*/;
    return Owned(new ReturnStmt(ReturnLoc, (Expr*)0));
  }

  if (!FnRetType->isDependentType() && !RetValExp->isTypeDependent()) {
    // we have a non-void function with an expression, continue checking
    QualType RetValType = RetValExp->getType();

    // C99 6.8.6.4p3(136): The return statement is not an assignment. The 
    // overlap restriction of subclause 6.5.16.1 does not apply to the case of 
    // function return.

    // In C++ the return statement is handled via a copy initialization.
    // the C version of which boils down to CheckSingleAssignmentConstraints.
    // FIXME: Leaks RetValExp.
    if (PerformCopyInitialization(RetValExp, FnRetType, "returning"))
      return StmtError();

    if (RetValExp) CheckReturnStackAddr(RetValExp, FnRetType, ReturnLoc);
  }

  return Owned(new ReturnStmt(ReturnLoc, RetValExp));
}

Sema::OwningStmtResult Sema::ActOnAsmStmt(SourceLocation AsmLoc,
                                          bool IsSimple,
                                          bool IsVolatile,
                                          unsigned NumOutputs,
                                          unsigned NumInputs,
                                          std::string *Names,
                                          MultiExprArg constraints,
                                          MultiExprArg exprs,
                                          ExprArg asmString,
                                          MultiExprArg clobbers,
                                          SourceLocation RParenLoc) {
  unsigned NumClobbers = clobbers.size();
  StringLiteral **Constraints =
    reinterpret_cast<StringLiteral**>(constraints.get());
  Expr **Exprs = reinterpret_cast<Expr **>(exprs.get());
  StringLiteral *AsmString = cast<StringLiteral>((Expr *)asmString.get());
  StringLiteral **Clobbers = reinterpret_cast<StringLiteral**>(clobbers.get());

  llvm::SmallVector<TargetInfo::ConstraintInfo, 4> OutputConstraintInfos;
  
  // The parser verifies that there is a string literal here.
  if (AsmString->isWide())
    return StmtError(Diag(AsmString->getLocStart(),diag::err_asm_wide_character)
      << AsmString->getSourceRange());


  for (unsigned i = 0; i != NumOutputs; i++) {
    StringLiteral *Literal = Constraints[i];
    if (Literal->isWide())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    std::string OutputConstraint(Literal->getStrData(), 
                                 Literal->getByteLength());

    TargetInfo::ConstraintInfo info;
    if (!Context.Target.validateOutputConstraint(OutputConstraint.c_str(),info))
      return StmtError(Diag(Literal->getLocStart(),
                  diag::err_asm_invalid_output_constraint) << OutputConstraint);

    // Check that the output exprs are valid lvalues.
    ParenExpr *OutputExpr = cast<ParenExpr>(Exprs[i]);
    Expr::isLvalueResult Result = OutputExpr->isLvalue(Context);
    if (Result != Expr::LV_Valid) {
      return StmtError(Diag(OutputExpr->getSubExpr()->getLocStart(),
                  diag::err_asm_invalid_lvalue_in_output)
        << OutputExpr->getSubExpr()->getSourceRange());
    }
    
    OutputConstraintInfos.push_back(info);
  }

  for (unsigned i = NumOutputs, e = NumOutputs + NumInputs; i != e; i++) {
    StringLiteral *Literal = Constraints[i];
    if (Literal->isWide())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    std::string InputConstraint(Literal->getStrData(),
                                Literal->getByteLength());

    TargetInfo::ConstraintInfo info;
    if (!Context.Target.validateInputConstraint(InputConstraint.c_str(),
                                                &Names[0],
                                                &Names[0] + NumOutputs, 
                                                &OutputConstraintInfos[0],
                                                info)) {
      return StmtError(Diag(Literal->getLocStart(),
                  diag::err_asm_invalid_input_constraint) << InputConstraint);
    }

    ParenExpr *InputExpr = cast<ParenExpr>(Exprs[i]);

    // Only allow void types for memory constraints.
    if ((info & TargetInfo::CI_AllowsMemory) 
        && !(info & TargetInfo::CI_AllowsRegister)) {
      if (InputExpr->isLvalue(Context) != Expr::LV_Valid)
        return StmtError(Diag(InputExpr->getSubExpr()->getLocStart(),
                              diag::err_asm_invalid_lvalue_in_input)
          << InputConstraint << InputExpr->getSubExpr()->getSourceRange());
    }

    if (info & TargetInfo::CI_AllowsRegister) {
      if (InputExpr->getType()->isVoidType()) {
        return StmtError(Diag(InputExpr->getSubExpr()->getLocStart(),
                              diag::err_asm_invalid_type_in_input)
          << InputExpr->getType() << InputConstraint 
          << InputExpr->getSubExpr()->getSourceRange());
      }
      
      DefaultFunctionArrayConversion(Exprs[i]);
    }
  }

  // Check that the clobbers are valid.
  for (unsigned i = 0; i != NumClobbers; i++) {
    StringLiteral *Literal = Clobbers[i];
    if (Literal->isWide())
      return StmtError(Diag(Literal->getLocStart(),diag::err_asm_wide_character)
        << Literal->getSourceRange());

    llvm::SmallString<16> Clobber(Literal->getStrData(),
                                  Literal->getStrData() +
                                  Literal->getByteLength());

    if (!Context.Target.isValidGCCRegisterName(Clobber.c_str()))
      return StmtError(Diag(Literal->getLocStart(),
                  diag::err_asm_unknown_register_name) << Clobber.c_str());
  }

  constraints.release();
  exprs.release();
  asmString.release();
  clobbers.release();
  return Owned(new AsmStmt(AsmLoc, IsSimple, IsVolatile, NumOutputs, NumInputs,
                           Names, Constraints, Exprs, AsmString, NumClobbers,
                           Clobbers, RParenLoc));
}

Action::OwningStmtResult
Sema::ActOnObjCAtCatchStmt(SourceLocation AtLoc,
                           SourceLocation RParen, StmtArg Parm,
                           StmtArg Body, StmtArg catchList) {
  Stmt *CatchList = static_cast<Stmt*>(catchList.release());
  ObjCAtCatchStmt *CS = new ObjCAtCatchStmt(AtLoc, RParen,
    static_cast<Stmt*>(Parm.release()), static_cast<Stmt*>(Body.release()),
    CatchList);
  return Owned(CatchList ? CatchList : CS);
}

Action::OwningStmtResult
Sema::ActOnObjCAtFinallyStmt(SourceLocation AtLoc, StmtArg Body) {
  return Owned(new ObjCAtFinallyStmt(AtLoc,
                                     static_cast<Stmt*>(Body.release())));
}

Action::OwningStmtResult
Sema::ActOnObjCAtTryStmt(SourceLocation AtLoc,
                         StmtArg Try, StmtArg Catch, StmtArg Finally) {
  return Owned(new ObjCAtTryStmt(AtLoc, static_cast<Stmt*>(Try.release()),
                                        static_cast<Stmt*>(Catch.release()),
                                        static_cast<Stmt*>(Finally.release())));
}

Action::OwningStmtResult
Sema::ActOnObjCAtThrowStmt(SourceLocation AtLoc, ExprArg Throw) {
  return Owned(new ObjCAtThrowStmt(AtLoc, static_cast<Expr*>(Throw.release())));
}

Action::OwningStmtResult
Sema::ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc, ExprArg SynchExpr,
                                  StmtArg SynchBody) {
  return Owned(new ObjCAtSynchronizedStmt(AtLoc,
                     static_cast<Stmt*>(SynchExpr.release()),
                     static_cast<Stmt*>(SynchBody.release())));
}

/// ActOnCXXCatchBlock - Takes an exception declaration and a handler block
/// and creates a proper catch handler from them.
Action::OwningStmtResult
Sema::ActOnCXXCatchBlock(SourceLocation CatchLoc, DeclTy *ExDecl,
                         StmtArg HandlerBlock) {
  // There's nothing to test that ActOnExceptionDecl didn't already test.
  return Owned(new CXXCatchStmt(CatchLoc, static_cast<VarDecl*>(ExDecl),
                                static_cast<Stmt*>(HandlerBlock.release())));
}

/// ActOnCXXTryBlock - Takes a try compound-statement and a number of
/// handlers and creates a try statement from them.
Action::OwningStmtResult
Sema::ActOnCXXTryBlock(SourceLocation TryLoc, StmtArg TryBlock,
                       MultiStmtArg RawHandlers) {
  unsigned NumHandlers = RawHandlers.size();
  assert(NumHandlers > 0 &&
         "The parser shouldn't call this if there are no handlers.");
  Stmt **Handlers = reinterpret_cast<Stmt**>(RawHandlers.get());

  for(unsigned i = 0; i < NumHandlers - 1; ++i) {
    CXXCatchStmt *Handler = llvm::cast<CXXCatchStmt>(Handlers[i]);
    if (!Handler->getExceptionDecl())
      return StmtError(Diag(Handler->getLocStart(), diag::err_early_catch_all));
  }
  // FIXME: We should detect handlers for the same type as an earlier one.
  // This one is rather easy.
  // FIXME: We should detect handlers that cannot catch anything because an
  // earlier handler catches a superclass. Need to find a method that is not
  // quadratic for this.
  // Neither of these are explicitly forbidden, but every compiler detects them
  // and warns.

  RawHandlers.release();
  return Owned(new CXXTryStmt(TryLoc, static_cast<Stmt*>(TryBlock.release()),
                              Handlers, NumHandlers));
}
