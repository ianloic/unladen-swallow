#!/usr/bin/env python

"""Compute timing statistics based on the output of Python with TSC enabled.

To use this script, pass --with-tsc to ./configure and call sys.settscdump(True)
in the script that you want to use to record timings.  When the script and
interpreter exit, the timings will be printed to stderr as a CSV file separated
by tabs.  You should redirect that to this script, either through a file or
pipe:

    ./python myscript.py 2>&1 >&3 3>&- | Misc/tsc_stats.py
    ./python myscript.py 2> stats ; Misc/tsc_stats.py stats

This script outputs statistics about function call overhead, exception handling
overhead, bytecode to LLVM IR compilation overhead, native code generation
overhead, and various other things.

In order to not use too much memory, the event timer in Python periodically
flushes the event buffer.  This messes up a lot of the timings, so it also
prints out events that let us figure out how long the flush took.  We take that
time and go back and adjust the times to erase that overhead.  Otherwise our
max, mean, and stddev statistics would be meaningless.

In order to get more meaningful results for function call overhead, any time
spent doing compilation in the eval loop is not counted against the function
call overhead.  Otherwise the mean and max function call overhead times would be
way off.

"""

from __future__ import division

import itertools
import math
import sys


def median(xs):
    """Return the median of some numeric values.

    Assumes that the input list is sorted.
    """
    mid = len(xs) // 2
    if len(xs) % 2 == 0:
        return (xs[mid] + xs[mid - 1]) / 2
    else:
        return xs[mid]


def decile_decorator(func):
    def decile_func(xs):
        """Return the func applied to the inter-decile 80% of the input.

        Assumes that the input list is sorted.
        """
        decile = len(xs) // 10
        if decile == 0:
            # Special case this because -0 == 0, so the list slice below doesn't
            # work.
            return func(xs)
        return func(xs[decile:-decile])
    return decile_func


def mean(xs):
    """Return the mean of some numeric values."""
    return float(sum(xs)) / len(xs)


decile_mean = decile_decorator(mean)


def stddev(xs):
    """Return the standard deviation of some numeric values."""
    if len(xs) == 1:
        return 0  # Avoid doing a ZeroDivisionError.
    mn = mean(xs)
    deviations = (x - mn for x in xs)
    square_sum = sum(d * d for d in deviations)
    variance = square_sum / float(len(xs) - 1)
    return math.sqrt(variance)


decile_stddev = decile_decorator(stddev)


class DeltaStatistic(object):

    """This class matches and stores delta timings for a class of events."""

    def __init__(self, start_prefix, end_prefix, missed_events):
        """Constructor.

        Args:
            start_prefix: the prefix matching the start event
            end_prefix: the prefix matching the end event
            missed_events: the list that we should missed events to
        """
        self.start_prefix = start_prefix
        self.end_prefix = end_prefix
        self.missed_events = missed_events
        self.delta_dict = {}
        self.aggregate_deltas = []
        self.started = False
        self.start_event = None
        self.start_time = 0

    def try_match(self, thread, event, time):
        """If this event matches the statistic, record it and return True.

        Args:
            thread: the thread id for this event
            event: the name of the event
            time: the timestamp counter when the event occurred
        """
        # TODO(rnk): Keep things thread local.
        if event.startswith(self.start_prefix):
            # If we already started, we missed an end event.  Record the old
            # start event that didn't get an end, and use this start event
            # instead.
            if self.started:
                self.missed_events.append((self.start_event, self.start_time))

            self.started = True
            self.start_event = event
            self.start_time = time
            return True

        elif event.startswith(self.end_prefix):
            # If we have not started, we missed a start event.  Record this
            # end event, and ignore it.
            if not self.started:
                self.missed_events.append((event, time))
                return True

            delta = time - self.start_time
            key = (self.start_event, event)
            self.delta_dict.setdefault(key, []).append(delta)
            self.aggregate_deltas.append(delta)

            # Reset us looking for a start event.
            self.started = False
            self.start_event = None
            self.start_time = 0
            return True

        return False


