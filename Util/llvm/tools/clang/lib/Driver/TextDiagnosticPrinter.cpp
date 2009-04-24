//===--- TextDiagnosticPrinter.cpp - Diagnostic Printer -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This diagnostic client prints out their diagnostic messages.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/TextDiagnosticPrinter.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallString.h"
using namespace clang;

void TextDiagnosticPrinter::
PrintIncludeStack(SourceLocation Loc, const SourceManager &SM) {
  if (Loc.isInvalid()) return;

  PresumedLoc PLoc = SM.getPresumedLoc(Loc);

  // Print out the other include frames first.
  PrintIncludeStack(PLoc.getIncludeLoc(), SM);
  
  OS << "In file included from " << PLoc.getFilename()
     << ':' << PLoc.getLine() << ":\n";
}

/// HighlightRange - Given a SourceRange and a line number, highlight (with ~'s)
/// any characters in LineNo that intersect the SourceRange.
void TextDiagnosticPrinter::HighlightRange(const SourceRange &R,
                                           const SourceManager &SM,
                                           unsigned LineNo, FileID FID,
                                           std::string &CaretLine,
                                           const std::string &SourceLine) {
  assert(CaretLine.size() == SourceLine.size() &&
         "Expect a correspondence between source and caret line!");
  if (!R.isValid()) return;

  SourceLocation Begin = SM.getInstantiationLoc(R.getBegin());
  SourceLocation End = SM.getInstantiationLoc(R.getEnd());
  
  unsigned StartLineNo = SM.getLineNumber(Begin);
  if (StartLineNo > LineNo || SM.getFileID(Begin) != FID)
    return;  // No intersection.
  
  unsigned EndLineNo = SM.getLineNumber(End);
  if (EndLineNo < LineNo || SM.getFileID(End) != FID)
    return;  // No intersection.
  
  // Compute the column number of the start.
  unsigned StartColNo = 0;
  if (StartLineNo == LineNo) {
    StartColNo = SM.getColumnNumber(Begin);
    if (StartColNo) --StartColNo;  // Zero base the col #.
  }

  // Pick the first non-whitespace column.
  while (StartColNo < SourceLine.size() &&
         (SourceLine[StartColNo] == ' ' || SourceLine[StartColNo] == '\t'))
    ++StartColNo;
  
  // Compute the column number of the end.
  unsigned EndColNo = CaretLine.size();
  if (EndLineNo == LineNo) {
    EndColNo = SM.getColumnNumber(End);
    if (EndColNo) {
      --EndColNo;  // Zero base the col #.
      
      // Add in the length of the token, so that we cover multi-char tokens.
      EndColNo += Lexer::MeasureTokenLength(End, SM);
    } else {
      EndColNo = CaretLine.size();
    }
  }
  
  // Pick the last non-whitespace column.
  if (EndColNo <= SourceLine.size())
    while (EndColNo-1 &&
           (SourceLine[EndColNo-1] == ' ' || SourceLine[EndColNo-1] == '\t'))
      --EndColNo;
  else
    EndColNo = SourceLine.size();
  
  // Fill the range with ~'s.
  assert(StartColNo <= EndColNo && "Invalid range!");
  for (unsigned i = StartColNo; i < EndColNo; ++i)
    CaretLine[i] = '~';
}

