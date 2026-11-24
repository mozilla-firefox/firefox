/*
 *
 * Copyright 2025 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_stacks_h
#define wasm_stacks_h

#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"

#include <bit>

#include "gc/Barrier.h"
#include "js/AllocPolicy.h"
#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "util/TrailingArray.h"
#include "vm/NativeObject.h"
#include "wasm/WasmAnyRef.h"
#include "wasm/WasmCode.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmFrame.h"

namespace js {
class WasmTagObject;
class Nursery;
namespace jit {
class CodeOffset;
class Label;
}  // namespace jit
namespace wasm {
class CallSiteDesc;
}  // namespace wasm
}  // namespace js

namespace js::wasm {

// Always forward declare these interfaces to simplify conditional compilation
// in a few places.
struct SwitchTarget;
struct Handler;
struct Handlers;
class Instance;
class ContStack;
class ContObject;
class ContStackArena;
class ContStackAllocator;

#ifdef ENABLE_WASM_JSPI

// A stack target describes a stack that can be switched to using the
// stack-switching feature. There is one for the 'main stack' and one for each
// continuation stack.
//
// StackTarget is declared here so that WasmContext.h can include this file
// and get both StackTarget and the allocator types without a cycle.
struct StackTarget {
  // The continuation stack, if any. This is a weak self-reference, as
  // it's only non-null when stored on the same ContStack.
  ContStack* stack = nullptr;

  // The limit that jit code should use on this stack. This will be constant
  // over the lifetime of the stack.
  JS::NativeStackLimit jitLimit = JS::NativeStackLimitMin;

  // The Win32 TIB stack base and limit fields. With lazy commit these may
  // change as the stack grows.
#  if defined(_WIN32)
  void* tibStackBase = nullptr;
  void* tibStackLimit = nullptr;
#  endif

  bool isMainStack() const { return !stack; }
};

struct ContStackDeleter {
  void operator()(ContStack* cont);
};
using UniqueContStack = mozilla::UniquePtr<ContStack, ContStackDeleter>;

// A switch target contains information about the destination of a stack switch
// operation.
//
// This must be aligned to match WasmStackAlignment.
struct alignas(16) SwitchTarget {
  void* framePointer = nullptr;
  void* stackPointer = nullptr;
#  ifdef JS_HW_SHADOW_STACK
  // The shadow stack pointer to resume this target with.
  uintptr_t* shadowStackPointer = nullptr;
#  endif
  void* resumePC = nullptr;
  wasm::Instance* instance = nullptr;
  // An optional pointer to where params for a stack switching operation can be
  // stored.
  void* paramsArea = nullptr;
  // The underlying stack this switch is on. This has the stack limits we need
  // to update to.
  const StackTarget* stack = nullptr;

  void trace(JSTracer* trc) const;
};

// A suspend handler for a given tag that indicates where to switch to.
struct Handler {
  // Rooted on the stack, and doesn't need barriers.
  WasmTagObject* tag = nullptr;
  // Reference to the containing handlers object.
  Handlers* handlers = nullptr;
  // Where to switch to when a suspend matches this tag.
  SwitchTarget target;
};

// An ordered list of handlers that is created by a `resume `instruction. It
// contains an ordered list of handlers to search when a `suspend` instruction
// is executed, and also owns the child continuation stack that was resumed.
//
// This must be aligned to match WasmStackAlignment.
struct alignas(16) Handlers : TrailingArray<Handlers> {
  // The continuation stack this handler is on. Null if we're on the main stack.
  // The next handler to search for can be found on this.
  ContStack* self = nullptr;

  // The owning reference for the child continuation stack.
  UniqueContStack child = nullptr;

  // Target for normal returns.
  SwitchTarget returnTarget{};

  // The number of handlers that trail this header.
  uint32_t numHandlers;

  // 32-bit's is enough for anyone.
  static_assert(MaxHandlers < UINT32_MAX);

  static constexpr size_t offsetOfHandler(size_t index) {
    return sizeof(wasm::Handlers) + index * sizeof(wasm::Handler);
  }

  static constexpr size_t sizeOf(size_t numHandlers) {
    MOZ_RELEASE_ASSERT(numHandlers <= wasm::MaxHandlers);
    return sizeof(wasm::Handlers) + sizeof(wasm::Handler) * numHandlers;
  }
  size_t sizeOf() const { return Handlers::sizeOf(numHandlers); }

  bool isMainStack() const { return returnTarget.stack->isMainStack(); }

  Handler* handler(uint32_t index) {
    MOZ_RELEASE_ASSERT(index < wasm::MaxHandlers);
    return offsetToPointer<Handler>(offsetOfHandler(index));
  }
  const Handler* handler(uint32_t index) const {
    MOZ_RELEASE_ASSERT(index < wasm::MaxHandlers);
    return offsetToPointer<Handler>(offsetOfHandler(index));
  }

  // This is always constructed by JIT code on the stack.
  Handlers() = delete;
  ~Handlers() = delete;

  void trace(JSTracer* trc) const;
};

// The size of a continuation stack is determined by the system page sizes and
// user preferences. We compute the dynamic parts once so it stays consistent
// within an allocator.
struct ContStackSize {
  size_t jitStackSize = 0;
  size_t headerSize = 0;
  size_t totalSize = 0;
#  ifdef JS_HW_SHADOW_STACK
  // The size of the separately mapped hardware shadow stack for each stack.
  size_t shadowStackSize = 0;
#  endif

  void compute();
};

// The underlying execution stack of a continuation. This class is the header
// of the stack and the actual execution stack is located physically before
// this header.
//
// See [SMDOC] Wasm Stack Switching in WasmStacks.cpp for more information.
class ContStack {
  // The arena this stack was allocated from.
  ContStackArena* arena_ = nullptr;

  // The base pointer of the allocation this stack is from.
  uintptr_t allocationBase_ = 0;

  // Pointers to the usable regions of the stack.
  JS::NativeStackBase stackBase_ = 0;
  JS::NativeStackLimit stackLimitForSystem_ = JS::NativeStackLimitMin;
  JS::NativeStackLimit stackLimitForJit_ = JS::NativeStackLimitMin;

#  ifdef JS_HW_SHADOW_STACK
  // The base of this stack's shadow stack allocation.
  uintptr_t shadowStackAllocationBase_ = 0;

  // The shadow stack pointer to switch to this stack's shadow stack with for
  // the initial resume.
  uintptr_t* initialShadowStackPointer_ = nullptr;

  // The shadow stack pointer to switch back to this stack's shadow stack with
  // when returning from a VM exit to the main stack. While the native stack
  // pointer is recovered by leaving the frame pointer on the continuation
  // stack, the shadow stack has no equivalent, so it is saved here. This
  // works since there can be at most one exit in progress for a ContStack.
  uintptr_t* exitShadowStackPointer_ = nullptr;
#  endif

  // The initial resume target and callee for the base frame to use.
  SwitchTarget initialResumeTarget_{};
  HeapPtr<JSFunction*> initialResumeCallee_;
  // Keeps the creator module's code alive while initialResumeTarget_.resumePC
  // points into it (before the first resume).
  SharedCode initialResumeCode_;

  // A target useable when switching to this stack.
  StackTarget target_{};

  // The parent handlers we can use when suspending. This is allocated on the
  // stack of a caller stack. We always have handlers if we are active.
  Handlers* handlers_ = nullptr;

  // The target that can be used to resume this stack if we're suspended and
  // can be resumed. This may be a different continuation stack than us if a
  // stack of continuations were suspended. That stack is the 'resume target'
  // and we are the 'resume base'. We are always an ancestor stack of the
  // resume target stack.
  SwitchTarget* resumeTarget_ = nullptr;

  // Current state of the jit stack pages. Transitions:
  //   Ready -> Poisoned    via poison()    (filled with poison, NoAccess)
  //   Ready -> Decommitted via decommit()  (physical pages returned to OS)
  //   *     -> Ready       via prepare()
  enum class PageState : uint8_t { Ready, Poisoned, Decommitted };
  PageState pageState_ = PageState::Ready;

  ContStack() = default;
  ~ContStack() = default;

  FrameWithInstances* baseFrame() {
    uintptr_t baseFrameAddress =
        reinterpret_cast<uintptr_t>(this) + ContStack::offsetOfBaseFrame();
    return reinterpret_cast<FrameWithInstances*>(baseFrameAddress);
  }

  // Return if this stack is dead (not active nor resumable).
  bool isDead() const { return !handlers_ && !resumeTarget_; }

  // Initialize a ContStack in a ContStackArena. This will leave it in a
  // poisoned state, ready to be prepared for use.
  static void init(ContStackArena* arena, uintptr_t allocationBase,
                   const ContStackSize& size);
  // Prepare a stack for execution. Must be called after init, poison, or
  // decommit. Transitions pageState_ to Ready.
  void prepare(Handle<ContObject*> continuation, Handle<JSFunction*> target,
               void* contBaseFrameStub, const Code* creatorCode);
  // Reset the fields for returning to a ContStackArena. Can call poison or
  // decommit after this. Must call prepare before executing.
  void reset();
  // Fill the jit stack pages with the poison pattern and mark them no-access.
  // Caller decides whether poisoning is wanted; this method does the work
  // unconditionally. Requires pageState_ == Ready.
  void poison();
  // Return the physical pages of the jit stack region to the OS. No-op if
  // already decommitted. Requires gc::DecommitEnabled().
  void decommit();

  // Ensure that the ContStack has a usable hardware shadow stack (if enabled).
  bool ensureShadowStack();
  // Release any hardware shadow stack pages. ensureShadowStack must be called
  // before this ContStack is used again.
  void freeShadowStack();

  static void free(ContStack* stack);
  friend ContStackDeleter;
  friend class ContStackArena;

 public:
  static void unwind(wasm::Handlers* handlers);
  static void freeSuspended(UniqueContStack resumeBase);

  // Trace the fields on this stack, but no the frames.
  void traceFields(JSTracer* trc);
  Instance* baseInstance() { return baseFrame()->calleeInstance(); }
  // Trace the fields and all frames for a suspended stack. This must be the
  // resume base. When marking, |src| (the owning ContObject) also traces the
  // inferred ContObject to Debugger.Frame edges via
  // DebugAPI::traceWasmContFrame.
  void traceSuspended(JSTracer* trc, JSObject* src);
  // Update all the frames for a moving GC. This must be the resume base.
  void updateSuspendedForMovingGC(Nursery& nursery);

  // Given the base of the allocation for a continuation stack, get this header.
  static ContStack* fromAllocation(uintptr_t allocation,
                                   const ContStackSize& size) {
    return reinterpret_cast<ContStack*>(allocation + size.totalSize -
                                        size.headerSize);
  }

  // Given the base frame pointer of a continuation stack, get this header.
  static ContStack* fromBaseFrameFP(void* fp) {
    return reinterpret_cast<ContStack*>(reinterpret_cast<uintptr_t>(fp) -
                                        offsetOfBaseFrameFP());
  }

  static int32_t offsetOfBaseFrame();
  static int32_t offsetOfBaseFrameFP();

  static constexpr int32_t offsetOfInitialResumeTarget() {
    return offsetof(ContStack, initialResumeTarget_);
  }
  static constexpr int32_t offsetOfInitialResumeCallee() {
    return offsetof(ContStack, initialResumeCallee_);
  }
  static constexpr int32_t offsetOfHandlers() {
    return offsetof(ContStack, handlers_);
  }
  static constexpr int32_t offsetOfStackTarget() {
    return offsetof(ContStack, target_);
  }
  static constexpr int32_t offsetOfResumeTarget() {
    return offsetof(ContStack, resumeTarget_);
  }

#  ifdef JS_HW_SHADOW_STACK
  static constexpr int32_t offsetOfExitShadowStackPointer() {
    return offsetof(ContStack, exitShadowStackPointer_);
  }
#  endif

  // The allocation base pointer for this stack. This is not the stack base for
  // execution.
  uintptr_t allocationBase() const { return allocationBase_; }

  uintptr_t* exitShadowStackPointer() const {
#  ifdef JS_HW_SHADOW_STACK
    return exitShadowStackPointer_;
#  else
    return nullptr;
#  endif
  }

  // Return if we can resume this stack.
  bool canResume() const {
    MOZ_RELEASE_ASSERT(!!handlers_ != !!resumeTarget_);
    return !!resumeTarget_;
  }
  // Return if this stack has never been resumed.
  bool isInitial() const { return resumeTarget_ == &initialResumeTarget_; }

  Handlers* handlers() { return handlers_; }
  const Handlers* handlers() const { return handlers_; }
  ContStack* handlersStack() const {
    if (!handlers_) {
      return nullptr;
    }
    return handlers_->returnTarget.stack->stack;
  }
  const SwitchTarget* resumeTarget() const { return resumeTarget_; }
  ContStack* resumeTargetStack() const {
    if (!resumeTarget_) {
      return nullptr;
    }
    return resumeTarget_->stack->stack;
  }
  const StackTarget& stackTarget() const { return target_; }

  // The logical beginning or bottom of the stack, which is the physically
  // highest memory address in the stack allocation.
  JS::NativeStackBase stackBase() const { return stackBase_; }

  // The logical end or top of the stack for system code, which is the
  // physically lowest memory address in the stack allocation. This does not
  // include any 'red zone' space, and so it is not safe to use if a stub
  // or OS interrupt handler could run on the stack. Use
  // `stackMemoryLimitForJit` instead.
  JS::NativeStackLimit stackLimitForSystem() const {
    return stackLimitForSystem_;
  }

  // The logical end or top of the stack for JIT code, which is the
  // physically lowest memory address in the stack allocation. This does
  // include 'red zone' space for running stubs or OS interrupt handlers.
  JS::NativeStackLimit stackLimitForJit() const { return stackLimitForJit_; }

  bool hasStackAddress(uintptr_t stackAddress) const {
    return stackBase_ >= stackAddress && stackAddress > stackLimitForSystem_;
  }

  // Do a linear search to see if this stack is linked to the main stack.
  bool findIfActive() const {
    MOZ_RELEASE_ASSERT(!canResume());
    const Handlers* baseHandlers = findBaseHandlers();
    return baseHandlers && baseHandlers->isMainStack();
  }

  // Do a linear search to find the base handler for this continuation.
  const Handlers* findBaseHandlers() const {
    if (!handlers_) {
      return nullptr;
    }
    const Handlers* handlers = handlers_;
    while (handlers->self && handlers->self->handlers()) {
      handlers = handlers->self->handlers();
    }
    return handlers;
  }
};

using UniqueContStackArena =
    mozilla::UniquePtr<ContStackArena, JS::DeletePolicy<ContStackArena>>;
using ContStackArenaVector =
    mozilla::Vector<UniqueContStackArena, 4, SystemAllocPolicy>;

// A free-list of contiguously allocated ContStack objects. This object is the
// header which points at the actual mmapped region.
class ContStackArena {
  ContStackAllocator* const owner_;
  // The base pointer of the mmapped region of this arena.
  void* base_ = nullptr;
  // How many stacks can be stored in this arena.
  const uint32_t capacity_ = 0;
  // A bitmask representing everything being freed.
  const uint64_t allFreeMask_ = 0;
  // A bitmask of the free stacks in this arena.
  uint64_t currentFreeMask_ = 0;
  // Set when a stack is freed; cleared by purge(). Used to skip redundant
  // madvise calls when nothing has been freed since the last purge.
  bool dirtySinceLastPurge_ = false;

  // Return the stack to the free list and poison it. Called automatically
  // by ContStackDeleter.
  void free(ContStack* stack);

  bool isAllocated(uint32_t index) const {
    MOZ_RELEASE_ASSERT(index < capacity_);
    return (currentFreeMask_ & (uint64_t(1) << index)) == 0;
  }

  // Return the base of the allocation for `index`.
  uintptr_t stackAllocation(uint32_t index) const;
  // Return the ContStack pointer for `index`.
  ContStack* stack(uint32_t index) const;
  // Compute the index of a given ContStack header within this arena.
  uint32_t stackIndex(const ContStack* stack) const;

  friend class ContStack;

 public:
  // Do not use this, only public to make js_new work well.
  ContStackArena(ContStackAllocator* owner, void* base);
  ~ContStackArena();

  // We use a bitvector for managing allocation status, which limits our
  // capacity.
  static constexpr size_t MaxCapacity = sizeof(currentFreeMask_) * CHAR_BIT;

  // Allocate and initialize an arena of continuation stacks.
  static UniqueContStackArena create(ContStackAllocator* owner);

  // The base pointer of the arena.
  uintptr_t base() const { return reinterpret_cast<uintptr_t>(base_); }
  // How many stacks can be stored in this arena.
  uint32_t capacity() const { return capacity_; }
  // Whether any stacks have been allocated in this arena.
  bool isEmpty() const { return currentFreeMask_ == allFreeMask_; }
  // Whether a new stack can be allocated from this arena.
  bool isFull() const { return currentFreeMask_ == 0; }
  // Whether this arena contains a stack pointer.
  bool contains(uintptr_t address) const;

  // Allocate a ContStack. The stack will be returned automatically to the pool
  // through ContStackDeleter when the UniquePtr goes out of scope.
  UniqueContStack allocate(Handle<ContObject*> continuation,
                           Handle<JSFunction*> target, void* contBaseFrameStub,
                           const Code* creatorCode);

  // Find the stack that would belong to this SP, if any.
  ContStack* findForAddress(uintptr_t address) const;

#  ifdef JS_HW_SHADOW_STACK
  // Bytes of shadow stack currently mapped for stacks in this arena.
  size_t shadowStackMappedBytes() const;
#  endif

  template <typename Fn>
  void forEachAllocatedStack(Fn&& fn) const {
    uint64_t allocatedMask = ~currentFreeMask_ & allFreeMask_;
    while (allocatedMask) {
      // Find the lowest allocated bit.
      uint32_t index = uint32_t(std::countr_zero(allocatedMask));

      // Visit the stack.
      fn(stack(index));

      // Clear the lowest set bit.
      allocatedMask &= allocatedMask - 1;
    }
  }

  template <typename Fn>
  void forEachFreedStack(Fn&& fn) const {
    uint64_t freeMask = currentFreeMask_;
    while (freeMask) {
      uint32_t index = uint32_t(std::countr_zero(freeMask));
      fn(stack(index));
      freeMask &= freeMask - 1;
    }
  }

  // Decommit the jit-stack pages of all freed slots in this arena.
  void purge();
};

// An allocator for ContStack. It supports efficient:
//   1. Allocation and deallocation
//   2. Iteration over all allocated stacks
//   3. Search for a stack given an SP
//
// Every ContStack has a fixed size determined at runtime and stored as
// ContStackSize. The allocator manages a pool of ContStackArena which each
// contain contiguous pools of ContStacks.
//
// This class is not thread-safe and must be used only on the same thread.
class ContStackAllocator {
  // The runtime computed size we should use for continuation stacks. Computed
  // once at the first allocation; changes to the relevant prefs at runtime do
  // not take effect.
  ContStackSize stackSize_;
  // How many stacks to put in an arena. Computed once at the first allocation,
  // like stackSize_.
  uint32_t arenaCapacity_ = 0;
  // The most arenas we will map, derived from the virtual memory cap. Computed
  // once at the first allocation, like stackSize_.
  size_t maxArenas_ = 0;
  // The pool of arenas. These are sorted by base address and don't overlap,
  // which allows us to binary search to find an arena for a given SP.
  ContStackArenaVector arenas_;
  // Whether we've been initialized or not.
  bool initialized_ = false;

  void ensureInitialized();

  ContStackArena* addArena(JSContext* cx);
  ContStackArena* findOrAddArenaForAllocate(JSContext* cx);
  ContStackArena* findArenaForAddress(uintptr_t address) const;

 public:
  ContStackAllocator() = default;

  const ContStackSize& stackSize() const { return stackSize_; }
  uint32_t arenaCapacity() const { return arenaCapacity_; }
  size_t arenaSize() const {
    // See the assertion in ContStackSize::compute for why this is safe.
    return arenaCapacity_ * stackSize_.totalSize;
  }

  // Allocate a ContStack. The stack will be returned automatically to the pool
  // through ContStackDeleter when the UniquePtr goes out of scope.
  UniqueContStack allocate(JSContext* cx, Handle<ContObject*> continuation,
                           Handle<JSFunction*> target, void* contBaseFrameStub,
                           const Code* creatorCode);

  // Find the ContStack whose stack region contains `address`.
  ContStack* findForAddress(uintptr_t address) const;

  // Call fn(ContStack*) for every currently allocated ContStack.
  template <typename Fn>
  void forEachAllocatedStack(Fn&& fn) const {
    for (const auto& arena : arenas_) {
      arena->forEachAllocatedStack(fn);
    }
  }

  // Free empty arenas. If !shrinking, keep one empty arena cached.
  void purge(bool shrinking);

  // Total mapped bytes across all arenas. This reads arenas_.length()
  // without synchronization, so it is only safe to call from the thread that
  // owns this allocator (typically during memory reporting on the main
  // thread).
  size_t sizeOfNonHeap() const;
};

// A suspended wasm continuation that can be resumed.
//
// See [SMDOC] Wasm Stack Switching in WasmStacks.cpp for more information.
class ContObject : public NativeObject {
 public:
  static const JSClass class_;

  enum {
    ResumeBaseSlot,
    SlotCount,
  };

  // Create a continuation that when resumed will call the `target` wasm
  // function. `contBaseFrameStub` is the corresponding stub created by
  // wasm::GenerateContBaseFrameStub for the wasm function type.
  static ContObject* create(JSContext* cx, Handle<JSFunction*> target,
                            void* contBaseFrameStub, const Code* creatorCode);
  // Create a continuation that is empty and cannot be resumed.
  static ContObject* createEmpty(JSContext* cx);

  static constexpr size_t offsetOfResumeBase() {
    return NativeObject::getFixedSlotOffset(ResumeBaseSlot);
  }

 private:
  static const JSClassOps classOps_;
  static const ClassExtension classExt_;

  ContStack* resumeBase() {
    Value stackSlot = getFixedSlot(ResumeBaseSlot);
    if (stackSlot.isUndefined()) {
      return nullptr;
    }
    return reinterpret_cast<ContStack*>(stackSlot.toPrivate());
  }

  // Destroy this continuation by taking the inner stack owned by it.
  UniqueContStack takeResumeBase() {
    UniqueContStack result = UniqueContStack(resumeBase());
    setFixedSlot(ResumeBaseSlot, JS::UndefinedValue());
    return result;
  }

  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);
};

// Adjust the VM stack limits for entering the stack target.
// Clobbers scratch. On Win32, also clobbers cx.
void EmitEnterStackTarget(jit::MacroAssembler& masm, jit::Register cx,
                          jit::Register stackTarget, jit::Register scratch);

// Switch to the given switch target and continue execution there.
// Clobbers all registers.
void EmitSwitchStack(jit::MacroAssembler& masm, jit::Register switchTarget,
                     jit::Register scratch1, jit::Register scratch2,
                     jit::Register scratch3);

// Zero out a switch target.
void EmitClearSwitchTarget(jit::MacroAssembler& masm,
                           jit::Register switchTarget);

// Search the handler chain to find the handler that matches a given tag.
// Output contains a pointer to the wasm::Handler that was matched.
// If no match is found then branch to `fail`.
void EmitFindHandler(jit::MacroAssembler& masm, jit::Register instance,
                     jit::Register tag, jit::Register output,
                     jit::Register scratch1, jit::Register scratch2,
                     jit::Register scratch3, jit::Register scratch4,
                     jit::Label* fail);

// Suspend to the given handler.
//
// Does not return. After the stack switch, execution resumes at
// *suspendCodeOffset with only InstanceReg live.
//
// suspendResultsAreaBase is the FP relative offset of the suspend results
// area (0 when no tag results). Its address is advertised into the resume
// SwitchTarget's paramsArea so the next resumer writes the tag results
// straight into this area; the suspender then reads them back from here.
//
// Clobbers scratch1, scratch2, scratch3, and suspendedCont.
void EmitSuspend(jit::MacroAssembler& masm, jit::Register instance,
                 jit::Register suspendedCont, jit::Register handler,
                 jit::Register scratch1, jit::Register scratch2,
                 jit::Register scratch3, const CallSiteDesc& callSiteDesc,
                 jit::CodeOffset* suspendCodeOffset,
                 uint32_t* suspendFramePushed, uint32_t suspendResultsAreaBase);

// Offsets used when initializing a handler for a resume.
struct HandlerJitOffsets {
  uint32_t tagInstanceDataOffset = UINT32_MAX;
  uint32_t resultsAreaOffset = UINT32_MAX;
};

// Validates the continuation (null/resumable checks, branching to fail on
// failure) and computes into `output` the destination paramsArea pointer that
// the resume params should be written to.
//
// For a fresh continuation the destination is the resumer's own
// resumeParamsArea (FP - resumeParamsAreaBase), which is also advertised into
// initialResumeTarget.paramsArea for the base frame stub. For a suspended
// continuation the destination is the paramsArea the suspender already
// advertised (its suspendResultsArea), so the params are written straight into
// the suspender's frame.
//
// Only used when the continuation takes params (resumeParamsAreaBase != 0).
//
// Clobbers scratch1, scratch2; preserves cont.
void EmitPrepareResume(jit::MacroAssembler& masm, jit::Register cont,
                       uint32_t resumeParamsAreaBase, jit::Register output,
                       jit::Register scratch1, jit::Register scratch2,
                       jit::Label* fail);

// Resume a suspended continuation with the given handlers.
//
// Does not return. After the resumed stack returns, execution continues at
// *resumeCodeOffset with only InstanceReg live. Each handler landing pad
// jumps to the corresponding handlerLabels entry with only InstanceReg live.
//
// handlersParamsAreaBase: FP relative offset of the handlers params area;
//   each handler's target.paramsArea is set to FP - base + resultsAreaOffset
//   (0 only when there are no handlers; the continuation is always passed, so a
//   handler with no tag params still has a non-empty area).
// contResultsAreaBase: FP relative offset of the cont results area whose
//   address is written into returnTarget.paramsArea so the typed base frame
//   stub can store cont results (0 when there are no cont results).
//
// The resume params are written by EmitPrepareResume before this is called, so
// EmitResume no longer touches resumeTarget.paramsArea.
//
// Clobbers scratch1, scratch2, scratch3, and cont.
void EmitResume(jit::MacroAssembler& masm, jit::Register instance,
                jit::Register cont, uint32_t handlersParamsAreaBase,
                jit::Register scratch1, jit::Register scratch2,
                jit::Register scratch3,
                mozilla::Span<HandlerJitOffsets> handlerOffsets,
                mozilla::Span<jit::Label*> handlerLabels,
                const CallSiteDesc& callSiteDesc,
                jit::CodeOffset* resumeCodeOffset, uint32_t* resumeFramePushed,
                uint32_t contResultsAreaBase);

#endif  // ENABLE_WASM_JSPI

}  // namespace js::wasm

#endif  // wasm_stacks_h
