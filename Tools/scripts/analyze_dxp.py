"""
Some helper functions to analyze the output of sys.getdxp() (which is
only available if Python was built with -DDYNAMIC_EXECUTION_PROFILE).
These will tell you which opcodes have been executed most frequently
in the current process, and, if Python was also built with -DDXPAIRS,
will tell you which instruction _pairs_ were executed most frequently,
which helps with defining superinstructions.

If Python was built without -DDYNAMIC_EXECUTION_PROFILE, importing
this module will raise a RuntimeError.

If you're running a script you want to profile, a simple way to get
the common pairs is:

$ PYTHONPATH=$PYTHONPATH:<python_srcdir>/Tools/scripts \
./python -i -O the_script.py --args
...
> from analyze_dxp import *
> s = RenderCommonPairs()
> open('/tmp/some_file', 'w').write(s)
"""

import copy
import opcode
import operator
import sys
import threading
import warnings

if not hasattr(sys, "getdxp"):
    raise RuntimeError("Can't import analyze_dxp: Python built without"
                       " -DDYNAMIC_EXECUTION_PROFILE.")


_profile_lock = threading.RLock()
_cumulative_profile = sys.getdxp()

# If Python was built with -DDXPAIRS, sys.getdxp() returns a list of
# lists of ints.  Otherwise it returns just a list of ints.
def HasPairs(profile):
    """Returns True if the Python that produced the argument profile
    was built with -DDXPAIRS."""

    return len(profile) > 0 and isinstance(profile[0], list)


def ResetProfile():
    """Forgets any execution profile that has been gathered so far."""
    with _profile_lock:
        sys.getdxp()  # Resets the internal profile
        _cumulative_profile = sys.getdxp()  # 0s out our copy.


def MergeProfile():
    """Reads sys.getdxp() and merges it into this module's cached copy.

    We need this because sys.getdxp() 0s itself every time it's called."""

    with _profile_lock:
        new_profile = sys.getdxp()
        if HasPairs(new_profile):
            for first_inst in range(len(_cumulative_profile)):
                for second_inst in range(len(_cumulative_profile[first_inst])):
                    _cumulative_profile[first_inst][second_inst] += (
                        new_profile[first_inst][second_inst])
        else:
            for inst in range(len(_cumulative_profile)):
                _cumulative_profile[inst] += new_profile[inst]


def SnapshotProfile():
    """Returns an the cumulative execution profile until this call."""
    with _profile_lock:
        MergeProfile()
        return copy.deepcopy(_cumulative_profile)


def _Opname(code):
    """Returns opcode.opname[code] or <unknown ###> for unknown opcodes."""
    if code < len(opcode.opname):
        return opcode.opname[code]
    else:
        return "<unknown %s>" % code


def CommonInstructions(profile):
    """Returns the most common opcodes in order of descending frequency.

    The result is a list of tuples of the form
      (opcode, opname, # of occurrences)

    """
    if HasPairs(profile) and profile:
        inst_list = profile[-1]
    else:
        inst_list = profile
    return sorted(((op, _Opname(op), count)
                   for op, count in enumerate(inst_list)
                   if count > 0),
                  key=operator.itemgetter(2),
                  reverse=True)


def CommonPairs(profile):
    """Returns the most common opcode pairs in order of descending frequency.

    The result is a list of tuples of the form
      ((1st opcode, 2nd opcode),
       (1st opname, 2nd opname),
       # of occurrences of the pair)

    """
    if not HasPairs(profile):
        return []
    result = [((op1, op2), (_Opname(op1), _Opname(op2)), count)
              # Drop the row of single-op profiles with [:-1]
              for op1, op1profile in enumerate(profile[:-1])
              for op2, count in enumerate(op1profile)
              if count > 0]
    # Add in superinstructions, which are made up of at least pairs of
    # ordinary instructions.  Include all superinstructions, even if
    # their count is 0, to identify superinstructions we should
    # delete.
    result.extend(((op,), (_Opname(op),), count)
                  for op, count in enumerate(profile[-1])
                  if op in opcode.super2prim)
    result.sort(key=operator.itemgetter(2), reverse=True)
    return result


def RenderCommonPairs(profile=None):
    """Renders the most common opcode pairs to a string in order of
    descending frequency.

    The result is a series of lines of the form:
      # of occurrences: ('1st opname', '2nd opname')
    or, if the line represents a lone superinstruction:
      # of occurrences: ('super-name',)

    """
    if profile is None:
        profile = SnapshotProfile()
    def seq():
        for _, ops, count in CommonPairs(profile):
            yield "%s: %s\n" % (count, ops)
    return ''.join(seq())


def CommonSequences(profile):
    """Decompiles the superinstructions in the common pairs to
    identify longer common sequences."""
    if not HasPairs(profile):
        return []
    result = []
    for ops, _, count in CommonPairs(profile):
        supers = list(ops)
        insts = []
        while supers:
            if supers[0] in opcode.super2prim:
                supers[0:1] = opcode.super2prim[supers[0]]
            else:
                insts.append(supers[0])
                supers = supers[1:]
        result.append((insts, map(_Opname, insts), count))
    return result
