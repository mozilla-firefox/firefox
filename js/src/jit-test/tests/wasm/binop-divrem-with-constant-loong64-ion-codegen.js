// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("loong64"); include:codegen-loong64-test.js

const WasmTrapIns = `break       0x6`;
const NopIns = `andi        \\$zero, \\$zero, 0x0`;

// Signed 32-bit division with constants.
const i32_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `slli\\.w      \\$a0, \\$a0, 0x0`,
  },
  {
    divisor: 2,
    expected: `srli\\.w      \\$a1, \\$a0, 0x1f
               add\\.w       \\$a1, \\$a1, \\$a0
               srai\\.w      \\$a0, \\$a1, 0x1`,
  },
  {
    divisor: 4,
    expected: `srai\\.w      \\$a1, \\$a0, 0x1f
               srli\\.w      \\$a1, \\$a1, 0x1e
               add\\.w       \\$a1, \\$a1, \\$a0
               srai\\.w      \\$a0, \\$a1, 0x2`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `lu12i\\.w     \\$t6, -524288
               bne           \\$a0, \\$t6, 16 -> ${HEX}+
               bge           \\$zero, \\$zero, 8 -> ${HEX}+
               ${NopIns}
               ${WasmTrapIns}
               sub\\.w       \\$a0, \\$zero, \\$a0`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 349525
               ori           \\$t6, \\$t6, 0x556
               mulh\\.w      \\$a0, \\$a1, \\$t6
               srai\\.w      \\$t6, \\$a1, 0x1f
               sub\\.w       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 5,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 419430
               ori           \\$t6, \\$t6, 0x667
               mulh\\.w      \\$a0, \\$a1, \\$t6
               srai\\.w      \\$a0, \\$a0, 0x1
               srai\\.w      \\$t6, \\$a1, 0x1f
               sub\\.w       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 7,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, -449390
               ori           \\$t6, \\$t6, 0x493
               mulh\\.w      \\$a0, \\$a1, \\$t6
               add\\.w       \\$a0, \\$a0, \\$a1
               srai\\.w      \\$a0, \\$a0, 0x2
               srai\\.w      \\$t6, \\$a1, 0x1f
               sub\\.w       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 9,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 233016
               ori           \\$t6, \\$t6, 0xe39
               mulh\\.w      \\$a0, \\$a1, \\$t6
               srai\\.w      \\$a0, \\$a0, 0x1
               srai\\.w      \\$t6, \\$a1, 0x1f
               sub\\.w       \\$a0, \\$a0, \\$t6`,
  },
];

for (let {divisor, expected} of i32_div_s) {
  let divs32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_s (local.get 0) (i32.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(divs32, "f", expected);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.div_s (local.get 0) (i32.const -${divisor}))))`;
    codegenTestLOONG64_adhoc(divs32, "f", expected + `
      sub\\.w       \\$a0, \\$zero, \\$a0`);
  }
}

