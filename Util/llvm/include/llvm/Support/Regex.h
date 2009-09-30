//===-- Regex.h - Regular Expression matcher implementation -*- C++ -*-----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a POSIX regular expression matcher.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

struct llvm_regex;
namespace llvm {
  class Regex {
  public:
    enum {
      /// Compile with support for subgroup matches, this is just to make
      /// constructs like Regex("...", 0) more readable as Regex("...", Sub).
      Sub=0,
      /// Compile for matching that ignores upper/lower case distinctions.
      IgnoreCase=1,
      /// Compile for matching that need only report success or failure,
      /// not what was matched.
      NoSub=2,
      /// Compile for newline-sensitive matching. With this flag '[^' bracket
      /// expressions and '.' never match newline. A ^ anchor matches the 
      /// null string after any newline in the string in addition to its normal 
      /// function, and the $ anchor matches the null string before any 
      /// newline in the string in addition to its normal function.
      Newline=4
    };

    /// Compiles the given POSIX Extended Regular Expression \arg Regex.
    /// This implementation supports regexes and matching strings with embedded
    /// NUL characters.
    Regex(const StringRef &Regex, unsigned Flags=NoSub);
    ~Regex();

    /// isValid - returns the error encountered during regex compilation, or
    /// matching, if any.
    bool isValid(std::string &Error);

    /// matches - Match the regex against a given \arg String.
    ///
    /// \param Matches - If given, on a succesful match this will be filled in
    /// with references to the matched group expressions (inside \arg String),
    /// the first group is always the entire pattern.
    /// By default the regex is compiled with NoSub, which disables support for
    /// Matches.
    /// For this feature to be enabled you must construct the regex using
    /// Regex("...", Regex::Sub) constructor.

    bool match(const StringRef &String, SmallVectorImpl<StringRef> *Matches=0);
  private:
    struct llvm_regex *preg;
    int error;
    bool sub;
  };
}
