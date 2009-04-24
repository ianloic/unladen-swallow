//===---  BugReporter.h - Generate PathDiagnostics --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BugReporter, a utility class for generating
//  PathDiagnostics for analyses based on GRState.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_BUGREPORTER
#define LLVM_CLANG_ANALYSIS_BUGREPORTER

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Analysis/PathSensitive/GRState.h"
#include "clang/Analysis/PathSensitive/ExplodedGraph.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include <list>

namespace clang {
  
class PathDiagnostic;
class PathDiagnosticPiece;
class PathDiagnosticClient;
class ASTContext;
class Diagnostic;
class BugReporter;
class GRExprEngine;
class GRState;
class Stmt;
class BugReport;
class ParentMap;
  
class BugType {
public:
  BugType() {}
  virtual ~BugType();
  
  virtual const char* getName() const = 0;
  virtual const char* getDescription() const { return getName(); }
  virtual const char* getCategory() const { return ""; }
  
  virtual std::pair<const char**,const char**> getExtraDescriptiveText() {
    return std::pair<const char**, const char**>(0, 0);
  }
      
  virtual void EmitWarnings(BugReporter& BR) {}
  virtual void GetErrorNodes(std::vector<ExplodedNode<GRState>*>& Nodes) {}
  
  virtual bool isCached(BugReport& R) = 0;
};
  
class BugTypeCacheLocation : public BugType {
  llvm::SmallSet<ProgramPoint,10> CachedErrors;
public:
  BugTypeCacheLocation() {}
  virtual ~BugTypeCacheLocation() {}  
  virtual bool isCached(BugReport& R);
  bool isCached(ProgramPoint P);
};
  
  
class BugReport {
  BugType& Desc;
  const ExplodedNode<GRState> *EndNode;
  SourceRange R;  
public:
  BugReport(BugType& D, const ExplodedNode<GRState> *n) : Desc(D), EndNode(n) {}
  virtual ~BugReport();
  
  const BugType& getBugType() const { return Desc; }
  BugType& getBugType() { return Desc; }
  
  const ExplodedNode<GRState>* getEndNode() const { return EndNode; }
  
  Stmt* getStmt(BugReporter& BR) const;
    
  const char* getName() const { return getBugType().getName(); }

  virtual const char* getDescription() const {
    return getBugType().getDescription();
  }
  
  virtual const char* getCategory() const {
    return getBugType().getCategory();
  }
  
  virtual std::pair<const char**,const char**> getExtraDescriptiveText() {
    return getBugType().getExtraDescriptiveText();
  }
  
  virtual PathDiagnosticPiece* getEndPath(BugReporter& BR,
                                          const ExplodedNode<GRState>* N);
  
  virtual FullSourceLoc getLocation(SourceManager& Mgr);
  
  virtual void getRanges(BugReporter& BR,const SourceRange*& beg,
                         const SourceRange*& end);
  
  virtual PathDiagnosticPiece* VisitNode(const ExplodedNode<GRState>* N,
                                         const ExplodedNode<GRState>* PrevN,
                                         const ExplodedGraph<GRState>& G,
                                         BugReporter& BR);
};
  
class RangedBugReport : public BugReport {
  std::vector<SourceRange> Ranges;
  const char* desc;
public:
  RangedBugReport(BugType& D, ExplodedNode<GRState> *n,
                  const char* description = 0)
    : BugReport(D, n), desc(description) {}
  
  virtual ~RangedBugReport();

  virtual const char* getDescription() const {
    return desc ? desc : BugReport::getDescription();
  }
  
  void addRange(SourceRange R) { Ranges.push_back(R); }
  
  
  virtual void getRanges(BugReporter& BR,const SourceRange*& beg,           
                         const SourceRange*& end) {
    
    if (Ranges.empty()) {
      beg = NULL;
      end = NULL;
    }
    else {
      beg = &Ranges[0];
      end = beg + Ranges.size();
    }
  }
};
  
class BugReporterData {
public:
  virtual ~BugReporterData();
  virtual Diagnostic& getDiagnostic() = 0;  
  virtual PathDiagnosticClient* getPathDiagnosticClient() = 0;  
  virtual ASTContext& getContext() = 0;
  virtual SourceManager& getSourceManager() = 0;
  virtual CFG* getCFG() = 0;
  virtual ParentMap& getParentMap() = 0;
  virtual LiveVariables* getLiveVariables() = 0;
};
  
class BugReporter {
public:
  enum Kind { BaseBRKind, GRBugReporterKind };

protected:
  Kind kind;  
  BugReporterData& D;
  
  BugReporter(BugReporterData& d, Kind k) : kind(k), D(d) {}
  
public:
  BugReporter(BugReporterData& d) : kind(BaseBRKind), D(d) {}
  virtual ~BugReporter();
  
  Kind getKind() const { return kind; }
  
  Diagnostic& getDiagnostic() {
    return D.getDiagnostic();
  }
  
  PathDiagnosticClient* getPathDiagnosticClient() {
    return D.getPathDiagnosticClient();
  }
  