// Unsigned 32-bit division with constants.
const i32_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `slli\\.w      \\$a0, \\$a0, 0x0`,
  },
  {
    divisor: 2,
    expected: `srli\\.w      \\$a0, \\$a0, 0x1`,
  },
  {
    divisor: 4,
    expected: `srli\\.w      \\$a0, \\$a0, 0x2`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, -349526
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.wu     \\$a0, \\$a1, \\$t6
               srli\\.w      \\$a0, \\$a0, 0x1`,
  },
  {
    divisor: 5,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, -209716
               ori           \\$t6, \\$t6, 0xccd
               mulh\\.wu     \\$a0, \\$a1, \\$t6
               srli\\.w      \\$a0, \\$a0, 0x2`,
  },
  {
    divisor: 7,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 149796
               ori           \\$t6, \\$t6, 0x925
               mulh\\.wu     \\$a0, \\$a1, \\$t6
               sub\\.w       \\$t6, \\$a1, \\$a0
               srli\\.w      \\$t6, \\$t6, 0x1
               add\\.w       \\$a0, \\$a0, \\$t6
               srli\\.w      \\$a0, \\$a0, 0x2`,
  },
  {
    divisor: 9,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 233016
               ori           \\$t6, \\$t6, 0xe39
               mulh\\.wu     \\$a0, \\$a1, \\$t6
               srli\\.w      \\$a0, \\$a0, 0x1`,
  },

  // Special case: Zero (additional) shift amount.
  {
    divisor: 641,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               lu12i\\.w     \\$t6, 1635
               ori           \\$t6, \\$t6, 0xd81
               mulh\\.wu     \\$a0, \\$a1, \\$t6`,
  },
];

for (let {divisor, expected} of i32_div_u) {
  let divu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_u (local.get 0) (i32.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(divu32, "f", expected);
}

// Signed 64-bit division with constants.
const i64_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               ${WasmTrapIns}`,
  },

  // Division by 1 yields no code.
  {
    divisor: 1,
    expected: ``,
  },

  // Power of two divisor
  {
    divisor: 2,
    expected: `srli\\.d      \\$t6, \\$a0, 0x3f
               add\\.d       \\$t6, \\$t6, \\$a0
               srai\\.d      \\$a0, \\$t6, 0x1`,
  },
  {
    divisor: 4,
    expected: `srai\\.d      \\$t6, \\$a0, 0x3f
               srli\\.d      \\$t6, \\$t6, 0x3e
               add\\.d       \\$t6, \\$t6, \\$a0
               srai\\.d      \\$a0, \\$t6, 0x2`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `srai\\.d      \\$t6, \\$a0, 0x3f
               srli\\.d      \\$t6, \\$t6, 0x20
               add\\.d       \\$t6, \\$t6, \\$a0
               srai\\.d      \\$a0, \\$t6, 0x20`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `lu52i\\.d     \\$t6, \\$zero, -2048
               bne           \\$a0, \\$t6, 16 -> ${HEX}+
               bge           \\$zero, \\$zero, 8 -> ${HEX}+
               ${NopIns}
               ${WasmTrapIns}
               sub\\.d       \\$a0, \\$zero, \\$a0`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, 349525
               lu32i\\.d     \\$t6, 349525
               lu52i\\.d     \\$t6, \\$t6, 1365
               ori           \\$t6, \\$t6, 0x556
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 5,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, 419430
               lu32i\\.d     \\$t6, 419430
               lu52i\\.d     \\$t6, \\$t6, 1638
               ori           \\$t6, \\$t6, 0x667
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$a0, \\$a0, 0x1
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 7,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, 149796
               lu32i\\.d     \\$t6, 299593
               lu52i\\.d     \\$t6, \\$t6, 1170
               ori           \\$t6, \\$t6, 0x925
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$a0, \\$a0, 0x1
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 9,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, 466033
               lu32i\\.d     \\$t6, 116508
               lu52i\\.d     \\$t6, \\$t6, 455
               ori           \\$t6, \\$t6, 0xc72
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, 682
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$a0, \\$a0, 0x1f
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6`,
  },
];

for (let {divisor, expected} of i64_div_s) {
  let divs64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_s (local.get 0) (i64.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(divs64, "f", expected);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.div_s (local.get 0) (i64.const -${divisor}))))`;
    codegenTestLOONG64_adhoc(divs64, "f", expected + `
      sub\\.d       \\$a0, \\$zero, \\$a0`);
  }
}

