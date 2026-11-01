/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_interceptor_PatcherDetour_h
#define mozilla_interceptor_PatcherDetour_h

#if defined(_M_ARM64)
#  include "mozilla/interceptor/Arm64.h"
#endif  // defined(_M_ARM64)
#include <utility>

#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/NativeNt.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/interceptor/PatcherBase.h"
#include "mozilla/interceptor/Trampoline.h"
#include "mozilla/interceptor/VMSharingPolicies.h"

#define COPY_CODES(NBYTES)                           \
  do {                                               \
    tramp.CopyCodes(origBytes.GetAddress(), NBYTES); \
    origBytes += NBYTES;                             \
  } while (0)

namespace mozilla {
namespace interceptor {

enum class DetourFlags : uint32_t {
  eDefault = 0,
  eEnable10BytePatch = 1,  // Allow 10-byte patches when conditions allow
  eTestOnlyForceShortPatch =
      2,  // Force short patches at all times (x86-64 and arm64 testing only)
  eDontResolveRedirection =
      4,  // Don't resolve the redirection of JMP (e.g. kernel32 -> kernelbase)
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(DetourFlags)

// This class is responsible to do tasks which depend on MMPolicy, decoupled
// from VMPolicy.  We already have WindowsDllPatcherBase, but it needs to
// depend on VMPolicy to hold an instance of VMPolicy as a member.
template <typename MMPolicyT>
class WindowsDllDetourPatcherPrimitive {
 protected:
#if defined(_M_ARM64)
  // LDR x16, .+8
  static const uint32_t kLdrX16Plus8 = 0x58000050U;
#endif  // defined(_M_ARM64)

  static void ApplyDefaultPatch(WritableTargetFunction<MMPolicyT>& target,
                                intptr_t aDest) {
#if defined(_M_IX86)
    target.WriteByte(0xe9);     // jmp
    target.WriteDisp32(aDest);  // hook displacement
    while (target.GetOffset() < target.GetNumBytes()) {
      // bug 1816936 - need to pad to the end of the last overwritten
      // instruction with noops.
      target.WriteByte(0x90);
    }
#elif defined(_M_X64)
    // mov r11, address
    target.WriteByte(0x49);
    target.WriteByte(0xbb);
    target.WritePointer(aDest);

    // jmp r11
    target.WriteByte(0x41);
    target.WriteByte(0xff);
    target.WriteByte(0xe3);
    while (target.GetOffset() < target.GetNumBytes()) {
      // bug 1816936 - need to pad to the end of the last overwritten
      // instruction with noops.
      target.WriteByte(0x90);
    }
#elif defined(_M_ARM64)
    // The default patch requires 16 bytes
    // LDR x16, .+8
    target.WriteLong(kLdrX16Plus8);
    // BR x16
    target.WriteLong(arm64::BuildUnconditionalBranchToRegister(16));
    target.WritePointer(aDest);
#else
#  error "Unsupported processor architecture"
#endif
  }

 public:
  constexpr static uint32_t GetWorstCaseRequiredBytesToPatch() {
#if defined(_M_IX86)
    return 5;
#elif defined(_M_X64)
    return 13;
#elif defined(_M_ARM64)
    return 16;
#else
#  error "Unsupported processor architecture"
#endif
  }

  WindowsDllDetourPatcherPrimitive() = default;

  WindowsDllDetourPatcherPrimitive(const WindowsDllDetourPatcherPrimitive&) =
      delete;
  WindowsDllDetourPatcherPrimitive(WindowsDllDetourPatcherPrimitive&&) = delete;
  WindowsDllDetourPatcherPrimitive& operator=(
      const WindowsDllDetourPatcherPrimitive&) = delete;
  WindowsDllDetourPatcherPrimitive& operator=(
      WindowsDllDetourPatcherPrimitive&&) = delete;

