//==- GRCoreEngine.h - Path-Sensitive Dataflow Engine ------------------*- C++ -*-//
//             
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a generic engine for intraprocedural, path-sensitive,
//  dataflow analysis via graph reachability.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_GRENGINE
#define LLVM_CLANG_ANALYSIS_GRENGINE

#include "clang/AST/Expr.h"
#include "clang/Analysis/PathSensitive/ExplodedGraph.h"
#include "clang/Analysis/PathSensitive/GRWorkList.h"
#include "clang/Analysis/PathSensitive/GRBlockCounter.h"
#include "clang/Analysis/PathSensitive/GRAuditor.h"
#include "llvm/ADT/OwningPtr.h"

namespace clang {
  
class GRStmtNodeBuilderImpl;
class GRBranchNodeBuilderImpl;
class GRIndirectGotoNodeBuilderImpl;
class GRSwitchNodeBuilderImpl;
class GREndPathNodeBuilderImpl;
class GRWorkList;

//===----------------------------------------------------------------------===//
/// GRCoreEngineImpl - Implements the core logic of the graph-reachability 
///   analysis. It traverses the CFG and generates the ExplodedGraph.
///   Program "states" are treated as opaque void pointers.
///   The template class GRCoreEngine (which subclasses GRCoreEngineImpl)
///   provides the matching component to the engine that knows the actual types
///   for states.  Note that this engine only dispatches to transfer functions
///   at the statement and block-level.  The analyses themselves must implement
///   any transfer function logic and the sub-expression level (if any).
class GRCoreEngineImpl {
protected:
  friend class GRStmtNodeBuilderImpl;
  friend class GRBranchNodeBuilderImpl;
  friend class GRIndirectGotoNodeBuilderImpl;
  friend class GRSwitchNodeBuilderImpl;
  friend class GREndPathNodeBuilderImpl;
    
  /// G - The simulation graph.  Each node is a (location,state) pair.
  llvm::OwningPtr<ExplodedGraphImpl> G;
      
  /// WList - A set of queued nodes that need to be processed by the
  ///  worklist algorithm.  It is up to the implementation of WList to decide
  ///  the order that nodes are processed.
  GRWorkList* WList;
  
  /// BCounterFactory - A factory object for created GRBlockCounter objects.
  ///   These are used to record for key nodes in the ExplodedGraph the
  ///   number of times different CFGBlocks have been visited along a path.
  GRBlockCounter::Factory BCounterFactory;
  
  void GenerateNode(const ProgramPoint& Loc, const void* State,
                    ExplodedNodeImpl* Pred);
  
  /// getInitialState - Gets the void* representing the initial 'state'
  ///  of the analysis.  This is simply a wrapper (implemented
  ///  in GRCoreEngine) that performs type erasure on the initial
  ///  state returned by the checker object.
  virtual const void* getInitialState() = 0;
  
  void HandleBlockEdge(const BlockEdge& E, ExplodedNodeImpl* Pred);
  void HandleBlockEntrance(const BlockEntrance& E, ExplodedNodeImpl* Pred);
  void HandleBlockExit(CFGBlock* B, ExplodedNodeImpl* Pred);
  void HandlePostStmt(const PostStmt& S, CFGBlock* B,
                      unsigned StmtIdx, ExplodedNodeImpl *Pred);
  
  void HandleBranch(Stmt* Cond, Stmt* Term, CFGBlock* B,
                    ExplodedNodeImpl* Pred);  
  
  virtual void ProcessEndPath(GREndPathNodeBuilderImpl& Builder) = 0;  
  
  virtual bool ProcessBlockEntrance(CFGBlock* Blk, const void* State,
                                    GRBlockCounter BC) = 0;

  virtual void ProcessStmt(Stmt* S, GRStmtNodeBuilderImpl& Builder) = 0;

  virtual void ProcessBranch(Stmt* Condition, Stmt* Terminator,
                             GRBranchNodeBuilderImpl& Builder) = 0;

