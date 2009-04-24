//===--- CodeGenTypes.cpp - Type translation for LLVM CodeGen -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that handles AST -> LLVM type lowering. 
//
//===----------------------------------------------------------------------===//

#include "CodeGenTypes.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/Target/TargetData.h"

#include "CGCall.h"

using namespace clang;
using namespace CodeGen;

namespace {
  /// RecordOrganizer - This helper class, used by CGRecordLayout, layouts 
  /// structs and unions. It manages transient information used during layout.
  /// FIXME : Handle field aligments. Handle packed structs.
  class RecordOrganizer {
  public:
    explicit RecordOrganizer(CodeGenTypes &Types, const RecordDecl& Record) : 
      CGT(Types), RD(Record), STy(NULL) {}

    /// layoutStructFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutStructFields(const ASTRecordLayout &RL);

    /// layoutUnionFields - Do the actual work and lay out all fields. Create
    /// corresponding llvm struct type.  This should be invoked only after
    /// all fields are added.
    void layoutUnionFields(const ASTRecordLayout &RL);

    /// getLLVMType - Return associated llvm struct type. This may be NULL
    /// if fields are not laid out.
    llvm::Type *getLLVMType() const {
      return STy;
    }

    llvm::SmallSet<unsigned, 8> &getPaddingFields() {
      return PaddingFields;
    }

  private:
    CodeGenTypes &CGT;
    const RecordDecl& RD;
    llvm::Type *STy;
    llvm::SmallSet<unsigned, 8> PaddingFields;
  };
}

CodeGenTypes::CodeGenTypes(ASTContext &Ctx, llvm::Module& M,
                           const llvm::TargetData &TD)
  : Context(Ctx), Target(Ctx.Target), TheModule(M), TheTargetData(TD),
    TheABIInfo(0) {
}

CodeGenTypes::~CodeGenTypes() {
  for(llvm::DenseMap<const Type *, CGRecordLayout *>::iterator
        I = CGRecordLayouts.begin(), E = CGRecordLayouts.end();
      I != E; ++I)
    delete I->second;
  CGRecordLayouts.clear();
}

/// ConvertType - Convert the specified type to its LLVM form.
const llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  llvm::PATypeHolder Result = ConvertTypeRecursive(T);

  // Any pointers that were converted defered evaluation of their pointee type,
  // creating an opaque type instead.  This is in order to avoid problems with
  // circular types.  Loop through all these defered pointees, if any, and
  // resolve them now.
  while (!PointersToResolve.empty()) {
    std::pair<const PointerLikeType *, llvm::OpaqueType*> P =
      PointersToResolve.back();
    PointersToResolve.pop_back();
    // We can handle bare pointers here because we know that the only pointers
    // to the Opaque type are P.second and from other types.  Refining the
    // opqaue type away will invalidate P.second, but we don't mind :).
    const llvm::Type *NT = ConvertTypeRecursive(P.first->getPointeeType());
    P.second->refineAbstractTypeTo(NT);
  }

  return Result;
}

const llvm::Type *CodeGenTypes::ConvertTypeRecursive(QualType T) {
  T = Context.getCanonicalType(T);;
  
  // See if type is already cached.
  llvm::DenseMap<Type *, llvm::PATypeHolder>::iterator
    I = TypeCache.find(T.getTypePtr());
  // If type is found in map and this is not a definition for a opaque
  // place holder type then use it. Otherwise, convert type T.
  if (I != TypeCache.end())
    return I->second.get();

  const llvm::Type *ResultType = ConvertNewType(T);
  TypeCache.insert(std::make_pair(T.getTypePtr(), 
                                  llvm::PATypeHolder(ResultType)));
  return ResultType;
}

/// ConvertTypeForMem - Convert type T into a llvm::Type.  This differs from
/// ConvertType in that it is used to convert to the memory representation for
/// a type.  For example, the scalar representation for _Bool is i1, but the
/// memory representation is usually i8 or i32, depending on the target.
const llvm::Type *CodeGenTypes::ConvertTypeForMem(QualType T) {
  const llvm::Type *R = ConvertType(T);
  
  // If this is a non-bool type, don't map it.
  if (R != llvm::Type::Int1Ty)
    return R;
    
  // Otherwise, return an integer of the target-specified size.
  return llvm::IntegerType::get((unsigned)Context.getTypeSize(T));
  
}