  bool AddIrreversibleHook(const MMPolicyT& aMMPolicy, FARPROC aTargetFn,
                           intptr_t aHookDest) {
    ReadOnlyTargetFunction<MMPolicyT> targetReadOnly(aMMPolicy, aTargetFn);

    WritableTargetFunction<MMPolicyT> targetWritable(
        targetReadOnly.Promote(GetWorstCaseRequiredBytesToPatch()));
    if (!targetWritable) {
      return false;
    }

    ApplyDefaultPatch(targetWritable, aHookDest);

    return targetWritable.Commit();
  }
};

template <typename VMPolicy>
class WindowsDllDetourPatcher final
    : public WindowsDllDetourPatcherPrimitive<typename VMPolicy::MMPolicyT>,
      public WindowsDllPatcherBase<VMPolicy> {
  using MMPolicyT = typename VMPolicy::MMPolicyT;
  using TrampPoolT = typename VMPolicy::PoolType;
  using PrimitiveT = WindowsDllDetourPatcherPrimitive<MMPolicyT>;
  Maybe<DetourFlags> mFlags;

 public:
  template <typename... Args>
  explicit WindowsDllDetourPatcher(Args&&... aArgs)
      : WindowsDllPatcherBase<VMPolicy>(std::forward<Args>(aArgs)...) {}

  ~WindowsDllDetourPatcher() { Clear(); }

  WindowsDllDetourPatcher(const WindowsDllDetourPatcher&) = delete;
  WindowsDllDetourPatcher(WindowsDllDetourPatcher&&) = delete;
  WindowsDllDetourPatcher& operator=(const WindowsDllDetourPatcher&) = delete;
  WindowsDllDetourPatcher& operator=(WindowsDllDetourPatcher&&) = delete;

  void Clear() {
    if (!this->mVMPolicy.ShouldUnhookUponDestruction()) {
      return;
    }

#if defined(_M_IX86)
    size_t nBytes = 1 + sizeof(intptr_t);
#elif defined(_M_X64)
    size_t nBytes = 2 + sizeof(intptr_t);
#elif defined(_M_ARM64)
    size_t nBytes = 2 * sizeof(uint32_t) + sizeof(uintptr_t);
#else
#  error "Unknown processor type"
#endif

    const auto& tramps = this->mVMPolicy.Items();
    for (auto&& tramp : tramps) {
      // First we read the pointer to the interceptor instance.
      Maybe<uintptr_t> instance = tramp.ReadEncodedPointer();
      if (!instance) {
        continue;
      }

      if (instance.value() != reinterpret_cast<uintptr_t>(this)) {
        // tramp does not belong to this interceptor instance.
        continue;
      }

      auto clearInstance = MakeScopeExit([&tramp]() -> void {
        // Clear the instance pointer so that no future instances with the same
        // |this| pointer will attempt to reset its hook.
        tramp.Rewind();
        tramp.WriteEncodedPointer(nullptr);
      });

      // Now we read the pointer to the intercepted function.
      Maybe<uintptr_t> interceptedFn = tramp.ReadEncodedPointer();
      if (!interceptedFn) {
        continue;
      }

      WritableTargetFunction<MMPolicyT> origBytes(
          this->mVMPolicy, interceptedFn.value(), nBytes);
      if (!origBytes) {
        continue;
      }

#if defined(_M_IX86) || defined(_M_X64)

      Maybe<uint8_t> maybeOpcode1 = origBytes.ReadByte();
      if (!maybeOpcode1) {
        continue;
      }

      uint8_t opcode1 = maybeOpcode1.value();

#  if defined(_M_IX86)
      // Ensure the JMP from CreateTrampoline is where we expect it to be.
      MOZ_ASSERT(opcode1 == 0xE9);
      if (opcode1 != 0xE9) {
        continue;
      }

      intptr_t startOfTrampInstructions =
          static_cast<intptr_t>(tramp.GetCurrentRemoteAddress());

      origBytes.WriteDisp32(startOfTrampInstructions);
      if (!origBytes) {
        continue;
      }

      origBytes.Commit();
#  elif defined(_M_X64)
      // Note: At the moment we clear 13-byte patches by replacing the jump to
      //       the patched function by a jump to the stub code. The original
      //       bytes of the original function are *not* restored. This implies
      //       that the stub code outlives our cleaning, so unwind information
      //       remains useful and must not be removed here.
      if (opcode1 == 0x49) {
        if (!Clear13BytePatch(origBytes, tramp.GetCurrentRemoteAddress())) {
          continue;
        }
      } else if (opcode1 == 0xB8) {
        if (!Clear10BytePatch(origBytes)) {
          continue;
        }
      } else if (opcode1 == 0x48) {
        // The original function was just a different trampoline
        if (!ClearTrampolinePatch(origBytes, tramp.GetCurrentRemoteAddress())) {
          continue;
        }
      } else {
        MOZ_ASSERT_UNREACHABLE("Unrecognized patch!");
        continue;
      }
#  endif

#elif defined(_M_ARM64)

      // Ensure that we see the instruction that we expect
      Maybe<uint32_t> inst1 = origBytes.ReadLong();
      if (!inst1) {
        continue;
      }

      if (inst1.value() == this->kLdrX16Plus8) {
        if (!Clear16BytePatch(origBytes, tramp.GetCurrentRemoteAddress())) {
          continue;
        }
      } else if (arm64::IsUnconditionalBranchImm(inst1.value())) {
        if (!Clear4BytePatch(inst1.value(), origBytes)) {
          continue;
        }
      } else {
        MOZ_ASSERT_UNREACHABLE("Unrecognized patch!");
        continue;
      }

#else
#  error "Unknown processor type"
#endif
    }

    this->mVMPolicy.Clear();
  }

#if defined(_M_X64)
  bool Clear13BytePatch(WritableTargetFunction<MMPolicyT>& aOrigBytes,
                        const uintptr_t aResetToAddress) {
    Maybe<uint8_t> maybeOpcode2 = aOrigBytes.ReadByte();
    if (!maybeOpcode2) {
      return false;
    }

    uint8_t opcode2 = maybeOpcode2.value();
    if (opcode2 != 0xBB) {
      return false;
    }

    aOrigBytes.WritePointer(aResetToAddress);
    if (!aOrigBytes) {
      return false;
    }

    return aOrigBytes.Commit();
  }

  bool ClearTrampolinePatch(WritableTargetFunction<MMPolicyT>& aOrigBytes,
                            const uintptr_t aPtrToResetToAddress) {
    // The target of the trampoline we replaced is stored at
    // aPtrToResetToAddress. We simply put it back where we got it from.
    Maybe<uint8_t> maybeOpcode2 = aOrigBytes.ReadByte();
    if (!maybeOpcode2) {
      return false;
    }

    uint8_t opcode2 = maybeOpcode2.value();
    if (opcode2 != 0xB8) {
      return false;
    }

    auto oldPtr = *(reinterpret_cast<const uintptr_t*>(aPtrToResetToAddress));

    aOrigBytes.WritePointer(oldPtr);
    if (!aOrigBytes) {
      return false;
    }

    return aOrigBytes.Commit();
  }

  bool Clear10BytePatch(WritableTargetFunction<MMPolicyT>& aOrigBytes) {
    Maybe<uint32_t> maybePtr32 = aOrigBytes.ReadLong();
    if (!maybePtr32) {
      return false;
    }

    uint32_t ptr32 = maybePtr32.value();
    // We expect the high bit to be clear
    if (ptr32 & 0x80000000) {
      return false;
    }

    uintptr_t trampPtr = ptr32;

    // trampPtr points to an intermediate trampoline that contains a 13-byte
    // patch. We back up by sizeof(uintptr_t) so that we can access the pointer
    // to the stub trampoline.
    WritableTargetFunction<MMPolicyT> writableIntermediate(
        this->mVMPolicy, trampPtr - sizeof(uintptr_t), 13 + sizeof(uintptr_t));
    if (!writableIntermediate) {
      return false;
    }

    Maybe<uintptr_t> stubTramp = writableIntermediate.ReadEncodedPtr();
    if (!stubTramp || !stubTramp.value()) {
      return false;
    }

    Maybe<uint8_t> maybeOpcode1 = writableIntermediate.ReadByte();
    if (!maybeOpcode1) {
      return false;
    }

    // We expect this opcode to be the beginning of our normal mov r11, ptr
    // patch sequence.
    uint8_t opcode1 = maybeOpcode1.value();
    if (opcode1 != 0x49) {
      return false;
    }

    // Now we can just delegate the rest to our normal 13-byte patch clearing.
    return Clear13BytePatch(writableIntermediate, stubTramp.value());
  }
#endif  // defined(_M_X64)

#if defined(_M_ARM64)
  bool Clear4BytePatch(const uint32_t aBranchImm,
                       WritableTargetFunction<MMPolicyT>& aOrigBytes) {
    MOZ_ASSERT(arm64::IsUnconditionalBranchImm(aBranchImm));

    arm64::LoadOrBranch decoded = arm64::BUncondImmDecode(
        aOrigBytes.GetCurrentAddress() - sizeof(uint32_t), aBranchImm);

    uintptr_t trampPtr = decoded.mAbsAddress;

    // trampPtr points to an intermediate trampoline that contains a veneer.
    // We back up by sizeof(uintptr_t) so that we can access the pointer to the
    // stub trampoline.

    // We want trampLen to be the size of the veneer, plus one pointer (since
    // we are backing up trampPtr by one pointer)
    size_t trampLen = 16 + sizeof(uintptr_t);

    WritableTargetFunction<MMPolicyT> writableIntermediate(
        this->mVMPolicy, trampPtr - sizeof(uintptr_t), trampLen);
    if (!writableIntermediate) {
      return false;
    }

    Maybe<uintptr_t> stubTramp = writableIntermediate.ReadEncodedPtr();
    if (!stubTramp || !stubTramp.value()) {
      return false;
    }

    Maybe<uint32_t> inst1 = writableIntermediate.ReadLong();
    if (!inst1 || inst1.value() != this->kLdrX16Plus8) {
      return false;
    }

    return Clear16BytePatch(writableIntermediate, stubTramp.value());
  }

  bool Clear16BytePatch(WritableTargetFunction<MMPolicyT>& aOrigBytes,
                        const uintptr_t aResetToAddress) {
    Maybe<uint32_t> inst2 = aOrigBytes.ReadLong();
    if (!inst2) {
      return false;
    }

    if (inst2.value() != arm64::BuildUnconditionalBranchToRegister(16)) {
      MOZ_ASSERT_UNREACHABLE("Unrecognized patch!");
      return false;
    }

    // Clobber the pointer to our hook function with a pointer to the
    // start of the trampoline.
    aOrigBytes.WritePointer(aResetToAddress);
    aOrigBytes.Commit();

    return true;
  }
#endif  // defined(_M_ARM64)

  void Init(DetourFlags aFlags = DetourFlags::eDefault) {
    if (Initialized()) {
      return;
    }

#if defined(_M_X64)
    if (aFlags & DetourFlags::eTestOnlyForceShortPatch) {
      aFlags |= DetourFlags::eEnable10BytePatch;
    }
#endif  // defined(_M_X64)

    mFlags = Some(aFlags);
  }

  bool Initialized() const { return mFlags.isSome(); }

  bool AddHook(FARPROC aTargetFn, intptr_t aHookDest, void** aOrigFunc) {
    ReadOnlyTargetFunction<MMPolicyT> target(
        (mFlags.value() & DetourFlags::eDontResolveRedirection)
            ? ReadOnlyTargetFunction<MMPolicyT>(
                  this->mVMPolicy, reinterpret_cast<uintptr_t>(aTargetFn))
            : this->ResolveRedirectedAddress(aTargetFn));

    TrampPoolT* trampPool = nullptr;

#if defined(_M_ARM64)
    // ARM64 uses two passes to build its trampoline. The first pass uses a
    // null tramp to determine how many bytes are needed. Once that is known,
    // CreateTrampoline calls itself recursively with a "real" tramp.
    Trampoline<MMPolicyT> tramp(nullptr);
#else
    Maybe<TrampPoolT> maybeTrampPool = DoReserve();
    MOZ_ASSERT(maybeTrampPool);
    if (!maybeTrampPool) {
      return false;
    }

    trampPool = maybeTrampPool.ptr();

    Maybe<Trampoline<MMPolicyT>> maybeTramp(trampPool->GetNextTrampoline());
    if (!maybeTramp) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_NEXT_TRAMPOLINE_ERROR);
      return false;
    }

    Trampoline<MMPolicyT> tramp(std::move(maybeTramp.ref()));
#endif

    CreateTrampoline(target, trampPool, tramp, aHookDest, aOrigFunc);
    if (!*aOrigFunc) {
      return false;
    }

    return true;
  }

