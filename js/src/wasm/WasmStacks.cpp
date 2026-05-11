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

#include "wasm/WasmStacks.h"

#include "mozilla/DebugOnly.h"

#include "builtin/Promise.h"
#include "gc/Memory.h"
#include "jit/Assembler.h"
#include "jit/MacroAssembler.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/NativeObject.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmContext.h"
#include "wasm/WasmFrameIter.h"
#include "wasm/WasmJS.h"
#include "wasm/WasmStubs.h"

#include "jit/MacroAssembler-inl.h"
#include "vm/JSObject-inl.h"

#ifdef XP_WIN
// We only need the `windows.h` header, but this file can get unified built
// with WasmSignalHandlers.cpp, which requires `winternal.h` to be included
// before the `windows.h` header, and so we must include it here for that case.
#  include <winternl.h>  // must include before util/WindowsWrapper.h's `#undef`s

#  include "util/WindowsWrapper.h"
#endif

using namespace js;
using namespace js::jit;

#ifdef ENABLE_WASM_JSPI

namespace js::wasm {

/* clang-format off */

// [SMDOC] Wasm Stack Switching (fka WasmFX/Typed Continuations)
//
// This file implements the runtime support for the wasm stack-switching
// proposal in SpiderMonkey.
//
// JS-PI is built on top of these primitives; see [SMDOC] JS Promise
// Integration in WasmPI.h.
//
// Implemented: `cont.new`, `resume` (with `on` suspend handlers), `suspend`.
// Not implemented: `switch`, `cont.bind`, `resume_throw`, `resume_throw_ref`,
// cont types with parameters, cont types with results.
//
// ## Overview
//
// A 'continuation value' in wasm represents a suspended execution of wasm code
// that can be resumed. A resumed continuation can be passed params and either
// return results from terminating or else suspend to a handler with params.
// The suspended continuation is given to the handler and can be resumed again
// by passing it new values.
//
// Continuation values have linear semantics and are 'single-shot'. Once a
// continuation has been resumed, the old value will trap if it is used again.
// A suspend receives a logically fresh continuation value. Continuations don't
// support reference equality and so actually allocating a new object isn't
// required as long as the old value is no longer usable.
//
// A 'continuation value' refers to the single-shot suspended wasm value. It
// is implemented with wasm::ContObject, which is a GC thing.
//
// A 'continuation stack' refers to the underlying execution stack. It is
// implemented with wasm::ContStack, which is a UniquePtr to an mmaped
// allocation.
//
// These are two different things and the precise term should be used even if
// it's verbose. 'continuation' alone can be used where it's unambiguous.
//
// ## Single Shot Implementation
//
// Resuming a ContObject destructures the inner ContStack and leaves the object
// with no stack. Any future resume of the object will trap. The ContStack
// is then moved to be owned by a Handlers struct on the stack.
//
// Suspending a ContStack will allocate a new ContObject and move the ContStack
// from the Handlers to the object.
//
// A future optimization can remove the allocation of a ContObject for every
// suspend by instead turning a wasm continuation value into a fat pointer pair
// of (wasm::ContStack*, uint64_t: generation).
//
// Every resume checks if the fat pointer's generation matches a mutable
// generation field on the ContStack. If they match, the operation succeeds
// and increments the generation field. If they don't match then there is a
// trap.
//
// ## Suspend Handlers
//
// A `suspend $tag` instruction generates a suspend effect which is caught by a
// `(on $tag $label)` suspend handler that is pushed by a `resume` instruction.
//
// `resume` pushes the wasm::Handlers variable-sized type onto the top of the
// stack before jumping to the suspended continuation. Handlers contains an
// inline array of wasm::Handler which contains the $tag and $label. The
// resumed continuation stack is linked to the pushed handlers through the
// 'handlers_' field.
//
// `suspend` performs a linear search through the handler chain to find a
// matching suspend handler for the given tag instance.
//
// ## Nested continuation stacks
//
// An active continuation stack can resume another continuation, which can then
// resume another continuation. This creates a stack of continuation stacks.
//
// A `suspend` effect can be caught by a handler on an ancestor stack that is
// not the direct parent of the current stack. This causes the stack of
// continuation stacks from [handler->child, currentStack] to be suspended into
// a continuation value.
//
// The base of the suspended stacks is the 'resume base' and the top is the
// 'resume target'. When a continuation value is resumed, the resume base is
// linked to the new handlers `child` field, and the resume target is jumped to
// continue execution.
//
// ## Active Stack Linkage
//
// When continuations are active, their stacks are owned through the
// Handlers::child field that lives on the stack that executed `resume`.
//
// A continuation stack has a non-owning reference to the parent handler that
// resumed the stack. This forms a handlers chain which is used when searching
// for a matching handler.
//
//   Main Stack                 ContStack $A                  ContStack $B
//                       ┌────►┌───────────────┐       ┌────►┌───────────────┐
//                       │  ┌──┤  .handlers_   │       │  ┌──┤  .handlers_   │
//  ┌───────────────┐    │  │  ├───────────────┤       │  │  ├───────────────┤
//  │  ...frames    │    │  │  │  ...frames    │       │  │  │  ...frames    │
//  ├───────────────┤    │  │  ├───────────────┤       │  │  │  (executing)  │
//  │  Handlers $0  │◄───│──┘  │  Handlers $1  │◄──────│──┘  └───────────────┘
//  │  .self = null │    │     │  .self = $A   │       │
//  │  .child ──────┼────┘     │  .child ──────┼───────┘
//  └───────────────┘          └───────────────┘
//
//  wasm::Context
//    baseHandlers_ ──────► Handlers $0
//    currentStack_ ──────────────────────────────────► ContStack $B
//
// Wasm exits from JIT code dynamically switch to the main stack if they're
// executing on a continuation stack (see "Continuation Safe" for rationale).
// The VM can then re-enter wasm code, which could then resume a continuation.
//
// To support this, the dynamic switch saves and restores the most recent
// wasm::Context baseHandlers_ and currentStack_ fields. A handler search will
// always terminate at the most recent baseHandlers_ and never cross
// VM/JS/embedder code.
//
// ## Suspended Stack Linking
//
// A suspended continuation is owned by a ContObject. The 'resume base' is
// the outermost ContStack, stored in the ContObject. The 'resume target' is
// the innermost stack that was executing when `suspend` was called. They
// may be the same stack (simple case) or different stacks (nested conts).
//
// ContObject
//    resumeBase
//      │
//      │             ┌────────────────────────────────────────────────────────────┐
//      ▼             │                                                            │
//  ContStack $A      │  ┌───►ContStack $B        ┌────►ContStack $C (innermost)   │
//  ┌───────────────┐ │  │    ┌───────────────┐   │     ┌───────────────┐          │
//  │               │ │  │    │               │   │     │               │          │
//  │ resumeTarget_─┼─┘  │    │               │   │     │               │          │
//  ├───────────────┤    │    ├───────────────┤   │     ├───────────────┤          │
//  │ Frames...     │    │    │ Frames...     │   │     │ Frames...     │          │
//  ├───────────────┤    │    ├───────────────┤   │     ├───────────────┤          │
//  │  Handlers     │    │    │  Handlers     │   │     │               │          │
//  │  .child ──────┼────┘    │  .child ──────┼───┘     │               │          │
//  │               │         │               │         │ SwitchTarget ◄┼──────────┘
//  └───────────────┘         └───────────────┘         └───────────────┘
//
//
// ## Continuation Stack Layout
//
// Each continuation stack is a single contiguous allocation. The regions are
// laid out in physical memory order (low to high address):
//
//   allocationStart ────► ┌──────────────────┐  (physical low address)
//                         │                  │
//                         │  Top Guard Page  │  (protected)
//                         │                  │
// stackLimitForSystem_ ──►├──────────────────┤ ◄── logical top of the stack
//                         │                  │
//                         │  Red Zone        │  (ContRedZoneSize, accessible)
//                         │                  │
//    stackLimitForJit_ ──►├──────────────────┤
//                         │                  │
//                         │  JIT Stack       │
//                         │                  │
//                         │         ▲        │
//                         │   grows |        │  (ContJitStackSize, accessible)
//                         │         |        │
//                         │                  │
//                         │  Base Frame      │ ◄── logical base of the stack
//        stackBase_ ─────►├──────────────────┤
//                         │                  │
//                         │ Bottom Guard Page│  (protected, catches underflow)
//                         │                  │
//                         ├──────────────────┤
//                         │ ContStack header │  (wasm::ContStack, accessible)
//                         │                  │
//                         │                  │
//      allocationEnd ────►└──────────────────┘  (physical high address)
//
// The JIT stack grows downward from stackBase_ toward the top guard page.
// The base frame is placed at the high-address end of the JIT stack, at the
// logical bottom of the execution stack.
//
// The ContStack and base frame are at a known-constant address from each other
// and can be converted between each other.
//
// The red zone is accessible but is not used by JIT code. OS signal handlers
// and VM code that may run on this stack can use the red zone as scratch
// space, so there is always room for them to run without overflowing into
// the top guard page.
//
// stackLimitForSystem_ marks the boundary below which even system code must
// not go.
//
// ## "Continuation Safe" Code
//
// Only wasm JIT code (functions and stubs) are safe to run on a continuation
// stack. This code is aware that there are different stack limits and has
// integration with the GC to handle suspend/resume of these stacks.
//
// C++ VM code could only run these stacks if it:
//  1. Doesn't trigger GC (i.e. no GC allocations)
//  2. Doesn't need frame iteration
//  3. Doesn't re-enter into wasm code (which could have it be captured in a
//     suspended continuation and violate normal stack discipline).
//  4. Doesn't enter into JS or embedder code (careful of callbacks!)
//
// In the future code that meets these requirements could be allowed to run on
// a continuation stack.
//
// ## Dynamic Switch to Main Stack
//
// Because of above, on all exits from Wasm JIT code we need a dynamic switch
// from a continuation stack to the main stack. This is implemented via
// GenerateExitPrologueMainStackSwitch and GenerateExitEpilogueMainStackReturn.
//
// VM code can assume that it's on the main stack, and currentStack_ and
// baseHandlers_ are null.
//
// Once we are back on the main stack, we are safe to re-enter wasm code, which
// could then possibly resume and enter a new continuation. This can lead to
// arbitrary interleavings of continuations and the main stack as below:
//
//
//   Main Stack                 ContStack $A                  ContStack $B
//                       ┌────►┌───────────────┐       ┌────►┌───────────────┐
//                       │  ┌──┤  .handlers_   │       │  ┌──┤  .handlers_   │
//  ┌───────────────┐    │  │  ├───────────────┤       │  │  ├───────────────┤
//  │  ...frames    │    │  │  │  ...frames    │       │  │  │  ...frames    │
//  ├───────────────┤    │  │  ├───────────────┤       │  │  │               │
//  │  Handlers $0  │◄───│──┘  │  Handlers $1  │◄──────│──┘  │               │
//  │  .self = null │    │     │  .self = $A   │       │     │               │
//  │  .child ──────┼────┘     │  .child ──────┼───────┘     │ Exit Frame    │
//  ├───────────────┤          └───────────────┘             └──┬────────────┘
//  │ ...frames     │◄──┐                                       │
//  │               │   │                                       │
//  │               │   └───────────────────────────────────────┘
//  │               │
//  │               │           ContStack $C
//  │  Handlers $3  │       ┌─►┌───────────────┐
//  │  .self = null │       │  │  .handlers_   │
//  │  .child ──────┼───────┘  ├───────────────┤
//  └───────────────┘          │  ...frames    │
//                             └───────────────┘
//
// ## Continuation Base Frame Stubs
//
// Resuming a continuation can pass params to it. Two things create a dillema
// for what ABI to use for passing params:
//
// 1. `cont.new` creates a continuation with an initial funcref that should be
// the first code that executes when the continuation is resumed for the first
// time. The params to the funcref are the initial params for the resume.
//
// 2. `cont.bind` consume a continuation and partially applies certain params.
//
// Optimally for (1) we would pass resume params using the wasm function call
// ABI. This would let us pass values through registers, and also implement the
// intial resume by storing a pointer to the function's prologue.
//
// But (2) means that a continuation value may have some params already
// partially applied. We cannot do that if some params are passed via register.
//
// We therefore need to pass all resume params through stack slots. This lets
// the partially applied values be stored to stack locations.
//
// But then how does the initial resume actually call into the given funcref?
//
// We implement this using a 'base frame stub' (see GenerateContBaseFrameStub).
// This stub is generic for any function type and adapts between the resume ABI
// and the call_ref ABI by loading the params from the stack slots and calling
// the initial funcref.
//
// ## GC Tracing and Barriers
//
// Wasm frames on continuation stacks can hold GC-managed objects (anyref,
// externref, etc.) in stack slots. Wasm code does not emit write barriers
// when storing values to the stack, relying instead on the GC to trace the
// stack during root marking.
//
// We don't need a post-write barrier for stack slots because we do root marking
// during minor GC's and so the stack slots don't need to be added to the store
// buffer.
//
// We also don't need a pre-write barrier for stack slots because we do root
// marking before possibly yielding to the mutator, and so we have snapshot at
// the beginning for the stack slots.
//
// This all works fine for active continuation stacks because
// TraceJitActivations and wasm::FrameIter has support for tracing through the
// active continuation stacks transparently.
//
// The problem is suspended continuations, which are owned by ContObject and
// do not get traced during root marking and therefore the above do not apply
// to it.
//
// For incremental GC, we use the set of all continuation stacks on
// wasm::Context and trace all suspended stacks. This treats every suspended
// continuation stack as a potential nursery root.
//
// The tricky case is incremental GC.
//
// The solution is a 'resume barrier': before every `resume` instruction,
// a barrier is emitted. If an incremental GC is in progress, this barrier
// traces the suspended stack immediately before resuming it. This ensures all
// objects reachable from the stack are marked before wasm code runs on the
// stack again and can create new unbarriered references.
//
// ## Exception Unwinding
//
// A thrown exception is an effect just like a suspend, but is implemented
// entirely differently. Exceptions unwind the stack using
// wasm::HandleExceptionWasm and wasm::GenerateJumpToCatchHandler.
//
// Unwinding across an active continuation stack frees it. Unwinding into a
// continuation stack causes a stack switch to happen when the unwinder
// returns (the unwinder always is on the main stack).
//
// ## Windows TIB StackLimit and StackBase
//
// On Windows we use vectored exception handling to implement wasm traps. On
// Windows 64 this calls into RtlGuardIsValidStackPointer before our handler
// can run and fails if SP is not within the stack base and limits of the TIB.
//
// StackTarget contains the appropriate stack base and limit to use for those
// fields and switches to a stack will update the fields on the TIB. For a
// continuation stack we set it to the whole range of stack memory. For the
// main stack we set it to the current TIB stack fields at startup.
//
// Unfortunately it's not that simple for the main stack. It appears that the
// TIB StackLimit (and maybe StackBase?) fields can change over time, possibly
// from lazy commit of new stack memory. This means that our cached value can
// become incorrect.
//
// We handle this by refreshing the cached TIB fields whenever we leave the
// main stack to go to a continuation stack.
//   1. When resuming a continuation from the main stack
//   2. When returning from a dynamic 'switch to main' function call
//
// We also must refresh the cached TIB fields when jumping to a wasm catch
// handler, as the jump to catch handler performs a stack switch which needs
// to see the latest values.

/* clang-format on */

static_assert(JS_STACK_GROWTH_DIRECTION < 0,
              "Stack switching is implemented only for native stacks that "
              "grows down");
static const size_t ContStackAlignment = 16;
static const size_t ContStackTopGuardPages = 1;
static const size_t ContStackBottomGuardPages = 1;

void ContStackDeleter::operator()(const ContStack* cont) {
  ContStack::free(cont);
}

void ContStack::free(const ContStack* stack) {
  void* allocation = stack->allocation_;
  size_t allocationSize = stack->allocationSize_;
  stack->~ContStack();
  gc::UnmapPages(allocation, allocationSize);
}

UniqueContStack ContStack::allocate(JSContext* cx,
                                    Handle<ContObject*> continuation,
                                    Handle<JSFunction*> target,
                                    void* contBaseFrameStub) {
  MOZ_RELEASE_ASSERT(target->isWasm());

  // Compute the size of all the regions of our allocation. See the SMDOC above
  // for the memory layout. This must be kept in sync with
  // ContStack::offsetOfBaseFrame.
  size_t pageSize = gc::SystemPageSize();
  size_t topGuardPageSize = ContStackTopGuardPages * pageSize;
  size_t bottomGuardPageSize = ContStackBottomGuardPages * pageSize;
  size_t headerSize = RoundUp(sizeof(ContStack), pageSize);
  size_t allocationSize = topGuardPageSize + ContRedZoneSize +
                          ContJitStackSize + bottomGuardPageSize + headerSize;

  // Allocate all the memory at once.
  void* allocation = gc::MapAlignedPages(allocationSize, ContStackAlignment);
  if (!allocation) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  // Lay out the regions sequentially in physical order.
  uintptr_t allocationStart = reinterpret_cast<uintptr_t>(allocation);
  mozilla::DebugOnly<uintptr_t> allocationEnd =
      allocationStart + allocationSize;

  uintptr_t topGuardPagePhysicalStart = allocationStart;
  uintptr_t topGuardPagePhysicalEnd =
      topGuardPagePhysicalStart + topGuardPageSize;

  uintptr_t redZonePhysicalStart = topGuardPagePhysicalEnd;
  uintptr_t redZonePhysicalEnd = redZonePhysicalStart + ContRedZoneSize;

  uintptr_t jitStackPhysicalStart = redZonePhysicalEnd;
  uintptr_t jitStackPhysicalEnd = jitStackPhysicalStart + ContJitStackSize;

  uintptr_t bottomGuardPagePhysicalStart = jitStackPhysicalEnd;
  uintptr_t bottomGuardPagePhysicalEnd =
      bottomGuardPagePhysicalStart + bottomGuardPageSize;

  uintptr_t headerPhysicalStart = bottomGuardPagePhysicalEnd;
  mozilla::DebugOnly<uintptr_t> headerPhysicalEnd =
      headerPhysicalStart + headerSize;

  // All regions are accounted for.
  MOZ_ASSERT(headerPhysicalEnd == allocationEnd);
  // The header is aligned to store the continuation stack header.
  MOZ_ASSERT(headerPhysicalStart % alignof(wasm::ContStack) == 0);
  // The logical base of the jit stack is aligned.
  MOZ_ASSERT(jitStackPhysicalEnd % jit::WasmStackAlignment == 0);
  // It should not be possible to confuse this stack with the system
  // stack.
  MOZ_ASSERT(!cx->stackContainsAddress(allocationStart,
                                       JS::StackKind::StackForSystemCode));
  MOZ_ASSERT(!cx->stackContainsAddress(allocationEnd,
                                       JS::StackKind::StackForSystemCode));

  // Protect the top and bottom guard pages.
  gc::ProtectPages(reinterpret_cast<char*>(topGuardPagePhysicalStart),
                   topGuardPageSize);
  gc::ProtectPages(reinterpret_cast<char*>(bottomGuardPagePhysicalStart),
                   bottomGuardPageSize);

  // Ensure everything after this point is infallible so that the destructor
  // doesn't see partially initialized state.
  char* headerAddress = reinterpret_cast<char*>(headerPhysicalStart);
  UniqueContStack stack = UniqueContStack(new (headerAddress) ContStack());

  // Store the original allocation for use when we're freed.
  stack->allocation_ = allocation;
  stack->allocationSize_ = allocationSize;

  // Store the stack base and limits.
  stack->stackBase_ = jitStackPhysicalEnd;
  stack->stackLimitForSystem_ = redZonePhysicalStart;
  stack->stackLimitForJit_ = jitStackPhysicalStart;

  // Initialize the stack target.
  stack->target_.stack = stack.get();
  stack->target_.jitLimit = stack->stackLimitForJit_;
#  if defined(_WIN32)
  stack->target_.tibStackBase = reinterpret_cast<void*>(stack->stackBase_);
  stack->target_.tibStackLimit =
      reinterpret_cast<void*>(stack->stackLimitForSystem_);
#  endif

  // Initialize the base frame to zero.
  FrameWithInstances* baseFrame = stack->baseFrame();
  memset(baseFrame, 0, sizeof(wasm::FrameWithInstances));

  // Initialize the initial resume target to point at the base frame.
  stack->initialResumeTarget_.framePointer = baseFrame;
  stack->initialResumeTarget_.stackPointer = baseFrame;
  stack->initialResumeTarget_.resumePC = contBaseFrameStub;
  stack->initialResumeTarget_.instance = &target->wasmInstance();
  stack->initialResumeTarget_.stack = &stack->target_;

  // The stack was aligned before the initial frame was pushed.
  MOZ_ASSERT((reinterpret_cast<uintptr_t>(baseFrame) + sizeof(wasm::Frame)) %
                 jit::WasmStackAlignment ==
             0);

  // Initialize the initial resume callee.
  stack->initialResumeCallee_ = target;

  // Initialize the handlers and resume target. We start out ready to be
  // resumed.
  stack->handlers_ = nullptr;
  stack->resumeTarget_ = &stack->initialResumeTarget_;

  return stack;
}

void ContStack::unwind(JSContext* cx, wasm::Handlers* handlers) {
  // There is a child of handlers that is active, which we will detach.
  MOZ_RELEASE_ASSERT(handlers->child);
  MOZ_RELEASE_ASSERT(!handlers->child->canResume());

  // Detach the stack from the handlers.
  handlers->child->handlers_ = nullptr;
  ContStack::unregisterAndFree(cx, std::move(handlers->child));
}

void ContStack::freeSuspended(JSContext* cx, UniqueContStack resumeBase) {
  // We must be suspended, which means we have no handlers and have a resume
  // target.
  MOZ_RELEASE_ASSERT(!resumeBase->handlers());
  MOZ_RELEASE_ASSERT(resumeBase->canResume());

  // Unwind all the handlers starting at the resume target until we reach back
  // to the resume base. This will free all the child continuations of the
  // resume base.
  for (wasm::Handlers* handlers = resumeBase->resumeTargetStack()->handlers();
       handlers != nullptr; handlers = handlers->self->handlers()) {
    MOZ_RELEASE_ASSERT(handlers->child && handlers->child != resumeBase);
    ContStack::unwind(cx, handlers);
    MOZ_ASSERT(!handlers->child);
  }

  // Now we just need to free the resume base.
  ContStack::unregisterAndFree(cx, std::move(resumeBase));
}

void ContStack::unregisterAndFree(JSContext* cx, UniqueContStack stack) {
  stack->unregisterSelf(cx);
  // Stack will be destroyed when it goes out of scope.
}

ContStack::~ContStack() { MOZ_RELEASE_ASSERT(registeredIndex_.isNothing()); }

[[nodiscard]] bool ContStack::registerSelf(JSContext* cx) {
  MOZ_RELEASE_ASSERT(registeredIndex_.isNothing());
  size_t index = cx->wasm().stacks_.length();
  if (!cx->wasm().stacks_.append(this)) {
    return false;
  }
  registeredIndex_ = mozilla::Some(index);
  return true;
}

void ContStack::unregisterSelf(JSContext* cx) {
  MOZ_RELEASE_ASSERT(registeredIndex_.isSome());
  size_t index = *registeredIndex_;
  ContStackVector& stacks = cx->wasm().stacks_;

  // Our index is still correct in the stacks vector.
  MOZ_RELEASE_ASSERT(stacks[index] == this);

  // Swap ourselves to the end of the vector, if we're not there already.
  // Be sure to update the index of the stack we moved. This code is
  // technically correct in length=1 case, but for clarity we skip it.
  if (stacks.length() != 1) {
    std::swap(stacks[index], stacks.back());
    stacks[index]->registeredIndex_ = mozilla::Some(index);
  }

  // Remove ourselves from the back, and clear our index.
  stacks.popBack();
  registeredIndex_ = mozilla::Nothing();
}

void ContStack::traceSuspended(JSTracer* trc) {
  MOZ_RELEASE_ASSERT(canResume());

  WasmFrameIter iter = WasmFrameIter(
      resumeTarget_->instance,
      static_cast<FrameWithInstances*>(resumeTarget_->framePointer),
      resumeTarget_->resumePC);

  // If the iter is done, then we're a stack that's never been resumed. We just
  // need to trace our fields and return.
  if (iter.done()) {
    MOZ_RELEASE_ASSERT(isInitial());
    traceFields(trc);
    return;
  }

  // The resume target is currently suspended on a stack switch.
  MOZ_RELEASE_ASSERT(iter.currentFrameStackSwitched());
  MOZ_RELEASE_ASSERT(iter.contStack() &&
                     iter.contStack() == resumeTarget_->stack->stack);

  // We trace frames until we reach our own base frame.
  uintptr_t highestByteVisitedInPrevWasmFrame = 0;
  while (true) {
    MOZ_RELEASE_ASSERT(!iter.done());

    if (iter.currentFrameStackSwitched()) {
      // If we've switched stacks, trace the new stack's fields.
      iter.contStack()->traceFields(trc);
      // Reset the highest byte assertion.
      highestByteVisitedInPrevWasmFrame = 0;
    }

    uint8_t* nextPC = iter.resumePCinCurrentFrame();
    Instance* instance = iter.instance();
    TraceInstanceEdge(trc, instance, "WasmFrameIter instance");
    highestByteVisitedInPrevWasmFrame = instance->traceFrame(
        trc, iter, nextPC, highestByteVisitedInPrevWasmFrame);

    if (iter.frame()->wasmCaller() == baseFrame()) {
      break;
    }
    ++iter;
  }
}

void ContStack::updateSuspendedForMovingGC(Nursery& nursery) {
  MOZ_RELEASE_ASSERT(canResume());

  WasmFrameIter iter = WasmFrameIter(
      resumeTarget_->instance,
      static_cast<FrameWithInstances*>(resumeTarget_->framePointer),
      resumeTarget_->resumePC);

  // If the iter is done, then we're a stack that's never been resumed.
  if (iter.done()) {
    MOZ_RELEASE_ASSERT(isInitial());
    return;
  }

  // The resume target is currently suspended on a stack switch.
  MOZ_RELEASE_ASSERT(iter.currentFrameStackSwitched());
  MOZ_RELEASE_ASSERT(iter.contStack() &&
                     iter.contStack() == resumeTarget_->stack->stack);

  // We trace frames until we reach our own base frame.
  while (true) {
    MOZ_RELEASE_ASSERT(!iter.done());
    iter.instance()->updateFrameForMovingGC(iter, iter.resumePCinCurrentFrame(),
                                            nursery);

    if (iter.frame()->wasmCaller() == baseFrame()) {
      break;
    }
    ++iter;
  }
}

void SwitchTarget::trace(JSTracer* trc) const {
  if (instance) {
    TraceInstanceEdge(trc, instance, "switch target instance");
  }
}

void Handlers::trace(JSTracer* trc) const {
  returnTarget.trace(trc);
  for (uint32_t i = 0; i < numHandlers; i++) {
    TraceManuallyBarrieredEdge(trc, &((Handler*)handler(i))->tag,
                               "handler tag");
  }
}

void ContStack::traceFields(JSTracer* trc) {
  // Trace the initial resume state.
  TraceEdge(trc, &initialResumeCallee_, "base frame callee");
  initialResumeTarget_.trace(trc);

  // This will trace our parent continuation/stack, which will trace their
  // internal fields.
  if (handlers_) {
    handlers_->trace(trc);
  }
}

int32_t ContStack::offsetOfBaseFrame() {
  // This must be kept in sync with ContStack::allocate.
  size_t bottomGuardPageSize = ContStackBottomGuardPages * gc::SystemPageSize();
  size_t preFrameFields =
      AlignBytes(wasm::FrameWithInstances::sizeOfInstanceFieldsAndShadowStack(),
                 jit::WasmStackAlignment);
  size_t sizeOfBaseFrame = sizeof(wasm::Frame);
  return -static_cast<int32_t>(bottomGuardPageSize + preFrameFields +
                               sizeOfBaseFrame);
}

int32_t ContStack::offsetOfBaseFrameFP() {
  return offsetOfBaseFrame() +
         static_cast<int32_t>(FrameWithInstances::callerFPOffset());
}

ContObject* ContObject::create(JSContext* cx, Handle<JSFunction*> target,
                               void* contBaseFrameStub) {
  Rooted<ContObject*> cont(cx, NewBuiltinClassInstance<ContObject>(cx));
  if (!cont) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  UniqueContStack stack(
      ContStack::allocate(cx, cont, target, contBaseFrameStub));
  if (!stack || !stack->registerSelf(cx)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  MOZ_ASSERT(stack->canResume());
  cont->initFixedSlot(ResumeBaseSlot, JS::PrivateValue(stack.release()));

  return cont;
}

ContObject* ContObject::createEmpty(JSContext* cx) {
  Rooted<ContObject*> cont(cx, NewBuiltinClassInstance<ContObject>(cx));
  if (!cont) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  MOZ_ASSERT(!cont->resumeBase());
  return cont;
}

// We must foreground finalize so that we can access the JSContext from
// finalize.
const JSClass ContObject::class_ = {
    "ContObject",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount) | JSCLASS_FOREGROUND_FINALIZE,
    &ContObject::classOps_,
    nullptr,
    &ContObject::classExt_,
};

const JSClassOps ContObject::classOps_ = {
    nullptr,   // addProperty
    nullptr,   // delProperty
    nullptr,   // enumerate
    nullptr,   // newEnumerate
    nullptr,   // resolve
    nullptr,   // mayResolve
    finalize,  // finalize
    nullptr,   // call
    nullptr,   // construct
    trace,     // trace
};

const ClassExtension ContObject::classExt_ = {};

/* static */
void ContObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  JSContext* cx = gcx->runtimeFromAnyThread()->mainContextFromAnyThread();
  ContObject& cont = obj->as<ContObject>();

