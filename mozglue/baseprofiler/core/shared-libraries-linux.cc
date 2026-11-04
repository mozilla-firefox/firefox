/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/SharedLibraries.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include "mozilla/Sprintf.h"

#if defined(GP_OS_android)
// Only the Android /proc/<pid>/maps scan needs these.
#  define PATH_MAX_TOSTRING(x) #x
#  define PATH_MAX_STRING(x) PATH_MAX_TOSTRING(x)
#  include <fstream>
#  include "platform.h"
#endif

#if defined(GP_OS_linux)
#  include <sys/auxv.h>
#endif

#include <algorithm>
#include <arpa/inet.h>
#include <elf.h>
#include <fcntl.h>
#if defined(GP_OS_linux) || defined(GP_OS_android)
#  include <features.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <optional>

#if defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
#  include <link.h>  // dl_phdr_info, ElfW()
#else
#  error "Unexpected configuration"
#endif

#if defined(GP_OS_android)
extern "C" MOZ_EXPORT __attribute__((weak)) int dl_iterate_phdr(
    int (*callback)(struct dl_phdr_info* info, size_t size, void* data),
    void* data);
#endif

#if defined(GP_OS_freebsd) && !defined(ElfW)
#  define ElfW(type) Elf_##type
#endif

// ----------------------------------------------------------------------------
// Starting imports from toolkit/crashreporter/google-breakpad/, as needed by
// this file when moved to mozglue.

// Imported from
// toolkit/crashreporter/google-breakpad/src/common/memory_range.h.
// A lightweight wrapper with a pointer and a length to encapsulate a contiguous
// range of memory. It provides helper methods for checked access of a subrange
// of the memory. Its implemementation does not allocate memory or call into
// libc functions, and is thus safer to use in a crashed environment.
class MemoryRange {
 public:
  MemoryRange() : data_(nullptr), length_(0) {}

  MemoryRange(const void* data, size_t length) { Set(data, length); }

  // Returns true if this memory range contains no data.
  bool IsEmpty() const {
    // Set() guarantees that |length_| is zero if |data_| is NULL.
    return length_ == 0;
  }

  // Resets to an empty range.
  void Reset() {
    data_ = nullptr;
    length_ = 0;
  }

  // Sets this memory range to point to |data| and its length to |length|.
  void Set(const void* data, size_t length) {
    data_ = reinterpret_cast<const uint8_t*>(data);
    // Always set |length_| to zero if |data_| is NULL.
    length_ = data ? length : 0;
  }

  // Returns true if this range covers a subrange of |sub_length| bytes
  // at |sub_offset| bytes of this memory range, or false otherwise.
  bool Covers(size_t sub_offset, size_t sub_length) const {
    // The following checks verify that:
    // 1. sub_offset is within [ 0 .. length_ - 1 ]
    // 2. sub_offset + sub_length is within
    //    [ sub_offset .. length_ ]
    return sub_offset < length_ && sub_offset + sub_length >= sub_offset &&
           sub_offset + sub_length <= length_;
  }

  // Returns a raw data pointer to a subrange of |sub_length| bytes at
  // |sub_offset| bytes of this memory range, or NULL if the subrange
  // is out of bounds.
  const void* GetData(size_t sub_offset, size_t sub_length) const {
    return Covers(sub_offset, sub_length) ? (data_ + sub_offset) : nullptr;
  }

  // Same as the two-argument version of GetData() but uses sizeof(DataType)
  // as the subrange length and returns an |DataType| pointer for convenience.
  template <typename DataType>
  const DataType* GetData(size_t sub_offset) const {
    return reinterpret_cast<const DataType*>(
        GetData(sub_offset, sizeof(DataType)));
  }

  // Returns a raw pointer to the |element_index|-th element of an array
  // of elements of length |element_size| starting at |sub_offset| bytes
  // of this memory range, or NULL if the element is out of bounds.
  const void* GetArrayElement(size_t element_offset, size_t element_size,
                              unsigned element_index) const {
    size_t sub_offset = element_offset + element_index * element_size;
    return GetData(sub_offset, element_size);
  }

