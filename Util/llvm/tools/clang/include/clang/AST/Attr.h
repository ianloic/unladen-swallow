//===--- Attr.h - Classes for representing expressions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Attr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ATTR_H
#define LLVM_CLANG_AST_ATTR_H

#include <cassert>
#include <cstring>
#include <string>
#include <algorithm>

namespace clang {
  class ASTContext;
}


// Defined in ASTContext.cpp
void *operator new(size_t Bytes, clang::ASTContext &C,
                   size_t Alignment = 16) throw ();

namespace clang {

/// Attr - This represents one attribute.
class Attr {
public:
  enum Kind {
    Alias,
    Aligned,
    AlwaysInline,
    AnalyzerNoReturn, // Clang-specific.
    Annotate,
    AsmLabel, // Represent GCC asm label extension.
    Blocks,
    Cleanup,
    Const,
    Constructor,
    DLLExport,
    DLLImport,
    Deprecated,
    Destructor,
    FastCall,    
    Format,
    GNUInline,
    IBOutletKind, // Clang-specific.  Use "Kind" suffix to not conflict with
    NoReturn,
    NoThrow,
    Nodebug,
    Noinline,
    NonNull,
    ObjCException,
    ObjCNSObject,
    CFOwnershipRelease,       // Clang/Checker-specific.
    CFOwnershipRetain,        // Clang/Checker-specific.
    CFOwnershipReturns,       // Clang/Checker-specific.
    NSOwnershipAutorelease,   // Clang/Checker-specific.
    NSOwnershipRelease,         // Clang/Checker-specific.
    NSOwnershipRetain,          // Clang/Checker-specific.
    NSOwnershipReturns,         // Clang/Checker-specific.
    Overloadable, // Clang-specific
    Packed,
    Pure,
    Regparm,
    Section,
    StdCall,
    TransparentUnion,
    Unavailable,
    Unused,    
    Used,
    Visibility,
    WarnUnusedResult,
    Weak,
    WeakImport
  };
    
private:
  Attr *Next;
  Kind AttrKind;
  bool Inherited : 1;

protected:
  void* operator new(size_t bytes) throw() {
    assert(0 && "Attrs cannot be allocated with regular 'new'.");
    return 0;
  }
  void operator delete(void* data) throw() {
    assert(0 && "Attrs cannot be released with regular 'delete'.");
  }
  
protected:
  Attr(Kind AK) : Next(0), AttrKind(AK), Inherited(false) {}
  virtual ~Attr() {
    assert(Next == 0 && "Destroy didn't work");
  }
public:
  
  void Destroy(ASTContext &C);
  
  /// \brief Whether this attribute should be merged to new
  /// declarations.
  virtual bool isMerged() const { return true; }

  Kind getKind() const { return AttrKind; }

  Attr *getNext() { return Next; }
  const Attr *getNext() const { return Next; }
  void setNext(Attr *next) { Next = next; }

  bool isInherited() const { return Inherited; }
  void setInherited(bool value) { Inherited = value; }

  void addAttr(Attr *attr) {
    assert((attr != 0) && "addAttr(): attr is null");
    
    // FIXME: This doesn't preserve the order in any way.
    attr->Next = Next;
    Next = attr;
  }
  
  // Clone this attribute.
  virtual Attr* clone(ASTContext &C) const = 0;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *) { return true; }
};

