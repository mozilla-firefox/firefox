/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/JitActivation.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT, MOZ_RELEASE_ASSERT

#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, uint32_t
#include <utility>   // std::move

#include "debugger/DebugAPI.h"        // js::DebugAPI
#include "jit/Invalidation.h"         // js::jit::Invalidate
#include "jit/JSJitFrameIter.h"       // js::jit::InlineFrameIterator
#include "jit/RematerializedFrame.h"  // js::jit::RematerializedFrame
#include "js/AllocPolicy.h"           // js::ReportOutOfMemory
#include "vm/EnvironmentObject.h"     // js::DebugEnvironments
#include "vm/JSContext.h"             // JSContext
#include "vm/Realm.h"                 // js::AutoRealmUnchecked
#include "wasm/WasmCode.h"            // js::wasm::Code
#include "wasm/WasmConstants.h"       // js::wasm::Trap
#include "wasm/WasmFrameIter.h"  // js::wasm::{RegisterState,StartUnwinding,UnwindState}
#include "wasm/WasmInstance.h"  // js::wasm::Instance
#include "wasm/WasmProcess.h"   // js::wasm::LookupCode
#include "wasm/WasmSummarizeInsn.h"

#include "vm/Realm-inl.h"  // js::~AutoRealm

class JS_PUBLIC_API JSTracer;

js::jit::JitActivation::JitActivation(JSContext* cx)
    : Activation(cx, Jit),
      packedExitFP_(nullptr),
      encodedWasmExitReason_(0),
      prevJitActivation_(cx->jitActivation),
      ionRecovery_(cx),
      bailoutData_(nullptr),
      lastProfilingFrame_(nullptr),
      lastProfilingCallSite_(nullptr) {
  cx->jitActivation = this;
  registerProfiling();
}

js::jit::JitActivation::~JitActivation() {
  if (isProfiling()) {
    unregisterProfiling();
  }
  cx_->jitActivation = prevJitActivation_;

  // Recover instruction results must have been removed by either the bailout
  // code or the exception handler.
  MOZ_ASSERT(ionRecovery_.empty());

  // The BailoutFrameInfo should have unregistered itself from the
  // JitActivations.
  MOZ_ASSERT(!bailoutData_);

  // Traps get handled immediately.
  MOZ_ASSERT(!isWasmTrapping());

  // Rematerialized frames must have been removed by either the bailout code or
  // the exception handler.
  MOZ_ASSERT_IF(rematerializedFrames_, rematerializedFrames_->empty());
}

void js::jit::JitActivation::trace(JSTracer* trc) {
  traceCommon(trc);

  TraceJitFrames(trc, this);

  if (rematerializedFrames_) {
    for (auto iter = rematerializedFrames_->iter(); !iter.done(); iter.next()) {
      iter.get().value().trace(trc);
    }
  }

  for (RInstructionResults* it = ionRecovery_.begin(); it != ionRecovery_.end();
       it++) {
    it->trace(trc);
  }
}

void js::jit::JitActivation::setBailoutData(
    jit::BailoutFrameInfo* bailoutData) {
  MOZ_ASSERT(!bailoutData_);
  bailoutData_ = bailoutData;
}

void js::jit::JitActivation::cleanBailoutData() {
  MOZ_ASSERT(bailoutData_);
  bailoutData_ = nullptr;
}

void js::jit::JitActivation::removeRematerializedFrame(uint8_t* top) {
  if (!rematerializedFrames_) {
    return;
  }

  if (RematerializedFrameTable::Ptr p = rematerializedFrames_->lookup(top)) {
    rematerializedFrames_->remove(p);
  }
}