  // Free the resume base if we have any.
  if (UniqueContStack resumeBase = cont.takeResumeBase()) {
    // We are foreground finalized and can grab the context.
    ContStack::freeSuspended(cx, std::move(resumeBase));
  }
}

/* static */
void ContObject::trace(JSTracer* trc, JSObject* obj) {
  // Minor GC's trace stacks directly unconditionally in TraceJitActivations.
  // We don't need to trace here then.
  if (trc->isTenuringTracer()) {
    return;
  }

  ContObject& cont = obj->as<ContObject>();
  ContStack* resumeBase = cont.resumeBase();
  if (resumeBase) {
    MOZ_RELEASE_ASSERT(resumeBase->canResume());
    resumeBase->traceSuspended(trc);
  }
}

// Updates the JSContext to reflect that we are now running on the stack
// described by `stackTarget`. Sets currentStack, stackLimit, and on Win32
// refreshes the TIB stack bounds.
//
//   cx.wasm.currentStack = stackTarget.stack
//   if stackTarget.stack == null:
//     ;; entering the main stack, clear baseHandlers
//     cx.wasm.baseHandlers = null
//   cx.wasm.stackLimit = stackTarget.jitLimit
//
//   ;; Win32 only:
//   tib.StackBase  = stackTarget.tibStackBase
//   tib.StackLimit = stackTarget.tibStackLimit
//
// Clobbers cx on Win32.
void EmitEnterStackTarget(MacroAssembler& masm, Register cx,
                          Register stackTarget, Register scratch) {
  // Set the Context::currentStack.
  masm.loadPtr(Address(stackTarget, offsetof(wasm::StackTarget, stack)),
               scratch);
  masm.storePtr(scratch,
                Address(cx, JSContext::offsetOfWasm() +
                                wasm::Context::offsetOfCurrentStack()));

  // Clear Context::baseHandlers when entering the main stack to maintain
  // the invariant that VM code sees null currentStack/baseHandlers.
  Label enteringContStack;
  masm.branchTestPtr(Assembler::NonZero, scratch, scratch, &enteringContStack);
  masm.storePtr(ImmWord(0),
                Address(cx, JSContext::offsetOfWasm() +
                                wasm::Context::offsetOfBaseHandlers()));
  masm.bind(&enteringContStack);

  // Set the Context::stackLimit
  masm.loadPtr(Address(stackTarget, offsetof(wasm::StackTarget, jitLimit)),
               scratch);
  masm.storePtr(scratch, Address(cx, JSContext::offsetOfWasm() +
                                         wasm::Context::offsetOfStackLimit()));

  // Update the Win32 TIB StackBase and StackLimit fields. This code is
  // really register constrained and would benefit if we could use the Win32
  // TIB directly through its segment register in masm.
  //
  // NOTE: cx will be clobbered here.
#  ifdef _WIN32
  // Load the TIB into cx.
  masm.loadPtr(
      Address(cx, JSContext::offsetOfWasm() + wasm::Context::offsetOfTib()),
      cx);

  masm.loadPtr(Address(stackTarget, offsetof(wasm::StackTarget, tibStackBase)),
               scratch);
  masm.storePtr(scratch, Address(cx, offsetof(_NT_TIB, StackBase)));

  masm.loadPtr(Address(stackTarget, offsetof(wasm::StackTarget, tibStackLimit)),
               scratch);
  masm.storePtr(scratch, Address(cx, offsetof(_NT_TIB, StackLimit)));
#  endif
}

