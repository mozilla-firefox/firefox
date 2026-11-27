/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_CacheIRStubKey_h
#define jit_CacheIRStubKey_h

#include "mozilla/HashFunctions.h"

#include <stdint.h>
#include <utility>

#include "js/HashTable.h"
#include "js/UniquePtr.h"
#include "js/Utility.h"

namespace js {
namespace jit {

enum class CacheKind : uint8_t;
class CacheIRStubInfo;

enum class ICStubEngine : uint8_t {
  // Baseline IC, see BaselineIC.h.
  Baseline = 0,

  // Ion IC, see IonIC.h.
  IonIC
};

struct CacheIRStubKey : public DefaultHasher<CacheIRStubKey> {
  struct Lookup {
    const uint8_t* code;
    uint32_t length;
    HashNumber hash;
    CacheKind kind;
    ICStubEngine engine;
    bool recordsProfilerCallSites;

    Lookup(CacheKind kind, ICStubEngine engine, const uint8_t* code,
           uint32_t length, bool recordsProfilerCallSites)
        : code(code),
          length(length),
          hash(mozilla::AddToHash(mozilla::HashBytes(code, length),
                                  uint32_t(kind), uint32_t(engine),
                                  recordsProfilerCallSites)),
          kind(kind),
          engine(engine),
          recordsProfilerCallSites(recordsProfilerCallSites) {}
  };

  static HashNumber hash(const Lookup& l) { return l.hash; }
  static bool match(const CacheIRStubKey& entry, const Lookup& l);

  UniquePtr<CacheIRStubInfo, JS::FreePolicy> stubInfo;

  explicit CacheIRStubKey(CacheIRStubInfo* info) : stubInfo(info) {}
  CacheIRStubKey(CacheIRStubKey&& other)
      : stubInfo(std::move(other.stubInfo)) {}

  void operator=(CacheIRStubKey&& other) {
    stubInfo = std::move(other.stubInfo);
  }
};

}  // namespace jit
}  // namespace js

#endif /* jit_CacheIRStubKey_h */
