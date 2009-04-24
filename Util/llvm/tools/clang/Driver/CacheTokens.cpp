//===--- CacheTokens.cpp - Caching of lexer tokens for PCH support --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides a possible implementation of PCH support for Clang that is
// based on caching lexed tokens and identifiers.
//
//===----------------------------------------------------------------------===//

#include "clang.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Path.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Streams.h"

using namespace clang;

typedef uint32_t Offset;

namespace {
class VISIBILITY_HIDDEN PCHEntry {
  Offset TokenData, PPCondData;  

public:  
  PCHEntry() {}

  PCHEntry(Offset td, Offset ppcd)
    : TokenData(td), PPCondData(ppcd) {}
  
  Offset getTokenOffset() const { return TokenData; }  
  Offset getPPCondTableOffset() const { return PPCondData; }
};
  
class OffsetOpt {
  bool valid;
  Offset off;
public:
  OffsetOpt() : valid(false) {}
  bool hasOffset() const { return valid; }
  Offset getOffset() const { assert(valid); return off; }
  void setOffset(Offset o) { off = o; valid = true; }
};
} // end anonymous namespace

typedef llvm::DenseMap<const FileEntry*, PCHEntry> PCHMap;
typedef llvm::DenseMap<const IdentifierInfo*,uint32_t> IDMap;
typedef llvm::StringMap<OffsetOpt, llvm::BumpPtrAllocator> CachedStrsTy;

namespace {
class VISIBILITY_HIDDEN PTHWriter {
  IDMap IM;
  llvm::raw_fd_ostream& Out;
  Preprocessor& PP;
  uint32_t idcount;
  PCHMap PM;
  CachedStrsTy CachedStrs;
  Offset CurStrOffset;
  std::vector<llvm::StringMapEntry<OffsetOpt>*> StrEntries;

  //// Get the persistent id for the given IdentifierInfo*.
  uint32_t ResolveID(const IdentifierInfo* II);
  
  /// Emit a token to the PTH file.
  void EmitToken(const Token& T);

  void Emit8(uint32_t V) {
    Out << (unsigned char)(V);
  }
    
  void Emit16(uint32_t V) {
    Out << (unsigned char)(V);
    Out << (unsigned char)(V >>  8);
    assert((V >> 16) == 0);
  }
  
  void Emit24(uint32_t V) {
    Out << (unsigned char)(V);
    Out << (unsigned char)(V >>  8);
    Out << (unsigned char)(V >> 16);
    assert((V >> 24) == 0);
  }

  void Emit32(uint32_t V) {
    Out << (unsigned char)(V);
    Out << (unsigned char)(V >>  8);
    Out << (unsigned char)(V >> 16);
    Out << (unsigned char)(V >> 24);
  }
  
  void EmitBuf(const char* I, const char* E) {
    for ( ; I != E ; ++I) Out << *I;
  }
  
  std::pair<Offset,std::pair<Offset, Offset> > EmitIdentifierTable();
  Offset EmitFileTable();
  PCHEntry LexTokens(Lexer& L);
  Offset EmitCachedSpellings();
  
public:
  PTHWriter(llvm::raw_fd_ostream& out, Preprocessor& pp) 
    : Out(out), PP(pp), idcount(0), CurStrOffset(0) {}
    
  void GeneratePTH();
};
} // end anonymous namespace
  
uint32_t PTHWriter::ResolveID(const IdentifierInfo* II) {  
  // Null IdentifierInfo's map to the persistent ID 0.
  if (!II)
    return 0;
  
  IDMap::iterator I = IM.find(II);

  if (I == IM.end()) {
    IM[II] = ++idcount; // Pre-increment since '0' is reserved for NULL.
    return idcount;
  }
  
  return I->second; // We've already added 1.
}

