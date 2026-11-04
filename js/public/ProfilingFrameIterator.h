/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_ProfilingFrameIterator_h
#define js_ProfilingFrameIterator_h

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"

#include "jstypes.h"

#include "js/GCAnnotations.h"
#include "js/ProfilingCategory.h"
#include "js/TypeDecls.h"

namespace js {
class Activation;
namespace jit {
class JitActivation;
class JSJitProfilingFrameIterator;
class JitcodeGlobalEntry;

// Information about a single frame in a JIT call stack, returned by
// JitcodeGlobalEntry::callStackAtAddr.
struct CallStackFrameInfo {
  // The function name or label for this frame.
  const char* label;
  // The script source ID for this frame. Used to identify which script source
  // this frame belongs to.
  uint32_t sourceId;
  // Line number (1-origin, 0 means no line info available)
  uint32_t line;
  // Column number (1-origin, 0 means no column info available)
  uint32_t column;
};

}  // namespace jit
namespace wasm {
class ProfilingFrameIterator;
}  // namespace wasm
}  // namespace js

namespace JS {

// This iterator can be used to walk the stack of a thread suspended at an
// arbitrary pc. To provide accurate results, profiling must have been enabled
// (via EnableRuntimeProfilingStack) before executing the callstack being
// unwound.
//
// Note that the caller must not do anything that could cause GC to happen while
// the iterator is alive, since this could invalidate Ion code and cause its
// contents to become out of date.
class MOZ_NON_PARAM JS_PUBLIC_API ProfilingFrameIterator {
 public:
  enum class Kind : bool { JSJit, Wasm };

 private:
  JSContext* cx_;
  mozilla::Maybe<uint64_t> samplePositionInProfilerBuffer_;
  js::Activation* activation_;
  // For each JitActivation, this records the lowest (most recent) stack
  // address. This will usually be either the exitFP of the activation or the
  // frame or stack pointer of currently executing JIT/Wasm code. The Gecko
  // profiler uses this to skip native frames between the activation and
  // endStackAddress_.
  void* endStackAddress_ = nullptr;
  Kind kind_ = Kind::JSJit;

  static const unsigned StorageSpace = 9 * sizeof(void*);
  alignas(void*) unsigned char storage_[StorageSpace];

  void* storage() { return storage_; }
  const void* storage() const { return storage_; }

  js::wasm::ProfilingFrameIterator& wasmIter() {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(isWasm());
    return *static_cast<js::wasm::ProfilingFrameIterator*>(storage());
  }
  const js::wasm::ProfilingFrameIterator& wasmIter() const {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(isWasm());
    return *static_cast<const js::wasm::ProfilingFrameIterator*>(storage());
  }

  js::jit::JSJitProfilingFrameIterator& jsJitIter() {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(isJSJit());
    return *static_cast<js::jit::JSJitProfilingFrameIterator*>(storage());
  }

  const js::jit::JSJitProfilingFrameIterator& jsJitIter() const {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(isJSJit());
    return *static_cast<const js::jit::JSJitProfilingFrameIterator*>(storage());
  }

  void maybeSetEndStackAddress(void* addr) {
    // If endStackAddress_ has already been set, don't change it because we
    // want this to correspond to the most recent frame.
    if (!endStackAddress_) {
      endStackAddress_ = addr;
    }
  }

  void settleFrames();
  void settle();

 public:
  struct RegisterState {
    RegisterState()
        : pc(nullptr),
          sp(nullptr),
          fp(nullptr),
          unused1(nullptr),
          unused2(nullptr) {}
    void* pc;
    void* sp;
    void* fp;
    union {
      // Value of the LR register on ARM platforms.
      void* lr;
      // The return address during a tail call operation.
      // Note that for ARM is still the value of LR register.
      void* tempRA;
      // Undefined on non-ARM plaforms outside tail calls operations.
      void* unused1;
    };
    union {
      // The FP reference during a tail call operation.
      void* tempFP;
      // Undefined outside tail calls operations.
      void* unused2;
    };
  };

