// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("arm64"); include:codegen-arm64-test.js

// Basic constant folding tests

for ( [op, lhs, rhs, expect] of
      [['add',   5,    8,    'mov     x0, #0xd'],
       ['sub',   4,    5,    'mov     x0, #0xffffffffffffffff'],
       ['mul',   8,    3,    'mov     x0, #0x18'],
       ['div_s', -8,   3,    'mov     x0, #0xfffffffffffffffe'],
       ['div_u', 8,    3,    'mov     x0, #0x2'],
       ['rem_s', 8,    5,    'mov     x0, #0x3'],
       ['rem_u', -7,   4,    'mov     x0, #0x1'],
       ['and',   0xfe, 0x77, 'mov     x0, #0x76'],
       ['or',    0xfe, 0x77, 'mov     x0, #0xff'],
       ['xor',   0xfe, 0x77, 'mov     x0, #0x89'],
       ['shl',   3,    4,    'mov     x0, #0x30'],
       ['shr_s', -8,   1,    'mov     x0, #0xfffffffffffffffc'],
       ['shr_u', -8,   1,    'mov     x0, #0x7ffffffffffffffc']] ) {
    codegenTestARM64_adhoc(`
(module
  (func (export "f") (result i64)
    (i64.${op} (i64.const ${lhs}) (i64.const ${rhs}))))`,
                           'f',
                           expect);
}

// Basic tests that addition and multiplication identities are collapsed, use
// arg 1 here to force an explicit move to be emitted.

for ( [op, args, expect] of
      [['add',   '(local.get 1) (i64.const 0)', 'mov     x0, x1'],
       ['add',   '(i64.const 0) (local.get 1)', 'mov     x0, x1'],
       ['mul',   '(local.get 1) (i64.const 1)', 'mov     x0, x1'],
       ['mul',   '(i64.const 1) (local.get 1)', 'mov     x0, x1']] ) {
    codegenTestARM64_adhoc(`
(module
  (func (export "f") (param i64) (param i64) (result i64)
    (i64.${op} ${args})))`,
                           'f',
                           expect);
}

// Test that multiplication by -1 yields negation.

let neg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const -1))))`;
codegenTestARM64_adhoc(
    neg32,
    'f',
    'neg w0, w0');
assertEq(wasmEvalText(neg32).exports.f(-37), 37)
assertEq(wasmEvalText(neg32).exports.f(42), -42)

let neg64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const -1))))`
codegenTestARM64_adhoc(
    neg64,
    'f',
    'neg x0, x0');
assertEq(wasmEvalText(neg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(neg64).exports.f(42000000000n), -42000000000n)

// Test that multiplication by zero yields zero

let zero32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 0))))`;
codegenTestARM64_adhoc(
    zero32,
    'f',
    'mov w0, wzr');
assertEq(wasmEvalText(zero32).exports.f(-37), 0)
assertEq(wasmEvalText(zero32).exports.f(42), 0)

let zero64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 0))))`
codegenTestARM64_adhoc(
    zero64,
    'f',
    'mov x0, xzr');
assertEq(wasmEvalText(zero64).exports.f(-37000000000n), 0n)
assertEq(wasmEvalText(zero64).exports.f(42000000000n), 0n)

// Test that multiplication by one yields no code (this optimization currently
// exists both in constant folding and in lowering).

let one32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 1))))`;
codegenTestARM64_adhoc(
    one32,
    'f',
    '');
assertEq(wasmEvalText(one32).exports.f(-37), -37)
assertEq(wasmEvalText(one32).exports.f(42), 42)

let one64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 1))))`
codegenTestARM64_adhoc(
    one64,
    'f',
    '');
assertEq(wasmEvalText(one64).exports.f(-37000000000n), -37000000000n)
assertEq(wasmEvalText(one64).exports.f(42000000000n), 42000000000n)

// Test that multiplication by two yields an add

let double32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 2))))`;
codegenTestARM64_adhoc(
    double32,
    'f',
    'add w0, w0, w0');
assertEq(wasmEvalText(double32).exports.f(-37), -74)
assertEq(wasmEvalText(double32).exports.f(42), 84)

let double64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 2))))`
codegenTestARM64_adhoc(
    double64,
    'f',
    'add x0, x0, x0');
