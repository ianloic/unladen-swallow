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

#include "clang/Frontend/AnalysisConsumer.h"
#include "clang/Frontend/PathDiagnosticClients.h"
#include "clang/Frontend/ManagerRegistry.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "llvm/Support/Compiler.h"
#include "llvm/ADT/OwningPtr.h"
#include "clang/AST/CFG.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/AST/ParentMap.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/PathSensitive/GRTransferFuncs.h"
#include "clang/Analysis/PathSensitive/GRExprEngine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Path.h"
#include "llvm/System/Program.h"

using namespace clang;

static ExplodedNodeImpl::Auditor* CreateUbiViz();

//===----------------------------------------------------------------------===//
// Basic type definitions.
//===----------------------------------------------------------------------===//

namespace {  
  class AnalysisManager;
  typedef void (*CodeAction)(AnalysisManager& Mgr);
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// AnalysisConsumer declaration.
//===----------------------------------------------------------------------===//

namespace {

  class VISIBILITY_HIDDEN AnalysisConsumer : public ASTConsumer {
    typedef std::vector<CodeAction> Actions;
    Actions FunctionActions;
    Actions ObjCMethodActions;
    Actions ObjCImplementationActions;
    Actions TranslationUnitActions;
    
  public:
    const LangOptions& LOpts;    
    Diagnostic &Diags;
    ASTContext* Ctx;
    Preprocessor* PP;
    PreprocessorFactory* PPF;
    const std::string OutDir;
    AnalyzerOptions Opts;
    llvm::OwningPtr<PathDiagnosticClient> PD;

    AnalysisConsumer(Diagnostic &diags, Preprocessor* pp,
                     PreprocessorFactory* ppf,
                     const LangOptions& lopts,
                     const std::string& outdir,
                     const AnalyzerOptions& opts)
      : LOpts(lopts), Diags(diags),
        Ctx(0), PP(pp), PPF(ppf),
        OutDir(outdir), Opts(opts) {}
    
    void addCodeAction(CodeAction action) {
      FunctionActions.push_back(action);
      ObjCMethodActions.push_back(action);
    }
    
    void addObjCImplementationAction(CodeAction action) {
      ObjCImplementationActions.push_back(action);
    }
    
    void addTranslationUnitAction(CodeAction action) {
      TranslationUnitActions.push_back(action);
    }
    
    virtual void Initialize(ASTContext &Context) {
      Ctx = &Context;
    }
    
    virtual void HandleTopLevelDecl(DeclGroupRef D) {
      for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I)
        HandleTopLevelSingleDecl(*I);
    }
    
    void HandleTopLevelSingleDecl(Decl *D);
    virtual void HandleTranslationUnit(ASTContext &C);
    
    void HandleCode(Decl* D, Stmt* Body, Actions& actions);
  };
    
  
  class VISIBILITY_HIDDEN AnalysisManager : public BugReporterData {
    Decl* D; Stmt* Body; 
    
    enum AnalysisScope { ScopeTU, ScopeDecl } AScope;
      
    AnalysisConsumer& C;
    bool DisplayedFunction;
    
    llvm::OwningPtr<CFG> cfg;
    llvm::OwningPtr<LiveVariables> liveness;
    llvm::OwningPtr<ParentMap> PM;

    // Configurable components creators.
    StoreManagerCreator CreateStoreMgr;
    ConstraintManagerCreator CreateConstraintMgr;

  public:
    AnalysisManager(AnalysisConsumer& c, Decl* d, Stmt* b, bool displayProgress) 
      : D(d), Body(b), AScope(ScopeDecl), C(c), 
        DisplayedFunction(!displayProgress) {
      setManagerCreators();
    }
    
    AnalysisManager(AnalysisConsumer& c, bool displayProgress) 
      : D(0), Body(0), AScope(ScopeTU), C(c),
        DisplayedFunction(!displayProgress) {
      setManagerCreators();
    }
    
    Decl* getCodeDecl() const { 
      assert (AScope == ScopeDecl);
      return D;
    }
    
    Stmt* getBody() const {
      assert (AScope == ScopeDecl);
      return Body;
    }
    
    StoreManagerCreator getStoreManagerCreator() {
      return CreateStoreMgr;
    };

    ConstraintManagerCreator getConstraintManagerCreator() {
      return CreateConstraintMgr;
    }
    
