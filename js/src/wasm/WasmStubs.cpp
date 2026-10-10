/*
 * Copyright 2015 Mozilla Foundation
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

#include "wasm/WasmStubs.h"

#include <algorithm>
#include <type_traits>

#include "jit/ABIArgGenerator.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/RegisterAllocator.h"
#include "js/Printf.h"
#include "util/Memory.h"
#include "wasm/WasmCode.h"
#include "wasm/WasmGenerator.h"
#include "wasm/WasmInstance.h"
#include "wasm/WasmPI.h"
#include "wasm/WasmStacks.h"

#include "jit/MacroAssembler-inl.h"
#include "wasm/WasmInstance-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

using mozilla::DebugOnly;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;

using MIRTypeVector = Vector<jit::MIRType, 8, SystemAllocPolicy>;
using ABIArgMIRTypeIter = jit::ABIArgIter<MIRTypeVector>;

/*****************************************************************************/
// ABIResultIter implementation

static uint32_t ResultStackSize(ValType type) {
  switch (type.kind()) {
    case ValType::I32:
      return ABIResult::StackSizeOfInt32;
    case ValType::I64:
      return ABIResult::StackSizeOfInt64;
    case ValType::F32:
      return ABIResult::StackSizeOfFloat;
    case ValType::F64:
      return ABIResult::StackSizeOfDouble;
#ifdef ENABLE_WASM_SIMD
    case ValType::V128:
      return ABIResult::StackSizeOfV128;
#endif
    case ValType::Ref:
      return ABIResult::StackSizeOfPtr;
    default:
      MOZ_CRASH("Unexpected result type");
  }
}

// Compute the size of the stack slot that the wasm ABI requires be allocated
// for a particular MIRType.  Note that this sometimes differs from the
// MIRType's natural size.  See also ResultStackSize above and ABIResult::size()
// and ABIResultIter below.

uint32_t js::wasm::MIRTypeToABIResultSize(jit::MIRType type) {
  switch (type) {
    case MIRType::Int32:
      return ABIResult::StackSizeOfInt32;
    case MIRType::Int64:
      return ABIResult::StackSizeOfInt64;
    case MIRType::Float32:
      return ABIResult::StackSizeOfFloat;
    case MIRType::Double:
      return ABIResult::StackSizeOfDouble;
#ifdef ENABLE_WASM_SIMD
    case MIRType::Simd128:
      return ABIResult::StackSizeOfV128;
#endif
    case MIRType::Pointer:
    case MIRType::WasmAnyRef:
      return ABIResult::StackSizeOfPtr;
    default:
      MOZ_CRASH("MIRTypeToABIResultSize - unhandled case");
  }
}

uint32_t ABIResult::size() const { return ResultStackSize(type()); }

void ABIResultIter::settleRegister(ValType type) {
  MOZ_ASSERT(!done());
  MOZ_ASSERT_IF(direction_ == Next, index() < MaxRegisterResults);
  MOZ_ASSERT_IF(direction_ == Prev, index() >= count_ - MaxRegisterResults);
  static_assert(MaxRegisterResults == 1, "expected a single register result");

  switch (type.kind()) {
    case ValType::I32:
      cur_ = ABIResult(type, ReturnReg);
      break;
    case ValType::I64:
      cur_ = ABIResult(type, ReturnReg64);
      break;
    case ValType::F32:
      cur_ = ABIResult(type, ReturnFloat32Reg);
      break;
    case ValType::F64:
      cur_ = ABIResult(type, ReturnDoubleReg);
      break;
    case ValType::Ref:
      cur_ = ABIResult(type, ReturnReg);
      break;
#ifdef ENABLE_WASM_SIMD
    case ValType::V128:
      cur_ = ABIResult(type, ReturnSimd128Reg);
      break;
#endif
    default:
      MOZ_CRASH("Unexpected result type");
  }
}

void ABIResultIter::settleNext() {
  MOZ_ASSERT(direction_ == Next);
  MOZ_ASSERT(!done());

  uint32_t typeIndex = count_ - index_ - 1;
  ValType type = type_[typeIndex];

  if (index_ < MaxRegisterResults) {
    settleRegister(type);
    return;
  }

  cur_ = ABIResult(type, nextStackOffset_);
  nextStackOffset_ += ResultStackSize(type);
}

void ABIResultIter::settlePrev() {
  MOZ_ASSERT(direction_ == Prev);
  MOZ_ASSERT(!done());
  uint32_t typeIndex = index_;
  ValType type = type_[typeIndex];

  if (count_ - index_ - 1 < MaxRegisterResults) {
    settleRegister(type);
    return;
  }

  uint32_t size = ResultStackSize(type);
  MOZ_ASSERT(nextStackOffset_ >= size);
  nextStackOffset_ -= size;
  cur_ = ABIResult(type, nextStackOffset_);
}

#ifdef WASM_CODEGEN_DEBUG
template <class Closure>
static void GenPrint(DebugChannel channel, MacroAssembler& masm,
                     const Maybe<Register>& taken, SymbolicAddress builtin,
                     Closure passArgAndCall) {
  if (!IsCodegenDebugEnabled(channel)) {
    return;
  }

  AllocatableRegisterSet regs(RegisterSet::All());
  LiveRegisterSet save(regs.asLiveSet());
  masm.PushRegsInMask(save);

  if (taken) {
    regs.take(taken.value());
  }
  Register temp = regs.takeAnyGeneral();

  {
    MOZ_ASSERT(MaybeGetJitContext(),
               "codegen debug checks require a jit context");
#  ifdef JS_CODEGEN_ARM64
    if (IsCompilingWasm()) {
      masm.setupWasmABICall(builtin);
    } else {
      // JS ARM64 has an extra stack pointer which is not managed in WASM.
      masm.setupUnalignedABICall(temp);
    }
#  else
    masm.setupUnalignedABICall(temp);
#  endif
    passArgAndCall(IsCompilingWasm(), temp);
  }

  masm.PopRegsInMask(save);
}