/// UpdateCompletedType - When we find the full definition for a TagDecl,
/// replace the 'opaque' type we previously made for it if applicable.
void CodeGenTypes::UpdateCompletedType(const TagDecl *TD) {
  const Type *Key = 
    Context.getTagDeclType(const_cast<TagDecl*>(TD)).getTypePtr();
  llvm::DenseMap<const Type*, llvm::PATypeHolder>::iterator TDTI = 
    TagDeclTypes.find(Key);
  if (TDTI == TagDeclTypes.end()) return;
  
  // Remember the opaque LLVM type for this tagdecl.
  llvm::PATypeHolder OpaqueHolder = TDTI->second;
  assert(isa<llvm::OpaqueType>(OpaqueHolder.get()) &&
         "Updating compilation of an already non-opaque type?");
  
  // Remove it from TagDeclTypes so that it will be regenerated.
  TagDeclTypes.erase(TDTI);

  // Generate the new type.
  const llvm::Type *NT = ConvertTagDeclType(TD);

  // Refine the old opaque type to its new definition.
  cast<llvm::OpaqueType>(OpaqueHolder.get())->refineAbstractTypeTo(NT);
}

static const llvm::Type* getTypeForFormat(const llvm::fltSemantics &format) {
  if (&format == &llvm::APFloat::IEEEsingle)
    return llvm::Type::FloatTy;
  if (&format == &llvm::APFloat::IEEEdouble)
    return llvm::Type::DoubleTy;
  if (&format == &llvm::APFloat::IEEEquad)
    return llvm::Type::FP128Ty;
  if (&format == &llvm::APFloat::PPCDoubleDouble)
    return llvm::Type::PPC_FP128Ty;
  if (&format == &llvm::APFloat::x87DoubleExtended)
    return llvm::Type::X86_FP80Ty;
  assert(0 && "Unknown float format!");
  return 0;
}