  virtual void ProcessIndirectGoto(GRIndirectGotoNodeBuilderImpl& Builder) = 0;
  
  virtual void ProcessSwitch(GRSwitchNodeBuilderImpl& Builder) = 0;

private:
  GRCoreEngineImpl(const GRCoreEngineImpl&); // Do not implement.
  GRCoreEngineImpl& operator=(const GRCoreEngineImpl&);
  
protected:  
  GRCoreEngineImpl(ExplodedGraphImpl* g, GRWorkList* wl)
    : G(g), WList(wl), BCounterFactory(g->getAllocator()) {}
  
public:
  /// ExecuteWorkList - Run the worklist algorithm for a maximum number of
  ///  steps.  Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(unsigned Steps);
  
  virtual ~GRCoreEngineImpl();
  
  CFG& getCFG() { return G->getCFG(); }
};
  
class GRStmtNodeBuilderImpl {
  GRCoreEngineImpl& Eng;
  CFGBlock& B;
  const unsigned Idx;
  ExplodedNodeImpl* Pred;
  ExplodedNodeImpl* LastNode;  
  
  typedef llvm::SmallPtrSet<ExplodedNodeImpl*,5> DeferredTy;
  DeferredTy Deferred;
  
  void GenerateAutoTransition(ExplodedNodeImpl* N);
  
public:
  GRStmtNodeBuilderImpl(CFGBlock* b, unsigned idx,
                    ExplodedNodeImpl* N, GRCoreEngineImpl* e);      
  
  ~GRStmtNodeBuilderImpl();
  
  ExplodedNodeImpl* getBasePredecessor() const { return Pred; }
  
  ExplodedNodeImpl* getLastNode() const {
    return LastNode ? (LastNode->isSink() ? NULL : LastNode) : NULL;
  }
  
  GRBlockCounter getBlockCounter() const { return Eng.WList->getBlockCounter();}
  
  unsigned getCurrentBlockCount() const {
    return getBlockCounter().getNumVisited(B.getBlockID());
  }  
    
  ExplodedNodeImpl*
  generateNodeImpl(Stmt* S, const void* State, ExplodedNodeImpl* Pred,
                   ProgramPoint::Kind K = ProgramPoint::PostStmtKind);

  ExplodedNodeImpl*
  generateNodeImpl(Stmt* S, const void* State,
                   ProgramPoint::Kind K = ProgramPoint::PostStmtKind) {    
    ExplodedNodeImpl* N = getLastNode();
    assert (N && "Predecessor of new node is infeasible.");
    return generateNodeImpl(S, State, N, K);
  }
  
  /// getStmt - Return the current block-level expression associated with
  ///  this builder.
  Stmt* getStmt() const { return B[Idx]; }
  
  /// getBlock - Return the CFGBlock associated with the block-level expression
  ///  of this builder.
  CFGBlock* getBlock() const { return &B; }
};
  
  
template<typename STATE>
class GRStmtNodeBuilder  {
public:
  typedef STATE                       StateTy;
  typedef typename StateTy::ManagerTy StateManagerTy;
  typedef ExplodedNode<StateTy>       NodeTy;
  
private:
  GRStmtNodeBuilderImpl& NB;
  StateManagerTy& Mgr;
  const StateTy* CleanedState;  
  GRAuditor<StateTy>* Auditor;
  
public:
  GRStmtNodeBuilder(GRStmtNodeBuilderImpl& nb, StateManagerTy& mgr) :
    NB(nb), Mgr(mgr), Auditor(0), PurgingDeadSymbols(false),
    BuildSinks(false), HasGeneratedNode(false),
    PointKind(ProgramPoint::PostStmtKind) {
      
    CleanedState = getLastNode()->getState();
  }

  void setAuditor(GRAuditor<StateTy>* A) {
    Auditor = A;
  }
    
  NodeTy* getLastNode() const {
    return static_cast<NodeTy*>(NB.getLastNode());
  }
  
