//=-- GRExprEngineInternalChecks.cpp - Builtin GRExprEngine Checks---*- C++ -*-=
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the BugType classes used by GRExprEngine to report
//  bugs derived from builtin checks in the path-sensitive engine.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Utility functions.
//===----------------------------------------------------------------------===//

template <typename ITERATOR> inline
ExplodedNode<GRState>* GetNode(ITERATOR I) {
  return *I;
}

template <> inline
ExplodedNode<GRState>* GetNode(GRExprEngine::undef_arg_iterator I) {
  return I->first;
}

//===----------------------------------------------------------------------===//
// Bug Descriptions.
//===----------------------------------------------------------------------===//

namespace {
class VISIBILITY_HIDDEN BuiltinBug : public BugTypeCacheLocation {
protected:
  const char* name;
  const char* desc;
public:
  BuiltinBug(const char* n, const char* d = 0) : name(n), desc(d) {}  
  virtual const char* getName() const { return name; }
  virtual const char* getDescription() const {
    return desc ? desc : name;
  }
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) = 0;
  virtual void EmitWarnings(BugReporter& BR) {
    EmitBuiltinWarnings(BR, cast<GRBugReporter>(BR).getEngine());
  }
  
  template <typename ITER>
  void Emit(BugReporter& BR, ITER I, ITER E) {
    for (; I != E; ++I) {
      BugReport R(*this, GetNode(I));
      BR.EmitWarning(R);
    }
  }
  
  virtual const char* getCategory() const { return "Logic Errors"; }
};
  
class VISIBILITY_HIDDEN NullDeref : public BuiltinBug {
public:
  NullDeref() : BuiltinBug("null dereference",
                           "Dereference of null pointer.") {}

  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.null_derefs_begin(), Eng.null_derefs_end());
  }
};
  
class VISIBILITY_HIDDEN UndefinedDeref : public BuiltinBug {
public:
  UndefinedDeref() : BuiltinBug("uninitialized pointer dereference",
                                "Dereference of undefined value.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.undef_derefs_begin(), Eng.undef_derefs_end());
  }
};

class VISIBILITY_HIDDEN DivZero : public BuiltinBug {
public:
  DivZero() : BuiltinBug("divide-by-zero",
                         "Division by zero/undefined value.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.explicit_bad_divides_begin(), Eng.explicit_bad_divides_end());
  }
};
  
class VISIBILITY_HIDDEN UndefResult : public BuiltinBug {
public:
  UndefResult() : BuiltinBug("undefined result",
                             "Result of operation is undefined.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.undef_results_begin(), Eng.undef_results_end());
  }
};
  
class VISIBILITY_HIDDEN BadCall : public BuiltinBug {
public:
  BadCall()
  : BuiltinBug("invalid function call",
        "Called function is a NULL or undefined function pointer value.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.bad_calls_begin(), Eng.bad_calls_end());
  }
};

class VISIBILITY_HIDDEN BadArg : public BuiltinBug {
public:
  BadArg() : BuiltinBug("uninitialized argument",  
    "Pass-by-value argument in function is undefined.") {}

  BadArg(const char* d) : BuiltinBug("uninitialized argument", d) {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::UndefArgsTy::iterator I = Eng.undef_arg_begin(),
         E = Eng.undef_arg_end(); I!=E; ++I) {

      // Generate a report for this bug.
      RangedBugReport report(*this, I->first);
      report.addRange(I->second->getSourceRange());

      // Emit the warning.
      BR.EmitWarning(report);
    }
  }
};
  
class VISIBILITY_HIDDEN BadMsgExprArg : public BadArg {
public:
  BadMsgExprArg() 
    : BadArg("Pass-by-value argument in message expression is undefined.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::UndefArgsTy::iterator I=Eng.msg_expr_undef_arg_begin(),
         E = Eng.msg_expr_undef_arg_end(); I!=E; ++I) {
      
      // Generate a report for this bug.
      RangedBugReport report(*this, I->first);
      report.addRange(I->second->getSourceRange());
      
      // Emit the warning.
      BR.EmitWarning(report);
    }    
  }
};
  
class VISIBILITY_HIDDEN BadReceiver : public BuiltinBug {
public:  
  BadReceiver()
  : BuiltinBug("uninitialized receiver",
               "Receiver in message expression is an uninitialized value.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::ErrorNodes::iterator I=Eng.undef_receivers_begin(),
         End = Eng.undef_receivers_end(); I!=End; ++I) {
      
      // Generate a report for this bug.
      RangedBugReport report(*this, *I);
      
      ExplodedNode<GRState>* N = *I;
      Stmt *S = cast<PostStmt>(N->getLocation()).getStmt();
      Expr* E = cast<ObjCMessageExpr>(S)->getReceiver();
      assert (E && "Receiver cannot be NULL");
      report.addRange(E->getSourceRange());
      
      // Emit the warning.
      BR.EmitWarning(report);
    }    
  }
};

