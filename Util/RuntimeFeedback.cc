#include "Python.h"
#include "Util/RuntimeFeedback.h"

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

using llvm::PointerIntPair;
using llvm::PointerLikeTypeTraits;
using llvm::SmallPtrSet;
using llvm::SmallVector;

const int PyLimitedFeedback::num_pointers_;

void
PyLimitedFeedback::SetFlagBit(unsigned index, bool value)
{
    assert(index < 6);
    PointerIntPair<PyObject*, 2>& slot = this->data_[index / 2];
    unsigned mask = 1 << (index % 2);
    unsigned old_value = slot.getInt();
    unsigned new_value = (old_value & ~mask) | (value << (index % 2));
    slot.setInt(new_value);
}

bool
PyLimitedFeedback::GetFlagBit(unsigned index) const
{
    assert(index < 6);
    const PointerIntPair<PyObject*, 2>& slot = this->data_[index / 2];
    unsigned value = slot.getInt();
    return (value >> (index % 2)) & 1;
}

void
PyLimitedFeedback::IncCounter(unsigned counter_id)
{
    assert(counter_id < (unsigned)PyLimitedFeedback::num_pointers_);
    uintptr_t old_value =
        reinterpret_cast<uintptr_t>(this->data_[counter_id].getPointer());
    uintptr_t shift = PointerLikeTypeTraits<PyObject*>::NumLowBitsAvailable;
    uintptr_t new_value = old_value + (1U << shift);
    if (new_value > old_value) {
        // Only increment if we're not saturated yet.
        this->data_[counter_id].setPointer(
            reinterpret_cast<PyObject*>(new_value));
    }
}

uintptr_t
PyLimitedFeedback::GetCounter(unsigned counter_id)
{
    uintptr_t shift = PointerLikeTypeTraits<PyObject*>::NumLowBitsAvailable;
    PyObject *counter_as_pointer = this->data_[counter_id].getPointer();
    return reinterpret_cast<uintptr_t>(counter_as_pointer) >> shift;
}

void
PyLimitedFeedback::AddTypeSeen(PyObject *obj)
{
    if (obj == NULL) {
        SetFlagBit(SAW_A_NULL_OBJECT_BIT, true);
        return;
    }
    PyObject *type = (PyObject *)Py_TYPE(obj);
    for (int i = 0; i < PyLimitedFeedback::num_pointers_; ++i) {
        PyObject *value = data_[i].getPointer();
        if (value == (PyObject*)type)
            return;
        if (value == NULL) {
            data_[i].setPointer((PyObject*)type);
            return;
        }
    }
    // Record overflow.
    SetFlagBit(SAW_MORE_THAN_THREE_TYPES_BIT, true);
}

void
PyLimitedFeedback::GetSeenTypesInto(
    SmallVector<PyTypeObject*, 3> &result) const
{
    result.clear();
    if (GetFlagBit(SAW_A_NULL_OBJECT_BIT)) {
        // Saw a NULL value, so add NULL to the result.
        result.push_back(NULL);
    }
    for (int i = 0; i < PyLimitedFeedback::num_pointers_; ++i) {
        PyObject *value = data_[i].getPointer();
        if (value == NULL)
            return;
        result.push_back((PyTypeObject*)value);
    }
}

PyFullFeedback::PyFullFeedback()
    : counters_(/* Zero out the array. */)
{
}

void
PyFullFeedback::AddTypeSeen(PyObject *obj)
{
    if (obj == NULL)
        this->data_.insert(NULL);
    else
        this->data_.insert(Py_TYPE(obj));
}

void
PyFullFeedback::GetSeenTypesInto(
    SmallVector<PyTypeObject*, /*in-object elems=*/3> &result) const
{
    result.clear();
    for (SmallPtrSet<PyTypeObject*, 3>::const_iterator it = this->data_.begin(),
             end = this->data_.end(); it != end; ++it) {
        result.push_back(*it);
    }
}

void
PyFullFeedback::IncCounter(unsigned counter_id)
{
    assert(counter_id < llvm::array_lengthof(this->counters_));
    uintptr_t old_value = this->counters_[counter_id];
    uintptr_t new_value = old_value + 1;
    if (new_value > old_value) {
        // Only increment if we're not saturated yet.
        this->counters_[counter_id] = new_value;
    }
}

uintptr_t
PyFullFeedback::GetCounter(unsigned counter_id)
{
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

PyRuntimeFeedback &
PyFeedbackMap::GetOrCreateFeedbackEntry(
    unsigned opcode_index, unsigned arg_index)
{
    return this->entries_[std::make_pair(opcode_index, arg_index)];
}