// Performs a full stack switch to the destination described by `switchTarget`.
// This is a one-way jump that does not return. The caller is responsible for
// having set up a SwitchTarget on the current stack so we can be switched
// back to later.
//
//   InstanceReg = switchTarget.instance
//   switchToRealm(InstanceReg)
//
//   EmitEnterStackTarget(cx, switchTarget.stack)
//
//   SP = switchTarget.stackPointer
//   FP = switchTarget.framePointer
//   pc' = switchTarget.resumePC
//   clobber all regs
//   jmp pc'
//
void EmitSwitchStack(MacroAssembler& masm, Register switchTarget,
                     Register scratch1, Register scratch2, Register scratch3) {
  // Switch to the destination instance from the switch target.
  masm.loadPtr(Address(switchTarget, offsetof(wasm::SwitchTarget, instance)),
               InstanceReg);
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());
#  ifdef WASM_HAS_HEAPREG
  MOZ_ASSERT(HeapReg != scratch1 && HeapReg != scratch2 && HeapReg != scratch3);
#  endif
  masm.switchToWasmInstanceRealm(scratch1, scratch2);
  // NOTE: InstanceReg (and HeapReg) is now live with the destination instance.

  // Load the cx from InstanceReg and stack target from the switch target, and
  // enter it. This will clobber scratch1, scratch2, scratch3.
  masm.loadPtr(Address(InstanceReg, wasm::Instance::offsetOfCx()), scratch1);
  masm.loadPtr(Address(switchTarget, offsetof(wasm::SwitchTarget, stack)),
               scratch2);
  EmitEnterStackTarget(masm, scratch1, scratch2, scratch3);

  // Switch the FP/SP/PC to the switch target.
  masm.loadStackPtr(
      Address(switchTarget, offsetof(wasm::SwitchTarget, stackPointer)));
