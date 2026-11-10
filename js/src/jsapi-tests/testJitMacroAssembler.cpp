/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/MathAlgorithms.h"
#include "mozilla/Sprintf.h"

#include "jit/IonAnalysis.h"
#include "jit/Linker.h"
#include "jit/MacroAssembler.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"
#include "jit/ValueNumbering.h"
#include "js/Value.h"
#include "jsapi-tests/tests.h"
#include "jsapi-tests/testsJit.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::NegativeInfinity;
using mozilla::PositiveInfinity;

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)

BEGIN_TEST(testJitMacroAssembler_flexibleDivMod) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  // Test case divides 9/2;
  const uintptr_t dividend = 9;
  const uintptr_t divisor = 2;
  const uintptr_t quotient_result = 4;
  const uintptr_t remainder_result = 1;

  AllocatableGeneralRegisterSet leftHandSides(GeneralRegisterSet::All());
  while (!leftHandSides.empty()) {
    Register lhs = leftHandSides.takeAny();

    AllocatableGeneralRegisterSet rightHandSides(GeneralRegisterSet::All());
    while (!rightHandSides.empty()) {
      Register rhs = rightHandSides.takeAny();

      AllocatableGeneralRegisterSet quotients(GeneralRegisterSet::All());
      while (!quotients.empty()) {
        Register quotientOutput = quotients.takeAny();

        AllocatableGeneralRegisterSet remainders(GeneralRegisterSet::All());
        while (!remainders.empty()) {
          Register remainderOutput = remainders.takeAny();

          // lhs and rhs are preserved by |flexibleDivMod32|, so neither can be
          // an output register.
          if (lhs == quotientOutput || lhs == remainderOutput) {
            continue;
          }
          if (rhs == quotientOutput || rhs == remainderOutput) {
            continue;
          }

          // Output registers must be distinct.
          if (quotientOutput == remainderOutput) {
            continue;
          }

          AllocatableRegisterSet regs(RegisterSet::Volatile());
          LiveRegisterSet save(regs.asLiveSet());

          Label next, fail;
          masm.mov(ImmWord(dividend), lhs);
          masm.mov(ImmWord(divisor), rhs);
          masm.flexibleDivMod32(lhs, rhs, quotientOutput, remainderOutput,
                                false, save);
          masm.branchPtr(Assembler::NotEqual, quotientOutput,
                         ImmWord(lhs != rhs ? quotient_result : 1), &fail);
          masm.branchPtr(Assembler::NotEqual, remainderOutput,
                         ImmWord(lhs != rhs ? remainder_result : 0), &fail);
          // Ensure LHS was not clobbered
          masm.branchPtr(Assembler::NotEqual, lhs,
                         ImmWord(lhs != rhs ? dividend : divisor), &fail);
          // Ensure RHS was not clobbered
          masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(divisor), &fail);
          masm.jump(&next);
          masm.bind(&fail);
          masm.printf("Failed\n");

          char buffer[128];
          SprintfLiteral(
              buffer,
              "\tlhs = %s, rhs = %s, quotientOutput = %s, remainderOutput "
              "= %s\n",
              lhs.name(), rhs.name(), quotientOutput.name(),
              remainderOutput.name());

          masm.printf(buffer);
          masm.printf("\tlhs = %d\n", lhs);
          masm.printf("\trhs = %d\n", rhs);
          masm.printf("\tquotientOutput = %d\n", quotientOutput);
          masm.printf("\tremainderOutput = %d\n", remainderOutput);

          masm.breakpoint();

          masm.bind(&next);
        }
      }
    }
  }

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_flexibleDivMod)

