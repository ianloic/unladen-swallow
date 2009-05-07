//===-- ARMISelLowering.cpp - ARM DAG Lowering Implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that ARM uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMAddressingModes.h"
#include "ARMConstantPoolValue.h"
#include "ARMISelLowering.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMRegisterInfo.h"
#include "ARMSubtarget.h"
#include "ARMTargetMachine.h"
#include "llvm/CallingConv.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/Intrinsics.h"
#include "llvm/GlobalValue.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

static bool CC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags,
                                   CCState &State);
static bool CC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags,
                                    CCState &State);
static bool RetCC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                      CCValAssign::LocInfo &LocInfo,
                                      ISD::ArgFlagsTy &ArgFlags,
                                      CCState &State);
static bool RetCC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                       CCValAssign::LocInfo &LocInfo,
                                       ISD::ArgFlagsTy &ArgFlags,
                                       CCState &State);

ARMTargetLowering::ARMTargetLowering(TargetMachine &TM)
    : TargetLowering(TM), ARMPCLabelIndex(0) {
  Subtarget = &TM.getSubtarget<ARMSubtarget>();

  if (Subtarget->isTargetDarwin()) {
    // Uses VFP for Thumb libfuncs if available.
    if (Subtarget->isThumb() && Subtarget->hasVFP2()) {
      // Single-precision floating-point arithmetic.
      setLibcallName(RTLIB::ADD_F32, "__addsf3vfp");
      setLibcallName(RTLIB::SUB_F32, "__subsf3vfp");
      setLibcallName(RTLIB::MUL_F32, "__mulsf3vfp");
      setLibcallName(RTLIB::DIV_F32, "__divsf3vfp");

      // Double-precision floating-point arithmetic.
      setLibcallName(RTLIB::ADD_F64, "__adddf3vfp");
      setLibcallName(RTLIB::SUB_F64, "__subdf3vfp");
      setLibcallName(RTLIB::MUL_F64, "__muldf3vfp");
      setLibcallName(RTLIB::DIV_F64, "__divdf3vfp");

      // Single-precision comparisons.
      setLibcallName(RTLIB::OEQ_F32, "__eqsf2vfp");
      setLibcallName(RTLIB::UNE_F32, "__nesf2vfp");
      setLibcallName(RTLIB::OLT_F32, "__ltsf2vfp");
      setLibcallName(RTLIB::OLE_F32, "__lesf2vfp");
      setLibcallName(RTLIB::OGE_F32, "__gesf2vfp");
      setLibcallName(RTLIB::OGT_F32, "__gtsf2vfp");
      setLibcallName(RTLIB::UO_F32,  "__unordsf2vfp");
      setLibcallName(RTLIB::O_F32,   "__unordsf2vfp");

      setCmpLibcallCC(RTLIB::OEQ_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::UNE_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OLT_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OLE_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OGE_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OGT_F32, ISD::SETNE);
      setCmpLibcallCC(RTLIB::UO_F32,  ISD::SETNE);
      setCmpLibcallCC(RTLIB::O_F32,   ISD::SETEQ);

      // Double-precision comparisons.
      setLibcallName(RTLIB::OEQ_F64, "__eqdf2vfp");
      setLibcallName(RTLIB::UNE_F64, "__nedf2vfp");
      setLibcallName(RTLIB::OLT_F64, "__ltdf2vfp");
      setLibcallName(RTLIB::OLE_F64, "__ledf2vfp");
      setLibcallName(RTLIB::OGE_F64, "__gedf2vfp");
      setLibcallName(RTLIB::OGT_F64, "__gtdf2vfp");
      setLibcallName(RTLIB::UO_F64,  "__unorddf2vfp");
      setLibcallName(RTLIB::O_F64,   "__unorddf2vfp");

      setCmpLibcallCC(RTLIB::OEQ_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::UNE_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OLT_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OLE_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OGE_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::OGT_F64, ISD::SETNE);
      setCmpLibcallCC(RTLIB::UO_F64,  ISD::SETNE);
      setCmpLibcallCC(RTLIB::O_F64,   ISD::SETEQ);

      // Floating-point to integer conversions.
      // i64 conversions are done via library routines even when generating VFP
      // instructions, so use the same ones.
      setLibcallName(RTLIB::FPTOSINT_F64_I32, "__fixdfsivfp");
      setLibcallName(RTLIB::FPTOUINT_F64_I32, "__fixunsdfsivfp");
      setLibcallName(RTLIB::FPTOSINT_F32_I32, "__fixsfsivfp");
      setLibcallName(RTLIB::FPTOUINT_F32_I32, "__fixunssfsivfp");

      // Conversions between floating types.
      setLibcallName(RTLIB::FPROUND_F64_F32, "__truncdfsf2vfp");
      setLibcallName(RTLIB::FPEXT_F32_F64,   "__extendsfdf2vfp");

      // Integer to floating-point conversions.
      // i64 conversions are done via library routines even when generating VFP
      // instructions, so use the same ones.
      // FIXME: There appears to be some naming inconsistency in ARM libgcc:
      // e.g., __floatunsidf vs. __floatunssidfvfp.
      setLibcallName(RTLIB::SINTTOFP_I32_F64, "__floatsidfvfp");
      setLibcallName(RTLIB::UINTTOFP_I32_F64, "__floatunssidfvfp");
      setLibcallName(RTLIB::SINTTOFP_I32_F32, "__floatsisfvfp");
      setLibcallName(RTLIB::UINTTOFP_I32_F32, "__floatunssisfvfp");
    }
  }

  if (Subtarget->isThumb())
    addRegisterClass(MVT::i32, ARM::tGPRRegisterClass);
  else
    addRegisterClass(MVT::i32, ARM::GPRRegisterClass);
  if (!UseSoftFloat && Subtarget->hasVFP2() && !Subtarget->isThumb()) {
    addRegisterClass(MVT::f32, ARM::SPRRegisterClass);
    addRegisterClass(MVT::f64, ARM::DPRRegisterClass);

    setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  }
  computeRegisterProperties();

  // ARM does not have f32 extending load.
  setLoadExtAction(ISD::EXTLOAD, MVT::f32, Expand);

  // ARM does not have i1 sign extending load.
  setLoadExtAction(ISD::SEXTLOAD, MVT::i1, Promote);

  // ARM supports all 4 flavors of integer indexed load / store.
  for (unsigned im = (unsigned)ISD::PRE_INC;
       im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
    setIndexedLoadAction(im,  MVT::i1,  Legal);
    setIndexedLoadAction(im,  MVT::i8,  Legal);
    setIndexedLoadAction(im,  MVT::i16, Legal);
    setIndexedLoadAction(im,  MVT::i32, Legal);
    setIndexedStoreAction(im, MVT::i1,  Legal);
    setIndexedStoreAction(im, MVT::i8,  Legal);
    setIndexedStoreAction(im, MVT::i16, Legal);
    setIndexedStoreAction(im, MVT::i32, Legal);
  }

  // i64 operation support.
  if (Subtarget->isThumb()) {
    setOperationAction(ISD::MUL,     MVT::i64, Expand);
    setOperationAction(ISD::MULHU,   MVT::i32, Expand);
    setOperationAction(ISD::MULHS,   MVT::i32, Expand);
    setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
    setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  } else {
    setOperationAction(ISD::MUL,     MVT::i64, Expand);
    setOperationAction(ISD::MULHU,   MVT::i32, Expand);
    if (!Subtarget->hasV6Ops())
      setOperationAction(ISD::MULHS, MVT::i32, Expand);
  }
  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL,       MVT::i64, Custom);
  setOperationAction(ISD::SRA,       MVT::i64, Custom);

  // ARM does not have ROTL.
  setOperationAction(ISD::ROTL,  MVT::i32, Expand);
  setOperationAction(ISD::CTTZ,  MVT::i32, Expand);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  if (!Subtarget->hasV5TOps() || Subtarget->isThumb())
    setOperationAction(ISD::CTLZ, MVT::i32, Expand);

  // Only ARMv6 has BSWAP.
  if (!Subtarget->hasV6Ops())
    setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  // These are expanded into libcalls.
  setOperationAction(ISD::SDIV,  MVT::i32, Expand);
  setOperationAction(ISD::UDIV,  MVT::i32, Expand);
  setOperationAction(ISD::SREM,  MVT::i32, Expand);
  setOperationAction(ISD::UREM,  MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  // Support label based line numbers.
  setOperationAction(ISD::DBG_STOPPOINT, MVT::Other, Expand);
  setOperationAction(ISD::DEBUG_LOC, MVT::Other, Expand);

  setOperationAction(ISD::RET,           MVT::Other, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i32,   Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32,   Custom);
  setOperationAction(ISD::GLOBAL_OFFSET_TABLE, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);

  // Use the default implementation.
  setOperationAction(ISD::VASTART,            MVT::Other, Custom);
  setOperationAction(ISD::VAARG,              MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,             MVT::Other, Expand);
  setOperationAction(ISD::VAEND,              MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE,          MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,       MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32,   Expand);
  setOperationAction(ISD::MEMBARRIER,         MVT::Other, Expand);

  if (!Subtarget->hasV6Ops()) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8,  Expand);
  }
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  if (!UseSoftFloat && Subtarget->hasVFP2() && !Subtarget->isThumb())
    // Turn f64->i64 into FMRRD, i64 -> f64 to FMDRR iff target supports vfp2.
    setOperationAction(ISD::BIT_CONVERT, MVT::i64, Custom);

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  setOperationAction(ISD::SETCC,     MVT::i32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f64, Expand);
  setOperationAction(ISD::SELECT,    MVT::i32, Expand);
  setOperationAction(ISD::SELECT,    MVT::f32, Expand);
  setOperationAction(ISD::SELECT,    MVT::f64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);

  setOperationAction(ISD::BRCOND,    MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,     MVT::i32,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f32,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f64,   Custom);
  setOperationAction(ISD::BR_JT,     MVT::Other, Custom);

  // We don't support sin/cos/fmod/copysign/pow
  setOperationAction(ISD::FSIN,      MVT::f64, Expand);
  setOperationAction(ISD::FSIN,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f64, Expand);
  setOperationAction(ISD::FREM,      MVT::f64, Expand);
  setOperationAction(ISD::FREM,      MVT::f32, Expand);
  if (!UseSoftFloat && Subtarget->hasVFP2() && !Subtarget->isThumb()) {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Custom);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Custom);
  }
  setOperationAction(ISD::FPOW,      MVT::f64, Expand);
  setOperationAction(ISD::FPOW,      MVT::f32, Expand);

  // int <-> fp are custom expanded into bit_convert + ARMISD ops.
  if (!UseSoftFloat && Subtarget->hasVFP2() && !Subtarget->isThumb()) {
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
  }

  // We have target-specific dag combine patterns for the following nodes:
  // ARMISD::FMRRD  - No need to call setTargetDAGCombine
  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::SUB);

  setStackPointerRegisterToSaveRestore(ARM::SP);
  setSchedulingPreference(SchedulingForRegPressure);
  setIfCvtBlockSizeLimit(Subtarget->isThumb() ? 0 : 10);
  setIfCvtDupBlockSizeLimit(Subtarget->isThumb() ? 0 : 2);

  maxStoresPerMemcpy = 1;   //// temporary - rewrite interface to use type
}