static void GenPrintf(DebugChannel channel, MacroAssembler& masm,
                      const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  UniqueChars str = JS_vsmprintf(fmt, ap);
  va_end(ap);

  GenPrint(channel, masm, Nothing(), SymbolicAddress::PrintText,
           [&](bool inWasm, Register temp) {
             // If we've gone this far, it means we're actually using the
             // debugging strings. In this case, we leak them! This is only for
             // debugging, and doing the right thing is cumbersome (in Ion, it'd
             // mean add a vec of strings to the IonScript; in wasm, it'd mean
             // add it to the current Module and serialize it properly).
             const char* text = str.release();

             masm.movePtr(ImmPtr((void*)text, ImmPtr::NoCheckToken()), temp);
             masm.passABIArg(temp);
             if (inWasm) {
               masm.callDebugWithABI(SymbolicAddress::PrintText);
             } else {
               using Fn = void (*)(const char* output);
               masm.callWithABI<Fn, PrintText>(
                   ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
             }
           });
}

static void GenPrintIsize(DebugChannel channel, MacroAssembler& masm,
                          const Register& src) {
  GenPrint(channel, masm, Some(src), SymbolicAddress::PrintI32,
           [&](bool inWasm, Register _temp) {
             masm.passABIArg(src);
             if (inWasm) {
               masm.callDebugWithABI(SymbolicAddress::PrintI32);
             } else {
               using Fn = void (*)(int32_t val);
               masm.callWithABI<Fn, PrintI32>(
                   ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
             }
           });
}

static void GenPrintPtr(DebugChannel channel, MacroAssembler& masm,
                        const Register& src) {
  GenPrint(channel, masm, Some(src), SymbolicAddress::PrintPtr,
           [&](bool inWasm, Register _temp) {
             masm.passABIArg(src);
             if (inWasm) {
               masm.callDebugWithABI(SymbolicAddress::PrintPtr);
             } else {
               using Fn = void (*)(uint8_t* val);
               masm.callWithABI<Fn, PrintPtr>(
                   ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
             }
           });
}

static void GenPrintI64(DebugChannel channel, MacroAssembler& masm,
                        const Register64& src) {
#  if JS_BITS_PER_WORD == 64
  GenPrintf(channel, masm, "i64 ");
  GenPrintIsize(channel, masm, src.reg);
#  else
  GenPrintf(channel, masm, "i64(");
  GenPrintIsize(channel, masm, src.low);
  GenPrintIsize(channel, masm, src.high);
  GenPrintf(channel, masm, ") ");
#  endif
}

static void GenPrintF32(DebugChannel channel, MacroAssembler& masm,
                        const FloatRegister& src) {
  GenPrint(channel, masm, Nothing(), SymbolicAddress::PrintF32,
           [&](bool inWasm, Register temp) {
             masm.passABIArg(src, ABIType::Float32);
             if (inWasm) {
               masm.callDebugWithABI(SymbolicAddress::PrintF32);
             } else {
               using Fn = void (*)(float val);
               masm.callWithABI<Fn, PrintF32>(
                   ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
             }
           });
}

static void GenPrintF64(DebugChannel channel, MacroAssembler& masm,
                        const FloatRegister& src) {
  GenPrint(channel, masm, Nothing(), SymbolicAddress::PrintF64,
           [&](bool inWasm, Register temp) {
             masm.passABIArg(src, ABIType::Float64);
             if (inWasm) {
               masm.callDebugWithABI(SymbolicAddress::PrintF64);
             } else {
               using Fn = void (*)(double val);
               masm.callWithABI<Fn, PrintF64>(
                   ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
             }
           });
}

#  ifdef ENABLE_WASM_SIMD
static void GenPrintV128(DebugChannel channel, MacroAssembler& masm,
                         const FloatRegister& src) {
  // TODO: We might try to do something meaningful here once SIMD data are
  // aligned and hence C++-ABI compliant.  For now, just make ourselves visible.
  GenPrintf(channel, masm, "v128");
}
#  endif
#else
static void GenPrintf(DebugChannel channel, MacroAssembler& masm,
                      const char* fmt, ...) {}
static void GenPrintIsize(DebugChannel channel, MacroAssembler& masm,
                          const Register& src) {}
static void GenPrintPtr(DebugChannel channel, MacroAssembler& masm,
                        const Register& src) {}
static void GenPrintI64(DebugChannel channel, MacroAssembler& masm,
                        const Register64& src) {}
static void GenPrintF32(DebugChannel channel, MacroAssembler& masm,
                        const FloatRegister& src) {}
static void GenPrintF64(DebugChannel channel, MacroAssembler& masm,
                        const FloatRegister& src) {}
#  ifdef ENABLE_WASM_SIMD
static void GenPrintV128(DebugChannel channel, MacroAssembler& masm,
                         const FloatRegister& src) {}
#  endif
#endif

static bool FinishOffsets(MacroAssembler& masm, Offsets* offsets) {
  // On old ARM hardware, constant pools could be inserted and they need to
  // be flushed before considering the size of the masm.
  masm.flushBuffer();
  offsets->end = masm.size();
  return !masm.oom();
}

template <class VectorT>
static unsigned StackArgBytesHelper(const VectorT& args, ABIKind kind) {
  ABIArgIter<VectorT> iter(args, kind);
  while (!iter.done()) {
    iter++;
  }
  return iter.stackBytesConsumedSoFar();
}

template <class VectorT>
static unsigned StackArgBytesForNativeABI(const VectorT& args) {
  return StackArgBytesHelper<VectorT>(args, ABIKind::System);
}

template <class VectorT>
static unsigned StackArgBytesForWasmABI(const VectorT& args) {
  return StackArgBytesHelper<VectorT>(args, ABIKind::Wasm);
}

static unsigned StackArgBytesForWasmABI(const FuncType& funcType) {
  ArgTypeVector args(funcType);
  return StackArgBytesForWasmABI(args);
}

static void SetupABIArguments(MacroAssembler& masm, const FuncExport& fe,
                              const FuncType& funcType, Register argv,
                              Register scratch) {
  // Copy parameters out of argv and into the registers/stack-slots specified by
  // the wasm ABI.
  //
  // SetupABIArguments are only used for C++ -> wasm calls through callExport(),
  // and V128 and Ref types (other than externref) are not currently allowed.
  ArgTypeVector args(funcType);
  for (ABIArgIter iter(args, ABIKind::Wasm); !iter.done(); iter++) {
    unsigned argOffset = iter.index() * sizeof(ExportArg);
    Address src(argv, argOffset);
    MIRType type = iter.mirType();
    switch (iter->kind()) {
      case ABIArg::GPR:
        if (type == MIRType::Int32) {
          masm.load32(src, iter->gpr());
        } else if (type == MIRType::Int64) {
          masm.load64(src, iter->gpr64());
        } else if (type == MIRType::WasmAnyRef) {
          masm.loadPtr(src, iter->gpr());
        } else if (type == MIRType::StackResults) {
          MOZ_ASSERT(args.isSyntheticStackResultPointerArg(iter.index()));
          masm.loadPtr(src, iter->gpr());
        } else {
          MOZ_CRASH("unknown GPR type");
        }
        break;
#ifdef JS_CODEGEN_REGISTER_PAIR
      case ABIArg::GPR_PAIR:
        if (type == MIRType::Int64) {
          masm.load64(src, iter->gpr64());
        } else {
          MOZ_CRASH("wasm uses hardfp for function calls.");
        }
        break;
#endif
      case ABIArg::FPU: {
        static_assert(sizeof(ExportArg) >= jit::Simd128DataSize,
                      "ExportArg must be big enough to store SIMD values");
        switch (type) {
          case MIRType::Double:
            masm.loadDouble(src, iter->fpu());
            break;
          case MIRType::Float32:
            masm.loadFloat32(src, iter->fpu());
            break;
          case MIRType::Simd128:
#ifdef ENABLE_WASM_SIMD
            // This is only used by the testing invoke path,
            // wasmLosslessInvoke, and is guarded against in normal JS-API
            // call paths.
            masm.loadUnalignedSimd128(src, iter->fpu());
            break;
#else
            MOZ_CRASH("V128 not supported in SetupABIArguments");
#endif
          default:
            MOZ_CRASH("unexpected FPU type");
            break;
        }
        break;
      }
      case ABIArg::Stack:
        switch (type) {
          case MIRType::Int32:
            masm.load32(src, scratch);
            masm.storePtr(scratch, Address(masm.getStackPointer(),
                                           iter->offsetFromArgBase()));
            break;
          case MIRType::Int64: {
            RegisterOrSP sp = masm.getStackPointer();
            masm.copy64(src, Address(sp, iter->offsetFromArgBase()), scratch);
            break;
          }
          case MIRType::WasmAnyRef:
            masm.loadPtr(src, scratch);
            masm.storePtr(scratch, Address(masm.getStackPointer(),
                                           iter->offsetFromArgBase()));
            break;
          case MIRType::Double: {
            ScratchDoubleScope fpscratch(masm);
            masm.loadDouble(src, fpscratch);
            masm.storeDouble(fpscratch, Address(masm.getStackPointer(),
                                                iter->offsetFromArgBase()));
            break;
          }
          case MIRType::Float32: {
            ScratchFloat32Scope fpscratch(masm);
            masm.loadFloat32(src, fpscratch);
            masm.storeFloat32(fpscratch, Address(masm.getStackPointer(),
                                                 iter->offsetFromArgBase()));
            break;
          }
          case MIRType::Simd128: {
#ifdef ENABLE_WASM_SIMD
            // This is only used by the testing invoke path,
            // wasmLosslessInvoke, and is guarded against in normal JS-API
            // call paths.
            ScratchSimd128Scope fpscratch(masm);
            masm.loadUnalignedSimd128(src, fpscratch);
            masm.storeUnalignedSimd128(
                fpscratch,
                Address(masm.getStackPointer(), iter->offsetFromArgBase()));
            break;
#else
            MOZ_CRASH("V128 not supported in SetupABIArguments");
#endif
          }
          case MIRType::StackResults: {
            MOZ_ASSERT(args.isSyntheticStackResultPointerArg(iter.index()));
            masm.loadPtr(src, scratch);
            masm.storePtr(scratch, Address(masm.getStackPointer(),
                                           iter->offsetFromArgBase()));
            break;
          }
          default:
            MOZ_CRASH("unexpected stack arg type");
        }
        break;
      case ABIArg::Uninitialized:
        MOZ_CRASH("Uninitialized ABIArg kind");
    }
  }
}

static void StoreRegisterResult(MacroAssembler& masm, const FuncExport& fe,
                                const FuncType& funcType, Register loc) {
  ResultType results = ResultType::Vector(funcType.results());
  DebugOnly<bool> sawRegisterResult = false;
  for (ABIResultIter iter(results); !iter.done(); iter.next()) {
    const ABIResult& result = iter.cur();
    if (result.inRegister()) {
      MOZ_ASSERT(!sawRegisterResult);
      sawRegisterResult = true;
      switch (result.type().kind()) {
        case ValType::I32:
          masm.store32(result.gpr(), Address(loc, 0));
          break;
        case ValType::I64:
          masm.store64(result.gpr64(), Address(loc, 0));
          break;
        case ValType::V128:
#ifdef ENABLE_WASM_SIMD
          masm.storeUnalignedSimd128(result.fpr(), Address(loc, 0));
          break;
#else
          MOZ_CRASH("V128 not supported in StoreABIReturn");
#endif
        case ValType::F32:
          masm.storeFloat32(result.fpr(), Address(loc, 0));
          break;
        case ValType::F64:
          masm.storeDouble(result.fpr(), Address(loc, 0));
          break;
        case ValType::Ref:
          masm.storePtr(result.gpr(), Address(loc, 0));
          break;
      }
    }
  }
  MOZ_ASSERT(sawRegisterResult == (results.length() > 0));
}

#if defined(JS_CODEGEN_ARM)
// The ARM system ABI also includes d15 & s31 in the non volatile float
// registers. Also exclude lr (a.k.a. r14) as we preserve it manually.
static const LiveRegisterSet NonVolatileRegs = LiveRegisterSet(
    GeneralRegisterSet(Registers::NonVolatileMask &
                       ~(Registers::SetType(1) << Registers::lr)),
    FloatRegisterSet(FloatRegisters::NonVolatileMask |
                     (FloatRegisters::SetType(1) << FloatRegisters::d15) |
                     (FloatRegisters::SetType(1) << FloatRegisters::s31)));
#elif defined(JS_CODEGEN_ARM64)
// Exclude the Link Register (x30) because it is preserved manually.
//
// Include x16 (scratch) to make a 16-byte aligned amount of integer registers.
// Include d31 (scratch) to make a 16-byte aligned amount of floating registers.
static const LiveRegisterSet NonVolatileRegs = LiveRegisterSet(
    GeneralRegisterSet((Registers::NonVolatileMask &
                        ~(Registers::SetType(1) << Registers::lr)) |
                       (Registers::SetType(1) << Registers::x16)),
    FloatRegisterSet(FloatRegisters::NonVolatileMask |
                     FloatRegisters::NonAllocatableMask));
#else
static const LiveRegisterSet NonVolatileRegs =
    LiveRegisterSet(GeneralRegisterSet(Registers::NonVolatileMask),
                    FloatRegisterSet(FloatRegisters::NonVolatileMask));
#endif

#ifdef JS_CODEGEN_ARM64
static const unsigned WasmPushSize = 16;
#else
static const unsigned WasmPushSize = sizeof(void*);
#endif

static void AssertExpectedSP(MacroAssembler& masm) {
#ifdef JS_CODEGEN_ARM64
  MOZ_ASSERT(sp.Is(masm.GetStackPointer64()));
#  ifdef DEBUG
  // Since we're asserting that SP is the currently active stack pointer,
  // let's also in effect assert that PSP is dead -- by setting it to 1, so as
  // to cause to cause any attempts to use it to segfault in an easily
  // identifiable way.
  masm.asVIXL().Mov(PseudoStackPointer64, 1);
#  endif
#endif
}

template <class Operand>
static void WasmPush(MacroAssembler& masm, const Operand& op) {
#ifdef JS_CODEGEN_ARM64
  // Allocate a pad word so that SP can remain properly aligned.  |op| will be
  // written at the lower-addressed of the two words pushed here.
  masm.reserveStack(WasmPushSize);
  masm.storePtr(op, Address(masm.getStackPointer(), 0));
#else
  masm.Push(op);
#endif
}

static void WasmPop(MacroAssembler& masm, Register r) {
#ifdef JS_CODEGEN_ARM64
  // Also pop the pad word allocated by WasmPush.
  masm.loadPtr(Address(masm.getStackPointer(), 0), r);
  masm.freeStack(WasmPushSize);
#else
  masm.Pop(r);
#endif
}

static void MoveSPForJitABI(MacroAssembler& masm) {
#ifdef JS_CODEGEN_ARM64
  masm.moveStackPtrTo(PseudoStackPointer);
#endif
}

static void CallFuncExport(MacroAssembler& masm, const FuncExport& fe,
                           const Maybe<ImmPtr>& funcPtr) {
  MOZ_ASSERT(fe.hasEagerStubs() == !funcPtr);
  MoveSPForJitABI(masm);
  if (funcPtr) {
    masm.call(*funcPtr);
  } else {
    masm.call(CallSiteDesc(CallSiteKind::Func), fe.funcIndex());
  }
}

// Generate a stub that enters wasm from a C++ caller via the native ABI. The
// signature of the entry point is Module::ExportFuncPtr. The exported wasm
// function has an ABI derived from its specific signature, so this function
// must map from the ABI of ExportFuncPtr to the export's signature's ABI.
static bool GenerateInterpEntry(MacroAssembler& masm, const FuncExport& fe,
                                const FuncType& funcType,
                                const Maybe<ImmPtr>& funcPtr,
                                Offsets* offsets) {
  AutoCreatedBy acb(masm, "GenerateInterpEntry");

  AssertExpectedSP(masm);

  // UBSAN expects that the word before a C++ function pointer is readable for
  // some sort of generated assertion.
  //
  // These interp entry points can sometimes be output at the beginning of a
  // code page allocation, which will cause access violations when called with
  // UBSAN enabled.
  //
  // Insert some padding in this case by inserting a breakpoint before we align
  // our code. This breakpoint will misalign the code buffer (which was aligned
  // due to being at the beginning of the buffer), which will then be aligned
  // and have at least one word of padding before this entry point.
  if (masm.currentOffset() == 0) {
    masm.breakpoint();
  }

  masm.haltingAlign(CodeAlignment);

  // Double check that the first word is available for UBSAN; see above.
  static_assert(CodeAlignment >= sizeof(uintptr_t));
  MOZ_ASSERT_IF(!masm.oom(), masm.currentOffset() >= sizeof(uintptr_t));

  offsets->begin = masm.currentOffset();

  // Save the return address if it wasn't already saved by the call insn.
#ifdef JS_USE_LINK_REGISTER
#  if defined(JS_CODEGEN_ARM) || defined(JS_CODEGEN_MIPS64) || \
      defined(JS_CODEGEN_LOONG64) || defined(JS_CODEGEN_RISCV64)
  masm.pushReturnAddress();
#  elif defined(JS_CODEGEN_ARM64)
  // WasmPush updates framePushed() unlike pushReturnAddress(), but that's
  // cancelled by the setFramePushed() below.
  WasmPush(masm, lr);
#  else
  MOZ_CRASH("Implement this");
#  endif
#endif

  // Save all caller non-volatile registers before we clobber them here and in
  // the wasm callee (which does not preserve non-volatile registers).
  masm.setFramePushed(0);
  masm.PushRegsInMask(NonVolatileRegs);

  const unsigned nonVolatileRegsPushSize =
      MacroAssembler::PushRegsInMaskSizeInBytes(NonVolatileRegs);

  MOZ_ASSERT(masm.framePushed() == nonVolatileRegsPushSize);

  // Put the 'argv' argument into a non-argument/return/instance register so
  // that we can use 'argv' while we fill in the arguments for the wasm callee.
  // Use a second non-argument/return register as temporary scratch.
  Register argv = ABINonArgReturnReg0;
  Register scratch = ABINonArgReturnReg1;

  // scratch := SP
  masm.moveStackPtrTo(scratch);

  // Dynamically align the stack since ABIStackAlignment is not necessarily
  // WasmStackAlignment. Preserve SP so it can be restored after the call.
#ifdef JS_CODEGEN_ARM64
  static_assert(WasmStackAlignment == 16, "ARM64 SP alignment");
#else
  masm.andToStackPtr(Imm32(~(WasmStackAlignment - 1)));
#endif
  masm.assertStackAlignment(WasmStackAlignment);

  // Create a fake frame: just previous RA and an FP.
  const size_t FakeFrameSize = 2 * sizeof(void*);
#ifdef JS_CODEGEN_ARM64
  masm.Ldr(ARMRegister(ABINonArgReturnReg0, 64),
           MemOperand(ARMRegister(scratch, 64), nonVolatileRegsPushSize));
#else
  masm.Push(Address(scratch, nonVolatileRegsPushSize));
#endif
  // Store fake wasm register state. Ensure the frame pointer passed by the C++
  // caller doesn't have the ExitFPTag bit set to not confuse frame iterators.
  // This bit shouldn't be set if C++ code is using frame pointers, so this has
  // no effect on native stack unwinders.
  masm.andPtr(Imm32(int32_t(~ExitFPTag)), FramePointer);
#ifdef JS_CODEGEN_ARM64
  masm.asVIXL().Push(ARMRegister(ABINonArgReturnReg0, 64),
                     ARMRegister(FramePointer, 64));
  masm.moveStackPtrTo(FramePointer);
#else
  masm.Push(FramePointer);
#endif

  masm.moveStackPtrTo(FramePointer);
  masm.setFramePushed(0);
#ifdef JS_CODEGEN_ARM64
  DebugOnly<size_t> fakeFramePushed = 0;
#else
  DebugOnly<size_t> fakeFramePushed = sizeof(void*);
  masm.Push(scratch);
#endif

  // Read the arguments of wasm::ExportFuncPtr according to the native ABI.
  // The entry stub's frame is 1 word.
  const unsigned argBase = sizeof(void*) + nonVolatileRegsPushSize;
  ABIArgGenerator abi(ABIKind::System);
  ABIArg arg;

  // arg 1: ExportArg*
  arg = abi.next(MIRType::Pointer);
  if (arg.kind() == ABIArg::GPR) {
    masm.movePtr(arg.gpr(), argv);
  } else {
    masm.loadPtr(Address(scratch, argBase + arg.offsetFromArgBase()), argv);
  }

  // Arg 2: Instance*
  arg = abi.next(MIRType::Pointer);
  if (arg.kind() == ABIArg::GPR) {
    masm.movePtr(arg.gpr(), InstanceReg);
  } else {
    masm.loadPtr(Address(scratch, argBase + arg.offsetFromArgBase()),
                 InstanceReg);
  }

  WasmPush(masm, InstanceReg);

  // Save 'argv' on the stack so that we can recover it after the call.
  WasmPush(masm, argv);

  MOZ_ASSERT(masm.framePushed() == 2 * WasmPushSize + fakeFramePushed,
             "expected instance, argv, and fake frame");
  uint32_t frameSizeBeforeCall = masm.framePushed();

  // Align (missing) results area to WasmStackAlignment boudary. Return calls
  // expect arguments to not overlap with results or other slots.
  unsigned aligned =
      AlignBytes(masm.framePushed() + FakeFrameSize, WasmStackAlignment);
  masm.reserveStack(aligned - masm.framePushed() + FakeFrameSize);

  // Reserve stack space for the wasm call.
  unsigned argDecrement = StackDecrementForCall(
      WasmStackAlignment, aligned, StackArgBytesForWasmABI(funcType));
  masm.reserveStack(argDecrement);

  // Copy parameters out of argv and into the wasm ABI registers/stack-slots.
  SetupABIArguments(masm, fe, funcType, argv, scratch);

  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());

  masm.storePtr(InstanceReg, Address(masm.getStackPointer(),
                                     WasmCalleeInstanceOffsetBeforeCall));

  // Call into the real function. Note that, due to the throw stub, fp, instance
  // and pinned registers may be clobbered.
  masm.assertStackAlignment(WasmStackAlignment);
  GenerateEnterWasmFPEnvironment(masm, InstanceReg);
  CallFuncExport(masm, fe, funcPtr);
  masm.assertStackAlignment(WasmStackAlignment);

  // Set the return value based on whether InstanceReg is the
  // InterpFailInstanceReg magic value (set by the exception handler).
  Label success, join;
  masm.branchPtr(Assembler::NotEqual, InstanceReg, Imm32(InterpFailInstanceReg),
                 &success);
  masm.move32(Imm32(false), scratch);
  masm.jump(&join);
  masm.bind(&success);
  masm.move32(Imm32(true), scratch);
  masm.bind(&join);

  // Pop the arguments pushed after the dynamic alignment.
  masm.setFramePushed(frameSizeBeforeCall);
  masm.freeStackTo(frameSizeBeforeCall);

  // Recover the 'argv' pointer which was saved before aligning the stack.
  WasmPop(masm, argv);

  WasmPop(masm, InstanceReg);

  // Once we have the original InstanceReg, we can restore our FP environment
  // for returning back to C++ code.
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

  // Pop the stack pointer to its value right before dynamic alignment.
#ifdef JS_CODEGEN_ARM64
  static_assert(WasmStackAlignment == 16, "ARM64 SP alignment");
  masm.setFramePushed(FakeFrameSize);
  masm.freeStack(FakeFrameSize);
#else
  masm.PopStackPtr();
#endif

  // Store the register result, if any, in argv[0].
  // No widening is required, as the value leaves ReturnReg.
  StoreRegisterResult(masm, fe, funcType, argv);

  masm.move32(scratch, ReturnReg);

  // Restore clobbered non-volatile registers of the caller.
  masm.setFramePushed(nonVolatileRegsPushSize);
  masm.PopRegsInMask(NonVolatileRegs);
  MOZ_ASSERT(masm.framePushed() == 0);

#if defined(JS_CODEGEN_ARM64)
  masm.setFramePushed(WasmPushSize);
  WasmPop(masm, lr);
  masm.abiret();
#else
  masm.ret();
#endif

  return FinishOffsets(masm, offsets);
}

#ifdef JS_PUNBOX64
static const ValueOperand ScratchValIonEntry = ValueOperand(ABINonArgReg0);
#else
static const ValueOperand ScratchValIonEntry =
    ValueOperand(ABINonArgReg0, ABINonArgReg1);
#endif
static const Register ScratchIonEntry = ABINonArgReg2;

static void CallSymbolicAddress(MacroAssembler& masm, bool isAbsolute,
                                SymbolicAddress sym) {
  if (isAbsolute) {
    masm.call(ImmPtr(SymbolicAddressTarget(sym), ImmPtr::NoCheckToken()));
  } else {
    masm.call(sym);
  }
}

// Load instance's instance from the callee.
static void GenerateJitEntryLoadInstance(MacroAssembler& masm) {
  // ScratchIonEntry := callee => JSFunction*
  unsigned offset = JitFrameLayout::offsetOfCalleeToken();
  masm.loadFunctionFromCalleeToken(Address(FramePointer, offset),
                                   ScratchIonEntry);

  // ScratchIonEntry := callee->getExtendedSlot(WASM_INSTANCE_SLOT)->toPrivate()
  //                 => Instance*
  offset = FunctionExtended::offsetOfExtendedSlot(
      FunctionExtended::WASM_INSTANCE_SLOT);
  masm.loadPrivate(Address(ScratchIonEntry, offset), InstanceReg);
}

// Creates a JS fake exit frame for wasm, so the frame iterators just use
// JSJit frame iteration.
//
// Note: the caller must ensure InstanceReg is valid.
static void GenerateJitEntryThrow(MacroAssembler& masm) {
  AssertExpectedSP(masm);

  MOZ_ASSERT(masm.framePushed() == 0);
  MoveSPForJitABI(masm);

  masm.loadPtr(Address(InstanceReg, Instance::offsetOfCx()), ScratchIonEntry);
  masm.enterFakeExitFrameForWasm(ScratchIonEntry, ScratchIonEntry,
                                 ExitFrameType::WasmGenericJitEntry);

  masm.loadPtr(Address(InstanceReg, Instance::offsetOfJSJitExceptionHandler()),
               ScratchIonEntry);
  masm.jump(ScratchIonEntry);
}

// Helper function for allocating a BigInt and initializing it from an I64 in
// GenerateJitEntry.  The return result is written to scratch.
//
// Note that this will create a new frame and must not - in its current form -
// be called from a context where there is already another stub frame on the
// stack, as that confuses unwinding during profiling.  This was a problem for
// its use from GenerateImportJitExit, see bug 1754258.  Therefore,
// FuncType::canHaveJitExit prevents the present function from being called for
// exits.
static void GenerateBigIntInitialization(MacroAssembler& masm,
                                         unsigned bytesPushedByPrologue,
                                         Register64 input, Register scratch,
                                         const FuncExport& fe, Label* fail) {
#if JS_BITS_PER_WORD == 32
  MOZ_ASSERT(input.low != scratch);
  MOZ_ASSERT(input.high != scratch);
#else
  MOZ_ASSERT(input.reg != scratch);
#endif

  MOZ_ASSERT(masm.framePushed() == 0);

  // We need to avoid clobbering other argument registers and the input.
  AllocatableRegisterSet regs(RegisterSet::Volatile());
  LiveRegisterSet save(regs.asLiveSet());
  masm.PushRegsInMask(save);

  unsigned frameSize = StackDecrementForCall(
      ABIStackAlignment, masm.framePushed() + bytesPushedByPrologue, 0);
  masm.reserveStack(frameSize);
  masm.assertStackAlignment(ABIStackAlignment);

  CallSymbolicAddress(masm, !fe.hasEagerStubs(),
                      SymbolicAddress::AllocateBigInt);
  masm.storeCallPointerResult(scratch);

  // AllocateBigInt goes through a builtin thunk, which upon return enters the
  // wasm FP environment (it assumes it's always called by a wasm function). We
  // should be in the system FP environment and so we need to reset it.
  //
  // The InstanceReg is live for the call above, and it is preserved by the
  // builtin thunk.
  MOZ_ASSERT(wasm::NeedsBuiltinThunk(SymbolicAddress::AllocateBigInt));
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

  masm.assertStackAlignment(ABIStackAlignment);
  masm.freeStack(frameSize);

  LiveRegisterSet ignore;
  ignore.add(scratch);
  masm.PopRegsInMaskIgnore(save, ignore);

  masm.branchTestPtr(Assembler::Zero, scratch, scratch, fail);
  masm.initializeBigInt64(Scalar::BigInt64, scratch, input);
}

// Generate a stub that enters wasm from a jit code caller via the jit ABI.
//
// ARM64 note: This does not save the PseudoStackPointer so we must be sure to
// recompute it on every return path, be it normal return or exception return.
// The JIT code we return to assumes it is correct.

static bool GenerateJitEntry(MacroAssembler& masm, size_t funcExportIndex,
                             const FuncExport& fe, const FuncType& funcType,
                             const Maybe<ImmPtr>& funcPtr,
                             CallableOffsets* offsets) {
  AutoCreatedBy acb(masm, "GenerateJitEntry");

  AssertExpectedSP(masm);

  RegisterOrSP sp = masm.getStackPointer();

  GenerateJitEntryPrologue(masm, offsets);

  // The jit caller has set up the following stack layout (sp grows to the
  // left):
  // <-- retAddr | descriptor | callee | argc | this | arg1..N
  //
  // GenerateJitEntryPrologue has additionally pushed the caller's frame
  // pointer. The stack pointer is now JitStackAlignment-aligned.
  //
  // We initialize an ExitFooterFrame (with ExitFrameType::WasmGenericJitEntry)
  // immediately below the frame pointer to ensure FP is a valid JS JIT exit
  // frame.

  MOZ_ASSERT(masm.framePushed() == 0);
  MOZ_RELEASE_ASSERT(!funcType.hasUnexposableArgOrRet());

  // Avoid overlapping aligned stack arguments area with ExitFooterFrame.
  const unsigned AlignedExitFooterFrameSize =
      AlignBytes(ExitFooterFrame::Size(), WasmStackAlignment);
  unsigned normalBytesNeeded =
      AlignedExitFooterFrameSize + StackArgBytesForWasmABI(funcType);

  MIRTypeVector coerceArgTypes;
  MOZ_ALWAYS_TRUE(coerceArgTypes.append(MIRType::Int32));
  MOZ_ALWAYS_TRUE(coerceArgTypes.append(MIRType::Pointer));
  MOZ_ALWAYS_TRUE(coerceArgTypes.append(MIRType::Pointer));
  unsigned oolBytesNeeded =
      AlignedExitFooterFrameSize + StackArgBytesForWasmABI(coerceArgTypes);

  unsigned bytesNeeded = std::max(normalBytesNeeded, oolBytesNeeded);

  // Note the jit caller ensures the stack is aligned *after* the call
  // instruction.
  unsigned frameSize = StackDecrementForCall(WasmStackAlignment,
                                             masm.framePushed(), bytesNeeded);

  // Reserve stack space for wasm ABI arguments, set up like this:
  // <-- ABI args | padding
  masm.reserveStack(frameSize);

  MOZ_ASSERT(masm.framePushed() == frameSize);

  // Initialize the ExitFooterFrame.
  static_assert(ExitFooterFrame::Size() == sizeof(uintptr_t));
  masm.storePtr(ImmWord(uint32_t(ExitFrameType::WasmGenericJitEntry)),
                Address(FramePointer, -int32_t(ExitFooterFrame::Size())));

  GenerateJitEntryLoadInstance(masm);

  FloatRegister scratchF = ABINonArgDoubleReg;
  Register scratchG = ScratchIonEntry;
  ValueOperand scratchV = ScratchValIonEntry;

  GenPrintf(DebugChannel::Function, masm, "wasm-function[%d]; arguments ",
            fe.funcIndex());

  // We do two loops:
  // - one loop up-front will make sure that all the Value tags fit the
  // expected signature argument types. If at least one inline conversion
  // fails, we just jump to the OOL path which will call into C++. Inline
  // conversions are ordered in the way we expect them to happen the most.
  // - the second loop will unbox the arguments into the right registers.
  Label oolCall;
  for (size_t i = 0; i < funcType.args().length(); i++) {
    Address jitArgAddr(FramePointer, JitFrameLayout::offsetOfActualArg(i));
    masm.loadValue(jitArgAddr, scratchV);

    Label next;
    switch (funcType.args()[i].kind()) {
      case ValType::I32: {
        Label isDouble, isUndefinedOrNull, isBoolean;
        {
          ScratchTagScope tag(masm, scratchV);
          masm.splitTagForTest(scratchV, tag);

          // For int32 inputs, just skip.
          masm.branchTestInt32(Assembler::Equal, tag, &next);

          masm.branchTestDouble(Assembler::Equal, tag, &isDouble);
          masm.branchTestUndefined(Assembler::Equal, tag, &isUndefinedOrNull);
          masm.branchTestNull(Assembler::Equal, tag, &isUndefinedOrNull);
          masm.branchTestBoolean(Assembler::Equal, tag, &isBoolean);

          // Other types (symbol, object, strings) go to the C++ call.
          masm.jump(&oolCall);
        }

        Label storeBack;

        // For double inputs, unbox, truncate and store back.
        masm.bind(&isDouble);
        {
          masm.unboxDouble(scratchV, scratchF);
          masm.branchTruncateDoubleMaybeModUint32(scratchF, scratchG, &oolCall);
          masm.jump(&storeBack);
        }

        // For null or undefined, store 0.
        masm.bind(&isUndefinedOrNull);
        {
          masm.storeValue(Int32Value(0), jitArgAddr);
          masm.jump(&next);
        }

        // For booleans, store the number value back.
        masm.bind(&isBoolean);
        masm.unboxBoolean(scratchV, scratchG);
        // fallthrough:

        masm.bind(&storeBack);
        masm.storeValue(JSVAL_TYPE_INT32, scratchG, jitArgAddr);
        break;
      }
      case ValType::I64: {
        // For BigInt inputs, just skip. Otherwise go to C++ for other
        // types that require creating a new BigInt or erroring.
        masm.branchTestBigInt(Assembler::NotEqual, scratchV, &oolCall);
        break;
      }
      case ValType::F32:
      case ValType::F64: {
        // Note we can reuse the same code for f32/f64 here, since for the
        // case of f32, the conversion of f64 to f32 will happen in the
        // second loop.

        Label isInt32OrBoolean, isUndefined, isNull;
        {
          ScratchTagScope tag(masm, scratchV);
          masm.splitTagForTest(scratchV, tag);

          // For double inputs, just skip.
          masm.branchTestDouble(Assembler::Equal, tag, &next);

          masm.branchTestInt32(Assembler::Equal, tag, &isInt32OrBoolean);
          masm.branchTestUndefined(Assembler::Equal, tag, &isUndefined);
          masm.branchTestNull(Assembler::Equal, tag, &isNull);
          masm.branchTestBoolean(Assembler::Equal, tag, &isInt32OrBoolean);

          // Other types (symbol, object, strings) go to the C++ call.
          masm.jump(&oolCall);
        }

        // For int32 and boolean inputs, convert and rebox.
        masm.bind(&isInt32OrBoolean);
        {
          masm.convertInt32ToDouble(scratchV.payloadOrValueReg(), scratchF);
          masm.boxDouble(scratchF, jitArgAddr);
          masm.jump(&next);
        }

        // For undefined (missing argument), store NaN.
        masm.bind(&isUndefined);
        {
          masm.storeValue(DoubleValue(JS::GenericNaN()), jitArgAddr);
          masm.jump(&next);
        }

        // +null is 0.
        masm.bind(&isNull);
        {
          masm.storeValue(DoubleValue(0.), jitArgAddr);
        }
        break;
      }
      case ValType::Ref: {
        // Guarded against by temporarilyUnsupportedReftypeForEntry()
        MOZ_RELEASE_ASSERT(funcType.args()[i].refType().isExtern());

        // If the value is a double that is a negative denormal and denormals
        // are disabled, then `branchValueConvertsToWasmAnyRefInline` will view
        // it as '-0' (which must be boxed in the OOL path). However, the
        // AnyRef boxing code uses `mozilla::NumberIsInt32` which does not
        // properly handle the CPU DAZ/FTZ flags and returns true and therefore
        // doesn't box the double. This leads to an assertion below where we
        // expected the value to be boxed.
        //
        // Making `mozilla::NumberIsInt32` handle the CPU DAZ/FTZ flags would
        // add a significant cost to many hot-paths. We instead just
        // eagerly canonicalize denormals to +-0.0 here to avoid inconsistent
        // results (see Bug 1971519).
        masm.canonicalizeValueZero(scratchV, scratchF);
        masm.storeValue(scratchV, jitArgAddr);

        masm.branchValueConvertsToWasmAnyRefInline(scratchV, scratchG, scratchF,
                                                   &next);
        masm.jump(&oolCall);
        break;
      }
      case ValType::V128: {
        // Guarded against by hasUnexposableArgOrRet()
        MOZ_CRASH("unexpected argument type when calling from the jit");
      }
      default: {
        MOZ_CRASH("unexpected argument type when calling from the jit");
      }
    }
    masm.nopAlign(CodeAlignment);
    masm.bind(&next);
  }

  Label rejoinBeforeCall;
  masm.bind(&rejoinBeforeCall);

  // Convert all the expected values to unboxed values on the stack.
  ArgTypeVector args(funcType);
  for (ABIArgIter iter(args, ABIKind::Wasm); !iter.done(); iter++) {
    Address argv(FramePointer, JitFrameLayout::offsetOfActualArg(iter.index()));
    bool isStackArg = iter->kind() == ABIArg::Stack;
    switch (iter.mirType()) {
      case MIRType::Int32: {
        Register target = isStackArg ? ScratchIonEntry : iter->gpr();
        masm.unboxInt32(argv, target);
        GenPrintIsize(DebugChannel::Function, masm, target);
        if (isStackArg) {
          masm.storePtr(target, Address(sp, iter->offsetFromArgBase()));
        }
        break;
      }
      case MIRType::Int64: {
        // The coercion has provided a BigInt value by this point, which
        // we need to convert to an I64 here.
        if (isStackArg) {
          Address dst(sp, iter->offsetFromArgBase());
          Register src = scratchV.payloadOrValueReg();
#if JS_BITS_PER_WORD == 64
          Register64 scratch64(scratchG);
#else
          Register64 scratch64(scratchG, ABINonArgReg3);
#endif
          masm.unboxBigInt(argv, src);
          masm.loadBigInt64(src, scratch64);
          GenPrintI64(DebugChannel::Function, masm, scratch64);
          masm.store64(scratch64, dst);
        } else {
          Register src = scratchG;
          Register64 target = iter->gpr64();
          masm.unboxBigInt(argv, src);
          masm.loadBigInt64(src, target);
          GenPrintI64(DebugChannel::Function, masm, target);
        }
        break;
      }
      case MIRType::Float32: {
        FloatRegister target = isStackArg ? ABINonArgDoubleReg : iter->fpu();
        masm.unboxDouble(argv, ABINonArgDoubleReg);
        masm.convertDoubleToFloat32(ABINonArgDoubleReg, target);
        GenPrintF32(DebugChannel::Function, masm, target.asSingle());
        if (isStackArg) {
          masm.storeFloat32(target, Address(sp, iter->offsetFromArgBase()));
        }
        break;
      }
      case MIRType::Double: {
        FloatRegister target = isStackArg ? ABINonArgDoubleReg : iter->fpu();
        masm.unboxDouble(argv, target);
        GenPrintF64(DebugChannel::Function, masm, target);
        if (isStackArg) {
          masm.storeDouble(target, Address(sp, iter->offsetFromArgBase()));
        }
        break;
      }
      case MIRType::WasmAnyRef: {
        ValueOperand src = ScratchValIonEntry;
        Register target = isStackArg ? ScratchIonEntry : iter->gpr();
        masm.loadValue(argv, src);
        // The loop before should ensure that all values that require boxing
        // have been taken care of.
        Label join;
        Label fail;
        masm.convertValueToWasmAnyRef(src, target, scratchF, &fail);
        masm.jump(&join);
        masm.bind(&fail);
        masm.breakpoint();
        masm.bind(&join);
        GenPrintPtr(DebugChannel::Function, masm, target);
        if (isStackArg) {
          masm.storePtr(target, Address(sp, iter->offsetFromArgBase()));
        }
        break;
      }
      default: {
        MOZ_CRASH("unexpected input argument when calling from jit");
      }
    }
  }

  GenPrintf(DebugChannel::Function, masm, "\n");

  // Setup wasm register state.
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());

  masm.storePtr(InstanceReg, Address(masm.getStackPointer(),
                                     WasmCalleeInstanceOffsetBeforeCall));

  // Call into the real function.
  masm.assertStackAlignment(WasmStackAlignment);
  GenerateEnterWasmFPEnvironment(masm, InstanceReg);
  CallFuncExport(masm, fe, funcPtr);
  masm.assertStackAlignment(WasmStackAlignment);

  GenPrintf(DebugChannel::Function, masm, "wasm-function[%d]; returns ",
            fe.funcIndex());

  // Pop frame. We set the stack pointer immediately after calling Wasm code
  // because the current stack pointer might not match the one before the call
  // if the callee performed a tail call.
  masm.moveToStackPtr(FramePointer);
  masm.setFramePushed(0);

  // wasm preserves InstanceReg across the call, so it is safe to use here to
  // restore the FP environment before returning to the JS JIT.
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

  // Store the return value in the JSReturnOperand.
  Label exception;
  const ValTypeVector& results = funcType.results();
  if (results.length() == 0) {
    GenPrintf(DebugChannel::Function, masm, "void");
    masm.moveValue(UndefinedValue(), JSReturnOperand);
  } else {
    MOZ_ASSERT(results.length() == 1, "multi-value return to JS unimplemented");
    switch (results[0].kind()) {
      case ValType::I32:
        GenPrintIsize(DebugChannel::Function, masm, ReturnReg);
#ifdef JS_64BIT
        // boxNonDouble requires a widened int32 value.
        masm.widenInt32(ReturnReg);
#endif
        masm.boxNonDouble(JSVAL_TYPE_INT32, ReturnReg, JSReturnOperand);
        break;
      case ValType::F32: {
        masm.canonicalizeFloatNaN(ReturnFloat32Reg);
        masm.convertFloat32ToDouble(ReturnFloat32Reg, ReturnDoubleReg);
        GenPrintF64(DebugChannel::Function, masm, ReturnDoubleReg);
        ScratchDoubleScope fpscratch(masm);
        masm.boxDouble(ReturnDoubleReg, JSReturnOperand, fpscratch);
        break;
      }
      case ValType::F64: {
        masm.canonicalizeDoubleNaN(ReturnDoubleReg);
        GenPrintF64(DebugChannel::Function, masm, ReturnDoubleReg);
        ScratchDoubleScope fpscratch(masm);
        masm.boxDouble(ReturnDoubleReg, JSReturnOperand, fpscratch);
        break;
      }
      case ValType::I64: {
        GenPrintI64(DebugChannel::Function, masm, ReturnReg64);
        MOZ_ASSERT(masm.framePushed() == 0);
        GenerateBigIntInitialization(masm, 0, ReturnReg64, scratchG, fe,
                                     &exception);
        masm.boxNonDouble(JSVAL_TYPE_BIGINT, scratchG, JSReturnOperand);
        break;
      }
      case ValType::V128: {
        MOZ_CRASH("unexpected return type when calling from ion to wasm");
      }
      case ValType::Ref: {
        GenPrintPtr(DebugChannel::Import, masm, ReturnReg);
        masm.convertWasmAnyRefToValue(InstanceReg, ReturnReg, JSReturnOperand,
                                      WasmJitEntryReturnScratch);
        break;
      }
    }
  }

  GenPrintf(DebugChannel::Function, masm, "\n");

  AssertExpectedSP(masm);
  GenerateJitEntryEpilogue(masm, offsets);
  MOZ_ASSERT(masm.framePushed() == 0);

  // Generate an OOL call to the C++ conversion path.
  bool hasFallThroughForException = false;
  if (oolCall.used()) {
    masm.bind(&oolCall);
    masm.setFramePushed(frameSize);

    // Baseline and Ion call C++ runtime via BuiltinThunk with wasm abi, so to
    // unify the BuiltinThunk's interface we call it here with wasm abi.
    jit::ABIArgIter<MIRTypeVector> argsIter(
        coerceArgTypes, ABIForBuiltin(SymbolicAddress::CoerceInPlace_JitEntry));

    // argument 0: function index.
    if (argsIter->kind() == ABIArg::GPR) {
      masm.movePtr(ImmWord(fe.funcIndex()), argsIter->gpr());
    } else {
      masm.storePtr(ImmWord(fe.funcIndex()),
                    Address(sp, argsIter->offsetFromArgBase()));
    }
    argsIter++;

    // argument 1: instance
    if (argsIter->kind() == ABIArg::GPR) {
      masm.movePtr(InstanceReg, argsIter->gpr());
    } else {
      masm.storePtr(InstanceReg, Address(sp, argsIter->offsetFromArgBase()));
    }
    argsIter++;

    // argument 2: effective address of start of argv
    Address argv(FramePointer, JitFrameLayout::offsetOfActualArgs());
    if (argsIter->kind() == ABIArg::GPR) {
      masm.computeEffectiveAddress(argv, argsIter->gpr());
    } else {
      masm.computeEffectiveAddress(argv, ScratchIonEntry);
      masm.storePtr(ScratchIonEntry,
                    Address(sp, argsIter->offsetFromArgBase()));
    }
    argsIter++;
    MOZ_ASSERT(argsIter.done());

    masm.assertStackAlignment(ABIStackAlignment);
    CallSymbolicAddress(masm, !fe.hasEagerStubs(),
                        SymbolicAddress::CoerceInPlace_JitEntry);
    masm.assertStackAlignment(ABIStackAlignment);

  // CoerceInPlace_JitEntry goes through a builtin thunk, which upon return enters the
  // wasm FP environment (it assumes it's always called by a wasm function). We
  // should be in the system FP environment and so we need to reset it.
  //
  // The InstanceReg is live for the call above, and it is preserved by the
  // builtin thunk.
  MOZ_ASSERT(wasm::NeedsBuiltinThunk(SymbolicAddress::CoerceInPlace_JitEntry));
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

    // No widening is required, as the return value is used as a bool.
    masm.branchTest32(Assembler::NonZero, ReturnReg, ReturnReg,
                      &rejoinBeforeCall);

    MOZ_ASSERT(masm.framePushed() == frameSize);
    masm.freeStack(frameSize);
    hasFallThroughForException = true;
  }

  if (exception.used() || hasFallThroughForException) {
    masm.bind(&exception);
    MOZ_ASSERT(masm.framePushed() == 0);
    GenerateJitEntryThrow(masm);
  }

  return FinishOffsets(masm, offsets);
}

void wasm::GenerateDirectCallFromJit(MacroAssembler& masm, const FuncExport& fe,
                                     const Instance& inst,
                                     const JitCallStackArgVector& stackArgs,
                                     Register scratch, uint32_t* callOffset) {
  MOZ_ASSERT(!IsCompilingWasm());

  const FuncType& funcType = inst.codeMeta().getFuncType(fe.funcIndex());

  size_t framePushedAtStart = masm.framePushed();

  // Note, if code here pushes a reference value into the frame for its own
  // purposes (and not just as an argument to the callee) then the frame must be
  // traced in TraceJitExitFrame, see the case there for DirectWasmJitCall.  The
  // callee will trace values that are pushed as arguments, however.

  // Push a special frame descriptor that indicates the frame size so we can
  // directly iterate from the current JIT frame without an extra call.
  // Note: buildFakeExitFrame pushes an ExitFrameLayout containing the current
  // frame pointer. We also use this to restore the frame pointer after the
  // call.
  *callOffset = masm.buildFakeExitFrame(scratch);
  // FP := ExitFrameLayout*
  masm.moveStackPtrTo(FramePointer);
  size_t framePushedAtFakeFrame = masm.framePushed();
  masm.setFramePushed(0);
  masm.loadJSContext(scratch);
  masm.enterFakeExitFrame(scratch, scratch, ExitFrameType::DirectWasmJitCall);

  static_assert(ExitFrameLayout::SizeWithFooter() % WasmStackAlignment == 0);
  MOZ_ASSERT(
      (masm.framePushed() + framePushedAtFakeFrame) % WasmStackAlignment == 0);

  // Move stack arguments to their final locations.
  unsigned bytesNeeded = StackArgBytesForWasmABI(funcType);
  bytesNeeded = StackDecrementForCall(
      WasmStackAlignment, framePushedAtFakeFrame + masm.framePushed(),
      bytesNeeded);
  if (bytesNeeded) {
    masm.reserveStack(bytesNeeded);
  }
  size_t fakeFramePushed = masm.framePushed();

  GenPrintf(DebugChannel::Function, masm, "wasm-function[%d]; arguments ",
            fe.funcIndex());

  ArgTypeVector args(funcType);
  for (ABIArgIter iter(args, ABIKind::Wasm); !iter.done(); iter++) {
    MOZ_ASSERT_IF(iter->kind() == ABIArg::GPR, iter->gpr() != scratch);
    MOZ_ASSERT_IF(iter->kind() == ABIArg::GPR, iter->gpr() != FramePointer);
    if (iter->kind() != ABIArg::Stack) {
      switch (iter.mirType()) {
        case MIRType::Int32:
          GenPrintIsize(DebugChannel::Function, masm, iter->gpr());
          break;
        case MIRType::Int64:
          GenPrintI64(DebugChannel::Function, masm, iter->gpr64());
          break;
        case MIRType::Float32:
          GenPrintF32(DebugChannel::Function, masm, iter->fpu());
          break;
        case MIRType::Double:
          GenPrintF64(DebugChannel::Function, masm, iter->fpu());
          break;
        case MIRType::WasmAnyRef:
          GenPrintPtr(DebugChannel::Function, masm, iter->gpr());
          break;
        case MIRType::StackResults:
          MOZ_ASSERT(args.isSyntheticStackResultPointerArg(iter.index()));
          GenPrintPtr(DebugChannel::Function, masm, iter->gpr());
          break;
        default:
          MOZ_CRASH("ion to wasm fast path can only handle i32/f32/f64");
      }
      continue;
    }

    Address dst(masm.getStackPointer(), iter->offsetFromArgBase());

    const JitCallStackArg& stackArg = stackArgs[iter.index()];
    switch (stackArg.tag()) {
      case JitCallStackArg::Tag::Imm32:
        GenPrintf(DebugChannel::Function, masm, "%d ", stackArg.imm32());
        masm.storePtr(ImmWord(stackArg.imm32()), dst);
        break;
      case JitCallStackArg::Tag::GPR:
        MOZ_ASSERT(stackArg.gpr() != scratch);
        MOZ_ASSERT(stackArg.gpr() != FramePointer);
        GenPrintIsize(DebugChannel::Function, masm, stackArg.gpr());
        masm.storePtr(stackArg.gpr(), dst);
        break;
      case JitCallStackArg::Tag::FPU:
        switch (iter.mirType()) {
          case MIRType::Double:
            GenPrintF64(DebugChannel::Function, masm, stackArg.fpu());
            masm.storeDouble(stackArg.fpu(), dst);
            break;
          case MIRType::Float32:
            GenPrintF32(DebugChannel::Function, masm, stackArg.fpu());
            masm.storeFloat32(stackArg.fpu(), dst);
            break;
          default:
            MOZ_CRASH(
                "unexpected MIR type for a float register in wasm fast call");
        }
        break;
      case JitCallStackArg::Tag::Address: {
        // The address offsets were valid *before* we pushed our frame.
        Address src = stackArg.addr();
        MOZ_ASSERT(src.base == masm.getStackPointer());
        src.offset += int32_t(framePushedAtFakeFrame + fakeFramePushed -
                              framePushedAtStart);
        switch (iter.mirType()) {
          case MIRType::Double: {
            ScratchDoubleScope fpscratch(masm);
            GenPrintF64(DebugChannel::Function, masm, fpscratch);
            masm.loadDouble(src, fpscratch);
            masm.storeDouble(fpscratch, dst);
            break;
          }
          case MIRType::Float32: {
            ScratchFloat32Scope fpscratch(masm);
            masm.loadFloat32(src, fpscratch);
            GenPrintF32(DebugChannel::Function, masm, fpscratch);
            masm.storeFloat32(fpscratch, dst);
            break;
          }
          case MIRType::Int32: {
            masm.loadPtr(src, scratch);
            GenPrintIsize(DebugChannel::Function, masm, scratch);
            masm.storePtr(scratch, dst);
            break;
          }
          case MIRType::WasmAnyRef: {
            masm.loadPtr(src, scratch);
            GenPrintPtr(DebugChannel::Function, masm, scratch);
            masm.storePtr(scratch, dst);
            break;
          }
          case MIRType::StackResults: {
            MOZ_CRASH("multi-value in ion to wasm fast path unimplemented");
          }
          default: {
            MOZ_CRASH("unexpected MIR type for a stack slot in wasm fast call");
          }
        }
        break;
      }
      case JitCallStackArg::Tag::Undefined: {
        MOZ_CRASH("can't happen because of arg.kind() check");
      }
    }
  }

  GenPrintf(DebugChannel::Function, masm, "\n");

  // Load instance; from now on, InstanceReg is live.
  masm.movePtr(ImmPtr(&inst), InstanceReg);
  masm.storePtr(InstanceReg, Address(masm.getStackPointer(),
                                     WasmCalleeInstanceOffsetBeforeCall));
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());

  // Actual call.
  const CodeBlock& codeBlock = inst.code().funcCodeBlock(fe.funcIndex());
  const CodeRange& codeRange = codeBlock.codeRange(fe);
  void* callee = const_cast<uint8_t*>(codeBlock.base()) +
                 codeRange.funcUncheckedCallEntry();

  masm.assertStackAlignment(WasmStackAlignment);
  MoveSPForJitABI(masm);
  GenerateEnterWasmFPEnvironment(masm, InstanceReg);
  masm.callJit(ImmPtr(callee));
#ifdef JS_CODEGEN_ARM64
  // WASM does not always keep PSP in sync with SP.  So reinitialize it as it
  // might be clobbered either by WASM or by any C++ calls within.
  masm.initPseudoStackPtr();
#endif
  masm.freeStackTo(fakeFramePushed);
  masm.assertStackAlignment(WasmStackAlignment);

  // The call to wasm above preserves InstanceReg, so it is safe to use here
  // to restore the FP environment before returning to the JS JIT.
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

  // Store the return value in the appropriate place.
  GenPrintf(DebugChannel::Function, masm, "wasm-function[%d]; returns ",
            fe.funcIndex());
  const ValTypeVector& results = funcType.results();
  if (results.length() == 0) {
    masm.moveValue(UndefinedValue(), JSReturnOperand);
    GenPrintf(DebugChannel::Function, masm, "void");
  } else {
    MOZ_ASSERT(results.length() == 1, "multi-value return to JS unimplemented");
    switch (results[0].kind()) {
      case wasm::ValType::I32:
        // The return value is in ReturnReg, which is what Ion expects.
        GenPrintIsize(DebugChannel::Function, masm, ReturnReg);
#ifdef JS_64BIT
        masm.widenInt32(ReturnReg);
#endif
        break;
      case wasm::ValType::I64:
        // The return value is in ReturnReg64, which is what Ion expects.
        GenPrintI64(DebugChannel::Function, masm, ReturnReg64);
        break;
      case wasm::ValType::F32:
        masm.canonicalizeFloatNaN(ReturnFloat32Reg);
        GenPrintF32(DebugChannel::Function, masm, ReturnFloat32Reg);
        break;
      case wasm::ValType::F64:
        masm.canonicalizeDoubleNaN(ReturnDoubleReg);
        GenPrintF64(DebugChannel::Function, masm, ReturnDoubleReg);
        break;
      case wasm::ValType::Ref:
        GenPrintPtr(DebugChannel::Import, masm, ReturnReg);
        // The call to wasm above preserves the InstanceReg, we don't
        // need to reload it here.
        masm.convertWasmAnyRefToValue(InstanceReg, ReturnReg, JSReturnOperand,
                                      WasmJitEntryReturnScratch);
        break;
      case wasm::ValType::V128:
        MOZ_CRASH("unexpected return type when calling from ion to wasm");
    }
  }

  GenPrintf(DebugChannel::Function, masm, "\n");

  // Restore the frame pointer.
  masm.loadPtr(Address(FramePointer, 0), FramePointer);
  masm.setFramePushed(fakeFramePushed + framePushedAtFakeFrame);

  // Free args + frame descriptor.
  masm.leaveExitFrame(bytesNeeded + ExitFrameLayout::Size());

  MOZ_ASSERT(framePushedAtStart == masm.framePushed());
}

static void StackCopy(MacroAssembler& masm, MIRType type, Register scratch,
                      Address src, Address dst) {
  if (type == MIRType::Int32) {
    masm.load32(src, scratch);
    GenPrintIsize(DebugChannel::Import, masm, scratch);
    masm.store32(scratch, dst);
  } else if (type == MIRType::Int64) {
#if JS_BITS_PER_WORD == 32
    MOZ_RELEASE_ASSERT(src.base != scratch && dst.base != scratch);
    GenPrintf(DebugChannel::Import, masm, "i64(");
    masm.load32(LowWord(src), scratch);
    GenPrintIsize(DebugChannel::Import, masm, scratch);
    masm.store32(scratch, LowWord(dst));
    masm.load32(HighWord(src), scratch);
    GenPrintIsize(DebugChannel::Import, masm, scratch);
    masm.store32(scratch, HighWord(dst));
    GenPrintf(DebugChannel::Import, masm, ") ");
#else
    Register64 scratch64(scratch);
    masm.load64(src, scratch64);
    GenPrintIsize(DebugChannel::Import, masm, scratch);
    masm.store64(scratch64, dst);
#endif
  } else if (type == MIRType::WasmAnyRef || type == MIRType::Pointer ||
             type == MIRType::StackResults) {
    masm.loadPtr(src, scratch);
    GenPrintPtr(DebugChannel::Import, masm, scratch);
    masm.storePtr(scratch, dst);
  } else if (type == MIRType::Float32) {
    ScratchFloat32Scope fpscratch(masm);
    masm.loadFloat32(src, fpscratch);
    GenPrintF32(DebugChannel::Import, masm, fpscratch);
    masm.storeFloat32(fpscratch, dst);
  } else if (type == MIRType::Double) {
    ScratchDoubleScope fpscratch(masm);
    masm.loadDouble(src, fpscratch);
    GenPrintF64(DebugChannel::Import, masm, fpscratch);
    masm.storeDouble(fpscratch, dst);
#ifdef ENABLE_WASM_SIMD
  } else if (type == MIRType::Simd128) {
    ScratchSimd128Scope fpscratch(masm);
    masm.loadUnalignedSimd128(src, fpscratch);
    GenPrintV128(DebugChannel::Import, masm, fpscratch);
    masm.storeUnalignedSimd128(fpscratch, dst);
#endif
  } else {
    MOZ_CRASH("StackCopy: unexpected type");
  }
}

static void FillArgumentArrayForInterpExit(MacroAssembler& masm,
                                           unsigned funcImportIndex,
                                           const FuncType& funcType,
                                           unsigned argOffset,
                                           Register scratch) {
  // This is `sizeof(Frame)` because the wasm ABIArgIter handles adding the
  // offsets of the shadow stack area and the instance slots.
  const unsigned offsetFromFPToCallerStackArgs = sizeof(Frame);

  GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; arguments ",
            funcImportIndex);

  ArgTypeVector args(funcType);
  for (ABIArgIter i(args, ABIKind::Wasm); !i.done(); i++) {
    Address dst(masm.getStackPointer(), argOffset + i.index() * sizeof(Value));

    MIRType type = i.mirType();
    MOZ_ASSERT(args.isSyntheticStackResultPointerArg(i.index()) ==
               (type == MIRType::StackResults));
    switch (i->kind()) {
      case ABIArg::GPR:
        if (type == MIRType::Int32) {
          GenPrintIsize(DebugChannel::Import, masm, i->gpr());
          masm.store32(i->gpr(), dst);
        } else if (type == MIRType::Int64) {
          GenPrintI64(DebugChannel::Import, masm, i->gpr64());
          masm.store64(i->gpr64(), dst);
        } else if (type == MIRType::WasmAnyRef) {
          GenPrintPtr(DebugChannel::Import, masm, i->gpr());
          masm.storePtr(i->gpr(), dst);
        } else if (type == MIRType::StackResults) {
          GenPrintPtr(DebugChannel::Import, masm, i->gpr());
          masm.storePtr(i->gpr(), dst);
        } else {
          MOZ_CRASH(
              "FillArgumentArrayForInterpExit, ABIArg::GPR: unexpected type");
        }
        break;
#ifdef JS_CODEGEN_REGISTER_PAIR
      case ABIArg::GPR_PAIR:
        if (type == MIRType::Int64) {
          GenPrintI64(DebugChannel::Import, masm, i->gpr64());
          masm.store64(i->gpr64(), dst);
        } else {
          MOZ_CRASH("wasm uses hardfp for function calls.");
        }
        break;
#endif
      case ABIArg::FPU: {
        FloatRegister srcReg = i->fpu();
        if (type == MIRType::Double) {
          GenPrintF64(DebugChannel::Import, masm, srcReg);
          masm.storeDouble(srcReg, dst);
        } else if (type == MIRType::Float32) {
          // Preserve the NaN pattern in the input.
          GenPrintF32(DebugChannel::Import, masm, srcReg);
          masm.storeFloat32(srcReg, dst);
        } else if (type == MIRType::Simd128) {
          // The value should never escape; the call will be stopped later as
          // the import is being called.  But we should generate something sane
          // here for the boxed case since a debugger or the stack walker may
          // observe something.
          ScratchDoubleScope dscratch(masm);
          masm.loadConstantDouble(0, dscratch);
          GenPrintF64(DebugChannel::Import, masm, dscratch);
          masm.storeDouble(dscratch, dst);
        } else {
          MOZ_CRASH("Unknown MIRType in wasm exit stub");
        }
        break;
      }
      case ABIArg::Stack: {
        Address src(FramePointer,
                    offsetFromFPToCallerStackArgs + i->offsetFromArgBase());
        if (type == MIRType::Simd128) {
          // As above.  StackCopy does not know this trick.
          ScratchDoubleScope dscratch(masm);
          masm.loadConstantDouble(0, dscratch);
          GenPrintF64(DebugChannel::Import, masm, dscratch);
          masm.storeDouble(dscratch, dst);
        } else {
          StackCopy(masm, type, scratch, src, dst);
        }
        break;
      }
      case ABIArg::Uninitialized:
        MOZ_CRASH("Uninitialized ABIArg kind");
    }
  }
  GenPrintf(DebugChannel::Import, masm, "\n");
}

// Note, this may destroy the values in incoming argument registers as a result
// of Spectre mitigation.
static void FillArgumentArrayForJitExit(MacroAssembler& masm, Register instance,
                                        unsigned funcImportIndex,
                                        const FuncType& funcType,
                                        unsigned argOffset, Register scratch,
                                        Register scratch2) {
  MOZ_ASSERT(scratch != scratch2);

  // This is `sizeof(Frame)` because the wasm ABIArgIter handles adding the
  // offsets of the shadow stack area and the instance slots.
  const unsigned offsetFromFPToCallerStackArgs = sizeof(Frame);

  // This loop does not root the values that are being constructed in
  // for the arguments. Allocations that are generated by code either
  // in the loop or called from it should be NoGC allocations.
  GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; arguments ",
            funcImportIndex);

  ArgTypeVector args(funcType);
  for (ABIArgIter i(args, ABIKind::Wasm); !i.done(); i++) {
    Address dst(masm.getStackPointer(), argOffset + i.index() * sizeof(Value));

    MIRType type = i.mirType();
    MOZ_ASSERT(args.isSyntheticStackResultPointerArg(i.index()) ==
               (type == MIRType::StackResults));
    switch (i->kind()) {
      case ABIArg::GPR:
        if (type == MIRType::Int32) {
          GenPrintIsize(DebugChannel::Import, masm, i->gpr());
          masm.storeValue(JSVAL_TYPE_INT32, i->gpr(), dst);
        } else if (type == MIRType::Int64) {
          // FuncType::canHaveJitExit should prevent this.  Also see comments
          // at GenerateBigIntInitialization.
          MOZ_CRASH("Should not happen");
        } else if (type == MIRType::WasmAnyRef) {
          // This works also for FuncRef because it is distinguishable from
          // a boxed AnyRef.
          masm.movePtr(i->gpr(), scratch2);
          GenPrintPtr(DebugChannel::Import, masm, scratch2);
          masm.convertWasmAnyRefToValue(instance, scratch2, dst, scratch);
        } else if (type == MIRType::StackResults) {
          MOZ_CRASH("Multi-result exit to JIT unimplemented");
        } else {
          MOZ_CRASH(
              "FillArgumentArrayForJitExit, ABIArg::GPR: unexpected type");
        }
        break;
#ifdef JS_CODEGEN_REGISTER_PAIR
      case ABIArg::GPR_PAIR:
        if (type == MIRType::Int64) {
          // FuncType::canHaveJitExit should prevent this.  Also see comments
          // at GenerateBigIntInitialization.
          MOZ_CRASH("Should not happen");
        } else {
          MOZ_CRASH("wasm uses hardfp for function calls.");
        }
        break;
#endif
      case ABIArg::FPU: {
        FloatRegister srcReg = i->fpu();
        if (type == MIRType::Double) {
          // Preserve the NaN pattern in the input.
          ScratchDoubleScope fpscratch(masm);
          masm.moveDouble(srcReg, fpscratch);
          masm.canonicalizeDoubleNaN(fpscratch);
          GenPrintF64(DebugChannel::Import, masm, fpscratch);
          masm.boxDouble(fpscratch, dst);
        } else if (type == MIRType::Float32) {
          // JS::Values can't store Float32, so convert to a Double.
          ScratchDoubleScope fpscratch(masm);
          masm.convertFloat32ToDouble(srcReg, fpscratch);
          masm.canonicalizeDoubleNaN(fpscratch);
          GenPrintF64(DebugChannel::Import, masm, fpscratch);
          masm.boxDouble(fpscratch, dst);
        } else if (type == MIRType::Simd128) {
          // The value should never escape; the call will be stopped later as
          // the import is being called.  But we should generate something sane
          // here for the boxed case since a debugger or the stack walker may
          // observe something.
          ScratchDoubleScope dscratch(masm);
          masm.loadConstantDouble(0, dscratch);
          GenPrintF64(DebugChannel::Import, masm, dscratch);
          masm.boxDouble(dscratch, dst);
        } else {
          MOZ_CRASH("Unknown MIRType in wasm exit stub");
        }
        break;
      }
      case ABIArg::Stack: {
        Address src(FramePointer,
                    offsetFromFPToCallerStackArgs + i->offsetFromArgBase());
        if (type == MIRType::Int32) {
          masm.load32(src, scratch);
          GenPrintIsize(DebugChannel::Import, masm, scratch);
          masm.storeValue(JSVAL_TYPE_INT32, scratch, dst);
        } else if (type == MIRType::Int64) {
          // FuncType::canHaveJitExit should prevent this.  Also see comments
          // at GenerateBigIntInitialization.
          MOZ_CRASH("Should not happen");
        } else if (type == MIRType::WasmAnyRef) {
          // This works also for FuncRef because it is distinguishable from a
          // boxed AnyRef.
          masm.loadPtr(src, scratch);
          GenPrintPtr(DebugChannel::Import, masm, scratch);
          masm.convertWasmAnyRefToValue(instance, scratch, dst, scratch2);
        } else if (IsFloatingPointType(type)) {
          ScratchDoubleScope dscratch(masm);
          FloatRegister fscratch = dscratch.asSingle();
          if (type == MIRType::Float32) {
            masm.loadFloat32(src, fscratch);
            masm.convertFloat32ToDouble(fscratch, dscratch);
          } else {
            masm.loadDouble(src, dscratch);
          }
          masm.canonicalizeDoubleNaN(dscratch);
          GenPrintF64(DebugChannel::Import, masm, dscratch);
          masm.boxDouble(dscratch, dst);
        } else if (type == MIRType::Simd128) {
          // The value should never escape; the call will be stopped later as
          // the import is being called.  But we should generate something
          // sane here for the boxed case since a debugger or the stack walker
          // may observe something.
          ScratchDoubleScope dscratch(masm);
          masm.loadConstantDouble(0, dscratch);
          GenPrintF64(DebugChannel::Import, masm, dscratch);
          masm.boxDouble(dscratch, dst);
        } else {
          MOZ_CRASH(
              "FillArgumentArrayForJitExit, ABIArg::Stack: unexpected type");
        }
        break;
      }
      case ABIArg::Uninitialized:
        MOZ_CRASH("Uninitialized ABIArg kind");
    }
  }
  GenPrintf(DebugChannel::Import, masm, "\n");
}

