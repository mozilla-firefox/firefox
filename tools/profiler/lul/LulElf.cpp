// Copyright (c) 2006, 2011, 2012 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Restructured in 2009 by: Jim Blandy <jimb@mozilla.com> <jimb@red-bean.com>

// (derived from)
// dump_symbols.cc: implement google_breakpad::WriteSymbolFile:
// Find all the debugging info in a file and dump it as a Breakpad symbol file.
//
// dump_symbols.h: Read debugging information from an ELF file, and write
// it out as a Breakpad symbol file.

// This file is derived from the following files in
// toolkit/crashreporter/google-breakpad:
//   src/common/linux/dump_symbols.cc
//   src/common/linux/elfutils.cc
//   src/common/linux/file_id.cc

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <set>
#include <string>
#include <vector>

#include "mozilla/Assertions.h"
#include "mozilla/Sprintf.h"

#include "mozilla/ProfilerPlatformMacros.h"
#include "LulCommonExt.h"
#include "LulDwarfExt.h"
#include "LulElfInt.h"
#include "LulMainInt.h"

#if defined(GP_PLAT_arm_android) && !defined(SHT_ARM_EXIDX)
// bionic and older glibsc don't define it
#  define SHT_ARM_EXIDX (SHT_LOPROC + 1)
#endif

#if (defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_amd64_android)) && \
    !defined(SHT_X86_64_UNWIND)
// This is sometimes necessary on x86_64-android and x86_64-linux.
#  define SHT_X86_64_UNWIND 0x70000001
#endif

// Old Linux header doesn't define EM_AARCH64
#ifndef EM_AARCH64
#  define EM_AARCH64 183
#endif

// Old Linux header doesn't define EM_RISCV.
#ifndef EM_RISCV
#  define EM_RISCV 243
#endif

// Old Linux header doesn't define EM_LOONGARCH.
#ifndef EM_LOONGARCH
#  define EM_LOONGARCH 258
#endif

// This namespace contains helper functions.
namespace {

using lul::DwarfCFIToModule;
using lul::FindElfSectionByName;
using lul::GetOffset;
using lul::IsValidElf;
using lul::Module;
using lul::scoped_ptr;
using lul::Summariser;
using lul::UniqueStringUniverse;
using std::set;
using std::string;
using std::vector;

//
// FDWrapper
//
// Wrapper class to make sure opened file is closed.
//
class FDWrapper {
 public:
  explicit FDWrapper(int fd) : fd_(fd) {}
  ~FDWrapper() {
    if (fd_ != -1) close(fd_);
  }
  int get() { return fd_; }
  int release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

 private:
  int fd_;
};

//
// MmapWrapper
//
// Wrapper class to make sure mapped regions are unmapped.
//
class MmapWrapper {
 public:
  MmapWrapper() : is_set_(false), base_(nullptr), size_(0) {}
  ~MmapWrapper() {
    if (is_set_ && base_ != nullptr) {
      MOZ_ASSERT(size_ > 0);
      munmap(base_, size_);
    }
  }
  void set(void* mapped_address, size_t mapped_size) {
    is_set_ = true;
    base_ = mapped_address;
    size_ = mapped_size;
  }
  void release() {
    MOZ_ASSERT(is_set_);
    is_set_ = false;
    base_ = nullptr;
    size_ = 0;
  }