 private:
  /**
   * This function returns a maximum distance that can be reached by a single
   * unconditional jump instruction. This is dependent on the processor ISA.
   * Note that this distance is *exclusive* when added to the pivot, so the
   * distance returned by this function is actually
   * (maximum_absolute_offset + 1).
   */
  static uint32_t GetDefaultPivotDistance() {
#if defined(_M_ARM64)
    // Immediate unconditional branch allows for +/- 128MB
    return 0x08000000U;
#elif defined(_M_IX86) || defined(_M_X64)
    // For these ISAs, our distance will assume the use of an unconditional jmp
    // with a 32-bit signed displacement.
    return 0x80000000U;
#else
#  error "Not defined for this processor arch"
#endif
  }

  /**
   * If we're reserving trampoline space for a specific module, we base the
   * pivot off of the median address of the module's .text section. While this
   * may not be precise, it should be accurate enough for our purposes: To
   * ensure that the trampoline space is reachable by any executable code in the
   * module.
   */
  Maybe<TrampPoolT> ReserveForModule(HMODULE aModule) {
    nt::PEHeaders moduleHeaders(aModule);
    if (!moduleHeaders) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_RESERVE_FOR_MODULE_PE_ERROR);
      return Nothing();
    }

    Maybe<Span<const uint8_t>> textSectionInfo =
        moduleHeaders.GetTextSectionInfo();
    if (!textSectionInfo) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_RESERVE_FOR_MODULE_TEXT_ERROR);
      return Nothing();
    }

    const uint8_t* median = textSectionInfo.value().data() +
                            (textSectionInfo.value().LengthBytes() / 2);

    Maybe<TrampPoolT> maybeTrampPool = this->mVMPolicy.Reserve(
        reinterpret_cast<uintptr_t>(median), GetDefaultPivotDistance());
    if (!maybeTrampPool) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_RESERVE_FOR_MODULE_RESERVE_ERROR);
    }
    return maybeTrampPool;
  }

  Maybe<TrampPoolT> DoReserve(HMODULE aModule = nullptr) {
    if (aModule) {
      return ReserveForModule(aModule);
    }

    uintptr_t pivot = 0;
    uint32_t distance = 0;

#if defined(_M_X64)
    if (mFlags.value() & DetourFlags::eEnable10BytePatch) {
      // We must stay below the 2GB mark because a 10-byte patch uses movsxd
      // (ie, sign extension) to expand the pointer to 64-bits, so bit 31 of any
      // pointers into the reserved region must be 0.
      pivot = 0x40000000U;
      distance = 0x40000000U;
    }
#endif  // defined(_M_X64)

    Maybe<TrampPoolT> maybeTrampPool = this->mVMPolicy.Reserve(pivot, distance);
#if defined(NIGHTLY_BUILD)
    if (!maybeTrampPool && this->GetLastDetourError().isNothing()) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_DO_RESERVE_ERROR);
    }
#endif  // defined(NIGHTLY_BUILD)
    return maybeTrampPool;
  }

 protected:
#if !defined(_M_ARM64)

  const static int kPageSize = 4096;

  // rex bits
  static const BYTE kMaskHighNibble = 0xF0;
  static const BYTE kRexOpcode = 0x40;
  static const BYTE kMaskRexW = 0x08;
  static const BYTE kMaskRexR = 0x04;
  static const BYTE kMaskRexX = 0x02;
  static const BYTE kMaskRexB = 0x01;

  // mod r/m bits
  static const BYTE kRegFieldShift = 3;
  static const BYTE kMaskMod = 0xC0;
  static const BYTE kMaskReg = 0x38;
  static const BYTE kMaskRm = 0x07;
  static const BYTE kRmNeedSib = 0x04;
  static const BYTE kModReg = 0xC0;
  static const BYTE kModDisp32 = 0x80;
  static const BYTE kModDisp8 = 0x40;
  static const BYTE kModNoRegDisp = 0x00;
  static const BYTE kRmNoRegDispDisp32 = 0x05;

  // sib bits
  static const BYTE kMaskSibScale = 0xC0;
  static const BYTE kMaskSibIndex = 0x38;
  static const BYTE kMaskSibBase = 0x07;
  static const BYTE kSibBaseEbp = 0x05;

  // Register bit IDs.
  static const BYTE kRegAx = 0x0;
  static const BYTE kRegCx = 0x1;
  static const BYTE kRegDx = 0x2;
  static const BYTE kRegBx = 0x3;
  static const BYTE kRegSp = 0x4;
  static const BYTE kRegBp = 0x5;
  static const BYTE kRegSi = 0x6;
  static const BYTE kRegDi = 0x7;

  // Special ModR/M codes.  These indicate operands that cannot be simply
  // memcpy-ed.
  // Operand is a 64-bit RIP-relative address.
  static const int kModOperand64 = -2;
  // Operand is not yet handled by our trampoline.
  static const int kModUnknown = -1;

  /**
   * Returns the number of bytes taken by the ModR/M byte, SIB (if present)
   * and the instruction's operand.  In special cases, the special MODRM codes
   * above are returned.
   * aModRm points to the ModR/M byte of the instruction.
   * On return, aSubOpcode (if present) is filled with the subopcode/register
   * code found in the ModR/M byte.
   */
  int CountModRmSib(const ReadOnlyTargetFunction<MMPolicyT>& aModRm,
                    BYTE* aSubOpcode = nullptr) {
    int numBytes = 1;  // Start with 1 for mod r/m byte itself
    switch (*aModRm & kMaskMod) {
      case kModReg:
        return numBytes;
      case kModDisp8:
        numBytes += 1;
        break;
      case kModDisp32:
        numBytes += 4;
        break;
      case kModNoRegDisp:
        if ((*aModRm & kMaskRm) == kRmNoRegDispDisp32) {
#  if defined(_M_X64)
          if (aSubOpcode) {
            *aSubOpcode = (*aModRm & kMaskReg) >> kRegFieldShift;
          }
          return kModOperand64;
#  else
          // On IA-32, all ModR/M instruction modes address memory relative to 0
          numBytes += 4;
#  endif
        } else if (((*aModRm & kMaskRm) == kRmNeedSib &&
                    (*(aModRm + 1) & kMaskSibBase) == kSibBaseEbp)) {
          numBytes += 4;
        }
        break;
      default:
        // This should not be reachable
        MOZ_ASSERT_UNREACHABLE("Impossible value for modr/m byte mod bits");
        return kModUnknown;
    }
    if ((*aModRm & kMaskRm) == kRmNeedSib) {
      // SIB byte
      numBytes += 1;
    }
    if (aSubOpcode) {
      *aSubOpcode = (*aModRm & kMaskReg) >> kRegFieldShift;
    }
    return numBytes;
  }