#  ifdef JS_CODEGEN_ARM64
  if (sp.Is(masm.GetStackPointer64())) {
    // If we're using the real SP, initialize the PSP. We may be jumping to
    // something that uses it.
    masm.Mov(PseudoStackPointer64, vixl::sp);
  } else {
    // If we're using the PSP, sync the real SP. We may be jumping to something
    // that uses it.
    masm.Mov(vixl::sp, PseudoStackPointer64);
  }
#  endif
  masm.loadPtr(
      Address(switchTarget, offsetof(wasm::SwitchTarget, framePointer)),
      FramePointer);
  masm.loadPtr(Address(switchTarget, offsetof(wasm::SwitchTarget, resumePC)),
               scratch1);

  // As a hardening measure, clobber all registers before we jump.
  ClobberWasmRegsForLongJmp(masm, scratch1);

  masm.jump(scratch1);
}

void EmitClearSwitchTarget(MacroAssembler& masm, Register switchTarget) {
  masm.storePtr(ImmWord(0), Address(switchTarget, offsetof(wasm::SwitchTarget,
                                                           framePointer)));
  masm.storePtr(ImmWord(0), Address(switchTarget, offsetof(wasm::SwitchTarget,
                                                           stackPointer)));
  masm.storePtr(ImmWord(0),
                Address(switchTarget, offsetof(wasm::SwitchTarget, resumePC)));
  masm.storePtr(ImmWord(0), Address(switchTarget,
                                    offsetof(wasm::SwitchTarget, paramsArea)));
  masm.storePtr(ImmWord(0),
                Address(switchTarget, offsetof(wasm::SwitchTarget, instance)));
  masm.storePtr(ImmWord(0),
                Address(switchTarget, offsetof(wasm::SwitchTarget, stack)));
}