 private:
  bool is_set_;
  void* base_;
  size_t size_;
};

// Set NUM_DW_REGNAMES to be the number of Dwarf register names
// appropriate to the machine architecture given in HEADER.  Return
// true on success, or false if HEADER's machine architecture is not
// supported.
template <typename ElfClass>
bool DwarfCFIRegisterNames(const typename ElfClass::Ehdr* elf_header,
                           unsigned int* num_dw_regnames) {
  switch (elf_header->e_machine) {
    case EM_386:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::I386();
      return true;
    case EM_ARM:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::ARM();
      return true;
    case EM_X86_64:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::X86_64();
      return true;
    case EM_MIPS:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::MIPS();
      return true;
    case EM_AARCH64:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::ARM64();
      return true;
    case EM_LOONGARCH:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::LOONGARCH();
      return true;
    case EM_RISCV:
      *num_dw_regnames = DwarfCFIToModule::RegisterNames::RISCV();
      return true;
    default:
      MOZ_ASSERT(0);
      return false;
  }
}

template <typename ElfClass>
bool LoadDwarfCFI(const string& dwarf_filename,
                  const typename ElfClass::Ehdr* elf_header,
                  const char* section_name,
                  const typename ElfClass::Shdr* section, const bool eh_frame,
                  const typename ElfClass::Shdr* got_section,
                  const typename ElfClass::Shdr* text_section,
                  const bool big_endian, SecMap* smap, uintptr_t text_bias,
                  UniqueStringUniverse* usu, void (*log)(const char*)) {
  // Find the appropriate set of register names for this file's
  // architecture.
  unsigned int num_dw_regs = 0;
  if (!DwarfCFIRegisterNames<ElfClass>(elf_header, &num_dw_regs)) {
    fprintf(stderr,
            "%s: unrecognized ELF machine architecture '%d';"
            " cannot convert DWARF call frame information\n",
            dwarf_filename.c_str(), elf_header->e_machine);
    return false;
  }

  const lul::Endianness endianness =
      big_endian ? lul::ENDIANNESS_BIG : lul::ENDIANNESS_LITTLE;

  // Find the call frame information and its size.
  const char* cfi = GetOffset<ElfClass, char>(elf_header, section->sh_offset);
  size_t cfi_size = section->sh_size;

  // Plug together the parser, handler, and their entourages.

  // Here's a summariser, which will receive the output of the
  // parser, create summaries, and add them to |smap|.
  Summariser summ(smap, text_bias, log);

  lul::ByteReader reader(endianness);
  reader.SetAddressSize(ElfClass::kAddrSize);

  DwarfCFIToModule::Reporter module_reporter(log, dwarf_filename, section_name);
  DwarfCFIToModule handler(num_dw_regs, &module_reporter, &reader, usu, &summ);

  // Provide the base addresses for .eh_frame encoded pointers, if
  // possible.
  reader.SetCFIDataBase(section->sh_addr, cfi);
  if (got_section) reader.SetDataBase(got_section->sh_addr);
  if (text_section) reader.SetTextBase(text_section->sh_addr);

  lul::CallFrameInfo::Reporter dwarf_reporter(log, dwarf_filename,
                                              section_name);
  lul::CallFrameInfo parser(cfi, cfi_size, &reader, &handler, &dwarf_reporter,
                            eh_frame);
  parser.Start();

  return true;
}

bool LoadELF(const string& obj_file, MmapWrapper* map_wrapper,
             void** elf_header) {
  int obj_fd = open(obj_file.c_str(), O_RDONLY);
  if (obj_fd < 0) {
    fprintf(stderr, "Failed to open ELF file '%s': %s\n", obj_file.c_str(),
            strerror(errno));
    return false;
  }
  FDWrapper obj_fd_wrapper(obj_fd);
  struct stat st;
  if (fstat(obj_fd, &st) != 0 && st.st_size <= 0) {
    fprintf(stderr, "Unable to fstat ELF file '%s': %s\n", obj_file.c_str(),
            strerror(errno));
    return false;
  }
  // Mapping it read-only is good enough.  In any case, mapping it
  // read-write confuses Valgrind's debuginfo acquire/discard
  // heuristics, making it hard to profile the profiler.
  void* obj_base = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, obj_fd, 0);
  if (obj_base == MAP_FAILED) {
    fprintf(stderr, "Failed to mmap ELF file '%s': %s\n", obj_file.c_str(),
            strerror(errno));
    return false;
  }
  map_wrapper->set(obj_base, st.st_size);
  *elf_header = obj_base;
  if (!IsValidElf(*elf_header)) {
    fprintf(stderr, "Not a valid ELF file: %s\n", obj_file.c_str());
    return false;
  }
  return true;
}

