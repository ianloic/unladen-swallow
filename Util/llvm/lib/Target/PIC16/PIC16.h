//===-- PIC16.h - Top-level interface for PIC16 representation --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in 
// the LLVM PIC16 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_PIC16_H
#define LLVM_TARGET_PIC16_H

#include "llvm/Target/TargetMachine.h"
#include <iosfwd>
#include <cassert>
#include <sstream>
#include <cstring>
#include <string>

namespace llvm {
  class PIC16TargetMachine;
  class FunctionPass;
  class MachineCodeEmitter;
  class raw_ostream;

namespace PIC16CC {
  enum CondCodes {
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,
    ULT,
    UGT,
    ULE,
    UGE
  };
}
  // A Central class to manage all ABI naming conventions.
  // PAN - [P]ic16 [A]BI [N]ames
  class PAN {
    public:
    // Map the name of the symbol to its section name.
    // Current ABI:
    // -----------------------------------------------------
    // ALL Names are prefixed with the symobl '@'.
    // ------------------------------------------------------
    // Global variables do not have any '.' in their names.
    // These are maily function names and global variable names.
    // Example - @foo,  @i
    // -------------------------------------------------------
    // Functions and auto variables.
    // Names are mangled as <prefix><funcname>.<tag>.<varname>
    // Where <prefix> is '@' and <tag> is any one of
    // the following
    // .auto. - an automatic var of a function.
    // .temp. - temproray data of a function.
    // .ret.  - return value label for a function.
    // .frame. - Frame label for a function where retval, args
    //           and temps are stored.
    // .args. - Label used to pass arguments to a direct call.
    // Example - Function name:   @foo
    //           Its frame:       @foo.frame.
    //           Its retval:      @foo.ret.
    //           Its local vars:  @foo.auto.a
    //           Its temp data:   @foo.temp.
    //           Its arg passing: @foo.args.
    //----------------------------------------------
    // Libcall - compiler generated libcall names must start with .lib.
    //           This id will be used to emit extern decls for libcalls.
    // Example - libcall name:   @.lib.sra.i8
    //           To pass args:   @.lib.sra.i8.args.
    //           To return val:  @.lib.sra.i8.ret.
    //----------------------------------------------
    // SECTION Names
    // uninitialized globals - @udata.<num>.#
    // initialized globals - @idata.<num>.#
    // Function frame - @<func>.frame_section.
    // Function autos - @<func>.autos_section.
    // Declarations - Enclosed in comments. No section for them.
    //----------------------------------------------------------
    
    // Tags used to mangle different names. 
    enum TAGS {
      PREFIX_SYMBOL,
      GLOBAL,
      STATIC_LOCAL,
      AUTOS_LABEL,
      FRAME_LABEL,
      RET_LABEL,
      ARGS_LABEL,
      TEMPS_LABEL,
      
      LIBCALL,
      
      FRAME_SECTION,
      AUTOS_SECTION,
      CODE_SECTION
    };

    // Textual names of the tags.
    inline static const char *getTagName(TAGS tag) {
      switch (tag) {
      default: return "";
      case PREFIX_SYMBOL:    return "@";
      case AUTOS_LABEL:       return ".auto.";
      case FRAME_LABEL:       return ".frame.";
      case TEMPS_LABEL:       return ".temp.";
      case ARGS_LABEL:       return ".args.";
      case RET_LABEL:       return ".ret.";
      case LIBCALL:       return ".lib.";
      case FRAME_SECTION:       return ".frame_section.";
      case AUTOS_SECTION:       return ".autos_section.";
      case CODE_SECTION:       return ".code_section.";
      }
    }

    // Get tag type for the Symbol.
    inline static TAGS getSymbolTag(const std::string &Sym) {
      if (Sym.find(getTagName(TEMPS_LABEL)) != std::string::npos)
        return TEMPS_LABEL;

      if (Sym.find(getTagName(FRAME_LABEL)) != std::string::npos)
        return FRAME_LABEL;

      if (Sym.find(getTagName(RET_LABEL)) != std::string::npos)
        return RET_LABEL;

      if (Sym.find(getTagName(ARGS_LABEL)) != std::string::npos)
        return ARGS_LABEL;

      if (Sym.find(getTagName(AUTOS_LABEL)) != std::string::npos)
        return AUTOS_LABEL;

      if (Sym.find(getTagName(LIBCALL)) != std::string::npos)
        return LIBCALL;

      // It does not have any Tag. So its a true global or static local.
      if (Sym.find(".") == std::string::npos) 
        return GLOBAL;
      
      // If a . is there, then it may be static local.
      // We should mangle these as well in clang.
      if (Sym.find(".") != std::string::npos) 
        return STATIC_LOCAL;
 
      assert (0 && "Could not determine Symbol's tag");
      return PREFIX_SYMBOL; // Silence warning when assertions are turned off.
    }

    // addPrefix - add prefix symbol to a name if there isn't one already.
    inline static std::string addPrefix (const std::string &Name) {
      std::string prefix = getTagName (PREFIX_SYMBOL);

      // If this name already has a prefix, nothing to do.
      if (Name.compare(0, prefix.size(), prefix) == 0)
        return Name;

      return prefix + Name;
    }

