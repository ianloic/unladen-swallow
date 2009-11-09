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
    self.groups = self.__parsed.pattern.groups-1
    self.groupindex = self.__parsed.pattern.groupdict
    self.pattern = pattern

  def __unicode(self, s):
    '''return @s as a unicode string'''
    try:
      return unicode(s)
    except:
      return unicode(s, 'latin1')

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
      elif op == 'assert' or op == 'assert_not':
        direction, pat = av
        new_pattern.append((op, (direction, self.__flatten_subpatterns(pat))))
      elif op == 'in':
        # until duplicate case handling is fixed, remove duplicates from 'in'
        arg = []
        if len(av) and av[0] == ('negate', None):
          arg.append(av.pop(0))
        opts = set()
        for l in av:
          opts.add(l)
        arg.extend(opts)
        new_pattern.append((op, arg))
      else:
        new_pattern.append((op, av))
    return new_pattern

  def match(self, string, pos=0, endpos=None):
    if endpos == None: endpos = len(string)
    groups = self.__re.match(self.__unicode(string), pos, endpos)
    if groups:
      return MatchObject(self, string, pos, endpos, groups, self.__parsed)
    else:
      return None

  def search(self, string, pos=0, endpos=None):
    if endpos == None: endpos = len(string)
    groups = self.__re.find(self.__unicode(string), pos, endpos)
    if groups:
      return MatchObject(self, string, pos, endpos, groups, self.__parsed)
    else:
      return None

  def split(self, string, maxsplit=0):
    # find matches for the separator
    matches = [m for m in self.__finditer(string, count=maxsplit, 
      ignore_empty=True)]
    # find the spans of those matches
    spans = [m.span() for m in matches]
    # find the spans between those matches
    spans = reduce(lambda result,span: 
        result[:-1] + [(result[-1], span[0]), span[1]], spans, [0])
    spans[-1] = (spans[-1],len(string))

    split = [string[a:b] for a,b in spans]
    if self.groups:
      # there are capturing groups, insert them into the result
      result = []
      for i in range(len(matches)):
        result.append(split[i])
        result.extend(matches[i].groups())
      result.append(split[-1])
      return result
    else:
      return split

  def __finditer(self, string, pos=0, endpos=None, count=0, ignore_empty=False):
    if endpos == None: endpos = len(string)
    _pos = pos
    num = 0
    while True:
      groups = self.__re.find(self.__unicode(string), _pos, endpos)
      if groups:
        # next time search after this result
        if groups[0] == groups[1]:
          _pos = groups[1] + 1
          # if we should ignore empty matches, skip it
          if ignore_empty:
            continue
        else:
          _pos = groups[1]
        # yeild this result
        yield MatchObject(self, string, pos, endpos, groups, self.__parsed)

        # increment the counter
        num = num + 1
        # check the counter
        if count and num >= count:
          raise StopIteration
      else:
        # no match, stop looking
        raise StopIteration

  def findall(self, string, pos=0, endpos=None):
    all = []
    for m in self.finditer(string, pos, endpos):
      if self.groups == 0:
        all.append(m.group(0))
      elif self.groups == 1:
        all.append(m.group(1))
      else:
        all.append(m.groups())
    return all

  def finditer(self, string, pos=0, endpos=None):
    return self.__finditer(string, pos, endpos)

  def sub(self, repl, string, count=0):
    return self.subn(repl, string, count)[0]

  def subn(self, repl, string, count=0):
    result = ''
    pos = 0
    matched = False

    # find the matches
    matches = list(self.__finditer(string, count=count))

    # substitute matches for replacements
    for m in matches:
      if matched and pos == m.start() and pos == m.end():
        # ignore empty matches immediately following another match
        continue
      matched = True
      result = result + string[pos:m.start()]
      if callable(repl):
        result = result + repl(m)
      else:
        result = result + m.expand(repl)
      pos = m.end()
    result = result + string[pos:len(string)]

    return (result, len(matches))


class MatchObject(object):
  def __init__(self, regex, string, pos, endpos, groups, parsed):
    self.__regex = regex
    self.regs = []
    self.__parsed = parsed
    if len(groups) > 3:
      self.lastindex = groups.pop(-1)
    else:
      self.lastindex = None
    while len(groups) >= 2: self.regs.append((groups.pop(0), groups.pop(0)))
    self.regs = tuple(self.regs)
    self.pos = pos
    self.endpos = endpos
    self.re = regex.pattern
    self.string = string
    self.lastgroup = None
    for name, num in regex.groupindex.items():
      if num == self.lastindex:
        self.lastgroup = name
        break

  def __groupnum(self, group):
    '''take a group name (or number) and return the group number'''
    if isinstance(group, numbers.Integral):
      return group
    else:
      return self.__regex.groupindex[group]

  def span(self, group=0):
    return self.regs[self.__groupnum(group)]

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
    for span in self.regs[1:]:
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
    return expand_template(parse_template(template, self.__regex), self)

def compile(pattern, flags=0):
  if isinstance(pattern, RegexObject):
    if flags != pattern.flags:
      raise ValueError()
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

def split(pattern, string, maxsplit=0):
  return compile(pattern).split(string, maxsplit)

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