  NodeTy* generateNode(Stmt* S, const StateTy* St, NodeTy* Pred,
                       ProgramPoint::Kind K) {
    HasGeneratedNode = true;
    if (PurgingDeadSymbols) K = ProgramPoint::PostPurgeDeadSymbolsKind;      
    return static_cast<NodeTy*>(NB.generateNodeImpl(S, St, Pred, K));
  }
  
  NodeTy* generateNode(Stmt* S, const StateTy* St, NodeTy* Pred) {
    return generateNode(S, St, Pred, PointKind);
  }
  
  NodeTy* generateNode(Stmt* S, const StateTy* St, ProgramPoint::Kind K) {
    HasGeneratedNode = true;
    if (PurgingDeadSymbols) K = ProgramPoint::PostPurgeDeadSymbolsKind;      
    return static_cast<NodeTy*>(NB.generateNodeImpl(S, St, K));
  }
  
  NodeTy* generateNode(Stmt* S, const StateTy* St) {
    return generateNode(S, St, PointKind);
  }

  
  GRBlockCounter getBlockCounter() const {
    return NB.getBlockCounter();
  }  
  
  unsigned getCurrentBlockCount() const {
    return NB.getCurrentBlockCount();
  }
  
  const StateTy* GetState(NodeTy* Pred) const {
    if ((ExplodedNodeImpl*) Pred == NB.getBasePredecessor())
      return CleanedState;
    else
      return Pred->getState();
  }
  
  void SetCleanedState(const StateTy* St) {
    CleanedState = St;
  }
  
  NodeTy* MakeNode(ExplodedNodeSet<StateTy>& Dst, Stmt* S,
                   NodeTy* Pred, const StateTy* St) {
    return MakeNode(Dst, S, Pred, St, PointKind);
  }
  
  NodeTy* MakeNode(ExplodedNodeSet<StateTy>& Dst, Stmt* S,
                   NodeTy* Pred, const StateTy* St, ProgramPoint::Kind K) {    
    
    const StateTy* PredState = GetState(Pred);
        
    // If the state hasn't changed, don't generate a new node.
    if (!BuildSinks && St == PredState && Auditor == 0) {
      Dst.Add(Pred);
      return NULL;
    }
    
    NodeTy* N = generateNode(S, St, Pred, K);
    
    if (N) {      
      if (BuildSinks)
        N->markAsSink();
      else {
        if (Auditor && Auditor->Audit(N, Mgr))
          N->markAsSink();
        
        Dst.Add(N);
      }
    }
    
    return N;
  }
  
  NodeTy* MakeSinkNode(ExplodedNodeSet<StateTy>& Dst, Stmt* S,
                       NodeTy* Pred, const StateTy* St) { 
    bool Tmp = BuildSinks;
    BuildSinks = true;
    NodeTy* N = MakeNode(Dst, S, Pred, St);
    BuildSinks = Tmp;
    return N;
  }
  
  bool PurgingDeadSymbols;
  bool BuildSinks;
  bool HasGeneratedNode;
  ProgramPoint::Kind PointKind;
};
  
class GRBranchNodeBuilderImpl {
  GRCoreEngineImpl& Eng;
  CFGBlock* Src;
  CFGBlock* DstT;
  CFGBlock* DstF;
  ExplodedNodeImpl* Pred;

  typedef llvm::SmallVector<ExplodedNodeImpl*,3> DeferredTy;
  DeferredTy Deferred;
  
  bool GeneratedTrue;
  bool GeneratedFalse;
  
public:
  GRBranchNodeBuilderImpl(CFGBlock* src, CFGBlock* dstT, CFGBlock* dstF,
                          ExplodedNodeImpl* pred, GRCoreEngineImpl* e) 
  : Eng(*e), Src(src), DstT(dstT), DstF(dstF), Pred(pred),
    GeneratedTrue(false), GeneratedFalse(false) {}
  
  ~GRBranchNodeBuilderImpl();
  
  ExplodedNodeImpl* getPredecessor() const { return Pred; }
  const ExplodedGraphImpl& getGraph() const { return *Eng.G; }
  GRBlockCounter getBlockCounter() const { return Eng.WList->getBlockCounter();}
    
