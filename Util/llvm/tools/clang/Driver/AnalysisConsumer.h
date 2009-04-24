//===--- AnalysisConsumer.cpp - ASTConsumer for running Analyses ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// "Meta" ASTConsumer for running different source analyses.
//
//===----------------------------------------------------------------------===//

#ifndef DRIVER_ANALYSISCONSUMER_H
#define DRIVER_ANALYSISCONSUMER_H

namespace clang {

enum Analyses {
#define ANALYSIS(NAME, CMDFLAG, DESC, SCOPE) NAME,
#include "Analyses.def"
NumAnalyses
};
  
enum AnalysisStores {
#define ANALYSIS_STORE(NAME, CMDFLAG, DESC) NAME##Model,
#include "Analyses.def"
NumStores
};
  
enum AnalysisDiagClients {
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN, AUTOCREAT) PD_##NAME,
#include "Analyses.def"
NUM_ANALYSIS_DIAG_CLIENTS
};

ASTConsumer* CreateAnalysisConsumer(Analyses* Beg, Analyses* End,
                                    AnalysisStores SM, AnalysisDiagClients DC,
                                    Diagnostic &diags, Preprocessor* pp,
                                    PreprocessorFactory* ppf,
                                    const LangOptions& lopts,
                                    const std::string& fname,
                                    const std::string& htmldir,
                                    bool VisualizeGraphViz,
                                    bool VisualizeUbi,
                                    bool VizTrimGraph,                                    
                                    bool AnalyzeAll,
                                    bool DisplayProgress);
} // end clang namespace

#endif
