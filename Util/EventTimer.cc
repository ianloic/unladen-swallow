#include "Util/EventTimer.h"

#include "Python.h"

#include "Include/pystate.h"

#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MutexGuard.h"

#include <algorithm>
#include <numeric>
#include <time.h>
#include <utility>


static llvm::ManagedStatic< _PyEventTimer > event_timer;

// XXX(rnk): I have only tested this on x86_64.  It needs to be tested on i386
// and PPC.
static inline uint64_t
read_tsc() {
        uint64_t time;

#if defined(__ppc__) /* <- Don't know if this is the correct symbol; this
			   section should work for GCC on any PowerPC
			   platform, irrespective of OS.
			   POWER?  Who knows :-) */

	register unsigned long tbu, tb, tbu2;
  loop:
	asm volatile ("mftbu %0" : "=r" (tbu) );
	asm volatile ("mftb  %0" : "=r" (tb)  );
	asm volatile ("mftbu %0" : "=r" (tbu2));
	if (__builtin_expect(tbu != tbu2, 0)) goto loop;

	/* The slightly peculiar way of writing the next lines is
	   compiled better by GCC than any other way I tried. */
	((long*)(time))[0] = tbu;
	((long*)(time))[1] = tb;

#elif defined(__x86_64__) || defined(__amd64__)

	uint64_t low, high;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	time = (high << 32) | low;

#elif defined(__i386__)

        asm volatile("rdtsc" : "=A" (time));

#endif

        return time;
}

/// _PyEventTimer

void
_PyLogEvent(_PyEventTimer::Event event) {
    event_timer->LogEvent(event);
}

// This must be kept in sync with the Event enum in EventTimer.h
static const char * const event_names[] = {
    "CALL_START_EVAL",
    "CALL_START_LLVM",
    "CALL_ENTER_EVAL",
    "CALL_ENTER_PYOBJ_CALL",
    "CALL_ENTER_C",
    "CALL_ENTER_LLVM",
    "CALL_END_EVAL",
    "LLVM_COMPILE_START",
    "LLVM_COMPILE_END",
    "JIT_START",
    "JIT_END",
};

const char * const
_PyEventTimer::EventToString(Event event) {
    return event_names[(int)event];
}

void
_PyEventTimer::LogEvent(Event event) {
    // XXX(rnk): This probably has more overhead than we'd like.
    PyThreadState *tstate = PyThreadState_Get();
    if (tstate->interp->tscdump) {
        uint64_t time = read_tsc();
        llvm::MutexGuard locked(this->lock_);
        this->data_.push_back(std::make_pair(event, time));
    }
}

_PyEventTimer::~_PyEventTimer() {
    // Print the data to stderr as a tab separated file.
    if (this->data_.size() > 0) {
        fprintf(stderr, "event\ttime\n");
        for (EventVector::iterator it = this->data_.begin();
             it != this->data_.end(); ++it) {
            const char * const str_name = this->EventToString(it->first);
            fprintf(stderr, "%s\t%ld\n", str_name, it->second);
        }
    }
}