// Get the endianness of ELF_HEADER. If it's invalid, return false.
template <typename ElfClass>
bool ElfEndianness(const typename ElfClass::Ehdr* elf_header,
                   bool* big_endian) {
  if (elf_header->e_ident[EI_DATA] == ELFDATA2LSB) {
    *big_endian = false;
    return true;
  }
  if (elf_header->e_ident[EI_DATA] == ELFDATA2MSB) {
    *big_endian = true;
    return true;
  }

  fprintf(stderr, "bad data encoding in ELF header: %d\n",
          elf_header->e_ident[EI_DATA]);
  return false;
}

//
// LoadSymbolsInfo
//
// Holds the state between the two calls to LoadSymbols() in case it's necessary
// to follow the .gnu_debuglink section and load debug information from a
// different file.
//
template <typename ElfClass>
class LoadSymbolsInfo {
 public:
  typedef typename ElfClass::Addr Addr;

  // Keeps track of which sections have been loaded so sections don't
  // accidentally get loaded twice from two different files.
  void LoadedSection(const string& section) {
    if (loaded_sections_.count(section) == 0) {
      loaded_sections_.insert(section);
    } else {
      fprintf(stderr, "Section %s has already been loaded.\n", section.c_str());
    }
  }

  string debuglink_file() const { return debuglink_file_; }

 private:
  string debuglink_file_;  // Full path to the debug ELF file.

  set<string> loaded_sections_;  // Tracks the Loaded ELF sections
                                 // between calls to LoadSymbols().
};

// Find the preferred loading address of the binary.
template <typename ElfClass>
typename ElfClass::Addr GetLoadingAddress(
    const typename ElfClass::Phdr* program_headers, int nheader) {
  typedef typename ElfClass::Phdr Phdr;

  // For non-PIC executables (e_type == ET_EXEC), the load address is
  // the start address of the first PT_LOAD segment.  (ELF requires
  // the segments to be sorted by load address.)  For PIC executables
  // and dynamic libraries (e_type == ET_DYN), this address will
  // normally be zero.
  for (int i = 0; i < nheader; ++i) {
    const Phdr& header = program_headers[i];
    if (header.p_type == PT_LOAD) return header.p_vaddr;
  }
  return 0;
}

