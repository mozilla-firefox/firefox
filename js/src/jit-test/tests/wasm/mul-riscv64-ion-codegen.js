// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("riscv64"); include:codegen-riscv64-test.js

// Multiplication by a constant. Note that MMul::canOverflow() is always
// false here, thus ma_mul32TestOverflow and friends are not exercised.

const i32_mul_constants = [
  // Multiplication by 0.
  {
    mul: 0,
    expected: [
      `mv         a0, zero`,
    ],
  },

  // Doubling.
  {
    mul: 2,
    expected: [
      `addw       a0, a0, a0`,
    ],
  },

  // Negation.
  {
    mul: -1,
    expected: [
      `negw       a0, a0`,
    ],
  },

  // Power of two, encoded as a shift.
  {
    mul: 4,
    expected: [
      `slliw      a0, a0, 2`,
    ],
  },
  {
    mul: 0x40000000,
    expected: [
      `slliw      a0, a0, 30`,
    ],
  },

  // 3x, 5x and 9x: sh1add, sh2add or sh3add when Zba is available.
  {
    mul: 3,
    expected: [
      `sh1add     a0, a0, a0
       sext\\.w   a0, a0`,
      `slliw      t4, a0, 2
       subw       a0, t4, a0`,
    ],
  },
  {
    mul: 5,
    expected: [
      `sh2add     a0, a0, a0
       sext\\.w   a0, a0`,
      `slliw      t4, a0, 2
       addw       a0, t4, a0`,
    ],
  },
  {
    mul: 9,
    expected: [
      `sh3add     a0, a0, a0
       sext\\.w   a0, a0`,
      `slliw      t4, a0, 3
       addw       a0, t4, a0`,
    ],
  },

  // (1 << n) + 1 as x + (x << n).
  {
    mul: 17,
    expected: [
      `slliw      t4, a0, 4
       addw       a0, t4, a0`,
    ],
  },
  {
    mul: 33,
    expected: [
      `slliw      t4, a0, 5
       addw       a0, t4, a0`,
    ],
  },

  // (1 << c1) | (1 << c2), with c2 != 0. As sh1add and a shift when Zba is
  // available.
  {
    mul: 6,
    expected: [
      `sh1add     a0, a0, a0
       slliw      a0, a0, 1`,
      `slliw      t4, a0, 1
       addw       a0, t4, a0
       slliw      a0, a0, 1`,
    ],
  },

  // (1 << n) - 1, as (x << n) - x.
  {
    mul: 7,
    expected: [
      `slliw      t4, a0, 3
       subw       a0, t4, a0`,
    ],
  },

  // INT32_MIN, no shortcut applies.
  {
    mul: -2147483648,
    expected: [
      `lui        t4, 0xfff80000
       mulw       a0, a0, t4`,
    ],
  },

  // Constant needing 2 instructions to materialize.
  {
    mul: 0x12345678,
    expected: [
      `lui        t4, 0x12345
       addiw      t4, t4, 1656
       mulw       a0, a0, t4`,
    ],
  },
];
for (const {mul, expected} of i32_mul_constants) {
  codegenTestRISCV64_adhoc(
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.mul (local.get 0) (i32.const ${mul}))))`,
      "f", `(${expected.join("|")})`);
}

const i64_mul_constants = [
  // Multiplication by 0.
  {
    mul: 0,
    expected: [
      `mv         a0, zero`,
    ],
  },

  // Doubling.
  {
    mul: 2,
    expected: [
      `add        a0, a0, a0`,
    ],
  },

  // Negation.
  {
    mul: -1,
    expected: [
      `neg        a0, a0`,
    ],
  },

  // Power of two, encoded as a shift.
  {
    mul: 4,
    expected: [
      `slli       a0, a0, 2`,
    ],
  },
  {
    mul: "0x4000000000000000",
    expected: [
      `slli       a0, a0, 62`,
    ],
  },

  // 3x, 5x and 9x: sh1add, sh2add or sh3add when Zba is available.
  {
    mul: 3,
    expected: [
      `sh1add     a0, a0, a0`,
      `slli       t4, a0, 2
       sub        a0, t4, a0`,
    ],
  },
  {
    mul: 5,
    expected: [
      `sh2add     a0, a0, a0`,
      `slli       t4, a0, 2
       add        a0, t4, a0`,
    ],
  },
  {
    mul: 9,
    expected: [
      `sh3add     a0, a0, a0`,
      `slli       t4, a0, 3
       add        a0, t4, a0`,
    ],
  },

  // (1 << n) + 1 as x + (x << n).
  {
    mul: 17,
    expected: [
      `slli       t4, a0, 4
       add        a0, t4, a0`,
    ],
  },
  {
    mul: 33,
    expected: [
      `slli       t4, a0, 5
       add        a0, t4, a0`,
    ],
  },

  // (1 << c1) | (1 << c2), with c2 != 0. As sh1add and a shift when Zba is
  // available.
  {
    mul: 6,
    expected: [
      `sh1add     a0, a0, a0
       slli       a0, a0, 1`,
      `slli       t4, a0, 1
       add        a0, t4, a0
       slli       a0, a0, 1`,
    ],
  },

  // (1 << n) - 1, as (x << n) - x.
  {
    mul: 7,
    expected: [
      `slli       t4, a0, 3
       sub        a0, t4, a0`,
    ],
  },

  // INT64_MIN, no shortcut applies. Materialized via bseti when Zbs is
  // available.
  {
    mul: "0x8000000000000000",
    expected: [
      `bseti      t4, zero, 63
       mul        a0, a0, t4`,
      `li         t4, -1
       slli       t4, t4, 63
       mul        a0, a0, t4`,
    ],
  },

  // Constant needing 6 instructions to materialize. Via slli.uw when Zba
  // is available.
  {
    mul: "0x0123456789abcdef",
    expected: [
      `lui        t4, 0xfff91a2b
       addiw      t4, t4, 965
       slli\\.uw  t4, t4, 13
       addi       t4, t4, -1347
       slli       t4, t4, 12
       addi       t4, t4, -529
       mul        a0, a0, t4`,
      `lui        t4, 0xfff89abd
       addiw      t4, t4, -529
       lui        t5, 0x1234
       addiw      t5, t5, 1384
       slli       t5, t5, 32
       add        t4, t4, t5
       mul        a0, a0, t4`,
    ],
  },
];
for (const {mul, expected} of i64_mul_constants) {
  codegenTestRISCV64_adhoc(
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.mul (local.get 0) (i64.const ${mul}))))`,
      "f", `(${expected.join("|")})`);
}
