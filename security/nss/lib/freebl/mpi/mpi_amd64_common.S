# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


# ------------------------------------------------------------------------
#
#  Implementation of s_mpv_mul_set_vec which exploits
#  the 64X64->128 bit  unsigned multiply instruction.
#
# ------------------------------------------------------------------------

# r = a * digit, r and a are vectors of length len
# returns the carry digit
# r and a are 64 bit aligned.
#
# uint64_t
# s_mpv_mul_set_vec64(uint64_t *r, uint64_t *a, int len, uint64_t digit)
#

.text; .balign 16; .globl s_mpv_mul_set_vec64;

#ifdef DARWIN
#define s_mpv_mul_set_vec64		_s_mpv_mul_set_vec64
.private_extern s_mpv_mul_set_vec64
s_mpv_mul_set_vec64:
#else
.type s_mpv_mul_set_vec64, @function; s_mpv_mul_set_vec64:
#endif

	xorq	%rax, %rax		# if (len == 0) return (0)
	testq	%rdx, %rdx
	jz	.L17

	movq	%rdx, %r8		# Use r8 for len; %rdx is used by mul
	xorq	%r9, %r9		# cy = 0

.L15:
	cmpq	$8, %r8			# 8 - len
	jb	.L16
	movq	0(%rsi), %rax		# rax = a[0]
	movq	8(%rsi), %r11		# prefetch a[1]
	mulq	%rcx			# p = a[0] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 0(%rdi)		# r[0] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	16(%rsi), %r11		# prefetch a[2]
	mulq	%rcx			# p = a[1] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 8(%rdi)		# r[1] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	24(%rsi), %r11		# prefetch a[3]
	mulq	%rcx			# p = a[2] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 16(%rdi)		# r[2] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	32(%rsi), %r11		# prefetch a[4]
	mulq	%rcx			# p = a[3] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 24(%rdi)		# r[3] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	40(%rsi), %r11		# prefetch a[5]
	mulq	%rcx			# p = a[4] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 32(%rdi)		# r[4] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	48(%rsi), %r11		# prefetch a[6]
	mulq	%rcx			# p = a[5] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 40(%rdi)		# r[5] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	56(%rsi), %r11		# prefetch a[7]
	mulq	%rcx			# p = a[6] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 48(%rdi)		# r[6] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	mulq	%rcx			# p = a[7] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 56(%rdi)		# r[7] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	addq	$64, %rsi
	addq	$64, %rdi
	subq	$8, %r8

	jz	.L17
	jmp	.L15

.L16:
	movq	0(%rsi), %rax
	mulq	%rcx			# p = a[0] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 0(%rdi)		# r[0] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	8(%rsi), %rax
	mulq	%rcx			# p = a[1] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 8(%rdi)		# r[1] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	16(%rsi), %rax
	mulq	%rcx			# p = a[2] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 16(%rdi)		# r[2] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	24(%rsi), %rax
	mulq	%rcx			# p = a[3] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 24(%rdi)		# r[3] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	32(%rsi), %rax
	mulq	%rcx			# p = a[4] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 32(%rdi)		# r[4] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	40(%rsi), %rax
	mulq	%rcx			# p = a[5] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 40(%rdi)		# r[5] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17

	movq	48(%rsi), %rax
	mulq	%rcx			# p = a[6] * digit
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 48(%rdi)		# r[6] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L17


.L17:
	movq	%r9, %rax
	ret

#ifndef DARWIN
.size s_mpv_mul_set_vec64, .-s_mpv_mul_set_vec64
#endif

# ------------------------------------------------------------------------
#
#  Implementation of s_mpv_mul_add_vec which exploits
#  the 64X64->128 bit  unsigned multiply instruction.
#
# ------------------------------------------------------------------------

# r += a * digit, r and a are vectors of length len
# returns the carry digit
# r and a are 64 bit aligned.
#
# uint64_t
# s_mpv_mul_add_vec64(uint64_t *r, uint64_t *a, int len, uint64_t digit)
#

.text; .balign 16; .globl s_mpv_mul_add_vec64;

