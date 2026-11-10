/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_BaselineCacheIRCompiler_h
#define jit_BaselineCacheIRCompiler_h

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"

#include <stdint.h>

#include "jstypes.h"

#include "jit/CacheIR.h"
#include "jit/CacheIRCompiler.h"
#include "jit/CacheIROpsGenerated.h"
#include "jit/CacheIRReader.h"

struct JS_PUBLIC_API JSContext;

class JSScript;

namespace js {

namespace gc {
class AutoMarkingLock;
}  // namespace gc

namespace jit {

class CacheIRWriter;
class ICFallbackStub;
class ICScript;
class JitCode;
class Label;
class MacroAssembler;

struct Address;
struct Register;

using MaybeMarkingLock = mozilla::Maybe<gc::AutoMarkingLock>;

enum class ICAttachResult { Attached, DuplicateStub, TooLarge, OOM };

enum class DiscardExistingStubs { Yes, No };

ICAttachResult AttachBaselineCacheIRStub(JSContext* cx,
                                         const CacheIRWriter& writer,
                                         CacheKind kind, JSScript* outerScript,
                                         ICScript* icScript,
                                         ICFallbackStub* stub,
                                         const char* name);
ICAttachResult AttachBaselineCacheIRStubLocked(
    JSContext* cx, const CacheIRWriter& writer, CacheKind kind,
    JSScript* outerScript, ICScript* icScript, ICFallbackStub* stub,
    DiscardExistingStubs discardFallbackStubs, const char* name,
    MaybeMarkingLock& lock);

// BaselineCacheIRCompiler compiles CacheIR to BaselineIC native code.
class MOZ_RAII BaselineCacheIRCompiler : public CacheIRCompiler {
  struct CreateThisData {
    uint32_t numFixedSlots;
    uint32_t numDynamicSlots;
    gc::AllocKind allocKind;
    uint32_t thisShapeOffset;
    uint32_t allocSiteOffset;
  };

  bool makesGCCalls_;
  uint8_t localTracingSlots_ = 0;
  Register baselineFrameReg_ = FramePointer;

  mozilla::Maybe<CreateThisData> createThisData_;

  // This register points to the baseline frame of the caller. It should only
  // be used before we enter a stub frame. This is normally the frame pointer
  // register, but with --enable-ic-frame-pointers we have to allocate a
  // separate register.
  inline Register baselineFrameReg() {
    MOZ_ASSERT(!enteredStubFrame_);
    return baselineFrameReg_;
  }

  [[nodiscard]] bool emitStoreSlotShared(bool isFixed, ObjOperandId objId,
                                         uint32_t offsetOffset,
                                         ValOperandId rhsId);
  [[nodiscard]] bool emitAddAndStoreSlotShared(
      CacheOp op, ObjOperandId objId, uint32_t offsetOffset, ValOperandId rhsId,
      uint32_t newShapeOffset, mozilla::Maybe<uint32_t> numNewSlotsOffset,
      bool preserveWrapper);

  bool updateArgc(CallFlags flags, Register argcReg, uint32_t argcFixed,
                  Register scratch);
  void loadStackObject(ArgumentKind kind, CallFlags flags, Register argcReg,
                       Register dest, uint32_t extraArgs = 0);
  void pushArguments(Register argcReg, Register calleeReg, Register scratch,
                     Register scratch2, Register scratch3, CallFlags flags,
                     uint32_t argcFixed, bool isJitCall);
  void prepareForArguments(Register argcReg, Register calleeReg,
                           Register scratch, Register scratch2, CallFlags flags,
                           uint32_t argcFixed);
  void pushNewTarget();
  void pushStandardArguments(Register argcReg, Register scratch,
                             Register scratch2, uint32_t argcFixed,
                             bool isJitCall, bool isConstructing);
  void pushArrayArguments(Register argcReg, Register scratch, Register scratch2,
                          bool isJitCall, bool isConstructing);
  void pushFunCallArguments(Register argcReg, Register calleeReg,
                            Register scratch, Register scratch2,
                            uint32_t argcFixed, bool isJitCall);
  void pushFunApplyArgsObj(Register argcReg, Register calleeReg,
                           Register scratch, Register scratch2, bool isJitCall);
  void pushFunApplyNullUndefinedArguments(Register calleeReg, bool isJitCall);
  void pushBoundFunctionArguments(Register argcReg, Register calleeReg,
                                  Register scratch, Register scratch2,
                                  CallFlags flags, uint32_t numBoundArgs,
                                  bool isJitCall);
  void createThis(Register argcReg, Register calleeReg, Register scratch,
                  Register scratch2, Register scratch3, CallFlags flags,
                  mozilla::Maybe<uint32_t> numBoundArgs = mozilla::Nothing());
  void updateReturnValue();