BEGIN_TEST(testJitMacroAssembler_flexibleRemainder) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  // Test case divides 9/2;
  const uintptr_t dividend = 9;
  const uintptr_t divisor = 2;
  const uintptr_t remainder_result = 1;

  AllocatableGeneralRegisterSet leftHandSides(GeneralRegisterSet::All());
  while (!leftHandSides.empty()) {
    Register lhs = leftHandSides.takeAny();

    AllocatableGeneralRegisterSet rightHandSides(GeneralRegisterSet::All());
    while (!rightHandSides.empty()) {
      Register rhs = rightHandSides.takeAny();

      AllocatableGeneralRegisterSet outputs(GeneralRegisterSet::All());
      while (!outputs.empty()) {
        Register output = outputs.takeAny();

        AllocatableRegisterSet regs(RegisterSet::Volatile());
        LiveRegisterSet save(regs.asLiveSet());

        Label next, fail;
        masm.mov(ImmWord(dividend), lhs);
        masm.mov(ImmWord(divisor), rhs);
        masm.flexibleRemainder32(lhs, rhs, output, false, save);
        masm.branchPtr(Assembler::NotEqual, output,
                       ImmWord(lhs != rhs ? remainder_result : 0), &fail);
        // Ensure LHS was not clobbered
        if (lhs != output) {
          masm.branchPtr(Assembler::NotEqual, lhs,
                         ImmWord(lhs != rhs ? dividend : divisor), &fail);
        }
        // Ensure RHS was not clobbered
        if (rhs != output) {
          masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(divisor), &fail);
        }
        masm.jump(&next);
        masm.bind(&fail);
        masm.printf("Failed\n");

        char buffer[128];
        SprintfLiteral(buffer, "\tlhs = %s, rhs = %s, output = %s\n",
                       lhs.name(), rhs.name(), output.name());

        masm.printf(buffer);
        masm.printf("\tlhs = %d\n", lhs);
        masm.printf("\trhs = %d\n", rhs);
        masm.printf("\toutput = %d\n", output);

        masm.breakpoint();

        masm.bind(&next);
      }
    }
  }

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_flexibleRemainder)

BEGIN_TEST(testJitMacroAssembler_flexibleQuotient) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  // Test case divides 9/2;
  const uintptr_t dividend = 9;
  const uintptr_t divisor = 2;
  const uintptr_t quotient_result = 4;

  AllocatableGeneralRegisterSet leftHandSides(GeneralRegisterSet::All());
  while (!leftHandSides.empty()) {
    Register lhs = leftHandSides.takeAny();

    AllocatableGeneralRegisterSet rightHandSides(GeneralRegisterSet::All());
    while (!rightHandSides.empty()) {
      Register rhs = rightHandSides.takeAny();

      AllocatableGeneralRegisterSet outputs(GeneralRegisterSet::All());
      while (!outputs.empty()) {
        Register output = outputs.takeAny();

        AllocatableRegisterSet regs(RegisterSet::Volatile());
        LiveRegisterSet save(regs.asLiveSet());

        Label next, fail;
        masm.mov(ImmWord(dividend), lhs);
        masm.mov(ImmWord(divisor), rhs);
        masm.flexibleQuotient32(lhs, rhs, output, false, save);
        masm.branchPtr(Assembler::NotEqual, output,
                       ImmWord(lhs != rhs ? quotient_result : 1), &fail);
        // Ensure LHS was not clobbered
        if (lhs != output) {
          masm.branchPtr(Assembler::NotEqual, lhs,
                         ImmWord(lhs != rhs ? dividend : divisor), &fail);
        }
        // Ensure RHS was not clobbered
        if (rhs != output) {
          masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(divisor), &fail);
        }
        masm.jump(&next);
        masm.bind(&fail);
        masm.printf("Failed\n");

        char buffer[128];
        SprintfLiteral(buffer, "\tlhs = %s, rhs = %s, output = %s\n",
                       lhs.name(), rhs.name(), output.name());

        masm.printf(buffer);
        masm.printf("\tlhs = %d\n", lhs);
        masm.printf("\trhs = %d\n", rhs);
        masm.printf("\toutput = %d\n", output);

        masm.breakpoint();

        masm.bind(&next);
      }
    }
  }

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_flexibleQuotient)