void PTHWriter::EmitToken(const Token& T) {
  Emit32(((uint32_t) T.getKind()) |
         (((uint32_t) T.getFlags()) << 8) |
         (((uint32_t) T.getLength()) << 16));

  // Literals (strings, numbers, characters) get cached spellings.
  if (T.isLiteral()) {
    // FIXME: This uses the slow getSpelling().  Perhaps we do better
    // in the future?  This only slows down PTH generation.
    const std::string &spelling = PP.getSpelling(T);
    const char* s = spelling.c_str();
    
    // Get the string entry.
    llvm::StringMapEntry<OffsetOpt> *E =
    &CachedStrs.GetOrCreateValue(s, s+spelling.size());
    
    if (!E->getValue().hasOffset()) {
      E->getValue().setOffset(CurStrOffset);
      StrEntries.push_back(E);
      CurStrOffset += spelling.size() + 1;
    }
    
    Emit32(E->getValue().getOffset());
  }
  else
    Emit32(ResolveID(T.getIdentifierInfo()));
    
  Emit32(PP.getSourceManager().getFileOffset(T.getLocation()));
}

namespace {
struct VISIBILITY_HIDDEN IDData {
  const IdentifierInfo* II;
  uint32_t FileOffset;
};
  
class VISIBILITY_HIDDEN CompareIDDataIndex {
  IDData* Table;
public:  
  CompareIDDataIndex(IDData* table) : Table(table) {}

  bool operator()(unsigned i, unsigned j) const {
    const IdentifierInfo* II_i = Table[i].II;
    const IdentifierInfo* II_j = Table[j].II;

    unsigned i_len = II_i->getLength();
    unsigned j_len = II_j->getLength();
    
    if (i_len > j_len)
      return false;
    
    if (i_len < j_len)
      return true;

    // Otherwise, compare the strings themselves!
    return strncmp(II_i->getName(), II_j->getName(), i_len) < 0;    
  }
};
}

std::pair<Offset,std::pair<Offset,Offset> >
PTHWriter::EmitIdentifierTable() {  
  llvm::BumpPtrAllocator Alloc;

  // Build an inverse map from persistent IDs -> IdentifierInfo*.
  IDData* IIDMap = Alloc.Allocate<IDData>(idcount);
  
  // Generate mapping from persistent IDs -> IdentifierInfo*.
  for (IDMap::iterator I=IM.begin(), E=IM.end(); I!=E; ++I) {
    // Decrement by 1 because we are using a vector for the lookup and
    // 0 is reserved for NULL.
    assert(I->second > 0);
    assert(I->second-1 < idcount);
    unsigned idx = I->second-1;
    IIDMap[idx].II = I->first;
  }  
  
  // We want to write out the strings in lexical order to support binary
  // search of strings to identifiers.  Create such a table.
  unsigned *LexicalOrder = Alloc.Allocate<unsigned>(idcount);
  for (unsigned i = 0; i < idcount ; ++i ) LexicalOrder[i] = i;
  std::sort(LexicalOrder, LexicalOrder+idcount, CompareIDDataIndex(IIDMap));
  
  // Write out the lexically-sorted table of persistent ids.
  Offset LexicalOff = Out.tell();
  for (unsigned i = 0; i < idcount ; ++i) Emit32(LexicalOrder[i]);
  
  // Write out the string data itself.
  Offset DataOff = Out.tell();
    
  for (unsigned i = 0; i < idcount; ++i) {
    IDData& d = IIDMap[i];
    d.FileOffset = Out.tell();            // Record the location for this data.  
    unsigned len = d.II->getLength(); // Write out the string length.
    Emit32(len);
    const char* buf = d.II->getName(); // Write out the string data.
    EmitBuf(buf, buf+len);
    // Emit a null character for those clients expecting that IdentifierInfo
    // strings are null terminated.
    Emit8('\0');
  }
  
  // Now emit the table mapping from persistent IDs to PTH file offsets.  
  Offset IDOff = Out.tell();
  Emit32(idcount);  // Emit the number of identifiers.
  for (unsigned i = 0 ; i < idcount; ++i) Emit32(IIDMap[i].FileOffset);

  return std::make_pair(DataOff, std::make_pair(IDOff, LexicalOff));
}

Offset PTHWriter::EmitFileTable() {
  // Determine the offset where this table appears in the PTH file.
  Offset off = (Offset) Out.tell();

  // Output the size of the table.
  Emit32(PM.size());

  for (PCHMap::iterator I=PM.begin(), E=PM.end(); I!=E; ++I) {
    const FileEntry* FE = I->first;
    const char* Name = FE->getName();
    unsigned size = strlen(Name);
    Emit32(size);
    EmitBuf(Name, Name+size);
    Emit32(I->second.getTokenOffset());
    Emit32(I->second.getPPCondTableOffset());
  }

  return off;
}

