//== GRTransferFuncs.cpp - Path-Sens. Transfer Functions Interface -*- C++ -*--=
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

#include "clang/Analysis/PathSensitive/GRTransferFuncs.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"

using namespace clang;

void GRTransferFuncs::RegisterChecks(GRExprEngine& Eng) {}

void GRTransferFuncs::EvalStore(ExplodedNodeSet<GRState>& Dst,
                                GRExprEngine& Eng,
                                GRStmtNodeBuilder<GRState>& Builder,
                                Expr* E, ExplodedNode<GRState>* Pred,
                                const GRState* St, SVal TargetLV, SVal Val) {
  
  // This code basically matches the "safety-net" logic of GRExprEngine:
  //  bind Val to TargetLV, and create a new node.  We replicate it here
  //  because subclasses of GRTransferFuncs may wish to call it.

  assert (!TargetLV.isUndef());
  
  if (TargetLV.isUnknown())
    Builder.MakeNode(Dst, E, Pred, St);
  else
    Builder.MakeNode(Dst, E, Pred,
                   Eng.getStateManager().BindLoc(St, cast<Loc>(TargetLV), Val));
}

void GRTransferFuncs::EvalBinOpNN(GRStateSet& OStates,
                                  GRExprEngine& Eng,
                                  const GRState *St, Expr* Ex,
                                  BinaryOperator::Opcode Op,
                                  NonLoc L, NonLoc R) {
  
  OStates.Add(Eng.getStateManager().BindExpr(St, Ex, DetermEvalBinOpNN(Eng, Op, L, R)));
}