class TimeAnalyzer(object):

    def __init__(self, input):
        self.input = input
        self.missed_events = []
        m_e = self.missed_events  # Shorthand
        self.call_stats = DeltaStatistic("CALL_START_", "CALL_ENTER_", m_e)
        self.exception_stats = DeltaStatistic("EXCEPT_RAISE_", "EXCEPT_CATCH_",
                                              m_e)
        self.global_stats = DeltaStatistic("LOAD_GLOBAL_ENTER_",
                                           "LOAD_GLOBAL_EXIT_", m_e)
        self.native_stats = DeltaStatistic("JIT_START", "JIT_END", m_e)
        self.llvm_stats = DeltaStatistic("LLVM_COMPILE_START",
                                         "LLVM_COMPILE_END", m_e)
        self.eval_compile_stats = DeltaStatistic("EVAL_COMPILE_START",
                                                 "EVAL_COMPILE_END", m_e)
        self.flush_stats = DeltaStatistic("FLUSH_START", "FLUSH_END", m_e)
        self.statistics = [
                self.call_stats,
                self.exception_stats,
                self.global_stats,
                self.native_stats,
                self.llvm_stats,
                self.eval_compile_stats,
                self.flush_stats,
                ]

    def flush_fudge(self, flush_delta):
        """Fudge the start time of open stats to eliminate flush overhead."""
        for stat in self.statistics:
            if stat.started:
                stat.start_time += flush_delta

    def analyze(self):
        """Process the input into categorized timings."""
        for line in self.input:
            (thread, event, time) = line.strip().split("\t")
            time = int(time)
            for stat in self.statistics:
                if stat.try_match(thread, event, time):
                    if not stat.started and stat.aggregate_deltas:
                        delta = stat.aggregate_deltas[-1]
                        if (stat is self.eval_compile_stats and
                            self.call_stats.started):
                            # Fudge the call_stats start time to erase
                            # compilation overhead in the eval loop.
                            self.call_stats.start_time += delta
                        if stat is self.flush_stats:
                            # Fudge every stat that has an open timing to
                            # eliminate the flush overhead.
                            self.flush_fudge(delta)
                    break
            else:
                # If no statistic matched the event, log it as missed.
                self.missed_events.append((event, time))

    def print_deltas(self, deltas):
        """Print out statistics about this sequence of timings."""
        print "occurrences:", len(deltas)
        if deltas:
            deltas = sorted(deltas)
            print "median:", median(deltas)
            print "inter-decile mean:", decile_mean(deltas)
            print "min delta:", deltas[0]
            print "max delta:", deltas[-1]
            print "inter-decile stddev:", decile_stddev(deltas)

    def print_stat_deltas(self, stat):
        """Print out the deltas for this statistic broken down by pairing."""
        for ((start, end), deltas) in stat.delta_dict.iteritems():
            print "for transitions from %s to %s:" % (start, end)
            self.print_deltas(deltas)
            print

    def print_stat_aggregate(self, stat):
        """Print out the aggregate deltas for this statistic."""
        print ("In aggregate, for transitions from %s* to %s*:" %
               (stat.start_prefix, stat.end_prefix))
        self.print_deltas(stat.aggregate_deltas)

    def print_analysis(self):
        print ("All times are in time stamp counter units, which are related "
               "to your CPU frequency.")
        print
        print "Call overhead:"
        print "----------------------------------------"
        self.print_stat_deltas(self.call_stats)
        self.print_stat_aggregate(self.call_stats)
        print
        print "Exception handling overhead:"
        print "----------------------------------------"
        self.print_stat_deltas(self.exception_stats)
        self.print_stat_aggregate(self.exception_stats)
        print
        print "LOAD_GLOBAL overhead:"
        print "----------------------------------------"
        self.print_stat_deltas(self.global_stats)
        self.print_stat_aggregate(self.global_stats)
        print
        print "LLVM IR compilation:"
        print "----------------------------------------"
        self.print_stat_aggregate(self.llvm_stats)
        print
        print "Native code generation:"
        print "----------------------------------------"
        self.print_stat_aggregate(self.native_stats)
        print
        print "Total time doing compilation in the eval loop:"
        print "----------------------------------------"
        self.print_stat_aggregate(self.eval_compile_stats)
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
