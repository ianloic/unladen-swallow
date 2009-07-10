//===--- Program.h - Entity originator and misc -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Storage for Entities and utility functions
//
//===----------------------------------------------------------------------===//

#include "clang/Index/Program.h"
#include "ProgramImpl.h"
#include "clang/Index/EntityHandler.h"
#include "clang/Index/TranslationUnit.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/ASTContext.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace idx;

// Out-of-line to give the virtual tables a home.
EntityHandler::~EntityHandler() { }
TranslationUnit::~TranslationUnit() { }

Program::Program() : Impl(new ProgramImpl()) { }

Program::~Program() {
  delete static_cast<ProgramImpl *>(Impl);
}

static void FindEntitiesInDC(DeclContext *DC, Program &Prog, EntityHandler *Handler) {
  for (DeclContext::decl_iterator
         I = DC->decls_begin(), E = DC->decls_end(); I != E; ++I) {
    Entity *Ent = Entity::get(*I, Prog);
    if (Ent)
      Handler->HandleEntity(Ent);
    if (DeclContext *SubDC = dyn_cast<DeclContext>(*I))
      FindEntitiesInDC(SubDC, Prog, Handler);
  }
}

/// \brief Traverses the AST and passes all the entities to the Handler.
void Program::FindEntities(ASTContext &Ctx, EntityHandler *Handler) {
  FindEntitiesInDC(Ctx.getTranslationUnitDecl(), *this, Handler);
}