    virtual CFG* getCFG() {
      if (!cfg) cfg.reset(CFG::buildCFG(getBody()));
      return cfg.get();
    }
    
    virtual ParentMap& getParentMap() {
      if (!PM) 
        PM.reset(new ParentMap(getBody()));
      return *PM.get();
    }
    
    virtual ASTContext& getContext() {
      return *C.Ctx;
    }
    
    virtual SourceManager& getSourceManager() {
      return getContext().getSourceManager();
    }
    
    virtual Diagnostic& getDiagnostic() {
      return C.Diags;
    }
    
    const LangOptions& getLangOptions() const {
      return C.LOpts;
    }
    
    virtual PathDiagnosticClient* getPathDiagnosticClient() {
      if (C.PD.get() == 0 && !C.OutDir.empty()) {
        switch (C.Opts.AnalysisDiagOpt) {
          default:
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN, AUTOCREATE)\
case PD_##NAME: C.PD.reset(CREATEFN(C.OutDir, C.PP, C.PPF)); break;
#include "clang/Frontend/Analyses.def"
        }
      }
      return C.PD.get();      
    }
      
    virtual LiveVariables* getLiveVariables() {
      if (!liveness) {
        CFG* c = getCFG();
        if (!c) return 0;
        
        liveness.reset(new LiveVariables(getContext(), *c));
        liveness->runOnCFG(*c);
        liveness->runOnAllBlocks(*c, 0, true);
      }
      
      return liveness.get();
    }
    
    bool shouldVisualizeGraphviz() const { return C.Opts.VisualizeEGDot; }

    bool shouldVisualizeUbigraph() const { return C.Opts.VisualizeEGUbi; }

    bool shouldVisualize() const {
      return C.Opts.VisualizeEGDot || C.Opts.VisualizeEGUbi;
    }

    bool shouldTrimGraph() const { return C.Opts.TrimGraph; }

    bool shouldPurgeDead() const { return C.Opts.PurgeDead; }

    bool shouldEagerlyAssume() const { return C.Opts.EagerlyAssume; }

    void DisplayFunction() {
      
      if (DisplayedFunction)
        return;
      
      DisplayedFunction = true;
      
      // FIXME: Is getCodeDecl() always a named decl?
      if (isa<FunctionDecl>(getCodeDecl()) ||
          isa<ObjCMethodDecl>(getCodeDecl())) {
        NamedDecl *ND = cast<NamedDecl>(getCodeDecl());
        SourceManager &SM = getContext().getSourceManager();
        llvm::cerr << "ANALYZE: "
          << SM.getPresumedLoc(ND->getLocation()).getFilename()
          << ' ' << ND->getNameAsString() << '\n';
      }
    }

  private:
    /// Set configurable analyzer components creators. First check if there are
    /// components registered at runtime. Otherwise fall back to builtin
    /// components.
    void setManagerCreators() {
      if (ManagerRegistry::StoreMgrCreator != 0) {
        CreateStoreMgr = ManagerRegistry::StoreMgrCreator;
      }
      else {
        switch (C.Opts.AnalysisStoreOpt) {
        default:
          assert(0 && "Unknown store manager.");
#define ANALYSIS_STORE(NAME, CMDFLAG, DESC, CREATEFN)     \
          case NAME##Model: CreateStoreMgr = CREATEFN; break;
#include "clang/Frontend/Analyses.def"
        }
      }

      if (ManagerRegistry::ConstraintMgrCreator != 0)
        CreateConstraintMgr = ManagerRegistry::ConstraintMgrCreator;
      else {
        switch (C.Opts.AnalysisConstraintsOpt) {
        default:
          assert(0 && "Unknown store manager.");
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATEFN)     \
          case NAME##Model: CreateConstraintMgr = CREATEFN; break;
#include "clang/Frontend/Analyses.def"
        }
      }

      
      // Some DiagnosticClients should be created all the time instead of
      // lazily.  Create those now.
      switch (C.Opts.AnalysisDiagOpt) {
        default: break;
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN, AUTOCREATE)\
case PD_##NAME: if (AUTOCREATE) getPathDiagnosticClient(); break;
#include "clang/Frontend/Analyses.def"
      }      
    }

  };

} // end anonymous namespace

namespace llvm {
  template <> struct FoldingSetTrait<CodeAction> {
    static inline void Profile(CodeAction X, FoldingSetNodeID& ID) {
      ID.AddPointer(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(X)));
    }
  };   
}

