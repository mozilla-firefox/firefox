/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Coverity model file in order to avoid false-positives

This file is not used by the build. After changing it, it has to be uploaded
manually to the "Modeling File" section of the Coverity Scan analysis settings:
https://scan.coverity.com/projects/firefox?tab=analysis_settings#:~:text=Modeling%20File
*/

#define NULL (void*)0

typedef unsigned char jsbytecode;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef unsigned char uint8_t;

static const uint16_t CHUNK_HEAD_SIZE = 8;

void assert(bool expr) {
  if (!expr) {
    __coverity_panic__();
  }
}

#define ERREXIT(cinfo, err) __coverity_panic__();

static void MOZ_ReportAssertionFailure(const char* aStr, const char* aFilename,
                                       int aLine) {
  __coverity_panic__();
}

static void MOZ_ReportCrash(const char* aStr, const char* aFilename,
                            int aLine) {
  __coverity_panic__();
}

#define MOZ_ASSERT(expr, ...) assert(!!(expr))

/// Unlike MOZ_ReportAssertionFailure(), these are taken in release builds too,
/// they back MOZ_CRASH(), MOZ_RELEASE_ASSERT() and the array bounds checks.
static void MOZ_Crash(const char* aFilename, int aLine, const char* aReason) {
  __coverity_panic__();
}

void mozalloc_abort(const char* const msg) { __coverity_panic__(); }

void NS_ABORT_OOM(size_t aSize) { __coverity_panic__(); }

namespace mozilla {
namespace detail {

void InvalidArrayIndex_CRASH(size_t aIndex, size_t aLength) {
  __coverity_panic__();
}

}  // namespace detail
}  // namespace mozilla

#define NS_ASSERTION(expr, msg) assert(!!(expr))

#define PORT_Assert(expr) assert(!!(expr))

#define PR_ASSERT(expr) assert(!!(expr))

#define NS_PRECONDITION(expr, msg) assert(!!(expr))

#define NS_RUNTIMEABORT(msg) __coverity_panic__()

int GET_JUMP_OFFSET(jsbytecode* pc) {
  __coverity_tainted_data_sanitize__(&pc[1]);
  __coverity_tainted_data_sanitize__(&pc[2]);
  __coverity_tainted_data_sanitize__(&pc[3]);
  __coverity_tainted_data_sanitize__(&pc[4]);

  return 0;
}

// Data sanity checkers
#define XPT_SWAB16(data) __coverity_tainted_data_sanitize__(&data)

#define XPT_SWAB32(data) __coverity_tainted_data_sanitize__(&data)

static unsigned GET_UINT24(const jsbytecode* pc) {
  __coverity_tainted_data_sanitize__(static_cast<void*>(pc));
  // return unsigned((pc[1] << 16) | (pc[2] << 8) | pc[3]);
  return 0;
}

class HeaderParser {
 private:
  class ChunkHeader {
    uint8_t mRaw[CHUNK_HEAD_SIZE];

    uint32_t ChunkSize() const {
      __coverity_tainted_data_sanitize__(static_cast<void*>(&mRaw[4]));
      __coverity_tainted_data_sanitize__(static_cast<void*>(&mRaw[5]));
      __coverity_tainted_data_sanitize__(static_cast<void*>(&mRaw[6]));
      __coverity_tainted_data_sanitize__(static_cast<void*>(&mRaw[7]));

      return ((mRaw[7] << 24) | (mRaw[6] << 16) | (mRaw[5] << 8) | (mRaw[4]));
    }
  };
};

void NS_DebugBreak(uint32_t aSeverity, const char* aStr, const char* aExpr,
                   const char* aFile, int32_t aLine) {
  __coverity_panic__();
}

static inline void Swap(uint32_t* value) {
  __coverity_tainted_data_sanitize__(static_cast<void*>(value));
  *value = (*value >> 24) | ((*value >> 8) & 0x0000ff00) |
           ((*value << 8) & 0x00ff0000) | (*value << 24);
}

static uint32_t xtolong(const uint8_t* ll) {
  __coverity_tainted_data_sanitize__(static_cast<void*>(&ll[0]));
  __coverity_tainted_data_sanitize__(static_cast<void*>(&ll[1]));
  __coverity_tainted_data_sanitize__(static_cast<void*>(&ll[2]));
  __coverity_tainted_data_sanitize__(static_cast<void*>(&ll[3]));

  return (uint32_t)((ll[0] << 0) | (ll[1] << 8) | (ll[2] << 16) |
                    (ll[3] << 24));
}

