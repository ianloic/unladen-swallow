//==- CheckObjCUnusedIVars.cpp - Check for unused ivars ----------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckObjCUnusedIvars, a checker that
//  analyzes an Objective-C class's interface/implementation to determine if it
//  has any ivars that are never accessed.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/LangOptions.h"
#include <sstream>

using namespace clang;

enum IVarState { Unused, Used };
typedef llvm::DenseMap<ObjCIvarDecl*,IVarState> IvarUsageMap;

static void Scan(IvarUsageMap& M, Stmt* S) {
  if (!S)
    return;
  
  if (ObjCIvarRefExpr* Ex = dyn_cast<ObjCIvarRefExpr>(S)) {
    ObjCIvarDecl* D = Ex->getDecl();
    IvarUsageMap::iterator I = M.find(D);
    if (I != M.end()) I->second = Used;
    return;
  }
  
  for (Stmt::child_iterator I=S->child_begin(), E=S->child_end(); I!=E;++I)
    Scan(M, *I);
}

static void Scan(IvarUsageMap& M, ObjCPropertyImplDecl* D) {
  if (!D)
    return;
  
  ObjCIvarDecl* ID = D->getPropertyIvarDecl();

  if (!ID)
    return;
  
  IvarUsageMap::iterator I = M.find(ID);
  if (I != M.end()) I->second = Used;
}

void clang::CheckObjCUnusedIvar(ObjCImplementationDecl* D, BugReporter& BR) {

  ObjCInterfaceDecl* ID = D->getClassInterface();
  IvarUsageMap M;


  ASTContext &Ctx = BR.getContext();

  // Iterate over the ivars.
  for (ObjCInterfaceDecl::ivar_iterator I=ID->ivar_begin(), E=ID->ivar_end();
       I!=E; ++I) {
    
    ObjCIvarDecl* ID = *I;
    
    // Ignore ivars that aren't private.
    if (ID->getAccessControl() != ObjCIvarDecl::Private)
      continue;

    // Skip IB Outlets.
    if (ID->getAttr<IBOutletAttr>())
      continue;
    
    M[ID] = Unused;
  }

  if (M.empty())
    return;
  
  // Now scan the methods for accesses.
  for (ObjCImplementationDecl::instmeth_iterator I = D->instmeth_begin(Ctx),
       E = D->instmeth_end(Ctx); I!=E; ++I)
    Scan(M, (*I)->getBody(Ctx));
  
  // Scan for @synthesized property methods that act as setters/getters
  // to an ivar.
  for (ObjCImplementationDecl::propimpl_iterator I = D->propimpl_begin(Ctx),
       E = D->propimpl_end(Ctx); I!=E; ++I)
    Scan(M, *I);  
  
  // Find ivars that are unused.
  for (IvarUsageMap::iterator I = M.begin(), E = M.end(); I!=E; ++I)
    if (I->second == Unused) {
      
      std::ostringstream os;
      os << "Instance variable '" << I->first->getNameAsString()
         << "' in class '" << ID->getNameAsString() 
         << "' is never used by the methods in its @implementation "
            "(although it may be used by category methods).";

      BR.EmitBasicReport("Unused instance variable", "Optimization",
                         os.str().c_str(), I->first->getLocation());
    }
}