void TextDiagnosticPrinter::HandleDiagnostic(Diagnostic::Level Level, 
                                             const DiagnosticInfo &Info) {
  unsigned ColNo = 0;
  
  // If the location is specified, print out a file/line/col and include trace
  // if enabled.
  if (Info.getLocation().isValid()) {
    const SourceManager &SM = Info.getLocation().getManager();
    PresumedLoc PLoc = SM.getPresumedLoc(Info.getLocation());
    unsigned LineNo = PLoc.getLine();
    
    // First, if this diagnostic is not in the main file, print out the
    // "included from" lines.
    if (LastWarningLoc != PLoc.getIncludeLoc()) {
      LastWarningLoc = PLoc.getIncludeLoc();
      PrintIncludeStack(LastWarningLoc, SM);
    }
  
    // Compute the column number.
    ColNo = PLoc.getColumn();
    if (ShowLocation) {
      OS << PLoc.getFilename() << ':' << LineNo << ':';
      if (ColNo && ShowColumn) 
        OS << ColNo << ':';
      OS << ' ';
    }
  }
  
  switch (Level) {
  default: assert(0 && "Unknown diagnostic type!");
  case Diagnostic::Note:    OS << "note: "; break;
  case Diagnostic::Warning: OS << "warning: "; break;
  case Diagnostic::Error:   OS << "error: "; break;
  }
  
  llvm::SmallString<100> OutStr;
  Info.FormatDiagnostic(OutStr);
  OS.write(OutStr.begin(), OutStr.size());
  OS << '\n';
  
  // If caret diagnostics are enabled and we have location, we want to emit the
  // caret.  However, we only do this if the location moved from the last
  // diagnostic, or if the diagnostic has ranges.  We don't want to emit the
  // same caret multiple times if one loc has multiple diagnostics.
  if (CaretDiagnostics && Info.getLocation().isValid() &&
      ((LastLoc != Info.getLocation()) || Info.getNumRanges())) {
    // Cache the LastLoc, it allows us to omit duplicate source/caret spewage.
    LastLoc = Info.getLocation();

    // Inspect the actual instantiation point of the diagnostic, we don't care
    // about presumed locations anymore.
    FullSourceLoc ILoc = Info.getLocation().getInstantiationLoc();

    // Rewind from the current position to the start of the line.
    const char *TokInstantiationPtr = ILoc.getCharacterData();
    const char *LineStart = TokInstantiationPtr-ColNo+1; // Column # is 1-based.
    
    // Compute the line end.  Scan forward from the error position to the end of
    // the line.
    const char *BufEnd = ILoc.getBufferData().second;
    const char *LineEnd = TokInstantiationPtr;
    while (LineEnd != BufEnd && 
           *LineEnd != '\n' && *LineEnd != '\r')
      ++LineEnd;
    
    // Copy the line of code into an std::string for ease of manipulation.
    std::string SourceLine(LineStart, LineEnd);
    
    // Create a line for the caret that is filled with spaces that is the same
    // length as the line of source code.
    std::string CaretLine(LineEnd-LineStart, ' ');

    // Highlight all of the characters covered by Ranges with ~ characters.
    for (unsigned i = 0; i != Info.getNumRanges(); ++i)
      HighlightRange(Info.getRange(i), ILoc.getManager(), ILoc.getLineNumber(),
                     ILoc.getFileID(), CaretLine, SourceLine);
    
    // Next, insert the caret itself.
    if (ColNo-1 < CaretLine.size())
      CaretLine[ColNo-1] = '^';
    else
      CaretLine.push_back('^');
    
    // Scan the source line, looking for tabs.  If we find any, manually expand
    // them to 8 characters and update the CaretLine to match.
    for (unsigned i = 0; i != SourceLine.size(); ++i) {
      if (SourceLine[i] != '\t') continue;
      
      // Replace this tab with at least one space.
      SourceLine[i] = ' ';
      
      // Compute the number of spaces we need to insert.
      unsigned NumSpaces = ((i+8)&~7) - (i+1);
      assert(NumSpaces < 8 && "Invalid computation of space amt");
      
      // Insert spaces into the SourceLine.
      SourceLine.insert(i+1, NumSpaces, ' ');
      
      // Insert spaces or ~'s into CaretLine.
      CaretLine.insert(i+1, NumSpaces, CaretLine[i] == '~' ? '~' : ' ');
    }
    
    // Finally, remove any blank spaces from the end of CaretLine.
    while (CaretLine[CaretLine.size()-1] == ' ')
      CaretLine.erase(CaretLine.end()-1);
    
    // Emit what we have computed.
    OS << SourceLine << '\n';
    OS << CaretLine << '\n';
  }
  
  OS.flush();
}