  // Same as the three-argument version of GetArrayElement() but deduces
  // the element size using sizeof(ElementType) and returns an |ElementType|
  // pointer for convenience.
  template <typename ElementType>
  const ElementType* GetArrayElement(size_t element_offset,
                                     unsigned element_index) const {
    return reinterpret_cast<const ElementType*>(
        GetArrayElement(element_offset, sizeof(ElementType), element_index));
  }

  // Returns a subrange of |sub_length| bytes at |sub_offset| bytes of
  // this memory range, or an empty range if the subrange is out of bounds.
  MemoryRange Subrange(size_t sub_offset, size_t sub_length) const {
    return Covers(sub_offset, sub_length)
               ? MemoryRange(data_ + sub_offset, sub_length)
               : MemoryRange();
  }

  // Returns a pointer to the beginning of this memory range.
  const uint8_t* data() const { return data_; }

  // Returns the length, in bytes, of this memory range.
  size_t length() const { return length_; }

 private:
  // Pointer to the beginning of this memory range.
  const uint8_t* data_;

  // Length, in bytes, of this memory range.
  size_t length_;
};

// Imported from
// toolkit/crashreporter/google-breakpad/src/common/linux/memory_mapped_file.h
// and inlined .cc.
// A utility class for mapping a file into memory for read-only access of the
// file content. Its implementation avoids calling into libc functions by
// directly making system calls for open, close, mmap, and munmap.
class MemoryMappedFile {
 public:
  MemoryMappedFile() = default;

  // Constructor that calls Map() to map a file at |path| into memory.
  // If Map() fails, the object behaves as if it is default constructed.
  MemoryMappedFile(const char* path, size_t offset) { Map(path, offset); }

  MemoryMappedFile(const MemoryMappedFile&) = delete;
  MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

  ~MemoryMappedFile() { Unmap(); }

  // Maps a file at |path| into memory, which can then be accessed via
  // content() as a MemoryRange object or via data(), and returns true on
  // success. Mapping an empty file will succeed but with data() and size()
  // returning NULL and 0, respectively. An existing mapping is unmapped
  // before a new mapping is created.
  bool Map(const char* path, size_t offset) {
    Unmap();

    int fd = open(path, O_RDONLY, 0);
    if (fd == -1) {
      return false;
    }

#if defined(__x86_64__) || defined(__aarch64__) ||  \
    (defined(__mips__) && _MIPS_SIM == _ABI64) ||   \
    (defined(__loongarch__) && __loongarch_lp64) || \
    (defined(__riscv) && (__riscv_xlen == 64)) ||   \
    !(defined(GP_OS_linux) || defined(GP_OS_android))

    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size < 0) {
#else
    struct stat64 st;
    if (fstat64(fd, &st) == -1 || st.st_size < 0) {
#endif
      close(fd);
      return false;
    }

    // Strangely file size can be negative, but we check above that it is not.
    size_t file_len = static_cast<size_t>(st.st_size);
    // If the file does not extend beyond the offset, simply use an empty
    // MemoryRange and return true. Don't bother to call mmap()
    // even though mmap() can handle an empty file on some platforms.
    if (offset >= file_len) {
      close(fd);
      return true;
    }

    void* data = mmap(nullptr, file_len, PROT_READ, MAP_PRIVATE, fd, offset);
    close(fd);
    if (data == MAP_FAILED) {
      return false;
    }

    content_.Set(data, file_len - offset);
    return true;
  }

  // Unmaps the memory for the mapped file. It's a no-op if no file is
  // mapped.
  void Unmap() {
    if (content_.data()) {
      munmap(const_cast<uint8_t*>(content_.data()), content_.length());
      content_.Set(nullptr, 0);
    }
  }

