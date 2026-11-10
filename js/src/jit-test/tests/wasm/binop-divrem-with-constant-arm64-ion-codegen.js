// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("arm64"); include:codegen-arm64-test.js

const WasmTrapIns = `dcps0   \\{#0x0\\} \\(Wasm Trap\\)`;

// Signed 32-bit division with constants.
const i32_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     w2, w0
               mov     w1, w2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w0, w1`,
  },
  {
    divisor: 2,
    expected: `mov     w2, w0
               mov     w1, w2
               lsr     w0, w1, #31
               add     w0, w0, w1
               asr     w0, w0, #1`,
  },
  {
    divisor: 4,
    expected: `mov     w2, w0
               mov     w1, w2
               asr     w0, w1, #31
               lsr     w0, w0, #30
               add     w0, w0, w1
               asr     w0, w0, #2`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `mov     w2, w0
               mov     w1, w2
               negs    w0, w1
               b.vc    #\\+0x8 \\(addr 0x${HEX}+\\)
               ${WasmTrapIns}`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov     w2, w0
                mov     w1, w2
                mov     w16, #0x5556
                movk    w16, #0x5555, lsl #16
                smull   x0, w16, w1
                asr     x0, x0, #32
                sub     w0, w0, w1, asr #31`,
  },
  {
    divisor: 5,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x6667
               movk    w16, #0x6666, lsl #16
               smull   x0, w16, w1
               asr     x0, x0, #33
               sub     w0, w0, w1, asr #31`,
  },
  {
    divisor: 7,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x2493
               movk    w16, #0x9249, lsl #16
               lsl     x0, x1, #32
               smaddl  x0, w16, w1, x0
               asr     x0, x0, #34
               sub     w0, w0, w1, asr #31`,
  },
  {
    divisor: 9,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x8e39
               movk    w16, #0x38e3, lsl #16
               smull   x0, w16, w1
               asr     x0, x0, #33
               sub     w0, w0, w1, asr #31`,
  },
];

for (let {divisor, expected} of i32_div_s) {
  let divs32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_s (local.get 0) (i32.const ${divisor}))))`
  codegenTestARM64_adhoc(divs32, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.div_s (local.get 0) (i32.const -${divisor}))))`
    codegenTestARM64_adhoc(divs32, 'f', expected + `
      neg     w0, w0`
    );
  }
}

// Unsigned 32-bit division with constants.
const i32_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     w2, w0
               mov     w1, w2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w0, w1`,
  },
  {
    divisor: 2,
    expected: `mov     w2, w0
               mov     w1, w2
               lsr     w0, w1, #1`,
  },
  {
    divisor: 4,
    expected: `mov     w2, w0
               mov     w1, w2
               lsr     w0, w1, #2`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov     w2, w0
                mov     w1, w2
                mov     w16, #0xaaab
                movk    w16, #0xaaaa, lsl #16
                umull   x0, w16, w1
                lsr     x0, x0, #33`,
  },
  {
    divisor: 5,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0xcccd
               movk    w16, #0xcccc, lsl #16
               umull   x0, w16, w1
               lsr     x0, x0, #34`,
  },
  {
    divisor: 7,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x4925
               movk    w16, #0x2492, lsl #16
               umull   x0, w16, w1
               add     x0, x1, x0, lsr #32
               lsr     x0, x0, #3`,
  },
  {
    divisor: 9,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x8e39
               movk    w16, #0x38e3, lsl #16
               umull   x0, w16, w1
               lsr     x0, x0, #33`,
  },

  // Special case: Zero (additional) shift amount.
  {
    divisor: 641,
    expected: `mov     w2, w0
               mov     w1, w2
               mov     w16, #0x3d81
               movk    w16, #0x66, lsl #16
               umull   x0, w16, w1
               lsr     x0, x0, #32`,
  },
];

