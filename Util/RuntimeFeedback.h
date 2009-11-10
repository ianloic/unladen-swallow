// -*- C++ -*-
//
// This file defines PyRuntimeFeedback as the basic unit of feedback
// data.  Each instance of Py{Limited,Full}Feedback is capable of operating
// in one of several modes: recording Python types, incrementing a set of
// counters, or recording called functions. These modes are mutually exclusive,
// and attempting to mix them is a fatal error.
//
// Use the AddTypeSeen(), GetSeenTypesInto(), and TypesOverflowed() methods to
// store types; the IncCounter() and GetCounter() methods to access the
// counters; or AddFuncSeen(), GetSeenFuncsInto(), and FuncsOverflowed() methods
// to store called functions.
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
#include <string>

namespace llvm {
template<typename, unsigned> class SmallVector;
}

// These are the counters used for feedback in the JUMP_IF opcodes.
// The number of boolean inputs can be computed as (PY_FDO_JUMP_TRUE +
// PY_FDO_JUMP_FALSE - PY_FDO_JUMP_NON_BOOLEAN).
enum { PY_FDO_JUMP_TRUE = 0, PY_FDO_JUMP_FALSE, PY_FDO_JUMP_NON_BOOLEAN };

// We copy data out of PyCFunctionObjects, rather than INCREFing them like we do
// types. We do this to avoid inflating the refcounts for bound methods, which
// may result in delaying or preventing the deallocation of the bound invocant;
// this is especially problematic for files.
class FunctionRecord {
public:
    FunctionRecord(const PyObject *func);
    FunctionRecord(const FunctionRecord &record);

    PyCFunction func;
    int flags;
    int arity;
    std::string name;
};

class PyLimitedFeedback {
public:
    PyLimitedFeedback();
    PyLimitedFeedback(const PyLimitedFeedback &src);
    ~PyLimitedFeedback();

    // Records that the type of obj has been seen.
    void AddTypeSeen(PyObject *obj);
    // Clears result and fills it with the set of seen types.
    void GetSeenTypesInto(llvm::SmallVector<PyTypeObject*, 3> &result) const;
    bool TypesOverflowed() const {
        return GetFlagBit(SAW_MORE_THAN_THREE_OBJS_BIT);
    }

    // Record that a given function was called.
    void AddFuncSeen(PyObject *obj);
    // Clears result and fills it with the set of observed FunctionRecords.
    void GetSeenFuncsInto(llvm::SmallVector<FunctionRecord*, 3> &result) const;
    bool FuncsOverflowed() const {
        return GetFlagBit(SAW_MORE_THAN_THREE_OBJS_BIT);
    }

    // There are three counters available.  Their storage space
    // overlaps with the type record, so you can't use both.  They
    // saturate rather than wrapping on overflow.
    void IncCounter(unsigned counter_id);
    uintptr_t GetCounter(unsigned counter_id) const;

    // Clears out the collected types, functions and counters.
    void Clear();

    // Assignment copies the list of collected types, fixing up refcounts.
    PyLimitedFeedback &operator=(PyLimitedFeedback rhs);

private:
    // 'index' must be between 0 and 5 inclusive.
    void SetFlagBit(unsigned index, bool value);
    bool GetFlagBit(unsigned index) const;

    void Swap(PyLimitedFeedback *other);

    enum { NUM_POINTERS  = 3 };
    enum Bits {
    // We have 6 bits available here to use to store flags (we get 2
    // bits at the bottom of each pointer on 32-bit systems, where
    // objects are generally aligned to 4-byte boundaries). These are
    // used as follows:
    //   0: True if we saw more than 3 objects.
        SAW_MORE_THAN_THREE_OBJS_BIT = 0,
    //   1: True if we got a NULL object.
        SAW_A_NULL_OBJECT_BIT = 1,
    //   2: True if this instance is being used in counter mode.
        COUNTER_MODE_BIT = 2,
    //   3: True if this instance is being used in type-gathering mode.
        TYPE_MODE_BIT = 3,
    //   4: True if this instance is being used in function-gathering mode.
        FUNC_MODE_BIT = 4,
    //   5: Unused.
    };
    //
    // The pointers in this array start out NULL and are filled from
    // the lowest index as we see new types. We store either PyObject *s (when
    // operating in type mode) or FunctionRecord *s (in function mode).
    llvm::PointerIntPair<void*, /*bits used from bottom of pointer=*/2>
        data_[NUM_POINTERS];

    bool InTypeMode() const {
        return GetFlagBit(TYPE_MODE_BIT) ||
            !(GetFlagBit(COUNTER_MODE_BIT) || GetFlagBit(FUNC_MODE_BIT));
    }
    bool InCounterMode() const {
        return GetFlagBit(COUNTER_MODE_BIT) ||
            !(GetFlagBit(TYPE_MODE_BIT) || GetFlagBit(FUNC_MODE_BIT));
    }
    bool InFuncMode() const {
        return GetFlagBit(FUNC_MODE_BIT) ||
            !(GetFlagBit(TYPE_MODE_BIT) || GetFlagBit(COUNTER_MODE_BIT));
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
    void GetSeenTypesInto(llvm::SmallVector<PyTypeObject*, 3> &result) const;
    bool TypesOverflowed() const { return false; }

    // Record that a given function was called.
    void AddFuncSeen(PyObject *obj);
    // Clears result and fills it with the set of seen function objects.
    void GetSeenFuncsInto(llvm::SmallVector<FunctionRecord*, 3> &result) const;
    bool FuncsOverflowed() const { return false; }

    void IncCounter(unsigned counter_id);
    uintptr_t GetCounter(unsigned counter_id) const;

    // Clears out the collected types and counters.
    void Clear();

    // Assignment copies the list of collected types, fixing up refcounts.
    PyFullFeedback &operator=(PyFullFeedback rhs);

private:
    // Assume three pointers in the set to start with. We store either
    // PyObject *s (when in type mode) or FunctionRecord *s (when in function
    // mode).
    typedef llvm::SmallPtrSet<void*, 3> ObjSet;

    void Swap(PyFullFeedback *other);

    ObjSet data_;
    uintptr_t counters_[3];

    enum UsageMode {
        UnknownMode,
        CounterMode,
        TypeMode,
        FuncMode,
    };
    UsageMode usage_;

    bool InTypeMode() const {
        return usage_ == TypeMode || usage_ == UnknownMode;
    }
    bool InFuncMode() const {
        return usage_ == FuncMode || usage_ == UnknownMode;
    }
    bool InCounterMode() const {
        return usage_ == CounterMode || usage_ == UnknownMode;
    }
};

typedef PyLimitedFeedback PyRuntimeFeedback;

// "struct" to make C and VC++ happy at the same time.
struct PyFeedbackMap {
    PyRuntimeFeedback &GetOrCreateFeedbackEntry(
        unsigned opcode_index, unsigned arg_index);

    const PyRuntimeFeedback *GetFeedbackEntry(
        unsigned opcode_index, unsigned arg_index) const;

    void Clear();

private:
    // The key is a (opcode_index, arg_index) pair.
    typedef std::pair<unsigned, unsigned> FeedbackKey;
    typedef llvm::DenseMap<FeedbackKey, PyRuntimeFeedback> FeedbackMap;

    FeedbackMap entries_;
};

#endif  // UTIL_RUNTIMEFEEDBACK_H
