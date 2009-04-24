//===--- TranslationUnit.h - Abstraction for Translation Units  -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// FIXME: This should eventually be moved out of the driver, or replaced
//        with its eventual successor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TRANSLATION_UNIT_H
#define LLVM_CLANG_TRANSLATION_UNIT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "llvm/Bitcode/SerializationFwd.h"
#include "llvm/System/Path.h"
#include <string>

namespace clang {
 
class FileManager;
class SourceManager;
class TargetInfo;
class IdentifierTable;
class SelectorTable;
class ASTContext;
class Decl;
class FileEntry;
  
class TranslationUnit {
  ASTContext* Context;
  bool OwnsMetaData;
  bool OwnsDecls;

  // The default ctor is only invoked during deserialization.
  explicit TranslationUnit() : Context(NULL), OwnsMetaData(true),
                               OwnsDecls(true) {}
  
public:
  explicit TranslationUnit(ASTContext& Ctx)
    : Context(&Ctx), OwnsMetaData(false), OwnsDecls(true) {}

  void SetOwnsDecls(bool val) { OwnsDecls = val; }

  ~TranslationUnit();

  const std::string& getSourceFile() const;
  
  /// Emit - Emit the translation unit to an arbitray bitcode stream.
  void Emit(llvm::Serializer& S) const;
  
  /// Create - Reconsititute a translation unit from a bitcode stream.
  static TranslationUnit* Create(llvm::Deserializer& D, FileManager& FMgr);
  
  // Accessors
  const LangOptions& getLangOptions() const { return Context->getLangOptions();}

  ASTContext&        getContext() { return *Context; }
  const ASTContext&  getContext() const { return *Context; }

  typedef DeclContext::decl_iterator iterator;
  iterator begin() const { 
    return Context->getTranslationUnitDecl()->decls_begin(); 
  }
  iterator end() const { return Context->getTranslationUnitDecl()->decls_end(); }
};
  
/// EmitASTBitcodeFile - Emit a translation unit to a bitcode file.
bool EmitASTBitcodeFile(const TranslationUnit& TU, 
                        const llvm::sys::Path& Filename);
  
bool EmitASTBitcodeFile(const TranslationUnit* TU, 
                        const llvm::sys::Path& Filename);
                     
/// EmitASTBitcodeStream - Emit a translation unit to a std::ostream.
bool EmitASTBitcodeStream(const TranslationUnit& TU, 
                          std::ostream& Stream);
  
bool EmitASTBitcodeStream(const TranslationUnit* TU, 
                          std::ostream& Stream);
                     
/// EmitASTBitcodeBuffer - Emit a translation unit to a buffer.
bool EmitASTBitcodeBuffer(const TranslationUnit& TU, 
                          std::vector<unsigned char>& Buffer);

bool EmitASTBitcodeBuffer(const TranslationUnit* TU, 
                          std::vector<unsigned char>& Buffer);

/// ReadASTBitcodeFile - Reconsitute a translation unit from a bitcode file.
TranslationUnit* ReadASTBitcodeFile(const llvm::sys::Path& Filename,
                                    FileManager& FMgr); 
                
/// ReadASTBitcodeBuffer - Reconsitute a translation unit from a buffer.
TranslationUnit* ReadASTBitcodeBuffer(llvm::MemoryBuffer& MBuffer,
                                      FileManager& FMgr); 
                

} // end namespace clang

#endif