#ifdef DARWIN
#define s_mpv_mul_add_vec64      _s_mpv_mul_add_vec64
.private_extern s_mpv_mul_add_vec64
s_mpv_mul_add_vec64:
#else
.type s_mpv_mul_add_vec64, @function; s_mpv_mul_add_vec64:
#endif

	xorq	%rax, %rax		# if (len == 0) return (0)
	testq	%rdx, %rdx
	jz	.L27

	movq	%rdx, %r8		# Use r8 for len; %rdx is used by mul
	xorq	%r9, %r9		# cy = 0

.L25:
	cmpq	$8, %r8			# 8 - len
	jb	.L26
	movq	0(%rsi), %rax		# rax = a[0]
	movq	0(%rdi), %r10		# r10 = r[0]
	movq	8(%rsi), %r11		# prefetch a[1]
	mulq	%rcx			# p = a[0] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[0]
	movq	8(%rdi), %r10		# prefetch r[1]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 0(%rdi)		# r[0] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	16(%rsi), %r11		# prefetch a[2]
	mulq	%rcx			# p = a[1] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[1]
	movq	16(%rdi), %r10		# prefetch r[2]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 8(%rdi)		# r[1] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	24(%rsi), %r11		# prefetch a[3]
	mulq	%rcx			# p = a[2] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[2]
	movq	24(%rdi), %r10		# prefetch r[3]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 16(%rdi)		# r[2] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	32(%rsi), %r11		# prefetch a[4]
	mulq	%rcx			# p = a[3] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[3]
	movq	32(%rdi), %r10		# prefetch r[4]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 24(%rdi)		# r[3] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	40(%rsi), %r11		# prefetch a[5]
	mulq	%rcx			# p = a[4] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[4]
	movq	40(%rdi), %r10		# prefetch r[5]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 32(%rdi)		# r[4] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	48(%rsi), %r11		# prefetch a[6]
	mulq	%rcx			# p = a[5] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[5]
	movq	48(%rdi), %r10		# prefetch r[6]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 40(%rdi)		# r[5] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	movq	56(%rsi), %r11		# prefetch a[7]
	mulq	%rcx			# p = a[6] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[6]
	movq	56(%rdi), %r10		# prefetch r[7]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 48(%rdi)		# r[6] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	movq	%r11, %rax
	mulq	%rcx			# p = a[7] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[7]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 56(%rdi)		# r[7] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)

	addq	$64, %rsi
	addq	$64, %rdi
	subq	$8, %r8

	jz	.L27
	jmp	.L25

.L26:
	movq	0(%rsi), %rax
	movq	0(%rdi), %r10
	mulq	%rcx			# p = a[0] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[0]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 0(%rdi)		# r[0] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	8(%rsi), %rax
	movq	8(%rdi), %r10
	mulq	%rcx			# p = a[1] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[1]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 8(%rdi)		# r[1] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	16(%rsi), %rax
	movq	16(%rdi), %r10
	mulq	%rcx			# p = a[2] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[2]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 16(%rdi)		# r[2] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	24(%rsi), %rax
	movq	24(%rdi), %r10
	mulq	%rcx			# p = a[3] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[3]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 24(%rdi)		# r[3] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	32(%rsi), %rax
	movq	32(%rdi), %r10
	mulq	%rcx			# p = a[4] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[4]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 32(%rdi)		# r[4] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	40(%rsi), %rax
	movq	40(%rdi), %r10
	mulq	%rcx			# p = a[5] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[5]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 40(%rdi)		# r[5] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27

	movq	48(%rsi), %rax
	movq	48(%rdi), %r10
	mulq	%rcx			# p = a[6] * digit
	addq	%r10, %rax
	adcq	$0, %rdx		# p += r[6]
	addq	%r9, %rax
	adcq	$0, %rdx		# p += cy
	movq	%rax, 48(%rdi)		# r[6] = lo(p)
	movq	%rdx, %r9		# cy = hi(p)
	decq	%r8
	jz	.L27


.L27:
	movq	%r9, %rax
	ret

#ifndef DARWIN
.size s_mpv_mul_add_vec64, .-s_mpv_mul_add_vec64

# Magic indicating no need for an executable stack
.section .note.GNU-stack, "", @progbits
.previous
#endif
