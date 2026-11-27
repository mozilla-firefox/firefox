/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitCodeSourceInfo_h
#define jit_JitCodeSourceInfo_h

#include <algorithm>  // std::lower_bound
#include <stdint.h>   // uint32_t

#include "js/AllocPolicy.h"   // SystemAllocPolicy
#include "js/ColumnNumber.h"  // JS::LimitedColumnNumberOneOrigin
#include "js/Vector.h"        // Vector

namespace js {
namespace jit {

// [SMDOC] Profiler JIT Source Info
//
// JitCodeSourceInfo maps a native code offset within a JIT entry to the source
// position (line/column) of the bytecode op whose code starts at that offset.
// It lets the Gecko profiler attribute a native address sampled inside JIT code
// back to a source line and column.
//
// Each entry's nativeOffset is relative to the start of the owning JIT entry's
// code (nativeStartAddr() on the owning JitcodeGlobalEntry). The vector is
// sorted by nativeOffset ascending and is built once at compile time by
// PerfSpewer (see PerfSpewer::extractSourceInfo). After that it is immutable,
// so the sampler-side callStackAtAddr reads it lock-free.
//
// The table is owned directly by the JitcodeGlobalEntry.
struct JitCodeSourceInfo {
  uint32_t nativeOffset;
  uint32_t line;
  JS::LimitedColumnNumberOneOrigin column;
};

using JitCodeSourceInfoVector = Vector<JitCodeSourceInfo, 0, SystemAllocPolicy>;

// Returns the source-info entry covering nativeOffset, or nullptr if the table
// is empty. If nativeOffset precedes the first recorded op (e.g. the prologue),
// returns the first entry as a best-effort fallback rather than nullptr.
//
// Like Ion's JitcodeIonTable::findRegionEntry, an op's range is open at its
// start and closed at its end: a return address equal to the next op's start
// offset belongs to the op that made the call.
inline const JitCodeSourceInfo* LookupSourceInfo(
    const JitCodeSourceInfoVector& table, uint32_t nativeOffset) {
  if (table.empty()) {
    return nullptr;
  }
  // lower_bound returns the first entry at or past nativeOffset; step back to
  // the last entry starting before it.
  auto* it = std::lower_bound(table.begin(), table.end(), nativeOffset,
                              [](const JitCodeSourceInfo& e, uint32_t off) {
                                return e.nativeOffset < off;
                              });
  if (it != table.begin()) {
    --it;
  }
  return it;
}

}  // namespace jit
}  // namespace js

#endif /* jit_JitCodeSourceInfo_h */
