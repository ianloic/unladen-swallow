//== ConstraintManager.h - Constraints on symbolic values.-------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the interface to manage constraints on symbolic values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_CONSTRAINT_MANAGER_H
#define LLVM_CLANG_ANALYSIS_CONSTRAINT_MANAGER_H

// FIXME: Typedef LiveSymbolsTy/DeadSymbolsTy at a more appropriate place.
#include "clang/Analysis/PathSensitive/Store.h"

namespace llvm {
class APSInt;
}

namespace clang {

class GRState;
class GRStateManager;
class SVal;
class SymbolRef;

class ConstraintManager {
public:
  virtual ~ConstraintManager();
  virtual const GRState* Assume(const GRState* St, SVal Cond, 
                                bool Assumption, bool& isFeasible) = 0;

  virtual const GRState* AssumeInBound(const GRState* St, SVal Idx, 
                                       SVal UpperBound, bool Assumption,
                                       bool& isFeasible) = 0;

  virtual const llvm::APSInt* getSymVal(const GRState* St, SymbolRef sym) = 0;

  virtual bool isEqual(const GRState* St, SymbolRef sym, 
                       const llvm::APSInt& V) const = 0;

  virtual const GRState* RemoveDeadBindings(const GRState* St,
                                            SymbolReaper& SymReaper) = 0;

  virtual void print(const GRState* St, std::ostream& Out, 
                     const char* nl, const char *sep) = 0;

  virtual void EndPath(const GRState* St) {}
};

ConstraintManager* CreateBasicConstraintManager(GRStateManager& statemgr);

} // end clang namespace

#endif