  ExplodedNodeImpl* generateNodeImpl(const void* State, bool branch);
  
  CFGBlock* getTargetBlock(bool branch) const {
    return branch ? DstT : DstF;
  }    
  
  void markInfeasible(bool branch) {
    if (branch) GeneratedTrue = true;
    else GeneratedFalse = true;
  }
};

template<typename STATE>
class GRBranchNodeBuilder {
  typedef STATE                                  StateTy;
  typedef ExplodedGraph<StateTy>                 GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;
  
  GRBranchNodeBuilderImpl& NB;
  
public:
  GRBranchNodeBuilder(GRBranchNodeBuilderImpl& nb) : NB(nb) {}
  
  const GraphTy& getGraph() const {
    return static_cast<const GraphTy&>(NB.getGraph());
  }
  
  NodeTy* getPredecessor() const {
    return static_cast<NodeTy*>(NB.getPredecessor());
  }
  
  const StateTy* getState() const {
    return getPredecessor()->getState();
  }

  NodeTy* generateNode(const StateTy* St, bool branch) {
    return static_cast<NodeTy*>(NB.generateNodeImpl(St, branch));
  }
  
  GRBlockCounter getBlockCounter() const {
    return NB.getBlockCounter();
  }
  
  CFGBlock* getTargetBlock(bool branch) const {
    return NB.getTargetBlock(branch);
  }
  
  void markInfeasible(bool branch) {
    NB.markInfeasible(branch);
  }
};
  
class GRIndirectGotoNodeBuilderImpl {
  GRCoreEngineImpl& Eng;
  CFGBlock* Src;
  CFGBlock& DispatchBlock;
  Expr* E;
  ExplodedNodeImpl* Pred;  
public:
  GRIndirectGotoNodeBuilderImpl(ExplodedNodeImpl* pred, CFGBlock* src,
                                Expr* e, CFGBlock* dispatch,
                                GRCoreEngineImpl* eng)
  : Eng(*eng), Src(src), DispatchBlock(*dispatch), E(e), Pred(pred) {}
  

  class Iterator {
    CFGBlock::succ_iterator I;
    
    friend class GRIndirectGotoNodeBuilderImpl;    
    Iterator(CFGBlock::succ_iterator i) : I(i) {}    
  public:
    
    Iterator& operator++() { ++I; return *this; }
    bool operator!=(const Iterator& X) const { return I != X.I; }
    
    LabelStmt* getLabel() const {
      return llvm::cast<LabelStmt>((*I)->getLabel());
    }
    
    CFGBlock*  getBlock() const {
      return *I;
    }
  };
  
  Iterator begin() { return Iterator(DispatchBlock.succ_begin()); }
  Iterator end() { return Iterator(DispatchBlock.succ_end()); }
  
  ExplodedNodeImpl* generateNodeImpl(const Iterator& I, const void* State,
                                     bool isSink);
  
  Expr* getTarget() const { return E; }
  const void* getState() const { return Pred->State; }
};
  
template<typename STATE>
class GRIndirectGotoNodeBuilder {
  typedef STATE                                  StateTy;
  typedef ExplodedGraph<StateTy>                 GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;

  GRIndirectGotoNodeBuilderImpl& NB;

public:
  GRIndirectGotoNodeBuilder(GRIndirectGotoNodeBuilderImpl& nb) : NB(nb) {}
  
  typedef GRIndirectGotoNodeBuilderImpl::Iterator     iterator;

  iterator begin() { return NB.begin(); }
  iterator end() { return NB.end(); }
  
  Expr* getTarget() const { return NB.getTarget(); }
  
  NodeTy* generateNode(const iterator& I, const StateTy* St, bool isSink=false){    
    return static_cast<NodeTy*>(NB.generateNodeImpl(I, St, isSink));
  }
  