const char *ARMTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default: return 0;
  case ARMISD::Wrapper:       return "ARMISD::Wrapper";
  case ARMISD::WrapperJT:     return "ARMISD::WrapperJT";
  case ARMISD::CALL:          return "ARMISD::CALL";
  case ARMISD::CALL_PRED:     return "ARMISD::CALL_PRED";
  case ARMISD::CALL_NOLINK:   return "ARMISD::CALL_NOLINK";
  case ARMISD::tCALL:         return "ARMISD::tCALL";
  case ARMISD::BRCOND:        return "ARMISD::BRCOND";
  case ARMISD::BR_JT:         return "ARMISD::BR_JT";
  case ARMISD::RET_FLAG:      return "ARMISD::RET_FLAG";
  case ARMISD::PIC_ADD:       return "ARMISD::PIC_ADD";
  case ARMISD::CMP:           return "ARMISD::CMP";
  case ARMISD::CMPNZ:         return "ARMISD::CMPNZ";
  case ARMISD::CMPFP:         return "ARMISD::CMPFP";
  case ARMISD::CMPFPw0:       return "ARMISD::CMPFPw0";
  case ARMISD::FMSTAT:        return "ARMISD::FMSTAT";
  case ARMISD::CMOV:          return "ARMISD::CMOV";
  case ARMISD::CNEG:          return "ARMISD::CNEG";

  case ARMISD::FTOSI:         return "ARMISD::FTOSI";
  case ARMISD::FTOUI:         return "ARMISD::FTOUI";
  case ARMISD::SITOF:         return "ARMISD::SITOF";
  case ARMISD::UITOF:         return "ARMISD::UITOF";

  case ARMISD::SRL_FLAG:      return "ARMISD::SRL_FLAG";
  case ARMISD::SRA_FLAG:      return "ARMISD::SRA_FLAG";
  case ARMISD::RRX:           return "ARMISD::RRX";

  case ARMISD::FMRRD:         return "ARMISD::FMRRD";
  case ARMISD::FMDRR:         return "ARMISD::FMDRR";

  case ARMISD::THREAD_POINTER:return "ARMISD::THREAD_POINTER";
  }
}

//===----------------------------------------------------------------------===//
// Lowering Code
//===----------------------------------------------------------------------===//

/// IntCCToARMCC - Convert a DAG integer condition code to an ARM CC
static ARMCC::CondCodes IntCCToARMCC(ISD::CondCode CC) {
  switch (CC) {
  default: assert(0 && "Unknown condition code!");
  case ISD::SETNE:  return ARMCC::NE;
  case ISD::SETEQ:  return ARMCC::EQ;
  case ISD::SETGT:  return ARMCC::GT;
  case ISD::SETGE:  return ARMCC::GE;
  case ISD::SETLT:  return ARMCC::LT;
  case ISD::SETLE:  return ARMCC::LE;
  case ISD::SETUGT: return ARMCC::HI;
  case ISD::SETUGE: return ARMCC::HS;
  case ISD::SETULT: return ARMCC::LO;
  case ISD::SETULE: return ARMCC::LS;
  }
}

/// FPCCToARMCC - Convert a DAG fp condition code to an ARM CC. It
/// returns true if the operands should be inverted to form the proper
/// comparison.
static bool FPCCToARMCC(ISD::CondCode CC, ARMCC::CondCodes &CondCode,
                        ARMCC::CondCodes &CondCode2) {
  bool Invert = false;
  CondCode2 = ARMCC::AL;
  switch (CC) {
  default: assert(0 && "Unknown FP condition!");
  case ISD::SETEQ:
  case ISD::SETOEQ: CondCode = ARMCC::EQ; break;
  case ISD::SETGT:
  case ISD::SETOGT: CondCode = ARMCC::GT; break;
  case ISD::SETGE:
  case ISD::SETOGE: CondCode = ARMCC::GE; break;
  case ISD::SETOLT: CondCode = ARMCC::MI; break;
  case ISD::SETOLE: CondCode = ARMCC::GT; Invert = true; break;
  case ISD::SETONE: CondCode = ARMCC::MI; CondCode2 = ARMCC::GT; break;
  case ISD::SETO:   CondCode = ARMCC::VC; break;
  case ISD::SETUO:  CondCode = ARMCC::VS; break;
  case ISD::SETUEQ: CondCode = ARMCC::EQ; CondCode2 = ARMCC::VS; break;
  case ISD::SETUGT: CondCode = ARMCC::HI; break;
  case ISD::SETUGE: CondCode = ARMCC::PL; break;
  case ISD::SETLT:
  case ISD::SETULT: CondCode = ARMCC::LT; break;
  case ISD::SETLE:
  case ISD::SETULE: CondCode = ARMCC::LE; break;
  case ISD::SETNE:
  case ISD::SETUNE: CondCode = ARMCC::NE; break;
  }
  return Invert;
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//
//  The lower operations present on calling convention works on this order:
//      LowerCALL (virt regs --> phys regs, virt regs --> stack)
//      LowerFORMAL_ARGUMENTS (phys --> virt regs, stack --> virt regs)
//      LowerRET (virt regs --> phys regs)
//      LowerCALL (phys regs --> virt regs)
//
//===----------------------------------------------------------------------===//

#include "ARMGenCallingConv.inc"

// APCS f64 is in register pairs, possibly split to stack
static bool CC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags,
                                   CCState &State) {
  static const unsigned HiRegList[] = { ARM::R0, ARM::R1, ARM::R2, ARM::R3 };
  static const unsigned LoRegList[] = { ARM::R1,
                                        ARM::R2,
                                        ARM::R3,
                                        ARM::NoRegister };

  unsigned Reg = State.AllocateReg(HiRegList, LoRegList, 4);
  if (Reg == 0) 
    return false; // we didn't handle it

  unsigned i;
  for (i = 0; i < 4; ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::i32, LocInfo));
  if (LoRegList[i] != ARM::NoRegister)
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                           MVT::i32, LocInfo));
  else
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(4, 4),
                                           MVT::i32, LocInfo));
  return true;  // we handled it
}

// AAPCS f64 is in aligned register pairs
static bool CC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags,
                                    CCState &State) {
  static const unsigned HiRegList[] = { ARM::R0, ARM::R2 };
  static const unsigned LoRegList[] = { ARM::R1, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, LoRegList, 2);
  if (Reg == 0)
    return false; // we didn't handle it

  unsigned i;
  for (i = 0; i < 2; ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::i32, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         MVT::i32, LocInfo));
  return true;  // we handled it
}

static bool RetCC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                      CCValAssign::LocInfo &LocInfo,
                                      ISD::ArgFlagsTy &ArgFlags,
                                      CCState &State) {
  static const unsigned HiRegList[] = { ARM::R0, ARM::R2 };
  static const unsigned LoRegList[] = { ARM::R1, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, LoRegList, 2);
  if (Reg == 0)
    return false; // we didn't handle it

  unsigned i;
  for (i = 0; i < 2; ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, MVT::i32, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         MVT::i32, LocInfo));
  return true;  // we handled it
}

static bool RetCC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                       CCValAssign::LocInfo &LocInfo,
                                       ISD::ArgFlagsTy &ArgFlags,
                                       CCState &State) {
  return RetCC_ARM_APCS_Custom_f64(ValNo, ValVT, LocVT, LocInfo, ArgFlags,
                                   State);
}

/// LowerCallResult - Lower the result values of an ISD::CALL into the
/// appropriate copies out of appropriate physical registers.  This assumes that
/// Chain/InFlag are the input chain/flag to use, and that TheCall is the call
/// being lowered.  The returns a SDNode with the same number of values as the
/// ISD::CALL.
SDNode *ARMTargetLowering::
LowerCallResult(SDValue Chain, SDValue InFlag, CallSDNode *TheCall,
                unsigned CallingConv, SelectionDAG &DAG) {

  DebugLoc dl = TheCall->getDebugLoc();
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  bool isVarArg = TheCall->isVarArg();
  CCState CCInfo(CallingConv, isVarArg, getTargetMachine(), RVLocs);
  CCInfo.AnalyzeCallResult(TheCall, RetCC_ARM);

  SmallVector<SDValue, 8> ResultVals;

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign VA = RVLocs[i];

    SDValue Val;
    if (VA.needsCustom()) {
      // Handle f64 as custom.
      SDValue Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain = Lo.getValue(1);
      InFlag = Lo.getValue(2);
      VA = RVLocs[++i]; // skip ahead to next loc
      SDValue Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain = Hi.getValue(1);
      InFlag = Hi.getValue(2);
      Val = DAG.getNode(ARMISD::FMDRR, dl, MVT::f64, Lo, Hi);
    } else {
      Val = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getLocVT(),
                               InFlag);
      Chain = Val.getValue(1);
      InFlag = Val.getValue(2);
    }

    switch (VA.getLocInfo()) {
    default: assert(0 && "Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BIT_CONVERT, dl, VA.getValVT(), Val);
      break;
    }

    ResultVals.push_back(Val);
  }

  // Merge everything together with a MERGE_VALUES node.
  ResultVals.push_back(Chain);
  return DAG.getNode(ISD::MERGE_VALUES, dl, TheCall->getVTList(),
                     &ResultVals[0], ResultVals.size()).getNode();
}

/// CreateCopyOfByValArgument - Make a copy of an aggregate at address specified
/// by "Src" to address "Dst" of size "Size".  Alignment information is
/// specified by the specific parameter attribute.  The copy will be passed as
/// a byval function parameter.
/// Sometimes what we are copying is the end of a larger object, the part that
/// does not fit in registers.
static SDValue
CreateCopyOfByValArgument(SDValue Src, SDValue Dst, SDValue Chain,
                          ISD::ArgFlagsTy Flags, SelectionDAG &DAG,
                          DebugLoc dl) {
  SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), MVT::i32);
  return DAG.getMemcpy(Chain, dl, Dst, Src, SizeNode, Flags.getByValAlign(),
                       /*AlwaysInline=*/false, NULL, 0, NULL, 0);
}

