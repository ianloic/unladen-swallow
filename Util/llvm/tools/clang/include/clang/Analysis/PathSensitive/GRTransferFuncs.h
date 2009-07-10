//== GRTransferFuncs.h - Path-Sens. Transfer Functions Interface -*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines GRTransferFuncs, which provides a base-class that
//  defines an interface for transfer functions used by GRExprEngine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_GRTF
#define LLVM_CLANG_ANALYSIS_GRTF

#include "clang/Analysis/PathSensitive/SVals.h"
#include "clang/Analysis/PathSensitive/GRCoreEngine.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include <vector>

namespace clang {
  
class GRExprEngine;
class BugReporter;
class ObjCMessageExpr;
class GRStmtNodeBuilderRef;
  
class GRTransferFuncs {
public:
  GRTransferFuncs() {}
  virtual ~GRTransferFuncs() {}
  
  virtual void RegisterPrinters(std::vector<GRState::Printer*>& Printers) {}
  virtual void RegisterChecks(BugReporter& BR) {}
  

  // Calls.
  
  virtual void EvalCall(ExplodedNodeSet<GRState>& Dst,
                        GRExprEngine& Engine,
                        GRStmtNodeBuilder<GRState>& Builder,
                        CallExpr* CE, SVal L,
                        ExplodedNode<GRState>* Pred) {}
  
  virtual void EvalObjCMessageExpr(ExplodedNodeSet<GRState>& Dst,
                                   GRExprEngine& Engine,
                                   GRStmtNodeBuilder<GRState>& Builder,
                                   ObjCMessageExpr* ME,
                                   ExplodedNode<GRState>* Pred) {}
  
  // Stores.
  
  virtual void EvalBind(GRStmtNodeBuilderRef& B, SVal location, SVal val) {}
  
  // End-of-path and dead symbol notification.
  
  virtual void EvalEndPath(GRExprEngine& Engine,
                           GREndPathNodeBuilder<GRState>& Builder) {}
  
  
  virtual void EvalDeadSymbols(ExplodedNodeSet<GRState>& Dst,
                               GRExprEngine& Engine,
                               GRStmtNodeBuilder<GRState>& Builder,
                               ExplodedNode<GRState>* Pred,
                               Stmt* S, const GRState* state,
                               SymbolReaper& SymReaper) {}
  
  // Return statements.  
  virtual void EvalReturn(ExplodedNodeSet<GRState>& Dst,
                          GRExprEngine& Engine,
                          GRStmtNodeBuilder<GRState>& Builder,
                          ReturnStmt* S,
                          ExplodedNode<GRState>* Pred) {}

  // Assumptions.  
  virtual const GRState* EvalAssume(const GRState *state,
                                    SVal Cond, bool Assumption) {
    return state;
  }
};
  
} // end clang namespace

#endif
