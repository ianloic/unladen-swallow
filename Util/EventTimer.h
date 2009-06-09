// -*- C++ -*-
#ifndef UTIL_EVENTTIMER_H
#define UTIL_EVENTTIMER_H

#include "Python.h"


#ifdef WITH_TSC

// This must be kept in sync with event_names in EventTimer.cc
typedef enum {
    CALL_START_EVAL,        // Top of CALL_FUNCTION_* opcodes
    CALL_START_LLVM,        // Top of CALL_FUNCTION_* LLVM IRs
    CALL_ENTER_EVAL,        // Top of PyEval_EvalFrame
    CALL_ENTER_PYOBJ_CALL,  // Any call to PyObject_Call from eval.cc
    CALL_ENTER_C,           // Before calling C methods in eval.cc
    CALL_ENTER_LLVM,        // Top of function entry block in LLVM
    CALL_END_EVAL,          // Bottom of CALL_FUNCTION* opcodes
    LLVM_COMPILE_START,     // Before JITing or looking up native code
    LLVM_COMPILE_END,       // After JITing or looking up native code
    JIT_START,              // Start of LLVM jitting
    JIT_END,                // End of LLVM jitting
    EXCEPT_RAISE_EVAL,      // Exception raised in eval loop
    EXCEPT_RAISE_LLVM,      // Exception raised in LLVM
    EXCEPT_CATCH_EVAL,      // Exception caught in eval loop
    EXCEPT_CATCH_LLVM,      // Exception caught in LLVM
} _PyEventId;

typedef struct {
    long thread_id;
    _PyEventId event_id;
    PY_LONG_LONG time;
} _PyEvent;

typedef unsigned PY_LONG_LONG tsc_t;

/// Log an event and the TSC when it occurred.
#ifdef __cplusplus
extern "C" void _PyLogEvent(_PyEventId event);
#else
extern void _PyLogEvent(_PyEventId event);
#endif

/// Simple macro that wraps up the ifdef WITH_TSC check so that callers don't
/// have to spell it out in their code.
#define PY_LOG_EVENT(event) _PyLogEvent(event)

#else

#define PY_LOG_EVENT(event)

#endif  // WITH_TSC


#endif  // UTIL_EVENTTIMER_H