template <typename ElfClass>
bool LoadSymbols(const string& obj_file, const bool big_endian,
                 const typename ElfClass::Ehdr* elf_header,
                 const bool /*read_gnu_debug_link*/,
                 LoadSymbolsInfo<ElfClass>* info, SecMap* smap, void* rx_avma,
                 size_t /*rx_size*/, UniqueStringUniverse* usu,
                 void (*log)(const char*)) {
  typedef typename ElfClass::Phdr Phdr;
  typedef typename ElfClass::Shdr Shdr;

  char buf[500];
  SprintfLiteral(buf, "LoadSymbols: BEGIN   %s\n", obj_file.c_str());
  buf[sizeof(buf) - 1] = 0;
  log(buf);

  // This is how the text bias is calculated.
  // BEGIN CALCULATE BIAS
  uintptr_t loading_addr = GetLoadingAddress<ElfClass>(
      GetOffset<ElfClass, Phdr>(elf_header, elf_header->e_phoff),
      elf_header->e_phnum);
  uintptr_t text_bias = ((uintptr_t)rx_avma) - loading_addr;
  SprintfLiteral(buf, "LoadSymbols:   rx_avma=%llx, text_bias=%llx",
                 (unsigned long long int)(uintptr_t)rx_avma,
                 (unsigned long long int)text_bias);
  buf[sizeof(buf) - 1] = 0;
  log(buf);
  // END CALCULATE BIAS

  const Shdr* sections =
      GetOffset<ElfClass, Shdr>(elf_header, elf_header->e_shoff);
  const Shdr* section_names = sections + elf_header->e_shstrndx;
  const char* names =
      GetOffset<ElfClass, char>(elf_header, section_names->sh_offset);
  const char* names_end = names + section_names->sh_size;
  bool found_usable_info = false;

  // Dwarf Call Frame Information (CFI) is actually independent from
  // the other DWARF debugging information, and can be used alone.
  const Shdr* dwarf_cfi_section =
      FindElfSectionByName<ElfClass>(".debug_frame", SHT_PROGBITS, sections,
                                     names, names_end, elf_header->e_shnum);
  if (dwarf_cfi_section) {
    // Ignore the return value of this function; even without call frame
    // information, the other debugging information could be perfectly
    // useful.
    info->LoadedSection(".debug_frame");
    bool result = LoadDwarfCFI<ElfClass>(
        obj_file, elf_header, ".debug_frame", dwarf_cfi_section, false, nullptr,
        nullptr, big_endian, smap, text_bias, usu, log);
    found_usable_info = found_usable_info || result;
    if (result) log("LoadSymbols:   read CFI from .debug_frame");
  }

  // Linux C++ exception handling information can also provide
  // unwinding data.
  const Shdr* eh_frame_section =
      FindElfSectionByName<ElfClass>(".eh_frame", SHT_PROGBITS, sections, names,
                                     names_end, elf_header->e_shnum);
#if defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_amd64_android)
  if (!eh_frame_section) {
    // Possibly depending on which linker created libxul.so, on x86_64-linux
    // and -android, .eh_frame may instead have the SHT_X86_64_UNWIND type.
    eh_frame_section =
        FindElfSectionByName<ElfClass>(".eh_frame", SHT_X86_64_UNWIND, sections,
                                       names, names_end, elf_header->e_shnum);
  }
#endif
  if (eh_frame_section) {
    // Pointers in .eh_frame data may be relative to the base addresses of
    // certain sections. Provide those sections if present.
    const Shdr* got_section = FindElfSectionByName<ElfClass>(
        ".got", SHT_PROGBITS, sections, names, names_end, elf_header->e_shnum);
    const Shdr* text_section = FindElfSectionByName<ElfClass>(
        ".text", SHT_PROGBITS, sections, names, names_end, elf_header->e_shnum);
    info->LoadedSection(".eh_frame");
    // As above, ignore the return value of this function.
    bool result = LoadDwarfCFI<ElfClass>(
        obj_file, elf_header, ".eh_frame", eh_frame_section, true, got_section,
        text_section, big_endian, smap, text_bias, usu, log);
    found_usable_info = found_usable_info || result;
    if (result) log("LoadSymbols:   read CFI from .eh_frame");
  }

  SprintfLiteral(buf, "LoadSymbols: END     %s\n", obj_file.c_str());
  buf[sizeof(buf) - 1] = 0;
  log(buf);

  return found_usable_info;
}

// Return the breakpad symbol file identifier for the architecture of
// ELF_HEADER.
template <typename ElfClass>
const char* ElfArchitecture(const typename ElfClass::Ehdr* elf_header) {
  typedef typename ElfClass::Half Half;
  Half arch = elf_header->e_machine;
  switch (arch) {
    case EM_386:
      return "x86";
    case EM_ARM:
      return "arm";
    case EM_AARCH64:
      return "arm64";
    case EM_MIPS:
      return "mips";
    case EM_PPC64:
      return "ppc64";
    case EM_PPC:
      return "ppc";
    case EM_S390:
      return "s390";
    case EM_SPARC:
      return "sparc";
    case EM_SPARCV9:
      return "sparcv9";
    case EM_X86_64:
      return "x86_64";
    case EM_LOONGARCH:
      return "loongarch";
    case EM_RISCV:
      return "riscv";
    default:
      return nullptr;
  }
}