assertEq(wasmEvalText(double64).exports.f(-37000000000n), -74000000000n)
assertEq(wasmEvalText(double64).exports.f(42000000000n), 84000000000n)

// Test that multiplication by four yields a shift

let quad32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 4))))`;
codegenTestARM64_adhoc(
    quad32,
    'f',
    'lsl w0, w0, #2');
assertEq(wasmEvalText(quad32).exports.f(-37), -148)
assertEq(wasmEvalText(quad32).exports.f(42), 168)

let quad64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 4))))`
codegenTestARM64_adhoc(
    quad64,
    'f',
    'lsl x0, x0, #2');
assertEq(wasmEvalText(quad64).exports.f(-37000000000n), -148000000000n)
assertEq(wasmEvalText(quad64).exports.f(42000000000n), 168000000000n)

// Test that multiplication by five yields an add with a shifted operand for
// i32, and a multiply for i64

let quint32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 5))))`;
codegenTestARM64_adhoc(
    quint32,
    'f',
    'add     w0, w0, w0, lsl #2');
assertEq(wasmEvalText(quint32).exports.f(-37), -37*5)
assertEq(wasmEvalText(quint32).exports.f(42), 42*5)

// Test that multiplication by ten and twelve yields an add with a shifted
// operand followed by a shift

let ten32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 10))))`;
codegenTestARM64_adhoc(
    ten32,
    'f',
    `add     w0, w0, w0, lsl #2
     lsl     w0, w0, #1`);
assertEq(wasmEvalText(ten32).exports.f(-37), -37*10)
assertEq(wasmEvalText(ten32).exports.f(42), 42*10)

let twelve32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 12))))`;
codegenTestARM64_adhoc(
    twelve32,
    'f',
    `add     w0, w0, w0, lsl #1
     lsl     w0, w0, #2`);
assertEq(wasmEvalText(twelve32).exports.f(-37), -37*12)
assertEq(wasmEvalText(twelve32).exports.f(42), 42*12)

// Test that multiplication by a constant with three bits set yields a multiply

let eleven32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 11))))`;
codegenTestARM64_adhoc(
    eleven32,
    'f',
    `mov     w16, #0xb
     mul     w0, w0, w16`);
assertEq(wasmEvalText(eleven32).exports.f(-37), -37*11)
assertEq(wasmEvalText(eleven32).exports.f(42), 42*11)

// Test that results wrap around on overflow

let big32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 0x40000001))))`;
codegenTestARM64_adhoc(
    big32,
    'f',
    'add     w0, w0, w0, lsl #30');
assertEq(wasmEvalText(big32).exports.f(-37), Math.imul(-37, 0x40000001))
assertEq(wasmEvalText(big32).exports.f(42), Math.imul(42, 0x40000001))

let quint64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 5))))`
codegenTestARM64_adhoc(
    quint64,
    'f',
    `mov     x16, #0x5
     mul     x0, x0, x16`);
assertEq(wasmEvalText(quint64).exports.f(-37000000000n), -37000000000n*5n)
assertEq(wasmEvalText(quint64).exports.f(42000000000n), 42000000000n*5n)

// Test that add/sub/and/or/xor don't need to reuse their input register.  The
// proof here is that the destination register does not equal any of the input
// registers.
//
// We have adequate functionality tests for these elsewhere, so test only
// codegen here.

for ( [op, imm, expectVar, expectImm] of
      [['and', 64,
        'and     x0, x1, x2',
        'and     x0, x1, #0x40'],
       ['or', 64,
        'orr     x0, x1, x2',
        'orr     x0, x1, #0x40'],
       ['xor', 64,
        'eor     x0, x1, x2',
        'eor     x0, x1, #0x40'],
       ['add', 64,
        'add     x0, x1, x2',
        'add     x0, x1, #0x40 \\(64\\)'],
       ['sub', 64,
        'sub     x0, x1, x2',
        'sub     x0, x1, #0x40 \\(64\\)']] ) {
    codegenTestARM64_adhoc(`
(module
  (func (export "f") (param i64) (param i64) (param i64) (result i64)
    (i64.${op} (local.get 1) (local.get 2))))`,
                           'f',
                           expectVar);
    codegenTestARM64_adhoc(`
