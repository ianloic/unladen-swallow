// -*- C++ -*-
#ifndef UTIL_DEADGLOBALELIM_H
#define UTIL_DEADGLOBALELIM_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Pass.h"

// Garbage-collects dead LLVM GlobalValues.  Given a ModuleProvider,
// does the right thing when confronted with a GlobalValue backed by
// bitcode.
llvm::ModulePass *PyCreateDeadGlobalElimPass(
    const llvm::DenseSet<llvm::AssertingVH<const llvm::GlobalValue> > *
    bitcode_gvs);

#endif  // UTIL_DEADGLOBALELIM_H