  // Returns a MemoryRange object that covers the memory for the mapped
  // file. The MemoryRange object is empty if no file is mapped.
  const MemoryRange& content() const { return content_; }

  // Returns a pointer to the beginning of the memory for the mapped file.
  // or NULL if no file is mapped or the mapped file is empty.
  const void* data() const { return content_.data(); }

  // Returns the size in bytes of the mapped file, or zero if no file
  // is mapped.
  size_t size() const { return content_.length(); }

 private:
  // Mapped file content as a MemoryRange object.
  MemoryRange content_;
};

// Imported from
// toolkit/crashreporter/google-breakpad/src/common/linux/file_id.h and inlined
// .cc.
// GNU binutils' ld defaults to 'sha1', which is 160 bits == 20 bytes,
// so this is enough to fit that, which most binaries will use.
// This is just a sensible default for vectors so most callers can get away with
// stack allocation.
static const size_t kDefaultBuildIdSize = 20;

// Used in a few places for backwards-compatibility.
typedef struct {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} MDGUID; /* GUID */

const size_t kMDGUIDSize = sizeof(MDGUID);

class FileID {
 public:
  explicit FileID(const std::string& path) : path_(path) {}
  ~FileID() = default;

  // Load the identifier for the elf file path specified in the constructor into
  // |identifier|.
  //
  // The current implementation will look for a .note.gnu.build-id
  // section and use that as the file id, otherwise it falls back to
  // XORing the first 4096 bytes of the .text section to generate an identifier.
  bool ElfFileIdentifier(std::vector<uint8_t>& identifier) {
    MemoryMappedFile mapped_file(path_.c_str(), 0);
    if (!mapped_file.data())  // Should probably check if size >= ElfW(Ehdr)?
      return false;

    return ElfFileIdentifierFromMappedFile(mapped_file.data(), identifier);
  }

  // Traits classes so consumers can write templatized code to deal
  // with specific ELF bits.
  struct ElfClass32 {
    typedef Elf32_Addr Addr;
    typedef Elf32_Ehdr Ehdr;
    typedef Elf32_Nhdr Nhdr;
    typedef Elf32_Phdr Phdr;
    typedef Elf32_Shdr Shdr;
    typedef Elf32_Half Half;
    typedef Elf32_Off Off;
    typedef Elf32_Sym Sym;
    typedef Elf32_Word Word;

    static const int kClass = ELFCLASS32;
    static const uint16_t kMachine = EM_386;
    static const size_t kAddrSize = sizeof(Elf32_Addr);
    static constexpr const char* kMachineName = "x86";
  };

  struct ElfClass64 {
    typedef Elf64_Addr Addr;
    typedef Elf64_Ehdr Ehdr;
    typedef Elf64_Nhdr Nhdr;
    typedef Elf64_Phdr Phdr;
    typedef Elf64_Shdr Shdr;
    typedef Elf64_Half Half;
    typedef Elf64_Off Off;
    typedef Elf64_Sym Sym;
    typedef Elf64_Word Word;

    static const int kClass = ELFCLASS64;
    static const uint16_t kMachine = EM_X86_64;
    static const size_t kAddrSize = sizeof(Elf64_Addr);
    static constexpr const char* kMachineName = "x86_64";
  };

  // Internal helper method, exposed for convenience for callers
  // that already have more info.
  template <typename ElfClass>
  static const typename ElfClass::Shdr* FindElfSectionByName(
      const char* name, typename ElfClass::Word section_type,
      const typename ElfClass::Shdr* sections, const char* section_names,
      const char* names_end, int nsection) {
    if (!name || !sections || nsection == 0) {
      return nullptr;
    }

    int name_len = strlen(name);
    if (name_len == 0) return nullptr;

    for (int i = 0; i < nsection; ++i) {
      const char* section_name = section_names + sections[i].sh_name;
      if (sections[i].sh_type == section_type &&
          names_end - section_name >= name_len + 1 &&
          strcmp(name, section_name) == 0) {
        return sections + i;
      }
    }
    return nullptr;
  }