/// LowerMemOpCallTo - Store the argument to the stack.
SDValue
ARMTargetLowering::LowerMemOpCallTo(CallSDNode *TheCall, SelectionDAG &DAG,
                                    const SDValue &StackPtr,
                                    const CCValAssign &VA, SDValue Chain,
                                    SDValue Arg, ISD::ArgFlagsTy Flags) {
  DebugLoc dl = TheCall->getDebugLoc();
  unsigned LocMemOffset = VA.getLocMemOffset();
  SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset);
  PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(), StackPtr, PtrOff);
  if (Flags.isByVal()) {
    return CreateCopyOfByValArgument(Arg, PtrOff, Chain, Flags, DAG, dl);
  }
  return DAG.getStore(Chain, dl, Arg, PtrOff,
                      PseudoSourceValue::getStack(), LocMemOffset);
}

/// LowerCALL - Lowering a ISD::CALL node into a callseq_start <-
/// ARMISD:CALL <- callseq_end chain. Also add input and output parameter
/// nodes.
SDValue ARMTargetLowering::LowerCALL(SDValue Op, SelectionDAG &DAG) {
  CallSDNode *TheCall = cast<CallSDNode>(Op.getNode());
  MVT RetVT           = TheCall->getRetValType(0);
  SDValue Chain       = TheCall->getChain();
  unsigned CC         = TheCall->getCallingConv();
  assert((CC == CallingConv::C ||
          CC == CallingConv::Fast) && "unknown calling convention");
  bool isVarArg       = TheCall->isVarArg();
  SDValue Callee      = TheCall->getCallee();
  DebugLoc dl         = TheCall->getDebugLoc();

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, isVarArg, getTargetMachine(), ArgLocs);
  CCInfo.AnalyzeCallOperands(TheCall, CC_ARM);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getNextStackOffset();

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  Chain = DAG.getCALLSEQ_START(Chain, DAG.getIntPtrConstant(NumBytes, true));

  SDValue StackPtr = DAG.getRegister(ARM::SP, MVT::i32);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Walk the register/memloc assignments, inserting copies/loads.  In the case
  // of tail call optimization, arguments are handled later.
  for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size();
       i != e;
       ++i, ++realArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = TheCall->getArg(realArgIdx);
    ISD::ArgFlagsTy Flags = TheCall->getArgFlags(realArgIdx);

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: assert(0 && "Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BIT_CONVERT, dl, VA.getLocVT(), Arg);
      break;
    }

    // f64 is passed in i32 pairs and must be combined
    if (VA.needsCustom()) {
      SDValue fmrrd = DAG.getNode(ARMISD::FMRRD, dl,
                                  DAG.getVTList(MVT::i32, MVT::i32), &Arg, 1);
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), fmrrd));
      VA = ArgLocs[++i]; // skip ahead to next loc
      if (VA.isRegLoc())
        RegsToPass.push_back(std::make_pair(VA.getLocReg(), fmrrd.getValue(1)));
      else {
        assert(VA.isMemLoc());
        if (StackPtr.getNode() == 0)
          StackPtr = DAG.getCopyFromReg(Chain, dl, ARM::SP, getPointerTy());

        MemOpChains.push_back(LowerMemOpCallTo(TheCall, DAG, StackPtr, VA,
                                               Chain, fmrrd.getValue(1),
                                               Flags));
      }
    } else if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      assert(VA.isMemLoc());
      if (StackPtr.getNode() == 0)
        StackPtr = DAG.getCopyFromReg(Chain, dl, ARM::SP, getPointerTy());

      MemOpChains.push_back(LowerMemOpCallTo(TheCall, DAG, StackPtr, VA,
                                             Chain, Arg, Flags));
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                        &MemOpChains[0], MemOpChains.size());

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  bool isDirect = false;
  bool isARMFunc = false;
  bool isLocalARMFunc = false;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    GlobalValue *GV = G->getGlobal();
    isDirect = true;
    bool isExt = (GV->isDeclaration() || GV->hasWeakLinkage() ||
                  GV->hasLinkOnceLinkage());
    bool isStub = (isExt && Subtarget->isTargetDarwin()) &&
                   getTargetMachine().getRelocationModel() != Reloc::Static;
    isARMFunc = !Subtarget->isThumb() || isStub;
    // ARM call to a local ARM function is predicable.
    isLocalARMFunc = !Subtarget->isThumb() && !isExt;
    // tBX takes a register source operand.
    if (isARMFunc && Subtarget->isThumb() && !Subtarget->hasV5TOps()) {
      ARMConstantPoolValue *CPV = new ARMConstantPoolValue(GV, ARMPCLabelIndex,
                                                           ARMCP::CPStub, 4);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, getPointerTy(), 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(getPointerTy(), dl,
                           DAG.getEntryNode(), CPAddr, NULL, 0);
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
      Callee = DAG.getNode(ARMISD::PIC_ADD, dl,
                           getPointerTy(), Callee, PICLabel);
   } else
      Callee = DAG.getTargetGlobalAddress(GV, getPointerTy());
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    isDirect = true;
    bool isStub = Subtarget->isTargetDarwin() &&
                  getTargetMachine().getRelocationModel() != Reloc::Static;
    isARMFunc = !Subtarget->isThumb() || isStub;
    // tBX takes a register source operand.
    const char *Sym = S->getSymbol();
    if (isARMFunc && Subtarget->isThumb() && !Subtarget->hasV5TOps()) {
      ARMConstantPoolValue *CPV = new ARMConstantPoolValue(Sym, ARMPCLabelIndex,
                                                           ARMCP::CPStub, 4);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, getPointerTy(), 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(getPointerTy(), dl,
                           DAG.getEntryNode(), CPAddr, NULL, 0);
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
      Callee = DAG.getNode(ARMISD::PIC_ADD, dl,
                           getPointerTy(), Callee, PICLabel);
    } else
      Callee = DAG.getTargetExternalSymbol(Sym, getPointerTy());
  }

  // FIXME: handle tail calls differently.
  unsigned CallOpc;
  if (Subtarget->isThumb()) {
    if (!Subtarget->hasV5TOps() && (!isDirect || isARMFunc))
      CallOpc = ARMISD::CALL_NOLINK;
    else
      CallOpc = isARMFunc ? ARMISD::CALL : ARMISD::tCALL;
  } else {
    CallOpc = (isDirect || Subtarget->hasV5TOps())
      ? (isLocalARMFunc ? ARMISD::CALL_PRED : ARMISD::CALL)
      : ARMISD::CALL_NOLINK;
  }
  if (CallOpc == ARMISD::CALL_NOLINK && !Subtarget->isThumb()) {
    // implicit def LR - LR mustn't be allocated as GRP:$dst of CALL_NOLINK
    Chain = DAG.getCopyToReg(Chain, dl, ARM::LR, DAG.getUNDEF(MVT::i32),InFlag);
    InFlag = Chain.getValue(1);
  }

  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  if (InFlag.getNode())
    Ops.push_back(InFlag);
  // Returns a chain and a flag for retval copy to use.
  Chain = DAG.getNode(CallOpc, dl, DAG.getVTList(MVT::Other, MVT::Flag),
                      &Ops[0], Ops.size());
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, true),
                             DAG.getIntPtrConstant(0, true), InFlag);
  if (RetVT != MVT::Other)
    InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return SDValue(LowerCallResult(Chain, InFlag, TheCall, CC, DAG),
                                 Op.getResNo());
}

SDValue ARMTargetLowering::LowerRET(SDValue Op, SelectionDAG &DAG) {
  // The chain is always operand #0
  SDValue Chain = Op.getOperand(0);
  DebugLoc dl = Op.getDebugLoc();

  // CCValAssign - represent the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> RVLocs;
  unsigned CC   = DAG.getMachineFunction().getFunction()->getCallingConv();
  bool isVarArg = DAG.getMachineFunction().getFunction()->isVarArg();

  // CCState - Info about the registers and stack slots.
  CCState CCInfo(CC, isVarArg, getTargetMachine(), RVLocs);

  // Analyze return values of ISD::RET.
  CCInfo.AnalyzeReturn(Op.getNode(), RetCC_ARM);

  // If this is the first return lowered for this function, add
  // the regs to the liveout set for the function.
  if (DAG.getMachineFunction().getRegInfo().liveout_empty()) {
    for (unsigned i = 0; i != RVLocs.size(); ++i)
      if (RVLocs[i].isRegLoc())
        DAG.getMachineFunction().getRegInfo().addLiveOut(RVLocs[i].getLocReg());
  }

  SDValue Flag;

  // Copy the result values into the output registers.
  for (unsigned i = 0, realRVLocIdx = 0;
       i != RVLocs.size();
       ++i, ++realRVLocIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    // ISD::RET => ret chain, (regnum1,val1), ...
    // So i*2+1 index only the regnums
    SDValue Arg = Op.getOperand(realRVLocIdx*2+1);

    switch (VA.getLocInfo()) {
    default: assert(0 && "Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BIT_CONVERT, dl, VA.getLocVT(), Arg);
      break;
    }

    // Legalize ret f64 -> ret 2 x i32.  We always have fmrrd if f64 is
    // available.
    if (VA.needsCustom()) {
      SDValue fmrrd = DAG.getNode(ARMISD::FMRRD, dl,
                                  DAG.getVTList(MVT::i32, MVT::i32), &Arg, 1);
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), fmrrd, Flag);
      Flag = Chain.getValue(1);
      VA = RVLocs[++i]; // skip ahead to next loc
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), fmrrd.getValue(1),
                               Flag);
    } else
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Arg, Flag);

    // Guarantee that all emitted copies are
    // stuck together, avoiding something bad.
    Flag = Chain.getValue(1);
  }

  SDValue result;
  if (Flag.getNode())
    result = DAG.getNode(ARMISD::RET_FLAG, dl, MVT::Other, Chain, Flag);
  else // Return Void
    result = DAG.getNode(ARMISD::RET_FLAG, dl, MVT::Other, Chain);

  return result;
}

// ConstantPool, JumpTable, GlobalAddress, and ExternalSymbol are lowered as
// their target countpart wrapped in the ARMISD::Wrapper node. Suppose N is
// one of the above mentioned nodes. It has to be wrapped because otherwise
// Select(N) returns N. So the raw TargetGlobalAddress nodes, etc. can only
// be used to form addressing mode. These wrapped nodes will be selected
// into MOVi.
static SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) {
  MVT PtrVT = Op.getValueType();
  // FIXME there is no actual debug info here
  DebugLoc dl = Op.getDebugLoc();
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  SDValue Res;
  if (CP->isMachineConstantPoolEntry())
    Res = DAG.getTargetConstantPool(CP->getMachineCPVal(), PtrVT,
                                    CP->getAlignment());
  else
    Res = DAG.getTargetConstantPool(CP->getConstVal(), PtrVT,
                                    CP->getAlignment());
  return DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Res);
}