  const StateTy* getState() const {
    return static_cast<const StateTy*>(NB.getState());
  }    
};
  
class GRSwitchNodeBuilderImpl {
  GRCoreEngineImpl& Eng;
  CFGBlock* Src;
  Expr* Condition;
  ExplodedNodeImpl* Pred;  
public:
  GRSwitchNodeBuilderImpl(ExplodedNodeImpl* pred, CFGBlock* src,
                          Expr* condition, GRCoreEngineImpl* eng)
  : Eng(*eng), Src(src), Condition(condition), Pred(pred) {}
  
  class Iterator {
    CFGBlock::succ_reverse_iterator I;
    
    friend class GRSwitchNodeBuilderImpl;    
    Iterator(CFGBlock::succ_reverse_iterator i) : I(i) {}    
  public:
    
    Iterator& operator++() { ++I; return *this; }
    bool operator!=(const Iterator& X) const { return I != X.I; }
    
    CaseStmt* getCase() const {
      return llvm::cast<CaseStmt>((*I)->getLabel());
    }
    
    CFGBlock* getBlock() const {
      return *I;
    }
  };
  
  Iterator begin() { return Iterator(Src->succ_rbegin()+1); }
  Iterator end() { return Iterator(Src->succ_rend()); }
  
  ExplodedNodeImpl* generateCaseStmtNodeImpl(const Iterator& I,
                                             const void* State);
  
  ExplodedNodeImpl* generateDefaultCaseNodeImpl(const void* State,
                                                bool isSink);
  
  Expr* getCondition() const { return Condition; }
  const void* getState() const { return Pred->State; }
};

template<typename STATE>
class GRSwitchNodeBuilder {
  typedef STATE                                  StateTy;
  typedef ExplodedGraph<StateTy>                 GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;
  
  GRSwitchNodeBuilderImpl& NB;
  
public:
  GRSwitchNodeBuilder(GRSwitchNodeBuilderImpl& nb) : NB(nb) {}
  
  typedef GRSwitchNodeBuilderImpl::Iterator     iterator;
  
  iterator begin() { return NB.begin(); }
  iterator end() { return NB.end(); }
  
  Expr* getCondition() const { return NB.getCondition(); }
  
  NodeTy* generateCaseStmtNode(const iterator& I, const StateTy* St) {
    return static_cast<NodeTy*>(NB.generateCaseStmtNodeImpl(I, St));
  }
  
  NodeTy* generateDefaultCaseNode(const StateTy* St, bool isSink = false) {    
    return static_cast<NodeTy*>(NB.generateDefaultCaseNodeImpl(St, isSink));
  }
  
  const StateTy* getState() const {
    return static_cast<const StateTy*>(NB.getState());
  }    
};
  

class GREndPathNodeBuilderImpl {
  GRCoreEngineImpl& Eng;
  CFGBlock& B;
  ExplodedNodeImpl* Pred;  
  bool HasGeneratedNode;
  
public:
  GREndPathNodeBuilderImpl(CFGBlock* b, ExplodedNodeImpl* N,
                           GRCoreEngineImpl* e)
    : Eng(*e), B(*b), Pred(N), HasGeneratedNode(false) {}      
  
  ~GREndPathNodeBuilderImpl();
  
  ExplodedNodeImpl* getPredecessor() const { return Pred; }
    
  GRBlockCounter getBlockCounter() const { return Eng.WList->getBlockCounter();}
  
  unsigned getCurrentBlockCount() const {
    return getBlockCounter().getNumVisited(B.getBlockID());
  }  
  
  ExplodedNodeImpl* generateNodeImpl(const void* State);
    
  CFGBlock* getBlock() const { return &B; }
};


template<typename STATE>
class GREndPathNodeBuilder  {
  typedef STATE                   StateTy;
  typedef ExplodedNode<StateTy>   NodeTy;
  
  GREndPathNodeBuilderImpl& NB;
  
public:
  GREndPathNodeBuilder(GREndPathNodeBuilderImpl& nb) : NB(nb) {}
  
  NodeTy* getPredecessor() const {
    return static_cast<NodeTy*>(NB.getPredecessor());
  }
  