class ByteReader {
 public:
  const uint8_t* Read(size_t aCount);
  uint32_t ReadU24() {
    const uint8_t* ptr = Read(3);
    if (!ptr) {
      MOZ_ASSERT(false);
      return 0;
    }
    __coverity_tainted_data_sanitize__(static_cast<void*>(&ptr[0]));
    __coverity_tainted_data_sanitize__(static_cast<void*>(&ptr[1]));
    __coverity_tainted_data_sanitize__(static_cast<void*>(&ptr[2]));
    return ptr[0] << 16 | ptr[1] << 8 | ptr[2];
  }
};

static inline uint16_t ReadShortAt(const uint8_t* aBuf, uint32_t aIndex) {
  uint16_t value = (aBuf[aIndex] << 8) | aBuf[aIndex + 1];
  __coverity_tainted_data_sanitize__(static_cast<void*>(&value));
  return value;
}

/// ots::Buffer, the bounds-checked reader of the OpenType sanitiser, verifies
/// that the requested bytes are within the buffer before reading them and
/// returns false otherwise, so the values it hands out are validated.
/// Sanitize them to avoid the tainted data reports (untrusted loop bound,
/// untrusted divisor, ...) on every field read from a font.
namespace ots {

class Buffer {
 public:
  /// Left undefined so that both the success and the failure paths of the
  /// real bounds check stay reachable.
  bool CheckBounds(size_t aCount);

  bool Read(uint8_t* buf, size_t n_bytes) {
    if (!CheckBounds(n_bytes)) {
      return false;
    }
    __coverity_writeall__(static_cast<void*>(buf));
    __coverity_tainted_data_sanitize__(static_cast<void*>(buf));
    return true;
  }

  bool Skip(size_t n_bytes) { return CheckBounds(n_bytes); }

  bool ReadU8(uint8_t* value) { return Read(value, 1); }

  bool ReadU16(uint16_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 2);
  }

  bool ReadS16(int16_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 2);
  }

  bool ReadU24(uint32_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 3);
  }

  bool ReadU32(uint32_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 4);
  }

  bool ReadS32(int32_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 4);
  }

  bool ReadR64(uint64_t* value) {
    return Read(reinterpret_cast<uint8_t*>(value), 8);
  }
};

}  // namespace ots

