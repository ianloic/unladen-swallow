//===- MC-X86Specific.cpp - X86-Specific code for MC ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements X86-specific parsing, encoding and decoding stuff for
// MC.
//
//===----------------------------------------------------------------------===//

#include "AsmParser.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/SourceMgr.h"
using namespace llvm;

/// X86Operand - Instances of this class represent one X86 machine instruction.
struct AsmParser::X86Operand {
  enum {
    Register,
    Immediate,
    Memory
  } Kind;
  
  union {
    struct {
      unsigned RegNo;
    } Reg;

    struct {
      MCValue Val;
    } Imm;
    
    struct {
      unsigned SegReg;
      MCValue Disp;
      unsigned BaseReg;
      unsigned IndexReg;
      unsigned Scale;
    } Mem;
  };
  
  unsigned getReg() const {
    assert(Kind == Register && "Invalid access!");
    return Reg.RegNo;
  }

  static X86Operand CreateReg(unsigned RegNo) {
    X86Operand Res;
    Res.Kind = Register;
    Res.Reg.RegNo = RegNo;
    return Res;
  }
  static X86Operand CreateImm(MCValue Val) {
    X86Operand Res;
    Res.Kind = Immediate;
    Res.Imm.Val = Val;
    return Res;
  }
  static X86Operand CreateMem(unsigned SegReg, MCValue Disp, unsigned BaseReg,
                              unsigned IndexReg, unsigned Scale) {
    // If there is no index register, we should never have a scale, and we
    // should always have a scale (in {1,2,4,8}) if we do.
    assert(((Scale == 0 && !IndexReg) ||
            (IndexReg && (Scale == 1 || Scale == 2 || 
                          Scale == 4 || Scale == 8))) &&
           "Invalid scale!");
    X86Operand Res;
    Res.Kind = Memory;
    Res.Mem.SegReg   = SegReg;
    Res.Mem.Disp     = Disp;
    Res.Mem.BaseReg  = BaseReg;
    Res.Mem.IndexReg = IndexReg;
    Res.Mem.Scale    = Scale;
    return Res;
  }
};

bool AsmParser::ParseX86Register(X86Operand &Op) {
  assert(Lexer.getKind() == asmtok::Register && "Invalid token kind!");

  // FIXME: Decode register number.
  Op = X86Operand::CreateReg(123);
  Lexer.Lex(); // Eat register token.

  return false;
}

bool AsmParser::ParseX86Operand(X86Operand &Op) {
  switch (Lexer.getKind()) {
  default:
    return ParseX86MemOperand(Op);
  case asmtok::Register:
    // FIXME: if a segment register, this could either be just the seg reg, or
    // the start of a memory operand.
    return ParseX86Register(Op);
  case asmtok::Dollar: {
    // $42 -> immediate.
    Lexer.Lex();
    MCValue Val;
    if (ParseRelocatableExpression(Val))
      return true;
    Op = X86Operand::CreateImm(Val);
    return false;
  }
  case asmtok::Star: {
    Lexer.Lex(); // Eat the star.
    
    if (Lexer.is(asmtok::Register)) {
      if (ParseX86Register(Op))
        return true;
    } else if (ParseX86MemOperand(Op))
      return true;

    // FIXME: Note the '*' in the operand for use by the matcher.
    return false;
  }
  }
}

