//===--- CodeGen/ModuleBuilder.h - Build LLVM from ASTs ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleBuilder interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_MODULEBUILDER_H
#define LLVM_CLANG_CODEGEN_MODULEBUILDER_H

#include "clang/AST/ASTConsumer.h"
#include <string>

namespace llvm {
  class Module;
}

namespace clang {
  class Diagnostic;
  struct LangOptions;
  
  class CodeGenerator : public ASTConsumer {
  public:
    virtual llvm::Module* GetModule() = 0;
    virtual llvm::Module* ReleaseModule() = 0;    
  };
  
  CodeGenerator *CreateLLVMCodeGen(Diagnostic &Diags,
                                   const LangOptions &Features,
                                   const std::string& ModuleName,
                                   bool GenerateDebugInfo);
}

#endif
