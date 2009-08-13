// -*- C++ -*-
//
// This file defines PyRuntimeFeedback as the basic unit of feedback
// data.  It is capable of holding either a set of seen types or three
// counters, but not both at the same time.  Use the AddTypeSeen(),
// GetSeenTypesInto(), and TypesOverflowed() methods to store types,
// or the IncCounter() and GetCounter() methods to access the
// counters.  Trying to use both modes with the same instance will result
// in a fatal error.
//
// We provide two implementations of this interface to make it easy to
// switch between a memory-efficient representation and a
// representation that can store all the data we could possibly
// collect.  PyLimitedFeedback stores up to three types, while
// PyFullFeedback uses an unbounded set.

#ifndef UTIL_RUNTIMEFEEDBACK_H
#define UTIL_RUNTIMEFEEDBACK_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "RuntimeFeedback_fwd.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace llvm {
template<typename, unsigned> class SmallVector;
}

// These are the counters used for feedback in the JUMP_IF opcodes.
// The number of boolean inputs can be computed as (PY_FDO_JUMP_TRUE +
// PY_FDO_JUMP_FALSE - PY_FDO_JUMP_NON_BOOLEAN).
enum { PY_FDO_JUMP_TRUE = 0, PY_FDO_JUMP_FALSE, PY_FDO_JUMP_NON_BOOLEAN };

class PyLimitedFeedback {
public:
    PyLimitedFeedback();
    PyLimitedFeedback(const PyLimitedFeedback &src);
    ~PyLimitedFeedback();

    // Records that the type of obj has been seen.
    void AddTypeSeen(PyObject *obj);
    // Clears result and fills it with the set of seen types.
    void GetSeenTypesInto(llvm::SmallVector<PyTypeObject*, 3> &result);
    bool TypesOverflowed() const {
        return GetFlagBit(SAW_MORE_THAN_THREE_TYPES_BIT);
    }
    // Clears out the collected types and counters.
    void Clear();

    // There are three counters available.  Their storage space
    // overlaps with the type record, so you can't use both.  They
    // saturate rather than wrapping on overflow.
    void IncCounter(unsigned counter_id);
    uintptr_t GetCounter(unsigned counter_id);

    // Assignment copies the list of collected types, fixing up refcounts.
    PyLimitedFeedback &operator=(const PyLimitedFeedback &rhs);

private:
    // 'index' must be between 0 and 5 inclusive.
    void SetFlagBit(unsigned index, bool value);
    bool GetFlagBit(unsigned index) const;

    enum { NUM_POINTERS  = 3 };
    enum Bits {
    // We have 6 bits available here to use to store flags (we get 2
    // bits at the bottom of each pointer on 32-bit systems, where
    // objects are generally aligned to 4-byte boundaries). These are
    // used as follows:
    //   0: True if we saw more than 3 types.
        SAW_MORE_THAN_THREE_TYPES_BIT = 0,
    //   1: True if we got a NULL object.
        SAW_A_NULL_OBJECT_BIT = 1,
    //   2: True if this instance is being used in counter mode.
        COUNTER_MODE_BIT = 2,
    //   3: True if this instance is being used in type mode.
        TYPE_MODE_BIT = 3,
    //   4: Unused.
    //   5: Unused.
    };
    //
    // The pointers in this array start out NULL and are filled from
    // the lowest index as we see new types.
    llvm::PointerIntPair<PyObject*, /*bits used from bottom of pointer=*/2>
        data_[NUM_POINTERS];

    bool InTypeMode() const {
        return GetFlagBit(TYPE_MODE_BIT) || !GetFlagBit(COUNTER_MODE_BIT);
    }
    bool InCounterMode() const {
        return GetFlagBit(COUNTER_MODE_BIT) || !GetFlagBit(TYPE_MODE_BIT);
    }
};

class PyFullFeedback {
public:
    PyFullFeedback();
    PyFullFeedback(const PyFullFeedback &src);
    ~PyFullFeedback();

    // Records that the type of obj has been seen.
    void AddTypeSeen(PyObject *obj);
    // Clears result and fills it with the set of seen types.
    void GetSeenTypesInto(llvm::SmallVector<PyTypeObject*, 3> &result);
    bool TypesOverflowed() const { return false; }
    // Clears out the collected types and counters.
    void Clear();

    void IncCounter(unsigned counter_id);
    uintptr_t GetCounter(unsigned counter_id);

    // Assignment copies the list of collected types, fixing up refcounts.
    PyFullFeedback &operator=(const PyFullFeedback &rhs);

private:
    // Assume three PyTypeObject pointers in the set to start with.
    typedef llvm::SmallPtrSet<PyTypeObject*, 3> TypeSet;

    TypeSet data_;
    uintptr_t counters_[3];

    enum UsageMode {
        UnknownMode,
        CounterMode,
        TypeMode,
    };
    UsageMode usage_;
};

typedef PyLimitedFeedback PyRuntimeFeedback;

// "struct" to make C and VC++ happy at the same time.
struct PyFeedbackMap {
    PyRuntimeFeedback &GetOrCreateFeedbackEntry(
        unsigned opcode_index, unsigned arg_index);

    void Clear();

private:
    // The key is a (opcode_index, arg_index) pair.
    typedef std::pair<unsigned, unsigned> FeedbackKey;
    typedef llvm::DenseMap<FeedbackKey, PyRuntimeFeedback> FeedbackMap;

    FeedbackMap entries_;
};

#endif  // UTIL_RUNTIMEFEEDBACK_H