// Emits code to find the innermost handler matching `tag` by walking the
// handler chain from the current stack outward. Traps if no handler is found.
//
//   stack = cx.currentStack
//   if stack == null:
//     goto fail
//   handlers = stack.handlers
//   assert handlers != null
//
//   loop:
//     ;; scan the handlers array in this Handlers node
//     if handlers.numHandlers == 0:
//       goto next
//     for i in 0..handlers.numHandlers:
//       if handlers[i].tag == tag:
//         output = &handlers[i]
//         return
//
//   next:
//     ;; walk up to the parent Handlers via the owning ContStack
//     stack = handlers.self
//     if stack == null:
//       goto fail
//     handlers = stack.handlers
//     if handlers == null:
//       goto fail
//     goto loop
//
void EmitFindHandler(MacroAssembler& masm, Register instance, Register tag,
                     Register output, Register scratch1, Register scratch2,
                     Register scratch3, Register scratch4, Label* fail) {
  // stack = cx->currentStack
  masm.loadPtr(Address(instance, wasm::Instance::offsetOfCx()), scratch1);
  masm.loadPtr(Address(scratch1, JSContext::offsetOfWasm() +
                                     wasm::Context::offsetOfCurrentStack()),
               scratch1);

  // if (!stack):
  //     trap;
  masm.branchTestPtr(Assembler::Zero, scratch1, scratch1, fail);

  // handlers = stack->handlers_;
  masm.loadPtr(Address(scratch1, wasm::ContStack::offsetOfHandlers()),
               scratch1);

  Label isNotNull1;
  masm.branchTestPtr(Assembler::NonZero, scratch1, scratch1, &isNotNull1);
  masm.breakpoint();
  masm.bind(&isNotNull1);

  // Linear search over linked-list of wasm::Handlers to find the matching
  // wasm::Handler.

  Label outerHandlersLoop;
  Label innerHandlerLoop;
  Label exitInnerHandlerLoop;
  Label done;

  masm.nopAlign(CodeAlignment);
  masm.bind(&outerHandlersLoop);

  // if handlers.numHandlers == 0:
  //   continue;
  masm.load32(Address(scratch1, offsetof(wasm::Handlers, numHandlers)),
              scratch2);
  masm.branchTest32(Assembler::Zero, scratch2, scratch2, &exitInnerHandlerLoop);
  masm.assert32Compare(Assembler::LessThanOrEqual, scratch2,
                       Imm32(wasm::MaxHandlers));

  masm.computeEffectiveAddress(
      Address(scratch1, wasm::Handlers::offsetOfHandler(0)), scratch3);

  masm.nopAlign(CodeAlignment);
  masm.bind(&innerHandlerLoop);

  // if handler.tag == tag:
  //   break 'done
  masm.loadPtr(Address(scratch3, offsetof(wasm::Handler, tag)), scratch4);
  masm.branchPtr(Assembler::Equal, tag, scratch4, &done);

  masm.addPtr(Imm32(sizeof(wasm::Handler)), scratch3);
  masm.decBranchPtr(Assembler::NonZero, scratch2, Imm32(1), &innerHandlerLoop);

  masm.bind(&exitInnerHandlerLoop);

  // handlers := handlers.self->parent
  // if !handlers:
  //   trap;
  // continue;
  masm.loadPtr(Address(scratch1, offsetof(wasm::Handlers, self)), scratch1);
  masm.branchTestPtr(Assembler::Zero, scratch1, scratch1, fail);
  masm.loadPtr(Address(scratch1, wasm::ContStack::offsetOfHandlers()),
               scratch1);
  masm.branchTestPtr(Assembler::Zero, scratch1, scratch1, fail);
  masm.jump(&outerHandlersLoop);

  // Return the matched handler.
  masm.bind(&done);
  masm.movePtr(scratch3, output);
}