bool shiftTest(JSContext* cx, const char* name,
               void (*operation)(StackMacroAssembler& masm, Register, Register),
               uintptr_t lhsInput, uintptr_t rhsInput, uintptr_t result) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  const uintptr_t guardEcx = 0xfeedbad;

  JS::AutoSuppressGCAnalysis suppress;
  AllocatableGeneralRegisterSet leftOutputHandSides(GeneralRegisterSet::All());

  while (!leftOutputHandSides.empty()) {
    Register lhsOutput = leftOutputHandSides.takeAny();

    AllocatableGeneralRegisterSet rightHandSides(GeneralRegisterSet::All());
    while (!rightHandSides.empty()) {
      Register rhs = rightHandSides.takeAny();

      // You can only use shift as the same reg if the values are the same
      if (lhsOutput == rhs && lhsInput != rhsInput) {
        continue;
      }

      Label next, outputFail, clobberRhs, clobberEcx, dump;
      masm.mov(ImmWord(guardEcx), ecx);
      masm.mov(ImmWord(lhsInput), lhsOutput);
      masm.mov(ImmWord(rhsInput), rhs);

      operation(masm, rhs, lhsOutput);

      // Ensure Result is correct
      masm.branchPtr(Assembler::NotEqual, lhsOutput, ImmWord(result),
                     &outputFail);

      // Ensure RHS was not clobbered, unless it's also the output register.
      if (lhsOutput != rhs) {
        masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(rhsInput),
                       &clobberRhs);
      }

      if (lhsOutput != ecx && rhs != ecx) {
        // If neither lhsOutput nor rhs is ecx, make sure ecx has been
        // preserved, otherwise it's expected to be covered by the RHS clobber
        // check above, or intentionally clobbered as the output.
        masm.branchPtr(Assembler::NotEqual, ecx, ImmWord(guardEcx),
                       &clobberEcx);
      }

      masm.jump(&next);

      masm.bind(&outputFail);
      masm.printf("Incorrect output (got %d) ", lhsOutput);
      masm.jump(&dump);

      masm.bind(&clobberRhs);
      masm.printf("rhs clobbered %d", rhs);
      masm.jump(&dump);

      masm.bind(&clobberEcx);
      masm.printf("ecx clobbered");
      masm.jump(&dump);

      masm.bind(&dump);
      masm.mov(ImmPtr(lhsOutput.name()), lhsOutput);
      masm.printf("(lhsOutput/srcDest) %s ", lhsOutput);
      masm.mov(ImmPtr(name), lhsOutput);
      masm.printf("%s ", lhsOutput);
      masm.mov(ImmPtr(rhs.name()), lhsOutput);
      masm.printf("(shift/rhs) %s \n", lhsOutput);
      // Breakpoint to force test failure.
      masm.breakpoint();
      masm.bind(&next);
    }
  }

  return ExecuteJit(cx, masm);
}

