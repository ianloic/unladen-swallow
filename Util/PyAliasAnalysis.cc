#include "Util/PyAliasAnalysis.h"

#include "Util/PyTypeBuilder.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"
#include "llvm/Support/ConstantRange.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Target/TargetData.h"
#include <utility>

namespace {

using llvm::APInt;
using llvm::AliasAnalysis;
using llvm::ConstantExpr;
using llvm::ConstantInt;
using llvm::ConstantRange;
using llvm::ConstantStruct;
using llvm::DenseMap;
using llvm::Function;
using llvm::FunctionPass;
using llvm::GlobalVariable;
using llvm::LLVMContext;
using llvm::Module;
using llvm::Pass;
using llvm::PointerType;
using llvm::SCEV;
using llvm::SCEVUnknown;
using llvm::ScalarEvolution;
using llvm::SmallPtrSet;
using llvm::StructLayout;
using llvm::StructType;
using llvm::TargetData;
using llvm::Type;
using llvm::Value;
using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::isa;

// This AliasAnalysis pass captures Python-specific knowledge about
// aliasing and constants.
//
// Aliasing:
//  1. We know that the _Py_TracingPossible global variable doesn't
//  alias any pointers.  This is important because it gets read a lot
//  and inhibits other optimizations when the alias analyses don't
//  know about it.
//
// Constants:
//  1. Certain types are constant (except for their refcounts),
//  certain types prevent the user from changing the type of their
//  instances, and certain types have constant or almost-constant
//  instances.  Specifying this lets the optimizers optimize based on
//  the results of type guards (although they don't yet have enough
//  other information to do this yet).  We use ScalarEvolution to find
//  which fields in a GlobalVariable a Value* points to, which, along
//  with the identity of the GlobalVariable, tells us whether it's
//  constant.
class PyAliasAnalysis : public FunctionPass, public AliasAnalysis {
public:
    static char ID;
    PyAliasAnalysis(PyGlobalLlvmData &global_data)
        : FunctionPass(&ID), context_(&global_data.context()),
          global_data_(global_data),
          tracing_possible_(NULL) {}

    // Exists for the pass registration infrastructure.
    PyAliasAnalysis()
        : FunctionPass(&ID), context_(NULL),
          global_data_(*PyGlobalLlvmData::Get()),
          tracing_possible_(NULL) {}

    virtual void getAnalysisUsage(llvm::AnalysisUsage &usage) const {
        AliasAnalysis::getAnalysisUsage(usage);
        // Used to canonicalize a value into an underlying
        // GlobalVariable and an offset, if possible.
        usage.addRequiredTransitive<ScalarEvolution>();
        usage.setPreservesAll();
    }

    virtual bool doInitialization(Module&);
    virtual bool runOnFunction(Function&);

    virtual AliasResult alias(const Value *V1, unsigned V1Size,
                              const Value *V2, unsigned V2Size);
    virtual bool pointsToConstantMemory(const Value *V);

private:
    // True if V points inside a PyObject to a field we know is
    // constant, usually by examining the type of the object.
    bool PointsToConstFieldInPyObject(const Value *V);

    bool IsPyObject(const GlobalVariable *gv);

    // Returns the range of bytes [first..second) that type.field_index
    // occupies.
    std::pair<uint64_t, uint64_t> GetByteRangeOfField(
        const StructType *type, int field_index);

    // True if 'value', which points inside 'containing_object', may overlap the
    // range of bytes
    // [containing_object+range.first..containing_object+range.second).
    bool MayOverlapByteRange(const SCEV *containing_object,
                             const SCEV *value,
                             const std::pair<uint64_t, uint64_t> &range);

    // These are the values for the types_with_constant_values_ map. They're
    // called when we already know that the field isn't the refcount or any
    // other mutable PyObject field.
    bool FullyConstant(const SCEV*, const SCEV*) { return true; }
    bool IsConstStringField(const SCEV *string_gv, const SCEV *field);
    bool IsConstUnicodeField(const SCEV *unicode_gv, const SCEV *field);

    // Registers a builtin type that's known not to change.  This returns the
    // GlobalVariable for the type so we can insert it into the constant-values
    // set if necessary.
    const GlobalVariable *RegisterConstantType(PyTypeObject *type);

    LLVMContext *context_;
    PyGlobalLlvmData &global_data_;

    ScalarEvolution *scev_;

    const GlobalVariable *tracing_possible_;

