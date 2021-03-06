#include "Python.h"
#include "Util/RuntimeFeedback.h"

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>

using llvm::PointerIntPair;
using llvm::PointerLikeTypeTraits;
using llvm::SmallPtrSet;
using llvm::SmallVector;


FunctionRecord::FunctionRecord(const PyObject *func)
{
    this->func = PyCFunction_GET_FUNCTION(func);
    this->flags = PyCFunction_GET_FLAGS(func);
    this->name = PyCFunction_GET_METHODDEF(func)->ml_name;
    this->min_arity = -1;
    this->max_arity = -1;

    if (this->flags & METH_ARG_RANGE) {
        this->min_arity = PyCFunction_GET_MIN_ARITY(func);
        this->max_arity = PyCFunction_GET_MAX_ARITY(func);
    }
}

FunctionRecord::FunctionRecord(const FunctionRecord &record)
{
    this->func = record.func;
    this->flags = record.flags;
    this->name = record.name;
    this->min_arity = record.min_arity;
    this->max_arity = record.max_arity;
}


static bool
is_duplicate_method(PyObject *a, FunctionRecord *b)
{
    return PyCFunction_Check(a) && PyCFunction_GET_FUNCTION(a) == b->func;
}

PyLimitedFeedback::PyLimitedFeedback()
{
}

PyLimitedFeedback::PyLimitedFeedback(const PyLimitedFeedback &src)
{
    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        if (src.InObjectMode()) {
            PyObject *value = (PyObject *)src.data_[i].getPointer();
            Py_XINCREF(value);
            this->data_[i] = src.data_[i];
        }
        else if (src.InFuncMode() && src.data_[i].getPointer() != NULL) {
            FunctionRecord *src_record =
                    (FunctionRecord *)src.data_[i].getPointer();
            FunctionRecord *new_record = new FunctionRecord(*src_record);
            this->data_[i].setInt(src.data_[i].getInt());
            this->data_[i].setPointer(new_record);
        }
        else {
            this->data_[i] = src.data_[i];
        }
    }
}

PyLimitedFeedback::~PyLimitedFeedback()
{
    this->Clear();
}

PyLimitedFeedback &
PyLimitedFeedback::operator=(PyLimitedFeedback rhs)
{
    this->Swap(&rhs);
    return *this;
}

void
PyLimitedFeedback::Swap(PyLimitedFeedback *other)
{
    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        std::swap(this->data_[i], other->data_[i]);
    }
}

void
PyLimitedFeedback::SetFlagBit(unsigned index, bool value)
{
    assert(index < 6);
    PointerIntPair<void*, 2>& slot = this->data_[index / 2];
    unsigned mask = 1 << (index % 2);
    unsigned old_value = slot.getInt();
    unsigned new_value = (old_value & ~mask) | (value << (index % 2));
    slot.setInt(new_value);
}

bool
PyLimitedFeedback::GetFlagBit(unsigned index) const
{
    assert(index < 6);
    const PointerIntPair<void*, 2>& slot = this->data_[index / 2];
    unsigned value = slot.getInt();
    return (value >> (index % 2)) & 1;
}

void
PyLimitedFeedback::IncCounter(unsigned counter_id)
{
    assert(this->InCounterMode());
    assert(counter_id < (unsigned)PyLimitedFeedback::NUM_POINTERS);
    this->SetFlagBit(COUNTER_MODE_BIT, true);

    uintptr_t old_value =
        reinterpret_cast<uintptr_t>(this->data_[counter_id].getPointer());
    uintptr_t shift = PointerLikeTypeTraits<PyObject*>::NumLowBitsAvailable;
    uintptr_t new_value = old_value + (1U << shift);
    if (new_value > old_value) {
        // Only increment if we're not saturated yet.
        this->data_[counter_id].setPointer(
            reinterpret_cast<void*>(new_value));
    }
}

uintptr_t
PyLimitedFeedback::GetCounter(unsigned counter_id) const
{
    assert(this->InCounterMode());

    uintptr_t shift = PointerLikeTypeTraits<PyObject*>::NumLowBitsAvailable;
    void *counter_as_pointer = this->data_[counter_id].getPointer();
    return reinterpret_cast<uintptr_t>(counter_as_pointer) >> shift;
}