#define DEF_SIMPLE_ATTR(ATTR)                                           \
class ATTR##Attr : public Attr {                                        \
public:                                                                 \
  ATTR##Attr() : Attr(ATTR) {}                                          \
  virtual Attr *clone(ASTContext &C) const { return ::new (C) ATTR##Attr; }\
  static bool classof(const Attr *A) { return A->getKind() == ATTR; }   \
  static bool classof(const ATTR##Attr *A) { return true; }             \
}

class PackedAttr : public Attr {
  unsigned Alignment;

public:
  PackedAttr(unsigned alignment) : Attr(Packed), Alignment(alignment) {}

  /// getAlignment - The specified alignment in bits.
  unsigned getAlignment() const { return Alignment; }

  virtual Attr* clone(ASTContext &C) const { 
    return ::new (C) PackedAttr(Alignment); 
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Packed;
  }
  static bool classof(const PackedAttr *A) { return true; }
};
  
class AlignedAttr : public Attr {
  unsigned Alignment;
public:
  AlignedAttr(unsigned alignment) : Attr(Aligned), Alignment(alignment) {}

  /// getAlignment - The specified alignment in bits.
  unsigned getAlignment() const { return Alignment; }

  virtual Attr* clone(ASTContext &C) const { return ::new (C) AlignedAttr(Alignment); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Aligned;
  }
  static bool classof(const AlignedAttr *A) { return true; }
};

class AnnotateAttr : public Attr {
  std::string Annotation;
public:
  AnnotateAttr(const std::string &ann) : Attr(Annotate), Annotation(ann) {}
  
  const std::string& getAnnotation() const { return Annotation; }

  virtual Attr* clone(ASTContext &C) const { return ::new (C) AnnotateAttr(Annotation); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Annotate;
  }
  static bool classof(const AnnotateAttr *A) { return true; }
};

class AsmLabelAttr : public Attr {
  std::string Label;
public:
  AsmLabelAttr(const std::string &L) : Attr(AsmLabel), Label(L) {}
  
  const std::string& getLabel() const { return Label; }
  
  virtual Attr* clone(ASTContext &C) const { return ::new (C) AsmLabelAttr(Label); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == AsmLabel;
  }
  static bool classof(const AsmLabelAttr *A) { return true; }
};

DEF_SIMPLE_ATTR(AlwaysInline);

class AliasAttr : public Attr {
  std::string Aliasee;
public:
  AliasAttr(const std::string &aliasee) : Attr(Alias), Aliasee(aliasee) {}

  const std::string& getAliasee() const { return Aliasee; }

  virtual Attr *clone(ASTContext &C) const { return ::new (C) AliasAttr(Aliasee); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Alias; }
  static bool classof(const AliasAttr *A) { return true; }
};

class ConstructorAttr : public Attr {
  int priority;
public:
  ConstructorAttr(int p) : Attr(Constructor), priority(p) {}

  int getPriority() const { return priority; }
  
  virtual Attr *clone(ASTContext &C) const { return ::new (C) ConstructorAttr(priority); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Constructor; }  
  static bool classof(const ConstructorAttr *A) { return true; }
};  
  
class DestructorAttr : public Attr {
  int priority;
public:
  DestructorAttr(int p) : Attr(Destructor), priority(p) {}

  int getPriority() const { return priority; }
  
  virtual Attr *clone(ASTContext &C) const { return ::new (C) DestructorAttr(priority); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Destructor; }  
  static bool classof(const DestructorAttr *A) { return true; }
};  
    
class GNUInlineAttr : public Attr {
public:
  GNUInlineAttr() : Attr(GNUInline) {}

  virtual Attr *clone(ASTContext &C) const { return ::new (C) GNUInlineAttr; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == GNUInline;
  }
  static bool classof(const GNUInlineAttr *A) { return true; }
};

class IBOutletAttr : public Attr {
public:
  IBOutletAttr() : Attr(IBOutletKind) {}

  virtual Attr *clone(ASTContext &C) const { return ::new (C) IBOutletAttr; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == IBOutletKind;
  }
  static bool classof(const IBOutletAttr *A) { return true; }
};

DEF_SIMPLE_ATTR(NoReturn);
DEF_SIMPLE_ATTR(AnalyzerNoReturn);  
DEF_SIMPLE_ATTR(Deprecated);

class SectionAttr : public Attr {
  std::string Name;
public:
  SectionAttr(const std::string &N) : Attr(Section), Name(N) {}
  
  const std::string& getName() const { return Name; }

  virtual Attr *clone(ASTContext &C) const { return ::new (C) SectionAttr(Name); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() == Section;
  }
  static bool classof(const SectionAttr *A) { return true; }
};

DEF_SIMPLE_ATTR(Unavailable);
DEF_SIMPLE_ATTR(Unused);
DEF_SIMPLE_ATTR(Used);  
DEF_SIMPLE_ATTR(Weak);  
DEF_SIMPLE_ATTR(WeakImport);
DEF_SIMPLE_ATTR(NoThrow);
DEF_SIMPLE_ATTR(Const);
DEF_SIMPLE_ATTR(Pure);

class NonNullAttr : public Attr {
  unsigned* ArgNums;
  unsigned Size;
public:
  NonNullAttr(unsigned* arg_nums = 0, unsigned size = 0) : Attr(NonNull),
    ArgNums(0), Size(0) {
  
    if (size == 0) return;
    assert(arg_nums);
    ArgNums = new unsigned[size];
    Size = size;
    memcpy(ArgNums, arg_nums, sizeof(*ArgNums)*size);
  }
  
  virtual ~NonNullAttr() {
    delete [] ArgNums;
  }

  typedef const unsigned *iterator;
  iterator begin() const { return ArgNums; }
  iterator end() const { return ArgNums + Size; }
  unsigned size() const { return Size; }

  bool isNonNull(unsigned arg) const {
    return ArgNums ? std::binary_search(ArgNums, ArgNums+Size, arg) : true;
  }  

  virtual Attr *clone(ASTContext &C) const { return ::new (C) NonNullAttr(ArgNums, Size); }

  static bool classof(const Attr *A) { return A->getKind() == NonNull; }
  static bool classof(const NonNullAttr *A) { return true; }
};

class FormatAttr : public Attr {
  std::string Type;
  int formatIdx, firstArg;
public:
  FormatAttr(const std::string &type, int idx, int first) : Attr(Format),
             Type(type), formatIdx(idx), firstArg(first) {}

  const std::string& getType() const { return Type; }
  void setType(const std::string &type) { Type = type; }
  int getFormatIdx() const { return formatIdx; }
  int getFirstArg() const { return firstArg; }

  virtual Attr *clone(ASTContext &C) const { 
    return ::new (C) FormatAttr(Type, formatIdx, firstArg); 
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Format; }
  static bool classof(const FormatAttr *A) { return true; }
};

class VisibilityAttr : public Attr {
public:
  /// @brief An enumeration for the kinds of visibility of symbols.
  enum VisibilityTypes {
    DefaultVisibility = 0,
    HiddenVisibility,
    ProtectedVisibility
  };
private:
  VisibilityTypes VisibilityType;
public:
  VisibilityAttr(VisibilityTypes v) : Attr(Visibility),
                 VisibilityType(v) {}

  VisibilityTypes getVisibility() const { return VisibilityType; }

  virtual Attr *clone(ASTContext &C) const { return ::new (C) VisibilityAttr(VisibilityType); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Visibility; }
  static bool classof(const VisibilityAttr *A) { return true; }
};

DEF_SIMPLE_ATTR(DLLImport);
DEF_SIMPLE_ATTR(DLLExport);
DEF_SIMPLE_ATTR(FastCall);
DEF_SIMPLE_ATTR(StdCall);
DEF_SIMPLE_ATTR(TransparentUnion);
DEF_SIMPLE_ATTR(ObjCNSObject);
DEF_SIMPLE_ATTR(ObjCException);

class OverloadableAttr : public Attr {
public:
  OverloadableAttr() : Attr(Overloadable) { }

  virtual bool isMerged() const { return false; }

  virtual Attr *clone(ASTContext &C) const { 
    return ::new (C) OverloadableAttr; 
  }

  static bool classof(const Attr *A) { return A->getKind() == Overloadable; }
  static bool classof(const OverloadableAttr *) { return true; }
};

class BlocksAttr : public Attr {
public:
  enum BlocksAttrTypes {
    ByRef = 0
  };
private:
  BlocksAttrTypes BlocksAttrType;
public:
  BlocksAttr(BlocksAttrTypes t) : Attr(Blocks), BlocksAttrType(t) {}

  BlocksAttrTypes getType() const { return BlocksAttrType; }

  virtual Attr *clone(ASTContext &C) const { return ::new (C) BlocksAttr(BlocksAttrType); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Blocks; }
  static bool classof(const BlocksAttr *A) { return true; }
};

class FunctionDecl;
  
class CleanupAttr : public Attr {
  FunctionDecl *FD;
  
public:
  CleanupAttr(FunctionDecl *fd) : Attr(Cleanup), FD(fd) {}

  const FunctionDecl *getFunctionDecl() const { return FD; }
  
  virtual Attr *clone(ASTContext &C) const { return ::new (C) CleanupAttr(FD); }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) { return A->getKind() == Cleanup; }
  static bool classof(const CleanupAttr *A) { return true; }
};