  struct ElfSegment {
    const void* start;
    size_t size;
  };

  // Convert an offset from an Elf header into a pointer to the mapped
  // address in the current process. Takes an extra template parameter
  // to specify the return type to avoid having to dynamic_cast the
  // result.
  template <typename ElfClass, typename T>
  static const T* GetOffset(const typename ElfClass::Ehdr* elf_header,
                            typename ElfClass::Off offset) {
    return reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(elf_header) +
                                      offset);
  }

// ELF note name and desc are 32-bits word padded.
#define NOTE_PADDING(a) ((a + 3) & ~3)

  static bool ElfClassBuildIDNoteIdentifier(const void* section, size_t length,
                                            std::vector<uint8_t>& identifier) {
    static_assert(sizeof(ElfClass32::Nhdr) == sizeof(ElfClass64::Nhdr),
                  "Elf32_Nhdr and Elf64_Nhdr should be the same");
    typedef typename ElfClass32::Nhdr Nhdr;

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

    const uint8_t* build_id = reinterpret_cast<const uint8_t*>(note_header) +
                              sizeof(Nhdr) +
                              NOTE_PADDING(note_header->n_namesz);
    identifier.insert(identifier.end(), build_id,
                      build_id + note_header->n_descsz);

    return true;
  }

  template <typename ElfClass>
  static bool FindElfClassSection(const char* elf_base,
                                  const char* section_name,
                                  typename ElfClass::Word section_type,
                                  const void** section_start,
                                  size_t* section_size) {
    typedef typename ElfClass::Ehdr Ehdr;
    typedef typename ElfClass::Shdr Shdr;

    if (!elf_base || !section_start || !section_size) {
      return false;
    }

    if (strncmp(elf_base, ELFMAG, SELFMAG) != 0) {
      return false;
    }

    const Ehdr* elf_header = reinterpret_cast<const Ehdr*>(elf_base);
    if (elf_header->e_ident[EI_CLASS] != ElfClass::kClass) {
      return false;
    }

    if (elf_header->e_shoff == 0) {
      *section_start = nullptr;
      *section_size = 0;
      return false;
    }

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

    return true;
  }

  template <typename ElfClass>
  static bool FindElfClassSegment(const char* elf_base,
                                  typename ElfClass::Word segment_type,
                                  std::vector<ElfSegment>* segments) {
    typedef typename ElfClass::Ehdr Ehdr;
    typedef typename ElfClass::Phdr Phdr;

    if (!elf_base || !segments) {
      return false;
    }

    if (strncmp(elf_base, ELFMAG, SELFMAG) != 0) {
      return false;
    }

    const Ehdr* elf_header = reinterpret_cast<const Ehdr*>(elf_base);
    if (elf_header->e_ident[EI_CLASS] != ElfClass::kClass) {
      return false;
    }

    const Phdr* phdrs =
        GetOffset<ElfClass, Phdr>(elf_header, elf_header->e_phoff);

    for (int i = 0; i < elf_header->e_phnum; ++i) {
      if (phdrs[i].p_type == segment_type) {
        ElfSegment seg = {};
        seg.start = elf_base + phdrs[i].p_offset;
        seg.size = phdrs[i].p_filesz;
        segments->push_back(seg);
      }
    }

    return true;
  }

  static bool IsValidElf(const void* elf_base) {
    return strncmp(reinterpret_cast<const char*>(elf_base), ELFMAG, SELFMAG) ==
           0;
  }

  static int ElfClass(const void* elf_base) {
    const ElfW(Ehdr)* elf_header =
        reinterpret_cast<const ElfW(Ehdr)*>(elf_base);

    return elf_header->e_ident[EI_CLASS];
  }

  static bool FindElfSection(const void* elf_mapped_base,
                             const char* section_name, uint32_t section_type,
                             const void** section_start, size_t* section_size) {
    if (!elf_mapped_base || !section_start || !section_size) {
      return false;
    }

    *section_start = nullptr;
    *section_size = 0;

    if (!IsValidElf(elf_mapped_base)) return false;

    int cls = ElfClass(elf_mapped_base);
    const char* elf_base = static_cast<const char*>(elf_mapped_base);

    if (cls == ELFCLASS32) {
      return FindElfClassSection<ElfClass32>(elf_base, section_name,
                                             section_type, section_start,
                                             section_size) &&
             *section_start != nullptr;
    } else if (cls == ELFCLASS64) {
      return FindElfClassSection<ElfClass64>(elf_base, section_name,
                                             section_type, section_start,
                                             section_size) &&
             *section_start != nullptr;
    }

    return false;
  }

  static bool FindElfSegments(const void* elf_mapped_base,
                              uint32_t segment_type,
                              std::vector<ElfSegment>* segments) {
    if (!elf_mapped_base || !segments) {
      return false;
    }

    if (!IsValidElf(elf_mapped_base)) return false;

    int cls = ElfClass(elf_mapped_base);
    const char* elf_base = static_cast<const char*>(elf_mapped_base);

    if (cls == ELFCLASS32) {
      return FindElfClassSegment<ElfClass32>(elf_base, segment_type, segments);
    }
    if (cls == ELFCLASS64) {
      return FindElfClassSegment<ElfClass64>(elf_base, segment_type, segments);
    }

    return false;
  }

  // Attempt to locate a .note.gnu.build-id section in an ELF binary
  // and copy it into |identifier|.
  static bool FindElfBuildIDNote(const void* elf_mapped_base,
                                 std::vector<uint8_t>& identifier) {
    // lld normally creates 2 PT_NOTEs, gold normally creates 1.
    std::vector<ElfSegment> segs;
    if (FindElfSegments(elf_mapped_base, PT_NOTE, &segs)) {
      for (ElfSegment& seg : segs) {
        if (ElfClassBuildIDNoteIdentifier(seg.start, seg.size, identifier)) {
          return true;
        }
      }
    }

    void* note_section;
    size_t note_size;
    if (FindElfSection(elf_mapped_base, ".note.gnu.build-id", SHT_NOTE,
                       (const void**)&note_section, &note_size)) {
      return ElfClassBuildIDNoteIdentifier(note_section, note_size, identifier);
    }

    return false;
  }

  // Attempt to locate the .text section of an ELF binary and generate
  // a simple hash by XORing the first page worth of bytes into |identifier|.
  static bool HashElfTextSection(const void* elf_mapped_base,
                                 std::vector<uint8_t>& identifier) {
    void* text_section;
    size_t text_size;
    if (!FindElfSection(elf_mapped_base, ".text", SHT_PROGBITS,
                        (const void**)&text_section, &text_size) ||
        text_size == 0) {
      return false;
    }

    // Only provide |kMDGUIDSize| bytes to keep identifiers produced by this
    // function backwards-compatible.
    identifier.resize(kMDGUIDSize);
    memset(&identifier[0], 0, kMDGUIDSize);
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text_section);
    const uint8_t* ptr_end =
        ptr + std::min(text_size, static_cast<size_t>(4096));
    while (ptr < ptr_end) {
      for (unsigned i = 0; i < kMDGUIDSize; i++) identifier[i] ^= ptr[i];
      ptr += kMDGUIDSize;
    }
    return true;
  }

  // Load the identifier for the elf file mapped into memory at |base| into
  // |identifier|. Return false if the identifier could not be created for this
  // file.
  static bool ElfFileIdentifierFromMappedFile(
      const void* base, std::vector<uint8_t>& identifier) {
    // Look for a build id note first.
    if (FindElfBuildIDNote(base, identifier)) return true;

    // Fall back on hashing the first page of the text section.
    return HashElfTextSection(base, identifier);
  }

  // These three functions are not ever called in an unsafe context, so it's OK
  // to allocate memory and use libc.
  static std::string bytes_to_hex_string(const uint8_t* bytes, size_t count,
                                         bool lowercase = false) {
    std::string result;
    for (unsigned int idx = 0; idx < count; ++idx) {
      char buf[3];
      SprintfLiteral(buf, lowercase ? "%02x" : "%02X", bytes[idx]);
      result.append(buf);
    }
    return result;
  }

  // Convert the |identifier| data to a string.  The string will
  // be formatted as a UUID in all uppercase without dashes.
  // (e.g., 22F065BBFC9C49F780FE26A7CEBD7BCE).
  static std::string ConvertIdentifierToUUIDString(
      const std::vector<uint8_t>& identifier) {
    uint8_t identifier_swapped[kMDGUIDSize] = {0};

    // Endian-ness swap to match dump processor expectation.
    memcpy(identifier_swapped, &identifier[0],
           std::min(kMDGUIDSize, identifier.size()));
    uint32_t* data1 = reinterpret_cast<uint32_t*>(identifier_swapped);
    *data1 = htonl(*data1);
    uint16_t* data2 = reinterpret_cast<uint16_t*>(identifier_swapped + 4);
    *data2 = htons(*data2);
    uint16_t* data3 = reinterpret_cast<uint16_t*>(identifier_swapped + 6);
    *data3 = htons(*data3);

    return bytes_to_hex_string(identifier_swapped, kMDGUIDSize);
  }

  // Convert the entire |identifier| data to a lowercase hex string.
  static std::string ConvertIdentifierToString(
      const std::vector<uint8_t>& identifier) {
    return bytes_to_hex_string(&identifier[0], identifier.size(),
                               /* lowercase */ true);
  }

 private:
  // Storage for the path specified
  std::string path_;
};