(module
  (func (export "f") (param i64) (param i64) (result i64)
    (i64.${op} (local.get 1) (i64.const ${imm}))))`,
                           'f',
                           expectImm);
}

// Test that shifts and rotates with a constant don't need to reuse their input
// register.  The proof here is that the destination register does not equal any
// of the input registers.
//
// We have adequate functionality tests for these elsewhere, so test only
// codegen here.

for ( [op, expect] of
      [['shl',   'lsl     x0, x1, #2'],
       ['shr_s', 'asr     x0, x1, #2'],
       ['shr_u', 'lsr     x0, x1, #2'],
       ['rotl',  'ror     x0, x1, #62'],
       ['rotr',  'ror     x0, x1, #2']] ) {
    codegenTestARM64_adhoc(`
(module
  (func (export "f") (param i64) (param i64) (result i64)
    (i64.${op} (local.get 1) (i64.const 2))))`,
                           'f',
                           expect);
}

// Test that 0-n yields negation.

let subneg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.sub (i32.const 0) (local.get 0))))`
codegenTestARM64_adhoc(
    subneg32,
    'f',
    'neg w0, w0');
assertEq(wasmEvalText(subneg32).exports.f(-37), 37)
assertEq(wasmEvalText(subneg32).exports.f(42), -42)

let subneg64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.sub (i64.const 0) (local.get 0))))`
codegenTestARM64_adhoc(
    subneg64,
    'f',
    'neg x0, x0');
assertEq(wasmEvalText(subneg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(subneg64).exports.f(42000000000n), -42000000000n)

// An AND with a complemented operand is fused into a single BIC.

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i64 i64) (result i64)
         (i64.and (local.get 0)
                  (i64.xor (local.get 1) (i64.const -1)))))`,
    "f",
    `bic     x0, x0, x1`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i64 i64) (result i64)
         (i64.and (i64.xor (local.get 1) (i64.const -1))
                  (local.get 0))))`,
    "f",
    `bic     x0, x0, x1`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i32 i32) (result i32)
         (i32.and (local.get 0)
                  (i32.xor (local.get 1) (i32.const -1)))))`,
    "f",
    `bic     w0, w0, w1`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i32 i32) (result i32)
         (i32.and (i32.xor (local.get 1) (i32.const -1))
                  (local.get 0))))`,
    "f",
    `bic     w0, w0, w1`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i32 i32) (result i32)
         (if (result i32)
           (i32.and (local.get 0)
                    (i32.xor (local.get 1) (i32.const -1)))
           (then (i32.const 1))
           (else (i32.const 0)))))`,
    "f",
    `bic     w0, w0, w1
     cbz     w0, #\\+0xc \\(addr .*\\)
     mov     w0, #0x1
     b       #\\+0x8 \\(addr .*\\)
     mov     w0, #0x0`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i32 i32) (result i32)
         (if (result i32)
           (i32.eq (i32.and (local.get 0)
                            (i32.xor (local.get 1) (i32.const -1)))
                   (i32.const 0))
           (then (i32.const 1))
           (else (i32.const 0)))))`,
    "f",
    `bic     w0, w0, w1
     cbnz    w0, #\\+0xc \\(addr .*\\)
     mov     w0, #0x1
     b       #\\+0x8 \\(addr .*\\)
     mov     w0, #0x0`);

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i64 i64) (result i64)
         (if (result i64)
           (i64.eq (i64.and (local.get 0)
                            (i64.xor (local.get 1) (i64.const -1)))
                   (i64.const 0))
           (then (i64.const 1))
           (else (i64.const 0)))))`,
    "f",
    `bic     x0, x0, x1
     cbnz    x0, #\\+0xc \\(addr .*\\)
     mov     x0, #0x1
     b       #\\+0x8 \\(addr .*\\)
     mov     x0, #0x0`);

// Test that select does something reasonable and does not tie its output to one
// of its inputs.

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param i64) (param i64) (param i64) (param i32) (result i64)
         (select (local.get 1) (local.get 2) (local.get 3))))`,
    'f',
    `tst     w3, w3
     csel    x0, x1, x2, ne`)

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param f64) (param f64) (param f64) (param i32) (result f64)
         (select (local.get 1) (local.get 2) (local.get 3))))`,
    'f',
    `tst     w0, w0
     fcsel   d0, d1, d2, ne`)