DEF_SIMPLE_ATTR(Nodebug);
DEF_SIMPLE_ATTR(WarnUnusedResult);  
DEF_SIMPLE_ATTR(Noinline);

class RegparmAttr : public Attr {
  unsigned NumParams;

public:
  RegparmAttr(unsigned np) : Attr(Regparm), NumParams(np) {}

  unsigned getNumParams() const { return NumParams; }

  virtual Attr *clone(ASTContext &C) const { 
    return ::new (C) RegparmAttr(NumParams); 
  }

  // Implement isa/cast/dyncast/etc.    
  static bool classof(const Attr *A) { return A->getKind() == Regparm; }
  static bool classof(const RegparmAttr *A) { return true; }
};

// Checker-specific attributes.
DEF_SIMPLE_ATTR(CFOwnershipRelease);
DEF_SIMPLE_ATTR(CFOwnershipRetain);
DEF_SIMPLE_ATTR(CFOwnershipReturns);
DEF_SIMPLE_ATTR(NSOwnershipRelease);
DEF_SIMPLE_ATTR(NSOwnershipRetain);
DEF_SIMPLE_ATTR(NSOwnershipAutorelease);
DEF_SIMPLE_ATTR(NSOwnershipReturns);

#undef DEF_SIMPLE_ATTR
  
}  // end namespace clang

#endif