const llvm::Type *CodeGenTypes::ConvertNewType(QualType T) {
  const clang::Type &Ty = *Context.getCanonicalType(T);
  
  switch (Ty.getTypeClass()) {
  case Type::TypeName:        // typedef isn't canonical.
  case Type::TemplateTypeParm:// template type parameters never generated
  case Type::DependentSizedArray: // dependent types are never generated
  case Type::TypeOfExp:       // typeof isn't canonical.
  case Type::TypeOfTyp:       // typeof isn't canonical.
    assert(0 && "Non-canonical type, shouldn't happen");
  case Type::Builtin: {
    switch (cast<BuiltinType>(Ty).getKind()) {
    default: assert(0 && "Unknown builtin type!");
    case BuiltinType::Void:
      // LLVM void type can only be used as the result of a function call.  Just
      // map to the same as char.
      return llvm::IntegerType::get(8);

    case BuiltinType::Bool:
      // Note that we always return bool as i1 for use as a scalar type.
      return llvm::Type::Int1Ty;
      
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::Int:
    case BuiltinType::UInt:
    case BuiltinType::Long:
    case BuiltinType::ULong:
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
    case BuiltinType::WChar:
      return llvm::IntegerType::get(
        static_cast<unsigned>(Context.getTypeSize(T)));
      
    case BuiltinType::Float:
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
      return getTypeForFormat(Context.getFloatTypeSemantics(T));
    }
    break;
  }
  case Type::Complex: {
    const llvm::Type *EltTy = 
      ConvertTypeRecursive(cast<ComplexType>(Ty).getElementType());
    return llvm::StructType::get(EltTy, EltTy, NULL);
  }
  case Type::Reference:
  case Type::Pointer: {
    const PointerLikeType &PTy = cast<PointerLikeType>(Ty);
    QualType ETy = PTy.getPointeeType();
    llvm::OpaqueType *PointeeType = llvm::OpaqueType::get();
    PointersToResolve.push_back(std::make_pair(&PTy, PointeeType));
    return llvm::PointerType::get(PointeeType, ETy.getAddressSpace());
  }
    
  case Type::VariableArray: {
    const VariableArrayType &A = cast<VariableArrayType>(Ty);
    assert(A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // VLAs resolve to the innermost element type; this matches
    // the return of alloca, and there isn't any obviously better choice.
    return ConvertTypeRecursive(A.getElementType());
  }
  case Type::IncompleteArray: {
    const IncompleteArrayType &A = cast<IncompleteArrayType>(Ty);
    assert(A.getIndexTypeQualifier() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // int X[] -> [0 x int]
    return llvm::ArrayType::get(ConvertTypeRecursive(A.getElementType()), 0);
  }
  case Type::ConstantArray: {
    const ConstantArrayType &A = cast<ConstantArrayType>(Ty);
    const llvm::Type *EltTy = ConvertTypeRecursive(A.getElementType());
    return llvm::ArrayType::get(EltTy, A.getSize().getZExtValue());
  }
  case Type::ExtVector:
  case Type::Vector: {
    const VectorType &VT = cast<VectorType>(Ty);
    return llvm::VectorType::get(ConvertTypeRecursive(VT.getElementType()),
                                 VT.getNumElements());
  }
  case Type::FunctionNoProto:
    return GetFunctionType(getFunctionInfo(cast<FunctionTypeNoProto>(&Ty)), 
                           true);
  case Type::FunctionProto: {
    const FunctionTypeProto *FTP = cast<FunctionTypeProto>(&Ty);
    return GetFunctionType(getFunctionInfo(FTP), FTP->isVariadic());
  }
  
  case Type::ASQual:
    return
      ConvertTypeRecursive(QualType(cast<ASQualType>(Ty).getBaseType(), 0));

  case Type::ObjCInterface: {
    // Warning: Use of this is strongly discouraged.  Late binding of instance
    // variables is supported on some runtimes and so using static binding can
    // break code when libraries are updated.  Only use this if you have
    // previously checked that the ObjCRuntime subclass in use does not support
    // late-bound ivars.
    // We are issuing warnings elsewhere!
    ObjCInterfaceType OIT = cast<ObjCInterfaceType>(Ty);
    ObjCInterfaceDecl *ID = OIT.getDecl();
    const RecordDecl *RD = Context.addRecordToClass(ID);
    return ConvertTagDeclType(cast<TagDecl>(RD));
  }
      
  case Type::ObjCQualifiedInterface: {
    ObjCQualifiedInterfaceType QIT = cast<ObjCQualifiedInterfaceType>(Ty);
    
    return ConvertTypeRecursive(Context.getObjCInterfaceType(QIT.getDecl()));
  }

  case Type::ObjCQualifiedId:
    // Protocols don't influence the LLVM type.
    return ConvertTypeRecursive(Context.getObjCIdType());

  case Type::Tagged: {
    const TagDecl *TD = cast<TagType>(Ty).getDecl();
    const llvm::Type *Res = ConvertTagDeclType(TD);
    
    std::string TypeName(TD->getKindName());
    TypeName += '.';
    
    // Name the codegen type after the typedef name
    // if there is no tag type name available
    if (TD->getIdentifier())
      TypeName += TD->getNameAsString();
    else if (const TypedefType *TdT = dyn_cast<TypedefType>(T))
      TypeName += TdT->getDecl()->getNameAsString();
    else
      TypeName += "anon";
    
    TheModule.addTypeName(TypeName, Res);  
    return Res;
  }

  case Type::BlockPointer: {
    const QualType FTy = cast<BlockPointerType>(Ty).getPointeeType();
    return llvm::PointerType::get(ConvertTypeRecursive(FTy), 
                                  FTy.getAddressSpace());
  }

  case Type::MemberPointer:
    // FIXME: Implement C++ pointer-to-member. The GCC representation is
    // documented here:
    // http://gcc.gnu.org/onlinedocs/gccint/Type-Layout.html#Type-Layout
    assert(0 && "FIXME: We can't handle member pointers yet.");
    return llvm::OpaqueType::get();
  }
  
  // FIXME: implement.
  return llvm::OpaqueType::get();
}

/// ConvertTagDeclType - Lay out a tagged decl type like struct or union or
/// enum.
const llvm::Type *CodeGenTypes::ConvertTagDeclType(const TagDecl *TD) {
  // TagDecl's are not necessarily unique, instead use the (clang)
  // type connected to the decl.
  const Type *Key = 
    Context.getTagDeclType(const_cast<TagDecl*>(TD)).getTypePtr();
  llvm::DenseMap<const Type*, llvm::PATypeHolder>::iterator TDTI = 
    TagDeclTypes.find(Key);
  
  // If we've already compiled this tag type, use the previous definition.
  if (TDTI != TagDeclTypes.end())
    return TDTI->second;
  
  // If this is still a forward definition, just define an opaque type to use
  // for this tagged decl.
  if (!TD->isDefinition()) {
    llvm::Type *ResultType = llvm::OpaqueType::get();  
    TagDeclTypes.insert(std::make_pair(Key, ResultType));
    return ResultType;
  }
  
  // Okay, this is a definition of a type.  Compile the implementation now.
  
  if (TD->isEnum()) {
    // Don't bother storing enums in TagDeclTypes.
    return ConvertTypeRecursive(cast<EnumDecl>(TD)->getIntegerType());
  }
  
  // This decl could well be recursive.  In this case, insert an opaque
  // definition of this type, which the recursive uses will get.  We will then
  // refine this opaque version later.

  // Create new OpaqueType now for later use in case this is a recursive
  // type.  This will later be refined to the actual type.
  llvm::PATypeHolder ResultHolder = llvm::OpaqueType::get();
  TagDeclTypes.insert(std::make_pair(Key, ResultHolder));
  
  const llvm::Type *ResultType;
  const RecordDecl *RD = cast<const RecordDecl>(TD);
  if (TD->isStruct() || TD->isClass()) {
    // Layout fields.
    RecordOrganizer RO(*this, *RD);
    
    RO.layoutStructFields(Context.getASTRecordLayout(RD));
    
    // Get llvm::StructType.
    const Type *Key = 
      Context.getTagDeclType(const_cast<TagDecl*>(TD)).getTypePtr();
    CGRecordLayouts[Key] = new CGRecordLayout(RO.getLLVMType(), 
                                              RO.getPaddingFields());
    ResultType = RO.getLLVMType();
    
  } else if (TD->isUnion()) {
    // Just use the largest element of the union, breaking ties with the
    // highest aligned member.
    if (!RD->field_empty()) {
      RecordOrganizer RO(*this, *RD);
      
      RO.layoutUnionFields(Context.getASTRecordLayout(RD));
      
      // Get llvm::StructType.
      const Type *Key = 
        Context.getTagDeclType(const_cast<TagDecl*>(TD)).getTypePtr();
      CGRecordLayouts[Key] = new CGRecordLayout(RO.getLLVMType(),
                                                RO.getPaddingFields());
      ResultType = RO.getLLVMType();
    } else {       
      ResultType = llvm::StructType::get(std::vector<const llvm::Type*>());
    }
  } else {
    assert(0 && "FIXME: Unknown tag decl kind!");
    abort();
  }
  
  // Refine our Opaque type to ResultType.  This can invalidate ResultType, so
  // make sure to read the result out of the holder.
  cast<llvm::OpaqueType>(ResultHolder.get())
    ->refineAbstractTypeTo(ResultType);
  
  return ResultHolder.get();
}  

/// getLLVMFieldNo - Return llvm::StructType element number
/// that corresponds to the field FD.
unsigned CodeGenTypes::getLLVMFieldNo(const FieldDecl *FD) {
  llvm::DenseMap<const FieldDecl*, unsigned>::iterator I = FieldInfo.find(FD);
  assert (I != FieldInfo.end()  && "Unable to find field info");
  return I->second;
}

/// addFieldInfo - Assign field number to field FD.
void CodeGenTypes::addFieldInfo(const FieldDecl *FD, unsigned No) {
  FieldInfo[FD] = No;
}

/// getBitFieldInfo - Return the BitFieldInfo  that corresponds to the field FD.
CodeGenTypes::BitFieldInfo CodeGenTypes::getBitFieldInfo(const FieldDecl *FD) {
  llvm::DenseMap<const FieldDecl *, BitFieldInfo>::iterator
    I = BitFields.find(FD);
  assert (I != BitFields.end()  && "Unable to find bitfield info");
  return I->second;
}

/// addBitFieldInfo - Assign a start bit and a size to field FD.
void CodeGenTypes::addBitFieldInfo(const FieldDecl *FD, unsigned Begin,
                                   unsigned Size) {
  BitFields.insert(std::make_pair(FD, BitFieldInfo(Begin, Size)));
}

/// getCGRecordLayout - Return record layout info for the given llvm::Type.
const CGRecordLayout *
CodeGenTypes::getCGRecordLayout(const TagDecl *TD) const {
  const Type *Key = 
    Context.getTagDeclType(const_cast<TagDecl*>(TD)).getTypePtr();
  llvm::DenseMap<const Type*, CGRecordLayout *>::iterator I
    = CGRecordLayouts.find(Key);
  assert (I != CGRecordLayouts.end() 
          && "Unable to find record layout information for type");
  return I->second;
}

/// layoutStructFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.
/// Note that this doesn't actually try to do struct layout; it depends on
/// the layout built by the AST.  (We have to do struct layout to do Sema,
/// and there's no point to duplicating the work.)
void RecordOrganizer::layoutStructFields(const ASTRecordLayout &RL) {
  // FIXME: This code currently always generates packed structures.
  // Unpacked structures are more readable, and sometimes more efficient!
  // (But note that any changes here are likely to impact CGExprConstant,
  // which makes some messy assumptions.)
  uint64_t llvmSize = 0;
  // FIXME: Make this a SmallVector
  std::vector<const llvm::Type*> LLVMFields;

  unsigned curField = 0;
  for (RecordDecl::field_iterator Field = RD.field_begin(),
                               FieldEnd = RD.field_end();
       Field != FieldEnd; ++Field) {
    uint64_t offset = RL.getFieldOffset(curField);
    const llvm::Type *Ty = CGT.ConvertTypeRecursive(Field->getType());
    uint64_t size = CGT.getTargetData().getTypePaddedSizeInBits(Ty);

    if (Field->isBitField()) {
      Expr *BitWidth = Field->getBitWidth();
      llvm::APSInt FieldSize(32);
      bool isBitField =
        BitWidth->isIntegerConstantExpr(FieldSize, CGT.getContext());
      assert(isBitField && "Invalid BitField size expression");
      isBitField=isBitField; // silence warning.
      uint64_t BitFieldSize = FieldSize.getZExtValue();

      // Bitfield field info is different from other field info;
      // it actually ignores the underlying LLVM struct because
      // there isn't any convenient mapping.
      CGT.addFieldInfo(*Field, offset / size);
      CGT.addBitFieldInfo(*Field, offset % size, BitFieldSize);
    } else {
      // Put the element into the struct. This would be simpler
      // if we didn't bother, but it seems a bit too strange to
      // allocate all structs as i8 arrays.
      while (llvmSize < offset) {
        LLVMFields.push_back(llvm::Type::Int8Ty);
        llvmSize += 8;
      }

      llvmSize += size;
      CGT.addFieldInfo(*Field, LLVMFields.size());
      LLVMFields.push_back(Ty);
    }
    ++curField;
  }

  while (llvmSize < RL.getSize()) {
    LLVMFields.push_back(llvm::Type::Int8Ty);
    llvmSize += 8;
  }

  STy = llvm::StructType::get(LLVMFields, true);
  assert(CGT.getTargetData().getTypePaddedSizeInBits(STy) == RL.getSize());
}

/// layoutUnionFields - Do the actual work and lay out all fields. Create
/// corresponding llvm struct type.  This should be invoked only after
/// all fields are added.
void RecordOrganizer::layoutUnionFields(const ASTRecordLayout &RL) {
  unsigned curField = 0;
  for (RecordDecl::field_iterator Field = RD.field_begin(),
                               FieldEnd = RD.field_end();
       Field != FieldEnd; ++Field) {
    // The offset should usually be zero, but bitfields could be strange
    uint64_t offset = RL.getFieldOffset(curField);
    CGT.ConvertTypeRecursive(Field->getType());

    if (Field->isBitField()) {
      Expr *BitWidth = Field->getBitWidth();
      uint64_t BitFieldSize =  
        BitWidth->getIntegerConstantExprValue(CGT.getContext()).getZExtValue();

      CGT.addFieldInfo(*Field, 0);
      CGT.addBitFieldInfo(*Field, offset, BitFieldSize);
    } else {
      CGT.addFieldInfo(*Field, 0);
    }
    ++curField;
  }

  // This looks stupid, but it is correct in the sense that
  // it works no matter how complicated the sizes and alignments
  // of the union elements are. The natural alignment
  // of the result doesn't matter because anyone allocating
  // structures should be aligning them appropriately anyway.
  // FIXME: We can be a bit more intuitive in a lot of cases.
  STy = llvm::ArrayType::get(llvm::Type::Int8Ty, RL.getSize() / 8);
  assert(CGT.getTargetData().getTypePaddedSizeInBits(STy) == RL.getSize());
}
