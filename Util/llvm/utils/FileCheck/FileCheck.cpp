//===- FileCheck.cpp - Check that File's Contents match what is expected --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// FileCheck does a line-by line check of a file that validates whether it
// contains the expected content.  This is useful for regression tests etc.
//
// This program exits with an error status of 2 on error, exit status of 0 if
// the file matched the expected contents, and exit status of 1 if it did not
// contain the expected contents.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Signals.h"
using namespace llvm;

static cl::opt<std::string>
CheckFilename(cl::Positional, cl::desc("<check-file>"), cl::Required);

static cl::opt<std::string>
InputFilename("input-file", cl::desc("File to check (defaults to stdin)"),
              cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
CheckPrefix("check-prefix", cl::init("CHECK"),
            cl::desc("Prefix to use from check file (defaults to 'CHECK')"));


/// FindStringInBuffer - This is basically just a strstr wrapper that differs in
/// two ways: first it handles 'nul' characters in memory buffers, second, it
/// returns the end of the memory buffer on match failure.
static const char *FindStringInBuffer(const char *Str, const char *CurPtr,
                                      const MemoryBuffer &MB) {
  // Check to see if we have a match.  If so, just return it.
  if (const char *Res = strstr(CurPtr, Str))
    return Res;
  
  // If not, check to make sure we didn't just find an embedded nul in the
  // memory buffer.
  const char *Ptr = CurPtr + strlen(CurPtr);
  
  // If we really reached the end of the file, return it.
  if (Ptr == MB.getBufferEnd())
    return Ptr;
    
  // Otherwise, just skip this section of the file, including the nul.
  return FindStringInBuffer(Str, Ptr+1, MB);
}

/// ReadCheckFile - Read the check file, which specifies the sequence of
/// expected strings.  The strings are added to the CheckStrings vector.
static bool ReadCheckFile(SourceMgr &SM,
                          std::vector<std::pair<std::string, SMLoc> >
                                         &CheckStrings) {
  // Open the check file, and tell SourceMgr about it.
  std::string ErrorStr;
  MemoryBuffer *F =
    MemoryBuffer::getFileOrSTDIN(CheckFilename.c_str(), &ErrorStr);
  if (F == 0) {
    errs() << "Could not open check file '" << CheckFilename << "': " 
           << ErrorStr << '\n';
    return true;
  }
  SM.AddNewSourceBuffer(F, SMLoc());

  // Find all instances of CheckPrefix followed by : in the file.  The
  // MemoryBuffer is guaranteed to be nul terminated, but may have nul's
  // embedded into it.  We don't support check strings with embedded nuls.
  std::string Prefix = CheckPrefix + ":";
  const char *CurPtr = F->getBufferStart(), *BufferEnd = F->getBufferEnd();

  while (1) {
    // See if Prefix occurs in the memory buffer.
    const char *Ptr = FindStringInBuffer(Prefix.c_str(), CurPtr, *F);
    
    // If we didn't find a match, we're done.
    if (Ptr == BufferEnd)
      break;
    
    // Okay, we found the prefix, yay.  Remember the rest of the line, but
    // ignore leading and trailing whitespace.
    Ptr += Prefix.size();
    while (*Ptr == ' ' || *Ptr == '\t')
      ++Ptr;
    
    // Scan ahead to the end of line.
    CurPtr = Ptr;
    while (CurPtr != BufferEnd && *CurPtr != '\n' && *CurPtr != '\r')
      ++CurPtr;
    
    // Ignore trailing whitespace.
    while (CurPtr[-1] == ' ' || CurPtr[-1] == '\t')
      --CurPtr;
    
    // Check that there is something on the line.
    if (Ptr >= CurPtr) {
      SM.PrintMessage(SMLoc::getFromPointer(CurPtr),
                      "found empty check string with prefix '"+Prefix+"'",
                      "error");
      return true;
    }
    
    // Okay, add the string we captured to the output vector and move on.
    CheckStrings.push_back(std::make_pair(std::string(Ptr, CurPtr),
                                          SMLoc::getFromPointer(Ptr)));
  }
  
  if (CheckStrings.empty()) {
    errs() << "error: no check strings found with prefix '" << Prefix << "'\n";
    return true;
  }
  
  return false;
}


int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv);

  SourceMgr SM;
  
  // Read the expected strings from the check file.
  std::vector<std::pair<std::string, SMLoc> > CheckStrings;
  if (ReadCheckFile(SM, CheckStrings))
    return 2;

  // Open the file to check and add it to SourceMgr.
  std::string ErrorStr;
  MemoryBuffer *F =
    MemoryBuffer::getFileOrSTDIN(InputFilename.c_str(), &ErrorStr);
  if (F == 0) {
    errs() << "Could not open input file '" << InputFilename << "': " 
           << ErrorStr << '\n';
    return true;
  }
  SM.AddNewSourceBuffer(F, SMLoc());
  
  // Check that we have all of the expected strings, in order, in the input
  // file.
  const char *CurPtr = F->getBufferStart(), *BufferEnd = F->getBufferEnd();
  
  for (unsigned StrNo = 0, e = CheckStrings.size(); StrNo != e; ++StrNo) {
    const std::pair<std::string, SMLoc> &CheckStr = CheckStrings[StrNo];
    
    // Find StrNo in the file.
    const char *Ptr = FindStringInBuffer(CheckStr.first.c_str(), CurPtr, *F);
    
    // If we found a match, we're done, move on.
    if (Ptr != BufferEnd) {
      CurPtr = Ptr + CheckStr.first.size();
      continue;
    }
    
    // Otherwise, we have an error, emit an error message.
    SM.PrintMessage(CheckStr.second, "expected string not found in input",
                    "error");
    SM.PrintMessage(SMLoc::getFromPointer(CurPtr), "scanning from here",
                    "note");
    return 1;
  }
  
  return 0;
}