// Generate a wrapper function with the standard intra-wasm call ABI which
// simply calls an import. This wrapper function allows any import to be treated
// like a normal wasm function for the purposes of exports and table calls. In
// particular, the wrapper function provides:
//  - a table entry, so JS imports can be put into tables
//  - normal entries, so that, if the import is re-exported, an entry stub can
//    be generated and called without any special cases
static bool GenerateImportFunction(jit::MacroAssembler& masm,
                                   uint32_t funcImportInstanceOffset,
                                   const FuncType& funcType,
                                   CallIndirectId callIndirectId,
                                   FuncOffsets* offsets, StackMaps* stackMaps) {
  AutoCreatedBy acb(masm, "wasm::GenerateImportFunction");

  AssertExpectedSP(masm);

  GenerateFunctionPrologue(masm, callIndirectId, Nothing(), offsets);

  MOZ_ASSERT(masm.framePushed() == 0);
  const unsigned sizeOfInstanceSlot = sizeof(void*);
  unsigned framePushed = StackDecrementForCall(
      WasmStackAlignment,
      sizeof(Frame),  // pushed by prologue
      StackArgBytesForWasmABI(funcType) + sizeOfInstanceSlot);

  Label stackOverflowTrap;
  masm.wasmReserveStackChecked(framePushed, &stackOverflowTrap);

  MOZ_ASSERT(masm.framePushed() == framePushed);

  masm.storePtr(InstanceReg, Address(masm.getStackPointer(),
                                     framePushed - sizeOfInstanceSlot));

  // The argument register state is already setup by our caller. We just need
  // to be sure not to clobber it before the call.
  Register scratch = ABINonArgReg0;

  // Copy our frame's stack arguments to the callee frame's stack argument.
  //
  // Note offsetFromFPToCallerStackArgs is sizeof(Frame) because the
  // WasmABIArgIter accounts for both the ShadowStackSpace and the instance
  // fields of FrameWithInstances.
  unsigned offsetFromFPToCallerStackArgs = sizeof(Frame);
  ArgTypeVector args(funcType);
  for (ABIArgIter i(args, ABIKind::Wasm); !i.done(); i++) {
    if (i->kind() != ABIArg::Stack) {
      continue;
    }

    Address src(FramePointer,
                offsetFromFPToCallerStackArgs + i->offsetFromArgBase());
    Address dst(masm.getStackPointer(), i->offsetFromArgBase());
    GenPrintf(DebugChannel::Import, masm,
              "calling exotic import function with arguments: ");
    StackCopy(masm, i.mirType(), scratch, src, dst);
    GenPrintf(DebugChannel::Import, masm, "\n");
  }

  // Call the import exit stub.
  CallSiteDesc desc(CallSiteKind::Import);
  MoveSPForJitABI(masm);
  masm.wasmCallImport(desc, CalleeDesc::import(funcImportInstanceOffset));

  // The call may not have preserved the stack pointer, so recover it from FP
  // before reading the instance slot.
  masm.freeStackTo(framePushed);

  // Restore the instance register and pinned regs, per wasm function ABI.
  masm.loadPtr(
      Address(masm.getStackPointer(), framePushed - sizeOfInstanceSlot),
      InstanceReg);
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());

  // Restore cx->realm.
  masm.switchToWasmInstanceRealm(ABINonArgReturnReg0, ABINonArgReturnReg1);

  GenerateFunctionEpilogue(masm, framePushed, offsets);

  // Emit the stack overflow trap as OOL code.
  masm.bind(&stackOverflowTrap);
  masm.wasmTrap(wasm::Trap::StackOverflow, wasm::TrapSiteDesc());

  return FinishOffsets(masm, offsets);
}