// Lower ISD::GlobalTLSAddress using the "general dynamic" model
SDValue
ARMTargetLowering::LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
                                                 SelectionDAG &DAG) {
  DebugLoc dl = GA->getDebugLoc();
  MVT PtrVT = getPointerTy();
  unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
  ARMConstantPoolValue *CPV =
    new ARMConstantPoolValue(GA->getGlobal(), ARMPCLabelIndex, ARMCP::CPValue,
                             PCAdj, "tlsgd", true);
  SDValue Argument = DAG.getTargetConstantPool(CPV, PtrVT, 4);
  Argument = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Argument);
  Argument = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Argument, NULL, 0);
  SDValue Chain = Argument.getValue(1);

  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
  Argument = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Argument, PICLabel);

  // call __tls_get_addr.
  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Argument;
  Entry.Ty = (const Type *) Type::Int32Ty;
  Args.push_back(Entry);
  // FIXME: is there useful debug info available here?
  std::pair<SDValue, SDValue> CallResult =
    LowerCallTo(Chain, (const Type *) Type::Int32Ty, false, false, false, false,
                CallingConv::C, false,
                DAG.getExternalSymbol("__tls_get_addr", PtrVT), Args, DAG, dl);
  return CallResult.first;
}

// Lower ISD::GlobalTLSAddress using the "initial exec" or
// "local exec" model.
SDValue
ARMTargetLowering::LowerToTLSExecModels(GlobalAddressSDNode *GA,
                                        SelectionDAG &DAG) {
  GlobalValue *GV = GA->getGlobal();
  DebugLoc dl = GA->getDebugLoc();
  SDValue Offset;
  SDValue Chain = DAG.getEntryNode();
  MVT PtrVT = getPointerTy();
  // Get the Thread Pointer
  SDValue ThreadPointer = DAG.getNode(ARMISD::THREAD_POINTER, dl, PtrVT);

  if (GV->isDeclaration()){
    // initial exec model
    unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
    ARMConstantPoolValue *CPV =
      new ARMConstantPoolValue(GA->getGlobal(), ARMPCLabelIndex, ARMCP::CPValue,
                               PCAdj, "gottpoff", true);
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(PtrVT, dl, Chain, Offset, NULL, 0);
    Chain = Offset.getValue(1);

    SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
    Offset = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Offset, PICLabel);

    Offset = DAG.getLoad(PtrVT, dl, Chain, Offset, NULL, 0);
  } else {
    // local exec model
    ARMConstantPoolValue *CPV =
      new ARMConstantPoolValue(GV, ARMCP::CPValue, "tpoff");
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(PtrVT, dl, Chain, Offset, NULL, 0);
  }

  // The address of the thread local variable is the add of the thread
  // pointer with the offset of the variable.
  return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer, Offset);
}

SDValue
ARMTargetLowering::LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) {
  // TODO: implement the "local dynamic" model
  assert(Subtarget->isTargetELF() &&
         "TLS not implemented for non-ELF targets");
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  // If the relocation model is PIC, use the "General Dynamic" TLS Model,
  // otherwise use the "Local Exec" TLS Model
  if (getTargetMachine().getRelocationModel() == Reloc::PIC_)
    return LowerToTLSGeneralDynamicModel(GA, DAG);
  else
    return LowerToTLSExecModels(GA, DAG);
}

SDValue ARMTargetLowering::LowerGlobalAddressELF(SDValue Op,
                                                 SelectionDAG &DAG) {
  MVT PtrVT = getPointerTy();
  DebugLoc dl = Op.getDebugLoc();
  GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  Reloc::Model RelocM = getTargetMachine().getRelocationModel();
  if (RelocM == Reloc::PIC_) {
    bool UseGOTOFF = GV->hasLocalLinkage() || GV->hasHiddenVisibility();
    ARMConstantPoolValue *CPV =
      new ARMConstantPoolValue(GV, ARMCP::CPValue, UseGOTOFF ? "GOTOFF":"GOT");
    SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    SDValue Result = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(),
                                 CPAddr, NULL, 0);
    SDValue Chain = Result.getValue(1);
    SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(PtrVT);
    Result = DAG.getNode(ISD::ADD, dl, PtrVT, Result, GOT);
    if (!UseGOTOFF)
      Result = DAG.getLoad(PtrVT, dl, Chain, Result, NULL, 0);
    return Result;
  } else {
    SDValue CPAddr = DAG.getTargetConstantPool(GV, PtrVT, 4);
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), CPAddr, NULL, 0);
  }
}

/// GVIsIndirectSymbol - true if the GV will be accessed via an indirect symbol
/// even in non-static mode.
static bool GVIsIndirectSymbol(GlobalValue *GV, Reloc::Model RelocM) {
  // If symbol visibility is hidden, the extra load is not needed if
  // the symbol is definitely defined in the current translation unit.
  bool isDecl = GV->isDeclaration() && !GV->hasNotBeenReadFromBitcode();
  if (GV->hasHiddenVisibility() && (!isDecl && !GV->hasCommonLinkage()))
    return false;
  return RelocM != Reloc::Static && (isDecl || GV->isWeakForLinker());
}

SDValue ARMTargetLowering::LowerGlobalAddressDarwin(SDValue Op,
                                                    SelectionDAG &DAG) {
  MVT PtrVT = getPointerTy();
  DebugLoc dl = Op.getDebugLoc();
  GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  Reloc::Model RelocM = getTargetMachine().getRelocationModel();
  bool IsIndirect = GVIsIndirectSymbol(GV, RelocM);
  SDValue CPAddr;
  if (RelocM == Reloc::Static)
    CPAddr = DAG.getTargetConstantPool(GV, PtrVT, 4);
  else {
    unsigned PCAdj = (RelocM != Reloc::PIC_)
      ? 0 : (Subtarget->isThumb() ? 4 : 8);
    ARMCP::ARMCPKind Kind = IsIndirect ? ARMCP::CPNonLazyPtr
      : ARMCP::CPValue;
    ARMConstantPoolValue *CPV = new ARMConstantPoolValue(GV, ARMPCLabelIndex,
                                                         Kind, PCAdj);
    CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
  }
  CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);

  SDValue Result = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), CPAddr, NULL, 0);
  SDValue Chain = Result.getValue(1);

  if (RelocM == Reloc::PIC_) {
    SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
    Result = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Result, PICLabel);
  }
  if (IsIndirect)
    Result = DAG.getLoad(PtrVT, dl, Chain, Result, NULL, 0);

  return Result;
}

SDValue ARMTargetLowering::LowerGLOBAL_OFFSET_TABLE(SDValue Op,
                                                    SelectionDAG &DAG){
  assert(Subtarget->isTargetELF() &&
         "GLOBAL OFFSET TABLE not implemented for non-ELF targets");
  MVT PtrVT = getPointerTy();
  DebugLoc dl = Op.getDebugLoc();
  unsigned PCAdj = Subtarget->isThumb() ? 4 : 8;
  ARMConstantPoolValue *CPV = new ARMConstantPoolValue("_GLOBAL_OFFSET_TABLE_",
                                                       ARMPCLabelIndex,
                                                       ARMCP::CPValue, PCAdj);
  SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
  CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
  SDValue Result = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), CPAddr, NULL, 0);
  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex++, MVT::i32);
  return DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Result, PICLabel);
}

static SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) {
  MVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy();
  unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  switch (IntNo) {
  default: return SDValue();    // Don't custom lower most intrinsics.
  case Intrinsic::arm_thread_pointer:
      return DAG.getNode(ARMISD::THREAD_POINTER, DebugLoc::getUnknownLoc(),
                         PtrVT);
  }
}

static SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG,
                            unsigned VarArgsFrameIndex) {
  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  DebugLoc dl = Op.getDebugLoc();
  MVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy();
  SDValue FR = DAG.getFrameIndex(VarArgsFrameIndex, PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1), SV, 0);
}

