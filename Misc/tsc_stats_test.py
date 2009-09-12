#!/usr/bin/env python

from __future__ import with_statement

import StringIO
import unittest
import warnings

import tsc_stats


class TscStatsTest(unittest.TestCase):

    def testMedianOdd(self):
        # Note: median requires that its input be sorted.
        xs = [1, 2, 3, 4, 5]
        self.assertEqual(tsc_stats.median(xs), 3)

    def testMedianEven(self):
        # Note: median requires that its input be sorted.
        xs = [1, 2, 3, 4, 5, 6]
        self.assertAlmostEqual(tsc_stats.median(xs), 3.5, 0.01)

    def testMean(self):
        xs = [1, 2, 3]
        self.assertAlmostEqual(tsc_stats.mean(xs), 2.0, 0.01)

    def testStdDevNoVariance(self):
        xs = [2, 2, 2]
        self.assertAlmostEqual(tsc_stats.stddev(xs), 0, 0.01)

    def testStdDev(self):
        xs = [1, 2, 3]
        expected = (1 + 0 + 1) / 2
        self.assertAlmostEqual(tsc_stats.stddev(xs), expected, 0.01)

    def testAnalyzerSimpleCalls(self):
        input = StringIO.StringIO("""\
0	CALL_START_EVAL	42602597860025
0	CALL_ENTER_C	42602597871713
0	CALL_START_EVAL	42602597880984
0	CALL_ENTER_PYOBJ_CALL	42602597883307
0	CALL_START_EVAL	42602597894126
0	CALL_ENTER_EVAL	42602597898375
0	CALL_START_EVAL	42602597902292
0	CALL_ENTER_C	42602597902916
0	LOAD_GLOBAL_ENTER_EVAL	42602633110000
0	LOAD_GLOBAL_EXIT_EVAL	42602633111000
""")
        analyzer = tsc_stats.TimeAnalyzer(input)
        analyzer.analyze()
        call_delta_dict = {
            ('CALL_START_EVAL', 'CALL_ENTER_C'):
                [42602597871713 - 42602597860025,
                 42602597902916 - 42602597902292],
            ('CALL_START_EVAL', 'CALL_ENTER_PYOBJ_CALL'):
                [42602597883307 - 42602597880984],
            ('CALL_START_EVAL', 'CALL_ENTER_EVAL'):
                [42602597898375 - 42602597894126],
        }
        global_delta_dict = {
            ('LOAD_GLOBAL_ENTER_EVAL', 'LOAD_GLOBAL_EXIT_EVAL'):
                [1000],
        }
        self.assertEqual(analyzer.call_stats.delta_dict, call_delta_dict)
        self.assertEqual(analyzer.global_stats.delta_dict, global_delta_dict)

    def testAnalyzerJittedCall(self):
        input = StringIO.StringIO("""\
0	CALL_START_EVAL	0
0	EVAL_COMPILE_START	10
0	LLVM_COMPILE_START	15
0	LLVM_COMPILE_END	35
0	JIT_START	40
0	JIT_END	95
0	EVAL_COMPILE_END	100
0	CALL_ENTER_LLVM	200
0	CALL_START_LLVM	300
0	CALL_ENTER_EVAL	400
""")
        analyzer = tsc_stats.TimeAnalyzer(input)
        analyzer.analyze()
        eval_compile_time = 90
        call_delta_dict = {
            ('CALL_START_EVAL', 'CALL_ENTER_LLVM'):
                [200 - eval_compile_time],
            ('CALL_START_LLVM', 'CALL_ENTER_EVAL'):
                [100],
        }
        self.assertEqual(analyzer.call_stats.delta_dict, call_delta_dict)
        self.assertEqual(analyzer.llvm_stats.aggregate_deltas, [20])
        self.assertEqual(analyzer.native_stats.aggregate_deltas, [55])
        self.assertEqual(analyzer.eval_compile_stats.aggregate_deltas,
                         [eval_compile_time])

    def testAnalyzerFlushFudge(self):
        input = StringIO.StringIO("""\
0	CALL_START_EVAL	0
0	EVAL_COMPILE_START	10
0	LLVM_COMPILE_START	15
0	LLVM_COMPILE_END	35
0	JIT_START	40
0	FLUSH_START	41
0	FLUSH_END	20041
0	JIT_END	20095
0	EVAL_COMPILE_END	20100
0	CALL_ENTER_LLVM	20200
0	CALL_START_LLVM	20300
0	CALL_ENTER_EVAL	20400
""")
        analyzer = tsc_stats.TimeAnalyzer(input)
        analyzer.analyze()
        eval_compile_time = 90
        flush_time = 20000
        call_delta_dict = {
            ('CALL_START_EVAL', 'CALL_ENTER_LLVM'): [200 - eval_compile_time],
            ('CALL_START_LLVM', 'CALL_ENTER_EVAL'): [100],
        }
        self.assertEqual(analyzer.call_stats.delta_dict, call_delta_dict)
        self.assertEqual(analyzer.flush_stats.aggregate_deltas, [20000])
        self.assertEqual(analyzer.llvm_stats.aggregate_deltas, [20])
        self.assertEqual(analyzer.native_stats.aggregate_deltas, [55])
        self.assertEqual(analyzer.eval_compile_stats.aggregate_deltas,
                         [eval_compile_time])


if __name__ == '__main__':
    # Silence a warning from the unittest module relating to floating point
    # equality.  We're doing the right thing, but it doesn't in Python 2.5.
    warnings.filterwarnings('ignore', '.*integer.*float.*')
    unittest.main()