static const unsigned STUBS_LIFO_DEFAULT_CHUNK_SIZE = 4 * 1024;

// Generate a stub that is called via the internal ABI derived from the
// signature of the import and calls into an appropriate callImport C++
// function, having boxed all the ABI arguments into a homogeneous Value array.
static bool GenerateImportInterpExit(MacroAssembler& masm, const FuncImport& fi,
                                     const FuncType& funcType,
                                     uint32_t funcImportIndex,
                                     Label* throwLabel,
                                     CallableOffsets* offsets) {
  AutoCreatedBy acb(masm, "GenerateImportInterpExit");

  AssertExpectedSP(masm);
  masm.setFramePushed(0);

  // Argument types for Instance::callImport_*:
  static const MIRType typeArray[] = {MIRType::Pointer,   // Instance*
                                      MIRType::Pointer,   // funcImportIndex
                                      MIRType::Int32,     // argc
                                      MIRType::Pointer};  // argv
  MIRTypeVector invokeArgTypes;
  MOZ_ALWAYS_TRUE(invokeArgTypes.append(typeArray, std::size(typeArray)));

  // At the point of the call, the stack layout is:
  //
  //  | stack args | padding | argv[] | padding | retaddr | caller stack | ...
  //  ^
  //  +-- sp
  //
  // The padding between stack args and argv ensures that argv is aligned on a
  // Value boundary. The padding between argv and retaddr ensures that sp is
  // aligned.  The caller stack includes a ShadowStackArea and the instance
  // fields before the args, see WasmFrame.h.
  //
  // The 'double' alignment is correct since the argv[] is a Value array.
  unsigned argOffset =
      AlignBytes(StackArgBytesForNativeABI(invokeArgTypes), sizeof(double));
  // The abiArgCount includes a stack result pointer argument if needed.
  unsigned abiArgCount = ArgTypeVector(funcType).lengthWithStackResults();
  unsigned argBytes = std::max<size_t>(1, abiArgCount) * sizeof(Value);
  unsigned framePushed = AlignBytes(argOffset + argBytes, ABIStackAlignment);
  GenerateExitPrologue(masm, ExitReason::Fixed::ImportInterp,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Static,
                       /*frameSize*/ framePushed, offsets);

  // Fill the argument array.
  Register scratch = ABINonArgReturnReg0;
  FillArgumentArrayForInterpExit(masm, funcImportIndex, funcType, argOffset,
                                 scratch);

  // Prepare the arguments for the call to Instance::callImport_*.
  ABIArgMIRTypeIter i(invokeArgTypes, ABIKind::System);

  // argument 0: Instance*
  if (i->kind() == ABIArg::GPR) {
    masm.movePtr(InstanceReg, i->gpr());
  } else {
    masm.storePtr(InstanceReg,
                  Address(masm.getStackPointer(), i->offsetFromArgBase()));
  }
  i++;

  // argument 1: funcImportIndex
  if (i->kind() == ABIArg::GPR) {
    masm.mov(ImmWord(funcImportIndex), i->gpr());
  } else {
    masm.store32(Imm32(funcImportIndex),
                 Address(masm.getStackPointer(), i->offsetFromArgBase()));
  }
  i++;

  // argument 2: argc
  unsigned argc = abiArgCount;
  if (i->kind() == ABIArg::GPR) {
    masm.mov(ImmWord(argc), i->gpr());
  } else {
    masm.store32(Imm32(argc),
                 Address(masm.getStackPointer(), i->offsetFromArgBase()));
  }
  i++;

  // argument 3: argv
  Address argv(masm.getStackPointer(), argOffset);
  if (i->kind() == ABIArg::GPR) {
    masm.computeEffectiveAddress(argv, i->gpr());
  } else {
    masm.computeEffectiveAddress(argv, scratch);
    masm.storePtr(scratch,
                  Address(masm.getStackPointer(), i->offsetFromArgBase()));
  }
  i++;
  MOZ_ASSERT(i.done());

  // Make the call, test whether it succeeded, and extract the return value.
  masm.assertStackAlignment(ABIStackAlignment);
  masm.call(SymbolicAddress::CallImport_General);
  masm.branchTest32(Assembler::Zero, ReturnReg, ReturnReg, throwLabel);

  ResultType resultType = ResultType::Vector(funcType.results());
  ValType registerResultType;
  for (ABIResultIter iter(resultType); !iter.done(); iter.next()) {
    if (iter.cur().inRegister()) {
      MOZ_ASSERT(!registerResultType.isValid());
      registerResultType = iter.cur().type();
    }
  }
  if (!registerResultType.isValid()) {
    GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
              funcImportIndex);
    GenPrintf(DebugChannel::Import, masm, "void");
  } else {
    switch (registerResultType.kind()) {
      case ValType::I32:
        masm.load32(argv, ReturnReg);
        // No widening is required, as we know the value comes from an i32 load.
        GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
                  funcImportIndex);
        GenPrintIsize(DebugChannel::Import, masm, ReturnReg);
        break;
      case ValType::I64:
        masm.load64(argv, ReturnReg64);
        GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
                  funcImportIndex);
        GenPrintI64(DebugChannel::Import, masm, ReturnReg64);
        break;
      case ValType::V128:
        // Note, CallImport_Rtt/V128 currently always throws, so we should never
        // reach this point.
        masm.breakpoint();
        break;
      case ValType::F32:
        masm.loadFloat32(argv, ReturnFloat32Reg);
        GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
                  funcImportIndex);
        GenPrintF32(DebugChannel::Import, masm, ReturnFloat32Reg);
        break;
      case ValType::F64:
        masm.loadDouble(argv, ReturnDoubleReg);
        GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
                  funcImportIndex);
        GenPrintF64(DebugChannel::Import, masm, ReturnDoubleReg);
        break;
      case ValType::Ref:
        masm.loadPtr(argv, ReturnReg);
        GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
                  funcImportIndex);
        GenPrintPtr(DebugChannel::Import, masm, ReturnReg);
        break;
    }
  }

  GenPrintf(DebugChannel::Import, masm, "\n");

  // The native ABI preserves the instance, heap and global registers since they
  // are non-volatile.
  MOZ_ASSERT(NonVolatileRegs.has(InstanceReg));