SDValue
ARMTargetLowering::LowerFORMAL_ARGUMENTS(SDValue Op, SelectionDAG &DAG) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();

  SDValue Root = Op.getOperand(0);
  DebugLoc dl = Op.getDebugLoc();
  bool isVarArg = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue() != 0;
  unsigned CC = MF.getFunction()->getCallingConv();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, isVarArg, getTargetMachine(), ArgLocs);
  CCInfo.AnalyzeFormalArguments(Op.getNode(), CC_ARM);

  SmallVector<SDValue, 16> ArgValues;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    // Arguments stored in registers.
    if (VA.isRegLoc()) {
      MVT RegVT = VA.getLocVT();
      TargetRegisterClass *RC;
      if (AFI->isThumbFunction())
        RC = ARM::tGPRRegisterClass;
      else
        RC = ARM::GPRRegisterClass;

      if (RegVT == MVT::f64) {
        // f64 is passed in pairs of GPRs and must be combined.
        RegVT = MVT::i32;
      } else if (!((RegVT == MVT::i32) || (RegVT == MVT::f32)))
        assert(0 && "RegVT not supported by FORMAL_ARGUMENTS Lowering");

      // Transform the arguments stored in physical registers into virtual ones.
      unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
      SDValue ArgValue = DAG.getCopyFromReg(Root, dl, Reg, RegVT);

      // f64 is passed in i32 pairs and must be combined.
      if (VA.needsCustom()) {
        SDValue ArgValue2;

        VA = ArgLocs[++i]; // skip ahead to next loc
        if (VA.isMemLoc()) {
          // must be APCS to split like this
          unsigned ArgSize = VA.getLocVT().getSizeInBits()/8;
          int FI = MFI->CreateFixedObject(ArgSize, VA.getLocMemOffset());

          // Create load node to retrieve arguments from the stack.
          SDValue FIN = DAG.getFrameIndex(FI, getPointerTy());
          ArgValue2 = DAG.getLoad(MVT::i32, dl, Root, FIN, NULL, 0);
        } else {
          Reg = MF.addLiveIn(VA.getLocReg(), RC);
          ArgValue2 = DAG.getCopyFromReg(Root, dl, Reg, MVT::i32);
        }

        ArgValue = DAG.getNode(ARMISD::FMDRR, dl, MVT::f64,
                               ArgValue, ArgValue2);
      }

      // If this is an 8 or 16-bit value, it is really passed promoted
      // to 32 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      switch (VA.getLocInfo()) {
      default: assert(0 && "Unknown loc info!");
      case CCValAssign::Full: break;
      case CCValAssign::BCvt:
        ArgValue = DAG.getNode(ISD::BIT_CONVERT, dl, VA.getValVT(), ArgValue);
        break;
      case CCValAssign::SExt:
        ArgValue = DAG.getNode(ISD::AssertSext, dl, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);
        break;
      case CCValAssign::ZExt:
        ArgValue = DAG.getNode(ISD::AssertZext, dl, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);
        break;
      }

      ArgValues.push_back(ArgValue);

    } else { // VA.isRegLoc()

      // sanity check
      assert(VA.isMemLoc());
      assert(VA.getValVT() != MVT::i64 && "i64 should already be lowered");

      unsigned ArgSize = VA.getLocVT().getSizeInBits()/8;
      int FI = MFI->CreateFixedObject(ArgSize, VA.getLocMemOffset());

      // Create load nodes to retrieve arguments from the stack.
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy());
      ArgValues.push_back(DAG.getLoad(VA.getValVT(), dl, Root, FIN, NULL, 0));
    }
  }

  // varargs
  if (isVarArg) {
    static const unsigned GPRArgRegs[] = {
      ARM::R0, ARM::R1, ARM::R2, ARM::R3
    };

    unsigned NumGPRs = CCInfo.getFirstUnallocated
      (GPRArgRegs, sizeof(GPRArgRegs) / sizeof(GPRArgRegs[0]));

    unsigned Align = MF.getTarget().getFrameInfo()->getStackAlignment();
    unsigned VARegSize = (4 - NumGPRs) * 4;
    unsigned VARegSaveSize = (VARegSize + Align - 1) & ~(Align - 1);
    unsigned ArgOffset = 0;
    if (VARegSaveSize) {
      // If this function is vararg, store any remaining integer argument regs
      // to their spots on the stack so that they may be loaded by deferencing
      // the result of va_next.
      AFI->setVarArgsRegSaveSize(VARegSaveSize);
      ArgOffset = CCInfo.getNextStackOffset();
      VarArgsFrameIndex = MFI->CreateFixedObject(VARegSaveSize, ArgOffset +
                                                 VARegSaveSize - VARegSize);
      SDValue FIN = DAG.getFrameIndex(VarArgsFrameIndex, getPointerTy());

      SmallVector<SDValue, 4> MemOps;
      for (; NumGPRs < 4; ++NumGPRs) {
        TargetRegisterClass *RC;
        if (AFI->isThumbFunction())
          RC = ARM::tGPRRegisterClass;
        else
          RC = ARM::GPRRegisterClass;

        unsigned VReg = MF.addLiveIn(GPRArgRegs[NumGPRs], RC);
        SDValue Val = DAG.getCopyFromReg(Root, dl, VReg, MVT::i32);
        SDValue Store = DAG.getStore(Val.getValue(1), dl, Val, FIN, NULL, 0);
        MemOps.push_back(Store);
        FIN = DAG.getNode(ISD::ADD, dl, getPointerTy(), FIN,
                          DAG.getConstant(4, getPointerTy()));
      }
      if (!MemOps.empty())
        Root = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                           &MemOps[0], MemOps.size());
    } else
      // This will point to the next argument passed via stack.
      VarArgsFrameIndex = MFI->CreateFixedObject(4, ArgOffset);
  }

  ArgValues.push_back(Root);

  // Return the new list of results.
  return DAG.getNode(ISD::MERGE_VALUES, dl, Op.getNode()->getVTList(),
                     &ArgValues[0], ArgValues.size()).getValue(Op.getResNo());
}

/// isFloatingPointZero - Return true if this is +0.0.
static bool isFloatingPointZero(SDValue Op) {
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op))
    return CFP->getValueAPF().isPosZero();
  else if (ISD::isEXTLoad(Op.getNode()) || ISD::isNON_EXTLoad(Op.getNode())) {
    // Maybe this has already been legalized into the constant pool?
    if (Op.getOperand(1).getOpcode() == ARMISD::Wrapper) {
      SDValue WrapperOp = Op.getOperand(1).getOperand(0);
      if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(WrapperOp))
        if (ConstantFP *CFP = dyn_cast<ConstantFP>(CP->getConstVal()))
          return CFP->getValueAPF().isPosZero();
    }
  }
  return false;
}

static bool isLegalCmpImmediate(unsigned C, bool isThumb) {
  return ( isThumb && (C & ~255U) == 0) ||
         (!isThumb && ARM_AM::getSOImmVal(C) != -1);
}

/// Returns appropriate ARM CMP (cmp) and corresponding condition code for
/// the given operands.
static SDValue getARMCmp(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                         SDValue &ARMCC, SelectionDAG &DAG, bool isThumb,
                         DebugLoc dl) {
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS.getNode())) {
    unsigned C = RHSC->getZExtValue();
    if (!isLegalCmpImmediate(C, isThumb)) {
      // Constant does not fit, try adjusting it by one?
      switch (CC) {
      default: break;
      case ISD::SETLT:
      case ISD::SETGE:
        if (isLegalCmpImmediate(C-1, isThumb)) {
          CC = (CC == ISD::SETLT) ? ISD::SETLE : ISD::SETGT;
          RHS = DAG.getConstant(C-1, MVT::i32);
        }
        break;
      case ISD::SETULT:
      case ISD::SETUGE:
        if (C > 0 && isLegalCmpImmediate(C-1, isThumb)) {
          CC = (CC == ISD::SETULT) ? ISD::SETULE : ISD::SETUGT;
          RHS = DAG.getConstant(C-1, MVT::i32);
        }
        break;
      case ISD::SETLE:
      case ISD::SETGT:
        if (isLegalCmpImmediate(C+1, isThumb)) {
          CC = (CC == ISD::SETLE) ? ISD::SETLT : ISD::SETGE;
          RHS = DAG.getConstant(C+1, MVT::i32);
        }
        break;
      case ISD::SETULE:
      case ISD::SETUGT:
        if (C < 0xffffffff && isLegalCmpImmediate(C+1, isThumb)) {
          CC = (CC == ISD::SETULE) ? ISD::SETULT : ISD::SETUGE;
          RHS = DAG.getConstant(C+1, MVT::i32);
        }
        break;
      }
    }
  }

  ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
  ARMISD::NodeType CompareType;
  switch (CondCode) {
  default:
    CompareType = ARMISD::CMP;
    break;
  case ARMCC::EQ:
  case ARMCC::NE:
  case ARMCC::MI:
  case ARMCC::PL:
    // Uses only N and Z Flags
    CompareType = ARMISD::CMPNZ;
    break;
  }
  ARMCC = DAG.getConstant(CondCode, MVT::i32);
  return DAG.getNode(CompareType, dl, MVT::Flag, LHS, RHS);
}

/// Returns a appropriate VFP CMP (fcmp{s|d}+fmstat) for the given operands.
static SDValue getVFPCmp(SDValue LHS, SDValue RHS, SelectionDAG &DAG,
                         DebugLoc dl) {
  SDValue Cmp;
  if (!isFloatingPointZero(RHS))
    Cmp = DAG.getNode(ARMISD::CMPFP, dl, MVT::Flag, LHS, RHS);
  else
    Cmp = DAG.getNode(ARMISD::CMPFPw0, dl, MVT::Flag, LHS);
  return DAG.getNode(ARMISD::FMSTAT, dl, MVT::Flag, Cmp);
}

static SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG,
                              const ARMSubtarget *ST) {
  MVT VT = Op.getValueType();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);
  DebugLoc dl = Op.getDebugLoc();

  if (LHS.getValueType() == MVT::i32) {
    SDValue ARMCC;
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMCC, DAG, ST->isThumb(), dl);
    return DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal, ARMCC, CCR,Cmp);
  }

  ARMCC::CondCodes CondCode, CondCode2;
  if (FPCCToARMCC(CC, CondCode, CondCode2))
    std::swap(TrueVal, FalseVal);

  SDValue ARMCC = DAG.getConstant(CondCode, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl);
  SDValue Result = DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal,
                                 ARMCC, CCR, Cmp);
  if (CondCode2 != ARMCC::AL) {
    SDValue ARMCC2 = DAG.getConstant(CondCode2, MVT::i32);
    // FIXME: Needs another CMP because flag can have but one use.
    SDValue Cmp2 = getVFPCmp(LHS, RHS, DAG, dl);
    Result = DAG.getNode(ARMISD::CMOV, dl, VT,
                         Result, TrueVal, ARMCC2, CCR, Cmp2);
  }
  return Result;
}

static SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG,
                          const ARMSubtarget *ST) {
  SDValue  Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue    LHS = Op.getOperand(2);
  SDValue    RHS = Op.getOperand(3);
  SDValue   Dest = Op.getOperand(4);
  DebugLoc dl = Op.getDebugLoc();

  if (LHS.getValueType() == MVT::i32) {
    SDValue ARMCC;
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMCC, DAG, ST->isThumb(), dl);
    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other,
                       Chain, Dest, ARMCC, CCR,Cmp);
  }

  assert(LHS.getValueType() == MVT::f32 || LHS.getValueType() == MVT::f64);
  ARMCC::CondCodes CondCode, CondCode2;
  if (FPCCToARMCC(CC, CondCode, CondCode2))
    // Swap the LHS/RHS of the comparison if needed.
    std::swap(LHS, RHS);

  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl);
  SDValue ARMCC = DAG.getConstant(CondCode, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDVTList VTList = DAG.getVTList(MVT::Other, MVT::Flag);
  SDValue Ops[] = { Chain, Dest, ARMCC, CCR, Cmp };
  SDValue Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops, 5);
  if (CondCode2 != ARMCC::AL) {
    ARMCC = DAG.getConstant(CondCode2, MVT::i32);
    SDValue Ops[] = { Res, Dest, ARMCC, CCR, Res.getValue(1) };
    Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops, 5);
  }
  return Res;
}

SDValue ARMTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) {
  SDValue Chain = Op.getOperand(0);
  SDValue Table = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);
  DebugLoc dl = Op.getDebugLoc();

  MVT PTy = getPointerTy();
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Table);
  ARMFunctionInfo *AFI = DAG.getMachineFunction().getInfo<ARMFunctionInfo>();
  SDValue UId =  DAG.getConstant(AFI->createJumpTableUId(), PTy);
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), PTy);
  Table = DAG.getNode(ARMISD::WrapperJT, dl, MVT::i32, JTI, UId);
  Index = DAG.getNode(ISD::MUL, dl, PTy, Index, DAG.getConstant(4, PTy));
  SDValue Addr = DAG.getNode(ISD::ADD, dl, PTy, Index, Table);
  bool isPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;
  Addr = DAG.getLoad(isPIC ? (MVT)MVT::i32 : PTy, dl,
                     Chain, Addr, NULL, 0);
  Chain = Addr.getValue(1);
  if (isPIC)
    Addr = DAG.getNode(ISD::ADD, dl, PTy, Addr, Table);
  return DAG.getNode(ARMISD::BR_JT, dl, MVT::Other, Chain, Addr, JTI, UId);
}

static SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) {
  DebugLoc dl = Op.getDebugLoc();
  unsigned Opc =
    Op.getOpcode() == ISD::FP_TO_SINT ? ARMISD::FTOSI : ARMISD::FTOUI;
  Op = DAG.getNode(Opc, dl, MVT::f32, Op.getOperand(0));
  return DAG.getNode(ISD::BIT_CONVERT, dl, MVT::i32, Op);
}

static SDValue LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) {
  MVT VT = Op.getValueType();
  DebugLoc dl = Op.getDebugLoc();
  unsigned Opc =
    Op.getOpcode() == ISD::SINT_TO_FP ? ARMISD::SITOF : ARMISD::UITOF;

  Op = DAG.getNode(ISD::BIT_CONVERT, dl, MVT::f32, Op.getOperand(0));
  return DAG.getNode(Opc, dl, VT, Op);
}

static SDValue LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) {
  // Implement fcopysign with a fabs and a conditional fneg.
  SDValue Tmp0 = Op.getOperand(0);
  SDValue Tmp1 = Op.getOperand(1);
  DebugLoc dl = Op.getDebugLoc();
  MVT VT = Op.getValueType();
  MVT SrcVT = Tmp1.getValueType();
  SDValue AbsVal = DAG.getNode(ISD::FABS, dl, VT, Tmp0);
  SDValue Cmp = getVFPCmp(Tmp1, DAG.getConstantFP(0.0, SrcVT), DAG, dl);
  SDValue ARMCC = DAG.getConstant(ARMCC::LT, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  return DAG.getNode(ARMISD::CNEG, dl, VT, AbsVal, AbsVal, ARMCC, CCR, Cmp);
}

SDValue
ARMTargetLowering::EmitTargetCodeForMemcpy(SelectionDAG &DAG, DebugLoc dl,
                                           SDValue Chain,
                                           SDValue Dst, SDValue Src,
                                           SDValue Size, unsigned Align,
                                           bool AlwaysInline,
                                         const Value *DstSV, uint64_t DstSVOff,
                                         const Value *SrcSV, uint64_t SrcSVOff){
  // Do repeated 4-byte loads and stores. To be improved.
  // This requires 4-byte alignment.
  if ((Align & 3) != 0)
    return SDValue();
  // This requires the copy size to be a constant, preferrably
  // within a subtarget-specific limit.
  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  if (!ConstantSize)
    return SDValue();
  uint64_t SizeVal = ConstantSize->getZExtValue();
  if (!AlwaysInline && SizeVal > getSubtarget()->getMaxInlineSizeThreshold())
    return SDValue();

  unsigned BytesLeft = SizeVal & 3;
  unsigned NumMemOps = SizeVal >> 2;
  unsigned EmittedNumMemOps = 0;
  MVT VT = MVT::i32;
  unsigned VTSize = 4;
  unsigned i = 0;
  const unsigned MAX_LOADS_IN_LDM = 6;
  SDValue TFOps[MAX_LOADS_IN_LDM];
  SDValue Loads[MAX_LOADS_IN_LDM];
  uint64_t SrcOff = 0, DstOff = 0;

  // Emit up to MAX_LOADS_IN_LDM loads, then a TokenFactor barrier, then the
  // same number of stores.  The loads and stores will get combined into
  // ldm/stm later on.
  while (EmittedNumMemOps < NumMemOps) {
    for (i = 0;
         i < MAX_LOADS_IN_LDM && EmittedNumMemOps + i < NumMemOps; ++i) {
      Loads[i] = DAG.getLoad(VT, dl, Chain,
                             DAG.getNode(ISD::ADD, dl, MVT::i32, Src,
                                         DAG.getConstant(SrcOff, MVT::i32)),
                             SrcSV, SrcSVOff + SrcOff);
      TFOps[i] = Loads[i].getValue(1);
      SrcOff += VTSize;
    }
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &TFOps[0], i);

    for (i = 0;
         i < MAX_LOADS_IN_LDM && EmittedNumMemOps + i < NumMemOps; ++i) {
      TFOps[i] = DAG.getStore(Chain, dl, Loads[i],
                           DAG.getNode(ISD::ADD, dl, MVT::i32, Dst,
                                       DAG.getConstant(DstOff, MVT::i32)),
                           DstSV, DstSVOff + DstOff);
      DstOff += VTSize;
    }
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &TFOps[0], i);

    EmittedNumMemOps += i;
  }

  if (BytesLeft == 0)
    return Chain;

  // Issue loads / stores for the trailing (1 - 3) bytes.
  unsigned BytesLeftSave = BytesLeft;
  i = 0;
  while (BytesLeft) {
    if (BytesLeft >= 2) {
      VT = MVT::i16;
      VTSize = 2;
    } else {
      VT = MVT::i8;
      VTSize = 1;
    }

    Loads[i] = DAG.getLoad(VT, dl, Chain,
                           DAG.getNode(ISD::ADD, dl, MVT::i32, Src,
                                       DAG.getConstant(SrcOff, MVT::i32)),
                           SrcSV, SrcSVOff + SrcOff);
    TFOps[i] = Loads[i].getValue(1);
    ++i;
    SrcOff += VTSize;
    BytesLeft -= VTSize;
  }
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &TFOps[0], i);

  i = 0;
  BytesLeft = BytesLeftSave;
  while (BytesLeft) {
    if (BytesLeft >= 2) {
      VT = MVT::i16;
      VTSize = 2;
    } else {
      VT = MVT::i8;
      VTSize = 1;
    }

    TFOps[i] = DAG.getStore(Chain, dl, Loads[i],
                            DAG.getNode(ISD::ADD, dl, MVT::i32, Dst,
                                        DAG.getConstant(DstOff, MVT::i32)),
                            DstSV, DstSVOff + DstOff);
    ++i;
    DstOff += VTSize;
    BytesLeft -= VTSize;
  }
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, &TFOps[0], i);
}

static SDValue ExpandBIT_CONVERT(SDNode *N, SelectionDAG &DAG) {
  SDValue Op = N->getOperand(0);
  DebugLoc dl = N->getDebugLoc();
  if (N->getValueType(0) == MVT::f64) {
    // Turn i64->f64 into FMDRR.
    SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, Op,
                             DAG.getConstant(0, MVT::i32));
    SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, Op,
                             DAG.getConstant(1, MVT::i32));
    return DAG.getNode(ARMISD::FMDRR, dl, MVT::f64, Lo, Hi);
  }

  // Turn f64->i64 into FMRRD.
  SDValue Cvt = DAG.getNode(ARMISD::FMRRD, dl,
                            DAG.getVTList(MVT::i32, MVT::i32), &Op, 1);

  // Merge the pieces into a single i64 value.
  return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Cvt, Cvt.getValue(1));
}

static SDValue ExpandSRx(SDNode *N, SelectionDAG &DAG, const ARMSubtarget *ST) {
  assert(N->getValueType(0) == MVT::i64 &&
         (N->getOpcode() == ISD::SRL || N->getOpcode() == ISD::SRA) &&
         "Unknown shift to lower!");

  // We only lower SRA, SRL of 1 here, all others use generic lowering.
  if (!isa<ConstantSDNode>(N->getOperand(1)) ||
      cast<ConstantSDNode>(N->getOperand(1))->getZExtValue() != 1)
    return SDValue();

  // If we are in thumb mode, we don't have RRX.
  if (ST->isThumb()) return SDValue();

  // Okay, we have a 64-bit SRA or SRL of 1.  Lower this to an RRX expr.
  DebugLoc dl = N->getDebugLoc();
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, N->getOperand(0),
                             DAG.getConstant(0, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, N->getOperand(0),
                             DAG.getConstant(1, MVT::i32));

  // First, build a SRA_FLAG/SRL_FLAG op, which shifts the top part by one and
  // captures the result into a carry flag.
  unsigned Opc = N->getOpcode() == ISD::SRL ? ARMISD::SRL_FLAG:ARMISD::SRA_FLAG;
  Hi = DAG.getNode(Opc, dl, DAG.getVTList(MVT::i32, MVT::Flag), &Hi, 1);

  // The low part is an ARMISD::RRX operand, which shifts the carry in.
  Lo = DAG.getNode(ARMISD::RRX, dl, MVT::i32, Lo, Hi.getValue(1));

  // Merge the pieces into a single i64 value.
 return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
}

SDValue ARMTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) {
  switch (Op.getOpcode()) {
  default: assert(0 && "Don't know how to custom lower this!"); abort();
  case ISD::ConstantPool:  return LowerConstantPool(Op, DAG);
  case ISD::GlobalAddress:
    return Subtarget->isTargetDarwin() ? LowerGlobalAddressDarwin(Op, DAG) :
      LowerGlobalAddressELF(Op, DAG);
  case ISD::GlobalTLSAddress:   return LowerGlobalTLSAddress(Op, DAG);
  case ISD::CALL:          return LowerCALL(Op, DAG);
  case ISD::RET:           return LowerRET(Op, DAG);
  case ISD::SELECT_CC:     return LowerSELECT_CC(Op, DAG, Subtarget);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG, Subtarget);
  case ISD::BR_JT:         return LowerBR_JT(Op, DAG);
  case ISD::VASTART:       return LowerVASTART(Op, DAG, VarArgsFrameIndex);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:    return LowerINT_TO_FP(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:    return LowerFP_TO_INT(Op, DAG);
  case ISD::FCOPYSIGN:     return LowerFCOPYSIGN(Op, DAG);
  case ISD::FORMAL_ARGUMENTS: return LowerFORMAL_ARGUMENTS(Op, DAG);
  case ISD::RETURNADDR:    break;
  case ISD::FRAMEADDR:     break;
  case ISD::GLOBAL_OFFSET_TABLE: return LowerGLOBAL_OFFSET_TABLE(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::BIT_CONVERT:   return ExpandBIT_CONVERT(Op.getNode(), DAG);
  case ISD::SRL:
  case ISD::SRA:           return ExpandSRx(Op.getNode(), DAG,Subtarget);
  }
  return SDValue();
}

/// ReplaceNodeResults - Replace the results of node with an illegal result
/// type with new values built out of custom code.
void ARMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue>&Results,
                                           SelectionDAG &DAG) {
  switch (N->getOpcode()) {
  default:
    assert(0 && "Don't know how to custom expand this!");
    return;
  case ISD::BIT_CONVERT:
    Results.push_back(ExpandBIT_CONVERT(N, DAG));
    return;
  case ISD::SRL:
  case ISD::SRA: {
    SDValue Res = ExpandSRx(N, DAG, Subtarget);
    if (Res.getNode())
      Results.push_back(Res);
    return;
  }
  }
}

//===----------------------------------------------------------------------===//
//                           ARM Scheduler Hooks
//===----------------------------------------------------------------------===//

