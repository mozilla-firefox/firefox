/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/testsJit.h"

#include "jit/JitCommon.h"
#include "jit/Linker.h"

#include "jit/MacroAssembler-inl.h"

// On entry to the JIT code, save every register.
void PrepareJit(js::jit::MacroAssembler& masm) {
  using namespace js::jit;
  masm.indirectTarget(TargetKind::Call);
#if defined(JS_CODEGEN_ARM64)
  masm.Mov(PseudoStackPointer64, sp);
  masm.SetStackPointer64(PseudoStackPointer64);
#endif

  AllocatableRegisterSet regs(RegisterSet::All());
  LiveRegisterSet save(regs.asLiveSet());
#if defined(JS_CODEGEN_ARM)
  save.add(js::jit::d15);
#endif
#if defined(JS_CODEGEN_MIPS64) || defined(JS_CODEGEN_LOONG64) || \
    defined(JS_CODEGEN_RISCV64)
  save.add(js::jit::ra);
#elif defined(JS_USE_LINK_REGISTER)
  save.add(js::jit::lr);
#endif
  masm.PushRegsInMask(save);
}

// Generate the exit path of the JIT code, which restores every register. Then,
// make it executable and run it.
bool ExecuteJit(JSContext* cx, js::jit::MacroAssembler& masm) {
  using namespace js::jit;

  AllocatableRegisterSet regs(RegisterSet::All());
  LiveRegisterSet restore(regs.asLiveSet());
#if defined(JS_CODEGEN_ARM)
  restore.add(js::jit::d15);
#endif
#if defined(JS_CODEGEN_MIPS64) || defined(JS_CODEGEN_LOONG64) || \
    defined(JS_CODEGEN_RISCV64)
  restore.add(js::jit::ra);
#elif defined(JS_USE_LINK_REGISTER)
  restore.add(js::jit::lr);
#endif
  masm.PopRegsInMask(restore);

#if defined(JS_CODEGEN_ARM64)
  // Return using the value popped into x30.
  masm.abiret();

  // Reset stack pointer.
  masm.SetStackPointer64(PseudoStackPointer64);
#else
  // Exit the JIT-ed code using the ABI return style.
  masm.abiret();
#endif

  if (masm.oom()) {
    return false;
  }

  JitCode* code = nullptr;
  {
    Linker linker(masm);
    code = linker.newCode(cx, CodeKind::Other);
    if (!code) {
      return false;
    }
  }

  JS::AutoSuppressGCAnalysis suppress;
  EnterTest test = code->as<EnterTest>();

#if defined(JS_CODEGEN_ARM64) && !defined(JS_SIMULATOR_ARM64)
  {
    // On arm64, we need to save/restore x20 -- the PSP -- across the call.
    //
    // This cannot be done
    //
    // * in the generated code, where both SP and PSP are in play; it is too
    //   confusing and difficult
    //
    // * by adding x20 to the `save`/`restore` sets above, since they are
    //   subsequently processed by masm.{Push/Pop}RegsInMask and that refuses
    //   to deal with PSP.
    //
    // So we do it here instead.  This relies on the (unchecked) assumption
    // that CALL_GENERATED_0 (and whatever it calls) does not place a value in
    // x20 that is needed after the call.
    MOZ_RELEASE_ASSERT(PseudoStackPointer64.code() == 20);
    uintptr_t savedX20;
    __asm__ __volatile__("str x20, %0" : : "m"(savedX20) : "cc", "memory");
    CALL_GENERATED_0(test);
    __asm__ __volatile__("ldr x20, %0" : : "m"(savedX20) : "cc", "memory");
  }
#else
  CALL_GENERATED_0(test);
#endif

  return true;
}
