#!/usr/bin/env python

import StringIO
import unittest

import tsc_stats


class TscStatsTest(unittest.TestCase):

    def testMedianOdd(self):
        xs = [2, 1, 5, 4, 3]
        self.assertEqual(tsc_stats.median(xs), 3)

    def testMedianEven(self):
        xs = [5, 6, 4, 3, 1, 2]
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
        delta_dict = {
            ('CALL_START_EVAL', 'CALL_ENTER_C'):
                [42602597871713 - 42602597860025,
                 42602597902916 - 42602597902292],
            ('CALL_START_EVAL', 'CALL_ENTER_PYOBJ_CALL'):
                [42602597883307 - 42602597880984],
            ('CALL_START_EVAL', 'CALL_ENTER_EVAL'):
                [42602597898375 - 42602597894126],
            ('LOAD_GLOBAL_ENTER_EVAL', 'LOAD_GLOBAL_EXIT_EVAL'):
                [1000],
        }
        self.assertEqual(analyzer.delta_dict, delta_dict)
        self.assertEqual(analyzer.jits, [])
        self.assertEqual(analyzer.compiles, [])

    def testAnalyzerJittedCall(self):
        input = StringIO.StringIO("""\
0	CALL_START_EVAL	42602598394717
0	LLVM_COMPILE_START	42602598396584
0	LLVM_COMPILE_END	42602601323449
0	JIT_START	42602601326314
0	JIT_END	42602631083429
0	CALL_ENTER_LLVM	42602631088012
0	CALL_START_LLVM	42602633083954
0	CALL_ENTER_EVAL	42602633101769
""")
        analyzer = tsc_stats.TimeAnalyzer(input)
        analyzer.analyze()
        compile_time = 42602601323449 - 42602598396584
        jit_time = 42602631083429 - 42602601326314
        delta_dict = {
            ('CALL_START_EVAL', 'CALL_ENTER_LLVM'):
                [42602631088012 - (42602598394717 + compile_time + jit_time)],
            ('CALL_START_LLVM', 'CALL_ENTER_EVAL'):
                [42602633101769 - 42602633083954],
        }
        self.assertEqual(analyzer.delta_dict, delta_dict)
        self.assertEqual(analyzer.jits, [jit_time])
        self.assertEqual(analyzer.compiles, [compile_time])


if __name__ == '__main__':
    unittest.main()