/// ParseX86MemOperand: segment: disp(basereg, indexreg, scale)
bool AsmParser::ParseX86MemOperand(X86Operand &Op) {
  // FIXME: If SegReg ':'  (e.g. %gs:), eat and remember.
  unsigned SegReg = 0;
  
  // We have to disambiguate a parenthesized expression "(4+5)" from the start
  // of a memory operand with a missing displacement "(%ebx)" or "(,%eax)".  The
  // only way to do this without lookahead is to eat the ( and see what is after
  // it.
  MCValue Disp = MCValue::get(0, 0, 0);
  if (Lexer.isNot(asmtok::LParen)) {
    if (ParseRelocatableExpression(Disp)) return true;
    
    // After parsing the base expression we could either have a parenthesized
    // memory address or not.  If not, return now.  If so, eat the (.
    if (Lexer.isNot(asmtok::LParen)) {
      Op = X86Operand::CreateMem(SegReg, Disp, 0, 0, 0);
      return false;
    }
    
    // Eat the '('.
    Lexer.Lex();
  } else {
    // Okay, we have a '('.  We don't know if this is an expression or not, but
    // so we have to eat the ( to see beyond it.
    Lexer.Lex(); // Eat the '('.
    
    if (Lexer.is(asmtok::Register) || Lexer.is(asmtok::Comma)) {
      // Nothing to do here, fall into the code below with the '(' part of the
      // memory operand consumed.
    } else {
      // It must be an parenthesized expression, parse it now.
      if (ParseParenRelocatableExpression(Disp))
        return true;
      
      // After parsing the base expression we could either have a parenthesized
      // memory address or not.  If not, return now.  If so, eat the (.
      if (Lexer.isNot(asmtok::LParen)) {
        Op = X86Operand::CreateMem(SegReg, Disp, 0, 0, 0);
        return false;
      }
      
      // Eat the '('.
      Lexer.Lex();
    }
  }
  
  // If we reached here, then we just ate the ( of the memory operand.  Process
  // the rest of the memory operand.
  unsigned BaseReg = 0, IndexReg = 0, Scale = 0;
  
  if (Lexer.is(asmtok::Register)) {
    if (ParseX86Register(Op))
      return true;
    BaseReg = Op.getReg();
  }
  
  if (Lexer.is(asmtok::Comma)) {
    Lexer.Lex(); // Eat the comma.

    // Following the comma we should have either an index register, or a scale
    // value. We don't support the later form, but we want to parse it
    // correctly.
    //
    // Not that even though it would be completely consistent to support syntax
    // like "1(%eax,,1)", the assembler doesn't.
    if (Lexer.is(asmtok::Register)) {
      if (ParseX86Register(Op))
        return true;
      IndexReg = Op.getReg();
      Scale = 1;      // If not specified, the scale defaults to 1.
    
      if (Lexer.isNot(asmtok::RParen)) {
        // Parse the scale amount:
        //  ::= ',' [scale-expression]
        if (Lexer.isNot(asmtok::Comma))
          return true;
        Lexer.Lex(); // Eat the comma.

        if (Lexer.isNot(asmtok::RParen)) {
          int64_t ScaleVal;
          if (ParseAbsoluteExpression(ScaleVal))
            return true;
          
          // Validate the scale amount.
          if (ScaleVal != 1 && ScaleVal != 2 && ScaleVal != 4 && ScaleVal != 8)
            return TokError("scale factor in address must be 1, 2, 4 or 8");
          Scale = (unsigned)ScaleVal;
        }
      }
    } else if (Lexer.isNot(asmtok::RParen)) {
      // Otherwise we have the unsupported form of a scale amount without an
      // index.
      SMLoc Loc = Lexer.getLoc();

      int64_t Value;
      if (ParseAbsoluteExpression(Value))
        return true;
      
      return Error(Loc, "cannot have scale factor without index register");
    }
  }
  
  // Ok, we've eaten the memory operand, verify we have a ')' and eat it too.
  if (Lexer.isNot(asmtok::RParen))
    return TokError("unexpected token in memory operand");
  Lexer.Lex(); // Eat the ')'.
  
  Op = X86Operand::CreateMem(SegReg, Disp, BaseReg, IndexReg, Scale);
  return false;
}

/// MatchX86Inst - Convert a parsed instruction name and operand list into a
/// concrete instruction.
static bool MatchX86Inst(const char *Name, 
                         llvm::SmallVector<AsmParser::X86Operand, 3> &Operands,
                         MCInst &Inst) {
  return false;
}

/// ParseX86InstOperands - Parse the operands of an X86 instruction and return
/// them as the operands of an MCInst.
bool AsmParser::ParseX86InstOperands(const char *InstName, MCInst &Inst) {
  llvm::SmallVector<X86Operand, 3> Operands;

  if (Lexer.isNot(asmtok::EndOfStatement)) {
    // Read the first operand.
    Operands.push_back(X86Operand());
    if (ParseX86Operand(Operands.back()))
      return true;
    
    while (Lexer.is(asmtok::Comma)) {
      Lexer.Lex();  // Eat the comma.
      
      // Parse and remember the operand.
      Operands.push_back(X86Operand());
      if (ParseX86Operand(Operands.back()))
        return true;
    }
  }

  return MatchX86Inst(InstName, Operands, Inst);
}
