//===--- PlistDiagnostics.cpp - Plist Diagnostics for Paths -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PlistDiagnostics object.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/PathDiagnosticClients.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/System/Path.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;

typedef llvm::DenseMap<FileID, unsigned> FIDMap;

namespace clang {
  class Preprocessor;
  class PreprocessorFactory;
}

namespace {
  class VISIBILITY_HIDDEN PlistDiagnostics : public PathDiagnosticClient {
    std::vector<const PathDiagnostic*> BatchedDiags;
    const std::string OutputFile;
  public:
    PlistDiagnostics(const std::string& prefix);
    ~PlistDiagnostics();
    void HandlePathDiagnostic(const PathDiagnostic* D);  
  };  
} // end anonymous namespace

PlistDiagnostics::PlistDiagnostics(const std::string& output)
  : OutputFile(output) {}

PathDiagnosticClient*
clang::CreatePlistDiagnosticClient(const std::string& s,
                                   Preprocessor*, PreprocessorFactory*) {
  return new PlistDiagnostics(s);
}

static void AddFID(FIDMap &FIDs, llvm::SmallVectorImpl<FileID> &V,
                   SourceManager* SM, SourceLocation L) {

  FileID FID = SM->getFileID(SM->getInstantiationLoc(L));
  FIDMap::iterator I = FIDs.find(FID);
  if (I != FIDs.end()) return;
  FIDs[FID] = V.size();
  V.push_back(FID);
}

static unsigned GetFID(const FIDMap& FIDs, SourceManager* SM, SourceLocation L){
  FileID FID = SM->getFileID(SM->getInstantiationLoc(L));
  FIDMap::const_iterator I = FIDs.find(FID);
  assert(I != FIDs.end());
  return I->second;
}

static llvm::raw_ostream& Indent(llvm::raw_ostream& o, const unsigned indent) {
  for (unsigned i = 0; i < indent; ++i)
    o << ' ';
  return o;
}

static void EmitLocation(llvm::raw_ostream& o, SourceManager* SM,
                         SourceLocation L, const FIDMap& FM,
                         const unsigned indent) {

  Indent(o, indent) << "<dict>\n";
  Indent(o, indent) << " <key>line</key><integer>"
                    << SM->getInstantiationLineNumber(L) << "</integer>\n";
  Indent(o, indent) << " <key>col</key><integer>"
                    << SM->getInstantiationColumnNumber(L) << "</integer>\n";
  Indent(o, indent) << " <key>file</key><integer>"
                    << GetFID(FM, SM, L) << "</integer>\n";
  Indent(o, indent) << "</dict>\n";
}

static void EmitRange(llvm::raw_ostream& o, SourceManager* SM, SourceRange R,
                      const FIDMap& FM, const unsigned indent) {
 
  Indent(o, indent) << "<array>\n";
  EmitLocation(o, SM, R.getBegin(), FM, indent+1);
  EmitLocation(o, SM, R.getEnd(), FM, indent+1);
  Indent(o, indent) << "</array>\n";
}

static void ReportDiag(llvm::raw_ostream& o, const PathDiagnosticPiece& P, 
                       const FIDMap& FM, SourceManager* SM) {
  
  unsigned indent = 4;
  Indent(o, indent) << "<dict>\n";
  ++indent;
  
  // Output the location.
  FullSourceLoc L = P.getLocation();

  Indent(o, indent) << "<key>location</key>\n";
  EmitLocation(o, SM, L, FM, indent);

  // Output the ranges (if any).
  PathDiagnosticPiece::range_iterator RI = P.ranges_begin(),
                                      RE = P.ranges_end();
  
  if (RI != RE) {
    Indent(o, indent) << "<key>ranges</key>\n";
    Indent(o, indent) << "<array>\n";
    ++indent;
    for ( ; RI != RE; ++RI ) EmitRange(o, SM, *RI, FM, indent+1);
    --indent;
    Indent(o, indent) << "</array>\n";
  }
  
  // Output the text.
  Indent(o, indent) << "<key>message</key>\n";
  Indent(o, indent) << "<string>" << P.getString() << "</string>\n";
  
  // Output the hint.
  Indent(o, indent) << "<key>displayhint</key>\n";
  Indent(o, indent) << "<string>"
                    << (P.getDisplayHint() == PathDiagnosticPiece::Above 
                        ? "above" : "below")
                    << "</string>\n";
  
  
  // Finish up.
  --indent;
  Indent(o, indent); o << "</dict>\n";
}