PCHEntry PTHWriter::LexTokens(Lexer& L) {
  // Pad 0's so that we emit tokens to a 4-byte alignment.
  // This speed up reading them back in.
  Offset off = (Offset) Out.tell();  
  for (unsigned Pad = off % 4 ; Pad != 0 ; --Pad, ++off) Emit8(0);
  
  // Keep track of matching '#if' ... '#endif'.
  typedef std::vector<std::pair<Offset, unsigned> > PPCondTable;
  PPCondTable PPCond;
  std::vector<unsigned> PPStartCond;
  bool ParsingPreprocessorDirective = false;
  Token Tok;
  
  do {
    L.LexFromRawLexer(Tok);
    
    if ((Tok.isAtStartOfLine() || Tok.is(tok::eof)) &&
        ParsingPreprocessorDirective) {
      // Insert an eom token into the token cache.  It has the same
      // position as the next token that is not on the same line as the
      // preprocessor directive.  Observe that we continue processing
      // 'Tok' when we exit this branch.
      Token Tmp = Tok;
      Tmp.setKind(tok::eom);
      Tmp.clearFlag(Token::StartOfLine);
      Tmp.setIdentifierInfo(0);
      EmitToken(Tmp);
      ParsingPreprocessorDirective = false;
    }
    
    if (Tok.is(tok::identifier)) {
      Tok.setIdentifierInfo(PP.LookUpIdentifierInfo(Tok));
      continue;
    }

    if (Tok.is(tok::hash) && Tok.isAtStartOfLine()) {
      // Special processing for #include.  Store the '#' token and lex
      // the next token.
      assert(!ParsingPreprocessorDirective);
      Offset HashOff = (Offset) Out.tell();
      EmitToken(Tok);

      // Get the next token.
      L.LexFromRawLexer(Tok);
            
      assert(!Tok.isAtStartOfLine());
      
      // Did we see 'include'/'import'/'include_next'?
      if (!Tok.is(tok::identifier))
        continue;
      
      IdentifierInfo* II = PP.LookUpIdentifierInfo(Tok);
      Tok.setIdentifierInfo(II);
      tok::PPKeywordKind K = II->getPPKeywordID();
      
      assert(K != tok::pp_not_keyword);
      ParsingPreprocessorDirective = true;
      
      switch (K) {
      default:
        break;
      case tok::pp_include:
      case tok::pp_import:
      case tok::pp_include_next: {        
        // Save the 'include' token.
        EmitToken(Tok);
        // Lex the next token as an include string.
        L.setParsingPreprocessorDirective(true);
        L.LexIncludeFilename(Tok); 
        L.setParsingPreprocessorDirective(false);
        assert(!Tok.isAtStartOfLine());
        if (Tok.is(tok::identifier))
          Tok.setIdentifierInfo(PP.LookUpIdentifierInfo(Tok));
        
        break;
      }
      case tok::pp_if:
      case tok::pp_ifdef:
      case tok::pp_ifndef: {
        // Ad an entry for '#if' and friends.  We initially set the target index
        // to 0.  This will get backpatched when we hit #endif.
        PPStartCond.push_back(PPCond.size());
        PPCond.push_back(std::make_pair(HashOff, 0U));
        break;
      }
      case tok::pp_endif: {
        // Add an entry for '#endif'.  We set the target table index to itself.
        // This will later be set to zero when emitting to the PTH file.  We
        // use 0 for uninitialized indices because that is easier to debug.
        unsigned index = PPCond.size();
        // Backpatch the opening '#if' entry.
        assert(!PPStartCond.empty());
        assert(PPCond.size() > PPStartCond.back());
        assert(PPCond[PPStartCond.back()].second == 0);
        PPCond[PPStartCond.back()].second = index;
        PPStartCond.pop_back();        
        // Add the new entry to PPCond.      
        PPCond.push_back(std::make_pair(HashOff, index));
        break;
      }
      case tok::pp_elif:
      case tok::pp_else: {
        // Add an entry for #elif or #else.
        // This serves as both a closing and opening of a conditional block.
        // This means that its entry will get backpatched later.
        unsigned index = PPCond.size();
        // Backpatch the previous '#if' entry.
        assert(!PPStartCond.empty());
        assert(PPCond.size() > PPStartCond.back());
        assert(PPCond[PPStartCond.back()].second == 0);
        PPCond[PPStartCond.back()].second = index;
        PPStartCond.pop_back();
        // Now add '#elif' as a new block opening.
        PPCond.push_back(std::make_pair(HashOff, 0U));
        PPStartCond.push_back(index);
        break;
      }
      }
    }    
  }
  while (EmitToken(Tok), Tok.isNot(tok::eof));

  assert(PPStartCond.empty() && "Error: imblanced preprocessor conditionals.");

  // Next write out PPCond.
  Offset PPCondOff = (Offset) Out.tell();

  // Write out the size of PPCond so that clients can identifer empty tables.
  Emit32(PPCond.size());

  for (unsigned i = 0, e = PPCond.size(); i!=e; ++i) {
    Emit32(PPCond[i].first - off);
    uint32_t x = PPCond[i].second;
    assert(x != 0 && "PPCond entry not backpatched.");
    // Emit zero for #endifs.  This allows us to do checking when
    // we read the PTH file back in.
    Emit32(x == i ? 0 : x);
  }

  return PCHEntry(off, PPCondOff);
}