// Writes the fields of a SwitchTarget struct into the current stack frame.
//
// switchTargetFramePushed is the masm.framePushed() at the base of the
// SwitchTarget allocation. returnFramePushed is the framePushed value at the
// SP the resume target should restore.
//
// Clobbers scratch; preserves all other input registers.
static void EmitBuildSwitchTarget(MacroAssembler& masm,
                                  uint32_t switchTargetFramePushed,
                                  uint32_t returnFramePushed, Register instance,
                                  Register stackTarget, Register resumePC,
                                  Register scratch) {
  masm.storePtr(
      FramePointer,
      Address(FramePointer, -static_cast<int32_t>(switchTargetFramePushed) +
                                static_cast<int32_t>(offsetof(
                                    wasm::SwitchTarget, framePointer))));
  masm.computeEffectiveAddress(
      Address(FramePointer, -static_cast<int32_t>(returnFramePushed)), scratch);
  masm.storePtr(
      scratch,
      Address(FramePointer, -static_cast<int32_t>(switchTargetFramePushed) +
                                static_cast<int32_t>(offsetof(
                                    wasm::SwitchTarget, stackPointer))));
  masm.storePtr(
      resumePC,
      Address(FramePointer, -static_cast<int32_t>(switchTargetFramePushed) +
                                static_cast<int32_t>(
                                    offsetof(wasm::SwitchTarget, resumePC))));
  masm.storePtr(
      ImmWord(0),
      Address(FramePointer, -static_cast<int32_t>(switchTargetFramePushed) +
                                static_cast<int32_t>(
                                    offsetof(wasm::SwitchTarget, paramsArea))));
  masm.storePtr(
      instance,
      Address(FramePointer, -static_cast<int32_t>(switchTargetFramePushed) +
                                static_cast<int32_t>(
                                    offsetof(wasm::SwitchTarget, instance))));
  masm.storePtr(
      stackTarget,
      Address(FramePointer,
              -static_cast<int32_t>(switchTargetFramePushed) +
                  static_cast<int32_t>(offsetof(wasm::SwitchTarget, stack))));
}

// Emits code for `suspend`: switches from the current continuation stack back
// to the handler that installed the matching tag. Ownership of the current
// ContStack is transferred to the suspendedCont so it can be resumed later.
//
//   ;; pre-condition: we must be on a continuation stack and have found the
//   ;; target suspend `handler` using EmitFindHandlers.
//
//   currentStack = cx.currentStack
//   handlers = handler.handlers
//   resumeBase = handlers.child
//
//   ;; transfer resumeBase to suspendedCont so it can be resumed later
//   suspendedCont.resumeBase = resumeBase
//
//   ;; unlink resumeBase from the handler chain
//   handlers.child = null
//   resumeBase.baseFrame = {null, null, null}
//   resumeBase.handlers = null
//
//   ;; build a SwitchTarget on the stack so we can be resumed
//   sp -= sizeof(SwitchTarget)
//   switchTarget = BuildSwitchTarget(resumeLabel, &currentStack.stackTarget)
//
//   ;; link the resumeBase back to our resumeTarget
//   resumeBase.resumeTarget = switchTarget
//
//   ;; switch to the handler's SwitchTarget
//   EmitSwitchStack(&handler.target)
//
//   resumeLabel:                        ;; landed here when resumed
//     sp += sizeof(SwitchTarget)
//
void EmitSuspend(jit::MacroAssembler& masm, jit::Register instance,
                 jit::Register suspendedCont, jit::Register handler,
                 jit::Register scratch1, jit::Register scratch2,
                 jit::Register scratch3, const CallSiteDesc& callSiteDesc,
                 jit::CodeOffset* suspendCodeOffset,
                 uint32_t* suspendFramePushed) {
  // Load cx->currentStack into scratch1.
  masm.loadPtr(Address(instance, wasm::Instance::offsetOfCx()), scratch1);
  masm.loadPtr(Address(scratch1, JSContext::offsetOfWasm() +
                                     wasm::Context::offsetOfCurrentStack()),
               scratch1);

  // Load the containing handlers into scratch2.
  masm.loadPtr(Address(handler, offsetof(wasm::Handler, handlers)), scratch2);

  // Load the resume base into scratch3. This is the child of the target
  // handler.
  masm.loadPtr(Address(scratch2, offsetof(wasm::Handlers, child)), scratch3);

  // Store the resume base into the suspendedCont's stack slot.
  masm.storePrivateValue(
      scratch3, Address(suspendedCont, wasm::ContObject::offsetOfResumeBase()));
  Register scratch4 = suspendedCont;

  // Unlink the resume base and target handler from each other.
  masm.storePtr(ImmWord(0), Address(scratch2, offsetof(wasm::Handlers, child)));
  masm.storePtr(
      ImmWord(0),
      Address(scratch3, wasm::ContStack::offsetOfBaseFrame() +
                            static_cast<int32_t>(
                                wasm::FrameWithInstances::callerFPOffset())));
  masm.storePtr(
      ImmWord(0),
      Address(scratch3,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::returnAddressOffset())));
  masm.storePtr(
      ImmWord(0),
      Address(scratch3,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::callerInstanceOffset())));
  masm.storePtr(
      ImmWord(0),
      Address(scratch3,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::calleeInstanceOffset())));
  masm.storePtr(ImmWord(0),
                Address(scratch3, wasm::ContStack::offsetOfHandlers()));

  // scratch1 is still live with the current continuation's stack.
  // scratch3 is still live with the resume base.

  // Build the resume target for coming back here.
  CodeLabel resumeLabel;
  masm.reserveStack(sizeof(wasm::SwitchTarget));
  masm.assertStackAlignment(WasmStackAlignment);
  uint32_t switchTargetFramePushed = masm.framePushed();
  *suspendFramePushed = masm.framePushed();

  masm.storeStackPtr(
      Address(scratch3, wasm::ContStack::offsetOfResumeTarget()));

  // Load the stack target for the current continuation into scratch4
  masm.computeEffectiveAddress(
      Address(scratch1, wasm::ContStack::offsetOfStackTarget()), scratch4);
  // Move the resume address to scratch3
  masm.mov(&resumeLabel, scratch3);
  // Build the resume switch target
  EmitBuildSwitchTarget(masm, switchTargetFramePushed, *suspendFramePushed,
                        instance, scratch4, scratch3, scratch1);

  // Go to the target handler.
  masm.computeEffectiveAddress(
      Address(handler, offsetof(wasm::Handler, target)), scratch4);
  EmitSwitchStack(masm, scratch4, scratch1, scratch2, scratch3);
  MOZ_ASSERT(*suspendFramePushed == masm.framePushed());

  masm.wasmTrapInstruction();
  masm.bind(&resumeLabel);
  *suspendCodeOffset = *resumeLabel.target();
  masm.addCodeLabel(resumeLabel);
  masm.append(callSiteDesc, *resumeLabel.target());

  masm.freeStack(sizeof(wasm::SwitchTarget));
}

// Validates that a ContObject can be resumed:
//   1. It must be non-null
//   2. Have a non-null resume base
//
// Branches to fail if any check fails.
//
// Clobbers scratch1; preserves cont.
static void EmitCheckContIsResumable(MacroAssembler& masm, Register cont,
                                     Register scratch1, Label* fail) {
  // Trap if the continuation is null.
  masm.branchWasmAnyRefIsNull(true, cont, fail);

  // Trap if the continuation's resume base is null or undefined (the latter
  // means the continuation has already been completed and is no longer
  // resumable).
  masm.branchTestUndefined(
      Assembler::Equal, Address(cont, wasm::ContObject::offsetOfResumeBase()),
      fail);

  // Load the resume base.
  masm.loadPrivate(Address(cont, wasm::ContObject::offsetOfResumeBase()),
                   scratch1);

  // Assert if the resume base was not undefined, then it should be non-null.
  masm.assertPtrNonZero(scratch1);

  // Assert the resume base has a resume target.
  masm.assertPtrNonZero(
      Address(scratch1, wasm::ContStack::offsetOfResumeTarget()));

  // Assert the resume base has no handlers.
  masm.assertPtrZero(Address(scratch1, wasm::ContStack::offsetOfHandlers()));
}