  ProfilingFrameIterator(
      JSContext* cx, const RegisterState& state,
      const mozilla::Maybe<uint64_t>& samplePositionInProfilerBuffer =
          mozilla::Nothing());
  ~ProfilingFrameIterator();
  void operator++();
  bool done() const { return !activation_; }

  // Assuming the stack grows down (we do), the return value:
  //  - always points into the stack
  //  - is weakly monotonically increasing (may be equal for successive frames)
  //  - will compare greater than newer native and psuedo-stack frame addresses
  //    and less than older native and psuedo-stack frame addresses
  // The exception is at the point of stack switching between the main stack
  // and a suspendable one (see WebAssembly JS Promise Integration proposal).
  void* stackAddress() const;

  enum FrameKind {
    Frame_BaselineInterpreter,
    Frame_Baseline,
    Frame_Ion,
    Frame_WasmBaseline,
    Frame_WasmIon,
    Frame_WasmOther,
  };

  struct Frame {
    FrameKind kind;
    void* stackAddress;
    union {
      void* returnAddress_;
      jsbytecode* interpreterPC_;
    };
    void* activation;
    void* endStackAddress;
    const char* label;
    JSScript* interpreterScript;
    uint64_t realmID;
    uint32_t sourceId;
    uint32_t line;
    uint32_t column;

   public:
    void* returnAddress() const {
      MOZ_ASSERT(kind != Frame_BaselineInterpreter);
      return returnAddress_;
    }
    jsbytecode* interpreterPC() const {
      MOZ_ASSERT(kind == Frame_BaselineInterpreter);
      return interpreterPC_;
    }
    ProfilingCategoryPair profilingCategory() const {
      switch (kind) {
        case FrameKind::Frame_BaselineInterpreter:
          return JS::ProfilingCategoryPair::JS_BaselineInterpret;
        case FrameKind::Frame_Baseline:
          return JS::ProfilingCategoryPair::JS_Baseline;
        case FrameKind::Frame_Ion:
          return JS::ProfilingCategoryPair::JS_IonMonkey;
        case FrameKind::Frame_WasmBaseline:
          return JS::ProfilingCategoryPair::JS_WasmBaseline;
        case FrameKind::Frame_WasmIon:
          return JS::ProfilingCategoryPair::JS_WasmIon;
        case FrameKind::Frame_WasmOther:
          return JS::ProfilingCategoryPair::JS_WasmOther;
      }
      MOZ_CRASH();
    }
  } JS_HAZ_GC_INVALIDATED;

  bool isWasm() const;
  bool isJSJit() const;

  uint32_t extractStack(Frame* frames, uint32_t offset, uint32_t end) const;

  mozilla::Maybe<Frame> getPhysicalFrameWithoutLabel() const;

  // Return the registers from the native caller frame.
  // Nothing{} if this iterator is NOT pointing at a native-to-JIT entry frame,
  // or if the information is not accessible/implemented on this platform.
  mozilla::Maybe<RegisterState> getCppEntryRegisters() const;

 private:
  mozilla::Maybe<Frame> getPhysicalFrameAndEntry(
      const js::jit::JitcodeGlobalEntry** entry) const;

  void iteratorConstruct(const RegisterState& state);
  void iteratorConstruct();
  void iteratorDestroy();
  bool iteratorDone();
} JS_HAZ_GC_INVALIDATED;

JS_PUBLIC_API bool IsProfilingEnabledForContext(JSContext* cx);

/**
 * After each sample run, this method should be called with the current buffer
 * position at which the buffer contents start. This will update the
 * corresponding field on the JSRuntime.
 *
 * See the field |profilerSampleBufferRangeStart| on JSRuntime for documentation
 * about what this value is used for.
 */
JS_PUBLIC_API void SetJSContextProfilerSampleBufferRangeStart(
    JSContext* cx, uint64_t rangeStart);

class ProfiledFrameRange;

// A handle to the underlying JitcodeGlobalEntry, so as to avoid repeated
// lookups on JitcodeGlobalTable.
class MOZ_STACK_CLASS ProfiledFrameHandle {
  friend class ProfiledFrameRange;

