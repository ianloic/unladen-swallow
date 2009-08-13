'''A replacement for Python's RE module, using native compilation via LLVM'''

# import flags from re
from re import I, IGNORECASE, L, LOCALE, M, MULTILINE, S, DOTALL, U, UNICODE, X, VERBOSE
# import the error from re
from re import error

import numbers

class RegexObject(object):
  def __init__(self, pattern, flags):
    # call sre_parse to parse re string into an object
    from sre_parse import parse
    parsed = parse(pattern, flags)
    flags = parsed.pattern.flags
    # flatten the subpatterns in the sequence
    processed = self.__flatten_subpatterns(parsed)
    # compile to native code
    from _llvmre import RegEx
    self.__re = RegEx(processed, flags, parsed.pattern.groups-1)
    # expected properties
    self.flags = flags
    self.groups = parsed.pattern.groups
    self.groupindex = parsed.pattern.groupdict
    self.pattern = pattern

  def __flatten_subpatterns(self, pattern):
    new_pattern = []
    for op, av in pattern:
      if op == 'subpattern':
        id, subpattern = av
        new_pattern.append(('subpattern_begin', id))
        new_pattern.extend(self.__flatten_subpatterns(subpattern))
        new_pattern.append(('subpattern_end', id))
      else:
        new_pattern.append((op, av))
    return new_pattern

  def match(self, string, pos=None, endpos=None):
    if pos: _pos = pos
    else: _pos = 0
    if endpos: _endpos = endpos
    else: _endpos = len(string)
    groups = self.__re.match(unicode(string), _pos, _endpos)
    if groups:
      return MatchObject(self, string, pos, endpos, groups)
    else:
      return None

  def search(self, string, pos=0, endpos=-1):
    pass

  def split(self, string, maxsplit=0):
    pass

  def findall(self, string, pos=0, endpos=-1):
    pass

  def finditer(self, string, pos=0, endpos=-1):
    pass

  def sub(self, repl, string, count=0):
    pass

  def subn(self, repl, string, count=0):
    pass


class MatchObject(object):
  def __init__(self, regex, string, pos, endpos, groups):
    self.__regex = regex
    self.__groups = []
    while len(groups): self.__groups.append((groups.pop(0), groups.pop(0)))
    # FIXME: implement lastindex, lastgroup
    self.pos = pos
    self.endpos = endpos
    self.re = regex.pattern
    self.string = string

  def __groupnum(self, group):
    '''take a group name (or number) and return the group number'''
    if isinstance(group, numbers.Integral):
      return group
    else:
      return self.__regex.groupindex[group]

  def span(self, group=0):
    return self.__groups[self.__groupnum(group)]

  def start(self, group=0):
    return self.span(group)[0]

  def end(self, group=0):
    return self.span(group)[1]
 
  def group(self, *groups):
    result = []
    for group in groups:
      span = self.span(group)
      if span[0] == -1 or span[1] == -1:
        result.append(None)
      else:
        result.append(self.string[span[0]:span[1]])
    if len(result) == 1: return result[0]
    else: return tuple(result)

  def groups(self, default=None):
    result = []
    for span in self.__groups[1:]:
      if span[0] == -1 or span[1] == -1:
        result.append(default)
      else:
        result.append(self.string[span[0]:span[1]])
    return tuple(result)

  def groupdict(self, default=None):
    result = {}
    for name, number in self.__regex.groupindex.items():
      result[name] = self.group(number)
      if result[name] == None: result[name] = default
    return result

  def expand(self, template):
    # see re._expand
    raise NotImplementedError('MatchObject.expand')


def compile(pattern, flags=0):
  return RegexObject(pattern, flags)
