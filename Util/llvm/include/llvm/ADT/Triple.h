//===-- llvm/ADT/Triple.h - Target triple helper class ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TRIPLE_H
#define LLVM_ADT_TRIPLE_H

#include <string>

namespace llvm {

/// Triple - Helper class for working with target triples.
///
/// Target triples are strings in the format of:
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM
/// or
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT
///
/// This class is used for clients which want to support arbitrary
/// target triples, but also want to implement certain special
/// behavior for particular targets. This class isolates the mapping
/// from the components of the target triple to well known IDs.
///
/// See autoconf/config.guess for a glimpse into what they look like
/// in practice.
class Triple {
public:
  enum ArchType {
    UnknownArch,
    
    x86,    // i?86
    ppc,    // powerpc
    ppc64,  // powerpc64
    x86_64, // amd64, x86_64

    InvalidArch
  };
  enum VendorType {
    UnknownVendor,

    Apple, 
    PC
  };
  enum OSType {
    UnknownOS,

    AuroraUX,
    Darwin,
    DragonFly,
    FreeBSD,
    Linux,
    OpenBSD
  };
  
private:
  std::string Data;

  /// The parsed arch type (or InvalidArch if uninitialized).
  mutable ArchType Arch;

  /// The parsed vendor type.
  mutable VendorType Vendor;

  /// The parsed OS type.
  mutable OSType OS;

  bool isInitialized() const { return Arch != InvalidArch; }
  void Parse() const;

public:
  /// @name Constructors
  /// @{
  
  Triple() : Data(""), Arch(InvalidArch) {}
  explicit Triple(const char *Str) : Data(Str), Arch(InvalidArch) {}
  explicit Triple(const char *ArchStr, const char *VendorStr, const char *OSStr)
    : Data(ArchStr), Arch(InvalidArch) {
    Data += '-';
    Data += VendorStr;
    Data += '-';
    Data += OSStr;
  }

  /// @}
  /// @name Typed Component Access
  /// @{
  
  /// getArch - Get the parsed architecture type of this triple.
  ArchType getArch() const { 
    if (!isInitialized()) Parse(); 
    return Arch;
  }
  
  /// getVendor - Get the parsed vendor type of this triple.
  VendorType getVendor() const { 
    if (!isInitialized()) Parse(); 
    return Vendor;
  }
  
  /// getOS - Get the parsed operating system type of this triple.
  OSType getOS() const { 
    if (!isInitialized()) Parse(); 
    return OS;
  }

  /// hasEnvironment - Does this triple have the optional environment
  /// (fourth) component?
  bool hasEnvironment() const {
    return getEnvironmentName() != "";
  }

  /// @}
  /// @name Direct Component Access
  /// @{

  const std::string &getTriple() const { return Data; }

  // FIXME: Invent a lightweight string representation for these to
  // use.

  /// getArchName - Get the architecture (first) component of the
  /// triple.
  std::string getArchName() const;

  /// getVendorName - Get the vendor (second) component of the triple.
  std::string getVendorName() const;

  /// getOSName - Get the operating system (third) component of the
  /// triple.
  std::string getOSName() const;

  /// getEnvironmentName - Get the optional environment (fourth)
  /// component of the triple, or "" if empty.
  std::string getEnvironmentName() const;

  /// getOSAndEnvironmentName - Get the operating system and optional
  /// environment components as a single string (separated by a '-'
  /// if the environment component is present).
  std::string getOSAndEnvironmentName() const;

  /// @}
  /// @name Mutators
  /// @{

  /// setArch - Set the architecture (first) component of the triple
  /// to a known type.
  void setArch(ArchType Kind);

  /// setVendor - Set the vendor (second) component of the triple to a
  /// known type.
  void setVendor(VendorType Kind);

  /// setOS - Set the operating system (third) component of the triple
  /// to a known type.
  void setOS(OSType Kind);

  /// setTriple - Set all components to the new triple \arg Str.
  void setTriple(const std::string &Str);

  /// setArchName - Set the architecture (first) component of the
  /// triple by name.
  void setArchName(const std::string &Str);

  /// setVendorName - Set the vendor (second) component of the triple
  /// by name.
  void setVendorName(const std::string &Str);

  /// setOSName - Set the operating system (third) component of the
  /// triple by name.
  void setOSName(const std::string &Str);

  /// setEnvironmentName - Set the optional environment (fourth)
  /// component of the triple by name.
  void setEnvironmentName(const std::string &Str);

  /// setOSAndEnvironmentName - Set the operating system and optional
  /// environment components with a single string.
  void setOSAndEnvironmentName(const std::string &Str);

  /// @}
  /// @name Static helpers for IDs.
  /// @{

  /// getArchTypeName - Get the canonical name for the \arg Kind
  /// architecture.
  static const char *getArchTypeName(ArchType Kind);

  /// getVendorTypeName - Get the canonical name for the \arg Kind
  /// vendor.
  static const char *getVendorTypeName(VendorType Kind);

  /// getOSTypeName - Get the canonical name for the \arg Kind vendor.
  static const char *getOSTypeName(OSType Kind);

  /// @}
};

} // End llvm namespace


#endif
