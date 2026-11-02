// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("loong64"); include:codegen-loong64-test.js

// Multiplication by a constant. Note that MMul::canOverflow() is always
// false here, thus ma_mul32TestOverflow and friends are not exercised.

const i32_mul_constants = [
  // Multiplication by 0.
  {
    mul: 0,
    expected: `slli\\.w       \\$a0, \\$zero, 0x0`,
  },

  // Doubling.
  {
    mul: 2,
    expected: `add\\.w        \\$a0, \\$a0, \\$a0`,
  },

  // Negation.
  {
    mul: -1,
    expected: `sub\\.w        \\$a0, \\$zero, \\$a0`,
  },

  // Power of two, encoded as a shift.
  {
    mul: 4,
    expected: `slli\\.w       \\$a0, \\$a0, 0x2`,
  },
  {
    mul: 0x40000000,
    expected: `slli\\.w       \\$a0, \\$a0, 0x1e`,
  },

  // (1 << n) + 1 as x + (x << n), which is an ALSL.W for n < 5.
  {
    mul: 3,
    expected: `alsl\\.w       \\$a0, \\$a0, \\$a0, 0x1`,
  },
  {
    mul: 5,
    expected: `alsl\\.w       \\$a0, \\$a0, \\$a0, 0x2`,
  },
  {
    mul: 9,
    expected: `alsl\\.w       \\$a0, \\$a0, \\$a0, 0x3`,
  },
  {
    mul: 17,
    expected: `alsl\\.w       \\$a0, \\$a0, \\$a0, 0x4`,
  },

  // (1 << n) + 1 for n >= 5 exceeds ALSL.W's shift range.
  {
    mul: 33,
    expected: `slli\\.w       \\$t6, \\$a0, 0x5
               add\\.w        \\$a0, \\$t6, \\$a0`,
  },

  // (1 << c1) | (1 << c2), with c2 != 0.
  {
    mul: 6,
    expected: `slli\\.w       \\$t6, \\$a0, 0x1
               add\\.w        \\$a0, \\$t6, \\$a0
               slli\\.w       \\$a0, \\$a0, 0x1`,
  },

  // (1 << n) - 1, as (x << n) - x.
  {
    mul: 7,
    expected: `slli\\.w       \\$t6, \\$a0, 0x3
               sub\\.w        \\$a0, \\$t6, \\$a0`,
  },

  // INT32_MIN, no shortcut applies.
  {
    mul: -2147483648,
    expected: `lu12i\\.w      \\$t6, -524288
               mul\\.w        \\$a0, \\$a0, \\$t6`,
  },

  // Constant needing 2 instructions to materialize.
  {
    mul: 0x12345678,
    expected: `lu12i\\.w      \\$t6, 74565
               ori            \\$t6, \\$t6, 0x678
               mul\\.w        \\$a0, \\$a0, \\$t6`,
  },
];
for (const {mul, expected} of i32_mul_constants) {
  codegenTestLOONG64_adhoc(
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.mul (local.get 0) (i32.const ${mul}))))`,
      "f", expected);
}

const i64_mul_constants = [
  // Multiplication by 0.
  {
    mul: 0,
    expected: `or             \\$a0, \\$zero, \\$zero`,
  },

  // Doubling.
  {
    mul: 2,
    expected: `add\\.d        \\$a0, \\$a0, \\$a0`,
  },

  // Negation.
  {
    mul: -1,
    expected: `sub\\.d        \\$a0, \\$zero, \\$a0`,
  },

  // Power of two, encoded as a shift.
  {
    mul: 4,
    expected: `slli\\.d       \\$a0, \\$a0, 0x2`,
  },
  {
    mul: "0x4000000000000000",
    expected: `slli\\.d       \\$a0, \\$a0, 0x3e`,
  },

  // (1 << n) + 1 as x + (x << n), which is an ALSL.D for n < 5.
  {
    mul: 3,
    expected: `alsl\\.d       \\$a0, \\$a0, \\$a0, 0x1`,
  },
  {
    mul: 5,
    expected: `alsl\\.d       \\$a0, \\$a0, \\$a0, 0x2`,
  },
  {
    mul: 9,
    expected: `alsl\\.d       \\$a0, \\$a0, \\$a0, 0x3`,
  },
  {
    mul: 17,
    expected: `alsl\\.d       \\$a0, \\$a0, \\$a0, 0x4`,
  },

  // (1 << n) + 1 for n >= 5 exceeds ALSL.D's shift range.
  {
    mul: 33,
    expected: `slli\\.d       \\$t6, \\$a0, 0x5
               add\\.d        \\$a0, \\$t6, \\$a0`,
  },

  // (1 << c1) | (1 << c2), with c2 != 0.
  {
    mul: 6,
    expected: `slli\\.d       \\$t6, \\$a0, 0x1
               add\\.d        \\$a0, \\$t6, \\$a0
               slli\\.d       \\$a0, \\$a0, 0x1`,
  },

  // (1 << n) - 1, as (x << n) - x.
  {
    mul: 7,
    expected: `slli\\.d       \\$t6, \\$a0, 0x3
               sub\\.d        \\$a0, \\$t6, \\$a0`,
  },

  // INT64_MIN, no shortcut applies.
  {
    mul: "0x8000000000000000",
    expected: `lu52i\\.d      \\$t6, \\$zero, -2048
               mul\\.d        \\$a0, \\$a0, \\$t6`,
  },

  // Constant needing 4 instructions to materialize.
  {
    mul: "0x0123456789abcdef",
    expected: `lu12i\\.w      \\$t6, -484676
               lu32i\\.d      \\$t6, 214375
               lu52i\\.d      \\$t6, \\$t6, 18
               ori            \\$t6, \\$t6, 0xdef
               mul\\.d        \\$a0, \\$a0, \\$t6`,
  },
];
for (const {mul, expected} of i64_mul_constants) {
  codegenTestLOONG64_adhoc(
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.mul (local.get 0) (i64.const ${mul}))))`,
      "f", expected);
}