Offset PTHWriter::EmitCachedSpellings() {
  // Write each cached strings to the PTH file.
  Offset SpellingsOff = Out.tell();
  
  for (std::vector<llvm::StringMapEntry<OffsetOpt>*>::iterator
       I = StrEntries.begin(), E = StrEntries.end(); I!=E; ++I) {

    const char* data = (*I)->getKeyData();
    EmitBuf(data, data + (*I)->getKeyLength());
    Emit8('\0');
  }
  
  return SpellingsOff;
}

void PTHWriter::GeneratePTH() {
  // Generate the prologue.
  Out << "cfe-pth";
  Emit32(PTHManager::Version);
  Offset JumpOffset = Out.tell();
  Emit32(0);
  
  // Iterate over all the files in SourceManager.  Create a lexer
  // for each file and cache the tokens.
  SourceManager &SM = PP.getSourceManager();
  const LangOptions &LOpts = PP.getLangOptions();
  
  for (SourceManager::fileinfo_iterator I = SM.fileinfo_begin(),
       E = SM.fileinfo_end(); I != E; ++I) {
    const SrcMgr::ContentCache &C = *I;
    const FileEntry *FE = C.Entry;
    
    // FIXME: Handle files with non-absolute paths.
    llvm::sys::Path P(FE->getName());
    if (!P.isAbsolute())
      continue;

    assert(!PM.count(FE) && "fileinfo's are not uniqued on FileEntry?");
    
    const llvm::MemoryBuffer *B = C.getBuffer();
    if (!B) continue;

    FileID FID = SM.createFileID(FE, SourceLocation(), SrcMgr::C_User);
    Lexer L(FID, SM, LOpts);
    PM[FE] = LexTokens(L);
  }

  // Write out the identifier table.
  const std::pair<Offset, std::pair<Offset,Offset> >& IdTableOff 
    = EmitIdentifierTable();
  
  // Write out the cached strings table.
  Offset SpellingOff = EmitCachedSpellings();
  
  // Write out the file table.
  Offset FileTableOff = EmitFileTable();  
  
  // Finally, write out the offset table at the end.
  Offset JumpTargetOffset = Out.tell();    
  Emit32(IdTableOff.first);
  Emit32(IdTableOff.second.first);
  Emit32(IdTableOff.second.second);
  Emit32(FileTableOff);
  Emit32(SpellingOff);
  
  // Now write the offset in the prologue.
  Out.seek(JumpOffset);
  Emit32(JumpTargetOffset);
}

void clang::CacheTokens(Preprocessor& PP, const std::string& OutFile) {
  // Lex through the entire file.  This will populate SourceManager with
  // all of the header information.
  Token Tok;
  PP.EnterMainSourceFile();
  do { PP.Lex(Tok); } while (Tok.isNot(tok::eof));
  
  // Open up the PTH file.
  std::string ErrMsg;
  llvm::raw_fd_ostream Out(OutFile.c_str(), true, ErrMsg);
  
  if (!ErrMsg.empty()) {
    llvm::errs() << "PTH error: " << ErrMsg << "\n";
    return;
  }
  
  // Create the PTHWriter and generate the PTH file.
  PTHWriter PW(Out, PP);
  PW.GeneratePTH();
}