// Unsigned 64-bit division with constants.
const i64_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               ${WasmTrapIns}`,
  },

  // Division by 1 yields no code.
  {
    divisor: 1,
    expected: ``,
  },

  // Power of two divisor
  {
    divisor: 2,
    expected: `srli\\.d      \\$a0, \\$a0, 0x1`,
  },
  {
    divisor: 4,
    expected: `srli\\.d      \\$a0, \\$a0, 0x2`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `srli\\.d      \\$a0, \\$a0, 0x20`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, -1366
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.du     \\$a0, \\$a1, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x1`,
  },
  {
    divisor: 5,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -209716
               lu32i\\.d     \\$t6, -209716
               lu52i\\.d     \\$t6, \\$t6, -820
               ori           \\$t6, \\$t6, 0xccd
               mulh\\.du     \\$a0, \\$a1, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x2`,
  },
  {
    divisor: 7,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -449390
               lu32i\\.d     \\$t6, 149796
               lu52i\\.d     \\$t6, \\$t6, 585
               ori           \\$t6, \\$t6, 0x493
               mulh\\.du     \\$a0, \\$a1, \\$t6
               sub\\.d       \\$t6, \\$a1, \\$a0
               srli\\.d      \\$t6, \\$t6, 0x1
               add\\.d       \\$a0, \\$a0, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x2`,
  },
  {
    divisor: 9,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -466034
               lu32i\\.d     \\$t6, -116509
               lu52i\\.d     \\$t6, \\$t6, -456
               ori           \\$t6, \\$t6, 0x38f
               mulh\\.du     \\$a0, \\$a1, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x3`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, -1366
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.du     \\$a0, \\$a1, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x21`,
  },

  // Special case: Zero shift amount.
  {
    divisor: 274177,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -58931
               lu32i\\.d     \\$t6, 15664
               ori           \\$t6, \\$t6, 0x101
               mulh\\.du     \\$a0, \\$a1, \\$t6`,
  },
];

for (let {divisor, expected} of i64_div_u) {
  let divu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_u (local.get 0) (i64.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(divu64, "f", expected);
}

//////////////

function i32RemPowTwoExpected(msb) {
  return `blt           \\$a0, \\$zero, 20 -> ${HEX}+
          ${NopIns}
          bstrpick\\.w  \\$a0, \\$a0, ${msb}, 0x0
          beq           \\$zero, \\$zero, 20 -> ${HEX}+
          ${NopIns}
          sub\\.w       \\$a0, \\$zero, \\$a0
          bstrpick\\.w  \\$a0, \\$a0, ${msb}, 0x0
          sub\\.w       \\$a0, \\$zero, \\$a0`;
}

// Signed 32-bit remainder with constants.

const i32_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `addi\\.w      \\$a0, \\$zero, 0`,
  },
  {
    divisor: 2,
    expected: i32RemPowTwoExpected("0x0"),
  },
  {
    divisor: 4,
    expected: i32RemPowTwoExpected("0x1"),
  },
  {
    divisor: 0x100,
    expected: i32RemPowTwoExpected("0x7"),
  },
  {
    divisor: 0x10000,
    expected: i32RemPowTwoExpected("0xf"),
  },
  {
    divisor: 0x8000_0000,
    expected: i32RemPowTwoExpected("0x1e"),
  },
];

for (let {divisor, expected} of i32_rem_s) {
  let rems32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_s (local.get 0) (i32.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(rems32, "f", expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.rem_s (local.get 0) (i32.const -${divisor}))))`;
    codegenTestLOONG64_adhoc(rems32, "f", expected);
  }
}