/// Infallible allocators
/// The moz_x* functions are MOZ_INFALLIBLE_ALLOCATOR: they abort on OOM instead
/// of returning a null pointer, so callers legitimately don't check the result.
void* moz_xmalloc(size_t size) {
  void* ptr = __coverity_alloc__(size);
  if (!ptr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(ptr, "free");
  return ptr;
}

void* moz_xcalloc(size_t nmemb, size_t size) {
  void* ptr = __coverity_alloc__(nmemb * size);
  if (!ptr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(ptr, "free");
  return ptr;
}

void* moz_xrealloc(void* ptr, size_t size) {
  __coverity_escape__(ptr);
  void* newPtr = __coverity_alloc__(size);
  if (!newPtr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(newPtr, "free");
  return newPtr;
}

char* moz_xstrdup(const char* str) {
  char* ptr = (char*)__coverity_alloc_nosize__();
  if (!ptr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(ptr, "free");
  return ptr;
}

char* moz_xstrndup(const char* str, size_t strsize) {
  char* ptr = (char*)__coverity_alloc__(strsize);
  if (!ptr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(ptr, "free");
  return ptr;
}

void* moz_xmemdup(const void* ptr, size_t size) {
  void* newPtr = __coverity_alloc__(size);
  if (!newPtr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(newPtr, "free");
  return newPtr;
}

void* moz_xmemalign(size_t boundary, size_t size) {
  void* ptr = __coverity_alloc__(size);
  if (!ptr) {
    __coverity_panic__();
  }
  __coverity_mark_as_afm_allocated__(ptr, "free");
  return ptr;
}

/// String classes modelling
/// nsAuto[C]String stores its characters in an inline stack buffer, so copying
/// a short string neither allocates nor is more expensive than moving it.
/// Model the copy operations of the string classes as trivial so that
/// COPY_INSTEAD_OF_MOVE stops reporting copies where std::move() brings no
/// benefit (CID 1697195 and friends).
/// The assignments across string classes (`nsCString = nsAutoCString`, as in
/// CID 1700142) go through the substring overloads, so model those too.
template <typename T>
class nsTSubstring {
 public:
  nsTSubstring& operator=(const nsTSubstring& aStr) { return *this; }
  void Assign(const nsTSubstring& aStr) {}
};

template <typename T>
class nsTString : public nsTSubstring<T> {
 public:
  nsTString(const nsTString& aStr) {}
  nsTString(const nsTSubstring<T>& aStr) {}
  nsTString& operator=(const nsTString& aStr) { return *this; }
  nsTString& operator=(const nsTSubstring<T>& aStr) { return *this; }
};

template <typename T, size_t N>
class nsTAutoStringN : public nsTString<T> {
 public:
  nsTAutoStringN(const nsTAutoStringN& aStr) {}
  nsTAutoStringN(const nsTSubstring<T>& aStr) {}
  nsTAutoStringN& operator=(const nsTAutoStringN& aStr) { return *this; }
  nsTAutoStringN& operator=(const nsTSubstring<T>& aStr) { return *this; }
};

/// mozilla::Atomic
/// A pointer stored with exchange() or compareExchange() is owned by the
/// atomic and freed by whoever later swaps it out, as in the lazily created
/// caches of gfxFont (CIDs 1505160, 1505322, 1680871). Mark the stored value
/// as escaping so RESOURCE_LEAK stops reporting it.
namespace mozilla {
enum MemoryOrdering : uint8_t {
  Relaxed,
  ReleaseAcquire,
  SequentiallyConsistent
};
namespace detail {
template <typename T, MemoryOrdering Order>
class AtomicBase {
 public:
  T exchange(T aVal) {
    __coverity_escape__(aVal);
    T old;
    return old;
  }

  bool compareExchange(T aOldValue, T aNewValue) {
    __coverity_escape__(aNewValue);
    bool exchanged;
    return exchanged;
  }
};
}  // namespace detail
}  // namespace mozilla

/// usrsctp defines all its usrsctp_sysctl_set_* setters with this macro, whose
/// lower bound check is `value < <PREFIX>_MIN` with value a uint32_t and most
/// of the _MIN being 0. Upstream silences the compiler about it with
/// -Wno-type-limits, do the same for Coverity by dropping that comparison,
/// which avoids one "macro compares unsigned to 0" report per setter.
#define USRSCTP_SYSCTL_SET_DEF(__field, __prefix)    \
  int usrsctp_sysctl_set_##__field(uint32_t value) { \
    if (value > __prefix##_MAX) {                    \
      errno = EINVAL;                                \
      return (-1);                                   \
    } else {                                         \
      SCTP_BASE_SYSCTL(__field) = value;             \
      return (0);                                    \
    }                                                \
  }

/// 3rd Party Libraries modelling

/// Google
/// Avoid false-positives like CID 1302578 where the reinterpretation of assert
/// is not understood by Coverity
#define GOOGLE_CHECK(expr) assert(!!(expr))

/// NSS
/// These functions take a new reference on a refcounted object and return the
/// same pointer. Each caller then owns its own reference and releases it on
/// its own, so wrapping both pointers in smart pointers is correct. Return an
/// unknown pointer instead of the argument so MULTIPLE_INIT_SMART_PTRS stops
/// treating them as one object owned twice (CIDs 1600983, 1601181, 1690208,
/// 1695041 and friends).
typedef struct CERTCertificateStr CERTCertificate;
typedef struct PK11SlotInfoStr PK11SlotInfo;
typedef struct SECMODModuleStr SECMODModule;

extern "C" {
CERTCertificate* CERT_DupCertificate(CERTCertificate* c) {
  CERTCertificate* reference;
  return reference;
}

PK11SlotInfo* PK11_ReferenceSlot(PK11SlotInfo* slot) {
  PK11SlotInfo* reference;
  return reference;
}

SECMODModule* SECMOD_ReferenceModule(SECMODModule* module) {
  SECMODModule* reference;
  return reference;
}
}