  GRBlockCounter getBlockCounter() const {
    return NB.getBlockCounter();
  }  
  
  unsigned getCurrentBlockCount() const {
    return NB.getCurrentBlockCount();
  }
  
  const StateTy* getState() const {
    return getPredecessor()->getState();
  }
  
  NodeTy* MakeNode(const StateTy* St) {  
    return static_cast<NodeTy*>(NB.generateNodeImpl(St));
  }
};

  
template<typename SUBENGINE>
class GRCoreEngine : public GRCoreEngineImpl {
public:
  typedef SUBENGINE                              SubEngineTy; 
  typedef typename SubEngineTy::StateTy          StateTy;
  typedef typename StateTy::ManagerTy            StateManagerTy;
  typedef ExplodedGraph<StateTy>                 GraphTy;
  typedef typename GraphTy::NodeTy               NodeTy;

protected:
  SubEngineTy& SubEngine;
  
  virtual const void* getInitialState() {
    return SubEngine.getInitialState();
  }
  
  virtual void ProcessEndPath(GREndPathNodeBuilderImpl& BuilderImpl) {
    GREndPathNodeBuilder<StateTy> Builder(BuilderImpl);
    SubEngine.ProcessEndPath(Builder);
  }
  
  virtual void ProcessStmt(Stmt* S, GRStmtNodeBuilderImpl& BuilderImpl) {
    GRStmtNodeBuilder<StateTy> Builder(BuilderImpl,SubEngine.getStateManager());
    SubEngine.ProcessStmt(S, Builder);
  }
  
  virtual bool ProcessBlockEntrance(CFGBlock* Blk, const void* State,
                                    GRBlockCounter BC) {    
    return SubEngine.ProcessBlockEntrance(Blk,
                                          static_cast<const StateTy*>(State),
                                          BC);
  }

  virtual void ProcessBranch(Stmt* Condition, Stmt* Terminator,
                             GRBranchNodeBuilderImpl& BuilderImpl) {
    GRBranchNodeBuilder<StateTy> Builder(BuilderImpl);
    SubEngine.ProcessBranch(Condition, Terminator, Builder);    
  }
  
  virtual void ProcessIndirectGoto(GRIndirectGotoNodeBuilderImpl& BuilderImpl) {
    GRIndirectGotoNodeBuilder<StateTy> Builder(BuilderImpl);
    SubEngine.ProcessIndirectGoto(Builder);
  }
  
  virtual void ProcessSwitch(GRSwitchNodeBuilderImpl& BuilderImpl) {
    GRSwitchNodeBuilder<StateTy> Builder(BuilderImpl);
    SubEngine.ProcessSwitch(Builder);
  }
  
public:  
  /// Construct a GRCoreEngine object to analyze the provided CFG using
  ///  a DFS exploration of the exploded graph.
  GRCoreEngine(CFG& cfg, Decl& cd, ASTContext& ctx, SubEngineTy& subengine)
    : GRCoreEngineImpl(new GraphTy(cfg, cd, ctx),
                       GRWorkList::MakeBFSBlockDFSContents()),
      SubEngine(subengine) {}
  
  /// Construct a GRCoreEngine object to analyze the provided CFG and to
  ///  use the provided worklist object to execute the worklist algorithm.
  ///  The GRCoreEngine object assumes ownership of 'wlist'.
  GRCoreEngine(CFG& cfg, Decl& cd, ASTContext& ctx, GRWorkList* wlist,
               SubEngineTy& subengine)
    : GRCoreEngineImpl(new GraphTy(cfg, cd, ctx), wlist),
      SubEngine(subengine) {}
  
  virtual ~GRCoreEngine() {}
  
  /// getGraph - Returns the exploded graph.
  GraphTy& getGraph() {
    return *static_cast<GraphTy*>(G.get());
  }
  
  /// takeGraph - Returns the exploded graph.  Ownership of the graph is
  ///  transfered to the caller.
  GraphTy* takeGraph() { 
    return static_cast<GraphTy*>(G.take());
  }
};

} // end clang namespace

#endif
