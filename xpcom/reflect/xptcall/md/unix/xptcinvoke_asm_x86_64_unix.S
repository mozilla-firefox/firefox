# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#if defined(__ELF__)
#include <cet.h>
#endif

# Darwin gives a leading '_' to symbols defined in C code.
#ifdef XP_DARWIN
#define SYM(x) _ ## x
#else
#define SYM(x) x
#endif

#define CFI_STARTPROC .cfi_startproc
#define CFI_ENDPROC .cfi_endproc
#define CFI_DEF_CFA_OFFSET(offset) .cfi_def_cfa_offset offset
#define CFI_OFFSET(reg, offset) .cfi_offset reg, offset
#define CFI_DEF_CFA_REGISTER(reg) .cfi_def_cfa_register reg
#define CFI_DEF_CFA(reg, offset) .cfi_def_cfa reg, offset

.intel_syntax noprefix

# nsresult NS_InvokeByIndex(nsISupports* this, uint32_t aVtableIndex,
#                           uint32_t argc, nsXPTCVariant* argv);
.text
.global SYM(NS_InvokeByIndex)
#ifndef XP_DARWIN
.type NS_InvokeByIndex, @function
#endif
.align 4
SYM(NS_InvokeByIndex):
  CFI_STARTPROC
  push rbp
  CFI_DEF_CFA_OFFSET(16)
  CFI_OFFSET(6, -16)
  mov rbp, rsp
  CFI_DEF_CFA_REGISTER(6)

# save r12 and r13 because we use them and they are callee saved.
  push r12
  push r13
  CFI_OFFSET(12, -24)
  CFI_OFFSET(13, -32)

# save this and the vtable index because we need them after setting up the
# stack.
  mov r12, rdi
  mov r13, rsi

# allocate space for stack arguments, in theory we only need 8 * (argc - 5)
# bytes because at least 5 arguments will go in registers, but for now it is
# just simpler to allocate 8 * argc bytes.  Note that we treat the this
# pointer specially.
  lea eax, [edx * 8]
  sub rsp, rax

# If there is an odd number of args the stack can be misaligned so realign it.
  and rsp, 0xfffffffffffffff0

# pass the stack slot area to InvokeCopyToStack.
  mov r8, rsp

# setup space for the register slots: there are 5 integer ones and 8 floating
# point ones.  So we need 104 bytes of space, but we allocate 112 to keep rsp
# aligned to 16 bytes.
  sub rsp, 112

# the first argument to InvokeCopyToStack is the integer register area, and the
# second is the floating point area.
  mov rdi, rsp
  lea rsi, [rsp + 40]

# The 3rd and 4th arguments to InvokeCopyToStack are already in the right
# registers.  So now we can just call InvokeCopyToStack.
  call SYM(InvokeCopyToStack)

# setup this
  mov rdi, r12

# copy the integer arguments into place.
  mov rsi, [rsp]
  mov rdx, [rsp + 8]
  mov rcx, [rsp + 16]
  mov r8, [rsp + 24]
  mov r9, [rsp + 32]

# copy the float arguments into place
  movsd xmm0, [rsp + 40]
  movsd xmm1, [rsp + 48]
  movsd xmm2, [rsp + 56]
  movsd xmm3, [rsp + 64]
  movsd xmm4, [rsp + 72]
  movsd xmm5, [rsp + 80]
  movsd xmm6, [rsp + 88]
  movsd xmm7, [rsp + 96]

# get rid of the scratch space for registers
  add rsp, 112

# load the function pointer and call
  lea eax, [r13d * 8]
  add rax, [rdi]
  call [rax]

# r12 and r13 were pushed relative to the old stack pointer which is now the
# frame pointer.
  mov r12, [rbp - 0x8]
  mov r13, [rbp - 0x10]

  mov rsp, rbp
  pop rbp
  CFI_DEF_CFA(7, 8)
  ret
  CFI_ENDPROC

#ifndef XP_DARWIN
// Magic indicating no need for an executable stack
.section .note.GNU-stack, "", @progbits
#endif
