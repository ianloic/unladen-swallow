// -*- C++ -*-
#ifndef UTIL_SINGLEFUNCTIONINLINER_H
#define UTIL_SINGLEFUNCTIONINLINER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Pass.h"

// Inlines calls into the active function according to Python-specific
// rules.  For now, this only inlines always-inline functions.
llvm::FunctionPass *PyCreateSingleFunctionInliningPass();

#endif  // UTIL_SINGLEFUNCTIONINLINER_H