// Format the Elf file identifier in IDENTIFIER as a UUID with the
// dashes removed.
string FormatIdentifier(unsigned char identifier[16]) {
  char identifier_str[40];
  lul::FileID::ConvertIdentifierToString(identifier, identifier_str,
                                         sizeof(identifier_str));
  string id_no_dash;
  for (int i = 0; identifier_str[i] != '\0'; ++i)
    if (identifier_str[i] != '-') id_no_dash += identifier_str[i];
  // Add an extra "0" by the end.  PDB files on Windows have an 'age'
  // number appended to the end of the file identifier; this isn't
  // really used or necessary on other platforms, but be consistent.
  id_no_dash += '0';
  return id_no_dash;
}

// Return the non-directory portion of FILENAME: the portion after the
// last slash, or the whole filename if there are no slashes.
string BaseFileName(const string& filename) {
  // Lots of copies!  basename's behavior is less than ideal.
  char* c_filename = strdup(filename.c_str());
  string base = basename(c_filename);
  free(c_filename);
  return base;
}

template <typename ElfClass>
bool ReadSymbolDataElfClass(const typename ElfClass::Ehdr* elf_header,
                            const string& obj_filename,
                            const vector<string>& debug_dirs, SecMap* smap,
                            void* rx_avma, size_t rx_size,
                            UniqueStringUniverse* usu,
                            void (*log)(const char*)) {
  typedef typename ElfClass::Ehdr Ehdr;

  unsigned char identifier[16];
  if (!lul ::FileID::ElfFileIdentifierFromMappedFile(elf_header, identifier)) {
    fprintf(stderr, "%s: unable to generate file identifier\n",
            obj_filename.c_str());
    return false;
  }

  const char* architecture = ElfArchitecture<ElfClass>(elf_header);
  if (!architecture) {
    fprintf(stderr, "%s: unrecognized ELF machine architecture: %d\n",
            obj_filename.c_str(), elf_header->e_machine);
    return false;
  }

  // Figure out what endianness this file is.
  bool big_endian;
  if (!ElfEndianness<ElfClass>(elf_header, &big_endian)) return false;

  string name = BaseFileName(obj_filename);
  string os = "Linux";
  string id = FormatIdentifier(identifier);

  LoadSymbolsInfo<ElfClass> info;
  if (!LoadSymbols<ElfClass>(obj_filename, big_endian, elf_header,
                             !debug_dirs.empty(), &info, smap, rx_avma, rx_size,
                             usu, log)) {
    const string debuglink_file = info.debuglink_file();
    if (debuglink_file.empty()) return false;

    // Load debuglink ELF file.
    fprintf(stderr, "Found debugging info in %s\n", debuglink_file.c_str());
    MmapWrapper debug_map_wrapper;
    Ehdr* debug_elf_header = nullptr;
    if (!LoadELF(debuglink_file, &debug_map_wrapper,
                 reinterpret_cast<void**>(&debug_elf_header)))
      return false;
    // Sanity checks to make sure everything matches up.
    const char* debug_architecture =
        ElfArchitecture<ElfClass>(debug_elf_header);
    if (!debug_architecture) {
      fprintf(stderr, "%s: unrecognized ELF machine architecture: %d\n",
              debuglink_file.c_str(), debug_elf_header->e_machine);
      return false;
    }
    if (strcmp(architecture, debug_architecture)) {
      fprintf(stderr,
              "%s with ELF machine architecture %s does not match "
              "%s with ELF architecture %s\n",
              debuglink_file.c_str(), debug_architecture, obj_filename.c_str(),
              architecture);
      return false;
    }

    bool debug_big_endian;
    if (!ElfEndianness<ElfClass>(debug_elf_header, &debug_big_endian))
      return false;
    if (debug_big_endian != big_endian) {
      fprintf(stderr, "%s and %s does not match in endianness\n",
              obj_filename.c_str(), debuglink_file.c_str());
      return false;
    }

    if (!LoadSymbols<ElfClass>(debuglink_file, debug_big_endian,
                               debug_elf_header, false, &info, smap, rx_avma,
                               rx_size, usu, log)) {
      return false;
    }
  }

  return true;
}

}  // namespace

