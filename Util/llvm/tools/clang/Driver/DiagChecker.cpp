//===--- DiagChecker.cpp - Diagnostic Checking Functions ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Process the input files and check that the diagnostic messages are expected.
//
//===----------------------------------------------------------------------===//

#include "clang.h"
#include "ASTConsumers.h"
#include "clang/Driver/TextDiagnosticBuffer.h"
#include "clang/Sema/ParseAST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
using namespace clang;

typedef TextDiagnosticBuffer::DiagList DiagList;
typedef TextDiagnosticBuffer::const_iterator const_diag_iterator;

static void EmitError(Preprocessor &PP, SourceLocation Pos, const char *String){
  unsigned ID = PP.getDiagnostics().getCustomDiagID(Diagnostic::Error, String);
  PP.Diag(Pos, ID);
}


// USING THE DIAGNOSTIC CHECKER:
//
// Indicating that a line expects an error or a warning is simple. Put a comment
// on the line that has the diagnostic, use "expected-{error,warning}" to tag
// if it's an expected error or warning, and place the expected text between {{
// and }} markers. The full text doesn't have to be included, only enough to
// ensure that the correct diagnostic was emitted.
//
// Here's an example:
//
//   int A = B; // expected-error {{use of undeclared identifier 'B'}}
//
// You can place as many diagnostics on one line as you wish. To make the code
// more readable, you can use slash-newline to separate out the diagnostics.

/// FindDiagnostics - Go through the comment and see if it indicates expected
/// diagnostics. If so, then put them in a diagnostic list.
/// 
static void FindDiagnostics(const char *CommentStart, unsigned CommentLen,
                            DiagList &ExpectedDiags,
                            Preprocessor &PP, SourceLocation Pos,
                            const char *ExpectedStr) {
  const char *CommentEnd = CommentStart+CommentLen;
  unsigned ExpectedStrLen = strlen(ExpectedStr);
  
  // Find all expected-foo diagnostics in the string and add them to
  // ExpectedDiags.
  while (CommentStart != CommentEnd) {
    CommentStart = std::find(CommentStart, CommentEnd, 'e');
    if (unsigned(CommentEnd-CommentStart) < ExpectedStrLen) return;
    
    // If this isn't expected-foo, ignore it.
    if (memcmp(CommentStart, ExpectedStr, ExpectedStrLen)) {
      ++CommentStart;
      continue;
    }
    
    CommentStart += ExpectedStrLen;
    
    // Skip whitespace.
    while (CommentStart != CommentEnd &&
           isspace(CommentStart[0]))
      ++CommentStart;
    
    // We should have a {{ now.
    if (CommentEnd-CommentStart < 2 ||
        CommentStart[0] != '{' || CommentStart[1] != '{') {
      if (std::find(CommentStart, CommentEnd, '{') != CommentEnd)
        EmitError(PP, Pos, "bogus characters before '{{' in expected string");
      else
        EmitError(PP, Pos, "cannot find start ('{{') of expected string");
      return;
    }    
    CommentStart += 2;

    // Find the }}.
    const char *ExpectedEnd = CommentStart;
    while (1) {
      ExpectedEnd = std::find(ExpectedEnd, CommentEnd, '}');
      if (CommentEnd-ExpectedEnd < 2) {
        EmitError(PP, Pos, "cannot find end ('}}') of expected string");
        return;
      }
      
      if (ExpectedEnd[1] == '}')
        break;

      ++ExpectedEnd;  // Skip over singular }'s
    }

    std::string Msg(CommentStart, ExpectedEnd);
    std::string::size_type FindPos;
    while ((FindPos = Msg.find("\\n")) != std::string::npos)
      Msg.replace(FindPos, 2, "\n");
    ExpectedDiags.push_back(std::make_pair(Pos, Msg));
    
    CommentStart = ExpectedEnd;
  }
}

/// FindExpectedDiags - Lex the main source file to find all of the
//   expected errors and warnings.
static void FindExpectedDiags(Preprocessor &PP,
                              DiagList &ExpectedErrors,
                              DiagList &ExpectedWarnings,
                              DiagList &ExpectedNotes) {
  // Create a raw lexer to pull all the comments out of the main file.  We don't
  // want to look in #include'd headers for expected-error strings.
  FileID FID = PP.getSourceManager().getMainFileID();
  
  // Create a lexer to lex all the tokens of the main file in raw mode.
  Lexer RawLex(FID, PP.getSourceManager(), PP.getLangOptions());
  
  // Return comments as tokens, this is how we find expected diagnostics.
  RawLex.SetCommentRetentionState(true);

  Token Tok;
  Tok.setKind(tok::comment);
  while (Tok.isNot(tok::eof)) {
    RawLex.Lex(Tok);
    if (!Tok.is(tok::comment)) continue;
    
    std::string Comment = PP.getSpelling(Tok);
    if (Comment.empty()) continue;

    
    // Find all expected errors.
    FindDiagnostics(&Comment[0], Comment.size(), ExpectedErrors, PP,
                    Tok.getLocation(), "expected-error");

    // Find all expected warnings.
    FindDiagnostics(&Comment[0], Comment.size(), ExpectedWarnings, PP,
                    Tok.getLocation(), "expected-warning");

    // Find all expected notes.
    FindDiagnostics(&Comment[0], Comment.size(), ExpectedNotes, PP,
                    Tok.getLocation(), "expected-note");
  };
}

