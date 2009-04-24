//== Environment.cpp - Map from Stmt* to Locations/Values -------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the Environment and EnvironmentManager classes.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathSensitive/Environment.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/Support/Streams.h"

using namespace clang;

SVal Environment::GetSVal(Stmt* E, BasicValueFactory& BasicVals) const {
  
  for (;;) {
    
    switch (E->getStmtClass()) {
        
      case Stmt::AddrLabelExprClass:        
        return Loc::MakeVal(cast<AddrLabelExpr>(E));
        
        // ParenExprs are no-ops.
        
      case Stmt::ParenExprClass:        
        E = cast<ParenExpr>(E)->getSubExpr();
        continue;
        
      case Stmt::CharacterLiteralClass: {
        CharacterLiteral* C = cast<CharacterLiteral>(E);
        return NonLoc::MakeVal(BasicVals, C->getValue(), C->getType());
      }
        
      case Stmt::IntegerLiteralClass: {
        return NonLoc::MakeVal(BasicVals, cast<IntegerLiteral>(E));
      }
        
      // Casts where the source and target type are the same
      // are no-ops.  We blast through these to get the descendant
      // subexpression that has a value.
       
      case Stmt::ImplicitCastExprClass:
      case Stmt::CStyleCastExprClass: {
        CastExpr* C = cast<CastExpr>(E);
        QualType CT = C->getType();
        
        if (CT->isVoidType())
          return UnknownVal();
        
        break;
      }
        
        // Handle all other Stmt* using a lookup.
        
      default:
        break;
    };
    
    break;
  }
  
  return LookupExpr(E);
}

SVal Environment::GetBlkExprSVal(Stmt* E, BasicValueFactory& BasicVals) const {
  
  while (1) {
    switch (E->getStmtClass()) {
      case Stmt::ParenExprClass:
        E = cast<ParenExpr>(E)->getSubExpr();
        continue;
        
      case Stmt::CharacterLiteralClass: {
        CharacterLiteral* C = cast<CharacterLiteral>(E);
        return NonLoc::MakeVal(BasicVals, C->getValue(), C->getType());
      }
        
      case Stmt::IntegerLiteralClass: {
        return NonLoc::MakeVal(BasicVals, cast<IntegerLiteral>(E));
      }
        
      default:
        return LookupBlkExpr(E);
    }
  }
}

Environment EnvironmentManager::BindExpr(const Environment& Env, Stmt* E,SVal V,
                                         bool isBlkExpr, bool Invalidate) {  
  assert (E);
  
  if (V.isUnknown()) {    
    if (Invalidate)
      return isBlkExpr ? RemoveBlkExpr(Env, E) : RemoveSubExpr(Env, E);
    else
      return Env;
  }

  return isBlkExpr ? AddBlkExpr(Env, E, V) : AddSubExpr(Env, E, V);
}

Environment 
EnvironmentManager::RemoveDeadBindings(Environment Env, Stmt* Loc,
                                       SymbolReaper& SymReaper,
                              llvm::SmallVectorImpl<const MemRegion*>& DRoots) {
  
  // Drop bindings for subexpressions.
  Env = RemoveSubExprBindings(Env);

  // Iterate over the block-expr bindings.
  for (Environment::beb_iterator I = Env.beb_begin(), E = Env.beb_end(); 
       I != E; ++I) {
    Stmt* BlkExpr = I.getKey();

    if (SymReaper.isLive(Loc, BlkExpr)) {
      SVal X = I.getData();

      // If the block expr's value is a memory region, then mark that region.
      if (isa<loc::MemRegionVal>(X))
        DRoots.push_back(cast<loc::MemRegionVal>(X).getRegion());

      // Mark all symbols in the block expr's value.
      for (SVal::symbol_iterator SI = X.symbol_begin(), SE = X.symbol_end();
           SI != SE; ++SI)
        SymReaper.markLive(*SI);

    } else {
      // The block expr is dead.
      SVal X = I.getData();

      // Do not misclean LogicalExpr or ConditionalOperator.  It is dead at the
      // beginning of itself, but we need its UndefinedVal to determine its
      // SVal.

      if (X.isUndef() && cast<UndefinedVal>(X).getData())
        continue;

      Env = RemoveBlkExpr(Env, BlkExpr);
    }
  }

  return Env;
}