//===----------------------------------------------------------------------===//
// AnalysisConsumer implementation.
//===----------------------------------------------------------------------===//

void AnalysisConsumer::HandleTopLevelSingleDecl(Decl *D) { 
  switch (D->getKind()) {
    case Decl::Function: {
      FunctionDecl* FD = cast<FunctionDecl>(D);

      if (Opts.AnalyzeSpecificFunction.size() > 0 && 
          Opts.AnalyzeSpecificFunction != FD->getIdentifier()->getName())
        break;
      
      Stmt* Body = FD->getBody();
      if (Body) HandleCode(FD, Body, FunctionActions);
      break;
    }
      
    case Decl::ObjCMethod: {
      ObjCMethodDecl* MD = cast<ObjCMethodDecl>(D);
      
      if (Opts.AnalyzeSpecificFunction.size() > 0 &&
          Opts.AnalyzeSpecificFunction != MD->getSelector().getAsString())
        return;
      
      Stmt* Body = MD->getBody();
      if (Body) HandleCode(MD, Body, ObjCMethodActions);
      break;
    }
      
    default:
      break;
  }
}

void AnalysisConsumer::HandleTranslationUnit(ASTContext &C) {

  if(!TranslationUnitActions.empty()) {
    AnalysisManager mgr(*this, Opts.AnalyzerDisplayProgress);
    for (Actions::iterator I = TranslationUnitActions.begin(), 
         E = TranslationUnitActions.end(); I != E; ++I)
      (*I)(mgr);  
  }

  if (!ObjCImplementationActions.empty()) {
    TranslationUnitDecl *TUD = C.getTranslationUnitDecl();
    
    for (DeclContext::decl_iterator I = TUD->decls_begin(),
                                    E = TUD->decls_end();
         I != E; ++I)
      if (ObjCImplementationDecl* ID = dyn_cast<ObjCImplementationDecl>(*I))
        HandleCode(ID, 0, ObjCImplementationActions);
  }
  
  // Delete the PathDiagnosticClient here just in case the AnalysisConsumer
  // object doesn't get released.  This will cause any side-effects in the
  // destructor of the PathDiagnosticClient to get executed.
  PD.reset();
}

void AnalysisConsumer::HandleCode(Decl* D, Stmt* Body, Actions& actions) {
  
  // Don't run the actions if an error has occured with parsing the file.
  if (Diags.hasErrorOccurred())
    return;

  // Don't run the actions on declarations in header files unless
  // otherwise specified.
  if (!Opts.AnalyzeAll &&
      !Ctx->getSourceManager().isFromMainFile(D->getLocation()))
    return;  

  // Create an AnalysisManager that will manage the state for analyzing
  // this method/function.
  AnalysisManager mgr(*this, D, Body, Opts.AnalyzerDisplayProgress);
  
  // Dispatch on the actions.  
  for (Actions::iterator I = actions.begin(), E = actions.end(); I != E; ++I)
    (*I)(mgr);  
}

//===----------------------------------------------------------------------===//
// Analyses
//===----------------------------------------------------------------------===//

static void ActionWarnDeadStores(AnalysisManager& mgr) {
  if (LiveVariables* L = mgr.getLiveVariables()) {
    BugReporter BR(mgr);
    CheckDeadStores(*L, BR);
  }
}

static void ActionWarnUninitVals(AnalysisManager& mgr) {
  if (CFG* c = mgr.getCFG())
    CheckUninitializedValues(*c, mgr.getContext(), mgr.getDiagnostic());
}


