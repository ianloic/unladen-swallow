//===-- MSP430InstPrinter.cpp - Convert MSP430 MCInst to assembly syntax --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints an MSP430 MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "asm-printer"
#include "MSP430.h"
#include "MSP430InstrInfo.h"
#include "MSP430InstPrinter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;


// Include the auto-generated portion of the assembly writer.
#define MachineInstr MCInst
#define MSP430AsmPrinter MSP430InstPrinter  // FIXME: REMOVE.
#define NO_ASM_WRITER_BOILERPLATE
#include "MSP430GenAsmWriter.inc"
#undef MachineInstr
#undef MSP430AsmPrinter

void MSP430InstPrinter::printInst(const MCInst *MI) {
  printInstruction(MI);
}

void MSP430InstPrinter::printPCRelImmOperand(const MCInst *MI, unsigned OpNo) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    O << Op.getImm();
  else {
    assert(Op.isExpr() && "unknown pcrel immediate operand");
    Op.getExpr()->print(O, &MAI);
  }
}

void MSP430InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                     const char *Modifier) {
  assert((Modifier == 0 || Modifier[0] == 0) && "No modifiers supported");
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
  } else if (Op.isImm()) {
    O << '#' << Op.getImm();
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    O << '#';
    Op.getExpr()->print(O, &MAI);
  }
}

void MSP430InstPrinter::printSrcMemOperand(const MCInst *MI, unsigned OpNo,
                                           const char *Modifier) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo+1);

  // FIXME: move global to displacement field!
  if (Base.isExpr()) {
    O << '&';
    Base.getExpr()->print(O, &MAI);
  } else if (Disp.isImm() && !Base.isReg())
    printOperand(MI, OpNo);
  else if (Base.isReg()) {
    if (Disp.getImm()) {
      O << Disp.getImm() << '(';
      printOperand(MI, OpNo);
      O << ')';
    } else {
      O << '@';
      printOperand(MI, OpNo);
    }
  } else {
    Base.dump();
    Disp.dump();
    llvm_unreachable("Unsupported memory operand");
  }
}

void MSP430InstPrinter::printCCOperand(const MCInst *MI, unsigned OpNo) {
  unsigned CC = MI->getOperand(OpNo).getImm();

  switch (CC) {
  default:
   llvm_unreachable("Unsupported CC code");
   break;
  case MSP430CC::COND_E:
   O << "eq";
   break;
  case MSP430CC::COND_NE:
   O << "ne";
   break;
  case MSP430CC::COND_HS:
   O << "hs";
   break;
  case MSP430CC::COND_LO:
   O << "lo";
   break;
  case MSP430CC::COND_GE:
   O << "ge";
   break;
  case MSP430CC::COND_L:
   O << 'l';
   break;
  }
}