// End of imports from toolkit/crashreporter/google-breakpad/.
// ----------------------------------------------------------------------------

static std::string IDtoUUIDString(const std::vector<uint8_t>& aIdentifier) {
  std::string uuid = FileID::ConvertIdentifierToUUIDString(aIdentifier);
  // This is '0', not '\0', since it represents the breakpad id age.
  uuid += '0';
  return uuid;
}

// Return raw Build ID in hex.
static std::string IDtoString(const std::vector<uint8_t>& aIdentifier) {
  std::string uuid = FileID::ConvertIdentifierToString(aIdentifier);
  return uuid;
}

// Get the ELF file identifier from file, which will be used for getting the
// breakpad Id and code Id for the binary file pointed by bin_name.
static std::optional<std::vector<uint8_t>> getElfFileIdentifierFromFile(
    const std::string& bin_name) {
  std::vector<uint8_t> identifier;
  identifier.reserve(kDefaultBuildIdSize);

  FileID file_id(bin_name);
  if (file_id.ElfFileIdentifier(identifier)) {
    return identifier;
  }

  return {};
}

// Get the breakpad Id for the ELF file identifier.
static std::string getBreakpadId(
    const std::optional<std::vector<uint8_t>>& aIdentifier) {
  if (aIdentifier) {
    return IDtoUUIDString(aIdentifier.value());
  }

  return {};
}