static void ActionGRExprEngine(AnalysisManager& mgr, GRTransferFuncs* tf,
                               bool StandardWarnings = true) {
  
  
  llvm::OwningPtr<GRTransferFuncs> TF(tf);

  // Display progress.
  mgr.DisplayFunction();

  // Construct the analysis engine.
  LiveVariables* L = mgr.getLiveVariables();
  if (!L) return;

  GRExprEngine Eng(*mgr.getCFG(), *mgr.getCodeDecl(), mgr.getContext(), *L, mgr,
                   mgr.shouldPurgeDead(), mgr.shouldEagerlyAssume(),
                   mgr.getStoreManagerCreator(), 
                   mgr.getConstraintManagerCreator());

  Eng.setTransferFunctions(tf);
  
  if (StandardWarnings) {
    Eng.RegisterInternalChecks();
    RegisterAppleChecks(Eng);
  }

  // Set the graph auditor.
  llvm::OwningPtr<ExplodedNodeImpl::Auditor> Auditor;
  if (mgr.shouldVisualizeUbigraph()) {
    Auditor.reset(CreateUbiViz());
    ExplodedNodeImpl::SetAuditor(Auditor.get());
  }
  
  // Execute the worklist algorithm.
  Eng.ExecuteWorkList();
  
  // Release the auditor (if any) so that it doesn't monitor the graph
  // created BugReporter.
  ExplodedNodeImpl::SetAuditor(0);

  // Visualize the exploded graph.
  if (mgr.shouldVisualizeGraphviz())
    Eng.ViewGraph(mgr.shouldTrimGraph());
  
  // Display warnings.
  Eng.getBugReporter().FlushReports();
}

static void ActionCheckerCFRefAux(AnalysisManager& mgr, bool GCEnabled,
                                  bool StandardWarnings) {
  
  GRTransferFuncs* TF = MakeCFRefCountTF(mgr.getContext(),
                                         GCEnabled,
                                         mgr.getLangOptions());
    
  ActionGRExprEngine(mgr, TF, StandardWarnings);
}

static void ActionCheckerCFRef(AnalysisManager& mgr) {
     
 switch (mgr.getLangOptions().getGCMode()) {
   default:
     assert (false && "Invalid GC mode.");
   case LangOptions::NonGC:
     ActionCheckerCFRefAux(mgr, false, true);
     break;
    
   case LangOptions::GCOnly:
     ActionCheckerCFRefAux(mgr, true, true);
     break;
     
   case LangOptions::HybridGC:
     ActionCheckerCFRefAux(mgr, false, true);
     ActionCheckerCFRefAux(mgr, true, false);
     break;
 }
}

static void ActionDisplayLiveVariables(AnalysisManager& mgr) {
  if (LiveVariables* L = mgr.getLiveVariables()) {
    mgr.DisplayFunction();  
    L->dumpBlockLiveness(mgr.getSourceManager());
  }
}

static void ActionCFGDump(AnalysisManager& mgr) {
  if (CFG* c = mgr.getCFG()) {
    mgr.DisplayFunction();
    LangOptions LO;  // FIXME!
    c->dump(LO);
  }
}

static void ActionCFGView(AnalysisManager& mgr) {
  if (CFG* c = mgr.getCFG()) {
    mgr.DisplayFunction();
    LangOptions LO; // FIXME!
    c->viewCFG(LO);
  }
}

static void ActionWarnObjCDealloc(AnalysisManager& mgr) {
  if (mgr.getLangOptions().getGCMode() == LangOptions::GCOnly)
    return;
      
  BugReporter BR(mgr);
  
  CheckObjCDealloc(cast<ObjCImplementationDecl>(mgr.getCodeDecl()), 
                   mgr.getLangOptions(), BR);  
}

static void ActionWarnObjCUnusedIvars(AnalysisManager& mgr) {
  BugReporter BR(mgr);
  CheckObjCUnusedIvar(cast<ObjCImplementationDecl>(mgr.getCodeDecl()), BR);  
}

static void ActionWarnObjCMethSigs(AnalysisManager& mgr) {
  BugReporter BR(mgr);
  
  CheckObjCInstMethSignature(cast<ObjCImplementationDecl>(mgr.getCodeDecl()),
                             BR);
}

//===----------------------------------------------------------------------===//
// AnalysisConsumer creation.
//===----------------------------------------------------------------------===//

ASTConsumer* clang::CreateAnalysisConsumer(Diagnostic &diags, Preprocessor* pp,
                                           PreprocessorFactory* ppf,
                                           const LangOptions& lopts,
                                           const std::string& OutDir,
                                           const AnalyzerOptions& Opts) {

  llvm::OwningPtr<AnalysisConsumer> C(new AnalysisConsumer(diags, pp, ppf,
                                                           lopts, OutDir,
                                                           Opts));

  for (unsigned i = 0; i < Opts.AnalysisList.size(); ++i)
    switch (Opts.AnalysisList[i]) {
#define ANALYSIS(NAME, CMD, DESC, SCOPE)\
      case NAME:\
        C->add ## SCOPE ## Action(&Action ## NAME);\
        break;
#include "clang/Frontend/Analyses.def"
      default: break;
    }
  
  // Last, disable the effects of '-Werror' when using the AnalysisConsumer.
  diags.setWarningsAsErrors(false);

  return C.take();
}

