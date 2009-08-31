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
    self.__parsed = parse(pattern, flags)
    flags = self.__parsed.pattern.flags
    # flatten the subpatterns in the sequence
    processed = self.__flatten_subpatterns(self.__parsed)
    # compile to native code
    from _llvmre import RegEx
    self.__re = RegEx(processed, flags, self.__parsed.pattern.groups-1)
    # expected properties
    self.flags = flags
    self.groups = self.__parsed.pattern.groups
    self.groupindex = self.__parsed.pattern.groupdict
    self.pattern = pattern

  def __flatten_subpatterns(self, pattern):
    new_pattern = []
    for op, av in pattern:
      if op == 'subpattern':
        id, subpattern = av
        if id == None:
          # we can ignore non-grouping subpatterns
          new_pattern.extend(self.__flatten_subpatterns(subpattern))
        else:
          # use new subpattern_begin & subpattern_end ops
          new_pattern.append(('subpattern_begin', id))
          new_pattern.extend(self.__flatten_subpatterns(subpattern))
          new_pattern.append(('subpattern_end', id))
      elif op == 'max_repeat' or op == 'min_repeat':
        min, max, pattern = av
        new_pattern.append((op, (min, max, self.__flatten_subpatterns(pattern))))

      elif op == 'branch':
        n, branches = av
        new_pattern.append((op, (n, map(self.__flatten_subpatterns, branches))))
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
      return MatchObject(self, string, pos, endpos, groups, self.__parsed)
    else:
      return None

  def search(self, string, pos=0, endpos=None):
    if pos: _pos = pos
    else: _pos = 0
    if endpos: _endpos = endpos
    else: _endpos = len(string)
    groups = self.__re.find(unicode(string), _pos, _endpos)
    if groups:
      return MatchObject(self, string, pos, endpos, groups, self.__parsed)
    else:
      return None

  def split(self, string, maxsplit=0):
    raise NotImplementedError('RegexObject.split')

  def findall(self, string, pos=0, endpos=None):
    if pos: _pos = pos
    else: _pos = 0
    if endpos: _endpos = endpos
    else: _endpos = len(string)
    results = []
    while True:
      groups = self.__re.find(unicode(string), _pos, _endpos)
      if groups:
        # add this match to the results
        results.append(string[groups[0]:groups[1]])
        # next time search after this result
        if _pos == groups[1]:
          _pos = groups[1] + 1
        else:
          _pos = groups[1]
      else:
        # no match, stop looking
        break
    return results

  def finditer(self, string, pos=0, endpos=None):
    return iter(self.findall(string, pos, endpos))

  def sub(self, repl, string, count=0):
    return self.subn(repl, string, count)[0]

  def subn(self, repl, string, count=0):
    pos = 0
    endpos = len(string)
    result = ''
    num = 0;
    while True:
      groups = self.__re.find(unicode(string), pos, endpos)
      if groups:
        # make a match object
        mo = MatchObject(self, string, pos, endpos, groups, self.__parsed)
        if callable(repl):
          # repl is a function, call it with the match object
          replacement = repl(mo)
        else:
          # repl is a string, use template expansion
          replacement = mo.expand(repl)

        # increment the counter
        num = num + 1
        # check the counter
        if count and num > count:
          num = num-1
          break

        # add the chars leading up to the match and the replacement
        result = result + string[pos:mo.start()] + replacement

        # next time search after this result
        if pos == mo.end():
          pos = mo.end() + 1
        else:
          pos = mo.end()

      else:
        # no match, stop looking
        break
    # add everything after the last match
    result = result + string[pos:]
    return (result, num)


class MatchObject(object):
  def __init__(self, regex, string, pos, endpos, groups, parsed):
    self.__regex = regex
    self.__groups = []
    self.__parsed = parsed
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
    # reuse SRE's template code
    from sre_parse import parse_template, expand_template
    return expand_template(parse_template(template, self.__parsed), self)

def compile(pattern, flags=0):
  if isinstance(pattern, RegexObject):
    assert flags == pattern.flags # right?
    return pattern
  else:
    return RegexObject(pattern, flags)

def match(pattern, string, flags=0):
  return compile(pattern, flags).match(string)

def search(pattern, string, flags=0):
  return compile(pattern, flags).search(string)

def sub(pattern, repl, string, count=0):
  return compile(pattern).sub(repl, string, count)

def subn(pattern, repl, string, count=0):
  return compile(pattern).subn(repl, string, count)

def findall(pattern, string, flags=0):
  return compile(pattern, flags).findall(string)

def finditer(pattern, string, flags=0):
  return compile(pattern, flags).finditer(string)

# taken from SRE's re.py
_alphanum = {}
for c in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890':
    _alphanum[c] = 1
del c
def escape(pattern):
    "Escape all non-alphanumeric characters in pattern."
    s = list(pattern)
    alphanum = _alphanum
    for i in range(len(pattern)):
        c = pattern[i]
        if c not in alphanum:
            if c == "\000":
                s[i] = "\\000"
            else:
                s[i] = "\\" + c
    return pattern[:0].join(s)
