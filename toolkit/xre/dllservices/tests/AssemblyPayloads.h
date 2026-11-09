/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* These assembly functions represent patterns that were already hooked by
 * another application before our detour.
 */

#ifndef mozilla_AssemblyPayloads_h
#define mozilla_AssemblyPayloads_h

#include "mozilla/Attributes.h"

#include <cstdint>

#define PADDING_256_NOP                                              \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;" \
  "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"

extern "C" {

#if defined(__clang__)
#  if defined(_M_X64)
constexpr uintptr_t JumpDestination = 0x7fff00000000;

__declspec(dllexport) MOZ_NAKED void MovPushRet() {
  asm volatile(
      "mov %0, %%rax;"
      "push %%rax;"
      "ret;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void MovRaxJump() {
  asm volatile(
      "mov %0, %%rax;"
      "jmpq *%%rax;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void DoubleJump() {
  asm volatile(
      "jmp label1;"

      "label2:"
      "mov %0, %%rax;"
      "jmpq *%%rax;"

      // 0x100 bytes padding to generate jmp rel32 instead of jmp rel8
      PADDING_256_NOP

      "label1:"
      "jmp label2;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void NearJump() {
  asm volatile(
      "jae label3;"
      "je  label3;"
      "jne label3;"
      // This is past the 13 bytes that we patch (each instruction
      // above is 6 bytes long)), so any instructions after
      // this won't be patched.

      "label4:"
      "mov %0, %%rax;"
      "jmpq *%%rax;"

      // 0x100 bytes padding to generate jae rel32 instead of jae rel8
      PADDING_256_NOP

      "label3:"
      "jmp label4;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void NearJump2() {
  asm volatile(
      "js  label5;"
      // Above instruction takes up 6 bytes of the 13 bytes
      // that we patch.

      "label6:"
      "mov %0, %%rax;"
      "jmpq *%%rax;"

      // 0x100 bytes padding to generate js rel32 instead of js rel8
      PADDING_256_NOP

      "label5:"
      "jmp label6;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void JumpWith8BitOffset() {
  asm volatile(
      "jae label7;"
      "je  label7;"
      "jne label7;"
      "js  label7;"
      // Above instructions take up 8 bytes (each one is 2 bytes long)
      // of the 13 bytes that we patch.

      "label8:"
      "mov %0, %%rax;"
      "jmpq *%%rax;"

      "label7:"
      "jmp label8;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void OpcodeFF() {
  // Skip PUSH (FF /6) because clang prefers Opcode 50+rd
  // to translate PUSH r64 rather than Opcode FF.
  asm volatile(
      "incl %eax;"
      "decl %ebx;"
      "call *%rcx;"
      "jmp *(%rip);"  // Indirect jump to 0xcccccccc`cccccccc
      "int $3;int $3;int $3;int $3;"
      "int $3;int $3;int $3;int $3;");
}

__declspec(dllexport) MOZ_NAKED void IndirectCall() {
  asm volatile(
      "call *(%rip);"  // Indirect call to 0x90909090`90909090
      "nop;nop;nop;nop;nop;nop;nop;nop;"
      "ret;");
}

__declspec(dllexport) MOZ_NAKED void MovImm64() {
  asm volatile(
      "mov $0x1234567812345678, %r10;"
      "nop;nop;nop");
}

__declspec(dllexport) MOZ_NAKED void AndWithSib() {
  asm volatile(
      "andl $15, (%%rax,%%rax,1);"  // AND r/m32, imm32;
      "nop;nop;nop;nop;nop;nop;nop;nop;nop;"
      :
      :
      : "memory", "cc");
}

__declspec(dllexport) MOZ_NAKED void AndWithoutSib() {
  asm volatile(
      "andl $16, (%%rax);"  // AND r/m32, imm32;
      "nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;"
      :
      :
      : "memory", "cc");
}

__declspec(dllexport) MOZ_NAKED void RexAndWithSib() {
  asm volatile(
      "andq $17, (%%r10,%%rcx,1);"  // AND r/m64, imm8;
      "nop;nop;nop;nop;nop;nop;nop;nop;"
      :
      :
      : "memory", "cc");
}

__declspec(dllexport) MOZ_NAKED void RexAndWithoutSib() {
  asm volatile(
      "andq $18, (%%r10);"  // AND r/m64, imm8
      "nop;nop;nop;nop;nop;nop;nop;nop;nop;"
      :
      :
      : "memory", "cc");
}

static unsigned char __attribute__((used)) gGlobalValue = 0;

__declspec(dllexport) MOZ_NAKED void RexCmpRipRelativeBytePtr() {
  asm volatile(
      "cmpb %sil, gGlobalValue(%rip);"
      "nop;nop;nop;nop;nop;nop;nop;nop;");
}

__declspec(dllexport) MOZ_NAKED void JmpInsideEarlyBytes() {
  asm volatile(
      "js label9;"
      "label10:"
      "jmpq *%%rax;"

      // "label9" is within the first 13 bytes
      "label9:"
      "mov %0, %%rax;"
      "jmp label10;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void CallInsideEarlyBytes() {
  asm volatile(
      "call label11;"
      "label12:"
      "jmpq *%%rax;"

      // "label11" is within the first 13 bytes
      "label11:"
      "mov %0, %%rax;"
      "jmp label12;"
      :
      : "i"(JumpDestination));
}

// A valid function that uses "cmp byte ptr [rip + offset], sil". It returns
// true if and only if gGlobalValue is equal to aValue.
__declspec(dllexport) MOZ_NAKED bool IsEqualToGlobalValue(
    unsigned char aValue) {
  asm volatile(
      "xorl %eax, %eax;"
      "pushq %rsi;"
      "pushq %rcx;"
      "popq %rsi;"
      "cmpb %sil, gGlobalValue(%rip);"
      "nop;"
      // end of 13 first bytes
      "movq $1, %rsi;"
      "cmoveq %rsi, %rax;"
      "popq %rsi;"
      "retq;");
}

// The following functions load kRipRelativeValue into a register through a
// RIP-relative instruction located within the first 13 bytes, and return the
// value of that register. They also XOR the result with the argument registers
// other than the destination register, so that they return kRipRelativeValue
// only if these registers are preserved when called with zero arguments.
constexpr uint64_t kRipRelativeValue = 0x0123456789abcdefULL;
static uint64_t __attribute__((used)) gRipRelativeValue = kRipRelativeValue;

// 48 8b 05 XX XX XX XX    mov rax, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadRaxRipRelative(uint64_t, uint64_t,
                                                            uint64_t,
                                                            uint64_t) {
  asm volatile(
      "movq gRipRelativeValue(%rip), %rax;"
      "nop;nop;nop;nop;nop;nop;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 4c 8b 15 XX XX XX XX    mov r10, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadR10RipRelative(uint64_t, uint64_t,
                                                            uint64_t,
                                                            uint64_t) {
  asm volatile(
      "movq gRipRelativeValue(%rip), %r10;"
      "nop;nop;nop;nop;nop;nop;"
      "movq %r10, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 4c 8b 05 XX XX XX XX    mov r8, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadR8RipRelative(uint64_t, uint64_t,
                                                           uint64_t, uint64_t) {
  asm volatile(
      "movq gRipRelativeValue(%rip), %r8;"
      "nop;nop;nop;nop;nop;nop;"
      "movq %r8, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 4d 8b 1d XX XX XX XX    mov r11, qword ptr [rip + offset gRipRelativeValue]
// (REX.WRB, where REX.B is ignored)
__declspec(dllexport) MOZ_NAKED uint64_t LoadR11RexWRBRipRelative(uint64_t,
                                                                  uint64_t,
                                                                  uint64_t,
                                                                  uint64_t) {
  asm volatile(
      ".byte 0x4d;"
      "movl gRipRelativeValue(%rip), %ebx;"
      "nop;nop;nop;nop;nop;nop;"
      "movq %r11, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 49 8b 15 XX XX XX XX    mov rdx, qword ptr [rip + offset gRipRelativeValue]
// (REX.WB, where REX.B is ignored)
__declspec(dllexport) MOZ_NAKED uint64_t LoadRdxRexWBRipRelative(uint64_t,
                                                                 uint64_t,
                                                                 uint64_t,
                                                                 uint64_t) {
  asm volatile(
      ".byte 0x49;"
      "movl gRipRelativeValue(%rip), %edx;"
      "nop;nop;nop;nop;nop;nop;"
      "movq %rdx, %rax;"
      "xorq %rcx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 41 54                   push r12
// 4c 8b 25 XX XX XX XX    mov r12, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadR12RipRelative(uint64_t, uint64_t,
                                                            uint64_t,
                                                            uint64_t) {
  asm volatile(
      "pushq %r12;"
      "movq gRipRelativeValue(%rip), %r12;"
      "nop;nop;nop;nop;"
      "movq %r12, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "popq %r12;"
      "retq;");
}

// 41 55                   push r13
// 4c 8b 2d XX XX XX XX    mov r13, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadR13RipRelative(uint64_t, uint64_t,
                                                            uint64_t,
                                                            uint64_t) {
  asm volatile(
      "pushq %r13;"
      "movq gRipRelativeValue(%rip), %r13;"
      "nop;nop;nop;nop;"
      "movq %r13, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "popq %r13;"
      "retq;");
}

// 55                      push rbp
// 48 8b 2d XX XX XX XX    mov rbp, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LoadRbpRipRelative(uint64_t, uint64_t,
                                                            uint64_t,
                                                            uint64_t) {
  asm volatile(
      "pushq %rbp;"
      "movq gRipRelativeValue(%rip), %rbp;"
      "nop;nop;nop;nop;nop;"
      "movq %rbp, %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "popq %rbp;"
      "retq;");
}

// 4c 8d 05 XX XX XX XX    lea r8, [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED uint64_t LeaR8RipRelative(uint64_t, uint64_t,
                                                          uint64_t, uint64_t) {
  asm volatile(
      "leaq gRipRelativeValue(%rip), %r8;"
      "nop;nop;nop;nop;nop;nop;"
      "movq (%r8), %rax;"
      "xorq %rcx, %rax;"
      "xorq %rdx, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

// 49 8d 15 XX XX XX XX    lea rdx, [rip + offset gRipRelativeValue]
// (REX.WB, where REX.B is ignored)
__declspec(dllexport) MOZ_NAKED uint64_t LeaRdxRexWBRipRelative(uint64_t,
                                                                uint64_t,
                                                                uint64_t,
                                                                uint64_t) {
  asm volatile(
      ".byte 0x49;"
      "leal gRipRelativeValue(%rip), %edx;"
      "nop;nop;nop;nop;nop;nop;"
      "movq (%rdx), %rax;"
      "xorq %rcx, %rax;"
      "xorq %r8, %rax;"
      "xorq %r9, %rax;"
      "retq;");
}

__declspec(dllexport) MOZ_NAKED uint64_t RexWIndirectJumpTarget() {
  asm volatile(
      "movabsq %0, %%rax;"
      "retq;"
      :
      : "i"(kRipRelativeValue));
}

// 48 ff 25 00 00 00 00    jmp qword ptr [rip + 0] (REX.W)
// followed by the address of RexWIndirectJumpTarget.
__declspec(dllexport) MOZ_NAKED uint64_t RexWIndirectJump() {
  asm volatile(
      "nop;nop;nop;nop;nop;nop;"
      ".byte 0x48;"
      "jmpq *0(%rip);"
      ".quad RexWIndirectJumpTarget;");
}

// 48 ff 15 00 20 00 00    call qword ptr [rip + 0x2000] (REX.W)
// The second byte of the displacement looks like the ModR/M byte of a jmp, and
// the code after the call distinguishes a call from a jmp.
__declspec(dllexport) MOZ_NAKED uint64_t RexWIndirectCall() {
  asm volatile(
      "nop;nop;nop;nop;nop;nop;"
      ".byte 0x48;"
      "callq *0x2000(%rip);"
      "notq %rax;"
      "retq;"
      ".fill 0x2000 - 4, 1, 0xcc;"
      ".quad RexWIndirectJumpTarget;");
}

// 31 c0                   xor eax, eax
// 33 41 08                xor eax, dword ptr [rcx + 8]
__declspec(dllexport) MOZ_NAKED uint64_t XorWithMemoryOperand(uint64_t*) {
  asm volatile(
      "xorl %eax, %eax;"
      "xorl 8(%rcx), %eax;"
      "nop;nop;nop;nop;nop;nop;nop;nop;"
      "retq;");
}

// 31 c0                   xor eax, eax
// 45 33 41 08             xor r8d, dword ptr [r9 + 8]
__declspec(dllexport) MOZ_NAKED uint64_t XorRexRBWithMemoryOperand(uint64_t,
                                                                   uint64_t,
                                                                   uint64_t,
                                                                   uint64_t*) {
  asm volatile(
      "xorl %eax, %eax;"
      "xorl 8(%r9), %r8d;"
      "nop;nop;nop;nop;nop;nop;nop;"
      "movl %r8d, %eax;"
      "retq;");
}

// The byte before the compared one differs from the immediate, so that the
// function returns true only if the RIP-relative address accounts for the
// imm8 that follows the displacement.
static unsigned char __attribute__((used)) gCmpImm8Values[] = {0x00, 0xcc};

// 31 c0                   xor eax, eax
// 80 3d XX XX XX XX cc    cmp byte ptr [rip + offset gCmpImm8Values + 1], 0xcc
__declspec(dllexport) MOZ_NAKED bool CmpImm8RipRelativeBytePtr() {
  asm volatile(
      "xorl %eax, %eax;"
      "cmpb $0xcc, gCmpImm8Values+1(%rip);"
      "nop;nop;nop;nop;"
      "sete %al;"
      "retq;");
}

// We don't support detouring the following functions.

// 4c 89 15 XX XX XX XX    mov qword ptr [rip + offset gRipRelativeValue], r10
__declspec(dllexport) MOZ_NAKED void StoreR10RipRelative() {
  asm volatile(
      "movq %r10, gRipRelativeValue(%rip);"
      "nop;nop;nop;nop;nop;nop;"
      "retq;");
}

// 48 8b 25 XX XX XX XX    mov rsp, qword ptr [rip + offset gRipRelativeValue]
__declspec(dllexport) MOZ_NAKED void LoadRspRipRelative() {
  asm volatile(
      "movq gRipRelativeValue(%rip), %rsp;"
      "nop;nop;nop;nop;nop;nop;"
      "retq;");
}

// 40 38 25 XX XX XX XX    cmp byte ptr [rip + offset gGlobalValue], spl
__declspec(dllexport) MOZ_NAKED void RexCmpRipRelativeSpl() {
  asm volatile(
      "cmpb %spl, gGlobalValue(%rip);"
      "nop;nop;nop;nop;nop;nop;"
      "retq;");
}

// This code reproduces bug 1798787: it uses the same prologue, the same unwind
// info, and it has a call instruction that starts within the 13 first bytes.
MOZ_NAKED void DetouredCallCode(uintptr_t aCallee) {
  asm volatile(
      "subq $0x28, %rsp;"
      "testq %rcx, %rcx;"
      "jz exit;"
      "callq *%rcx;"
      // Add padding here so we're not trying to jump within the
      // first 13 bytes of the function
      "nop;nop;nop;nop;nop;nop;nop;nop;"
      "exit:"
      "addq $0x28, %rsp;"
      "retq;");
}
constexpr uint8_t gDetouredCallCodeSize = 24;  // size of function in bytes
alignas(uint32_t) uint8_t gDetouredCallUnwindInfo[] = {
    0x01,  // Version (1), Flags (0)
    0x04,  // SizeOfProlog (4)
    0x01,  // CountOfUnwindCodes (1)
    0x00,  // FrameRegister (0), FrameOffset (0)
    // UnwindCodes[0]
    0x04,  // .OffsetInProlog (4)
    0x42,  // .UnwindOpCode(UWOP_ALLOC_SMALL=2), .UnwindInfo (4)
};

// This points to the same code as DetouredCallCode, but dynamically generated
// so that it can have custom unwinding info. See TestDllInterceptor.cpp.
extern decltype(&DetouredCallCode) gDetouredCall;

// This is just a jumper: our hooking code will thus detour the jump target
// -- it will not detour DetouredCallJumper. We need to do this to point our
// hooking code to the dynamic code, because our hooking API works with an
// exported function name.
MOZ_NAKED __declspec(dllexport noinline) void DetouredCallJumper(
    uintptr_t aCallee) {
  // Ideally we would want this to be:
  //   jmp qword ptr [rip + offset gDetouredCall]
  // Unfortunately, it is unclear how to do that with inline assembly, so we
  // use a zero offset and patch it before the test.
  asm volatile("jmpq *0(%rip)");
}

#  elif defined(_M_IX86)
constexpr uintptr_t JumpDestination = 0x7fff0000;

__declspec(dllexport) MOZ_NAKED void PushRet() {
  asm volatile(
      "push %0;"
      "ret;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void MovEaxJump() {
  asm volatile(
      "mov %0, %%eax;"
      "jmp *%%eax;"
      :
      : "i"(JumpDestination));
}

__declspec(dllexport) MOZ_NAKED void Opcode83() {
  asm volatile(
      "xor $0x42, %eax;"
      "cmpl $1, 0xc(%ebp);");
}

__declspec(dllexport) MOZ_NAKED void LockPrefix() {
  // Test an instruction with a LOCK prefix (0xf0) at a non-zero offset
  asm volatile(
      "push $0x7c;"
      "lock push $0x7c;");
}

__declspec(dllexport) MOZ_NAKED void LooksLikeLockPrefix() {
  // This is for a regression scenario of bug 1625452, where we double-counted
  // the offset in CountPrefixBytes.  When we count prefix bytes in front of
  // the 2nd PUSH located at offset 2, we mistakenly started counting from
  // the byte 0xf0 at offset 4, which is considered as LOCK, thus we try to
  // detour the next byte 0xcc and it fails.
  //
  // 0: 6a7c       push 7Ch
  // 2: 68ccf00000 push 0F0CCh
  //
  asm volatile(
      "push $0x7c;"
      "push $0x0000f0cc;");
}

__declspec(dllexport) MOZ_NAKED void DoubleJump() {
  asm volatile(
      "jmp label1;"

      "label2:"
      "mov %0, %%eax;"
      "jmp *%%eax;"

      // 0x100 bytes padding to generate jmp rel32 instead of jmp rel8
      PADDING_256_NOP

      "label1:"
      "jmp label2;"
      :
      : "i"(JumpDestination));
}
#  endif

#  if !defined(_M_ARM64)
__declspec(dllexport) MOZ_NAKED void UnsupportedOp() {
  asm volatile(
      "ud2;"
      "nop;nop;nop;nop;nop;nop;nop;nop;"
      "nop;nop;nop;nop;nop;nop;nop;nop;");
}

// bug 1816936
// Make sure no instruction ends at 5 (for x86) or 13 (for x64) bytes
__declspec(dllexport) MOZ_NAKED void SpareBytesAfterDetour() {
  asm volatile(
      "incl %eax;"                // 2 bytes on x64, 1 byte on x86
      "mov $0x01234567, %eax;"    // 5 bytes
      "mov $0xfedcba98, %eax;"    // 5 bytes
      "mov $0x01234567, %eax;"    // 5 bytes
      "mov $0xfedcba98, %eax;");  // 5 bytes
}

// bug 1816936
// Make sure no instruction ends at 10 (for x64) bytes
// This is slightly different than SpareBytesAfterDetour so the compiler doesn't
// combine them, which would make the test that detours this one behave
// unexpectedly since it is already detoured.
__declspec(dllexport) MOZ_NAKED void SpareBytesAfterDetourFor10BytePatch() {
  asm volatile(
      "incl %eax;"                // 2 bytes
      "mov $0x01234567, %ecx;"    // 5 bytes
      "mov $0xfedcba98, %ebx;"    // 5 bytes
      "mov $0x01234567, %eax;"    // 5 bytes
      "mov $0xfedcba98, %edx;");  // 5 bytes
}
#  endif  // !defined(_M_ARM64)

#endif  // defined(__clang__)

}  // extern "C"

#endif  // mozilla_AssemblyPayloads_h