// Reserves stack space for a Handlers struct and initializes its self pointer.
//
// If resuming from the main stack, sets wasm::Context::baseHandlers_ and
// refreshes the Win32 TIB limits on the main stack target.
//
// If resuming from a continuation stack, sets self to the current ContStack.
//
// Clobbers scratch2 and scratch3. On exit, scratch1 holds the address of the
// current stack's StackTarget.
static void EmitPushHandlers(MacroAssembler& masm, size_t sizeOfHandlers,
                             Register instance, Register scratch1,
                             Register scratch2, Register scratch3,
                             uint32_t* handlersFramePushed) {
  // Load cx into scratch3, and cx->currentStack into scratch1
  masm.loadPtr(Address(instance, wasm::Instance::offsetOfCx()), scratch3);
  masm.loadPtr(Address(scratch3, JSContext::offsetOfWasm() +
                                     wasm::Context::offsetOfCurrentStack()),
               scratch1);

  // Reserve all stack space up front, ensure we do this before we maybe save
  // the main SP.
  masm.reserveStack(sizeOfHandlers);
  *handlersFramePushed = masm.framePushed();
  MOZ_RELEASE_ASSERT((sizeOfHandlers) % WasmStackAlignment == 0);
  masm.assertStackAlignment(WasmStackAlignment);

  Label onMainStack;
  Label rejoin;
  masm.branchTestPtr(Assembler::Zero, scratch1, scratch1, &onMainStack);

  // Assert base handlers has been set by someone.
  masm.assertPtrNonZero(Address(
      scratch3,
      JSContext::offsetOfWasm() + wasm::Context::offsetOfBaseHandlers()));

  // Store ourself into handlers.
  masm.storePtr(scratch1, Address(masm.getStackPointer(),
                                  offsetof(wasm::Handlers, self)));

  // Load currentStack's stack target into scratch1 (clobbering itself)
  masm.computeEffectiveAddress(
      Address(scratch1, wasm::ContStack::offsetOfStackTarget()), scratch1);

  masm.jump(&rejoin);
  masm.bind(&onMainStack);

  // Refresh the Win32 TIB limits on wasm::Context with the latest values.
#  ifdef _WIN32
  masm.loadPtr(Address(scratch3, JSContext::offsetOfWasm() +
                                     wasm::Context::offsetOfTib()),
               scratch2);
  masm.loadPtr(Address(scratch2, offsetof(_NT_TIB, StackBase)), scratch1);
  masm.storePtr(
      scratch1,
      Address(scratch3, JSContext::offsetOfWasm() +
                            wasm::Context::offsetOfMainStackTarget() +
                            offsetof(wasm::StackTarget, tibStackBase)));
  masm.loadPtr(Address(scratch2, offsetof(_NT_TIB, StackLimit)), scratch1);
  masm.storePtr(
      scratch1,
      Address(scratch3, JSContext::offsetOfWasm() +
                            wasm::Context::offsetOfMainStackTarget() +
                            offsetof(wasm::StackTarget, tibStackLimit)));
#  endif

  // This will be the base handler on the main stack. Assert no one has set it.
  masm.assertPtrZero(Address(
      scratch3,
      JSContext::offsetOfWasm() + wasm::Context::offsetOfBaseHandlers()));

  // Set ourselves as the current base handler.
  masm.storeStackPtr(Address(
      scratch3,
      JSContext::offsetOfWasm() + wasm::Context::offsetOfBaseHandlers()));

  // Initialize our handler to have no parents.
  masm.storePtr(ImmWord(0), Address(masm.getStackPointer(),
                                    offsetof(wasm::Handlers, self)));

  // Load the address of the stack target for the main stack into scratch1.
  masm.computeEffectiveAddress(
      Address(scratch3, JSContext::offsetOfWasm() +
                            wasm::Context::offsetOfMainStackTarget()),
      scratch1);

  masm.bind(&rejoin);
}

// Initializes one Handler entry within the Handlers struct on the stack.
//
// Stores the tag object, the back-reference to the containing Handlers, and
// a SwitchTarget pointing at handlerLabel.
//
// If handlersParamsArea is valid, also stores the pointer to this handler's
// slice of the results area.
//
// Clobbers scratch2, scratch3; preserves instance, handlersParamsArea, and
// stackTarget.
static void EmitInitializeHandler(
    MacroAssembler& masm, uint32_t handlersFramePushed,
    uint32_t handlerFramePushed, uint32_t returnFramePushed,
    HandlerJitOffsets& handler, CodeLabel* handlerLabel, Register instance,
    Register handlersParamsArea, Register stackTarget, Register scratch2,
    Register scratch3) {
  // Load tag and store it
  size_t tagObjectOffset = wasm::Instance::offsetInData(
      handler.tagInstanceDataOffset + offsetof(wasm::TagInstanceData, object));
  masm.loadPtr(Address(instance, tagObjectOffset), scratch3);
  masm.storePtr(
      scratch3,
      Address(FramePointer,
              -static_cast<int32_t>(handlerFramePushed) +
                  static_cast<int32_t>(offsetof(wasm::Handler, tag))));

  // Store the back-reference to the containing wasm::Handlers*
  masm.computeEffectiveAddress(
      Address(FramePointer, -static_cast<int32_t>(handlersFramePushed)),
      scratch3);
  masm.storePtr(
      scratch3,
      Address(FramePointer,
              -static_cast<int32_t>(handlerFramePushed) +
                  static_cast<int32_t>(offsetof(wasm::Handler, handlers))));

  // Load the handler label address into scratch2
  masm.mov(handlerLabel, scratch2);

  // Build the switch target
  EmitBuildSwitchTarget(
      masm, handlerFramePushed - offsetof(wasm::Handler, target),
      returnFramePushed, instance, stackTarget, scratch2, scratch3);

  if (handlersParamsArea != Register::Invalid()) {
    masm.movePtr(handlersParamsArea, scratch2);
    masm.addPtr(Imm32(handler.resultsAreaOffset), scratch2);
    masm.storePtr(
        scratch2,
        Address(FramePointer,
                -static_cast<int32_t>(handlerFramePushed) +
                    static_cast<int32_t>(offsetof(wasm::Handler, target)) +
                    static_cast<int32_t>(
                        offsetof(wasm::SwitchTarget, paramsArea))));
  }
}

// Transfers ownership of the ContStack from cont to the Handlers struct on the
// stack, wires up the bidirectional Handlers <-> ContStack link, and loads
// the ContStack's resume target into resumeTarget while clearing it.
//
// On entry cont holds the ContObject.
// On exit cont is clobbered; resumeBase holds the ContStack*; resumeTarget
// holds the SwitchTarget* to jump to.
static void EmitActivateResumeBase(MacroAssembler& masm, Register instance,
                                   Register cont, Register resumeBase,
                                   Register resumeTarget, Register scratch3) {
  // Transfer ownership of the ContStack from cont to resumeBase.
  masm.loadPrivate(Address(cont, wasm::ContObject::offsetOfResumeBase()),
                   resumeBase);
  masm.storeValue(UndefinedValue(),
                  Address(cont, wasm::ContObject::offsetOfResumeBase()));

  // Wire up the bidirectional Handlers <-> ContStack link.
  masm.storePtr(resumeBase, Address(masm.getStackPointer(),
                                    offsetof(wasm::Handlers, child)));
  masm.storeStackPtr(Address(resumeBase, wasm::ContStack::offsetOfHandlers()));

  // Set the resume base's base frame to point back at the resume site.
  masm.storePtr(
      FramePointer,
      Address(resumeBase, wasm::ContStack::offsetOfBaseFrame() +
                              static_cast<int32_t>(
                                  wasm::FrameWithInstances::callerFPOffset())));
  masm.loadPtr(Address(masm.getStackPointer(),
                       offsetof(wasm::Handlers, returnTarget) +
                           offsetof(wasm::SwitchTarget, resumePC)),
               scratch3);
  masm.storePtr(
      scratch3,
      Address(resumeBase,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::returnAddressOffset())));
  masm.storePtr(
      instance,
      Address(resumeBase,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::callerInstanceOffset())));
  masm.storePtr(
      instance,
      Address(resumeBase,
              wasm::ContStack::offsetOfBaseFrame() +
                  static_cast<int32_t>(
                      wasm::FrameWithInstances::calleeInstanceOffset())));

  // Load and clear the resume target.
  masm.loadPtr(Address(resumeBase, wasm::ContStack::offsetOfResumeTarget()),
               resumeTarget);
  masm.storePtr(ImmWord(0),
                Address(resumeBase, wasm::ContStack::offsetOfResumeTarget()));
}