#if defined(JS_CODEGEN_X64) || defined(JS_CODEGEN_ARM) ||      \
    defined(JS_CODEGEN_ARM64) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONG64) || defined(JS_CODEGEN_RISCV64)
  MOZ_ASSERT(NonVolatileRegs.has(HeapReg));
#endif

  GenerateExitEpilogue(masm, ExitReason::Fixed::ImportInterp,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Static,
                       offsets);

  return FinishOffsets(masm, offsets);
}

// Generate a stub that is called via the internal ABI derived from the
// signature of the import and calls into a compatible JIT function,
// having boxed all the ABI arguments into the JIT stack frame layout.
static bool GenerateImportJitExit(MacroAssembler& masm,
                                  uint32_t funcImportInstanceOffset,
                                  const FuncType& funcType,
                                  unsigned funcImportIndex,
                                  uint32_t fallbackOffset, Label* throwLabel,
                                  ImportOffsets* offsets) {
  AutoCreatedBy acb(masm, "GenerateImportJitExit");

  AssertExpectedSP(masm);

  // The JS ABI uses the following layout:
  //
  //   | caller's frame | <-  aligned to WasmStackAlignment
  //   +----------------+
  //   | return address | <- RetAddrAndFP
  //   | caller fp      | <---- fp/current sp
  //   +................+
  //   | saved instance | <-- Stored relative to fp, not sp
  //   | padding?       |
  //   | undefined args?|
  //   | ...            |
  //   | defined args   |
  //   | ...            |
  //   | this           | <- If this is JitStack-aligned, the layout will be too
  //   +................+
  //   | callee token   | <- PreFrame
  //   | descriptor     | <---- sp after allocating stack frame
  //   +----------------+
  //   | return address |
  //   | frame pointer  | <- Must be JitStack-aligned
  //
  // Note: WasmStackAlignment requires that sp be WasmStackAlignment-aligned
  // when calling, *before* pushing the return address and frame pointer. The JS
  // ABI requires that sp be JitStackAlignment-aligned *after* pushing the
  // return address and frame pointer.
  static_assert(WasmStackAlignment >= JitStackAlignment, "subsumes");

  // We allocate a full 8 bytes for the instance register, even on 32-bit,
  // because alignment padding will round it up anyway. Treating it as 8 bytes
  // is easier in the OOL underflow path.
  const unsigned sizeOfInstanceSlot = sizeof(Value);
  const unsigned sizeOfRetAddrAndFP = 2 * sizeof(void*);
  const unsigned sizeOfPreFrame =
      WasmToJSJitFrameLayout::Size() - sizeOfRetAddrAndFP;

  // These values are used if there is no arguments underflow.
  // If we need to push extra undefined arguments, we calculate them
  // dynamically in an out-of-line path.
  const unsigned sizeOfThisAndArgs =
      (1 + funcType.args().length()) * sizeof(Value);
  const unsigned totalJitFrameBytes = sizeOfRetAddrAndFP + sizeOfPreFrame +
                                      sizeOfThisAndArgs + sizeOfInstanceSlot;
  const unsigned jitFramePushed =
      StackDecrementForCall(JitStackAlignment,
                            sizeof(Frame),  // pushed by prologue
                            totalJitFrameBytes) -
      sizeOfRetAddrAndFP;

  // Generate a minimal prologue. Don't allocate a stack frame until we know
  // how big it should be.
  GenerateJitExitPrologue(masm, fallbackOffset, offsets);

  // 1. Allocate the stack frame.
  // 1.1. Get the callee. This must be a JSFunction if we're using this JIT
  // exit.
  Register callee = ABINonArgReturnReg0;
  Register scratch = ABINonArgReturnReg1;
  Register scratch2 = ABINonVolatileReg;
  masm.loadPtr(
      Address(InstanceReg, Instance::offsetInData(
                               funcImportInstanceOffset +
                               offsetof(FuncImportInstanceData, callable))),
      callee);

  // 1.2 Check to see if we are passing enough arguments. If not, we have to
  // allocate a larger stack frame and push `undefined` for the extra args.
  // (Passing too many arguments is not a problem; the JS ABI expects at *least*
  // numFormals arguments.)
  Label argUnderflow, argUnderflowRejoin;
  Register numFormals = scratch2;
  unsigned argc = funcType.args().length();
  masm.loadFunctionArgCount(callee, numFormals);
  masm.branch32(Assembler::GreaterThan, numFormals, Imm32(argc), &argUnderflow);

  // Otherwise, we can compute the stack frame size statically.
  masm.subFromStackPtr(Imm32(jitFramePushed));
  masm.bind(&argUnderflowRejoin);

  // 2. Descriptor.
  size_t argOffset = 0;
  uint32_t descriptor =
      MakeFrameDescriptorForJitCall(FrameType::WasmToJSJit, argc);
  masm.storePtr(ImmWord(uintptr_t(descriptor)),
                Address(masm.getStackPointer(), argOffset));
  argOffset += sizeof(size_t);

  // 3. Callee.
  masm.storePtr(callee, Address(masm.getStackPointer(), argOffset));
  argOffset += sizeof(size_t);
  MOZ_ASSERT(argOffset == sizeOfPreFrame);

  // 3. |this| value.
  masm.storeValue(UndefinedValue(), Address(masm.getStackPointer(), argOffset));
  argOffset += sizeof(Value);

  // 4. Fill the arguments.
  FillArgumentArrayForJitExit(masm, InstanceReg, funcImportIndex, funcType,
                              argOffset, scratch, scratch2);

  // 5. Preserve the instance register. We store it at a fixed negative offset
  // to the frame pointer so that we can recover it after the call without
  // needing to know how many arguments were passed.
  Address savedInstanceReg(FramePointer, -int32_t(sizeof(size_t)));
  masm.storePtr(InstanceReg, savedInstanceReg);

  // 6. Load callee executable entry point.
  masm.loadJitCodeRaw(callee, callee);

  masm.assertStackAlignment(JitStackAlignment, sizeOfRetAddrAndFP);
#ifdef JS_CODEGEN_ARM64
  AssertExpectedSP(masm);
  // Manually resync PSP.  Omitting this causes eg tests/wasm/import-export.js
  // to segfault.
  masm.moveStackPtrTo(PseudoStackPointer);
#endif
  masm.callJitNoProfiler(callee);

  // Note that there might be a GC thing in the JSReturnOperand now.
  // In all the code paths from here:
  // - either the value is unboxed because it was a primitive and we don't
  //   need to worry about rooting anymore.
  // - or the value needs to be rooted, but nothing can cause a GC between
  //   here and CoerceInPlace, which roots before coercing to a primitive.

  // The JIT callee clobbers all registers other than the frame pointer, so
  // restore InstanceReg here.
  masm.assertStackAlignment(JitStackAlignment, sizeOfRetAddrAndFP);
  masm.loadPtr(savedInstanceReg, InstanceReg);

  // The frame was aligned for the JIT ABI such that
  //   (sp - 2 * sizeof(void*)) % JitStackAlignment == 0
  // But now we possibly want to call one of several different C++ functions,
  // so subtract 2 * sizeof(void*) so that sp is aligned for an ABI call.
  static_assert(ABIStackAlignment <= JitStackAlignment, "subsumes");
  masm.subFromStackPtr(Imm32(sizeOfRetAddrAndFP));
  masm.assertStackAlignment(ABIStackAlignment);

#ifdef DEBUG
  {
    Label ok;
    masm.branchTestMagic(Assembler::NotEqual, JSReturnOperand, &ok);
    masm.breakpoint();
    masm.bind(&ok);
  }
#endif

  GenPrintf(DebugChannel::Import, masm, "wasm-import[%u]; returns ",
            funcImportIndex);

  Label oolConvert;
  const ValTypeVector& results = funcType.results();
  if (results.length() == 0) {
    GenPrintf(DebugChannel::Import, masm, "void");
  } else {
    MOZ_ASSERT(results.length() == 1, "multi-value return unimplemented");
    switch (results[0].kind()) {
      case ValType::I32:
        // No widening is required, as the return value does not come to us in
        // ReturnReg.
        masm.truncateValueToInt32(JSReturnOperand, ReturnDoubleReg, ReturnReg,
                                  &oolConvert);
        GenPrintIsize(DebugChannel::Import, masm, ReturnReg);
        break;
      case ValType::I64:
        // No fastpath for now, go immediately to ool case
        masm.jump(&oolConvert);
        break;
      case ValType::V128:
        // Unreachable as callImport should not call the stub.
        masm.breakpoint();
        break;
      case ValType::F32:
        masm.convertValueToFloat32(JSReturnOperand, ReturnFloat32Reg,
                                   &oolConvert);
        GenPrintF32(DebugChannel::Import, masm, ReturnFloat32Reg);
        break;
      case ValType::F64:
        masm.convertValueToDouble(JSReturnOperand, ReturnDoubleReg,
                                  &oolConvert);
        GenPrintF64(DebugChannel::Import, masm, ReturnDoubleReg);
        break;
      case ValType::Ref:
        // Guarded by temporarilyUnsupportedReftypeForExit()
        MOZ_RELEASE_ASSERT(results[0].refType().isExtern());
        masm.convertValueToWasmAnyRef(JSReturnOperand, ReturnReg,
                                      ABINonArgDoubleReg, &oolConvert);
        GenPrintPtr(DebugChannel::Import, masm, ReturnReg);
        break;
    }
  }

  GenPrintf(DebugChannel::Import, masm, "\n");

  Label done;
  masm.bind(&done);

  masm.moveToStackPtr(FramePointer);
  GenerateJitExitEpilogue(masm, offsets);

  masm.bind(&argUnderflow);
  // We aren't passing enough arguments.
  //
  // Compute the size of the stack frame (in Value-sized slots). On 32-bit, the
  // instance reg slot is 4 bytes of data and 4 bytes of alignment padding.
  Register numSlots = scratch;
  static_assert(sizeof(WasmToJSJitFrameLayout) % JitStackAlignment == 0);
  MOZ_ASSERT(sizeOfPreFrame % sizeof(Value) == 0);
  const uint32_t numSlotsForPreFrame = sizeOfPreFrame / sizeof(Value);
  const uint32_t extraSlots = numSlotsForPreFrame + 2;  // this + instance
  if (JitStackValueAlignment == 1) {
    // If we only need 8-byte alignment, no padding is necessary.
    masm.add32(Imm32(extraSlots), numFormals, numSlots);
  } else {
    MOZ_ASSERT(JitStackValueAlignment == 2);
    MOZ_ASSERT(sizeOfRetAddrAndFP == sizeOfPreFrame);
    // We have to allocate space for the preframe, `this`, the arguments, and
    // the saved instance. While doing so, we have to ensure that `this` is
    // aligned to JitStackAlignment, which will in turn guarantee the correct
    // alignment of the frame layout in the callee. To ensure alignment, we can
    // add padding between the arguments and the saved instance. sp was aligned
    // to WasmStackAlignment before pushing the return address / frame pointer
    // for this stub.
    //
    //                           (this)  (args)           (instance)
    // numSlots:        PreFrame + 1 + numFormals + padding + 1
    // aligned if even:            1 + numFormals + padding + 1 + RetAddrAndFP
    //
    // Conveniently, since numSlotsForPreFrame and numSlotsForRetAddrAndFP are
    // the same, these calculations give the same value. So we can ensure
    // alignment by rounding numSlots up to the next even number.
    masm.add32(Imm32(extraSlots + 1), numFormals, numSlots);
    masm.and32(Imm32(~1), numSlots);
  }

  // Adjust the stack pointer
  masm.lshift32(Imm32(3), scratch);
  masm.subFromStackPtr(scratch);

  // Fill the undefined arguments.
  Label loop;
  masm.bind(&loop);
  masm.sub32(Imm32(1), numFormals);
  BaseValueIndex argAddr(masm.getStackPointer(), numFormals,
                         2 * sizeof(uintptr_t) +  // descriptor + callee
                             sizeof(Value));      // this
  masm.storeValue(UndefinedValue(), BaseValueIndex(masm.getStackPointer(),
                                                   numFormals, argOffset));
  masm.branch32(Assembler::Above, numFormals, Imm32(argc), &loop);
  masm.jump(&argUnderflowRejoin);

  if (oolConvert.used()) {
    masm.bind(&oolConvert);

    // Coercion calls use the following stack layout (sp grows to the left):
    //   | args | padding | Value argv[1] | padding | exit Frame |
    MIRTypeVector coerceArgTypes;
    MOZ_ALWAYS_TRUE(coerceArgTypes.append(MIRType::Pointer));
    unsigned offsetToCoerceArgv =
        AlignBytes(StackArgBytesForNativeABI(coerceArgTypes), sizeof(Value));
    masm.assertStackAlignment(ABIStackAlignment);

    // Store return value into argv[0].
    masm.storeValue(JSReturnOperand,
                    Address(masm.getStackPointer(), offsetToCoerceArgv));

    // From this point, it's safe to reuse the scratch register (which
    // might be part of the JSReturnOperand).

    // The JIT might have clobbered exitFP at this point. Since there's
    // going to be a CoerceInPlace call, pretend we're still doing the JIT
    // call by restoring our tagged exitFP.
    LoadActivation(masm, InstanceReg, scratch);
    SetExitFP(masm, ExitReason::Fixed::ImportJit, scratch, scratch2);

    // argument 0: argv
    ABIArgMIRTypeIter i(coerceArgTypes, ABIKind::System);
    Address argv(masm.getStackPointer(), offsetToCoerceArgv);
    if (i->kind() == ABIArg::GPR) {
      masm.computeEffectiveAddress(argv, i->gpr());
    } else {
      masm.computeEffectiveAddress(argv, scratch);
      masm.storePtr(scratch,
                    Address(masm.getStackPointer(), i->offsetFromArgBase()));
    }
    i++;
    MOZ_ASSERT(i.done());

    // Call coercion function. Note that right after the call, the value of
    // FP is correct because FP is non-volatile in the native ABI.
    masm.assertStackAlignment(ABIStackAlignment);
    const ValTypeVector& results = funcType.results();
    if (results.length() > 0) {
      // NOTE that once there can be more than one result and we can box some of
      // the results (as we must for AnyRef), pointer and already-boxed results
      // must be rooted while subsequent results are boxed.
      MOZ_ASSERT(results.length() == 1, "multi-value return unimplemented");
      switch (results[0].kind()) {
        case ValType::I32:
          masm.call(SymbolicAddress::CoerceInPlace_ToInt32);
          masm.branchTest32(Assembler::Zero, ReturnReg, ReturnReg, throwLabel);
          masm.unboxInt32(Address(masm.getStackPointer(), offsetToCoerceArgv),
                          ReturnReg);
          // No widening is required, as we generate a known-good value in a
          // safe way here.
          break;
        case ValType::I64: {
          masm.call(SymbolicAddress::CoerceInPlace_ToBigInt);
          masm.branchTest32(Assembler::Zero, ReturnReg, ReturnReg, throwLabel);
          Address argv(masm.getStackPointer(), offsetToCoerceArgv);
          masm.unboxBigInt(argv, scratch);
          masm.loadBigInt64(scratch, ReturnReg64);
          break;
        }
        case ValType::F64:
        case ValType::F32:
          masm.call(SymbolicAddress::CoerceInPlace_ToNumber);
          masm.branchTest32(Assembler::Zero, ReturnReg, ReturnReg, throwLabel);
          masm.unboxDouble(Address(masm.getStackPointer(), offsetToCoerceArgv),
                           ReturnDoubleReg);
          if (results[0].kind() == ValType::F32) {
            masm.convertDoubleToFloat32(ReturnDoubleReg, ReturnFloat32Reg);
          }
          break;
        case ValType::Ref:
          // Guarded by temporarilyUnsupportedReftypeForExit()
          MOZ_RELEASE_ASSERT(results[0].refType().isExtern());
          masm.call(SymbolicAddress::BoxValue_Anyref);
          masm.branchWasmAnyRefIsNull(true, ReturnReg, throwLabel);
          break;
        default:
          MOZ_CRASH("Unsupported convert type");
      }
    }

    // Maintain the invariant that exitFP is either unset or not set to a
    // wasm tagged exitFP, per the jit exit contract.
    LoadActivation(masm, InstanceReg, scratch);
    ClearExitFP(masm, scratch);

    masm.jump(&done);
  }

  return FinishOffsets(masm, offsets);
}