#  if defined(_M_X64)
  enum class JumpType { Je, Jne, Jae, Jmp, Call, Js };

  static bool GenerateJump(Trampoline<MMPolicyT>& aTramp,
                           uintptr_t aAbsTargetAddress, const JumpType aType,
                           const ReadOnlyTargetFunction<MMPolicyT>& origBytes,
                           uint32_t aBytesToPatch) {
    if (aAbsTargetAddress >= origBytes.GetBaseAddress() &&
        aAbsTargetAddress < origBytes.GetBaseAddress() + aBytesToPatch) {
      // We don't correctly detour jumps or calls with target inside the section
      // that we are patching. We don't think we need this for any realistic
      // use case, so just fail here.
      return false;
    }

    // Near call, absolute indirect, address given in r/m32
    if (aType == JumpType::Call) {
      // CALL [RIP+0]
      aTramp.WriteByte(0xff);
      aTramp.WriteByte(0x15);
      // The offset to jump destination -- 2 bytes after the current position.
      aTramp.WriteInteger(2);
      aTramp.WriteByte(0xeb);  // JMP + 8 (jump over target address)
      aTramp.WriteByte(8);
      aTramp.WritePointer(aAbsTargetAddress);
      return !!aTramp;
    }

    // Write an opposite conditional jump because the destination branches
    // are swapped.
    if (aType == JumpType::Je) {
      // JNE RIP+14
      aTramp.WriteByte(0x75);
      aTramp.WriteByte(14);
    } else if (aType == JumpType::Jne) {
      // JE RIP+14
      aTramp.WriteByte(0x74);
      aTramp.WriteByte(14);
    } else if (aType == JumpType::Jae) {
      // JB RIP+14
      aTramp.WriteByte(0x72);
      aTramp.WriteByte(14);
    } else if (aType == JumpType::Js) {
      // JNS RIP+14
      aTramp.WriteByte(0x79);
      aTramp.WriteByte(14);
    }

    // Near jmp, absolute indirect, address given in r/m32
    // JMP [RIP+0]
    aTramp.WriteByte(0xff);
    aTramp.WriteByte(0x25);
    // The offset to jump destination is 0
    aTramp.WriteInteger(0);
    aTramp.WritePointer(aAbsTargetAddress);

    return !!aTramp;
  }

  // Emit MOV r64, imm64 where aReg is a register number from 0 to 15.
  static void WriteMovImm64(Trampoline<MMPolicyT>& aTramp, BYTE aReg,
                            uintptr_t aImm) {
    MOZ_ASSERT(aReg < 16);
    aTramp.WriteByte(kRexOpcode | kMaskRexW | ((aReg & 8) ? kMaskRexB : 0));
    aTramp.WriteByte(0xb8 + (aReg & kMaskRm));
    aTramp.WritePointer(aImm);
  }

  // Emit instructions that load the 64-bit value stored at aAbsAddr into the
  // register aReg, where aReg is a register number from 0 to 15 other than
  // rsp. No other register is modified.
  void WriteLoadFromAbsoluteAddress(Trampoline<MMPolicyT>& aTramp, BYTE aReg,
                                    uintptr_t aAbsAddr) {
    MOZ_ASSERT(aReg < 16 && aReg != kRegSp);

    if (aReg == kRegAx) {
      // MOV rax, moffs64
      aTramp.WriteByte(kRexOpcode | kMaskRexW);
      aTramp.WriteByte(0xa1);
      aTramp.WritePointer(aAbsAddr);
      return;
    }

    // MOV reg, imm64 followed by MOV reg, [reg]
    WriteMovImm64(aTramp, aReg, aAbsAddr);

    const BYTE lowReg = aReg & kMaskRm;
    aTramp.WriteByte(kRexOpcode | kMaskRexW |
                     ((aReg & 8) ? (kMaskRexR | kMaskRexB) : 0));
    aTramp.WriteByte(0x8b);
    if (lowReg == kRegSp) {
      // r12 as a base register requires a SIB byte: base = r12, no index.
      aTramp.WriteByte(BuildModRmByte(kModNoRegDisp, lowReg, kRmNeedSib));
      aTramp.WriteByte(0x24);
    } else if (lowReg == kRegBp) {
      // rbp and r13 as a base register with mod = 00 would mean RIP-relative,
      // so use mod = 01 with a zero disp8 instead.
      aTramp.WriteByte(BuildModRmByte(kModDisp8, lowReg, lowReg));
      aTramp.WriteByte(0x00);
    } else {
      aTramp.WriteByte(BuildModRmByte(kModNoRegDisp, lowReg, lowReg));
    }
  }
#  endif

  enum ePrefixGroupBits {
    eNoPrefixes = 0,
    ePrefixGroup1 = (1 << 0),
    ePrefixGroup2 = (1 << 1),
    ePrefixGroup3 = (1 << 2),
    ePrefixGroup4 = (1 << 3)
  };

  int CountPrefixBytes(const ReadOnlyTargetFunction<MMPolicyT>& aBytes,
                       unsigned char* aOutGroupBits) {
    unsigned char& groupBits = *aOutGroupBits;
    groupBits = eNoPrefixes;
    int index = 0;
    while (true) {
      switch (aBytes[index]) {
        // Group 1
        case 0xF0:  // LOCK
        case 0xF2:  // REPNZ
        case 0xF3:  // REP / REPZ
          if (groupBits & ePrefixGroup1) {
            return -1;
          }
          groupBits |= ePrefixGroup1;
          ++index;
          break;

        // Group 2
        case 0x2E:  // CS override / branch not taken
        case 0x36:  // SS override
        case 0x3E:  // DS override / branch taken
        case 0x64:  // FS override
        case 0x65:  // GS override
          if (groupBits & ePrefixGroup2) {
            return -1;
          }
          groupBits |= ePrefixGroup2;
          ++index;
          break;

        // Group 3
        case 0x66:  // operand size override
          if (groupBits & ePrefixGroup3) {
            return -1;
          }
          groupBits |= ePrefixGroup3;
          ++index;
          break;

        // Group 4
        case 0x67:  // Address size override
          if (groupBits & ePrefixGroup4) {
            return -1;
          }
          groupBits |= ePrefixGroup4;
          ++index;
          break;

        default:
          return index;
      }
    }
  }

  // Return a ModR/M byte made from the 2 Mod bits, the register used for the
  // reg bits and the register used for the R/M bits.
  BYTE BuildModRmByte(BYTE aModBits, BYTE aReg, BYTE aRm) {
    MOZ_ASSERT((aRm & kMaskRm) == aRm);
    MOZ_ASSERT((aModBits & kMaskMod) == aModBits);
    MOZ_ASSERT(((aReg << kRegFieldShift) & kMaskReg) ==
               (aReg << kRegFieldShift));
    return aModBits | (aReg << kRegFieldShift) | aRm;
  }

#endif  // !defined(_M_ARM64)

  // If originalFn is a recognized trampoline then patch it to call aDest,
  // set *aTramp and *aOutTramp to that trampoline's target and return true.
  bool PatchIfTargetIsRecognizedTrampoline(
      Trampoline<MMPolicyT>& aTramp,
      ReadOnlyTargetFunction<MMPolicyT>& aOriginalFn, intptr_t aDest,
      void** aOutTramp) {
#if defined(_M_X64)
    // Variation 1:
    // 48 b8 imm64  mov rax, imm64
    // ff e0        jmp rax
    //
    // Variation 2:
    // 48 b8 imm64  mov rax, imm64
    // 50           push rax
    // c3           ret
    if ((aOriginalFn[0] == 0x48) && (aOriginalFn[1] == 0xB8) &&
        ((aOriginalFn[10] == 0xFF && aOriginalFn[11] == 0xE0) ||
         (aOriginalFn[10] == 0x50 && aOriginalFn[11] == 0xC3))) {
      uintptr_t originalTarget =
          (aOriginalFn + 2).template ChasePointer<uintptr_t>();

      // Skip the first two bytes (48 b8) so that we can overwrite the imm64
      WritableTargetFunction<MMPolicyT> target(aOriginalFn.Promote(8, 2));
      if (!target) {
        return false;
      }

      // Set aOutTramp now so that the new target won't race if accessing this
      // value. Bug 1838286 did not fix this path, resulting in bug 1950847.
      *aOutTramp = reinterpret_cast<void*>(originalTarget);

      // Write the new JMP target address.
      target.WritePointer(aDest);
      if (!target.Commit()) {
        *aOutTramp = nullptr;
        return false;
      }

      // Store the old target address so we can restore it when we're cleared
      aTramp.WritePointer(originalTarget);
      if (!aTramp) {
        *aOutTramp = nullptr;
        return false;
      }

      return true;
    }
#endif  // defined(_M_X64)

    return false;
  }

#if defined(_M_ARM64)
  bool Apply4BytePatch(TrampPoolT* aTrampPool, void* aTrampPtr,
                       WritableTargetFunction<MMPolicyT>& target,
                       intptr_t aDest) {
    MOZ_ASSERT(aTrampPool);
    if (!aTrampPool) {
      return false;
    }

    uintptr_t hookDest = arm64::MakeVeneer(*aTrampPool, aTrampPtr, aDest);
    if (!hookDest) {
      return false;
    }

    Maybe<uint32_t> branchImm = arm64::BuildUnconditionalBranchImm(
        target.GetCurrentAddress(), hookDest);
    if (!branchImm) {
      return false;
    }

    target.WriteLong(branchImm.value());

    return true;
  }
#endif  // defined(_M_ARM64)