// Get the code Id for the ELF file identifier.
static std::string getCodeId(
    const std::optional<std::vector<uint8_t>>& aIdentifier) {
  if (aIdentifier) {
    return IDtoString(aIdentifier.value());
  }

  return {};
}

#if defined(GP_OS_linux)
// Returns the path of the main executable, or an empty string if it can't be
// determined.
static std::string GetExecutablePath() {
  const char* execfn = reinterpret_cast<const char*>(getauxval(AT_EXECFN));
  if (!execfn || execfn[0] == '\0') {
    return {};
  }
  if (execfn[0] == '/') {
    return execfn;
  }

  // execfn is relative, because getauxval(AT_EXECFN) returns the path
  // exactly as it was passed to execve.  It needs resolving.
  //
  // This works in the parent process, but in child processes the sandbox
  // would prevent realpath from working.  However child process' are execed
  // with an absolute path in their command line, see XRE_InitCommandLine.
  char resolved[PATH_MAX];
  if (!realpath(execfn, resolved)) {
    return execfn;
  }
  return resolved;
}
#endif  // defined(GP_OS_linux)

static SharedLibrary SharedLibraryAtPath(
    std::string pathStr, unsigned long libStart, unsigned long libEnd,
    unsigned long offset = 0,
    const std::optional<std::vector<uint8_t>>& elfFileIdentifier =
        std::nullopt) {
  size_t pos = pathStr.rfind('/');
  std::string nameStr =
      (pos != std::string::npos) ? pathStr.substr(pos + 1) : pathStr;

  const auto identifier = elfFileIdentifier
                              ? elfFileIdentifier
                              : getElfFileIdentifierFromFile(pathStr);

  return SharedLibrary(libStart, libEnd, offset, getBreakpadId(identifier),
                       getCodeId(identifier), nameStr, pathStr, nameStr,
                       pathStr, std::string{}, "");
}