//===----------------------------------------------------------------------===//
// Ubigraph Visualization.  FIXME: Move to separate file.
//===----------------------------------------------------------------------===//

namespace {
  
class UbigraphViz : public ExplodedNodeImpl::Auditor {
  llvm::OwningPtr<llvm::raw_ostream> Out;
  llvm::sys::Path Dir, Filename;
  unsigned Cntr;

  typedef llvm::DenseMap<void*,unsigned> VMap;
  VMap M;
  
public:
  UbigraphViz(llvm::raw_ostream* out, llvm::sys::Path& dir,
              llvm::sys::Path& filename);
  
  ~UbigraphViz();
  
  virtual void AddEdge(ExplodedNodeImpl* Src, ExplodedNodeImpl* Dst);  
};
  
} // end anonymous namespace

static ExplodedNodeImpl::Auditor* CreateUbiViz() {
  std::string ErrMsg;
  
  llvm::sys::Path Dir = llvm::sys::Path::GetTemporaryDirectory(&ErrMsg);
  if (!ErrMsg.empty())
    return 0;

  llvm::sys::Path Filename = Dir;
  Filename.appendComponent("llvm_ubi");
  Filename.makeUnique(true,&ErrMsg);

  if (!ErrMsg.empty())
    return 0;

  llvm::cerr << "Writing '" << Filename << "'.\n";
  
  llvm::OwningPtr<llvm::raw_fd_ostream> Stream;
  std::string filename = Filename.toString();
  Stream.reset(new llvm::raw_fd_ostream(filename.c_str(), false, ErrMsg));

  if (!ErrMsg.empty())
    return 0;
  
  return new UbigraphViz(Stream.take(), Dir, Filename);
}

void UbigraphViz::AddEdge(ExplodedNodeImpl* Src, ExplodedNodeImpl* Dst) {
  
  assert (Src != Dst && "Self-edges are not allowed.");
  
  // Lookup the Src.  If it is a new node, it's a root.
  VMap::iterator SrcI= M.find(Src);
  unsigned SrcID;
  
  if (SrcI == M.end()) {
    M[Src] = SrcID = Cntr++;
    *Out << "('vertex', " << SrcID << ", ('color','#00ff00'))\n";
  }
  else
    SrcID = SrcI->second;
  
  // Lookup the Dst.
  VMap::iterator DstI= M.find(Dst);
  unsigned DstID;

  if (DstI == M.end()) {
    M[Dst] = DstID = Cntr++;
    *Out << "('vertex', " << DstID << ")\n";
  }
  else {
    // We have hit DstID before.  Change its style to reflect a cache hit.
    DstID = DstI->second;
    *Out << "('change_vertex_style', " << DstID << ", 1)\n";
  }

  // Add the edge.
  *Out << "('edge', " << SrcID << ", " << DstID 
       << ", ('arrow','true'), ('oriented', 'true'))\n";
}

UbigraphViz::UbigraphViz(llvm::raw_ostream* out, llvm::sys::Path& dir,
                         llvm::sys::Path& filename)
  : Out(out), Dir(dir), Filename(filename), Cntr(0) {

  *Out << "('vertex_style_attribute', 0, ('shape', 'icosahedron'))\n";
  *Out << "('vertex_style', 1, 0, ('shape', 'sphere'), ('color', '#ffcc66'),"
          " ('size', '1.5'))\n";
}

UbigraphViz::~UbigraphViz() {
  Out.reset(0);
  llvm::cerr << "Running 'ubiviz' program... ";
  std::string ErrMsg;
  llvm::sys::Path Ubiviz = llvm::sys::Program::FindProgramByName("ubiviz");
  std::vector<const char*> args;
  args.push_back(Ubiviz.c_str());
  args.push_back(Filename.c_str());
  args.push_back(0);
  
  if (llvm::sys::Program::ExecuteAndWait(Ubiviz, &args[0],0,0,0,0,&ErrMsg)) {
    llvm::cerr << "Error viewing graph: " << ErrMsg << "\n";
  }
  
  // Delete the directory.
  Dir.eraseFromDisk(true); 
}