MachineBasicBlock *
ARMTargetLowering::EmitInstrWithCustomInserter(MachineInstr *MI,
                                               MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = getTargetMachine().getInstrInfo();
  DebugLoc dl = MI->getDebugLoc();
  switch (MI->getOpcode()) {
  default: assert(false && "Unexpected instr type to insert");
  case ARM::tMOVCCr: {
    // To "insert" a SELECT_CC instruction, we actually have to insert the
    // diamond control-flow pattern.  The incoming instruction knows the
    // destination vreg to set, the condition code register to branch on, the
    // true/false values to select between, and a branch opcode to use.
    const BasicBlock *LLVM_BB = BB->getBasicBlock();
    MachineFunction::iterator It = BB;
    ++It;

    //  thisMBB:
    //  ...
    //   TrueVal = ...
    //   cmpTY ccX, r1, r2
    //   bCC copy1MBB
    //   fallthrough --> copy0MBB
    MachineBasicBlock *thisMBB  = BB;
    MachineFunction *F = BB->getParent();
    MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
    BuildMI(BB, dl, TII->get(ARM::tBcc)).addMBB(sinkMBB)
      .addImm(MI->getOperand(3).getImm()).addReg(MI->getOperand(4).getReg());
    F->insert(It, copy0MBB);
    F->insert(It, sinkMBB);
    // Update machine-CFG edges by first adding all successors of the current
    // block to the new block which will contain the Phi node for the select.
    for(MachineBasicBlock::succ_iterator i = BB->succ_begin(),
        e = BB->succ_end(); i != e; ++i)
      sinkMBB->addSuccessor(*i);
    // Next, remove all successors of the current block, and add the true
    // and fallthrough blocks as its successors.
    while(!BB->succ_empty())
      BB->removeSuccessor(BB->succ_begin());
    BB->addSuccessor(copy0MBB);
    BB->addSuccessor(sinkMBB);

    //  copy0MBB:
    //   %FalseValue = ...
    //   # fallthrough to sinkMBB
    BB = copy0MBB;

    // Update machine-CFG edges
    BB->addSuccessor(sinkMBB);

    //  sinkMBB:
    //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
    //  ...
    BB = sinkMBB;
    BuildMI(BB, dl, TII->get(ARM::PHI), MI->getOperand(0).getReg())
      .addReg(MI->getOperand(1).getReg()).addMBB(copy0MBB)
      .addReg(MI->getOperand(2).getReg()).addMBB(thisMBB);

    F->DeleteMachineInstr(MI);   // The pseudo instruction is gone now.
    return BB;
  }
  }
}

//===----------------------------------------------------------------------===//
//                           ARM Optimization Hooks
//===----------------------------------------------------------------------===//

static
SDValue combineSelectAndUse(SDNode *N, SDValue Slct, SDValue OtherOp,
                            TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  MVT VT = N->getValueType(0);
  unsigned Opc = N->getOpcode();
  bool isSlctCC = Slct.getOpcode() == ISD::SELECT_CC;
  SDValue LHS = isSlctCC ? Slct.getOperand(2) : Slct.getOperand(1);
  SDValue RHS = isSlctCC ? Slct.getOperand(3) : Slct.getOperand(2);
  ISD::CondCode CC = ISD::SETCC_INVALID;

  if (isSlctCC) {
    CC = cast<CondCodeSDNode>(Slct.getOperand(4))->get();
  } else {
    SDValue CCOp = Slct.getOperand(0);
    if (CCOp.getOpcode() == ISD::SETCC)
      CC = cast<CondCodeSDNode>(CCOp.getOperand(2))->get();
  }

  bool DoXform = false;
  bool InvCC = false;
  assert ((Opc == ISD::ADD || (Opc == ISD::SUB && Slct == N->getOperand(1))) &&
          "Bad input!");

  if (LHS.getOpcode() == ISD::Constant &&
      cast<ConstantSDNode>(LHS)->isNullValue()) {
    DoXform = true;
  } else if (CC != ISD::SETCC_INVALID &&
             RHS.getOpcode() == ISD::Constant &&
             cast<ConstantSDNode>(RHS)->isNullValue()) {
    std::swap(LHS, RHS);
    SDValue Op0 = Slct.getOperand(0);
    MVT OpVT = isSlctCC ? Op0.getValueType() :
                          Op0.getOperand(0).getValueType();
    bool isInt = OpVT.isInteger();
    CC = ISD::getSetCCInverse(CC, isInt);

    if (!TLI.isCondCodeLegal(CC, OpVT))
      return SDValue();         // Inverse operator isn't legal.

    DoXform = true;
    InvCC = true;
  }

  if (DoXform) {
    SDValue Result = DAG.getNode(Opc, RHS.getDebugLoc(), VT, OtherOp, RHS);
    if (isSlctCC)
      return DAG.getSelectCC(N->getDebugLoc(), OtherOp, Result,
                             Slct.getOperand(0), Slct.getOperand(1), CC);
    SDValue CCOp = Slct.getOperand(0);
    if (InvCC)
      CCOp = DAG.getSetCC(Slct.getDebugLoc(), CCOp.getValueType(),
                          CCOp.getOperand(0), CCOp.getOperand(1), CC);
    return DAG.getNode(ISD::SELECT, N->getDebugLoc(), VT,
                       CCOp, OtherOp, Result);
  }
  return SDValue();
}

/// PerformADDCombine - Target-specific dag combine xforms for ISD::ADD.
static SDValue PerformADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  // added by evan in r37685 with no testcase.
  SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);

  // fold (add (select cc, 0, c), x) -> (select cc, x, (add, x, c))
  if (N0.getOpcode() == ISD::SELECT && N0.getNode()->hasOneUse()) {
    SDValue Result = combineSelectAndUse(N, N0, N1, DCI);
    if (Result.getNode()) return Result;
  }
  if (N1.getOpcode() == ISD::SELECT && N1.getNode()->hasOneUse()) {
    SDValue Result = combineSelectAndUse(N, N1, N0, DCI);
    if (Result.getNode()) return Result;
  }

  return SDValue();
}

/// PerformSUBCombine - Target-specific dag combine xforms for ISD::SUB.
static SDValue PerformSUBCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  // added by evan in r37685 with no testcase.
  SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);

  // fold (sub x, (select cc, 0, c)) -> (select cc, x, (sub, x, c))
  if (N1.getOpcode() == ISD::SELECT && N1.getNode()->hasOneUse()) {
    SDValue Result = combineSelectAndUse(N, N1, N0, DCI);
    if (Result.getNode()) return Result;
  }

  return SDValue();
}


/// PerformFMRRDCombine - Target-specific dag combine xforms for ARMISD::FMRRD.
static SDValue PerformFMRRDCombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI) {
  // fmrrd(fmdrr x, y) -> x,y
  SDValue InDouble = N->getOperand(0);
  if (InDouble.getOpcode() == ARMISD::FMDRR)
    return DCI.CombineTo(N, InDouble.getOperand(0), InDouble.getOperand(1));
  return SDValue();
}

SDValue ARMTargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default: break;
  case ISD::ADD:      return PerformADDCombine(N, DCI);
  case ISD::SUB:      return PerformSUBCombine(N, DCI);
  case ARMISD::FMRRD: return PerformFMRRDCombine(N, DCI);
  }

  return SDValue();
}

/// isLegalAddressImmediate - Return true if the integer value can be used
/// as the offset of the target addressing mode for load / store of the
/// given type.
static bool isLegalAddressImmediate(int64_t V, MVT VT,
                                    const ARMSubtarget *Subtarget) {
  if (V == 0)
    return true;

  if (!VT.isSimple())
    return false;

  if (Subtarget->isThumb()) {
    if (V < 0)
      return false;

    unsigned Scale = 1;
    switch (VT.getSimpleVT()) {
    default: return false;
    case MVT::i1:
    case MVT::i8:
      // Scale == 1;
      break;
    case MVT::i16:
      // Scale == 2;
      Scale = 2;
      break;
    case MVT::i32:
      // Scale == 4;
      Scale = 4;
      break;
    }

    if ((V & (Scale - 1)) != 0)
      return false;
    V /= Scale;
    return V == (V & ((1LL << 5) - 1));
  }

  if (V < 0)
    V = - V;
  switch (VT.getSimpleVT()) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i32:
    // +- imm12
    return V == (V & ((1LL << 12) - 1));
  case MVT::i16:
    // +- imm8
    return V == (V & ((1LL << 8) - 1));
  case MVT::f32:
  case MVT::f64:
    if (!Subtarget->hasVFP2())
      return false;
    if ((V & 3) != 0)
      return false;
    V >>= 2;
    return V == (V & ((1LL << 8) - 1));
  }
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool ARMTargetLowering::isLegalAddressingMode(const AddrMode &AM,
                                              const Type *Ty) const {
  MVT VT = getValueType(Ty, true);
  if (!isLegalAddressImmediate(AM.BaseOffs, VT, Subtarget))
    return false;

  // Can never fold addr of global into load/store.
  if (AM.BaseGV)
    return false;

  switch (AM.Scale) {
  case 0:  // no scale reg, must be "r+i" or "r", or "i".
    break;
  case 1:
    if (Subtarget->isThumb())
      return false;
    // FALL THROUGH.
  default:
    // ARM doesn't support any R+R*scale+imm addr modes.
    if (AM.BaseOffs)
      return false;

    if (!VT.isSimple())
      return false;

    int Scale = AM.Scale;
    switch (VT.getSimpleVT()) {
    default: return false;
    case MVT::i1:
    case MVT::i8:
    case MVT::i32:
    case MVT::i64:
      // This assumes i64 is legalized to a pair of i32. If not (i.e.
      // ldrd / strd are used, then its address mode is same as i16.
      // r + r
      if (Scale < 0) Scale = -Scale;
      if (Scale == 1)
        return true;
      // r + r << imm
      return isPowerOf2_32(Scale & ~1);
    case MVT::i16:
      // r + r
      if (((unsigned)AM.HasBaseReg + Scale) <= 2)
        return true;
      return false;

    case MVT::isVoid:
      // Note, we allow "void" uses (basically, uses that aren't loads or
      // stores), because arm allows folding a scale into many arithmetic
      // operations.  This should be made more precise and revisited later.

      // Allow r << imm, but the imm has to be a multiple of two.
      if (AM.Scale & 1) return false;
      return isPowerOf2_32(AM.Scale);
    }
    break;
  }
  return true;
}