  ASTContext& getContext() {
    return D.getContext();
  }
  
  SourceManager& getSourceManager() {
    return D.getSourceManager();
  }
  
  CFG* getCFG() {
    return D.getCFG();
  }
  
  ParentMap& getParentMap() {
    return D.getParentMap();  
  }
  
  LiveVariables* getLiveVariables() {
    return D.getLiveVariables();
  }
  
  virtual void GeneratePathDiagnostic(PathDiagnostic& PD, BugReport& R) {}

  void EmitWarning(BugReport& R);
  
  void EmitBasicReport(const char* BugName, const char* BugStr,
                       SourceLocation Loc,
                       SourceRange* RangeBeg, unsigned NumRanges);

  void EmitBasicReport(const char* BugName, const char* BugCategory,
                       const char* BugStr, SourceLocation Loc,
                       SourceRange* RangeBeg, unsigned NumRanges);
  
  
  void EmitBasicReport(const char* BugName, const char* BugStr,
                       SourceLocation Loc) {
    EmitBasicReport(BugName, BugStr, Loc, 0, 0);
  }
  
  void EmitBasicReport(const char* BugName, const char* BugCategory,
                       const char* BugStr, SourceLocation Loc) {
    EmitBasicReport(BugName, BugCategory, BugStr, Loc, 0, 0);
  }
  
  void EmitBasicReport(const char* BugName, const char* BugStr,
                       SourceLocation Loc, SourceRange R) {
    EmitBasicReport(BugName, BugStr, Loc, &R, 1);
  }
  
  void EmitBasicReport(const char* BugName, const char* Category,
                       const char* BugStr, SourceLocation Loc, SourceRange R) {
    EmitBasicReport(BugName, Category, BugStr, Loc, &R, 1);
  }
  
  static bool classof(const BugReporter* R) { return true; }
};
  
class GRBugReporter : public BugReporter {
  GRExprEngine& Eng;
  llvm::SmallSet<SymbolRef, 10> NotableSymbols;
public:
  
  GRBugReporter(BugReporterData& d, GRExprEngine& eng)
    : BugReporter(d, GRBugReporterKind), Eng(eng) {}
  
  virtual ~GRBugReporter();
  
  /// getEngine - Return the analysis engine used to analyze a given
  ///  function or method.
  GRExprEngine& getEngine() {
    return Eng;
  }

  /// getGraph - Get the exploded graph created by the analysis engine
  ///  for the analyzed method or function.
  ExplodedGraph<GRState>& getGraph();
  
  /// getStateManager - Return the state manager used by the analysis
  ///  engine.
  GRStateManager& getStateManager();
  
  virtual void GeneratePathDiagnostic(PathDiagnostic& PD, BugReport& R);

  void addNotableSymbol(SymbolRef Sym) {
    NotableSymbols.insert(Sym);
  }
  
  bool isNotable(SymbolRef Sym) const {
    return (bool) NotableSymbols.count(Sym);
  }
    
  /// classof - Used by isa<>, cast<>, and dyn_cast<>.
  static bool classof(const BugReporter* R) {
    return R->getKind() == GRBugReporterKind;
  }
};
  

class DiagBugReport : public RangedBugReport {
  std::list<std::string> Strs;
  FullSourceLoc L;
  const char* description;
public:
  DiagBugReport(const char* desc, BugType& D, FullSourceLoc l) :
  RangedBugReport(D, NULL), L(l), description(desc) {}
  
  virtual ~DiagBugReport() {}
  virtual FullSourceLoc getLocation(SourceManager&) { return L; }
  
  virtual const char* getDescription() const {
    return description;
  }
  
  void addString(const std::string& s) { Strs.push_back(s); }  
  
  typedef std::list<std::string>::const_iterator str_iterator;
  str_iterator str_begin() const { return Strs.begin(); }
  str_iterator str_end() const { return Strs.end(); }
};

class DiagCollector : public DiagnosticClient {
  std::list<DiagBugReport> Reports;
  BugType& D;
public:
  DiagCollector(BugType& d) : D(d) {}
  
  virtual ~DiagCollector() {}
  
  bool IncludeInDiagnosticCounts() const { return false; }
  
  void HandleDiagnostic(Diagnostic::Level DiagLevel,
                        const DiagnosticInfo &Info);
  
  // Iterators.
  typedef std::list<DiagBugReport>::iterator iterator;
  iterator begin() { return Reports.begin(); }
  iterator end() { return Reports.end(); }
};
  
class SimpleBugType : public BugTypeCacheLocation {
  const char* name;
  const char* category;
  const char* desc;
public:
  SimpleBugType(const char* n) : name(n), category(""), desc(0) {}
  SimpleBugType(const char* n, const char* c, const char* d)
    : name(n), category(c), desc(d) {}
  
  virtual const char* getName() const { return name; }
  virtual const char* getDescription() const { return desc ? desc : name; }
  virtual const char* getCategory() const { return category; }
};
  
} // end clang namespace

#endif