    // Get mangled func name from a mangled sym name.
    // In all cases func name is the first component before a '.'.
    static inline std::string getFuncNameForSym(const std::string &Sym1) {
      assert (getSymbolTag(Sym1) != GLOBAL && "not belongs to a function");

      std::string Sym = addPrefix(Sym1);

      // Position of the . after func name. That's where func name ends.
      size_t func_name_end = Sym.find ('.');

      return Sym.substr (0, func_name_end);
    }

    // Get Frame start label for a func.
    static std::string getFrameLabel(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(FRAME_LABEL);
      return Func1 + tag;
    }

    static std::string getRetvalLabel(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(RET_LABEL);
      return Func1 + tag;
    }

    static std::string getArgsLabel(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(ARGS_LABEL);
      return Func1 + tag;
    }

    static std::string getTempdataLabel(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(TEMPS_LABEL);
      return Func1 + tag;
    }

    static std::string getFrameSectionName(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(FRAME_SECTION);
      return Func1 + tag + "# UDATA_OVR";
    }

    static std::string getAutosSectionName(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(AUTOS_SECTION);
      return Func1 + tag + "# UDATA_OVR";
    }

    static std::string getCodeSectionName(const std::string &Func) {
      std::string Func1 = addPrefix(Func);
      std::string tag = getTagName(CODE_SECTION);
      return Func1 + tag + "# CODE";
    }

    // udata, romdata and idata section names are generated by a given number.
    // @udata.<num>.# 
    static std::string getUdataSectionName(unsigned num, 
                                           std::string prefix = "") {
       std::ostringstream o;
       o << getTagName(PREFIX_SYMBOL) << prefix << "udata." << num 
         << ".# UDATA"; 
       return o.str(); 
    }

    static std::string getRomdataSectionName(unsigned num,
                                             std::string prefix = "") {
       std::ostringstream o;
       o << getTagName(PREFIX_SYMBOL) << prefix << "romdata." << num 
         << ".# ROMDATA";
       return o.str();
    }

    static std::string getIdataSectionName(unsigned num,
                                           std::string prefix = "") {
       std::ostringstream o;
       o << getTagName(PREFIX_SYMBOL) << prefix << "idata." << num 
         << ".# IDATA"; 
       return o.str(); 
    }

    inline static bool isLocalName (const std::string &Name) {
      if (getSymbolTag(Name) == AUTOS_LABEL)
        return true;

      return false;
    }

    inline static bool isLocalToFunc (std::string &Func, std::string &Var) {
      if (! isLocalName(Var)) return false;

      std::string Func1 = addPrefix(Func);
      // Extract func name of the varilable.
      const std::string &fname = getFuncNameForSym(Var);

      if (fname.compare(Func1) == 0)
        return true;

      return false;
    }


    // Get the section for the given external symbol names.
    // This tries to find the type (Tag) of the symbol from its mangled name
    // and return appropriate section name for it.
    static inline std::string getSectionNameForSym(const std::string &Sym1) {
      std::string Sym = addPrefix(Sym1);

      std::string SectionName;
 
      std::string Fname = getFuncNameForSym (Sym);
      TAGS id = getSymbolTag (Sym);

      switch (id) {
        default : assert (0 && "Could not determine external symbol type");
        case FRAME_LABEL:
        case RET_LABEL:
        case TEMPS_LABEL:
        case ARGS_LABEL:  {
          return getFrameSectionName(Fname);
        }
        case AUTOS_LABEL: {
          return getAutosSectionName(Fname);
        }
      }
    }
  }; // class PAN.


  // External symbol names require memory to live till the program end.
  // So we have to allocate it and keep.
  inline static const char *createESName (const std::string &name) {
    char *tmpName = new char[name.size() + 1];
    strcpy (tmpName, name.c_str());
    return tmpName;
  }



  inline static const char *PIC16CondCodeToString(PIC16CC::CondCodes CC) {
    switch (CC) {
    default: assert(0 && "Unknown condition code");
    case PIC16CC::NE:  return "ne";
    case PIC16CC::EQ:   return "eq";
    case PIC16CC::LT:   return "lt";
    case PIC16CC::ULT:   return "lt";
    case PIC16CC::LE:  return "le";
    case PIC16CC::ULE:  return "le";
    case PIC16CC::GT:  return "gt";
    case PIC16CC::UGT:  return "gt";
    case PIC16CC::GE:   return "ge";
    case PIC16CC::UGE:   return "ge";
    }
  }

  inline static bool isSignedComparison(PIC16CC::CondCodes CC) {
    switch (CC) {
    default: assert(0 && "Unknown condition code");
    case PIC16CC::NE:  
    case PIC16CC::EQ: 
    case PIC16CC::LT:
    case PIC16CC::LE:
    case PIC16CC::GE:
    case PIC16CC::GT:
      return true;
    case PIC16CC::ULT:
    case PIC16CC::UGT:
    case PIC16CC::ULE:
    case PIC16CC::UGE:
      return false;   // condition codes for unsigned comparison. 
    }
  }



  FunctionPass *createPIC16ISelDag(PIC16TargetMachine &TM);
  FunctionPass *createPIC16CodePrinterPass(raw_ostream &OS, 
                                           PIC16TargetMachine &TM,
                                           bool Verbose);
  // Banksel optimzer pass.
  FunctionPass *createPIC16MemSelOptimizerPass();
} // end namespace llvm;

// Defines symbolic names for PIC16 registers.  This defines a mapping from
// register name to register number.
#include "PIC16GenRegisterNames.inc"

// Defines symbolic names for the PIC16 instructions.
#include "PIC16GenInstrNames.inc"

#endif