// State and callback for dl_iterate_phdr
class DLIterateState {
 public:
  SharedLibraryInfo& mInfo;
#if defined(GP_OS_linux)
  bool mExeNameAssigned = false;
  std::string mExeName;
#endif

#if defined(GP_OS_linux)
  DLIterateState(SharedLibraryInfo& aInfo, std::string&& aExeName)
      : mInfo(aInfo), mExeName(std::move(aExeName)) {}
#else
  explicit DLIterateState(SharedLibraryInfo& aInfo) : mInfo(aInfo) {}
#endif

  static int Callback(struct dl_phdr_info* dl_info, size_t size, void* data) {
    DLIterateState* state = reinterpret_cast<DLIterateState*>(data);
    return state->CallbackInternal(dl_info, size);
  }

  int CallbackInternal(struct dl_phdr_info* dl_info, size_t size) {
    if (dl_info->dlpi_phnum <= 0) {
      return 0;
    }

    unsigned long baseAddress = dl_info->dlpi_addr;

    // Skip entries with null base address.
    // Correct implementations of dl_iterate_phdr should never pass a null base
    // address here, but we have a custom implementation of dl_iterate_phdr in
    // in our custom linker which is used on Android 22 and older, and this
    // implementation can sometimes pass null, e.g., from SystemElf::GetBase().
    // We can remove this workaround once we remove the custom linker when we
    // drop support for those Android versions.
    if (baseAddress == 0) {
      return 0;
    }

    unsigned long firstMappingStart = -1;
    unsigned long lastMappingEnd = 0;
    std::vector<uint8_t> elfFileIdentifier;

    for (size_t i = 0; i < dl_info->dlpi_phnum; i++) {
      // Find the mapping start and end.
      if (dl_info->dlpi_phdr[i].p_type == PT_LOAD) {
        unsigned long start =
            dl_info->dlpi_addr + dl_info->dlpi_phdr[i].p_vaddr;
        unsigned long end = start + dl_info->dlpi_phdr[i].p_memsz;
        if (start < firstMappingStart) {
          firstMappingStart = start;
        }
        if (end > lastMappingEnd) {
          lastMappingEnd = end;
        }
      }

      // Try to find the ELF file identifier from memory by looking at the
      // PT_NOTE segments.
      if (dl_info->dlpi_phdr[i].p_type == PT_NOTE &&
          elfFileIdentifier.empty()) {
        const void* section_start = reinterpret_cast<const void*>(
            dl_info->dlpi_addr + dl_info->dlpi_phdr[i].p_vaddr);
        size_t section_length = dl_info->dlpi_phdr[i].p_memsz;
        FileID::ElfClassBuildIDNoteIdentifier(section_start, section_length,
                                              elfFileIdentifier);
      }
    }

    auto optionalElfFileId =
        elfFileIdentifier.size() > 0
            ? std::make_optional(std::move(elfFileIdentifier))
            : std::nullopt;
    // Check in case it's a nullptr.  It's UB to pass nullptr to the
    // std::string constructor.
    std::string libName = dl_info->dlpi_name ? dl_info->dlpi_name : "";

#if defined(GP_OS_linux)
    // The main executable won't have a name and we need to fix that.  The
    // manpage for dl_iterate_phdr says that the item will be the main
    // executable.
    if (!mExeNameAssigned) {
      // This is the first item,  These assertions can help check though.

      // The name is empty.
      MOZ_ASSERT(libName.empty());

      // The program header matches
      MOZ_ASSERT(reinterpret_cast<void*>(getauxval(AT_PHDR)) ==
                 dl_info->dlpi_phdr);

#  ifdef MOZ_DEBUG
      unsigned long entry = getauxval(AT_ENTRY);
      MOZ_ASSERT(entry != 0 && firstMappingStart <= entry &&
                 entry < lastMappingEnd);
#  endif

      libName = mExeName;
      mExeNameAssigned = true;
    }
#endif

    mInfo.AddSharedLibrary(SharedLibraryAtPath(
        std::move(libName), firstMappingStart, lastMappingEnd,
        firstMappingStart - baseAddress, optionalElfFileId));

    return 0;
  }
};

