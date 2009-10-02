// -*- C++ -*-
#ifndef UTIL_ALIASANALYSIS_H
#define UTIL_ALIASANALYSIS_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/Pass.h"

// Implements Python-specific aliasing rules.
llvm::Pass *CreatePyAliasAnalysis(struct PyGlobalLlvmData &global_data);

#endif  // UTIL_ALIASANALYSIS_H
