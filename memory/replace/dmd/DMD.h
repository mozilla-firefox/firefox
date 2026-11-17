/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DMD_h_
#define DMD_h_

#include <stdarg.h>
#include <string.h>

#include <utility>

#include "mozilla/Assertions.h"
#include "mozilla/UniquePtr.h"
#include "replace_malloc_bridge.h"

namespace mozilla {

class JSONWriteFunc;

namespace dmd {

struct Sizes {
  size_t mStackTracesUsed;
  size_t mStackTracesUnused;
  size_t mStackTraceTable;
  size_t mLiveBlockTable;
  size_t mDeadBlockTable;

  Sizes() { Clear(); }
  void Clear() { memset(this, 0, sizeof(Sizes)); }
};

// See further below for a description of each method. The DMDFuncs class
// should contain a virtual method for each of them (except IsRunning,
// which can be inferred from the DMDFuncs singleton existing).
struct DMDFuncs {
  virtual void Report(const void*);

  virtual void ReportOnAlloc(const void*);

  virtual void ClearReports();

  virtual void Analyze(UniquePtr<JSONWriteFunc>);

  virtual void SizeOf(Sizes*);

  virtual void StatusMsg(const char*, va_list) MOZ_FORMAT_PRINTF(2, 0);

  virtual void ResetEverything(const char*);

#ifndef REPLACE_MALLOC_IMPL
  // We deliberately don't use ReplaceMalloc::GetDMDFuncs here, because if we
  // did, the following would happen.
  // - The code footprint of each call to Get() larger as GetDMDFuncs ends
  //   up inlined.
  // - When no replace-malloc library is loaded, the number of instructions
  //   executed is equivalent, but don't necessarily fit in the same cache
  //   line.
  // - When a non-DMD replace-malloc library is loaded, the overhead is
  //   higher because there is first a check for the replace malloc bridge
  //   and then for the DMDFuncs singleton.
  // Initializing the DMDFuncs singleton on the first access makes the
  // overhead even worse. Either Get() is inlined and massive, or it isn't
  // and a simple value check becomes a function call.
  static DMDFuncs* Get() { return sSingleton.Get(); }

 private:
  // Wrapper class keeping a pointer to the DMD functions. It is statically
  // initialized because it needs to be set early enough.
  // Debug builds also check that it's never accessed before the static
  // initialization actually occured, which could be the case if some other
  // static initializer ended up calling into DMD.
  class Singleton {
   public:
    Singleton() : mValue(ReplaceMalloc::GetDMDFuncs()) {}

    DMDFuncs* Get() {
      MOZ_ASSERT(mInitialized);
      return mValue;
    }

   private:
    DMDFuncs* mValue;
#  ifdef DEBUG
    bool mInitialized = true;
#  endif
  };

  // This singleton pointer must be defined on the program side. In Gecko,
  // this is done in xpcom/base/nsMemoryInfoDumper.cpp.
  static /* DMDFuncs:: */ Singleton sSingleton;
#endif
};

#ifndef REPLACE_MALLOC_IMPL
// Mark a heap block as reported by a memory reporter.
inline void Report(const void* aPtr) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->Report(aPtr);
  }
}

// Mark a heap block as reported immediately on allocation.
inline void ReportOnAlloc(const void* aPtr) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->ReportOnAlloc(aPtr);
  }
}

// Clears existing reportedness data from any prior runs of the memory
// reporters.  The following sequence should be used.
// - ClearReports()
// - run the memory reporters
// - Analyze()
// This sequence avoids spurious twice-reported warnings.
inline void ClearReports() {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->ClearReports();
  }
}

