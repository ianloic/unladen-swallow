//===- AsmLexer.h - Lexer for Assembly Files --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class declares the lexer for assembly files.
//
//===----------------------------------------------------------------------===//

#ifndef ASMLEXER_H
#define ASMLEXER_H

#include "llvm/Support/DataTypes.h"
#include <string>
#include <cassert>

namespace llvm {
class MemoryBuffer;
class SourceMgr;
class SMLoc;

namespace asmtok {
  enum TokKind {
    // Markers
    Eof, Error,

    // String values.
    Identifier,
    Register,
    String,
    
    // Integer values.
    IntVal,
    
    // No-value.
    EndOfStatement,
    Colon,
    Plus, Minus, Tilde,
    Slash,    // '/'
    LParen, RParen,
    Star, Comma, Dollar, Equal, EqualEqual,
    
    Pipe, PipePipe, Caret, 
    Amp, AmpAmp, Exclaim, ExclaimEqual, Percent, 
    Less, LessEqual, LessLess, LessGreater,
    Greater, GreaterEqual, GreaterGreater
  };
}

/// AsmLexer - Lexer class for assembly files.
class AsmLexer {
  SourceMgr &SrcMgr;
  
  const char *CurPtr;
  const MemoryBuffer *CurBuf;
  // A llvm::StringSet<>, which provides uniqued and null-terminated strings.
  void *TheStringSet;
  
  // Information about the current token.
  const char *TokStart;
  asmtok::TokKind CurKind;
  const char *CurStrVal;  // This is valid for Identifier.
  int64_t CurIntVal;
  
  /// CurBuffer - This is the current buffer index we're lexing from as managed
  /// by the SourceMgr object.
  int CurBuffer;
  
  void operator=(const AsmLexer&); // DO NOT IMPLEMENT
  AsmLexer(const AsmLexer&);       // DO NOT IMPLEMENT
public:
  AsmLexer(SourceMgr &SrcMgr);
  ~AsmLexer();
  
  asmtok::TokKind Lex() {
    return CurKind = LexToken();
  }
  
  asmtok::TokKind getKind() const { return CurKind; }
  bool is(asmtok::TokKind K) const { return CurKind == K; }
  bool isNot(asmtok::TokKind K) const { return CurKind != K; }
  
  const char *getCurStrVal() const {
    assert((CurKind == asmtok::Identifier || CurKind == asmtok::Register ||
            CurKind == asmtok::String) &&
           "This token doesn't have a string value");
    return CurStrVal;
  }
  int64_t getCurIntVal() const {
    assert(CurKind == asmtok::IntVal && "This token isn't an integer");
    return CurIntVal;
  }
  
  SMLoc getLoc() const;
  
  void PrintMessage(SMLoc Loc, const std::string &Msg, const char *Type) const;
  
private:
  int getNextChar();
  asmtok::TokKind ReturnError(const char *Loc, const std::string &Msg);

  /// LexToken - Read the next token and return its code.
  asmtok::TokKind LexToken();
  asmtok::TokKind LexIdentifier();
  asmtok::TokKind LexPercent();
  asmtok::TokKind LexSlash();
  asmtok::TokKind LexLineComment();
  asmtok::TokKind LexDigit();
  asmtok::TokKind LexQuote();
};
  
} // end namespace llvm

#endif