namespace lul {

bool ReadSymbolDataInternal(const uint8_t* obj_file, const string& obj_filename,
                            const vector<string>& debug_dirs, SecMap* smap,
                            void* rx_avma, size_t rx_size,
                            UniqueStringUniverse* usu,
                            void (*log)(const char*)) {
  if (!IsValidElf(obj_file)) {
    fprintf(stderr, "Not a valid ELF file: %s\n", obj_filename.c_str());
    return false;
  }

  int elfclass = ElfClass(obj_file);
  if (elfclass == ELFCLASS32) {
    return ReadSymbolDataElfClass<ElfClass32>(
        reinterpret_cast<const Elf32_Ehdr*>(obj_file), obj_filename, debug_dirs,
        smap, rx_avma, rx_size, usu, log);
  }
  if (elfclass == ELFCLASS64) {
    return ReadSymbolDataElfClass<ElfClass64>(
        reinterpret_cast<const Elf64_Ehdr*>(obj_file), obj_filename, debug_dirs,
        smap, rx_avma, rx_size, usu, log);
  }

  return false;
}

bool ReadSymbolData(const string& obj_file, const vector<string>& debug_dirs,
                    SecMap* smap, void* rx_avma, size_t rx_size,
                    UniqueStringUniverse* usu, void (*log)(const char*)) {
  MmapWrapper map_wrapper;
  void* elf_header = nullptr;
  if (!LoadELF(obj_file, &map_wrapper, &elf_header)) return false;

  return ReadSymbolDataInternal(reinterpret_cast<uint8_t*>(elf_header),
                                obj_file, debug_dirs, smap, rx_avma, rx_size,
                                usu, log);
}

namespace {

template <typename ElfClass>
void FindElfClassSection(const char* elf_base, const char* section_name,
                         typename ElfClass::Word section_type,
                         const void** section_start, int* section_size) {
  typedef typename ElfClass::Ehdr Ehdr;
  typedef typename ElfClass::Shdr Shdr;

  MOZ_ASSERT(elf_base);
  MOZ_ASSERT(section_start);
  MOZ_ASSERT(section_size);

  MOZ_ASSERT(strncmp(elf_base, ELFMAG, SELFMAG) == 0);

  const Ehdr* elf_header = reinterpret_cast<const Ehdr*>(elf_base);
  MOZ_ASSERT(elf_header->e_ident[EI_CLASS] == ElfClass::kClass);

  const Shdr* sections =
      GetOffset<ElfClass, Shdr>(elf_header, elf_header->e_shoff);
  const Shdr* section_names = sections + elf_header->e_shstrndx;
  const char* names =
      GetOffset<ElfClass, char>(elf_header, section_names->sh_offset);
  const char* names_end = names + section_names->sh_size;

  const Shdr* section =
      FindElfSectionByName<ElfClass>(section_name, section_type, sections,
                                     names, names_end, elf_header->e_shnum);

  if (section != nullptr && section->sh_size > 0) {
    *section_start = elf_base + section->sh_offset;
    *section_size = section->sh_size;
  }
}

template <typename ElfClass>
void FindElfClassSegment(const char* elf_base,
                         typename ElfClass::Word segment_type,
                         const void** segment_start, int* segment_size) {
  typedef typename ElfClass::Ehdr Ehdr;
  typedef typename ElfClass::Phdr Phdr;

  MOZ_ASSERT(elf_base);
  MOZ_ASSERT(segment_start);
  MOZ_ASSERT(segment_size);

  MOZ_ASSERT(strncmp(elf_base, ELFMAG, SELFMAG) == 0);

  const Ehdr* elf_header = reinterpret_cast<const Ehdr*>(elf_base);
  MOZ_ASSERT(elf_header->e_ident[EI_CLASS] == ElfClass::kClass);

  const Phdr* phdrs =
      GetOffset<ElfClass, Phdr>(elf_header, elf_header->e_phoff);

  for (int i = 0; i < elf_header->e_phnum; ++i) {
    if (phdrs[i].p_type == segment_type) {
      *segment_start = elf_base + phdrs[i].p_offset;
      *segment_size = phdrs[i].p_filesz;
      return;
    }
  }
}

}  // namespace

bool IsValidElf(const void* elf_base) {
  return strncmp(reinterpret_cast<const char*>(elf_base), ELFMAG, SELFMAG) == 0;
}

int ElfClass(const void* elf_base) {
  const ElfW(Ehdr)* elf_header = reinterpret_cast<const ElfW(Ehdr)*>(elf_base);

  return elf_header->e_ident[EI_CLASS];
}

bool FindElfSection(const void* elf_mapped_base, const char* section_name,
                    uint32_t section_type, const void** section_start,
                    int* section_size, int* elfclass) {
  MOZ_ASSERT(elf_mapped_base);
  MOZ_ASSERT(section_start);
  MOZ_ASSERT(section_size);

  *section_start = nullptr;
  *section_size = 0;

  if (!IsValidElf(elf_mapped_base)) return false;

  int cls = ElfClass(elf_mapped_base);
  if (elfclass) {
    *elfclass = cls;
  }

  const char* elf_base = static_cast<const char*>(elf_mapped_base);

  if (cls == ELFCLASS32) {
    FindElfClassSection<ElfClass32>(elf_base, section_name, section_type,
                                    section_start, section_size);
    return *section_start != nullptr;
  } else if (cls == ELFCLASS64) {
    FindElfClassSection<ElfClass64>(elf_base, section_name, section_type,
                                    section_start, section_size);
    return *section_start != nullptr;
  }

  return false;
}

bool FindElfSegment(const void* elf_mapped_base, uint32_t segment_type,
                    const void** segment_start, int* segment_size,
                    int* elfclass) {
  MOZ_ASSERT(elf_mapped_base);
  MOZ_ASSERT(segment_start);
  MOZ_ASSERT(segment_size);

  *segment_start = nullptr;
  *segment_size = 0;

  if (!IsValidElf(elf_mapped_base)) return false;

  int cls = ElfClass(elf_mapped_base);
  if (elfclass) {
    *elfclass = cls;
  }

  const char* elf_base = static_cast<const char*>(elf_mapped_base);

  if (cls == ELFCLASS32) {
    FindElfClassSegment<ElfClass32>(elf_base, segment_type, segment_start,
                                    segment_size);
    return *segment_start != nullptr;
  } else if (cls == ELFCLASS64) {
    FindElfClassSegment<ElfClass64>(elf_base, segment_type, segment_start,
                                    segment_size);
    return *segment_start != nullptr;
  }

  return false;
}

// (derived from)
// file_id.cc: Return a unique identifier for a file
//
// See file_id.h for documentation
//

// ELF note name and desc are 32-bits word padded.
#define NOTE_PADDING(a) ((a + 3) & ~3)

// These functions are also used inside the crashed process, so be safe
// and use the syscall/libc wrappers instead of direct syscalls or libc.

template <typename ElfClass>
static bool ElfClassBuildIDNoteIdentifier(const void* section, int length,
                                          uint8_t identifier[kMDGUIDSize]) {
  typedef typename ElfClass::Nhdr Nhdr;

  const void* section_end = reinterpret_cast<const char*>(section) + length;
  const Nhdr* note_header = reinterpret_cast<const Nhdr*>(section);
  while (reinterpret_cast<const void*>(note_header) < section_end) {
    if (note_header->n_type == NT_GNU_BUILD_ID) break;
    note_header = reinterpret_cast<const Nhdr*>(
        reinterpret_cast<const char*>(note_header) + sizeof(Nhdr) +
        NOTE_PADDING(note_header->n_namesz) +
        NOTE_PADDING(note_header->n_descsz));
  }
  if (reinterpret_cast<const void*>(note_header) >= section_end ||
      note_header->n_descsz == 0) {
    return false;
  }

  const char* build_id = reinterpret_cast<const char*>(note_header) +
                         sizeof(Nhdr) + NOTE_PADDING(note_header->n_namesz);
  // Copy as many bits of the build ID as will fit
  // into the GUID space.
  memset(identifier, 0, kMDGUIDSize);
  memcpy(identifier, build_id,
         std::min(kMDGUIDSize, (size_t)note_header->n_descsz));

  return true;
}

// Attempt to locate a .note.gnu.build-id section in an ELF binary
// and copy as many bytes of it as will fit into |identifier|.
static bool FindElfBuildIDNote(const void* elf_mapped_base,
                               uint8_t identifier[kMDGUIDSize]) {
  void* note_section;
  int note_size, elfclass;
  if ((!FindElfSegment(elf_mapped_base, PT_NOTE, (const void**)&note_section,
                       &note_size, &elfclass) ||
       note_size == 0) &&
      (!FindElfSection(elf_mapped_base, ".note.gnu.build-id", SHT_NOTE,
                       (const void**)&note_section, &note_size, &elfclass) ||
       note_size == 0)) {
    return false;
  }

  if (elfclass == ELFCLASS32) {
    return ElfClassBuildIDNoteIdentifier<ElfClass32>(note_section, note_size,
                                                     identifier);
  } else if (elfclass == ELFCLASS64) {
    return ElfClassBuildIDNoteIdentifier<ElfClass64>(note_section, note_size,
                                                     identifier);
  }

  return false;
}

// Attempt to locate the .text section of an ELF binary and generate
// a simple hash by XORing the first page worth of bytes into |identifier|.
static bool HashElfTextSection(const void* elf_mapped_base,
                               uint8_t identifier[kMDGUIDSize]) {
  void* text_section;
  int text_size;
  if (!FindElfSection(elf_mapped_base, ".text", SHT_PROGBITS,
                      (const void**)&text_section, &text_size, nullptr) ||
      text_size == 0) {
    return false;
  }

  memset(identifier, 0, kMDGUIDSize);
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text_section);
  const uint8_t* ptr_end = ptr + std::min(text_size, 4096);
  while (ptr < ptr_end) {
    for (unsigned i = 0; i < kMDGUIDSize; i++) identifier[i] ^= ptr[i];
    ptr += kMDGUIDSize;
  }
  return true;
}