BEGIN_TEST(testJitMacroAssembler_flexibleRshift) {
  {
    // Test case  16 >> 2 == 4;
    const uintptr_t lhsInput = 16;
    const uintptr_t rhsInput = 2;
    const uintptr_t result = 4;

    bool res = shiftTest(
        cx, "flexibleRshift32",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleRshift32(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  {
    // Test case  16 >> 16 == 0 -- this helps cover the case where the same
    // register can be passed for source and dest.
    const uintptr_t lhsInput = 16;
    const uintptr_t rhsInput = 16;
    const uintptr_t result = 0;

    bool res = shiftTest(
        cx, "flexibleRshift32",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleRshift32(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  return true;
}
END_TEST(testJitMacroAssembler_flexibleRshift)

BEGIN_TEST(testJitMacroAssembler_flexibleRshiftArithmetic) {
  {
    // Test case  4294967295 >> 2 == 4294967295;
    const uintptr_t lhsInput = 4294967295;
    const uintptr_t rhsInput = 2;
    const uintptr_t result = 4294967295;
    bool res = shiftTest(
        cx, "flexibleRshift32Arithmetic",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleRshift32Arithmetic(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  {
    // Test case  16 >> 16 == 0 -- this helps cover the case where the same
    // register can be passed for source and dest.
    const uintptr_t lhsInput = 16;
    const uintptr_t rhsInput = 16;
    const uintptr_t result = 0;

    bool res = shiftTest(
        cx, "flexibleRshift32Arithmetic",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleRshift32Arithmetic(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  return true;
}
END_TEST(testJitMacroAssembler_flexibleRshiftArithmetic)

BEGIN_TEST(testJitMacroAssembler_flexibleLshift) {
  {
    // Test case  16 << 2 == 64;
    const uintptr_t lhsInput = 16;
    const uintptr_t rhsInput = 2;
    const uintptr_t result = 64;

    bool res = shiftTest(
        cx, "flexibleLshift32",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleLshift32(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  {
    // Test case  4 << 4 == 64; duplicated input case
    const uintptr_t lhsInput = 4;
    const uintptr_t rhsInput = 4;
    const uintptr_t result = 64;

    bool res = shiftTest(
        cx, "flexibleLshift32",
        [](StackMacroAssembler& masm, Register rhs, Register lhsOutput) {
          masm.flexibleLshift32(rhs, lhsOutput);
        },
        lhsInput, rhsInput, result);
    if (!res) {
      return false;
    }
  }

  return true;
}
END_TEST(testJitMacroAssembler_flexibleLshift)

BEGIN_TEST(testJitMacroAssembler_truncateDoubleToInt64) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
  FloatRegister input = allFloatRegs.takeAny();
#  ifdef JS_NUNBOX32
  Register64 output(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 output(allRegs.takeAny());
#  endif
  Register temp = allRegs.takeAny();

  masm.reserveStack(sizeof(int32_t));

#  define TEST(INPUT, OUTPUT)                                                 \
    {                                                                         \
      Label next;                                                             \
      masm.loadConstantDouble(double(INPUT), input);                          \
      masm.storeDouble(input, Operand(esp, 0));                               \
      masm.truncateDoubleToInt64(Address(esp, 0), Address(esp, 0), temp);     \
      masm.branch64(Assembler::Equal, Address(esp, 0), Imm64(OUTPUT), &next); \
      masm.printf("truncateDoubleToInt64(" #INPUT ") failed\n");              \
      masm.breakpoint();                                                      \
      masm.bind(&next);                                                       \
    }

  TEST(0, 0);
  TEST(-0, 0);
  TEST(1, 1);
  TEST(9223372036854774784.0, 9223372036854774784);
  TEST(-9223372036854775808.0, 0x8000000000000000);
  TEST(9223372036854775808.0, 0x8000000000000000);
  TEST(JS::GenericNaN(), 0x8000000000000000);
  TEST(PositiveInfinity<double>(), 0x8000000000000000);
  TEST(NegativeInfinity<double>(), 0x8000000000000000);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_truncateDoubleToInt64)

BEGIN_TEST(testJitMacroAssembler_truncateDoubleToUInt64) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
  FloatRegister input = allFloatRegs.takeAny();
  FloatRegister floatTemp = allFloatRegs.takeAny();
#  ifdef JS_NUNBOX32
  Register64 output(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 output(allRegs.takeAny());
#  endif
  Register temp = allRegs.takeAny();

  masm.reserveStack(sizeof(int32_t));

#  define TEST(INPUT, OUTPUT)                                                 \
    {                                                                         \
      Label next;                                                             \
      masm.loadConstantDouble(double(INPUT), input);                          \
      masm.storeDouble(input, Operand(esp, 0));                               \
      masm.truncateDoubleToUInt64(Address(esp, 0), Address(esp, 0), temp,     \
                                  floatTemp);                                 \
      masm.branch64(Assembler::Equal, Address(esp, 0), Imm64(OUTPUT), &next); \
      masm.printf("truncateDoubleToUInt64(" #INPUT ") failed\n");             \
      masm.breakpoint();                                                      \
      masm.bind(&next);                                                       \
    }

  TEST(0, 0);
  TEST(1, 1);
  TEST(9223372036854774784.0, 9223372036854774784);
  TEST((uint64_t)0x8000000000000000, 0x8000000000000000);
  TEST((uint64_t)0x8000000000000001, 0x8000000000000000);
  TEST((uint64_t)0x8006004000000001, 0x8006004000000000);
  TEST(-0.0, 0);
  TEST(-0.5, 0);
  TEST(-0.99, 0);
  TEST(JS::GenericNaN(), 0x8000000000000000);
  TEST(PositiveInfinity<double>(), 0x8000000000000000);
  TEST(NegativeInfinity<double>(), 0x8000000000000000);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_truncateDoubleToUInt64)

BEGIN_TEST(testJitMacroAssembler_branchDoubleNotInInt64Range) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
  FloatRegister input = allFloatRegs.takeAny();
#  ifdef JS_NUNBOX32
  Register64 output(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 output(allRegs.takeAny());
#  endif
  Register temp = allRegs.takeAny();

  masm.reserveStack(sizeof(int32_t));

#  define TEST(INPUT, OUTPUT)                                           \
    {                                                                   \
      Label next;                                                       \
      masm.loadConstantDouble(double(INPUT), input);                    \
      masm.storeDouble(input, Operand(esp, 0));                         \
      if (OUTPUT) {                                                     \
        masm.branchDoubleNotInInt64Range(Address(esp, 0), temp, &next); \
      } else {                                                          \
        Label fail;                                                     \
        masm.branchDoubleNotInInt64Range(Address(esp, 0), temp, &fail); \
        masm.jump(&next);                                               \
        masm.bind(&fail);                                               \
      }                                                                 \
      masm.printf("branchDoubleNotInInt64Range(" #INPUT ") failed\n");  \
      masm.breakpoint();                                                \
      masm.bind(&next);                                                 \
    }

  TEST(0, false);
  TEST(-0, false);
  TEST(1, false);
  TEST(9223372036854774784.0, false);
  TEST(-9223372036854775808.0, true);
  TEST(9223372036854775808.0, true);
  TEST(JS::GenericNaN(), true);
  TEST(PositiveInfinity<double>(), true);
  TEST(NegativeInfinity<double>(), true);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_branchDoubleNotInInt64Range)

BEGIN_TEST(testJitMacroAssembler_branchDoubleNotInUInt64Range) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
  FloatRegister input = allFloatRegs.takeAny();
#  ifdef JS_NUNBOX32
  Register64 output(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 output(allRegs.takeAny());
#  endif
  Register temp = allRegs.takeAny();

  masm.reserveStack(sizeof(int32_t));

#  define TEST(INPUT, OUTPUT)                                            \
    {                                                                    \
      Label next;                                                        \
      masm.loadConstantDouble(double(INPUT), input);                     \
      masm.storeDouble(input, Operand(esp, 0));                          \
      if (OUTPUT) {                                                      \
        masm.branchDoubleNotInUInt64Range(Address(esp, 0), temp, &next); \
      } else {                                                           \
        Label fail;                                                      \
        masm.branchDoubleNotInUInt64Range(Address(esp, 0), temp, &fail); \
        masm.jump(&next);                                                \
        masm.bind(&fail);                                                \
      }                                                                  \
      masm.printf("branchDoubleNotInUInt64Range(" #INPUT ") failed\n");  \
      masm.breakpoint();                                                 \
      masm.bind(&next);                                                  \
    }

  TEST(0, false);
  TEST(1, false);
  TEST(9223372036854774784.0, false);
  TEST((uint64_t)0x8000000000000000, false);
  TEST((uint64_t)0x8000000000000001, false);
  TEST((uint64_t)0x8006004000000001, false);
  TEST(-0.0, true);
  TEST(-0.5, true);
  TEST(-0.99, true);
  TEST(JS::GenericNaN(), true);
  TEST(PositiveInfinity<double>(), true);
  TEST(NegativeInfinity<double>(), true);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_branchDoubleNotInUInt64Range)

BEGIN_TEST(testJitMacroAssembler_lshift64) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
#  if defined(JS_CODEGEN_X86)
  Register shift = ecx;
  allRegs.take(shift);
#  elif defined(JS_CODEGEN_X64)
  Register shift = rcx;
  allRegs.take(shift);
#  else
  Register shift = allRegs.takeAny();
#  endif

#  ifdef JS_NUNBOX32
  Register64 input(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 input(allRegs.takeAny());
#  endif

  masm.reserveStack(sizeof(int32_t));

#  define TEST(SHIFT, INPUT, OUTPUT)                                        \
    {                                                                       \
      Label next;                                                           \
      masm.move64(Imm64(INPUT), input);                                     \
      masm.move32(Imm32(SHIFT), shift);                                     \
      masm.lshift64(shift, input);                                          \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);         \
      masm.printf("lshift64(" #SHIFT ", " #INPUT ") failed\n");             \
      masm.breakpoint();                                                    \
      masm.bind(&next);                                                     \
    }                                                                       \
    {                                                                       \
      Label next;                                                           \
      masm.move64(Imm64(INPUT), input);                                     \
      masm.lshift64(Imm32(SHIFT & 0x3f), input);                            \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);         \
      masm.printf("lshift64(Imm32(" #SHIFT "&0x3f), " #INPUT ") failed\n"); \
      masm.breakpoint();                                                    \
      masm.bind(&next);                                                     \
    }

  TEST(0, 1, 1);
  TEST(1, 1, 2);
  TEST(2, 1, 4);
  TEST(32, 1, 0x0000000100000000);
  TEST(33, 1, 0x0000000200000000);
  TEST(0, -1, 0xffffffffffffffff);
  TEST(1, -1, 0xfffffffffffffffe);
  TEST(2, -1, 0xfffffffffffffffc);
  TEST(32, -1, 0xffffffff00000000);
  TEST(0xffffffff, 1, 0x8000000000000000);
  TEST(0xfffffffe, 1, 0x4000000000000000);
  TEST(0xfffffffd, 1, 0x2000000000000000);
  TEST(0x80000001, 1, 2);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_lshift64)

BEGIN_TEST(testJitMacroAssembler_rshift64Arithmetic) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
#  if defined(JS_CODEGEN_X86)
  Register shift = ecx;
  allRegs.take(shift);
#  elif defined(JS_CODEGEN_X64)
  Register shift = rcx;
  allRegs.take(shift);
#  else
  Register shift = allRegs.takeAny();
#  endif

#  ifdef JS_NUNBOX32
  Register64 input(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 input(allRegs.takeAny());
#  endif

  masm.reserveStack(sizeof(int32_t));

#  define TEST(SHIFT, INPUT, OUTPUT)                                      \
    {                                                                     \
      Label next;                                                         \
      masm.move64(Imm64(INPUT), input);                                   \
      masm.move32(Imm32(SHIFT), shift);                                   \
      masm.rshift64Arithmetic(shift, input);                              \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);       \
      masm.printf("rshift64Arithmetic(" #SHIFT ", " #INPUT ") failed\n"); \
      masm.breakpoint();                                                  \
      masm.bind(&next);                                                   \
    }                                                                     \
    {                                                                     \
      Label next;                                                         \
      masm.move64(Imm64(INPUT), input);                                   \
      masm.rshift64Arithmetic(Imm32(SHIFT & 0x3f), input);                \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);       \
      masm.printf("rshift64Arithmetic(Imm32(" #SHIFT "&0x3f), " #INPUT    \
                  ") failed\n");                                          \
      masm.breakpoint();                                                  \
      masm.bind(&next);                                                   \
    }

  TEST(0, 0x4000000000000000, 0x4000000000000000);
  TEST(1, 0x4000000000000000, 0x2000000000000000);
  TEST(2, 0x4000000000000000, 0x1000000000000000);
  TEST(32, 0x4000000000000000, 0x0000000040000000);
  TEST(0, 0x8000000000000000, 0x8000000000000000);
  TEST(1, 0x8000000000000000, 0xc000000000000000);
  TEST(2, 0x8000000000000000, 0xe000000000000000);
  TEST(32, 0x8000000000000000, 0xffffffff80000000);
  TEST(0xffffffff, 0x8000000000000000, 0xffffffffffffffff);
  TEST(0xfffffffe, 0x8000000000000000, 0xfffffffffffffffe);
  TEST(0xfffffffd, 0x8000000000000000, 0xfffffffffffffffc);
  TEST(0x80000001, 0x8000000000000000, 0xc000000000000000);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_rshift64Arithmetic)

BEGIN_TEST(testJitMacroAssembler_rshift64) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  AllocatableGeneralRegisterSet allRegs(GeneralRegisterSet::All());
  AllocatableFloatRegisterSet allFloatRegs(FloatRegisterSet::All());
#  if defined(JS_CODEGEN_X86)
  Register shift = ecx;
  allRegs.take(shift);
#  elif defined(JS_CODEGEN_X64)
  Register shift = rcx;
  allRegs.take(shift);
#  else
  Register shift = allRegs.takeAny();
#  endif

#  ifdef JS_NUNBOX32
  Register64 input(allRegs.takeAny(), allRegs.takeAny());
#  else
  Register64 input(allRegs.takeAny());
#  endif

  masm.reserveStack(sizeof(int32_t));

#  define TEST(SHIFT, INPUT, OUTPUT)                                        \
    {                                                                       \
      Label next;                                                           \
      masm.move64(Imm64(INPUT), input);                                     \
      masm.move32(Imm32(SHIFT), shift);                                     \
      masm.rshift64(shift, input);                                          \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);         \
      masm.printf("rshift64(" #SHIFT ", " #INPUT ") failed\n");             \
      masm.breakpoint();                                                    \
      masm.bind(&next);                                                     \
    }                                                                       \
    {                                                                       \
      Label next;                                                           \
      masm.move64(Imm64(INPUT), input);                                     \
      masm.rshift64(Imm32(SHIFT & 0x3f), input);                            \
      masm.branch64(Assembler::Equal, input, Imm64(OUTPUT), &next);         \
      masm.printf("rshift64(Imm32(" #SHIFT "&0x3f), " #INPUT ") failed\n"); \
      masm.breakpoint();                                                    \
      masm.bind(&next);                                                     \
    }

  TEST(0, 0x4000000000000000, 0x4000000000000000);
  TEST(1, 0x4000000000000000, 0x2000000000000000);
  TEST(2, 0x4000000000000000, 0x1000000000000000);
  TEST(32, 0x4000000000000000, 0x0000000040000000);
  TEST(0, 0x8000000000000000, 0x8000000000000000);
  TEST(1, 0x8000000000000000, 0x4000000000000000);
  TEST(2, 0x8000000000000000, 0x2000000000000000);
  TEST(32, 0x8000000000000000, 0x0000000080000000);
  TEST(0xffffffff, 0x8000000000000000, 0x0000000000000001);
  TEST(0xfffffffe, 0x8000000000000000, 0x0000000000000002);
  TEST(0xfffffffd, 0x8000000000000000, 0x0000000000000004);
  TEST(0x80000001, 0x8000000000000000, 0x4000000000000000);
#  undef TEST

  masm.freeStack(sizeof(int32_t));

  return ExecuteJit(cx, masm);
}
END_TEST(testJitMacroAssembler_rshift64)

#endif

#if defined(JS_CODEGEN_ARM64)

BEGIN_TEST(testJitMacroAssembler_moveImmediate) {
  TempAllocator tempAlloc(&cx->tempLifoAlloc());
  JitContext jcx(cx);
  StackMacroAssembler masm(cx, tempAlloc);
  AutoCreatedBy acb(masm, __func__);

  PrepareJit(masm);

  struct {
    uint64_t value;
    uint32_t instructions;
  } cases[] = {
      {0x0000000000000000, 1},
      {0xffffffffffffffff, 1},
      {0x0000ffff0000ffff, 1},
      {0x0000000012345678, 2},
      {0xfff9000100000000, 2},
      {0x123456789abcdef0, 4},
      {0xfff9000012345678, 3},
      // 32-bit move and movk.
      {0xfff90000fffffc18, 2},
      {0x00000001fffffc18, 2},
      {0xfff9000000ff00ff, 2},
      // orr and movk.
      {0x41efffffffe00000, 2},
      {0x3fe6666666666666, 2},
      {0x7fffffff00000001, 2},
      {0xcccccccccccccccd, 2},
  };

  // Logical immediates with one halfword replaced, and 32-bit values with
  // one upper halfword set.
  constexpr size_t NumRandom = 2048;
  uint64_t values[std::size(cases) + NumRandom];
  for (size_t i = 0; i < std::size(cases); i++) {
    values[i] = cases[i].value;
  }
  uint64_t seed = 1;
  auto next = [&seed]() {
    seed = seed * 6364136223846793005 + 1442695040888963407;
    return uint32_t(seed >> 32);
  };
  for (size_t i = 0; i < NumRandom; i++) {
    uint64_t value;
    uint32_t shift;
    if (i % 2 == 0) {
      uint32_t size = 2 << (next() % 6);
      uint64_t element = (uint64_t(1) << (1 + next() % (size - 1))) - 1;
      value = 0;
      for (uint32_t j = 0; j < 64; j += size) {
        value |= element << j;
      }
      value = mozilla::RotateRight(value, next() % 64);
      shift = 16 * (next() % 4);
    } else {
      value = uint64_t(next() & 0xffff) << (16 * (next() % 2));
      if (next() % 2) {
        value ^= 0xffffffff;
      }
      shift = 32 + 16 * (next() % 2);
    }
    value &= ~(uint64_t(0xffff) << shift);
    value |= uint64_t(next() & 0xffff) << shift;
    values[std::size(cases) + i] = value;
  }

  uint64_t results[std::size(values)] = {};
  Register base = CallTempReg0;
  Register dest = CallTempReg1;
  masm.movePtr(ImmPtr(results), base);
  for (size_t i = 0; i < std::size(values); i++) {
    uint32_t start = masm.currentOffset();
    masm.movePtr(ImmWord(values[i]), dest);
    uint32_t instructions = (masm.currentOffset() - start) / 4;
    if (i < std::size(cases)) {
      CHECK_EQUAL(instructions, cases[i].instructions);
    }
    masm.storePtr(dest, Address(base, i * sizeof(uint64_t)));
  }

  CHECK(ExecuteJit(cx, masm));
  for (size_t i = 0; i < std::size(values); i++) {
    CHECK_EQUAL(results[i], values[i]);
  }
  return true;
}
END_TEST(testJitMacroAssembler_moveImmediate)

#endif