struct ABIFunctionArgs {
  ABIFunctionType abiType;
  size_t len;

  explicit ABIFunctionArgs(ABIFunctionType sig)
      : abiType(ABIFunctionType(sig >> ABITypeArgShift)) {
    len = 0;
    uint64_t i = uint64_t(abiType);
    while (i) {
      i = i >> ABITypeArgShift;
      len++;
    }
  }

  size_t length() const { return len; }

  MIRType operator[](size_t i) const {
    MOZ_ASSERT(i < len);
    uint64_t abi = uint64_t(abiType);
    size_t argAtLSB = len - 1;
    while (argAtLSB != i) {
      abi = abi >> ABITypeArgShift;
      argAtLSB--;
    }
    return ToMIRType(ABIType(abi & ABITypeArgMask));
  }
};

bool wasm::GenerateBuiltinThunk(MacroAssembler& masm, ABIFunctionType abiType,
                                bool switchToMainStack, ExitReason exitReason,
                                void* funcPtr, CallableOffsets* offsets) {
  AssertExpectedSP(masm);
  masm.setFramePushed(0);

  ABIFunctionArgs args(abiType);
  unsigned framePushed =
      AlignBytes(StackArgBytesForNativeABI(args), ABIStackAlignment);
  GenerateExitPrologue(masm, exitReason, switchToMainStack,
                       ExitFrameAlignment::Static,
                       /*frameSize*/ framePushed, offsets);

  // Copy out and convert caller arguments, if needed. We are translating from
  // the wasm ABI to the system ABI.
  Register scratch = ABINonArgReturnReg0;

  // Use two arg iterators to track the different offsets that arguments must
  // go. We are translating from the wasm ABI to the system ABI.
  ABIArgIter selfArgs(args, ABIKind::Wasm);
  ABIArgIter callArgs(args, ABIKind::System);

  // `selfArgs` gives us offsets from 'arg base' which is the SP immediately
  // before our frame is added. We must add `sizeof(Frame)` now that the
  // prologue has executed to access our stack args.
  unsigned offsetFromFPToCallerStackArgs = sizeof(wasm::Frame);

  for (; !selfArgs.done(); selfArgs++, callArgs++) {
    // This loop doesn't handle all the possible cases of differing ABI's and
    // relies on the wasm argument ABI being very close to the system ABI.
    MOZ_ASSERT(!callArgs.done());
    MOZ_ASSERT(selfArgs->argInRegister() == callArgs->argInRegister());
    MOZ_ASSERT(selfArgs.mirType() == callArgs.mirType());

    if (selfArgs->argInRegister()) {
#ifdef JS_CODEGEN_ARM
      // The system ABI may use soft-FP, while the wasm ABI will always use
      // hard-FP. We must adapt FP args in this case.
      if (!ARMFlags::UseHardFpABI() &&
          IsFloatingPointType(selfArgs.mirType())) {
        FloatRegister input = selfArgs->fpu();
        if (selfArgs.mirType() == MIRType::Float32) {
          masm.ma_vxfer(input, Register::FromCode(input.id()));
        } else if (selfArgs.mirType() == MIRType::Double) {
          uint32_t regId = input.singleOverlay().id();
          masm.ma_vxfer(input, Register::FromCode(regId),
                        Register::FromCode(regId + 1));
        }
      }
#endif
      continue;
    }

    Address src(FramePointer,
                offsetFromFPToCallerStackArgs + selfArgs->offsetFromArgBase());
    Address dst(masm.getStackPointer(), callArgs->offsetFromArgBase());
    StackCopy(masm, selfArgs.mirType(), scratch, src, dst);
  }
  // If selfArgs is done, callArgs must be done.
  MOZ_ASSERT(callArgs.done());

  // Call into the native builtin function
  masm.assertStackAlignment(ABIStackAlignment);
  MoveSPForJitABI(masm);
  masm.call(ImmPtr(funcPtr, ImmPtr::NoCheckToken()));

#if defined(JS_CODEGEN_X64)
  // No widening is required, as the caller will widen.
#elif defined(JS_CODEGEN_X86)
  // The wasm ABI always uses SSE for floating point returns, and so we must
  // convert the x87 FP stack result over.
  Operand op(esp, 0);
  MIRType retType = ToMIRType(ABIType(
      std::underlying_type_t<ABIFunctionType>(abiType) & ABITypeArgMask));
  if (retType == MIRType::Float32) {
    masm.fstp32(op);
    masm.loadFloat32(op, ReturnFloat32Reg);
  } else if (retType == MIRType::Double) {
    masm.fstp(op);
    masm.loadDouble(op, ReturnDoubleReg);
  }
#elif defined(JS_CODEGEN_ARM)
  // We must adapt the system soft-fp return value from a GPR to a FPR.
  MIRType retType = ToMIRType(ABIType(
      std::underlying_type_t<ABIFunctionType>(abiType) & ABITypeArgMask));
  if (!ARMFlags::UseHardFpABI() && IsFloatingPointType(retType)) {
    masm.ma_vxfer(r0, r1, d0);
  }
#endif

  GenerateExitEpilogue(masm, exitReason, switchToMainStack,
                       ExitFrameAlignment::Static, offsets);
  return FinishOffsets(masm, offsets);
}

#if defined(JS_CODEGEN_ARM)
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~((Registers::SetType(1) << Registers::sp) |
                         (Registers::SetType(1) << Registers::pc))),
    FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  ifdef ENABLE_WASM_SIMD
#    error "high lanes of SIMD registers need to be saved too."
#  endif
#elif defined(JS_CODEGEN_MIPS64)
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~((Registers::SetType(1) << Registers::k0) |
                         (Registers::SetType(1) << Registers::k1) |
                         (Registers::SetType(1) << Registers::sp) |
                         (Registers::SetType(1) << Registers::zero))),
    FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  ifdef ENABLE_WASM_SIMD
#    error "high lanes of SIMD registers need to be saved too."
#  endif
#elif defined(JS_CODEGEN_LOONG64)
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~((uint32_t(1) << Registers::tp) |
                         (uint32_t(1) << Registers::fp) |
                         (uint32_t(1) << Registers::sp) |
                         (uint32_t(1) << Registers::zero))),
    FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  ifdef ENABLE_WASM_SIMD
#    error "high lanes of SIMD registers need to be saved too."
#  endif
#elif defined(JS_CODEGEN_RISCV64)
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~((uint32_t(1) << Registers::tp) |
                         (uint32_t(1) << Registers::fp) |
                         (uint32_t(1) << Registers::sp) |
                         (uint32_t(1) << Registers::zero))),
    FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  ifdef ENABLE_WASM_SIMD
#    error "high lanes of SIMD registers need to be saved too."
#  endif
#elif defined(JS_CODEGEN_ARM64)
// We assume that traps do not happen while lr is live. This both ensures that
// the size of RegsToPreserve is a multiple of 2 (preserving WasmStackAlignment)
// and gives us a register to clobber in the return path.
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~((Registers::SetType(1) << RealStackPointer.code()) |
                         (Registers::SetType(1) << Registers::lr))),
#  ifdef ENABLE_WASM_SIMD
    FloatRegisterSet(FloatRegisters::AllSimd128Mask));
#  else
    // If SIMD is not enabled, it's pointless to save/restore the upper 64
    // bits of each vector register.
    FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  endif
#elif defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
// It's correct to use FloatRegisters::AllMask even when SIMD is not enabled;
// PushRegsInMask strips out the high lanes of the XMM registers in this case,
// while the singles will be stripped as they are aliased by the larger doubles.
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(Registers::AllMask &
                       ~(Registers::SetType(1) << Registers::StackPointer)),
    FloatRegisterSet(FloatRegisters::AllMask));
#else
static const LiveRegisterSet RegsToPreserve(
    GeneralRegisterSet(0), FloatRegisterSet(FloatRegisters::AllDoubleMask));
#  ifdef ENABLE_WASM_SIMD
#    error "no SIMD support"
#  endif
#endif