class VISIBILITY_HIDDEN RetStack : public BuiltinBug {
public:
  RetStack() : BuiltinBug("return of stack address") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::ret_stackaddr_iterator I=Eng.ret_stackaddr_begin(),
         End = Eng.ret_stackaddr_end(); I!=End; ++I) {

      ExplodedNode<GRState>* N = *I;
      Stmt *S = cast<PostStmt>(N->getLocation()).getStmt();
      Expr* E = cast<ReturnStmt>(S)->getRetValue();
      assert (E && "Return expression cannot be NULL");
      
      // Get the value associated with E.
      loc::MemRegionVal V =
        cast<loc::MemRegionVal>(Eng.getStateManager().GetSVal(N->getState(),
                                                               E));
      
      // Generate a report for this bug.
      std::string buf;
      llvm::raw_string_ostream os(buf);
      SourceRange R;
      
      // Check if the region is a compound literal.
      if (const CompoundLiteralRegion* CR = 
            dyn_cast<CompoundLiteralRegion>(V.getRegion())) {
        
        const CompoundLiteralExpr* CL = CR->getLiteralExpr();
        os << "Address of stack memory associated with a compound literal "
              "declared on line "
            << BR.getSourceManager()
                    .getInstantiationLineNumber(CL->getLocStart())
            << " returned.";
        
        R = CL->getSourceRange();
      }
      else if (const AllocaRegion* AR = dyn_cast<AllocaRegion>(V.getRegion())) {
        const Expr* ARE = AR->getExpr();
        SourceLocation L = ARE->getLocStart();
        R = ARE->getSourceRange();
        
        os << "Address of stack memory allocated by call to alloca() on line "
           << BR.getSourceManager().getInstantiationLineNumber(L)
           << " returned.";
      }      
      else {        
        os << "Address of stack memory associated with local variable '"
           << V.getRegion()->getString() << "' returned.";
      }
      
      RangedBugReport report(*this, N, os.str().c_str());
      report.addRange(E->getSourceRange());
      if (R.isValid()) report.addRange(R);
      
      // Emit the warning.
      BR.EmitWarning(report);
    }
  }
};
  
class VISIBILITY_HIDDEN RetUndef : public BuiltinBug {
public:
  RetUndef() : BuiltinBug("uninitialized return value",
              "Uninitialized or undefined return value returned to caller.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.ret_undef_begin(), Eng.ret_undef_end());
  }
};

class VISIBILITY_HIDDEN UndefBranch : public BuiltinBug {
  struct VISIBILITY_HIDDEN FindUndefExpr {
    GRStateManager& VM;
    const GRState* St;
    
    FindUndefExpr(GRStateManager& V, const GRState* S) : VM(V), St(S) {}
    
    Expr* FindExpr(Expr* Ex) {      
      if (!MatchesCriteria(Ex))
        return 0;
      
      for (Stmt::child_iterator I=Ex->child_begin(), E=Ex->child_end();I!=E;++I)
        if (Expr* ExI = dyn_cast_or_null<Expr>(*I)) {
          Expr* E2 = FindExpr(ExI);
          if (E2) return E2;
        }
      
      return Ex;
    }
    
    bool MatchesCriteria(Expr* Ex) { return VM.GetSVal(St, Ex).isUndef(); }
  };
  
public:
  UndefBranch()
    : BuiltinBug("uninitialized value",
                 "Branch condition evaluates to an uninitialized value.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::undef_branch_iterator I=Eng.undef_branches_begin(),
         E=Eng.undef_branches_end(); I!=E; ++I) {

      // What's going on here: we want to highlight the subexpression of the
      // condition that is the most likely source of the "uninitialized
      // branch condition."  We do a recursive walk of the condition's
      // subexpressions and roughly look for the most nested subexpression
      // that binds to Undefined.  We then highlight that expression's range.

      BlockEdge B = cast<BlockEdge>((*I)->getLocation());
      Expr* Ex = cast<Expr>(B.getSrc()->getTerminatorCondition());
      assert (Ex && "Block must have a terminator.");

      // Get the predecessor node and check if is a PostStmt with the Stmt
      // being the terminator condition.  We want to inspect the state
      // of that node instead because it will contain main information about
      // the subexpressions.

      assert (!(*I)->pred_empty());

      // Note: any predecessor will do.  They should have identical state,
      // since all the BlockEdge did was act as an error sink since the value
      // had to already be undefined.
      ExplodedNode<GRState> *N = *(*I)->pred_begin();
      ProgramPoint P = N->getLocation();

      const GRState* St = (*I)->getState();

      if (PostStmt* PS = dyn_cast<PostStmt>(&P))
        if (PS->getStmt() == Ex)
          St = N->getState();

      FindUndefExpr FindIt(Eng.getStateManager(), St);
      Ex = FindIt.FindExpr(Ex);

      RangedBugReport R(*this, *I);
      R.addRange(Ex->getSourceRange());

      BR.EmitWarning(R);
    }
  }
};