  enum class NativeCallType { Native, ClassHook };
  enum class ClearLocalAllocSite { No, Yes };
  bool emitCallNativeShared(
      NativeCallType callType, ObjOperandId calleeId, Int32OperandId argcId,
      CallFlags flags, uint32_t argcFixed,
      mozilla::Maybe<bool> ignoresReturnValue,
      mozilla::Maybe<uint32_t> targetOffset,
      ClearLocalAllocSite clearLocalAllocSite = ClearLocalAllocSite::No);
  bool emitCallBoundScriptedFunctionShared(
      ObjOperandId calleeId, ObjOperandId targetId, Int32OperandId argcId,
      CallFlags flags, uint32_t numBoundArgs,
      mozilla::Maybe<uint32_t> icScriptOffset);
  void loadAllocSiteIntoContext(uint32_t siteOffset);

  enum class StringCode { CodeUnit, CodePoint };
  bool emitStringFromCodeResult(Int32OperandId codeId, StringCode stringCode);

  enum class StringCharOutOfBounds { Failure, EmptyString, UndefinedValue };
  bool emitLoadStringCharResult(StringOperandId strId, Int32OperandId indexId,
                                StringCharOutOfBounds outOfBounds);

  void emitAtomizeString(Register str, Register temp, Label* failure);

  bool emitCallScriptedGetterShared(ValOperandId receiverId,
                                    ObjOperandId calleeId, bool sameRealm,
                                    uint32_t nargsAndFlagsOffset,
                                    mozilla::Maybe<uint32_t> icScriptOffset);
  bool emitCallScriptedSetterShared(ObjOperandId receiverId,
                                    ObjOperandId calleeId, ValOperandId rhsId,
                                    bool sameRealm,
                                    uint32_t nargsAndFlagsOffset,
                                    mozilla::Maybe<uint32_t> icScriptOffset);
  bool emitCallScriptedFunctionShared(ObjOperandId calleeId,
                                      Int32OperandId argcId, CallFlags flags,
                                      uint32_t argcFixed,
                                      mozilla::Maybe<uint32_t> icScriptOffset);

  template <typename IdType>
  bool emitCallScriptedProxyGetShared(ValOperandId targetId,
                                      ObjOperandId receiverId,
                                      ObjOperandId handlerId,
                                      ObjOperandId trapId, IdType id,
                                      uint32_t nargsAndFlags);

  BaselineICPerfSpewer perfSpewer_;

 public:
  BaselineICPerfSpewer& perfSpewer() { return perfSpewer_; }

  friend class AutoStubFrame;

  BaselineCacheIRCompiler(JSContext* cx, TempAllocator& alloc,
                          const CacheIRWriter& writer, uint32_t stubDataOffset);

  [[nodiscard]] bool init(CacheKind kind);

  template <typename Fn, Fn fn>
  void callVM(MacroAssembler& masm);

  JitCode* compile();

  bool makesGCCalls() const;
  bool localTracingSlots() const { return localTracingSlots_; }

  Address stubAddress(uint32_t offset) const;

 private:
  CACHE_IR_COMPILER_UNSHARED_GENERATED
};

}  // namespace jit
}  // namespace js

#endif /* jit_BaselineCacheIRCompiler_h */