// Generate a RegisterOffsets which describes the locations of the GPRs as saved
// by GenerateTrapExit.  FP registers are ignored.  Note that the values
// stored in the RegisterOffsets are offsets in words downwards from the top of
// the save area.  That is, a higher value implies a lower address.
void wasm::GenerateTrapExitRegisterOffsets(RegisterOffsets* offsets,
                                           size_t* numWords) {
  // This is the number of words pushed by the initial WasmPush().
  *numWords = WasmPushSize / sizeof(void*);
  MOZ_ASSERT(*numWords == TrapExitDummyValueOffsetFromTop + 1);

  // And these correspond to the PushRegsInMask() that immediately follows.
  for (GeneralRegisterBackwardIterator iter(RegsToPreserve.gprs()); iter.more();
       ++iter) {
    offsets->setOffset(*iter, *numWords);
    (*numWords)++;
  }
}

// Generate a stub which calls WasmReportTrap() and can be executed by having
// the signal handler redirect PC from any trapping instruction.
static bool GenerateTrapExit(MacroAssembler& masm, Label* throwLabel,
                             Offsets* offsets) {
  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);

  masm.setFramePushed(0);

  offsets->begin = masm.currentOffset();

  // Traps can only happen at well-defined program points. However, since
  // traps may resume and the optimal assumption for the surrounding code is
  // that registers are not clobbered, we need to preserve all registers in
  // the trap exit. One simplifying assumption is that flags may be clobbered.
  // Push a dummy word to use as return address below.
  WasmPush(masm, ImmWord(TrapExitDummyValue));
  unsigned framePushedBeforePreserve = masm.framePushed();
  masm.PushRegsInMask(RegsToPreserve);
  unsigned offsetOfReturnWord = masm.framePushed() - framePushedBeforePreserve;

  // Load the instance register from the wasm::FrameWithInstances. Normally we
  // are only guaranteed to have a valid instance there if the frame was a
  // cross-instance call, however wasm::HandleTrap in the signal handler is
  // kind enough to store the active instance into that slot for us.
  masm.loadPtr(
      Address(FramePointer, wasm::FrameWithInstances::calleeInstanceOffset()),
      InstanceReg);

  // We are leaving wasm code to run the trap handler, which must always see
  // an IEEE FP environment.
  GenerateLeaveWasmFPEnvironment(masm, InstanceReg);

  // Grab the stack pointer before we do any stack switches or dynamic
  // alignment. Store it in a register that won't be used in the stack switch
  // operation.
  Register originalStackPointer = ABINonArgReg3;
#ifdef ENABLE_WASM_JSPI
  masm.reserveStack(sizeof(void*) * 2);
#endif
  masm.moveStackPtrTo(originalStackPointer);

#ifdef ENABLE_WASM_JSPI
  GenerateExitPrologueMainStackSwitch(masm, Address(masm.getStackPointer(), 0),
                                      InstanceReg, ABINonArgReg0, ABINonArgReg1,
                                      ABINonArgReg2);
#endif

  // We know that StackPointer is word-aligned, but not necessarily
  // stack-aligned, so we need to align it dynamically. After we've aligned the
  // stack, we store the original stack pointer in a slot on the stack.
  // We're careful to not break stack alignment with that slot.
  masm.andToStackPtr(Imm32(~(ABIStackAlignment - 1)));
  masm.reserveStack(ABIStackAlignment);
  masm.storePtr(originalStackPointer, Address(masm.getStackPointer(), 0));

  // Push the shadow stack space for the call if we need to. This won't break
  // stack alignment.
  if (ShadowStackSpace) {
    masm.subFromStackPtr(Imm32(ShadowStackSpace));
  }

  // Call the WasmHandleTrap function.
  masm.assertStackAlignment(ABIStackAlignment);
  masm.call(SymbolicAddress::HandleTrap);

  // WasmHandleTrap returns null if control should transfer to the throw stub.
  // That will unwind the stack, and so we don't need to pop anything from the
  // stack ourselves.
  masm.branchTestPtr(Assembler::Zero, ReturnReg, ReturnReg, throwLabel);

  // Remove the shadow stack space that we added.
  if (ShadowStackSpace) {
    masm.addToStackPtr(Imm32(ShadowStackSpace));
  }

  // Get the original stack pointer back for before we dynamically aligned it.
  // This will switch the SP back to the original stack we were on. Be careful
  // not to use the return register for this, which is live.
  masm.loadPtr(Address(masm.getStackPointer(), 0), ABINonArgReturnReg0);
  masm.moveToStackPtr(ABINonArgReturnReg0);

#ifdef ENABLE_WASM_JSPI
  // We don't need to reload the InstanceReg because it is non-volatile in the
  // system ABI.
  MOZ_ASSERT(NonVolatileRegs.has(InstanceReg));
  GenerateExitEpilogueMainStackReturn(masm, Address(masm.getStackPointer(), 0),
                                      InstanceReg, ABINonArgReturnReg0,
                                      ABINonArgReturnReg1);

  masm.freeStack(sizeof(void*) * 2);
#endif

  // We are about to resume wasm code. This must happen before restoring the
  // registers below, because the interrupted wasm code need not have had the
  // instance in InstanceReg. InstanceReg still holds the instance we loaded
  // above because it is non-volatile in the system ABI.
  MOZ_ASSERT(NonVolatileRegs.has(InstanceReg));
  GenerateEnterWasmFPEnvironment(masm, InstanceReg);

  // Otherwise, the return value is the TrapData::resumePC we must jump to.
  // We must restore register state before jumping, which will clobber
  // ReturnReg, so store ReturnReg in the above-reserved stack slot which we
  // use to jump to via ret.
  masm.storePtr(ReturnReg, Address(masm.getStackPointer(), offsetOfReturnWord));
  masm.PopRegsInMask(RegsToPreserve);

#ifdef JS_CODEGEN_ARM64
  WasmPop(masm, lr);
  masm.abiret();
#else
  masm.ret();
#endif

  return FinishOffsets(masm, offsets);
}

void wasm::ClobberWasmRegsForLongJmp(MacroAssembler& masm, Register jumpReg) {
  // Get the set of all registers that are allocatable in wasm functions
  AllocatableGeneralRegisterSet gprs(GeneralRegisterSet::All());
  RegisterAllocator::takeWasmRegisters(gprs);
  // Remove the instance register from this set as landing pads require it to be
  // valid
  gprs.take(InstanceReg);
  // Remove a specified register that will be used for the longjmp
  gprs.take(jumpReg);
  // Set all of these registers to zero
  for (GeneralRegisterIterator iter(gprs.asLiveSet()); iter.more(); ++iter) {
    Register reg = *iter;
    masm.xorPtr(reg, reg);
  }

  // Get the set of all floating point registers that are allocatable in wasm
  // functions
  AllocatableFloatRegisterSet fprs(FloatRegisterSet::All());
  // Set all of these registers to NaN. We attempt for this to be a signalling
  // NaN, but the bit format for signalling NaNs are implementation defined
  // and so this is just best effort.
  Maybe<FloatRegister> regNaN;
  for (FloatRegisterIterator iter(fprs.asLiveSet()); iter.more(); ++iter) {
    FloatRegister reg = *iter;
    if (!reg.isDouble()) {
      continue;
    }
    if (regNaN) {
      masm.moveDouble(*regNaN, reg);
      continue;
    }
    masm.loadConstantDouble(std::numeric_limits<double>::signaling_NaN(), reg);
    regNaN = Some(reg);
  }
}

#ifdef ENABLE_WASM_JSPI
bool wasm::GenerateContBaseFrameStub(jit::MacroAssembler& masm,
                                     Offsets* offsets) {
  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);
  masm.setFramePushed(0);

  offsets->begin = masm.currentOffset();

  Register scratch1 = ABINonArgReg0;
  Register scratch2 = ABINonArgReg1;
  Register scratch3 = ABINonArgReg2;
  Register scratch4 = ABINonArgReg3;

  int32_t offsetFromFPToStack = -ContStack::offsetOfBaseFrameFP();

  // Store initial callee's InstanceReg into calleeInstance_ before clearing
  // initialResumeTarget_.
  masm.storePtr(InstanceReg,
                Address(FramePointer,
                        static_cast<int32_t>(
                            wasm::FrameWithInstances::calleeInstanceOffset())));

  // Clear the 'resumeTarget' in our frame.
  masm.computeEffectiveAddress(
      Address(FramePointer,
              offsetFromFPToStack + ContStack::offsetOfInitialResumeTarget()),
      scratch1);
  EmitClearSwitchTarget(masm, scratch1);

  // Load the cont.new callee funcref into WasmCallRefReg, and clear it the
  // field.
  MOZ_ASSERT(scratch4 == WasmCallRefReg);
  masm.loadPtr(
      Address(FramePointer,
              offsetFromFPToStack + ContStack::offsetOfInitialResumeCallee()),
      scratch4);
  masm.storePtr(
      ImmWord(0),
      Address(FramePointer,
              offsetFromFPToStack + ContStack::offsetOfInitialResumeCallee()));

  // Perform a call_ref of the callee. We aren't passing an arguments or
  // receiving any results yet.
  masm.reserveStack(
      ComputeByteAlignment(sizeof(Frame), WasmStackAlignment) +
      AlignBytes(wasm::FrameWithInstances::sizeOfInstanceFieldsAndShadowStack(),
                 WasmStackAlignment));
  masm.assertStackAlignment(WasmStackAlignment);
  wasm::CallSiteDesc callSite(CallSiteKind::FuncRef);
  wasm::CalleeDesc callee = wasm::CalleeDesc::wasmFuncRef();
  CodeOffset fastCallOffset;
  CodeOffset slowCallOffset;
  masm.wasmCallRef(callSite, callee, &fastCallOffset, &slowCallOffset);
  // This is probably unnecessary. At the least we should free the same amount?
  masm.freeStack(
      wasm::FrameWithInstances::sizeOfInstanceFieldsAndShadowStack());

  // The call returned and we must switch back to our handler's stack. Get the
  // return target of handler and switch to it.
  masm.loadPtr(Address(FramePointer, -ContStack::offsetOfBaseFrameFP() +
                                         ContStack::offsetOfHandlers()),
               scratch1);
  masm.computeEffectiveAddress(
      Address(scratch1, offsetof(wasm::Handlers, returnTarget)), scratch1);
  wasm::EmitSwitchStack(masm, scratch1, scratch2, scratch3, scratch4);

  // We should never reach here.
  masm.breakpoint();

  return FinishOffsets(masm, offsets);
}
#endif

// Generates code to jump to a Wasm catch handler after unwinding the stack.
// The |rfe| register stores a pointer to the ResumeFromException struct
// allocated on the stack.
void wasm::GenerateJumpToCatchHandler(MacroAssembler& masm, Register rfe,
                                      Register scratch1, Register scratch2,
                                      Register scratch3) {
  masm.loadPtr(Address(rfe, ResumeFromException::offsetOfInstance()),
               InstanceReg);
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());
  masm.switchToWasmInstanceRealm(scratch1, scratch2);

#ifdef ENABLE_WASM_JSPI
  // Restore the stack-switching state for the catch handler's stack. Exception
  // unwinding may have freed continuation stacks and returned to the main
  // stack, so we need to restore baseHandlers_ (which may now be null) and
  // enter the correct StackTarget so that currentStack_ and stackLimit are
  // consistent with where the catch handler lives.
  masm.loadPtr(Address(InstanceReg, wasm::Instance::offsetOfCx()), scratch1);
  masm.loadPtr(Address(rfe, ResumeFromException::offsetOfBaseHandlers()),
               scratch2);
  masm.storePtr(scratch2,
                Address(scratch1, JSContext::offsetOfWasm() +
                                      wasm::Context::offsetOfBaseHandlers()));
  masm.loadPtr(Address(rfe, ResumeFromException::offsetOfStackTarget()),
               scratch2);
  EmitEnterStackTarget(masm, scratch1, scratch2, scratch3);
#endif

  masm.loadPtr(Address(rfe, ResumeFromException::offsetOfTarget()), scratch1);
  masm.loadPtr(Address(rfe, ResumeFromException::offsetOfFramePointer()),
               FramePointer);
  masm.loadStackPtr(Address(rfe, ResumeFromException::offsetOfStackPointer()));
  MoveSPForJitABI(masm);
  wasm::ClobberWasmRegsForLongJmp(masm, scratch1);

  // We are about to re-enter wasm code, disable denormals if wasm requires
  // that.
  GenerateEnterWasmFPEnvironment(masm, InstanceReg);

  masm.jump(scratch1);
}

// Generate a stub that calls the C++ exception handler.
static bool GenerateThrowStub(MacroAssembler& masm, Label* throwLabel,
                              Offsets* offsets) {
  Register scratch1 = ABINonArgReturnReg0;

  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);
  masm.setFramePushed(0);

  masm.bind(throwLabel);

  offsets->begin = masm.currentOffset();

  // Every jump to this stub is expected to happen in the system FP
  // environment.
  AssertDenormalsEnabled(masm);

  // Conservatively, the stack pointer can be unaligned and we must align it
  // dynamically.
  masm.andToStackPtr(Imm32(~(ABIStackAlignment - 1)));
  if (ShadowStackSpace) {
    masm.subFromStackPtr(Imm32(ShadowStackSpace));
  }

  // Allocate space for exception or regular resume information.
  masm.reserveStack(sizeof(jit::ResumeFromException));
  masm.moveStackPtrTo(scratch1);

  MIRTypeVector handleThrowTypes;
  MOZ_ALWAYS_TRUE(handleThrowTypes.append(MIRType::Pointer));

  unsigned frameSize =
      StackDecrementForCall(ABIStackAlignment, masm.framePushed(),
                            StackArgBytesForNativeABI(handleThrowTypes));
  masm.reserveStack(frameSize);
  masm.assertStackAlignment(ABIStackAlignment);

  ABIArgMIRTypeIter i(handleThrowTypes, ABIKind::System);
  if (i->kind() == ABIArg::GPR) {
    masm.movePtr(scratch1, i->gpr());
  } else {
    masm.storePtr(scratch1,
                  Address(masm.getStackPointer(), i->offsetFromArgBase()));
  }
  i++;
  MOZ_ASSERT(i.done());

  // WasmHandleThrow unwinds JitActivation::wasmExitFP() and initializes the
  // ResumeFromException struct we allocated on the stack.
  //
  // It returns the address of the JIT's exception handler trampoline that we
  // should jump to. This trampoline will return to the interpreter entry or
  // jump to a catch handler.
  masm.call(SymbolicAddress::HandleThrow);

  // Ensure the ResumeFromException struct is on top of the stack.
  masm.freeStack(frameSize);

  // Jump to the "return value check" code of the JIT's exception handler
  // trampoline. On ARM64 ensure PSP matches SP.
#ifdef JS_CODEGEN_ARM64
  masm.Mov(PseudoStackPointer64, sp);
#endif
  masm.jump(ReturnReg);

  return FinishOffsets(masm, offsets);
}

// Generate a stub that handles toggleable enter/leave frame traps or
// breakpoints.  The stub records the frame pointer (via GenerateExitPrologue)
// and saves most of registers, so as to not affect the code generated by
// WasmBaselineCompile.
static bool GenerateDebugStub(MacroAssembler& masm, Label* throwLabel,
                              CallableOffsets* offsets) {
  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);
  masm.setFramePushed(0);

  GenerateExitPrologue(masm, ExitReason::Fixed::DebugStub,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Dynamic,
                       0, offsets);

  uint32_t framePushed = masm.framePushed();

  if (ShadowStackSpace) {
    masm.subFromStackPtr(Imm32(ShadowStackSpace));
  }
  masm.assertStackAlignment(ABIStackAlignment);
  masm.call(SymbolicAddress::HandleDebugTrap);

  masm.branchIfFalseBool(ReturnReg, throwLabel);

  if (ShadowStackSpace) {
    masm.addToStackPtr(Imm32(ShadowStackSpace));
  }

  MOZ_ASSERT(NonVolatileRegs.has(InstanceReg));
  masm.loadWasmPinnedRegsFromInstance(mozilla::Nothing());

  masm.setFramePushed(framePushed);

  GenerateExitEpilogue(masm, ExitReason::Fixed::DebugStub,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Dynamic,
                       offsets);

  return FinishOffsets(masm, offsets);
}

static bool GenerateRequestTierUpStub(MacroAssembler& masm,
                                      CallableOffsets* offsets) {
  // This is similar to GenerateDebugStub.  As with that routine, all registers
  // are saved, we call out to a C++ helper, then restore the registers.  The
  // helper can't fail, though.
  //
  // On entry to (the code generated by) this routine, we expect the requesting
  // instance pointer to be in InstanceReg, regardless of the platform.

  AutoCreatedBy acb(masm, "GenerateRequestTierUpStub");
  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);
  masm.setFramePushed(0);

  GenerateExitPrologue(masm, ExitReason::Fixed::RequestTierUp,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Dynamic,
                       0, offsets);

  uint32_t framePushed = masm.framePushed();

  if (ShadowStackSpace > 0) {
    masm.subFromStackPtr(Imm32(ShadowStackSpace));
  }
  masm.assertStackAlignment(ABIStackAlignment);

  // Pass InstanceReg as the first (and only) arg to the C++ routine.  We
  // expect that the only target to pass the first integer arg in memory is
  // x86_32, and handle that specially.
  ABIArgGenerator abi(ABIKind::System);
  ABIArg arg = abi.next(MIRType::Pointer);
#ifndef JS_CODEGEN_X86
  // The arg rides in a reg.
  MOZ_RELEASE_ASSERT(arg.kind() == ABIArg::GPR);
  masm.movePtr(InstanceReg, arg.gpr());
#else
  // Ensure we don't need to consider ShadowStackSpace.
  static_assert(ShadowStackSpace == 0);
  // Ensure the ABIArgGenerator is consistent with the code generation
  // assumptions we make here.
  MOZ_RELEASE_ASSERT(arg.kind() == ABIArg::Stack &&
                     arg.offsetFromArgBase() == 0);
  // Get the arg on the stack without messing up the stack alignment.
  masm.subFromStackPtr(Imm32(12));
  masm.push(InstanceReg);