SharedLibraryInfo SharedLibraryInfo::GetInfoForSelf() {
  SharedLibraryInfo info;

#if defined(GP_OS_android)
  // If dl_iterate_phdr doesn't exist, we give up immediately.
  if (!dl_iterate_phdr) {
    // On ARM Android, dl_iterate_phdr is provided by the custom linker.
    // So if libxul was loaded by the system linker (e.g. as part of
    // xpcshell when running tests), it won't be available and we should
    // not call it.
    return info;
  }
#endif

#if defined(GP_OS_android)
  // Read info from /proc/self/maps. We ignore most of it.
  pid_t pid = mozilla::baseprofiler::profiler_current_process_id().ToNumber();
  char path[PATH_MAX];
  SprintfLiteral(path, "/proc/%d/maps", pid);
  std::ifstream maps(path);
  std::string line;
  while (std::getline(maps, line)) {
    int ret;
    unsigned long start;
    unsigned long end;
    char perm[6 + 1] = "";
    unsigned long offset;
    char modulePath[PATH_MAX + 1] = "";
    ret = sscanf(line.c_str(),
                 "%lx-%lx %6s %lx %*s %*x %" PATH_MAX_STRING(PATH_MAX) "s\n",
                 &start, &end, perm, &offset, modulePath);
    if (!strchr(perm, 'x')) {
      // Ignore non executable entries
      continue;
    }
    if (ret != 5 && ret != 4) {
      // LOG("SharedLibraryInfo::GetInfoForSelf(): "
      //     "reading /proc/self/maps failed");
      continue;
    }

    // Use /proc/pid/maps to get the dalvik-jit section since it has no
    // associated phdrs.
    if (0 == strcmp(modulePath, "/dev/ashmem/dalvik-jit-code-cache")) {
      info.AddSharedLibrary(
          SharedLibraryAtPath(modulePath, start, end, offset));
      if (info.GetSize() > 10000) {
        // LOG("SharedLibraryInfo::GetInfoForSelf(): "
        //     "implausibly large number of mappings acquired");
        break;
      }
    }
  }
#endif

#if defined(GP_OS_linux)
  DLIterateState state(info, GetExecutablePath());
#else
  DLIterateState state(info);
#endif

  // We collect the bulk of the library info using dl_iterate_phdr.
  dl_iterate_phdr(DLIterateState::Callback, &state);

  return info;
}

void SharedLibraryInfo::Initialize() { /* do nothing */ }
