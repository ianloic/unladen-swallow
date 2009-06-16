"""Tests harness for distutils.versionpredicate.

"""

import distutils.versionpredicate
import doctest
import sys
import unittest

def test_suite():
    # Docstrings are omitted at this optimization level, so skip all doctests.
    if sys.flags.optimize >= 2:
        return unittest.TestSuite()
    else:
        return doctest.DocTestSuite(distutils.versionpredicate)