class VISIBILITY_HIDDEN OutOfBoundMemoryAccess : public BuiltinBug {
public:
  OutOfBoundMemoryAccess() : BuiltinBug("out-of-bound memory access",
                       "Load or store into an out-of-bound memory position.") {}

  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    Emit(BR, Eng.explicit_oob_memacc_begin(), Eng.explicit_oob_memacc_end());
  }
};
  
class VISIBILITY_HIDDEN BadSizeVLA : public BuiltinBug {

public:
  BadSizeVLA() : BuiltinBug("Zero-sized VLA",  
                             "VLAs with zero-size are undefined.") {}
  
  virtual void EmitBuiltinWarnings(BugReporter& BR, GRExprEngine& Eng) {
    for (GRExprEngine::ErrorNodes::iterator
          I = Eng.ExplicitBadSizedVLA.begin(),
          E = Eng.ExplicitBadSizedVLA.end(); I!=E; ++I) {

      // Determine whether this was a 'zero-sized' VLA or a VLA with an
      // undefined size.
      GRExprEngine::NodeTy* N = *I;
      PostStmt PS = cast<PostStmt>(N->getLocation());      
      DeclStmt *DS = cast<DeclStmt>(PS.getStmt());
      VarDecl* VD = cast<VarDecl>(*DS->decl_begin());
      QualType T = Eng.getContext().getCanonicalType(VD->getType());
      VariableArrayType* VT = cast<VariableArrayType>(T);
      Expr* SizeExpr = VT->getSizeExpr();
      
      std::string buf;
      llvm::raw_string_ostream os(buf);
      os << "The expression used to specify the number of elements in the VLA '"
          << VD->getNameAsString() << "' evaluates to ";
      
      SVal X = Eng.getStateManager().GetSVal(N->getState(), SizeExpr);
      if (X.isUndef()) {
        name = "Undefined size for VLA";
        os << "an undefined or garbage value.";
      }
      else {
        name = "Zero-sized VLA";
        os << " to 0.  VLAs with no elements have undefined behavior.";
      }

      desc = os.str().c_str();
      RangedBugReport report(*this, N);
      report.addRange(SizeExpr->getSourceRange());
      
      // Emit the warning.
      BR.EmitWarning(report);
    }
  }
};

//===----------------------------------------------------------------------===//
// __attribute__(nonnull) checking

class VISIBILITY_HIDDEN CheckAttrNonNull : public GRSimpleAPICheck {
  SimpleBugType BT;
  std::list<RangedBugReport> Reports;
  
public:
  CheckAttrNonNull() :
  BT("'nonnull' argument passed null", "API",
     "Null pointer passed as an argument to a 'nonnull' parameter") {}

  virtual bool Audit(ExplodedNode<GRState>* N, GRStateManager& VMgr) {
    CallExpr* CE = cast<CallExpr>(cast<PostStmt>(N->getLocation()).getStmt());
    const GRState* state = N->getState();
    
    SVal X = VMgr.GetSVal(state, CE->getCallee());
    
    if (!isa<loc::FuncVal>(X))
      return false;
    
    FunctionDecl* FD = dyn_cast<FunctionDecl>(cast<loc::FuncVal>(X).getDecl());
    const NonNullAttr* Att = FD->getAttr<NonNullAttr>();
    
    if (!Att)
      return false;
    
    // Iterate through the arguments of CE and check them for null.
    
    unsigned idx = 0;
    bool hasError = false;
    
    for (CallExpr::arg_iterator I=CE->arg_begin(), E=CE->arg_end(); I!=E;
         ++I, ++idx) {
      
      if (!VMgr.isEqual(state, *I, 0) || !Att->isNonNull(idx))
        continue;
      
      RangedBugReport R(BT, N);
      R.addRange((*I)->getSourceRange());
      Reports.push_back(R);
      hasError = true;
    }
    
    return hasError;
  }
  
  virtual void EmitWarnings(BugReporter& BR) {
    for (std::list<RangedBugReport>::iterator I=Reports.begin(),
         E=Reports.end(); I!=E; ++I)
      BR.EmitWarning(*I);
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Check registration.

void GRExprEngine::RegisterInternalChecks() {
  Register(new NullDeref());
  Register(new UndefinedDeref());
  Register(new UndefBranch());
  Register(new DivZero());
  Register(new UndefResult());
  Register(new BadCall());
  Register(new RetStack());
  Register(new RetUndef());
  Register(new BadArg());
  Register(new BadMsgExprArg());
  Register(new BadReceiver());
  Register(new OutOfBoundMemoryAccess());
  Register(new BadSizeVLA());
  AddCheck(new CheckAttrNonNull(), Stmt::CallExprClass); 
}