void PlistDiagnostics::HandlePathDiagnostic(const PathDiagnostic* D) {
  if (!D)
    return;
  
  if (D->empty()) {
    delete D;
    return;
  }
  
  BatchedDiags.push_back(D);
}

PlistDiagnostics::~PlistDiagnostics() { 

  // Build up a set of FIDs that we use by scanning the locations and
  // ranges of the diagnostics.
  FIDMap FM;
  llvm::SmallVector<FileID, 10> Fids;
  SourceManager* SM = 0;
  
  if (!BatchedDiags.empty())  
    SM = &(*BatchedDiags.begin())->begin()->getLocation().getManager();

  for (std::vector<const PathDiagnostic*>::iterator DI = BatchedDiags.begin(),
       DE = BatchedDiags.end(); DI != DE; ++DI) {
    
    const PathDiagnostic *D = *DI;
  
    for (PathDiagnostic::const_iterator I=D->begin(), E=D->end(); I!=E; ++I) {
      AddFID(FM, Fids, SM, I->getLocation());
    
      for (PathDiagnosticPiece::range_iterator RI=I->ranges_begin(),
           RE=I->ranges_end(); RI!=RE; ++RI) {
        AddFID(FM, Fids, SM, RI->getBegin());
        AddFID(FM, Fids, SM, RI->getEnd());
      }
    }
  }

  // Open the file.
  std::string ErrMsg;
  llvm::raw_fd_ostream o(OutputFile.c_str(), false, ErrMsg);
  if (!ErrMsg.empty()) {
    llvm::errs() << "warning: could not creat file: " << OutputFile << '\n';
    return;
  }
  
  // Write the plist header.
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" "
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
  "<plist version=\"1.0\">\n";
  
  // Write the root object: a <dict> containing...
  //  - "files", an <array> mapping from FIDs to file names
  //  - "diagnostics", an <array> containing the path diagnostics  
  o << "<dict>\n"
       " <key>files</key>\n"
       " <array>\n";
  
  for (llvm::SmallVectorImpl<FileID>::iterator I=Fids.begin(), E=Fids.end();
       I!=E; ++I)
    o << "  <string>" << SM->getFileEntryForID(*I)->getName() << "</string>\n";    
  
  o << " </array>\n"
       " <key>diagnostics</key>\n"
       " <array>\n";
  
  for (std::vector<const PathDiagnostic*>::iterator DI=BatchedDiags.begin(),
       DE = BatchedDiags.end(); DI!=DE; ++DI) {
       
    o << "  <dict>\n"
         "   <key>path</key>\n";
    
    const PathDiagnostic *D = *DI;
    // Create an owning smart pointer for 'D' just so that we auto-free it
    // when we exit this method.
    llvm::OwningPtr<PathDiagnostic> OwnedD(const_cast<PathDiagnostic*>(D));

    o << "   <array>\n";
  
    for (PathDiagnostic::const_iterator I=D->begin(), E=D->end(); I != E; ++I)
      ReportDiag(o, *I, FM, SM);
    
    o << "   </array>\n";
    
    // Output the bug type and bug category.  
    o << "   <key>description</key>\n   <string>" << D->getDescription()
      << "</string>\n"
      << "   <key>category</key>\n   <string>" << D->getCategory()
      << "</string>\n"
      << "  </dict>\n";    
  }

  o << " </array>\n";
  
  // Finish.
  o << "</dict>\n</plist>";
}