    // These are GlobalVariables for builtin types that we know are constant.
    SmallPtrSet<const GlobalVariable*, 8> constant_types_;
    // And this is the subset of constant_types_ whose values are also (partly)
    // constant.  It maps to a method that tells whether the value is actually
    // constant.
    DenseMap<const GlobalVariable*,
             bool (PyAliasAnalysis::*)(const SCEV *gv, const SCEV *v)>
        types_with_constant_values_;
    // Things like the PyFooMethods fields in constant PyTypeObjects.
    SmallPtrSet<const GlobalVariable*, 8> fully_constant_values_;
    // Special case because one field is mutable.
    GlobalVariable *string_type_;
};

// The address of this variable identifies the pass.  See
// http://llvm.org/docs/WritingAnLLVMPass.html#basiccode.
char PyAliasAnalysis::ID = 0;

// Register this pass.
static llvm::RegisterPass<PyAliasAnalysis>
U("python-aa", "Python-specific Alias Analysis", false, true);

// Declare that we implement the AliasAnalysis interface.
static llvm::RegisterAnalysisGroup<AliasAnalysis> V(U);

bool
PyAliasAnalysis::doInitialization(Module& module)
{
    this->context_ = &module.getContext();

    this->tracing_possible_ = module.getGlobalVariable("_Py_TracingPossible");

    this->constant_types_.clear();
    this->types_with_constant_values_.clear();
    this->fully_constant_values_.clear();

    this->types_with_constant_values_[
        this->RegisterConstantType(&PyInt_Type)] =
        &PyAliasAnalysis::FullyConstant;
    this->types_with_constant_values_[
        this->RegisterConstantType(&PyLong_Type)] =
        &PyAliasAnalysis::FullyConstant;
    this->types_with_constant_values_[
        this->RegisterConstantType(&PyString_Type)] =
        &PyAliasAnalysis::IsConstStringField;
    this->types_with_constant_values_[
        this->RegisterConstantType(&PyUnicode_Type)] =
        &PyAliasAnalysis::IsConstUnicodeField;
    this->types_with_constant_values_[
        this->RegisterConstantType(&PyTuple_Type)] =
        &PyAliasAnalysis::FullyConstant;
    this->RegisterConstantType(&PyType_Type);
    this->RegisterConstantType(&PyList_Type);
    this->RegisterConstantType(&PyDict_Type);
    this->RegisterConstantType(&PySet_Type);
    this->RegisterConstantType(&PyFrozenSet_Type);

    return false;
}

const GlobalVariable *
PyAliasAnalysis::RegisterConstantType(PyTypeObject *type)
{
    PyConstantMirror &constant_mirror = this->global_data_.constant_mirror();
    const GlobalVariable *type_var = dyn_cast<GlobalVariable>(
        constant_mirror.GetGlobalVariableFor((PyObject*)type));
    this->constant_types_.insert(type_var);
    if (const ConstantStruct *type_value =
        dyn_cast<ConstantStruct>(type_var->getInitializer())) {
#define ADD_CONSTANT(FIELD_NAME) \
        this->fully_constant_values_.insert( \
            dyn_cast<GlobalVariable>( \
                type_value->getOperand( \
                    py::TypeTy::FIELD_NAME##_index(*this->context_))))

        ADD_CONSTANT(tp_as_number);
        ADD_CONSTANT(tp_as_sequence);
        ADD_CONSTANT(tp_as_mapping);
        ADD_CONSTANT(tp_as_buffer);
#undef ADD_CONSTANT
    }

    return type_var;
}

bool
PyAliasAnalysis::runOnFunction(Function &f)
{
    // If this pass is run through a FunctionPassManager, it doesn't run
    // doInitialization.
    if (this->constant_types_.empty())
        this->doInitialization(*f.getParent());

    AliasAnalysis::InitializeAliasAnalysis(this);
    this->scev_ = &getAnalysis<ScalarEvolution>();
    return false;
}

AliasAnalysis::AliasResult
PyAliasAnalysis::alias(const Value *V1, unsigned V1Size,
                       const Value *V2, unsigned V2Size)
{
    if (V1 == V2)
        return MustAlias;
    // No code copies the address of _Py_TracingPossible, so it can't
    // alias any other pointer.
    if (V1 == this->tracing_possible_ || V2 == this->tracing_possible_)
        return NoAlias;
    return AliasAnalysis::alias(V1, V1Size, V2, V2Size);
}

bool
PyAliasAnalysis::pointsToConstantMemory(const Value *v)
{
    if (this->PointsToConstFieldInPyObject(v))
        return true;
    return AliasAnalysis::pointsToConstantMemory(v);
}

// Copied and adapted from ScalarEvolutionAliasAnalysis.
const SCEVUnknown *
GetUnderlyingGlobalVariableSCEV(const SCEV *S) {
  if (const llvm::SCEVAddRecExpr *AR = dyn_cast<llvm::SCEVAddRecExpr>(S)) {
    // In an addrec, assume that the base will be in the start, rather
    // than the step.
    return GetUnderlyingGlobalVariableSCEV(AR->getStart());
  } else if (const llvm::SCEVAddExpr *A = dyn_cast<llvm::SCEVAddExpr>(S)) {
    // If there's a pointer operand, it'll be sorted at the end of the list.
    const SCEV *Last = A->getOperand(A->getNumOperands()-1);
    if (isa<PointerType>(Last->getType()))
      return GetUnderlyingGlobalVariableSCEV(Last);
  } else if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    // Determine if we've found a GlobalVariable.
    if (isa<GlobalVariable>(U->getValue()))
        return U;
  }
  // No GlobalVariable found.
  return 0;
}

bool
PyAliasAnalysis::MayOverlapByteRange(const SCEV *containing_object,
                                     const SCEV *value,
                                     const std::pair<uint64_t, uint64_t> &range)
{
    uint64_t value_length = getTargetData()->getTypeAllocSize(value->getType());

    ConstantRange offset_in_object = this->scev_->getUnsignedRange(
        this->scev_->getMinusSCEV(value, containing_object));
    // 'value' may start anywhere between offset.getLower() and
    // offset.getUpper() (which may wrap around the integers % 1<<bit_width).
    // It occupies bytes lower..(upper+value_length). The field occupies bytes
    // field_offset..(field_offset+field_length).  If these ranges intersect, we
    // return true.  The additions may overflow, but checking for an
    // intersection mod 1<<bit_width is more conservative than checking for an
    // intersection on the full integers.
    unsigned bit_width = offset_in_object.getBitWidth();
    ConstantRange value_byte_range = offset_in_object.add(
        ConstantRange(APInt(bit_width, 0),
                      APInt(bit_width, value_length)));
    ConstantRange field_byte_range(APInt(bit_width, range.first),
                                   APInt(bit_width, range.second));
    return !field_byte_range.intersectWith(value_byte_range).isEmptySet();
}

std::pair<uint64_t, uint64_t>
PyAliasAnalysis::GetByteRangeOfField(const StructType *type, int field_index)
{
    const TargetData &td = *this->getTargetData();
    uint64_t field_offset =
        td.getStructLayout(type)->getElementOffset(field_index);
    const Type *field_type = type->getElementType(field_index);
    uint64_t field_length = td.getTypeAllocSize(field_type);
    return std::make_pair(field_offset, field_offset + field_length);
}

bool
PyAliasAnalysis::PointsToConstFieldInPyObject(const Value *v)
{
    // Analyze v to figure out what GlobalVariable it points to and
    // what fields it points to inside of that Variable.  If it
    // doesn't point to part of a GlobalVariable, we have to assume
    // it's not constant.
    const SCEV *s = this->scev_->getSCEV(const_cast<Value*>(v));
    const SCEVUnknown *gv_scev = GetUnderlyingGlobalVariableSCEV(s);
    if (gv_scev == NULL)
        return false;
    GlobalVariable *gv = cast<GlobalVariable>(gv_scev->getValue());

    // The GV may be one of the known completely constant variables.
    if (this->fully_constant_values_.count(gv))
        return true;

    // For other non-PyObjects we can't say anything.
    if (!this->IsPyObject(gv))
        return false;

    // The fields up to and including the refcount are mutable.
    const int refcnt_index = py::ObjectTy::ob_refcnt_index(*this->context_);
    std::pair<uint64_t, uint64_t> refcnt_range =
        this->GetByteRangeOfField(py::ObjectTy::get(*this->context_),
                                  refcnt_index);
    if (this->MayOverlapByteRange(gv_scev, s,
                                  std::make_pair(0, refcnt_range.second)))
        return false;

    if (this->constant_types_.count(gv)) {
        // This is a known builtin type that can't change, except for
        // the refcount and a couple fields at the end.
        const StructLayout *type_layout =
            getTargetData()->getStructLayout(py::TypeTy::get(*this->context_));
#ifdef COUNT_ALLOCS
        uint64_t tp_allocs_offset =
            type_layout->getElementOffset(
                py::TypeTy::tp_allocs_index(*this->context_));
#else
        // Use the end of the object to cover heap types, which have some fields
        // tacked on the end.
        uint64_t tp_allocs_offset = type_layout->getSizeInBytes();
#endif
        return !this->MayOverlapByteRange(
            gv_scev, s, std::make_pair<uint64_t>(tp_allocs_offset, ~0ULL));
    }

    ConstantStruct *initial_value =
        cast_or_null<ConstantStruct>(gv->getInitializer());
    if (initial_value == NULL) {
        // We can't decide anything if we don't have an initial value.
        return false;
    }
    GlobalVariable *initial_type = dyn_cast<GlobalVariable>(
        initial_value->getOperand(
            py::ObjectTy::ob_type_index(*this->context_)));
    if (initial_type == NULL)
        return false;

    // If the value doesn't start with a builtin type, we can't trust
    // that its current type is still its original type, because users
    // are allowed to set __class__, and on new-style classes this
    // actually has the effect of changing the type pointer.
    if (!constant_types_.count(initial_type))
        return false;

    // If the type has immutable values, the whole object is constant. If not,
    // only the type field is constant.
    if (bool (PyAliasAnalysis::*is_value_const)(const SCEV*, const SCEV*) =
        this->types_with_constant_values_.lookup(initial_type)) {
        return (this->*is_value_const)(gv_scev, s);
    }

    if (isa<PointerType>(v->getType())) {
        // Right size to be the type pointer.
        const SCEV *offset = this->scev_->getMinusSCEV(s, gv_scev);
        const SCEV *type_field = this->scev_->getFieldOffsetExpr(
            py::ObjectTy::get(*this->context_),
            py::ObjectTy::ob_type_index(*this->context_));
        if (offset == type_field)
            return true;
    }

    return false;
}

bool
PyAliasAnalysis::IsConstStringField(const SCEV *string_gv, const SCEV *field)
{
    // ob_sstate is the only mutable field in PyStringObject because
    // ConstantMirror::GetGlobalVariable hashes the string before mirroring it.
    std::pair<uint64_t, uint64_t> sstate_range =
        this->GetByteRangeOfField(
            py::StringTy::get(*this->context_),
            py::StringTy::ob_sstate_index(*this->context_));
    return !this->MayOverlapByteRange(string_gv, field, sstate_range);
}

bool
PyAliasAnalysis::IsConstUnicodeField(const SCEV *unicode_gv, const SCEV *field)
{
    // defenc is the only mutable field in PyUnicodeObject because
    // ConstantMirror::GetGlobalVariable hashes the unicode before mirroring it.
    std::pair<uint64_t, uint64_t> defenc_range =
        this->GetByteRangeOfField(
            PyTypeBuilder<PyUnicodeObject>::get(*this->context_),
            PyTypeBuilder<PyUnicodeObject>::defenc_index(*this->context_));
    return !this->MayOverlapByteRange(unicode_gv, field, defenc_range);
}

bool
PyAliasAnalysis::IsPyObject(const GlobalVariable *gv)
{
    const StructType *gv_type =
        dyn_cast<StructType>(gv->getType()->getElementType());
    // A non-struct can't be a PyObject.
    if (gv_type == NULL)
        return false;
    const StructType *pyobject_type =
        PyTypeBuilder<PyObject>::get(*this->context_);
    // PyObjects always have at least as many fields as PyObject itself.
    if (gv_type->getNumElements() < pyobject_type->getNumElements())
        return false;
    for (unsigned i = 0, num_pyobject_fields = pyobject_type->getNumElements();
         i < num_pyobject_fields; ++i) {
        // PyObject fields match up to the last field in PyObject itself.
        if (gv_type->getElementType(i) != pyobject_type->getElementType(i))
            return false;
    }
    // For now, we assume that PyTypeObject* is a distinct enough type that no
    // non-PyObject could possibly have it as a field in the right place.
    return true;
}

}  // anonymous namespace

Pass *
CreatePyAliasAnalysis(PyGlobalLlvmData &global_data)
{
    return new PyAliasAnalysis(global_data);
}