js::jit::RematerializedFrame* js::jit::JitActivation::getRematerializedFrame(
    JSContext* cx, const JSJitFrameIter& iter, size_t inlineDepth,
    IsLeavingFrame leaving) {
  MOZ_ASSERT(iter.activation() == this);
  MOZ_ASSERT(iter.isIonScripted());

  if (!rematerializedFrames_) {
    rematerializedFrames_ = cx->make_unique<RematerializedFrameTable>(cx);
    if (!rematerializedFrames_) {
      return nullptr;
    }
  }

  uint8_t* top = iter.fp();
  RematerializedFrameTable::AddPtr p = rematerializedFrames_->lookupForAdd(top);
  if (!p) {
    RematerializedFrameVector frames(cx);

    // The unit of rematerialization is an uninlined frame and its inlined
    // frames. Since inlined frames do not exist outside of snapshots, it
    // is impossible to synchronize their rematerialized copies to
    // preserve identity. Therefore, we always rematerialize an uninlined
    // frame and all its inlined frames at once.
    InlineFrameIterator inlineIter(cx, &iter);

    // We can run recover instructions without invalidating if we're always
    // leaving the frame.
    MaybeReadFallback::FallbackConsequence consequence =
        MaybeReadFallback::Fallback_Invalidate;
    if (leaving == IsLeavingFrame::Yes) {
      consequence = MaybeReadFallback::Fallback_DoNothing;
    }
    MaybeReadFallback recover(cx, this, &iter, consequence);

    // Frames are often rematerialized with the cx inside a Debugger's
    // realm. To recover slots and to create CallObjects, we need to
    // be in the script's realm.
    AutoRealmUnchecked ar(cx, iter.script()->realm());

    // The Ion frame must be invalidated to ensure the rematerialized frame will
    // be removed by the bailout code or the exception handler. If we're always
    // leaving the frame, the caller is responsible for cleaning up the
    // rematerialized frame.
    if (leaving == IsLeavingFrame::No && !iter.checkInvalidation()) {
      jit::Invalidate(cx, iter.script());
    }

    if (!RematerializedFrame::RematerializeInlineFrames(cx, top, inlineIter,
                                                        recover, frames)) {
      return nullptr;
    }

    if (!rematerializedFrames_->add(p, top, std::move(frames))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    // See comment in unsetPrevUpToDateUntil.
    DebugEnvironments::unsetPrevUpToDateUntil(cx,
                                              p->value()[inlineDepth].get());
  }

  return p->value()[inlineDepth].get();
}

js::jit::RematerializedFrame* js::jit::JitActivation::lookupRematerializedFrame(
    uint8_t* top, size_t inlineDepth) {
  if (!rematerializedFrames_) {
    return nullptr;
  }
  if (RematerializedFrameTable::Ptr p = rematerializedFrames_->lookup(top)) {
    return inlineDepth < p->value().length() ? p->value()[inlineDepth].get()
                                             : nullptr;
  }
  return nullptr;
}

void js::jit::JitActivation::removeRematerializedFramesFromDebugger(
    JSContext* cx, uint8_t* top) {
  // Ion bailout can fail due to overrecursion and OOM. In such cases we
  // cannot honor any further Debugger hooks on the frame, and need to
  // ensure that its Debugger.Frame entry is cleaned up.
  if (!cx->realm()->isDebuggee() || !rematerializedFrames_) {
    return;
  }
  if (RematerializedFrameTable::Ptr p = rematerializedFrames_->lookup(top)) {
    for (uint32_t i = 0; i < p->value().length(); i++) {
      DebugAPI::handleUnrecoverableIonBailoutError(cx, p->value()[i].get());
    }
    rematerializedFrames_->remove(p);
  }
}

bool js::jit::JitActivation::registerIonFrameRecovery(
    RInstructionResults&& results) {
  // Check that there is no entry in the vector yet.
  MOZ_ASSERT(!maybeIonFrameRecovery(results.frame()));
  if (!ionRecovery_.append(std::move(results))) {
    return false;
  }

  return true;
}

js::jit::RInstructionResults* js::jit::JitActivation::maybeIonFrameRecovery(
    JitFrameLayout* fp) {
  for (RInstructionResults* it = ionRecovery_.begin(); it != ionRecovery_.end();
       it++) {
    if (it->frame() == fp) {
      return it;
    }
  }

  return nullptr;
}

void js::jit::JitActivation::removeIonFrameRecovery(JitFrameLayout* fp) {
  RInstructionResults* elem = maybeIonFrameRecovery(fp);
  if (!elem) {
    return;
  }

  ionRecovery_.erase(elem);
}

void js::jit::JitActivation::startWasmTrap(wasm::Trap trap,
                                           const wasm::TrapSite& trapSite,
                                           const wasm::RegisterState& state) {
  MOZ_ASSERT(!isWasmTrapping());

  bool unwound;
  wasm::UnwindState unwindState;
  MOZ_RELEASE_ASSERT(wasm::StartUnwinding(state, &unwindState, &unwound));
  // With return calls, it is possible to not unwind when there is only an
  // entry left on the stack, e.g. after a return call collapsed the frame of a
  // function called from an entry stub; see CanUnwindSignatureCheck.
  MOZ_ASSERT_IF(unwound, trap == wasm::Trap::IndirectCallBadSig);

  void* pc = unwindState.pc;
  const wasm::Frame* fp = wasm::Frame::fromUntaggedWasmExitFP(unwindState.fp);

  // Look up the code from the unwound PC itself rather than the frame's
  // effective instance: a return_call_indirect signature mismatch in a
  // continuation's entry function leaves the PC in the ContBaseFrame stub.
  // In this case, its instance (the cont.new creator) can differ from the
  // frame's when the continuation runs an imported function. In all other cases
  // it should be equivalent, as asserted below.
  const wasm::Code* code = wasm::LookupCode(pc);
  MOZ_RELEASE_ASSERT(code);
  MOZ_ASSERT_IF(!unwound,
                code == &wasm::GetNearestEffectiveInstance(fp)->code());

  // The cross-instance return_call switched cx->realm to the callee and
  // collapsed the caller, so on unwind no live frame keeps that realm alive.
  // Restore it to the frame we unwound to, as the exception handler will.
  if (unwound) {
    cx()->setRealmForJitExceptionHandler(
        wasm::GetNearestEffectiveInstance(fp)->realm());
  }

  // Keep the trapping code alive across a GC. For an IndirectCallBadSig trap
  // reached through a tail call, this code can belong to a different instance
  // than the effective one above, whose frame has already been unwound. Nothing
  // else then keeps alive the code segment we are about to run the trap stub
  // in.
  wasmTrapCode_ = wasm::LookupCode(state.pc);
  MOZ_RELEASE_ASSERT(wasmTrapCode_);

  // Set resumePC.  Unfortunately this is used both as the execution resumption
  // address, in the case where the trap will resume, and the key for the
  // stackmap associated with the trap, which may be valid even if the trap
  // doesn't resume.  These two uses should be split, so we can specify a resume
  // point that MOZ_CRASHes if we mistakenly resume, while at the same time
  // specifying a valid stackmap for the trap.
  wasm::SummarizeResult summary =
      wasm::SummarizeTrapInstruction((const uint8_t*)state.pc);
  // SummarizeTrapInstruction must be able to identify the trapping instruction
  // and compute its length.
  MOZ_RELEASE_ASSERT(summary.identified());
  uint8_t* resumePC = ((uint8_t*)state.pc) + summary.length();

  setWasmExitFP(fp);
  wasmTrapData_.emplace();
  wasmTrapData_->resumePC = resumePC;
  wasmTrapData_->unwoundPC = pc;
  wasmTrapData_->trap = trap;
  wasmTrapData_->unwoundFrame = unwound;
  // If the frame was unwound, the source location must be recovered from the
  // callsite so that it is accurate.
  if (unwound) {
    wasm::CallSite site;
    MOZ_ALWAYS_TRUE(code->lookupCallSite(pc, &site));
    wasmTrapData_->trapSite.bytecodeOffset =
        wasm::BytecodeOffset(site.bytecodeOffset());
    wasmTrapData_->trapSite.inlinedCallerOffsets = site.inlinedCallerOffsets();
  } else {
    wasmTrapData_->trapSite = trapSite;
  }
  wasmTrapData_->failedUnwindSignatureMismatch =
      !unwound && trap == wasm::Trap::IndirectCallBadSig;

  MOZ_ASSERT(isWasmTrapping());
}

void js::jit::JitActivation::finishWasmTrap() {
  MOZ_ASSERT(hasWasmExitFP());
  MOZ_ASSERT(isWasmTrapping());
  wasmTrapData_.reset();
  // Keep wasmTrapCode_ alive: we are still executing inside this code and are
  // about to return into it, so releasing it now could free it. It is released
  // on the next trap or when the activation is destroyed.
  packedExitFP_ = nullptr;
  MOZ_ASSERT(!isWasmTrapping());
}