#endif

  masm.call(SymbolicAddress::HandleRequestTierUp);
  // The call can't fail (meaning, if it does fail, we ignore that)

#ifdef JS_CODEGEN_X86
  // Remove the arg and padding we just pushed.
  masm.addToStackPtr(Imm32(16));
#endif

  if (ShadowStackSpace > 0) {
    masm.addToStackPtr(Imm32(ShadowStackSpace));
  }

  masm.setFramePushed(framePushed);

  GenerateExitEpilogue(masm, ExitReason::Fixed::RequestTierUp,
                       /*switchToMainStack*/ true, ExitFrameAlignment::Dynamic,
                       offsets);

  return FinishOffsets(masm, offsets);
}

static bool GenerateUpdateCallRefMetricsStub(MacroAssembler& masm,
                                             CallableOffsets* offsets) {
  // This is a stub which is entirely self-contained -- it calls no other
  // functions, cannot fail, and creates a minimal stack frame.  It can only
  // use three registers, `regMetrics`, `regFuncRef` and `regScratch`, as set
  // up below, and as described in BaseCompiler::updateCallRefMetrics.  All
  // other registers must remain unchanged.  Also, we may read InstanceReg.
  //
  // `regMetrics` (the CallRefMetrics*) should satisfy
  // CallRefMetrics::invariantsOK() both on entry to and exit from the code
  // generated here.

  // `regMetrics` and `regFuncRef` are live at entry, but not `regScratch`.
  const Register regMetrics = WasmCallRefCallScratchReg0;  // CallRefMetrics*
  const Register regFuncRef = WasmCallRefCallScratchReg1;  // FuncExtended*
  const Register regScratch = WasmCallRefCallScratchReg2;  // scratch

  // At entry to the stub, `regMetrics` points at the CallRefMetrics,
  // `regFuncRef` points at the FunctionExtended, `regScratch` is available as
  // scratch, `regFuncRef` is known to be non-null, and, if the target0/count0
  // slot is in use, it is known not to match that slot.  The call may or may
  // not be cross-instance.

  // Briefly, what we generate here is:
  //
  //   assert(regFuncRef is non-null)
  //
  //   if (regFuncRef is a cross instance call) {
  //     regMetrics->countOther++;
  //     return;
  //   }
  //
  //   assert(regFuncRef != regMetrics->targets[0]);
  //
  //   for (i = 1; i < NUM_SLOTS; i++) {
  //     if (regFuncRef == regMetrics->targets[i]) {
  //       regMetrics->counts[i]++;
  //       if (regMetrics->counts[i-1] <u regMetrics->counts[i]) {
  //         // swap regMetrics->counts[i-1]/[i] and
  //         regMetrics->targets[i-1]/[i]
  //       }
  //       return;
  //     }
  //   }
  //
  //   for (i = 0; i < NUM_SLOTS; i++) {
  //     if (regMetrics->targets[i] is nullptr) {
  //       regMetrics->targets[i] = regFuncRef;
  //       regMetrics->counts[i] = 1;
  //       return;
  //     }
  //   }
  //
  //   regMetrics->countsOther++;
  //   return;
  //
  // And the loops are unrolled.

  // Frame setup and unwinding: we generate the absolute minimal frame setup
  // (`push FP; FP := SP` / `pop FP; ret`).  There is no register save/restore
  // in the frame.  The routine created here is a leaf and will neither trap
  // nor invoke GC, so the unwindability requirements are minimal -- only the
  // profiler will need to be able to unwind through it.

  // See declaration of CallRefMetrics for comments about assignments of
  // funcrefs to `CallRefMetrics::targets[]` fields.

  AutoCreatedBy acb(masm, "GenerateUpdateCallRefMetricsStub");
  Label ret;

  AssertExpectedSP(masm);
  masm.haltingAlign(CodeAlignment);
  masm.setFramePushed(0);

  GenerateMinimalPrologue(masm, &offsets->begin);

#ifdef DEBUG
  // Assertion: we know the target is non-null at entry, because the in-line
  // code created by BaseCompiler::callRef handles that case.
  // if (regFuncRef == nullptr) {
  //   crash;
  // }
  Label after1;
  masm.branchWasmAnyRefIsNull(/*isNull=*/false, regFuncRef, &after1);

  masm.breakpoint();

  masm.bind(&after1);
#endif

  // If it is a cross-instance call, add it to the `countOther` bin.
  // regScratch = regFuncRef->instance;
  // if (regScratch != thisInstance) {
  //   regScratch = regMetrics->countOther;
  //   regScratch++;
  //   regMetrics->countOther = regScratch;
  //   return;
  // }
  Label after2;
  const size_t offsetOfInstanceSlot = FunctionExtended::offsetOfExtendedSlot(
      FunctionExtended::WASM_INSTANCE_SLOT);
  masm.loadPtr(Address(regFuncRef, offsetOfInstanceSlot), regScratch);
  masm.branchPtr(Assembler::Equal, InstanceReg, regScratch, &after2);
  //
  const size_t offsetOfCountOther = CallRefMetrics::offsetOfCountOther();
  masm.load32(Address(regMetrics, offsetOfCountOther), regScratch);
  masm.add32(Imm32(1), regScratch);
  masm.store32(regScratch, Address(regMetrics, offsetOfCountOther));
  masm.jump(&ret);
  //
  masm.bind(&after2);

#ifdef DEBUG
  // Assertion: we know it can't be a hit at slot zero, because the inline code
  // also handles that case.
  // regScratch = regMetrics->targets[0];
  // if (regScratch == regFuncRef) {
  //   crash;
  // }
  Label after3;
  const size_t offsetOfTarget0 = CallRefMetrics::offsetOfTarget(0);
  masm.loadPtr(Address(regMetrics, offsetOfTarget0), regScratch);
  masm.branchPtr(Assembler::NotEqual, regScratch, regFuncRef, &after3);

  masm.breakpoint();

  masm.bind(&after3);
#endif

  // If it matches slot one, increment count, swap with slot zero if needed
  // regScratch = regMetrics->targets[1];
  // if (regFuncRef == regScratch) {
  //   // We need a second temp register (regScratch being the first).
  //   // We no longer need regFuncRef so use that as the second temp.
  //   regScratch = regMetrics->counts[0];
  //   regFuncRef = regMetrics->counts[1];
  //   regFuncRef++;
  //   regMetrics->counts[1] = regFuncRef;
  //   if (regScratch <u regFuncRef) {
  //     // regScratch and regFuncRef
  //     // are regMetrics->counts[0] and [1] respectively
  //     regMetrics->counts[0] = regFuncRef;
  //     regMetrics->counts[1] = regScratch;
  //     regScratch = regMetrics->targets[0];
  //     regFuncRef = regMetrics->targets[1];
  //     regMetrics->targets[0] = regFuncRef;
  //     regMetrics->targets[1] = regScratch;
  //   }
  //   return;
  // }
  // and the same for slots 2, 3, 4, etc
  for (size_t i = 1; i < CallRefMetrics::NUM_SLOTS; i++) {
    Label after4;
    masm.loadPtr(Address(regMetrics, CallRefMetrics::offsetOfTarget(i)),
                 regScratch);
    masm.branchPtr(Assembler::NotEqual, regFuncRef, regScratch, &after4);

    masm.load32(Address(regMetrics, CallRefMetrics::offsetOfCount(i - 1)),
                regScratch);
    masm.load32(Address(regMetrics, CallRefMetrics::offsetOfCount(i)),
                regFuncRef);
    masm.add32(Imm32(1), regFuncRef);
    masm.store32(regFuncRef,
                 Address(regMetrics, CallRefMetrics::offsetOfCount(i)));
    masm.branch32(Assembler::AboveOrEqual, regScratch, regFuncRef, &ret);

    masm.store32(regFuncRef,
                 Address(regMetrics, CallRefMetrics::offsetOfCount(i - 1)));
    masm.store32(regScratch,
                 Address(regMetrics, CallRefMetrics::offsetOfCount(i)));
    masm.loadPtr(Address(regMetrics, CallRefMetrics::offsetOfTarget(i - 1)),
                 regScratch);
    masm.loadPtr(Address(regMetrics, CallRefMetrics::offsetOfTarget(i)),
                 regFuncRef);
    masm.storePtr(regFuncRef,
                  Address(regMetrics, CallRefMetrics::offsetOfTarget(i - 1)));
    masm.storePtr(regScratch,
                  Address(regMetrics, CallRefMetrics::offsetOfTarget(i)));
    masm.jump(&ret);

    masm.bind(&after4);
  }

  // Not found.  Use the first unused slot, if available.  This assumes that T
  // is non-null; but that is assured us on entry (and asserted above).  See
  // CallRefMetrics::invariantsOK.
  // if (regMetrics->targets[0] == nullptr) {
  //   regMetrics->targets[0] = regFuncRef;
  //   regMetrics->counts[0] = 1;
  //   return;
  // }
  // and the same for slots 1, 2, 3, 4, etc
  for (size_t i = 0; i < CallRefMetrics::NUM_SLOTS; i++) {
    Label after5;
    masm.loadPtr(Address(regMetrics, CallRefMetrics::offsetOfTarget(i)),
                 regScratch);
    masm.branchWasmAnyRefIsNull(/*isNull=*/false, regScratch, &after5);

    masm.storePtr(regFuncRef,
                  Address(regMetrics, CallRefMetrics::offsetOfTarget(i)));
    masm.store32(Imm32(1),
                 Address(regMetrics, CallRefMetrics::offsetOfCount(i)));
    masm.jump(&ret);

    masm.bind(&after5);
  }

  // Not found, and we don't have a slot with which to track this new target
  // individually.  Instead just increment the "all others" bin.
  // regScratch = regMetrics->countOther;
  // regScratch++;
  // regMetrics->countOther = regScratch;
  // return;
  masm.load32(Address(regMetrics, CallRefMetrics::offsetOfCountOther()),
              regScratch);
  masm.add32(Imm32(1), regScratch);
  masm.store32(regScratch,
               Address(regMetrics, CallRefMetrics::offsetOfCountOther()));

  masm.bind(&ret);

  MOZ_ASSERT(masm.framePushed() == 0);
  GenerateMinimalEpilogue(masm, &offsets->ret);

  return FinishOffsets(masm, offsets);
}

bool wasm::GenerateEntryStubs(const CodeMetadata& codeMeta,
                              const FuncExportVector& exports,
                              CompiledCode* code) {
  LifoAlloc lifo(STUBS_LIFO_DEFAULT_CHUNK_SIZE, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jcx;
  WasmMacroAssembler masm(alloc);
  AutoCreatedBy acb(masm, "wasm::GenerateEntryStubs");

  // Swap in already-allocated empty vectors to avoid malloc/free.
  if (!code->swap(masm)) {
    return false;
  }

  JitSpew(JitSpew_Codegen, "# Emitting wasm export stubs");

  Maybe<ImmPtr> noAbsolute;
  for (size_t i = 0; i < exports.length(); i++) {
    const FuncExport& fe = exports[i];
    const FuncType& funcType = codeMeta.getFuncType(fe.funcIndex());
    if (!fe.hasEagerStubs()) {
      continue;
    }
    if (!GenerateEntryStubs(masm, i, fe, funcType, noAbsolute,
                            codeMeta.isAsmJS(), &code->codeRanges)) {
      return false;
    }
  }

  masm.finish();
  if (masm.oom()) {
    return false;
  }

  return code->swap(masm);
}

bool wasm::GenerateEntryStubs(MacroAssembler& masm, size_t funcExportIndex,
                              const FuncExport& fe, const FuncType& funcType,
                              const Maybe<ImmPtr>& callee, bool isAsmJS,
                              CodeRangeVector* codeRanges) {
  MOZ_ASSERT(!callee == fe.hasEagerStubs());
  MOZ_ASSERT_IF(isAsmJS, fe.hasEagerStubs());

  Offsets offsets;
  if (!GenerateInterpEntry(masm, fe, funcType, callee, &offsets)) {
    return false;
  }
  if (!codeRanges->emplaceBack(CodeRange::InterpEntry, fe.funcIndex(),
                               offsets)) {
    return false;
  }

  if (isAsmJS || !funcType.canHaveJitEntry()) {
    return true;
  }

  CallableOffsets jitOffsets;
  if (!GenerateJitEntry(masm, funcExportIndex, fe, funcType, callee,
                        &jitOffsets)) {
    return false;
  }
  return codeRanges->emplaceBack(CodeRange::JitEntry, fe.funcIndex(),
                                 jitOffsets);
}

bool wasm::GenerateProvisionalLazyJitEntryStub(MacroAssembler& masm,
                                               Offsets* offsets) {
  AssertExpectedSP(masm);
  masm.setFramePushed(0);
  offsets->begin = masm.currentOffset();

#ifdef JS_CODEGEN_ARM64
  // Unaligned ABI calls require SP+PSP, but our mode here is SP-only
  masm.SetStackPointer64(PseudoStackPointer64);
  masm.Mov(PseudoStackPointer64, sp);
#endif

#ifdef JS_USE_LINK_REGISTER
  masm.pushReturnAddress();
#endif

  AllocatableGeneralRegisterSet regs(GeneralRegisterSet::Volatile());
  Register temp = regs.takeAny();

  using Fn = void* (*)();
  masm.setupUnalignedABICall(temp);
  masm.callWithABI<Fn, GetContextSensitiveInterpreterStub>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

#ifdef JS_USE_LINK_REGISTER
  masm.popReturnAddress();
#endif

  masm.jump(ReturnReg);

#ifdef JS_CODEGEN_ARM64
  // Undo the SP+PSP mode
  masm.SetStackPointer64(sp);
#endif

  return FinishOffsets(masm, offsets);
}

bool wasm::GenerateStubs(const CodeMetadata& codeMeta,
                         const FuncImportVector& imports,
                         const FuncExportVector& exports, CompiledCode* code) {
  LifoAlloc lifo(STUBS_LIFO_DEFAULT_CHUNK_SIZE, js::MallocArena);
  TempAllocator alloc(&lifo);
  JitContext jcx;
  WasmMacroAssembler masm(alloc);
  AutoCreatedBy acb(masm, "wasm::GenerateStubs");

  // Swap in already-allocated empty vectors to avoid malloc/free.
  if (!code->swap(masm)) {
    return false;
  }

  Label throwLabel;

  JitSpew(JitSpew_Codegen, "# Emitting wasm import stubs");

  for (uint32_t funcIndex = 0; funcIndex < imports.length(); funcIndex++) {
    const FuncImport& fi = imports[funcIndex];
    const FuncType& funcType = codeMeta.getFuncType(funcIndex);

    CallIndirectId callIndirectId =
        CallIndirectId::forFunc(codeMeta, funcIndex);

    FuncOffsets wrapperOffsets;
    if (!GenerateImportFunction(
            masm, codeMeta.offsetOfFuncImportInstanceData(funcIndex), funcType,
            callIndirectId, &wrapperOffsets, &code->stackMaps)) {
      return false;
    }
    if (!code->codeRanges.emplaceBack(funcIndex, wrapperOffsets,
                                      /* hasUnwindInfo = */ false)) {
      return false;
    }

    CallableOffsets interpOffsets;
    if (!GenerateImportInterpExit(masm, fi, funcType, funcIndex, &throwLabel,
                                  &interpOffsets)) {
      return false;
    }
    if (!code->codeRanges.emplaceBack(CodeRange::ImportInterpExit, funcIndex,
                                      interpOffsets)) {
      return false;
    }

    // Skip if the function does not have a signature that allows for a JIT
    // exit.
    if (!funcType.canHaveJitExit()) {
      continue;
    }

    ImportOffsets jitOffsets;
    if (!GenerateImportJitExit(
            masm, codeMeta.offsetOfFuncImportInstanceData(funcIndex), funcType,
            funcIndex, interpOffsets.begin, &throwLabel, &jitOffsets)) {
      return false;
    }
    if (!code->codeRanges.emplaceBack(CodeRange::ImportJitExit, funcIndex,
                                      jitOffsets)) {
      return false;
    }
  }

  JitSpew(JitSpew_Codegen, "# Emitting wasm entry stubs");

  Maybe<ImmPtr> noAbsolute;
  for (size_t i = 0; i < exports.length(); i++) {
    const FuncExport& fe = exports[i];
    const FuncType& funcType = codeMeta.getFuncType(fe.funcIndex());
    if (!fe.hasEagerStubs()) {
      continue;
    }
    if (!GenerateEntryStubs(masm, i, fe, funcType, noAbsolute,
                            codeMeta.isAsmJS(), &code->codeRanges)) {
      return false;
    }
  }

  JitSpew(JitSpew_Codegen, "# Emitting wasm trap, debug and throw stubs");

  Offsets offsets;

  if (!GenerateTrapExit(masm, &throwLabel, &offsets)) {
    return false;
  }
  if (!code->codeRanges.emplaceBack(CodeRange::TrapExit, offsets)) {
    return false;
  }

#ifdef ENABLE_WASM_JSPI
  if (codeMeta.stackSwitchingEnabled()) {
    if (!GenerateContBaseFrameStub(masm, &offsets) ||
        !code->codeRanges.emplaceBack(CodeRange::ContBaseFrame, offsets)) {
      return false;
    }
  }
#endif

  CallableOffsets callableOffsets;
  if (!GenerateDebugStub(masm, &throwLabel, &callableOffsets)) {
    return false;
  }
  if (!code->codeRanges.emplaceBack(CodeRange::DebugStub, callableOffsets)) {
    return false;
  }

  if (!GenerateRequestTierUpStub(masm, &callableOffsets)) {
    return false;
  }
  if (!code->codeRanges.emplaceBack(CodeRange::RequestTierUpStub,
                                    callableOffsets)) {
    return false;
  }

  if (!GenerateUpdateCallRefMetricsStub(masm, &callableOffsets)) {
    return false;
  }
  if (!code->codeRanges.emplaceBack(CodeRange::UpdateCallRefMetricsStub,
                                    callableOffsets)) {
    return false;
  }

  if (!GenerateThrowStub(masm, &throwLabel, &offsets)) {
    return false;
  }
  if (!code->codeRanges.emplaceBack(CodeRange::Throw, offsets)) {
    return false;
  }

  masm.finish();
  if (masm.oom()) {
    return false;
  }

  return code->swap(masm);
}