// static
bool FileID::ElfFileIdentifierFromMappedFile(const void* base,
                                             uint8_t identifier[kMDGUIDSize]) {
  // Look for a build id note first.
  if (FindElfBuildIDNote(base, identifier)) return true;

  // Fall back on hashing the first page of the text section.
  return HashElfTextSection(base, identifier);
}

// static
void FileID::ConvertIdentifierToString(const uint8_t identifier[kMDGUIDSize],
                                       char* buffer, int buffer_length) {
  uint8_t identifier_swapped[kMDGUIDSize];

  // Endian-ness swap to match dump processor expectation.
  memcpy(identifier_swapped, identifier, kMDGUIDSize);
  uint32_t* data1 = reinterpret_cast<uint32_t*>(identifier_swapped);
  *data1 = htonl(*data1);
  uint16_t* data2 = reinterpret_cast<uint16_t*>(identifier_swapped + 4);
  *data2 = htons(*data2);
  uint16_t* data3 = reinterpret_cast<uint16_t*>(identifier_swapped + 6);
  *data3 = htons(*data3);

  int buffer_idx = 0;
  for (unsigned int idx = 0;
       (buffer_idx < buffer_length) && (idx < kMDGUIDSize); ++idx) {
    int hi = (identifier_swapped[idx] >> 4) & 0x0F;
    int lo = (identifier_swapped[idx]) & 0x0F;

    if (idx == 4 || idx == 6 || idx == 8 || idx == 10)
      buffer[buffer_idx++] = '-';

    buffer[buffer_idx++] = (hi >= 10) ? 'A' + hi - 10 : '0' + hi;
    buffer[buffer_idx++] = (lo >= 10) ? 'A' + lo - 10 : '0' + lo;
  }

  // NULL terminate
  buffer[(buffer_idx < buffer_length) ? buffer_idx : buffer_idx - 1] = 0;
}

}  // namespace lul