// Here we test that no boolean is generated and then re-tested, and that
// operands are swapped so that we can use an immediate constant, and that the
// input is not tied to the output.

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param $a i32) (param $b i32) (param $c i32) (param $d i32) (result i32)
         (select (local.get $b) (local.get $d) (i32.lt_s (i32.const 0) (local.get $c)))))`,
    'f',
    `cmp     w2, #0x0 \\(0\\)
     csel    w0, w1, w3, gt`)

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param $a f64) (param $b f64) (param $c f64) (param $d f64) (result f64)
         (select (local.get $b) (local.get $d) (f64.lt (f64.const 0) (local.get $c)))))`,
    'f',
    `movi    d0, #0x0
     fcmp    d0, d2
     fcsel   d0, d1, d3, lo`)

// FP ABS should not tie its input to its output.

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param f32) (param f32) (result f32)
         (f32.abs (local.get 1))))`,
    'f',
    'fabs    s0, s1');

codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param f64) (param f64) (result f64)
         (f64.abs (local.get 1))))`,
    'f',
    'fabs    d0, d1');

// AND{32,64} followed by `== 0`: check the two operations are merged into a
// single 'tst' insn, and no 'and' insn.  The merging isn't done for
// {OR,XOR}{32,64}.  This is for both arguments being non-constant.

for ( [ty, expect_tst] of
      [['i32',   'tst w0, w1'],
       ['i64',   'tst x0, x1']] ) {
   codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param $p1 ${ty}) (param $p2 ${ty}) (result i32)
         (local $x i32)
         (local.set $x (i32.const 0x4D2))
         (if (${ty}.eq (${ty}.and (local.get $p1) (local.get $p2))
                       (${ty}.const 0))
           (then (local.set $x (i32.const 0x11D7)))
         )
         (local.get $x)
       )
    )`,
    'f',
    `${expect_tst}
     b\\.ne  #\\+0xc \\(addr .*\\)
     mov     w0, #0x11d7
     b       #\\+0x8 \\(addr .*\\)
     mov     w0, #0x4d2`
   );
}

// AND64 followed by `== 0`, with one of the args being a constant.

for ( [imm, expect1, expect2] of
      [ // as a valid logical-immediate => imm in insn
        ['0x0F0F0F0F0F0F0F0F',
         'tst  x0, #0xf0f0f0f0f0f0f0f',
         ''],
        // anything else => imm synth'd into a reg
        ['-0x4771',
         'mov  x16, #0xffffffffffffb88f',
         'tst  x0, x16']]
      ) {
   codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param $p1 i64) (result i32)
         (local $x i32)
         (local.set $x (i32.const 0x4D2))
         (if (i64.eq (i64.and (i64.const ${imm}) (local.get $p1))
                     (i64.const 0))
           (then (local.set $x (i32.const 0x11D7)))
         )
         (local.get $x)
       )
    )`,
    'f',
    `${expect1}
     ${expect2}
     b\\.ne  #\\+0xc \\(addr .*\\)
     mov     w0, #0x11d7
     b       #\\+0x8 \\(addr .*\\)
     mov     w0, #0x4d2`
   );
}

// For integer comparison followed by select, check that the comparison result
// isn't materialised into a register, for specific types.

for ( [cmpTy, cmpOp, selTy, cmpRegPfx, cselRegPfx, armCC] of
      [ ['i32', 'le_s', 'i32',  'w', 'w', 'le'],
        ['i32', 'lt_u', 'i64',  'w', 'x', 'lo'],
        ['i64', 'le_s', 'i32',  'x', 'w', 'le'],
        ['i64', 'lt_u', 'i64',  'x', 'x', 'lo'],
      ] ) {
   codegenTestARM64_adhoc(
    `(module
       (func (export "f")
             (param $p1 ${cmpTy}) (param $p2 ${cmpTy})
             (param $p3 ${selTy}) (param $p4 ${selTy})
             (result ${selTy})
         (select (local.get $p3)
                 (local.get $p4)
                 (${cmpTy}.${cmpOp} (local.get $p1) (local.get $p2)))
       )
    )`,
    'f',
    `cmp  ${cmpRegPfx}0, ${cmpRegPfx}1
     csel ${cselRegPfx}0, ${cselRegPfx}2, ${cselRegPfx}3, ${armCC}`
   );
}