void
PyLimitedFeedback::Clear()
{
    bool object_mode = this->InObjectMode();
    bool func_mode = this->InFuncMode();

    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        if (object_mode)
            Py_XDECREF((PyObject *)this->data_[i].getPointer());
        else if (func_mode)
            delete (FunctionRecord *)this->data_[i].getPointer();
        this->data_[i].setPointer(NULL);
        this->data_[i].setInt(0);
    }
}

void
PyLimitedFeedback::AddObjectSeen(PyObject *obj)
{
    assert(this->InObjectMode());
    this->SetFlagBit(OBJECT_MODE_BIT, true);

    if (obj == NULL) {
        SetFlagBit(SAW_A_NULL_OBJECT_BIT, true);
        return;
    }
    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        PyObject *value = (PyObject *)data_[i].getPointer();
        if (value == obj)
            return;
        if (value == NULL) {
            Py_INCREF(obj);
            data_[i].setPointer((void *)obj);
            return;
        }
    }
    // Record overflow.
    SetFlagBit(SAW_MORE_THAN_THREE_OBJS_BIT, true);
}

void
PyLimitedFeedback::GetSeenObjectsInto(SmallVector<PyObject*, 3> &result) const
{
    assert(this->InObjectMode());

    result.clear();
    if (GetFlagBit(SAW_A_NULL_OBJECT_BIT)) {
        // Saw a NULL value, so add NULL to the result.
        result.push_back(NULL);
    }
    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        PyObject *value = (PyObject *)data_[i].getPointer();
        if (value == NULL)
            return;
        result.push_back(value);
    }
}

void
PyLimitedFeedback::AddFuncSeen(PyObject *obj)
{
    assert(this->InFuncMode());
    this->SetFlagBit(FUNC_MODE_BIT, true);

    if (this->GetFlagBit(SAW_MORE_THAN_THREE_OBJS_BIT))
        return;
    if (obj == NULL) {
        this->SetFlagBit(SAW_A_NULL_OBJECT_BIT, true);
        return;
    }
    if (!PyCFunction_Check(obj))
        return;

    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        FunctionRecord *value = (FunctionRecord *)this->data_[i].getPointer();
        if (value == NULL) {
            FunctionRecord *record = new FunctionRecord(obj);
            this->data_[i].setPointer((void *)record);
            return;
        }
        // Deal with the fact that "for x in y: l.append(x)" results in 
        // multiple method objects for l.append.
        if (is_duplicate_method(obj, value))
            return;
    }
    // Record overflow.
    this->SetFlagBit(SAW_MORE_THAN_THREE_OBJS_BIT, true);
}

void
PyLimitedFeedback::GetSeenFuncsInto(
    SmallVector<FunctionRecord*, 3> &result) const
{
    assert(this->InFuncMode());

    result.clear();
    if (this->GetFlagBit(SAW_A_NULL_OBJECT_BIT)) {
        // Saw a NULL value, so add NULL to the result.
        result.push_back(NULL);
    }
    for (int i = 0; i < PyLimitedFeedback::NUM_POINTERS; ++i) {
        FunctionRecord *value = (FunctionRecord *)this->data_[i].getPointer();
        if (value == NULL)
            return;
        result.push_back(value);
    }
}


PyFullFeedback::PyFullFeedback()
    : counters_(/* Zero out the array. */),
      usage_(UnknownMode)
{
}

PyFullFeedback::PyFullFeedback(const PyFullFeedback &src)
{
    this->usage_ = src.usage_;
    for (unsigned i = 0; i < llvm::array_lengthof(this->counters_); ++i)
        this->counters_[i] = src.counters_[i];
    for (ObjSet::iterator it = src.data_.begin(), end = src.data_.end();
            it != end; ++it) {
        void *obj = *it;
        if (src.usage_ == ObjectMode) {
            Py_XINCREF((PyObject *)obj);
        }
        else if (src.usage_ == FuncMode) {
            obj = new FunctionRecord(*static_cast<const FunctionRecord*>(*it));
        }
        this->data_.insert(obj);
    }
}

PyFullFeedback::~PyFullFeedback()
{
    this->Clear();
}

PyFullFeedback &
PyFullFeedback::operator=(PyFullFeedback rhs)
{
    this->Swap(&rhs);
    return *this;
}

void
PyFullFeedback::Swap(PyFullFeedback *other)
{
    std::swap(this->usage_, other->usage_);
    std::swap(this->data_, other->data_);
    for (unsigned i = 0; i < llvm::array_lengthof(this->counters_); ++i)
        std::swap(this->counters_[i], other->counters_[i]);
}