/// PrintProblem - This takes a diagnostic map of the delta between expected and
/// seen diagnostics. If there's anything in it, then something unexpected
/// happened. Print the map out in a nice format and return "true". If the map
/// is empty and we're not going to print things, then return "false".
/// 
static bool PrintProblem(SourceManager &SourceMgr,
                         const_diag_iterator diag_begin,
                         const_diag_iterator diag_end,
                         const char *Msg) {
  if (diag_begin == diag_end) return false;

  fprintf(stderr, "%s\n", Msg);

  for (const_diag_iterator I = diag_begin, E = diag_end; I != E; ++I)
    fprintf(stderr, "  Line %d: %s\n",
            SourceMgr.getInstantiationLineNumber(I->first),
            I->second.c_str());

  return true;
}

/// CompareDiagLists - Compare two diangnostic lists and return the difference
/// between them.
/// 
static bool CompareDiagLists(SourceManager &SourceMgr,
                             const_diag_iterator d1_begin,
                             const_diag_iterator d1_end,
                             const_diag_iterator d2_begin,
                             const_diag_iterator d2_end,
                             const char *Msg) {
  DiagList DiffList;

  for (const_diag_iterator I = d1_begin, E = d1_end; I != E; ++I) {
    unsigned LineNo1 = SourceMgr.getInstantiationLineNumber(I->first);
    const std::string &Diag1 = I->second;
    bool Found = false;

    for (const_diag_iterator II = d2_begin, IE = d2_end; II != IE; ++II) {
      unsigned LineNo2 = SourceMgr.getInstantiationLineNumber(II->first);
      if (LineNo1 != LineNo2) continue;

      const std::string &Diag2 = II->second;
      if (Diag2.find(Diag1) != std::string::npos ||
          Diag1.find(Diag2) != std::string::npos) {
        Found = true;
        break;
      }
    }

    if (!Found)
      DiffList.push_back(std::make_pair(I->first, Diag1));
  }

  return PrintProblem(SourceMgr, DiffList.begin(), DiffList.end(), Msg);
}

/// CheckResults - This compares the expected results to those that
/// were actually reported. It emits any discrepencies. Return "true" if there
/// were problems. Return "false" otherwise.
/// 
static bool CheckResults(Preprocessor &PP,
                         const DiagList &ExpectedErrors,
                         const DiagList &ExpectedWarnings,
                         const DiagList &ExpectedNotes) {
  const DiagnosticClient *DiagClient = PP.getDiagnostics().getClient();
  assert(DiagClient != 0 &&
      "DiagChecker requires a valid TextDiagnosticBuffer");
  const TextDiagnosticBuffer &Diags =
    static_cast<const TextDiagnosticBuffer&>(*DiagClient);
  SourceManager &SourceMgr = PP.getSourceManager();

  // We want to capture the delta between what was expected and what was
  // seen.
  //
  //   Expected \ Seen - set expected but not seen
  //   Seen \ Expected - set seen but not expected
  bool HadProblem = false;

  // See if there were errors that were expected but not seen.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 ExpectedErrors.begin(), ExpectedErrors.end(),
                                 Diags.err_begin(), Diags.err_end(),
                                 "Errors expected but not seen:");

  // See if there were errors that were seen but not expected.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 Diags.err_begin(), Diags.err_end(),
                                 ExpectedErrors.begin(), ExpectedErrors.end(),
                                 "Errors seen but not expected:");

  // See if there were warnings that were expected but not seen.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 ExpectedWarnings.begin(),
                                 ExpectedWarnings.end(),
                                 Diags.warn_begin(), Diags.warn_end(),
                                 "Warnings expected but not seen:");

  // See if there were warnings that were seen but not expected.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 Diags.warn_begin(), Diags.warn_end(),
                                 ExpectedWarnings.begin(),
                                 ExpectedWarnings.end(),
                                 "Warnings seen but not expected:");

  // See if there were notes that were expected but not seen.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 ExpectedNotes.begin(),
                                 ExpectedNotes.end(),
                                 Diags.note_begin(), Diags.note_end(),
                                 "Notes expected but not seen:");

  // See if there were notes that were seen but not expected.
  HadProblem |= CompareDiagLists(SourceMgr,
                                 Diags.note_begin(), Diags.note_end(),
                                 ExpectedNotes.begin(),
                                 ExpectedNotes.end(),
                                 "Notes seen but not expected:");

  return HadProblem;
}


/// CheckDiagnostics - Gather the expected diagnostics and check them.
bool clang::CheckDiagnostics(Preprocessor &PP) {
  // Gather the set of expected diagnostics.
  DiagList ExpectedErrors, ExpectedWarnings, ExpectedNotes;
  FindExpectedDiags(PP, ExpectedErrors, ExpectedWarnings, ExpectedNotes);

  // Check that the expected diagnostics occurred.
  return CheckResults(PP, ExpectedErrors, ExpectedWarnings, ExpectedNotes);
}