  JSRuntime* rt_;
  js::jit::JitcodeGlobalEntry& entry_;
  void* addr_;
  void* canonicalAddr_;
  js::jit::CallStackFrameInfo frameInfo_;
  uint32_t depth_;

  ProfiledFrameHandle(JSRuntime* rt, js::jit::JitcodeGlobalEntry& entry,
                      void* addr, const js::jit::CallStackFrameInfo& frameInfo,
                      uint32_t depth);

 public:
  const char* label() const { return frameInfo_.label; }
  uint32_t depth() const { return depth_; }
  void* canonicalAddress() const { return canonicalAddr_; }

  JS_PUBLIC_API ProfilingFrameIterator::FrameKind frameKind() const;

  JS_PUBLIC_API uint64_t realmID() const;

  JS_PUBLIC_API uint32_t sourceId() const { return frameInfo_.sourceId; }

  JS_PUBLIC_API uint32_t line() const { return frameInfo_.line; }

  JS_PUBLIC_API uint32_t column() const { return frameInfo_.column; }
};

class ProfiledFrameRange {
 public:
  class Iter final {
   public:
    Iter(const ProfiledFrameRange& range, uint32_t index)
        : range_(range), index_(index) {}

    JS_PUBLIC_API ProfiledFrameHandle operator*() const;

    // Provide the bare minimum of iterator methods that are needed for
    // C++ ranged for loops.
    Iter& operator++() {
      ++index_;
      return *this;
    }
    bool operator==(const Iter& rhs) const { return index_ == rhs.index_; }
    bool operator!=(const Iter& rhs) const { return !(*this == rhs); }

   private:
    const ProfiledFrameRange& range_;
    uint32_t index_;
  };

  Iter begin() const { return Iter(*this, 0); }
  Iter end() const { return Iter(*this, depth_); }

  // These frames apply to all samples from the position passed to
  // GetProfiledFrames up to and including this position. In later samples,
  // the address belongs to code that was placed there afterwards. UINT64_MAX
  // if no code was placed at this address afterwards.
  uint64_t lastSamplePosition() const { return lastSamplePos_; }

 private:
  friend JS_PUBLIC_API ProfiledFrameRange
  GetProfiledFrames(JSContext* cx, void* addr, uint64_t samplePositionInBuffer);

  ProfiledFrameRange(JSRuntime* rt, void* addr,
                     js::jit::JitcodeGlobalEntry* entry, uint64_t lastSamplePos)
      : rt_(rt),
        addr_(addr),
        entry_(entry),
        lastSamplePos_(lastSamplePos),
        depth_(0) {}

  JSRuntime* rt_;
  void* addr_;
  js::jit::JitcodeGlobalEntry* entry_;
  uint64_t lastSamplePos_;
  // Maximum inlining depth. This must match InlineScriptTree::MaxDepth.
  // We can't use InlineScriptTree::MaxDepth directly here because this is a
  // public header and InlineScriptTree.h is private.
  static constexpr uint32_t MaxInliningDepth = 8;
  js::jit::CallStackFrameInfo frames_[MaxInliningDepth] = {};
  uint32_t depth_;
};

// Returns the frames for the JIT code address |addr| in the sample that starts
// at |samplePositionInBuffer| in the profiler buffer. The same address can
// belong to different code in different samples, because the memory of
// collected code can be reused for new code. The result is a range that can be
// iterated over using C++ ranged for loops.
JS_PUBLIC_API ProfiledFrameRange
GetProfiledFrames(JSContext* cx, void* addr, uint64_t samplePositionInBuffer);

}  // namespace JS

#endif /* js_ProfilingFrameIterator_h */