#if defined(_M_X64)
  bool Apply10BytePatch(TrampPoolT* aTrampPool, void* aTrampPtr,
                        WritableTargetFunction<MMPolicyT>& target,
                        intptr_t aDest) {
    // Note: Even if the target function is also below 2GB, we still use an
    // intermediary trampoline so that we consistently have a 64-bit pointer
    // that we can use to reset the trampoline upon interceptor shutdown.
    Maybe<Trampoline<MMPolicyT>> maybeCallTramp(
        aTrampPool->GetNextTrampoline());
    if (!maybeCallTramp) {
      return false;
    }

    Trampoline<MMPolicyT> callTramp(std::move(maybeCallTramp.ref()));

    // Write a null instance so that Clear() does not consider this tramp to
    // be a normal tramp to be torn down.
    callTramp.WriteEncodedPointer(nullptr);
    // Use the second pointer slot to store a pointer to the primary tramp
    callTramp.WriteEncodedPointer(aTrampPtr);
    callTramp.StartExecutableCode();

    // mov r11, address
    callTramp.WriteByte(0x49);
    callTramp.WriteByte(0xbb);
    callTramp.WritePointer(aDest);

    // jmp r11
    callTramp.WriteByte(0x41);
    callTramp.WriteByte(0xff);
    callTramp.WriteByte(0xe3);

    void* callTrampStart = callTramp.EndExecutableCode();
    if (!callTrampStart) {
      return false;
    }

    target.WriteByte(0xB8);  // MOV EAX, IMM32

    // Assert that the topmost 33 bits are 0
    MOZ_ASSERT(
        !(reinterpret_cast<uintptr_t>(callTrampStart) & (~0x7FFFFFFFULL)));

    target.WriteLong(static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(callTrampStart) & 0x7FFFFFFFU));
    target.WriteByte(0x48);  // REX.W
    target.WriteByte(0x63);  // MOVSXD r64, r/m32
    // dest: rax, src: eax
    target.WriteByte(BuildModRmByte(kModReg, kRegAx, kRegAx));
    target.WriteByte(0xFF);                                // JMP /4
    target.WriteByte(BuildModRmByte(kModReg, 4, kRegAx));  // rax
    while (target.GetOffset() < target.GetNumBytes()) {
      // bug 1816936 - need to pad to the end of the last overwritten
      // instruction with noops.
      target.WriteByte(0x90);
    }

    return true;
  }
#endif  // defined(_M_X64)

  // Note that *aOutTramp is not set until CreateTrampoline has completed
  // successfully, so callers can use that to check for success.
  void CreateTrampoline(ReadOnlyTargetFunction<MMPolicyT>& origBytes,
                        TrampPoolT* aTrampPool, Trampoline<MMPolicyT>& aTramp,
                        intptr_t aDest, void** aOutTramp) {
    *aOutTramp = nullptr;

    Trampoline<MMPolicyT>& tramp = aTramp;
    if (!tramp) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_INVALID_TRAMPOLINE);
      return;
    }

    // The beginning of the trampoline contains two pointer-width slots:
    // [0]: |this|, so that we know whether the trampoline belongs to us;
    // [1]: Pointer to original function, so that we can reset the hooked
    // function to its original behavior upon destruction.  In rare cases
    // where the function was already a different trampoline, this is
    // just a pointer to that trampoline's target address.
    tramp.WriteEncodedPointer(this);
    if (!tramp) {
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_WRITE_POINTER_ERROR);
      return;
    }

    auto clearInstanceOnFailure = MakeScopeExit([this, aOutTramp, &tramp,
                                                 &origBytes]() -> void {
      // If *aOutTramp was set this method ran successfully.
      if (*aOutTramp) {
        return;
      }

      // Clear the instance pointer so that we don't try to reset a
      // nonexistent hook.
      tramp.Rewind();
      tramp.WriteEncodedPointer(nullptr);

#if defined(NIGHTLY_BUILD)
      origBytes.Rewind();
      this->SetLastDetourError(
          DetourResultCode::DETOUR_PATCHER_CREATE_TRAMPOLINE_ERROR);
      DetourError& lastError = *this->mVMPolicy.mLastError;
      size_t bytesToCapture = std::min(
          std::size(lastError.mOrigBytes),
          static_cast<size_t>(PrimitiveT::GetWorstCaseRequiredBytesToPatch()));
#  if defined(_M_ARM64)
      size_t numInstructionsToCapture = bytesToCapture / sizeof(uint32_t);
      auto origBytesDst = reinterpret_cast<uint32_t*>(lastError.mOrigBytes);
      for (size_t i = 0; i < numInstructionsToCapture; ++i) {
        origBytesDst[i] = origBytes.ReadNextInstruction();
      }
#  else
      for (size_t i = 0; i < bytesToCapture; ++i) {
        lastError.mOrigBytes[i] = origBytes[i];
      }
#  endif  // defined(_M_ARM64)
#else
      // Silence -Wunused-lambda-capture in non-Nightly.
      (void)this;
      (void)origBytes;
#endif  // defined(NIGHTLY_BUILD)
    });

    tramp.WritePointer(origBytes.AsEncodedPtr());
    if (!tramp) {
      return;
    }

    if (PatchIfTargetIsRecognizedTrampoline(tramp, origBytes, aDest,
                                            aOutTramp)) {
      return;
    }

    tramp.StartExecutableCode();

    constexpr uint32_t kWorstCaseBytesRequired =
        PrimitiveT::GetWorstCaseRequiredBytesToPatch();

#if defined(_M_IX86)
    int pJmp32 = -1;
    while (origBytes.GetOffset() < kWorstCaseBytesRequired) {
      // Understand some simple instructions that might be found in a
      // prologue; we might need to extend this as necessary.
      //
      // Note!  If we ever need to understand jump instructions, we'll
      // need to rewrite the displacement argument.
      unsigned char prefixGroups;
      int numPrefixBytes = CountPrefixBytes(origBytes, &prefixGroups);
      if (numPrefixBytes < 0 ||
          (prefixGroups & (ePrefixGroup3 | ePrefixGroup4))) {
        // Either the prefix sequence was bad, or there are prefixes that
        // we don't currently support (groups 3 and 4)
        MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
        return;
      }

      origBytes += numPrefixBytes;
      if (*origBytes >= 0x88 && *origBytes <= 0x8B) {
        // various MOVs
        ++origBytes;
        int len = CountModRmSib(origBytes);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized MOV opcode sequence");
          return;
        }
        origBytes += len;
      } else if (*origBytes == 0x0f &&
                 (origBytes[1] == 0x10 || origBytes[1] == 0x11)) {
        // SSE: movups xmm, xmm/m128
        //      movups xmm/m128, xmm
        origBytes += 2;
        int len = CountModRmSib(origBytes);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized MOV opcode sequence");
          return;
        }
        origBytes += len;
      } else if (*origBytes == 0xA1) {
        // MOV eax, [seg:offset]
        origBytes += 5;
      } else if (*origBytes == 0xB8) {
        // MOV 0xB8: http://ref.x86asm.net/coder32.html#xB8
        origBytes += 5;
      } else if (*origBytes == 0x33 && (origBytes[1] & kMaskMod) == kModReg) {
        // XOR r32, r32
        origBytes += 2;
      } else if ((*origBytes & 0xf8) == 0x40) {
        // INC r32
        origBytes += 1;
      } else if (*origBytes == 0x83) {
        // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP on imm8
        int len = CountModRmSib(origBytes + 1);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(len + 2);  // 1 for 0x83, 1 for imm8
      } else if (*origBytes == 0x68) {
        // PUSH with 4-byte operand
        origBytes += 5;
      } else if ((*origBytes & 0xf0) == 0x50) {
        // 1-byte PUSH/POP
        ++origBytes;
      } else if (*origBytes == 0x6A) {
        // PUSH imm8
        origBytes += 2;
      } else if (*origBytes == 0xe9) {
        pJmp32 = origBytes.GetOffset();
        // jmp 32bit offset
        origBytes += 5;
      } else if (*origBytes == 0xff && origBytes[1] == 0x25) {
        // jmp [disp32]
        origBytes += 6;
      } else if (*origBytes == 0xc2) {
        // ret imm16.  We can't handle this but it happens.  We don't ASSERT but
        // we do fail to hook.
#  if defined(MOZILLA_INTERNAL_API)
        NS_WARNING("Cannot hook method -- RET opcode found");
#  endif
        return;
      } else {
        // printf ("Unknown x86 instruction byte 0x%02x, aborting trampoline\n",
        // *origBytes);
        MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
        return;
      }
    }

    // The trampoline is a copy of the instructions that we just traced,
    // followed by a jump that we add below.
    tramp.CopyFrom(origBytes.GetBaseAddress(), origBytes.GetOffset());
    if (!tramp) {
      return;
    }