void
PyFullFeedback::Clear()
{
    for (ObjSet::iterator it = this->data_.begin(),
            end = this->data_.end(); it != end; ++it) {
        if (this->usage_ == ObjectMode) {
            Py_XDECREF((PyObject *)*it);
        }
        else if (this->usage_ == FuncMode) {
            delete (FunctionRecord *)*it;
        }
    }
    this->data_.clear();
    for (unsigned i = 0; i < llvm::array_lengthof(this->counters_); ++i)
        this->counters_[i] = 0;
    this->usage_ = UnknownMode;
}

void
PyFullFeedback::AddObjectSeen(PyObject *obj)
{
    assert(this->InObjectMode());
    this->usage_ = ObjectMode;

    if (obj == NULL) {
        this->data_.insert(NULL);
        return;
    }

    if (!this->data_.count(obj)) {
        Py_INCREF(obj);
        this->data_.insert((void *)obj);
    }
}

void
PyFullFeedback::GetSeenObjectsInto(
    SmallVector<PyObject*, /*in-object elems=*/3> &result) const
{
    assert(this->InObjectMode());

    result.clear();
    for (ObjSet::const_iterator it = this->data_.begin(),
             end = this->data_.end(); it != end; ++it) {
        result.push_back((PyObject *)*it);
    }
}

void
PyFullFeedback::AddFuncSeen(PyObject *obj)
{
    assert(this->InFuncMode());
    this->usage_ = FuncMode;

    // We only record C functions for now.
    if (!PyCFunction_Check(obj))
        return;
    if (obj == NULL)
        this->data_.insert(NULL);
    else if (!this->data_.count(obj)) {
        // Deal with the fact that "for x in y: l.append(x)" results in 
        // multiple method objects for l.append.
        for (ObjSet::const_iterator it = this->data_.begin(),
                end = this->data_.end(); it != end; ++it) {
            if (is_duplicate_method(obj, (FunctionRecord *)*it))
                return;
        }

        FunctionRecord *record = new FunctionRecord(obj);
        this->data_.insert((void *)record);
    }
}

void
PyFullFeedback::GetSeenFuncsInto(
    SmallVector<FunctionRecord*, /*in-object elems=*/3> &result) const
{
    assert(this->InFuncMode());

    result.clear();
    for (ObjSet::const_iterator it = this->data_.begin(),
            end = this->data_.end(); it != end; ++it) {
        result.push_back((FunctionRecord *)*it);
    }
}

void
PyFullFeedback::IncCounter(unsigned counter_id)
{
    assert(this->InCounterMode());
    assert(counter_id < llvm::array_lengthof(this->counters_));
    this->usage_ = CounterMode;

    uintptr_t old_value = this->counters_[counter_id];
    uintptr_t new_value = old_value + 1;
    if (new_value > old_value) {
        // Only increment if we're not saturated yet.
        this->counters_[counter_id] = new_value;
    }
}

uintptr_t
PyFullFeedback::GetCounter(unsigned counter_id) const
{
    assert(this->InCounterMode());

    return this->counters_[counter_id];
}

PyFeedbackMap *
PyFeedbackMap_New()
{
    return new PyFeedbackMap;
}

void
PyFeedbackMap_Del(PyFeedbackMap *map)
{
    delete map;
}

void
PyFeedbackMap_Clear(PyFeedbackMap *map)
{
    map->Clear();
}

const PyRuntimeFeedback *
PyFeedbackMap::GetFeedbackEntry(unsigned opcode_index, unsigned arg_index) const
{
    llvm::DenseMap<std::pair<unsigned, unsigned>,
                   PyRuntimeFeedback>::const_iterator result =
        this->entries_.find(std::make_pair(opcode_index, arg_index));
    if (result == this->entries_.end())
        return NULL;
    return &result->second;
}

PyRuntimeFeedback &
PyFeedbackMap::GetOrCreateFeedbackEntry(
    unsigned opcode_index, unsigned arg_index)
{
    return this->entries_[std::make_pair(opcode_index, arg_index)];
}

void
PyFeedbackMap::Clear()
{
    for (FeedbackMap::iterator it = this->entries_.begin(),
            end = this->entries_.end(); it != end; ++it) {
        it->second.Clear();
    }
}