for (let {divisor, expected} of i32_div_u) {
  let divu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_u (local.get 0) (i32.const ${divisor}))))`
  codegenTestARM64_adhoc(divu32, 'f', expected);
}

// Signed 64-bit division with constants.
const i64_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     x2, x0
               mov     x1, x2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x0, x1`,
  },
  {
    divisor: 2,
    expected: `mov     x2, x0
               mov     x1, x2
               lsr     x0, x1, #63
               add     x0, x0, x1
               asr     x0, x0, #1`,
  },
  {
    divisor: 4,
    expected: `mov     x2, x0
               mov     x1, x2
               asr     x0, x1, #63
               lsr     x0, x0, #62
               add     x0, x0, x1
               asr     x0, x0, #2`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               asr     x0, x1, #63
               lsr     x0, x0, #32
               add     x0, x0, x1
               asr     x0, x0, #32`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `mov     x2, x0
               mov     x1, x2
               negs    x0, x1
               b.vc    #\\+0x8 \\(addr 0x${HEX}+\\)
               ${WasmTrapIns}`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov     x2, x0
                mov     x1, x2
                mov     x16, #0x5555555555555555
                movk    x16, #0x5556
                smulh   x0, x1, x16
                sub     x0, x0, x1, asr #63`,
  },
  {
    divisor: 5,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0x6666666666666666
               movk    x16, #0x6667
               smulh   x0, x1, x16
               asr     x0, x0, #1
               sub     x0, x0, x1, asr #63`,
  },
  {
    divisor: 7,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0x4925
               movk    x16, #0x2492, lsl #16
               movk    x16, #0x9249, lsl #32
               movk    x16, #0x4924, lsl #48
               smulh   x0, x1, x16
               asr     x0, x0, #1
               sub     x0, x0, x1, asr #63`,
  },
  {
    divisor: 9,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0x1c72
               movk    x16, #0x71c7, lsl #16
               movk    x16, #0xc71c, lsl #32
               movk    x16, #0x1c71, lsl #48
               smulh   x0, x1, x16
               sub     x0, x0, x1, asr #63`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xaaab
               movk    x16, #0xaaaa, lsl #16
               movk    x16, #0xaaaa, lsl #32
               movk    x16, #0x2aaa, lsl #48
               smulh   x0, x1, x16
               asr     x0, x0, #31
               sub     x0, x0, x1, asr #63`,
  },
];

for (let {divisor, expected} of i64_div_s) {
  let divs64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_s (local.get 0) (i64.const ${divisor}))))`
  codegenTestARM64_adhoc(divs64, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.div_s (local.get 0) (i64.const -${divisor}))))`
    codegenTestARM64_adhoc(divs64, 'f', expected + `
      neg     x0, x0`);
  }
}

// Unsigned 64-bit division with constants.
const i64_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     x2, x0
               mov     x1, x2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x0, x1`,
  },
  {
    divisor: 2,
    expected: `mov     x2, x0
               mov     x1, x2
               lsr     x0, x1, #1`,
  },
  {
    divisor: 4,
    expected: `mov     x2, x0
               mov     x1, x2
               lsr     x0, x1, #2`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               lsr     x0, x1, #32`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov     x2, x0
                mov     x1, x2
                mov     x16, #0xaaaaaaaaaaaaaaaa
                movk    x16, #0xaaab
                umulh   x0, x1, x16
                lsr     x0, x0, #1`,
  },
  {
    divisor: 5,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xcccccccccccccccc
               movk    x16, #0xcccd
               umulh   x0, x1, x16
               lsr     x0, x0, #2`,
  },
  {
    divisor: 7,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0x2493
               movk    x16, #0x9249, lsl #16
               movk    x16, #0x4924, lsl #32
               movk    x16, #0x2492, lsl #48
               umulh   x0, x1, x16
               sub     x16, x1, x0
               add     x0, x0, x16, lsr #1
               lsr     x0, x0, #2`,
  },
  {
    divisor: 9,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xe38f
               movk    x16, #0x8e38, lsl #16
               movk    x16, #0x38e3, lsl #32
               movk    x16, #0xe38e, lsl #48
               umulh   x0, x1, x16
               lsr     x0, x0, #3`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xaaaaaaaaaaaaaaaa
               movk    x16, #0xaaab
               umulh   x0, x1, x16
               lsr     x0, x0, #33`,
  },

  // Special case: Zero shift amount.
  {
    divisor: 274177,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xd101
               movk    x16, #0xf19c, lsl #16
               movk    x16, #0x3d30, lsl #32
               umulh   x0, x1, x16`,
  },
];

for (let {divisor, expected} of i64_div_u) {
  let divu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_u (local.get 0) (i64.const ${divisor}))))`
  codegenTestARM64_adhoc(divu64, 'f', expected);
}

//////////////