static bool getIndexedAddressParts(SDNode *Ptr, MVT VT,
                                   bool isSEXTLoad, SDValue &Base,
                                   SDValue &Offset, bool &isInc,
                                   SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;

  if (VT == MVT::i16 || ((VT == MVT::i8 || VT == MVT::i1) && isSEXTLoad)) {
    // AddressingMode 3
    Base = Ptr->getOperand(0);
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -256) {
        isInc = false;
        Offset = DAG.getConstant(-RHSC, RHS->getValueType(0));
        return true;
      }
    }
    isInc = (Ptr->getOpcode() == ISD::ADD);
    Offset = Ptr->getOperand(1);
    return true;
  } else if (VT == MVT::i32 || VT == MVT::i8 || VT == MVT::i1) {
    // AddressingMode 2
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -0x1000) {
        isInc = false;
        Offset = DAG.getConstant(-RHSC, RHS->getValueType(0));
        Base = Ptr->getOperand(0);
        return true;
      }
    }

    if (Ptr->getOpcode() == ISD::ADD) {
      isInc = true;
      ARM_AM::ShiftOpc ShOpcVal= ARM_AM::getShiftOpcForNode(Ptr->getOperand(0));
      if (ShOpcVal != ARM_AM::no_shift) {
        Base = Ptr->getOperand(1);
        Offset = Ptr->getOperand(0);
      } else {
        Base = Ptr->getOperand(0);
        Offset = Ptr->getOperand(1);
      }
      return true;
    }

    isInc = (Ptr->getOpcode() == ISD::ADD);
    Base = Ptr->getOperand(0);
    Offset = Ptr->getOperand(1);
    return true;
  }

  // FIXME: Use FLDM / FSTM to emulate indexed FP load / store.
  return false;
}

/// getPreIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool
ARMTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                             SDValue &Offset,
                                             ISD::MemIndexedMode &AM,
                                             SelectionDAG &DAG) const {
  if (Subtarget->isThumb())
    return false;

  MVT VT;
  SDValue Ptr;
  bool isSEXTLoad = false;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT  = LD->getMemoryVT();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT  = ST->getMemoryVT();
  } else
    return false;

  bool isInc;
  bool isLegal = getIndexedAddressParts(Ptr.getNode(), VT, isSEXTLoad, Base, Offset,
                                        isInc, DAG);
  if (isLegal) {
    AM = isInc ? ISD::PRE_INC : ISD::PRE_DEC;
    return true;
  }
  return false;
}

/// getPostIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if this node can be
/// combined with a load / store to form a post-indexed load / store.
bool ARMTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                   SDValue &Base,
                                                   SDValue &Offset,
                                                   ISD::MemIndexedMode &AM,
                                                   SelectionDAG &DAG) const {
  if (Subtarget->isThumb())
    return false;

  MVT VT;
  SDValue Ptr;
  bool isSEXTLoad = false;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT  = LD->getMemoryVT();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT  = ST->getMemoryVT();
  } else
    return false;

  bool isInc;
  bool isLegal = getIndexedAddressParts(Op, VT, isSEXTLoad, Base, Offset,
                                        isInc, DAG);
  if (isLegal) {
    AM = isInc ? ISD::POST_INC : ISD::POST_DEC;
    return true;
  }
  return false;
}

void ARMTargetLowering::computeMaskedBitsForTargetNode(const SDValue Op,
                                                       const APInt &Mask,
                                                       APInt &KnownZero,
                                                       APInt &KnownOne,
                                                       const SelectionDAG &DAG,
                                                       unsigned Depth) const {
  KnownZero = KnownOne = APInt(Mask.getBitWidth(), 0);
  switch (Op.getOpcode()) {
  default: break;
  case ARMISD::CMOV: {
    // Bits are known zero/one if known on the LHS and RHS.
    DAG.ComputeMaskedBits(Op.getOperand(0), Mask, KnownZero, KnownOne, Depth+1);
    if (KnownZero == 0 && KnownOne == 0) return;

    APInt KnownZeroRHS, KnownOneRHS;
    DAG.ComputeMaskedBits(Op.getOperand(1), Mask,
                          KnownZeroRHS, KnownOneRHS, Depth+1);
    KnownZero &= KnownZeroRHS;
    KnownOne  &= KnownOneRHS;
    return;
  }
  }
}

//===----------------------------------------------------------------------===//
//                           ARM Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
ARMTargetLowering::ConstraintType
ARMTargetLowering::getConstraintType(const std::string &Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:  break;
    case 'l': return C_RegisterClass;
    case 'w': return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass*>
ARMTargetLowering::getRegForInlineAsmConstraint(const std::string &Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC RS6000 Constraint Letters
    switch (Constraint[0]) {
    case 'l':
      if (Subtarget->isThumb())
        return std::make_pair(0U, ARM::tGPRRegisterClass);
      else
        return std::make_pair(0U, ARM::GPRRegisterClass);
    case 'r':
      return std::make_pair(0U, ARM::GPRRegisterClass);
    case 'w':
      if (VT == MVT::f32)
        return std::make_pair(0U, ARM::SPRRegisterClass);
      if (VT == MVT::f64)
        return std::make_pair(0U, ARM::DPRRegisterClass);
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(Constraint, VT);
}

std::vector<unsigned> ARMTargetLowering::
getRegClassForInlineAsmConstraint(const std::string &Constraint,
                                  MVT VT) const {
  if (Constraint.size() != 1)
    return std::vector<unsigned>();

  switch (Constraint[0]) {      // GCC ARM Constraint Letters
  default: break;
  case 'l':
    return make_vector<unsigned>(ARM::R0, ARM::R1, ARM::R2, ARM::R3,
                                 ARM::R4, ARM::R5, ARM::R6, ARM::R7,
                                 0);
  case 'r':
    return make_vector<unsigned>(ARM::R0, ARM::R1, ARM::R2, ARM::R3,
                                 ARM::R4, ARM::R5, ARM::R6, ARM::R7,
                                 ARM::R8, ARM::R9, ARM::R10, ARM::R11,
                                 ARM::R12, ARM::LR, 0);
  case 'w':
    if (VT == MVT::f32)
      return make_vector<unsigned>(ARM::S0, ARM::S1, ARM::S2, ARM::S3,
                                   ARM::S4, ARM::S5, ARM::S6, ARM::S7,
                                   ARM::S8, ARM::S9, ARM::S10, ARM::S11,
                                   ARM::S12,ARM::S13,ARM::S14,ARM::S15,
                                   ARM::S16,ARM::S17,ARM::S18,ARM::S19,
                                   ARM::S20,ARM::S21,ARM::S22,ARM::S23,
                                   ARM::S24,ARM::S25,ARM::S26,ARM::S27,
                                   ARM::S28,ARM::S29,ARM::S30,ARM::S31, 0);
    if (VT == MVT::f64)
      return make_vector<unsigned>(ARM::D0, ARM::D1, ARM::D2, ARM::D3,
                                   ARM::D4, ARM::D5, ARM::D6, ARM::D7,
                                   ARM::D8, ARM::D9, ARM::D10,ARM::D11,
                                   ARM::D12,ARM::D13,ARM::D14,ARM::D15, 0);
      break;
  }

  return std::vector<unsigned>();
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void ARMTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     char Constraint,
                                                     bool hasMemory,
                                                     std::vector<SDValue>&Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result(0, 0);

  switch (Constraint) {
  default: break;
  case 'I': case 'J': case 'K': case 'L':
  case 'M': case 'N': case 'O':
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
    if (!C)
      return;

    int64_t CVal64 = C->getSExtValue();
    int CVal = (int) CVal64;
    // None of these constraints allow values larger than 32 bits.  Check
    // that the value fits in an int.
    if (CVal != CVal64)
      return;

    switch (Constraint) {
      case 'I':
        if (Subtarget->isThumb()) {
          // This must be a constant between 0 and 255, for ADD immediates.
          if (CVal >= 0 && CVal <= 255)
            break;
        } else {
          // A constant that can be used as an immediate value in a
          // data-processing instruction.
          if (ARM_AM::getSOImmVal(CVal) != -1)
            break;
        }
        return;

      case 'J':
        if (Subtarget->isThumb()) {
          // This must be a constant between -255 and -1, for negated ADD
          // immediates. This can be used in GCC with an "n" modifier that
          // prints the negated value, for use with SUB instructions. It is
          // not useful otherwise but is implemented for compatibility.
          if (CVal >= -255 && CVal <= -1)
            break;
        } else {
          // This must be a constant between -4095 and 4095. It is not clear
          // what this constraint is intended for. Implemented for
          // compatibility with GCC.
          if (CVal >= -4095 && CVal <= 4095)
            break;
        }
        return;

      case 'K':
        if (Subtarget->isThumb()) {
          // A 32-bit value where only one byte has a nonzero value. Exclude
          // zero to match GCC. This constraint is used by GCC internally for
          // constants that can be loaded with a move/shift combination.
          // It is not useful otherwise but is implemented for compatibility.
          if (CVal != 0 && ARM_AM::isThumbImmShiftedVal(CVal))
            break;
        } else {
          // A constant whose bitwise inverse can be used as an immediate
          // value in a data-processing instruction. This can be used in GCC
          // with a "B" modifier that prints the inverted value, for use with
          // BIC and MVN instructions. It is not useful otherwise but is
          // implemented for compatibility.
          if (ARM_AM::getSOImmVal(~CVal) != -1)
            break;
        }
        return;

      case 'L':
        if (Subtarget->isThumb()) {
          // This must be a constant between -7 and 7,
          // for 3-operand ADD/SUB immediate instructions.
          if (CVal >= -7 && CVal < 7)
            break;
        } else {
          // A constant whose negation can be used as an immediate value in a
          // data-processing instruction. This can be used in GCC with an "n"
          // modifier that prints the negated value, for use with SUB
          // instructions. It is not useful otherwise but is implemented for
          // compatibility.
          if (ARM_AM::getSOImmVal(-CVal) != -1)
            break;
        }
        return;

      case 'M':
        if (Subtarget->isThumb()) {
          // This must be a multiple of 4 between 0 and 1020, for
          // ADD sp + immediate.
          if ((CVal >= 0 && CVal <= 1020) && ((CVal & 3) == 0))
            break;
        } else {
          // A power of two or a constant between 0 and 32.  This is used in
          // GCC for the shift amount on shifted register operands, but it is
          // useful in general for any shift amounts.
          if ((CVal >= 0 && CVal <= 32) || ((CVal & (CVal - 1)) == 0))
            break;
        }
        return;

      case 'N':
        if (Subtarget->isThumb()) {
          // This must be a constant between 0 and 31, for shift amounts.
          if (CVal >= 0 && CVal <= 31)
            break;
        }
        return;

      case 'O':
        if (Subtarget->isThumb()) {
          // This must be a multiple of 4 between -508 and 508, for
          // ADD/SUB sp = sp + immediate.
          if ((CVal >= -508 && CVal <= 508) && ((CVal & 3) == 0))
            break;
        }
        return;
    }
    Result = DAG.getTargetConstant(CVal, Op.getValueType());
    break;
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }
  return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, hasMemory,
                                                      Ops, DAG);
}
