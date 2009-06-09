#!/usr/bin/env python

"""Compute timing statistics based on the output of Python with TSC enabled.

To use this script, pass --with-tsc to ./configure and call sys.settscdump(True)
in the script that you want to use to record timings.  When the script and
interpreter exit, the timings will be printed to stderr as a CSV file separated
by tabs.  You should redirect that to this script, either through a file or
pipe:

    ./python myscript.py 2>&1 >&3 3>&- | Misc/tsc_stats.py
    ./python myscript.py 2> stats ; Misc/tsc_stats.py stats

This script outputs statistics about function call overhead, bytecode to LLVM
IR compilation overhead, and JIT compilation.  In order to get more meaningful
results for function call overhead, LLVM IR and JIT compilation time is not
counted against the function call overhead.  Otherwise the mean and max
function call overhead times would be way off.

"""

from __future__ import division

import itertools
import math
import sys


def median(xs):
    """Return the median of some numeric values."""
    xs = sorted(xs)
    mid = len(xs) // 2
    if len(xs) % 2 == 0:
        return (xs[mid] + xs[mid - 1]) / 2
    else:
        return xs[mid]


def mean(xs):
    """Return the mean of some numeric values."""
    return float(sum(xs)) / len(xs)


def stddev(xs):
    """Return the standard deviation of some numeric values."""
    if len(xs) == 1:
        return 0  # Avoid doing a ZeroDivisionError.
    mn = mean(xs)
    deviations = (x - mn for x in xs)
    square_sum = sum(d * d for d in deviations)
    variance = square_sum / float(len(xs) - 1)
    return math.sqrt(variance)


class TimeAnalyzer(object):

    def __init__(self, input):
        self.input = input
        self.missed_events = []
        self.delta_dict = {}
        self.compiles = []
        self.jits = []

    def analyze(self):
        compile_started = False
        compile_start = 0
        jit_started = False
        jit_start = 0
        call_start_event = None
        call_start = 0
        raise_event = None
        raise_start = 0
        for line in self.input:
            # TODO(rnk): Keep things thread local.
            (thread, event, time) = line.strip().split("\t")
            time = int(time)
            if event.startswith("CALL_START_"):
                if call_start_event:
                    self.missed_events.append((call_start_event, call_start))
                call_start_event = event
                call_start = time
            elif event.startswith("CALL_ENTER_"):
                if not call_start_event:
                    self.missed_events.append((event, time))
                    continue
                delta = time - call_start
                key = (call_start_event, event)
                self.delta_dict.setdefault(key, []).append(delta)
                call_start_event = None
                call_start = 0
            elif event.startswith("EXCEPT_RAISE_"):
                if raise_event:
                    self.missed_events.append((call_start_event, call_start))
                raise_event = event
                raise_start = time
            elif event.startswith("EXCEPT_CATCH_"):
                if not raise_event:
                    self.missed_events.append((event, time))
                    continue
                delta = time - raise_start
                key = (raise_event, event)
                self.delta_dict.setdefault(key, []).append(delta)
                raise_event = None
                raise_start = 0
            elif event == "LLVM_COMPILE_START":
                compile_started = True
                compile_start = time
            elif event == "LLVM_COMPILE_END" and compile_started:
                compile_time = time - compile_start
                self.compiles.append(compile_time)
                # Fudge the call_start_event time to erase the compilation
                # overhead.
                if call_start_event:
                    call_start += compile_time
                if raise_event:
                    raise_start += compile_time
            elif event == "JIT_START":
                jit_started = True
                jit_start = time
            elif event == "JIT_END" and jit_started:
                jit_time = time - jit_start
                self.jits.append(jit_time)
                # Fudge call_start and raise_time to erase the JIT overhead.
                if call_start_event:
                    call_start += jit_time
                if raise_event:
                    raise_start += jit_time
            else:
                self.missed_events.append((event, time))

    def print_deltas(self, deltas):
        print "occurrences:", len(deltas)
        if deltas:
                print "median delta:", median(deltas)
                print "mean delta:", mean(deltas)
                print "min delta:", min(deltas)
                print "max delta:", max(deltas)
                print "stddev:", stddev(deltas)

    def print_analysis(self):
        print ("All times are in time stamp counter units, which are related "
               "to your CPU frequency.")
        print
        total_deltas = []
        for ((start, end), deltas) in sorted(self.delta_dict.iteritems()):
            total_deltas.extend(deltas)
            print "for transitions from %s to %s:" % (start, end)
            self.print_deltas(deltas)
            print
        print "LLVM function builder:"
        self.print_deltas(self.compiles)
        print
        print "JIT:"
        self.print_deltas(self.jits)
        print
        grouped = {}
        for (event, time) in self.missed_events:
            grouped[event] = grouped.get(event, 0) + 1
        print "missed events:",
        print ", ".join("%s %d" % (event, count)
                        for (event, count) in grouped.iteritems())


def main(argv):
    if argv:
        assert len(argv) == 2, "tsc_stats.py expects one file as input."
        input = open(argv[1])
    else:
        input = sys.stdin
    analyzer = TimeAnalyzer(input)
    analyzer.analyze()
    analyzer.print_analysis()


if __name__ == "__main__":
    main(sys.argv)