// Signed 32-bit remainder with constants.
const i32_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     w2, w0
               mov     w1, w2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     w0, wzr`,
  },
  {
    divisor: 2,
    expected: `tbnz    w0, #31, #\\+0xc \\(addr 0x${HEX}+\\)
               and     w0, w0, #0x1
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     w0, w0
               and     w0, w0, #0x1
               neg     w0, w0`,
  },
  {
    divisor: 4,
    expected: `tbnz    w0, #31, #\\+0xc \\(addr 0x${HEX}+\\)
               and     w0, w0, #0x3
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     w0, w0
               and     w0, w0, #0x3
               neg     w0, w0`,
  },
  {
    divisor: 0x100,
    expected: `tbnz    w0, #31, #\\+0xc \\(addr 0x${HEX}+\\)
               and     w0, w0, #0xff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     w0, w0
               and     w0, w0, #0xff
               neg     w0, w0`,
  },
  {
    divisor: 0x10000,
    expected: `tbnz    w0, #31, #\\+0xc \\(addr 0x${HEX}+\\)
               and     w0, w0, #0xffff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     w0, w0
               and     w0, w0, #0xffff
               neg     w0, w0`,
  },
  {
    divisor: 0x8000_0000,
    expected: `tbnz    w0, #31, #\\+0xc \\(addr 0x${HEX}+\\)
               and     w0, w0, #0x7fffffff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     w0, w0
               and     w0, w0, #0x7fffffff
               neg     w0, w0`,
  },
];

for (let {divisor, expected} of i32_rem_s) {
  let rems32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_s (local.get 0) (i32.const ${divisor}))))`
  codegenTestARM64_adhoc(rems32, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.rem_s (local.get 0) (i32.const -${divisor}))))`
    codegenTestARM64_adhoc(rems32, 'f', expected);
  }
}

// Unigned 32-bit remainder with constants.
const u32_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     w2, w0
               mov     w1, w2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     w0, wzr`,
  },
  {
    divisor: 2,
    expected: `and     w0, w0, #0x1`,
  },
  {
    divisor: 4,
    expected: `and     w0, w0, #0x3`,
  },
  {
    divisor: 0x100,
    expected: `and     w0, w0, #0xff`,
  },
  {
    divisor: 0x10000,
    expected: `and     w0, w0, #0xffff`,
  },
  {
    divisor: 0x8000_0000,
    expected: `and     w0, w0, #0x7fffffff`,
  },
];

for (let {divisor, expected} of u32_rem_s) {
  let remu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_u (local.get 0) (i32.const ${divisor}))))`
  codegenTestARM64_adhoc(remu32, 'f', expected);
}

// Signed 64-bit remainder with constants.
const i64_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     x2, x0
               mov     x1, x2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     x0, xzr`,
  },
  {
    divisor: 2,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0x1
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0x1
               neg     x0, x0`,
  },
  {
    divisor: 4,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0x3
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0x3
               neg     x0, x0`,
  },
  {
    divisor: 0x100,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0xff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0xff
               neg     x0, x0`,
  },
  {
    divisor: 0x10000,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0xffff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0xffff
               neg     x0, x0`,
  },
  {
    divisor: 0x8000_0000,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0x7fffffff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0x7fffffff
               neg     x0, x0`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               mov     w0, w0
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               mov     w0, w0
               neg     x0, x0`,
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: `tbnz    x0, #63, #\\+0xc \\(addr 0x${HEX}+\\)
               and     x0, x0, #0x7fffffffffffffff
               b       #\\+0x10 \\(addr 0x${HEX}+\\)
               neg     x0, x0
               and     x0, x0, #0x7fffffffffffffff
               neg     x0, x0`,
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xaaab
               movk    x16, #0xaaaa, lsl #16
               movk    x16, #0xaaaa, lsl #32
               movk    x16, #0x2aaa, lsl #48
               smulh   x0, x1, x16
               asr     x0, x0, #31
               sub     x0, x0, x1, asr #63
               mov     x16, #0x300000000
               msub    x0, x0, x16, x1`,
    negative: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xaaab
               movk    x16, #0xaaaa, lsl #16
               movk    x16, #0xaaaa, lsl #32
               movk    x16, #0x2aaa, lsl #48
               smulh   x0, x1, x16
               asr     x0, x0, #31
               sub     x0, x0, x1, asr #63
               neg     x0, x0
               mov     x16, #0xfffd00000000
               movk    x16, #0xffff, lsl #48
               msub    x0, x0, x16, x1`,
  },
];

for (let {divisor, expected, negative = expected} of i64_rem_s) {
  let rems64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_s (local.get 0) (i64.const ${divisor}))))`
  codegenTestARM64_adhoc(rems64, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.rem_s (local.get 0) (i64.const -${divisor}))))`
    codegenTestARM64_adhoc(rems64, 'f', negative);
  }
}

// Unsigned 64-bit remainder with constants.
const i64_rem_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `mov     x2, x0
               mov     x1, x2
               ${WasmTrapIns}`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov     x0, xzr`,
  },
  {
    divisor: 2,
    expected: `and     x0, x0, #0x1`,
  },
  {
    divisor: 4,
    expected: `and     x0, x0, #0x3`,
  },
  {
    divisor: 0x100,
    expected: `and     x0, x0, #0xff`,
  },
  {
    divisor: 0x10000,
    expected: `and     x0, x0, #0xffff`,
  },
  {
    divisor: 0x8000_0000,
    expected: `and     x0, x0, #0x7fffffff`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov     w0, w0`,
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: `and     x0, x0, #0x7fffffffffffffff`,
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `mov     x2, x0
               mov     x1, x2
               mov     x16, #0xaaaaaaaaaaaaaaaa
               movk    x16, #0xaaab
               umulh   x0, x1, x16
               lsr     x0, x0, #33
               mov     x16, #0x300000000
               msub    x0, x0, x16, x1`,
  },
];

for (let {divisor, expected} of i64_rem_u) {
  let remu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_u (local.get 0) (i64.const ${divisor}))))`
  codegenTestARM64_adhoc(remu64, 'f', expected);
}