#elif defined(_M_X64)
    bool foundJmp = false;
    // |use10BytePatch| should always default to |false| in production. It is
    // not set to true unless we detect that a 10-byte patch is necessary.
    // OTOH, for testing purposes, if we want to force a 10-byte patch, we
    // always initialize |use10BytePatch| to |true|.
    bool use10BytePatch =
        (mFlags.value() & DetourFlags::eTestOnlyForceShortPatch) ==
        DetourFlags::eTestOnlyForceShortPatch;
    const uint32_t bytesRequired =
        use10BytePatch ? 10 : kWorstCaseBytesRequired;

    while (origBytes.GetOffset() < bytesRequired) {
      // If we found JMP 32bit offset, we require that the next bytes must
      // be NOP or INT3.  There is no reason to copy them.
      // TODO: This used to trigger for Je as well.  Now that I allow
      // instructions after CALL and JE, I don't think I need that.
      // The only real value of this condition is that if code follows a JMP
      // then its _probably_ the target of a JMP somewhere else and we
      // will be overwriting it, which would be tragic.  This seems
      // highly unlikely.
      if (foundJmp) {
        if (*origBytes == 0x90 || *origBytes == 0xcc) {
          ++origBytes;
          continue;
        }

        // If our trampoline space is located in the lowest 2GB, we can do a ten
        // byte patch instead of a thirteen byte patch.
        if (aTrampPool && aTrampPool->IsInLowest2GB() &&
            origBytes.GetOffset() >= 10) {
          use10BytePatch = true;
          break;
        }

        MOZ_ASSERT_UNREACHABLE("Opcode sequence includes commands after JMP");
        return;
      }
      if (*origBytes == 0x0f) {
        COPY_CODES(1);
        if (*origBytes == 0x1f) {
          // nop (multibyte)
          COPY_CODES(1);
          if ((*origBytes & 0xc0) == 0x40 && (*origBytes & 0x7) == 0x04) {
            COPY_CODES(3);
          } else {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
        } else if (*origBytes == 0x05) {
          // syscall
          COPY_CODES(1);
        } else if (*origBytes == 0x10 || *origBytes == 0x11) {
          // SSE: movups xmm, xmm/m128
          //      movups xmm/m128, xmm
          COPY_CODES(1);
          int nModRmSibBytes = CountModRmSib(origBytes);
          if (nModRmSibBytes < 0) {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          } else {
            COPY_CODES(nModRmSibBytes);
          }
        } else if (*origBytes >= 0x83 && *origBytes <= 0x85) {
          // 0f 83 cd    JAE rel32
          // 0f 84 cd    JE  rel32
          // 0f 85 cd    JNE rel32
          const JumpType kJumpTypes[] = {JumpType::Jae, JumpType::Je,
                                         JumpType::Jne};
          auto jumpType = kJumpTypes[*origBytes - 0x83];
          ++origBytes;
          --tramp;  // overwrite the 0x0f we copied above

          if (!GenerateJump(tramp, origBytes.ReadDisp32AsAbsolute(), jumpType,
                            origBytes, bytesRequired)) {
            return;
          }
        } else if (*origBytes == 0x88) {
          // 0f 88 cd    JS  rel32
          ++origBytes;
          --tramp;  // overwrite the 0x0f we copied above

          if (!GenerateJump(tramp, origBytes.ReadDisp32AsAbsolute(),
                            JumpType::Js, origBytes, bytesRequired)) {
            return;
          }
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes >= 0x88 && *origBytes <= 0x8B) {
        // various 32-bit MOVs
        COPY_CODES(1);
        int len = CountModRmSib(origBytes);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized MOV opcode sequence");
          return;
        }
        COPY_CODES(len);
      } else if (*origBytes == 0x40 && origBytes[1] == 0x38 &&
                 (origBytes[2] & (kMaskMod | kMaskRm)) ==
                     (kModNoRegDisp | kRmNoRegDispDisp32)) {
        int reg = origBytes[2] & kMaskReg;
        if (reg == (kRegSp << kRegFieldShift)) {
          // The register is spl, which the push below would modify.
          return;
        }
        origBytes += 3;

        // rex cmp byte ptr [rip-relative address], reg
        // We'll compute the absolute address and do the cmp in r11

        // push r11 (to save the old value)
        tramp.WriteByte(0x49);
        tramp.WriteByte(0x53);

        uintptr_t absAddr = origBytes.ReadDisp32AsAbsolute();

        // mov r11, absolute address
        tramp.WriteByte(0x49);
        tramp.WriteByte(0xbb);
        tramp.WritePointer(absAddr);

        // rex.b cmp byte ptr [r11], reg (.b is to get r11 instead of rbx -
        // doesn't affect reg)
        tramp.WriteByte(0x41);
        tramp.WriteByte(0x38);
        tramp.WriteByte(kModNoRegDisp | reg | kRegBx);

        // pop r11 (doesn't affect the flags from the cmp)
        tramp.WriteByte(0x49);
        tramp.WriteByte(0x5b);
      } else if (*origBytes == 0x40 || *origBytes == 0x41) {
        // Plain REX or REX.B
        COPY_CODES(1);
        if ((*origBytes & 0xf0) == 0x50) {
          // push/pop with Rx register
          COPY_CODES(1);
        } else if (*origBytes >= 0xb8 && *origBytes <= 0xbf) {
          // mov r32, imm32
          COPY_CODES(5);
        } else if (*origBytes == 0x8b && (origBytes[1] & kMaskMod) == kModReg) {
          // 8B /r: mov r32, r/m32
          COPY_CODES(2);
        } else if (*origBytes == 0xf7 &&
                   (origBytes[1] & (kMaskMod | kMaskReg)) ==
                       (kModReg | (0 << kRegFieldShift))) {
          // F7 /0 id: test r/m32, imm32
          COPY_CODES(6);
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes == 0x44) {
        // REX.R
        COPY_CODES(1);

        // TODO: Combine with the "0x89" case below in the REX.W section
        if (*origBytes == 0x89) {
          // mov r/m32, r32
          COPY_CODES(1);
          int len = CountModRmSib(origBytes);
          if (len < 0) {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
          COPY_CODES(len);
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes == 0x45) {
        // REX.R & REX.B
        COPY_CODES(1);

        if (*origBytes == 0x33) {
          // xor r32, r/m32
          int len = CountModRmSib(origBytes + 1);
          if (len < 0) {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
          COPY_CODES(len + 1);
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if ((*origBytes & 0xfa) == 0x48) {
        // REX.W | REX.WR | REX.WRB | REX.WB
        const BYTE rexR = (*origBytes & kMaskRexR) ? 8 : 0;
        COPY_CODES(1);

        if (*origBytes == 0x81 && (origBytes[1] & 0xf8) == 0xe8) {
          // sub r, dword
          COPY_CODES(6);
        } else if (*origBytes == 0x83) {
          // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP on imm8
          int len = CountModRmSib(origBytes + 1);
          if (len < 0) {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
          COPY_CODES(len + 2);  // 1 for 0x83, 1 for imm8
        } else if (*origBytes == 0x2b && (origBytes[1] & kMaskMod) == kModReg) {
          // sub r64, r64
          COPY_CODES(2);
        } else if (*origBytes == 0x85) {
          // 85 /r => TEST r/m32, r32
          if ((origBytes[1] & 0xc0) == 0xc0) {
            COPY_CODES(2);
          } else {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
        } else if ((*origBytes & 0xfd) == 0x89) {
          // MOV r/m64, r64 | MOV r64, r/m64
          BYTE reg;
          int len = CountModRmSib(origBytes + 1, &reg);
          if (len < 0) {
            MOZ_ASSERT(len == kModOperand64);
            if (len != kModOperand64) {
              return;
            }
            if (*origBytes == 0x89) {
              // MOV [rip+disp32], r64 would require a scratch register, which
              // we would need to save and restore on the stack. Not supported.
              return;
            }

            reg |= rexR;
            if (reg == kRegSp) {
              // Loading into rsp would require moving the stack pointer to
              // an arbitrary address temporarily. Not supported.
              return;
            }

            origBytes += 2;  // skip the MOV and MOD R/M bytes

            // The instruction MOVs 64-bit data from a RIP-relative memory
            // address (determined with a 32-bit offset from RIP) into a
            // 64-bit register.
            uintptr_t absAddr = origBytes.ReadDisp32AsAbsolute();

            --tramp;  // overwrite the REX prefix we copied above
            WriteLoadFromAbsoluteAddress(tramp, reg, absAddr);
          } else {
            COPY_CODES(len + 1);
          }
        } else if ((*origBytes & 0xf8) == 0xb8) {
          // MOV r64, imm64
          COPY_CODES(9);
        } else if (*origBytes == 0xc7) {
          // MOV r/m64, imm32
          if (origBytes[1] == 0x44) {
            // MOV [r64+disp8], imm32
            // ModR/W + SIB + disp8 + imm32
            COPY_CODES(8);
          } else {
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
        } else if (*origBytes == 0xff) {
          // JMP/4 or CALL/2
          if ((origBytes[1] & 0xc0) == 0x0 && (origBytes[1] & 0x07) == 0x5 &&
              ((origBytes[1] & 0x38) == 0x20 ||
               (origBytes[1] & 0x38) == 0x10)) {
            foundJmp = (origBytes[1] & 0x38) == 0x20;
            origBytes += 2;
            --tramp;  // overwrite the REX.W/REX.RW we copied above

            if (!GenerateJump(tramp, origBytes.ChasePointerFromDisp(),
                              foundJmp ? JumpType::Jmp : JumpType::Call,
                              origBytes, bytesRequired)) {
              return;
            }
          } else {
            // not support yet!
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
        } else if (*origBytes == 0x8d) {
          // LEA reg, addr
          if ((origBytes[1] & kMaskMod) == 0x0 &&
              (origBytes[1] & kMaskRm) == 0x5) {
            // [rip+disp32]
            // convert 32bit offset to 64bit direct and convert instruction
            // to a simple 64-bit mov
            BYTE reg = ((origBytes[1] & kMaskReg) >> kRegFieldShift) | rexR;
            origBytes += 2;
            uintptr_t absAddr = origBytes.ReadDisp32AsAbsolute();
            --tramp;  // overwrite the REX prefix we copied above
            WriteMovImm64(tramp, reg, absAddr);
          } else {
            // Above we dealt with RIP-relative instructions.  Any other
            // operand form can simply be copied.
            int len = CountModRmSib(origBytes + 1);
            // We handled the kModOperand64 -- ie RIP-relative -- case above
            MOZ_ASSERT(len > 0);
            COPY_CODES(len + 1);
          }
        } else if (*origBytes == 0x63 && (origBytes[1] & kMaskMod) == kModReg) {
          // movsxd r64, r32 (move + sign extend)
          COPY_CODES(2);
        } else {
          // not support yet!
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes == 0x66) {
        // operand override prefix
        COPY_CODES(1);
        // This is the same as the x86 version
        if (*origBytes >= 0x88 && *origBytes <= 0x8B) {
          // various MOVs
          unsigned char b = origBytes[1];
          if (((b & 0xc0) == 0xc0) ||
              (((b & 0xc0) == 0x00) && ((b & 0x07) != 0x04) &&
               ((b & 0x07) != 0x05))) {
            // REG=r, R/M=r or REG=r, R/M=[r]
            COPY_CODES(2);
          } else if ((b & 0xc0) == 0x40) {
            if ((b & 0x07) == 0x04) {
              // REG=r, R/M=[SIB + disp8]
              COPY_CODES(4);
            } else {
              // REG=r, R/M=[r + disp8]
              COPY_CODES(3);
            }
          } else {
            // complex MOV, bail
            MOZ_ASSERT_UNREACHABLE("Unrecognized MOV opcode sequence");
            return;
          }
        } else if (*origBytes == 0x44 && origBytes[1] == 0x89) {
          // mov word ptr [reg+disp8], reg
          COPY_CODES(2);
          int len = CountModRmSib(origBytes);
          if (len < 0) {
            // no way to support this yet.
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
          COPY_CODES(len);
        }
      } else if ((*origBytes & 0xf0) == 0x50) {
        // 1-byte push/pop
        COPY_CODES(1);
      } else if (*origBytes == 0x65) {
        // GS prefix
        //
        // The entry of GetKeyState on Windows 10 has the following code.
        // 65 48 8b 04 25 30 00 00 00    mov   rax,qword ptr gs:[30h]
        // (GS prefix + REX + MOV (0x8b) ...)
        if (origBytes[1] == 0x48 &&
            (origBytes[2] >= 0x88 && origBytes[2] <= 0x8b)) {
          COPY_CODES(3);
          int len = CountModRmSib(origBytes);
          if (len < 0) {
            // no way to support this yet.
            MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
            return;
          }
          COPY_CODES(len);
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes == 0x80 && origBytes[1] == 0x3d) {
        origBytes += 2;

        // cmp byte ptr [rip-relative address], imm8
        // We'll compute the absolute address and do the cmp in r11

        // push r11 (to save the old value)
        tramp.WriteByte(0x49);
        tramp.WriteByte(0x53);

        // The displacement is relative to the end of the instruction, which
        // includes the imm8 that follows it.
        uintptr_t absAddr = origBytes.ReadDisp32AsAbsolute() + 1;

        // mov r11, absolute address
        tramp.WriteByte(0x49);
        tramp.WriteByte(0xbb);
        tramp.WritePointer(absAddr);

        // cmp byte ptr [r11],...
        tramp.WriteByte(0x41);
        tramp.WriteByte(0x80);
        tramp.WriteByte(0x3b);

        // ...imm8
        COPY_CODES(1);

        // pop r11 (doesn't affect the flags from the cmp)
        tramp.WriteByte(0x49);
        tramp.WriteByte(0x5b);
      } else if (*origBytes == 0x90) {
        // nop
        COPY_CODES(1);
      } else if ((*origBytes & 0xf8) == 0xb8) {
        // MOV r32, imm32
        COPY_CODES(5);
      } else if (*origBytes == 0x31 || *origBytes == 0x33) {
        // xor r/m32, r32
        // xor r32, r/m32
        int len = CountModRmSib(origBytes + 1);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(len + 1);
      } else if (*origBytes == 0xf6) {
        // test r/m8, imm8 (used by ntdll on Windows 10 x64)
        // (no flags are affected by near jmp since there is no task switch,
        // so it is ok for a jmp to be written immediately after a test)
        BYTE subOpcode = 0;
        int nModRmSibBytes = CountModRmSib(origBytes + 1, &subOpcode);
        if (nModRmSibBytes < 0 || subOpcode != 0) {
          // Unsupported
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(2 + nModRmSibBytes);
      } else if (*origBytes == 0x85) {
        // test r/m32, r32
        int nModRmSibBytes = CountModRmSib(origBytes + 1);
        if (nModRmSibBytes < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(1 + nModRmSibBytes);
      } else if (*origBytes == 0xd1 && (origBytes[1] & kMaskMod) == kModReg) {
        // bit shifts/rotates : (SA|SH|RO|RC)(R|L) r32
        // (e.g. 0xd1 0xe0 is SAL, 0xd1 0xc8 is ROR)
        COPY_CODES(2);
      } else if (*origBytes == 0x83) {
        // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP on imm8
        int len = CountModRmSib(origBytes + 1);
        if (len < 0) {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(len + 2);  // 1 for 0x83, 1 for imm8
      } else if (*origBytes == 0xc3) {
        // ret
        COPY_CODES(1);
      } else if (*origBytes == 0xcc) {
        // int 3
        COPY_CODES(1);
      } else if (*origBytes == 0xe8 || *origBytes == 0xe9) {
        // CALL (0xe8) or JMP (0xe9) 32bit offset
        foundJmp = *origBytes == 0xe9;
        ++origBytes;

        if (!GenerateJump(tramp, origBytes.ReadDisp32AsAbsolute(),
                          foundJmp ? JumpType::Jmp : JumpType::Call, origBytes,
                          bytesRequired)) {
          return;
        }
      } else if (*origBytes >= 0x73 && *origBytes <= 0x75) {
        // 73 cb    JAE rel8
        // 74 cb    JE  rel8
        // 75 cb    JNE rel8
        const JumpType kJumpTypes[] = {JumpType::Jae, JumpType::Je,
                                       JumpType::Jne};
        auto jumpType = kJumpTypes[*origBytes - 0x73];
        int8_t offset = origBytes[1];
        if (offset < 0) {
          // We don't support backwards relative jumps. If we want to in the
          // future we should find a good way to test them.
          MOZ_ASSERT_UNREACHABLE(
              "Unrecognized opcode sequence - backwards relative jump");
          return;
        }

        origBytes += 2;

        if (!GenerateJump(tramp, origBytes.OffsetToAbsolute(offset), jumpType,
                          origBytes, bytesRequired)) {
          return;
        }
      } else if (*origBytes == 0x78) {
        // 78 cb    JS rel8
        int8_t offset = origBytes[1];
        if (offset < 0) {
          // We don't support backwards relative jumps. If we want to in the
          // future we should find a good way to test them.
          MOZ_ASSERT_UNREACHABLE(
              "Unrecognized opcode sequence - backwards relative jump");
          return;
        }

        origBytes += 2;

        if (!GenerateJump(tramp, origBytes.OffsetToAbsolute(offset),
                          JumpType::Js, origBytes, bytesRequired)) {
          return;
        }
      } else if (*origBytes == 0xff) {
        uint8_t mod = origBytes[1] & kMaskMod;
        uint8_t reg = (origBytes[1] & kMaskReg) >> kRegFieldShift;
        uint8_t rm = origBytes[1] & kMaskRm;
        if (mod == kModReg && (reg == 0 || reg == 1 || reg == 2 || reg == 6)) {
          // INC|DEC|CALL|PUSH r64
          COPY_CODES(2);
        } else if (mod == kModNoRegDisp && reg == 2 &&
                   rm == kRmNoRegDispDisp32) {
          // FF 15    CALL [disp32]
          origBytes += 2;
          if (!GenerateJump(tramp, origBytes.ChasePointerFromDisp(),
                            JumpType::Call, origBytes, bytesRequired)) {
            return;
          }
        } else if (reg == 4) {
          // FF /4 (Opcode=ff, REG=4): JMP r/m
          if (mod == kModNoRegDisp && rm == kRmNoRegDispDisp32) {
            // FF 25    JMP [disp32]
            foundJmp = true;

            origBytes += 2;

            uintptr_t jmpDest = origBytes.ChasePointerFromDisp();

            if (!GenerateJump(tramp, jmpDest, JumpType::Jmp, origBytes,
                              bytesRequired)) {
              return;
            }
          } else {
            // JMP r/m except JMP [disp32]
            int len = CountModRmSib(origBytes + 1);
            if (len < 0) {
              // RIP-relative not yet supported
              MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
              return;
            }

            COPY_CODES(len + 1);

            foundJmp = true;
          }
        } else {
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
      } else if (*origBytes == 0xc6) {
        // mov [r+d], imm8
        int len = CountModRmSib(origBytes + 1);
        if (len < 0) {
          // RIP-relative not yet supported
          MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
          return;
        }
        COPY_CODES(len + 2);
      } else {
        MOZ_ASSERT_UNREACHABLE("Unrecognized opcode sequence");
        return;
      }
    }
#elif defined(_M_ARM64)

    // The number of bytes required to facilitate a detour depends on the
    // proximity of the hook function to the target function. In the best case,
    // we can branch within +/- 128MB of the current location, requiring only
    // 4 bytes. In the worst case, we need 16 bytes to load an absolute address
    // into a register and then branch to it.
    const uint32_t bytesRequiredFromDecode =
        (mFlags.value() & DetourFlags::eTestOnlyForceShortPatch)
            ? 4
            : kWorstCaseBytesRequired;

    while (origBytes.GetOffset() < bytesRequiredFromDecode) {
      uintptr_t curPC = origBytes.GetCurrentAbsolute();
      uint32_t curInst = origBytes.ReadNextInstruction();

      Result<arm64::LoadOrBranch, arm64::PCRelCheckError> pcRelInfo =
          arm64::CheckForPCRel(curPC, curInst);
      if (pcRelInfo.isErr()) {
        if (pcRelInfo.unwrapErr() ==
            arm64::PCRelCheckError::InstructionNotPCRel) {
          // Instruction is not PC-relative, we can just copy it verbatim
          tramp.WriteInstruction(curInst);
          continue;
        }

        // At this point we have determined that there is no decoder available
        // for the current, PC-relative, instruction.

        // origBytes is now pointing one instruction past the one that we
        // need the trampoline to jump back to.
        if (!origBytes.BackUpOneInstruction()) {
          return;
        }

        break;
      }

      if (pcRelInfo.inspect().mType == arm64::LoadOrBranch::Type::Branch) {
        // We need to branch to the original target of this instruction.
        // This is a problem because on the first pass
        // GetCurrentRemoteAddress() is 0, so the offset may be too large for
        // BuildUnconditionalBranchImm. We don't need this for now on ARM64.
        // so just behave like we don't have a decoder.

        // origBytes is now pointing one instruction past the one that we
        // need the trampoline to jump back to.
        if (!origBytes.BackUpOneInstruction()) {
          return;
        }

        break;
      }

      MOZ_ASSERT(pcRelInfo.inspect().mType == arm64::LoadOrBranch::Type::Load);
      // We need to load an absolute address into a particular register
      tramp.WriteLoadLiteral(pcRelInfo.inspect().mAbsAddress,
                             pcRelInfo.inspect().mDestReg);
    }

#else
#  error "Unknown processor type"
#endif

    if (origBytes.GetOffset() > 100) {
      // printf ("Too big!");
      return;
    }

#if defined(_M_IX86)
    if (pJmp32 >= 0) {
      // Jump directly to the original target of the jump instead of jumping to
      // the original function. Adjust jump target displacement to jump location
      // in the trampoline.
      tramp.AdjustDisp32AtOffset(pJmp32 + 1, origBytes.GetBaseAddress());
    } else {
      tramp.WriteByte(0xe9);  // jmp
      tramp.WriteDisp32(origBytes.GetAddress());
    }
#elif defined(_M_X64)
    // If we found a Jmp, we don't need to add another instruction. However,
    // if we found a _conditional_ jump or a CALL (or no control operations
    // at all) then we still need to run the rest of aOriginalFunction.
    if (!foundJmp) {
      if (!GenerateJump(tramp, origBytes.GetAddress(), JumpType::Jmp, origBytes,
                        bytesRequired)) {
        return;
      }
    }
#elif defined(_M_ARM64)
    // Let's find out how many bytes we have available to us for patching
    uint32_t numBytesForPatching = tramp.GetCurrentExecutableCodeLen();

    if (!numBytesForPatching) {
      // There's nothing we can do
      return;
    }

    if (tramp.IsNull()) {
      // Recursive case
      HMODULE targetModule = nullptr;

      if (numBytesForPatching < kWorstCaseBytesRequired) {
        if (!::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(origBytes.GetBaseAddress()),
                &targetModule)) {
          return;
        }
      }

      Maybe<TrampPoolT> maybeTrampPool = DoReserve(targetModule);
      MOZ_ASSERT(maybeTrampPool);
      if (!maybeTrampPool) {
        return;
      }

      Maybe<Trampoline<MMPolicyT>> maybeRealTramp(
          maybeTrampPool.ref().GetNextTrampoline());
      if (!maybeRealTramp) {
        return;
      }

      origBytes.Rewind();
      CreateTrampoline(origBytes, maybeTrampPool.ptr(), maybeRealTramp.ref(),
                       aDest, aOutTramp);
      return;
    }

    // Write the branch from the trampoline back to the original code

    tramp.WriteLoadLiteral(origBytes.GetAddress(), 16);
    tramp.WriteInstruction(arm64::BuildUnconditionalBranchToRegister(16));
#else
#  error "Unsupported processor architecture"
#endif

    // The trampoline is now complete.
    void* trampPtr = tramp.EndExecutableCode();
    if (!trampPtr) {
      return;
    }

#ifdef _M_X64
    if constexpr (MMPolicyT::kSupportsUnwindInfo) {
      DebugOnly<bool> unwindInfoAdded = tramp.AddUnwindInfo(
          origBytes.GetBaseAddress(), origBytes.GetOffset());
      MOZ_ASSERT(unwindInfoAdded);
    }
#endif  // _M_X64

    WritableTargetFunction<MMPolicyT> target(origBytes.Promote());
    if (!target) {
      return;
    }

    do {
      // Now patch the original function.
      // When we're instructed to apply a non-default patch, apply it and exit.
      // If non-default patching fails, bail out, no fallback.
      // Otherwise, we go straight to the default patch.

#if defined(_M_X64)
      if (use10BytePatch) {
        if (!Apply10BytePatch(aTrampPool, trampPtr, target, aDest)) {
          return;
        }
        break;
      }
#elif defined(_M_ARM64)
      if (numBytesForPatching < kWorstCaseBytesRequired) {
        if (!Apply4BytePatch(aTrampPool, trampPtr, target, aDest)) {
          return;
        }
        break;
      }
#endif

      PrimitiveT::ApplyDefaultPatch(target, aDest);
    } while (false);

    // Output the trampoline, thus signalling that this call was a success. This
    // must happen before our patched function can be reached from another
    // thread, so before we commit the target code (bug 1838286).
    *aOutTramp = trampPtr;

    if (!target.Commit()) {
      *aOutTramp = nullptr;
    }
  }
};

}  // namespace interceptor
}  // namespace mozilla

#endif  // mozilla_interceptor_PatcherDetour_h
