/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_CompilationDependencyTracker_h
#define jit_CompilationDependencyTracker_h

#include "jstypes.h"
#include "NamespaceImports.h"

#include "ds/InlineTable.h"
#include "jit/JitAllocPolicy.h"

struct JSContext;

namespace js::jit {

class IonScriptKey;
class MIRGenerator;

struct CompilationDependency : public TempObject {
  enum class Type {
    GetIteratorBytecode,
    ArraySpecies,
    TypedArraySpecies,
    RegExpSpecies,
    RegExpPrototype,
    ObjectProtoHasNoIndexedProps,
    ArrayProtoHasNoIndexedProps,
    StringProtoHasNoIndexedProps,
    EmulatesUndefined,
    ArrayExceedsInt32Length,
    ObjectFuseProperty,
    DefaultCaseMapping,
    Limit
  };

  Type type;

  CompilationDependency(Type type) : type(type) {}

  virtual HashNumber hash() const = 0;
  virtual bool operator==(const CompilationDependency& other) const = 0;

  // Return true iff this dependency still holds. May only be called on main
  // thread.
  virtual bool checkDependency(JSContext* cx) const = 0;
  [[nodiscard]] virtual bool registerDependency(
      JSContext* cx, const IonScriptKey& ionScript) = 0;

  virtual CompilationDependency* clone(TempAllocator& alloc) const = 0;
  virtual ~CompilationDependency() = default;

  struct Hasher {
    using Lookup = const CompilationDependency*;
    static HashNumber hash(const CompilationDependency* dep) {
      return dep->hash();
    }
    static bool match(const CompilationDependency* k,
                      const CompilationDependency* l) {
      return *k == *l;
    }
  };
};

// For a given Warp compilation keep track of the dependencies this compilation
// is depending on. These dependencies will be checked on main thread during
// link time, causing abandonment of a compilation if they no longer hold.
struct CompilationDependencyTracker {
  using SetType = InlineSet<CompilationDependency*, 8,
                            CompilationDependency::Hasher, SystemAllocPolicy>;
  SetType dependencies;

  [[nodiscard]] bool addDependency(TempAllocator& alloc,
                                   const CompilationDependency& dep) {
    // Ensure we don't add duplicates.
    auto p = dependencies.lookupForAdd(&dep);
    if (p) {
      return true;
    }
    auto* clone = dep.clone(alloc);
    if (!clone) {
      return false;
    }
    return dependencies.add(p, clone);
  }

  // Check all dependencies. May only be checked on main thread.
  bool checkDependencies(JSContext* cx) {
    for (auto iter = dependencies.iter(); !iter.done(); iter.next()) {
      const CompilationDependency* dep = iter.get();
      if (!dep->checkDependency(cx)) {
        return false;
      }
    }
    return true;
  }

  void reset() { dependencies.clearAndCompact(); }
};

}  // namespace js::jit
#endif /* jit_CompilationDependencyTracker_h */