// Unigned 32-bit remainder with constants.
const i32_rem_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `slli\\.w      \\$a2, \\$a0, 0x0
               slli\\.w      \\$a1, \\$a2, 0x0
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `addi\\.w      \\$a0, \\$zero, 0`,
  },
  {
    divisor: 2,
    expected: `bstrpick\\.w  \\$a0, \\$a0, 0x0, 0x0`,
  },
  {
    divisor: 4,
    expected: `bstrpick\\.w  \\$a0, \\$a0, 0x1, 0x0`,
  },
  {
    divisor: 0x100,
    expected: `bstrpick\\.w  \\$a0, \\$a0, 0x7, 0x0`,
  },
  {
    divisor: 0x10000,
    expected: `bstrpick\\.w  \\$a0, \\$a0, 0xf, 0x0`,
  },
  {
    divisor: 0x8000_0000,
    expected: `bstrpick\\.w  \\$a0, \\$a0, 0x1e, 0x0`,
  },
];

for (let {divisor, expected} of i32_rem_u) {
  let remu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_u (local.get 0) (i32.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(remu32, "f", expected);
}

function i64RemPowTwoExpected(msb) {
  return `blt           \\$a0, \\$zero, 20 -> ${HEX}+
          ${NopIns}
          bstrpick\\.d  \\$a0, \\$a0, ${msb}, 0x0
          beq           \\$zero, \\$zero, 20 -> ${HEX}+
          ${NopIns}
          sub\\.d       \\$a0, \\$zero, \\$a0
          bstrpick\\.d  \\$a0, \\$a0, ${msb}, 0x0
          sub\\.d       \\$a0, \\$zero, \\$a0`;
}

// Signed 64-bit remainder with constants.
const i64_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `or            \\$a0, \\$zero, \\$zero`,
  },
  {
    divisor: 2,
    expected: i64RemPowTwoExpected("0x0"),
  },
  {
    divisor: 4,
    expected: i64RemPowTwoExpected("0x1"),
  },
  {
    divisor: 0x100,
    expected: i64RemPowTwoExpected("0x7"),
  },
  {
    divisor: 0x10000,
    expected: i64RemPowTwoExpected("0xf"),
  },
  {
    divisor: 0x8000_0000,
    expected: i64RemPowTwoExpected("0x1e"),
  },
  {
    divisor: 0x1_0000_0000,
    expected: i64RemPowTwoExpected("0x1f"),
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: i64RemPowTwoExpected("0x3e"),
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, 682
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$a0, \\$a0, 0x1f
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6
               slli\\.d      \\$t6, \\$a0, 0x1
               add\\.d       \\$a0, \\$t6, \\$a0
               slli\\.d      \\$a0, \\$a0, 0x20
               sub\\.d       \\$a0, \\$a1, \\$a0`,
    negative: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, 682
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.d      \\$a0, \\$a1, \\$t6
               srai\\.d      \\$a0, \\$a0, 0x1f
               srai\\.d      \\$t6, \\$a1, 0x3f
               sub\\.d       \\$a0, \\$a0, \\$t6
               sub\\.d       \\$a0, \\$zero, \\$a0
               lu12i\\.w     \\$t6, 0
               lu32i\\.d     \\$t6, -3
               mul\\.d       \\$a0, \\$a0, \\$t6
               sub\\.d       \\$a0, \\$a1, \\$a0`,
  },
];

for (let {divisor, expected, negative = expected} of i64_rem_s) {
  let rems64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_s (local.get 0) (i64.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(rems64, "f", expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.rem_s (local.get 0) (i64.const -${divisor}))))`;
    codegenTestLOONG64_adhoc(rems64, "f", negative);
  }
}

// Unsigned 64-bit remainder with constants.
const i64_rem_u = [
  {
    divisor: 0,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               ${WasmTrapIns}`,
  },
  {
    divisor: 1,
    expected: `or            \\$a0, \\$zero, \\$zero`,
  },
  {
    divisor: 2,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x0, 0x0`,
  },
  {
    divisor: 4,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x1, 0x0`,
  },
  {
    divisor: 0x100,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x7, 0x0`,
  },
  {
    divisor: 0x10000,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0xf, 0x0`,
  },
  {
    divisor: 0x8000_0000,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x1e, 0x0`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x1f, 0x0`,
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: `bstrpick\\.d  \\$a0, \\$a0, 0x3e, 0x0`,
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `or            \\$a2, \\$a0, \\$zero
               or            \\$a1, \\$a2, \\$zero
               lu12i\\.w     \\$t6, -349526
               lu32i\\.d     \\$t6, -349526
               lu52i\\.d     \\$t6, \\$t6, -1366
               ori           \\$t6, \\$t6, 0xaab
               mulh\\.du     \\$a0, \\$a1, \\$t6
               srli\\.d      \\$a0, \\$a0, 0x21
               slli\\.d      \\$t6, \\$a0, 0x1
               add\\.d       \\$a0, \\$t6, \\$a0
               slli\\.d      \\$a0, \\$a0, 0x20
               sub\\.d       \\$a0, \\$a1, \\$a0`,
  },
];

for (let {divisor, expected} of i64_rem_u) {
  let remu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_u (local.get 0) (i64.const ${divisor}))))`;
  codegenTestLOONG64_adhoc(remu64, "f", expected);
}