// Calls Instance::contUnwind to detach and free the ContStack that just
// returned normally through the Handlers struct pointed to by handlers.
//
// Clobbers all caller-saved registers; saves and restores instance/InstanceReg.
static void EmitCallContUnwind(MacroAssembler& masm, Register instance,
                               Register handlers) {
  MOZ_ASSERT(instance == InstanceReg);
  masm.Push(instance);
  int32_t framePushedAfterInstance = masm.framePushed();

  masm.setupWasmABICall(wasm::SymbolicAddress::ContUnwind);
  masm.passABIArg(instance);
  masm.passABIArg(handlers);
  int32_t instanceOffset = masm.framePushed() - framePushedAfterInstance;
  masm.callWithABI(wasm::BytecodeOffset(0), wasm::SymbolicAddress::ContUnwind,
                   mozilla::Some(instanceOffset), ABIType::General);

  masm.Pop(instance);
#  if JS_CODEGEN_ARM64
  masm.syncStackPtr();
#  endif
}

// Emits code for `resume`: switches from the current stack to the continuation
// stack stored in `cont`, installing the given suspend handlers. When the
// resumed continuation returns or suspends, control transfers back here.
//
// The codegen for this is complicated and has been broken down into some
// single-use helpers. This is the pseudo-code for what we're doing:
//
//   EmitCheckContIsResumable(cont):
//     if cont == null || cont.resumeBase == undefined:
//       goto fail
//
//   EmitPushHandlers():
//     sp -= sizeof(Handlers)
//     handlers = sp
//
//     if cx.currentStack == null:
//       ;; on main stack
//       cx.wasm.baseHandlers = sp
//       handlers.self = null
//       stackTarget = &cx.wasm.mainStackTarget
//     else:
//       ;; on continuation stack
//       handlers.self = cx.currentStack
//       stackTarget = &cx.currentStack.stackTarget
//
//   handlers.returnTarget = BuildSwitchTarget(returnLabel, stackTarget)
//   handlers.numHandlers = N
//   for i in 0..N:
//     EmitInitializeHandler(i):
//       handlers[i].tag = instance.tags[i]
//       handlers[i].handlers = &handlers
//       ;; the switch target for this handler will take us to a landing pad.
//       ;; Adjust the SP on the switch target so that it pops handlers
//       ;; automatically.
//       handlers[i].target = BuildSwitchTarget(handlerLandingPad[i],
//       stackTarget)
//
//   EmitActivateResumeBase(cont):
//     ;; take ownership of cont.resumeBase and link handlers to it
//     resumeBase = cont.resumeBase
//     cont.resumeBase = undefined
//     handlers.child = resumeBase
//
//     ;; link resumeBase to our handlers
//     resumeBase.handlers = handlers
//     resumeBase.baseFrame = {FP, returnPC, instance}
//
//     ;; take ownership of resumeBase.resumeTarget
//     resumeTarget = resumeBase.resumeTarget
//     resumeBase.resumeTarget = null
//
//   ;; the shared stack switch operation
//   EmitSwitchStack(resumeTarget)
//
//   ;; landing pad for suspending to a handler. the switch target was set up
//   ;; to set sp back to pop handlers already.
//   handlerLandingPad[i]:
//     jmp handlerLabels[i]
//
//   returnLabel:                          // landed here from normal return
//     ContUnwind(instance, &handlers)     // free the returned ContStack
//     sp += sizeof(Handlers)
//
void EmitResume(MacroAssembler& masm, Register instance, Register cont,
                Register handlersParamsArea, Register scratch1,
                Register scratch2, Register scratch3, Label* fail,
                mozilla::Span<HandlerJitOffsets> handlerOffsets,
                mozilla::Span<jit::Label*> handlerLabels,
                const wasm::CallSiteDesc& callSiteDesc,
                jit::CodeOffset* resumeCodeOffset,
                uint32_t* resumeFramePushed) {
  MOZ_ASSERT(handlerOffsets.size() == handlerLabels.size());
  size_t numHandlers = handlerOffsets.size();
  size_t sizeOfHandlers = wasm::Handlers::sizeOf(numHandlers);
  uint32_t handlersFramePushed = 0;
  CodeLabel returnLabel;
  // Initialize the suspend handlers. Be sure to resize upfront so the addresses
  // are stable.
  Vector<CodeLabel, 2, SystemAllocPolicy> handlerCodeLabels;
  if (!handlerCodeLabels.resize(numHandlers)) {
    masm.propagateOOM(false);
    return;
  }

  EmitCheckContIsResumable(masm, cont, scratch1, fail);
  EmitPushHandlers(masm, sizeOfHandlers, instance, scratch1, scratch2, scratch3,
                   &handlersFramePushed);
  // scratch1 has currentStack's stack target.

  // Initialize the returnTarget within the Handlers struct.
  masm.mov(&returnLabel, scratch2);
  EmitBuildSwitchTarget(
      masm, handlersFramePushed - offsetof(wasm::Handlers, returnTarget),
      handlersFramePushed, instance, scratch1, scratch2, scratch3);
  // scratch1 still has currentStack's stack target.

  masm.store32(
      Imm32(numHandlers),
      Address(masm.getStackPointer(), offsetof(wasm::Handlers, numHandlers)));
  for (uint32_t i = 0; i < numHandlers; i++) {
    uint32_t handlerFramePushed =
        handlersFramePushed - wasm::Handlers::offsetOfHandler(i);
    // Switching to a handler will pop all the handlers.
    uint32_t returnFramePushed = handlersFramePushed - sizeOfHandlers;
    EmitInitializeHandler(masm, handlersFramePushed, handlerFramePushed,
                          returnFramePushed, handlerOffsets[i],
                          &handlerCodeLabels[i], instance, handlersParamsArea,
                          scratch1, scratch2, scratch3);
  }
  // All scratches are free here.

  // Transfer ownership of the resume base from cont, link it to the Handlers
  // frame, and get the SwitchTarget to jump to. cont is dead after this.
  EmitActivateResumeBase(masm, instance, cont, scratch1, scratch2, scratch3);

  // Perform the stack switch, using cont as a scratch now that it's dead.
  EmitSwitchStack(masm, scratch2, scratch1, scratch3, cont);
  *resumeFramePushed = masm.framePushed();
  // wasm::Handlers are always at the top of the stack at a resume. This
  // ensures that %sp == &handlers. We store the most recent base handler
  // on wasm::Context, and can use the address of that as the most recent
  // main stack SP.
  MOZ_ASSERT(*resumeFramePushed == handlersFramePushed);

  for (uint32_t i = 0; i < numHandlers; i++) {
    masm.bind(&handlerCodeLabels[i]);
    masm.addCodeLabel(handlerCodeLabels[i]);
    // All registers are dead here, except for InstanceReg. Jump to our final
    // handler's label.
    masm.jump(handlerLabels[i]);
  }

  masm.wasmTrapInstruction();
  masm.bind(&returnLabel);
  masm.addCodeLabel(returnLabel);

  *resumeCodeOffset = *returnLabel.target();
  masm.append(callSiteDesc, *returnLabel.target());

  // All registers are dead here, except for InstanceReg.

  // We now need to free the stack that just returned. Pass the handlers (
  // currently still at the top of the stack) to a builtin to free it, then
  // pop the handlers from the stack.
  masm.moveStackPtrTo(scratch1);
  EmitCallContUnwind(masm, InstanceReg, scratch1);
  masm.freeStack(sizeOfHandlers);
}

}  // namespace js::wasm

#endif  // ENABLE_WASM_JSPI