// Determines which heap blocks have been reported, and dumps JSON output
// (via |aWriter|) describing the heap.
//
// The following sample output contains comments that explain the format and
// design choices. The output files can be quite large, so a number of
// decisions were made to minimize size, such as using short property names and
// omitting properties whenever possible.
//
// {
//   // The version number of the format, which will be incremented each time
//   // backwards-incompatible changes are made. A mandatory integer.
//   //
//   // Version history:
//   // - 1: Bug 1044709
//   // - 2: Bug 1094552
//   // - 3: Bug 1100851
//   // - 4: Bug 1121830
//   // - 5: Bug 1253512
//   "version": 5,
//
//   // Information about how DMD was invoked. A mandatory object.
//   "invocation": {
//     // The contents of the $DMD environment variable. A string, or |null| if
//     // $DMD is undefined.
//     "dmdEnvVar": "--mode=dark-matter",
//
//     // The profiling mode. A mandatory string taking one of the following
//     // values: "live", "dark-matter", "cumulative", "scan".
//     "mode": "dark-matter",
//   },
//
//   // Details of all analyzed heap blocks. A mandatory array.
//   "blockList": [
//     // An example of a heap block.
//     {
//       // Requested size, in bytes. This is a mandatory integer.
//       "req": 3584,
//
//       // Requested slop size, in bytes. This is mandatory if it is non-zero,
//       // but omitted otherwise.
//       "slop": 512,
//
//       // The stack trace at which the block was allocated. An optional
//       // string that indexes into the "traceTable" object. If omitted, no
//       // allocation stack trace was recorded for the block.
//       "alloc": "A",
//
//       // One or more stack traces at which this heap block was reported by a
//       // memory reporter. An optional array that will only be present in
//       // "dark-matter" mode. The elements are strings that index into
//       // the "traceTable" object.
//       "reps": ["B"]
//
//       // The number of heap blocks with exactly the above properties. This
//       // is mandatory if it is greater than one, but omitted otherwise.
//       // (Blocks with identical properties don't have to be aggregated via
//       // this property, but it can greatly reduce output file size.)
//       "num": 5,
//
//       // The address of the block. This is mandatory in "scan" mode, but
//       // omitted otherwise.
//       "addr": "4e4e4e4e",
//
//       // The contents of the block, read one word at a time. This is
//       // mandatory in "scan" mode for blocks at least one word long, but
//       // omitted otherwise.
//       "contents": ["0", "6", "7f7f7f7f", "0"]
//     }
//   ],
//
//   // The stack traces referenced by elements of the "blockList" array. This
//   // could be an array, but making it an object makes it easier to see
//   // which stacks correspond to which references in the "blockList" array.
//   "traceTable": {
//     // Each property corresponds to a stack trace mentioned in the "blocks"
//     // object. Each element is an index into the "frameTable" object.
//     "A": ["D", "E"],
//     "B": ["F", "G"]
//   },
//
//   // The stack frames referenced by the "traceTable" object. The
//   // descriptions can be quite long, so they are stored separately from the
//   // "traceTable" object so that each one only has to be written once.
//   // This could also be an array, but again, making it an object makes it
//   // easier to see which frames correspond to which references in the
//   // "traceTable" object.
//   "frameTable": {
//     // Each property key is a frame key mentioned in the "traceTable" object.
//     // Each property value is a string containing a frame description. Each
//     // frame description must be in a format recognized by `fix_stacks.py`,
//     // which requires a frame number at the start. Because each stack frame
//     // description in this table can be shared between multiple stack
//     // traces, we use a dummy value of #00. The proper frame number can be
//     // reconstructed later by scripts that output stack traces in a
//     // conventional non-shared format.
//     "D": "#00: foo (Foo.cpp:123)",
//     "E": "#00: bar (Bar.cpp:234)",
//     "F": "#00: baz (Baz.cpp:345)",
//     "G": "#00: quux (Quux.cpp:456)"
//   }
// }
//
// Implementation note: normally, this function wouldn't be templated, but in
// that case, the function is compiled, which makes the destructor for the
// UniquePtr fire up, and that needs JSONWriteFunc to be fully defined. That,
// in turn, requires to include JSONWriter.h, which includes
// double-conversion.h, which ends up breaking various things built with
// -Werror for various reasons.
//
template <typename JSONWriteFunc>
inline void Analyze(UniquePtr<JSONWriteFunc> aWriteFunc) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->Analyze(std::move(aWriteFunc));
  }
}

// Gets the size of various data structures.  Used to implement a memory
// reporter for DMD.
inline void SizeOf(Sizes* aSizes) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->SizeOf(aSizes);
  }
}

// Prints a status message prefixed with "DMD[<pid>]". Use sparingly.
MOZ_FORMAT_PRINTF(1, 2)
inline void StatusMsg(const char* aFmt, ...) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    va_list ap;
    va_start(ap, aFmt);
    funcs->StatusMsg(aFmt, ap);
    va_end(ap);
  }
}

// Indicates whether or not DMD is running.
inline bool IsRunning() { return !!DMDFuncs::Get(); }

// Resets all DMD options and then sets new ones according to those specified
// in |aOptions|. Also clears all recorded data about allocations. Only used
// for testing purposes.
inline void ResetEverything(const char* aOptions) {
  DMDFuncs* funcs = DMDFuncs::Get();
  if (funcs) {
    funcs->ResetEverything(aOptions);
  }
}
#endif

}  // namespace dmd
}  // namespace mozilla

#endif /* DMD_h_ */
