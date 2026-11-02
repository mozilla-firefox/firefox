// |jit-test| test-also=--wasm-compiler=optimizing; skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("loong64"); include:codegen-loong64-test.js

const NopIns = `andi         \\$zero, \\$zero, 0x0`;

// Test that small i64 constants in bin-ops are folded into {AND,OR,XOR}I.

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0xff))))`,
    "f",
    `andi           \\$a0, \\$a0, 0xff`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.or (local.get 0) (i64.const 0x100))))`,
    "f",
    `ori            \\$a0, \\$a0, 0x100`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.xor (local.get 0) (i64.const 0x1f))))`,
    "f",
    `xori           \\$a0, \\$a0, 0x1f`);

// Test that small i32 constants in bin-ops are folded into {AND,OR,XOR}I.

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.and (local.get 0) (i32.const 0xff))))`,
    "f",
    `andi           \\$a0, \\$a0, 0xff`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.or (local.get 0) (i32.const 0x100))))`,
    "f",
    `ori            \\$a0, \\$a0, 0x100`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.xor (local.get 0) (i32.const 0x1f))))`,
    "f",
    `xori           \\$a0, \\$a0, 0x1f`);

// A contiguous run of one-bits is applied with BSTRPICK.D and optional SLLI.D.

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0xffffffff))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x1f, 0x0`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0x7fffffff))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x1e, 0x0`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0xff00))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0xf, 0x8
     slli.d         \\$a0, \\$a0, 0x8`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0xffff0000))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x1f, 0x10
     slli.d         \\$a0, \\$a0, 0x10`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.and (local.get 0) (i64.const 0xffffffffffffff00))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x3f, 0x8
     slli.d         \\$a0, \\$a0, 0x8`);

// A contiguous run of one-bits in an i32 mask is applied with BSTRPICK.D
// followed by an optional SLLI.D.

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.and (local.get 0) (i32.const 0x7fffffff))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x1e, 0x0`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.and (local.get 0) (i32.const 0xff00))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0xf, 0x8
     slli.d         \\$a0, \\$a0, 0x8`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.and (local.get 0) (i32.const 0xffff0000))))`,
    "f",
    `bstrpick\\.d   \\$a0, \\$a0, 0x3f, 0x10
     slli.d         \\$a0, \\$a0, 0x10`);

// Test that signed comparisons against negative constants use SLTI directly.

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i32)
         (i64.lt_s (local.get 0) (i64.const -100))))`,
    "f",
    `slti           \\$a0, \\$a0, -100`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i32)
         (i64.ge_s (local.get 0) (i64.const -100))))`,
    "f",
    `slti           \\$a0, \\$a0, -100
     xori           \\$a0, \\$a0, 0x1`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i32)
         (i64.gt_s (local.get 0) (i64.const -100))))`,
    "f",
    `slti           \\$a0, \\$a0, -99
     xori           \\$a0, \\$a0, 0x1`);

// Test that sign extension of the low byte/halfword uses a single EXT.W.[BH].

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.extend8_s (local.get 0))))`,
    "f",
    `ext\\.w\\.b    \\$a0, \\$a0`);

codegenTestLOONG64_adhoc(
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.extend16_s (local.get 0))))`,
    "f",
    `ext\\.w\\.h    \\$a0, \\$a0`);

// Test that multiplication by -1 yields negation.

let neg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const -1))))`;
codegenTestLOONG64_adhoc(
    neg32,
    "f",
    `sub.w           \\$a0, \\$zero, \\$a0`);
assertEq(wasmEvalText(neg32).exports.f(-37), 37)
assertEq(wasmEvalText(neg32).exports.f(42), -42)

let neg64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const -1))))`;
codegenTestLOONG64_adhoc(
    neg64,
    "f",
    `sub.d           \\$a0, \\$zero, \\$a0`);
assertEq(wasmEvalText(neg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(neg64).exports.f(42000000000n), -42000000000n)

// Test that multiplication by zero yields zero.

let zero32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 0))))`;
codegenTestLOONG64_adhoc(
    zero32,
    "f",
    `slli.w          \\$a0, \\$zero, 0x0`);
assertEq(wasmEvalText(zero32).exports.f(-37), 0)
assertEq(wasmEvalText(zero32).exports.f(42), 0)

let zero64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 0))))`;
codegenTestLOONG64_adhoc(
    zero64,
    "f",
    `or              \\$a0, \\$zero, \\$zero`);
assertEq(wasmEvalText(zero64).exports.f(-37000000000n), 0n)
assertEq(wasmEvalText(zero64).exports.f(42000000000n), 0n)

// Test that multiplication by one yields no code.

let one32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 1))))`;
codegenTestLOONG64_adhoc(
    one32,
    "f",
    '');
assertEq(wasmEvalText(one32).exports.f(-37), -37)
assertEq(wasmEvalText(one32).exports.f(42), 42)

let one64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 1))))`;
codegenTestLOONG64_adhoc(
    one64,
    "f",
    '');
assertEq(wasmEvalText(one64).exports.f(-37000000000n), -37000000000n)
assertEq(wasmEvalText(one64).exports.f(42000000000n), 42000000000n)

// Test that multiplication by two yields an ADD.

let double32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 2))))`;
codegenTestLOONG64_adhoc(
    double32,
    "f",
    `add.w           \\$a0, \\$a0, \\$a0`);
assertEq(wasmEvalText(double32).exports.f(-37), -74)
assertEq(wasmEvalText(double32).exports.f(42), 84)

let double64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 2))))`;
codegenTestLOONG64_adhoc(
    double64,
    "f",
    `add.d           \\$a0, \\$a0, \\$a0`);
assertEq(wasmEvalText(double64).exports.f(-37000000000n), -74000000000n)
assertEq(wasmEvalText(double64).exports.f(42000000000n), 84000000000n)

// Test that multiplication by four yields a shift.

let quad32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 4))))`;
codegenTestLOONG64_adhoc(
    quad32,
    "f",
    `slli.w          \\$a0, \\$a0, 0x2`);
assertEq(wasmEvalText(quad32).exports.f(-37), -148)
assertEq(wasmEvalText(quad32).exports.f(42), 168)

let quad64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 4))))`;
codegenTestLOONG64_adhoc(
    quad64,
    "f",
    `slli.d          \\$a0, \\$a0, 0x2`);
assertEq(wasmEvalText(quad64).exports.f(-37000000000n), -148000000000n)
assertEq(wasmEvalText(quad64).exports.f(42000000000n), 168000000000n)

// Test that multiplication by five yields an alsl.

let quint32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 5))))`;
codegenTestLOONG64_adhoc(
    quint32,
    "f",
    `alsl.w          \\$a0, \\$a0, \\$a0, 0x2`);
assertEq(wasmEvalText(quint32).exports.f(-37), -185)
assertEq(wasmEvalText(quint32).exports.f(42), 210)

let quint64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 5))))`;
codegenTestLOONG64_adhoc(
    quint64,
    "f",
    `alsl.d          \\$a0, \\$a0, \\$a0, 0x2`);
assertEq(wasmEvalText(quint64).exports.f(-37000000000n), -185000000000n)
assertEq(wasmEvalText(quint64).exports.f(42000000000n), 210000000000n)

// Test that multiplication by six yields a shift-add.

let sext32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 6))))`;
codegenTestLOONG64_adhoc(
    sext32,
    "f",
    `slli.w          \\$t6, \\$a0, 0x1
     add.w           \\$a0, \\$t6, \\$a0
     slli.w          \\$a0, \\$a0, 0x1`);
assertEq(wasmEvalText(sext32).exports.f(-37), -222)
assertEq(wasmEvalText(sext32).exports.f(42), 252)

let sext64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 6))))`;
codegenTestLOONG64_adhoc(
    sext64,
    "f",
    `slli.d          \\$t6, \\$a0, 0x1
     add.d           \\$a0, \\$t6, \\$a0
     slli.d          \\$a0, \\$a0, 0x1`);
assertEq(wasmEvalText(sext64).exports.f(-37000000000n), -222000000000n)
assertEq(wasmEvalText(sext64).exports.f(42000000000n), 252000000000n)

// Test that multiplication by UINT32_MAX yields x*2^32 - x.

let uint32max64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 0xffffffff))))`;
codegenTestLOONG64_adhoc(
    uint32max64,
    "f",
    `slli.d          \\$t6, \\$a0, 0x20
     sub.d           \\$a0, \\$t6, \\$a0`);
assertEq(wasmEvalText(uint32max64).exports.f(-37000000000n), BigInt.asIntN(64, -37000000000n * 0xffffffffn))
assertEq(wasmEvalText(uint32max64).exports.f(42000000000n), BigInt.asIntN(64, 42000000000n * 0xffffffffn))

// Test that 0-n yields negation.

let subneg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.sub (i32.const 0) (local.get 0))))`;
codegenTestLOONG64_adhoc(
    subneg32,
    "f",
    `sub.w           \\$a0, \\$zero, \\$a0`);
assertEq(wasmEvalText(subneg32).exports.f(-37), 37)
assertEq(wasmEvalText(subneg32).exports.f(42), -42)

let subneg64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.sub (i64.const 0) (local.get 0))))`;
codegenTestLOONG64_adhoc(
    subneg64,
    "f",
    `sub.d           \\$a0, \\$zero, \\$a0`);
assertEq(wasmEvalText(subneg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(subneg64).exports.f(42000000000n), -42000000000n)

// AND followed by `== 0`: check the two operations are merged into a single
// 'AND' + branch on the result, and no comparison result is materialized.

function andEq0(ty, const0) {
    return `(module
       (func (export "f") (param $p1 ${ty}) (param $p2 ${ty}) (result i32)
         (if (result i32) (${ty}.eq (${ty}.and (local.get $p1) (local.get $p2))
                                   (${ty}.const ${const0}))
           (then (i32.const 1))
           (else (i32.const 0)))))`;
}
for ( [ty, const0, a, b, c, d] of
      [['i32', '0', 0, 3, 4, 0x100], ['i64', '0', 0n, 3n, 4n, 0x100n]] ) {
  let m = andEq0(ty, const0);
  codegenTestLOONG64_adhoc(
      m,
      "f",
      `and             \\$t6, \\$a0, \\$a1
       bnez            \\$t6, \\d+ -> ${HEX}+
       bge             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 1
       beq             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 0`);
  assertEq(wasmEvalText(m).exports.f(a, a), 1)
  assertEq(wasmEvalText(m).exports.f(b, c), 1)
  assertEq(wasmEvalText(m).exports.f(d, d), 0)
}

// The same with the constant being a contiguous run of one-bits.
// The final shift is not needed when compared against zero.

function andEq0Mask(ty, imm) {
    return `(module
       (func (export "f") (param $p1 ${ty}) (result i32)
         (if (result i32) (${ty}.eq (${ty}.and (local.get $p1) (${ty}.const ${imm}))
                                  (${ty}.const 0))
           (then (i32.const 1))
           (else (i32.const 0)))))`;
}
for ( [ty, imm, expect] of
      [ // low-byte mask => ANDI
        ['i64', '0xff',
         `andi            \\$t6, \\$a0, 0xff`],
        // low-word mask => BSTRPICK.D
        ['i64', '0xffffffff',
         `bstrpick\\.d    \\$t6, \\$a0, 0x1f, 0x0`],
        // non-low contiguous mask => BSTRPICK.D
        ['i64', '0xff00',
         `bstrpick\\.d    \\$t6, \\$a0, 0xf, 0x8`],
        // same for i32
        ['i32', '0xff00',
         `bstrpick\\.d    \\$t6, \\$a0, 0xf, 0x8`]] ) {
  let m = andEq0Mask(ty, imm);
  codegenTestLOONG64_adhoc(
      m,
      "f",
      `${expect}
       bnez            \\$t6, \\d+ -> ${HEX}+
       bge             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 1
       beq             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 0`);
}
assertEq(wasmEvalText(andEq0Mask('i64', '0xff')).exports.f(0x1ffn), 0)
assertEq(wasmEvalText(andEq0Mask('i64', '0xff')).exports.f(0x100n), 1)
assertEq(wasmEvalText(andEq0Mask('i64', '0xffffffff')).exports.f(0x100000000n), 1)
assertEq(wasmEvalText(andEq0Mask('i64', '0xff00')).exports.f(0x10000n), 1)
assertEq(wasmEvalText(andEq0Mask('i32', '0xff00')).exports.f(0x10000), 1)

// The same for Signed/NotSigned branches on a masked value.

function andCmpSign0(ty, cmpOp, imm) {
    return `(module
       (func (export "f") (param $p1 ${ty}) (result i32)
         (if (result i32) (${ty}.${cmpOp} (${ty}.and (local.get $p1) (${ty}.const ${imm}))
                                        (${ty}.const 0))
           (then (i32.const 1))
           (else (i32.const 0)))))`;
}
for ( [ty, cmpOp, imm, expect] of
      [
        ['i32', 'lt_s', '0x80000000',
         `bstrpick\\.d    \\$a0, \\$a0, 0x3f, 0x1f
          slli.d          \\$a0, \\$a0, 0x1f
          bge             \\$a0, \\$zero, \\d+ -> ${HEX}+`],
        ['i64', 'lt_s', '0x8000000000000000',
         `bstrpick\\.d    \\$a0, \\$a0, 0x3f, 0x3f
          slli.d          \\$a0, \\$a0, 0x3f
          bge             \\$a0, \\$zero, \\d+ -> ${HEX}+`],
        ['i32', 'ge_s', '0x80000000',
         `bstrpick\\.d    \\$a0, \\$a0, 0x3f, 0x1f
          slli.d          \\$a0, \\$a0, 0x1f
          blt             \\$a0, \\$zero, \\d+ -> ${HEX}+`],
        ['i64', 'ge_s', '0x8000000000000000',
         `bstrpick\\.d    \\$a0, \\$a0, 0x3f, 0x3f
          slli.d          \\$a0, \\$a0, 0x3f
          blt             \\$a0, \\$zero, \\d+ -> ${HEX}+`],
        ['i32', 'lt_s', '0x7fffffff',
         `bstrpick\\.d    \\$a0, \\$a0, 0x1e, 0x0
          bge             \\$a0, \\$zero, \\d+ -> ${HEX}+`]] ) {
  let m = andCmpSign0(ty, cmpOp, imm);
  codegenTestLOONG64_adhoc(
      m,
      "f",
      `${expect}
       bge             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 1
       beq             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 0`);
}
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x80000000')).exports.f(-1), 1)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x80000000')).exports.f(1), 0)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x80000000')).exports.f(0x80000000 | 0), 1)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x80000000')).exports.f(0x7fffffff), 0)
assertEq(wasmEvalText(andCmpSign0('i64', 'lt_s', '0x8000000000000000')).exports.f(-1n), 1)
assertEq(wasmEvalText(andCmpSign0('i64', 'lt_s', '0x8000000000000000')).exports.f(1n), 0)
assertEq(wasmEvalText(andCmpSign0('i64', 'lt_s', '0x8000000000000000')).exports.f(0x8000000000000000n), 1)
assertEq(wasmEvalText(andCmpSign0('i64', 'lt_s', '0x8000000000000000')).exports.f(0x7fffffffffffffffn), 0)
assertEq(wasmEvalText(andCmpSign0('i32', 'ge_s', '0x80000000')).exports.f(-1), 0)
assertEq(wasmEvalText(andCmpSign0('i32', 'ge_s', '0x80000000')).exports.f(1), 1)
assertEq(wasmEvalText(andCmpSign0('i64', 'ge_s', '0x8000000000000000')).exports.f(-1n), 0)
assertEq(wasmEvalText(andCmpSign0('i64', 'ge_s', '0x8000000000000000')).exports.f(1n), 1)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x7fffffff')).exports.f(-1), 0)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x7fffffff')).exports.f(0x7fffffff), 0)
assertEq(wasmEvalText(andCmpSign0('i32', 'lt_s', '0x7fffffff')).exports.f(0), 0)

// The same with one of the args being a constant.  Depending on the constant
// we get one of three materialization forms.

function andEq0Const(imm) {
    return `(module
       (func (export "f") (param $p1 i64) (result i32)
         (if (result i32) (i64.eq (i64.and (i64.const ${imm}) (local.get $p1))
                                  (i64.const 0))
           (then (i32.const 1))
           (else (i32.const 0)))))`;
}
for ( [imm, expect] of
      [ // in signed-32 range => LU12I.W + ORI
        ['0x17654321',
         `lu12i.w         \\$t6, 95828
          ori             \\$t6, \\$t6, 0x321`],
        // in unsigned-32 range => LU12I.W + BSTRINS.D + ORI
        ['0x87654321',
         `lu12i.w         \\$t6, -493996
          bstrins.d       \\$t6, \\$zero, 0x3f, 0x20
          ori             \\$t6, \\$t6, 0x321`],
        // not in either range => LU12I.W + LU32I.D + ORI
        ['0x187654321',
         `lu12i.w         \\$t6, -493996
          lu32i.d         \\$t6, 1
          ori             \\$t6, \\$t6, 0x321`]] ) {
  codegenTestLOONG64_adhoc(
      andEq0Const(imm),
      "f",
      `${expect}
       and             \\$t6, \\$a0, \\$t6
       bnez            \\$t6, \\d+ -> ${HEX}+
       bge             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 1
       beq             \\$zero, \\$zero, \\d+ -> ${HEX}+
       ${NopIns}
       addi.w          \\$a0, \\$zero, 0`);
}
assertEq(wasmEvalText(andEq0Const('0x17654321')).exports.f(0n), 1)
assertEq(wasmEvalText(andEq0Const('0x17654321')).exports.f(0x17654321n), 0)
assertEq(wasmEvalText(andEq0Const('0x187654321')).exports.f(0x40000000n), 1)

// For integer comparison followed by select, check the comparison is
// evaluated into a register and then applied with MASKEQZ/MASKNEZ.

function cmpSel32vs64(cmpTy, cmpOp, selTy) {
    return `(module
              (func (export "f")
                    (param $p1 ${cmpTy}) (param $p2 ${cmpTy})
                    (param $p3 ${selTy}) (param $p4 ${selTy})
                    (result ${selTy})
                (select (local.get $p3)
                        (local.get $p4)
                        (${cmpTy}.${cmpOp} (local.get $p1) (local.get $p2)))
              )
            )`;
}
for ( [cmpTy, cmpOp, selTy, insns] of
      [ ['i32', 'le_s', 'i32',
         `slt             \\$t6, \\$a1, \\$a0
          xori            \\$t6, \\$t6, 0x1`],
        ['i32', 'lt_u', 'i64',
         `sltu            \\$t6, \\$a0, \\$a1`],
        ['i64', 'le_s', 'i32',
         `slt             \\$t6, \\$a1, \\$a0
          xori            \\$t6, \\$t6, 0x1`],
        ['i64', 'lt_u', 'i64',
         `sltu            \\$t6, \\$a0, \\$a1`]
      ] ) {
  let m = cmpSel32vs64(cmpTy, cmpOp, selTy);
  codegenTestLOONG64_adhoc(
      m,
      "f",
      `${insns}
       maskeqz         \\$a0, \\$a2, \\$t6
       masknez         \\$t6, \\$a3, \\$t6
       or              \\$a0, \\$a0, \\$t6`);
}
assertEq(wasmEvalText(cmpSel32vs64('i32', 'le_s', 'i32')).exports.f(1, 2, 10, 20), 10)
assertEq(wasmEvalText(cmpSel32vs64('i32', 'le_s', 'i32')).exports.f(2, 1, 10, 20), 20)
assertEq(wasmEvalText(cmpSel32vs64('i32', 'lt_u', 'i64')).exports.f(1, 2, 10n, 20n), 10n)
assertEq(wasmEvalText(cmpSel32vs64('i64', 'le_s', 'i32')).exports.f(1n, 2n, 10, 20), 10)
assertEq(wasmEvalText(cmpSel32vs64('i64', 'lt_u', 'i64')).exports.f(2n, 1n, 10n, 20n), 20n)

// For integer comparison followed by select, check correct use of operands in
// registers vs memory.  The first eight params are in registers; the ninth
// and tenth are on the stack.

function select8(selArgT, selArgF, cmpArgL, cmpArgR) {
    return `(module
       (func (export "f")
             (param i64) (param i64) (param i64) (param i64) (param i64)
             (param i64) (param i64) (param i64)
             (result i64)
         (select (local.get ${selArgT}) (local.get ${selArgF})
                 (i64.eq (local.get ${cmpArgL}) (local.get ${cmpArgR})))))`;
}

let selRR = select8(2, 3, 0, 1);
codegenTestLOONG64_adhoc(
    selRR,
    "f",
    `xor             \\$t6, \\$a0, \\$a1
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a2, \\$t6
     masknez         \\$t6, \\$a3, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selRR).exports.f(5n, 5n, 10n, 20n, 0n, 0n, 0n, 0n), 10n)
assertEq(wasmEvalText(selRR).exports.f(5n, 6n, 10n, 20n, 0n, 0n, 0n, 0n), 20n)

let selRM = select8(2, 6, 0, 4);
codegenTestLOONG64_adhoc(
    selRM,
    "f",
    `xor             \\$t6, \\$a0, \\$a4
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a2, \\$t6
     masknez         \\$t6, \\$a6, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selRM).exports.f(5n, 0n, 10n, 0n, 5n, 0n, 20n, 0n), 10n)
assertEq(wasmEvalText(selRM).exports.f(5n, 0n, 10n, 0n, 6n, 0n, 20n, 0n), 20n)

let selMR = select8(6, 3, 4, 1);
codegenTestLOONG64_adhoc(
    selMR,
    "f",
    `xor             \\$t6, \\$a4, \\$a1
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a6, \\$t6
     masknez         \\$t6, \\$a3, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selMR).exports.f(0n, 5n, 0n, 20n, 5n, 0n, 10n, 0n), 10n)
assertEq(wasmEvalText(selMR).exports.f(0n, 5n, 0n, 20n, 6n, 0n, 10n, 0n), 20n)

let selMM = select8(6, 7, 4, 5);
codegenTestLOONG64_adhoc(
    selMM,
    "f",
    `xor             \\$t6, \\$a4, \\$a5
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a6, \\$t6
     masknez         \\$t6, \\$a7, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selMM).exports.f(0n, 0n, 0n, 0n, 5n, 5n, 10n, 20n), 10n)
assertEq(wasmEvalText(selMM).exports.f(0n, 0n, 0n, 0n, 5n, 6n, 10n, 20n), 20n)

// A stack-resident select operand or comparison operand is loaded first.

function select10(selArgT, selArgF, cmpArgL, cmpArgR) {
    return `(module
       (func (export "f")
             (param i64) (param i64) (param i64) (param i64) (param i64)
             (param i64) (param i64) (param i64) (param i64) (param i64)
             (result i64)
         (select (local.get ${selArgT}) (local.get ${selArgF})
                 (i64.eq (local.get ${cmpArgL}) (local.get ${cmpArgR})))))`;
}

let selStack = select10(2, 8, 0, 1);
codegenTestLOONG64_adhoc(
    selStack,
    "f",
    `ld.d            \\$a3, \\$fp, 32
     xor             \\$t6, \\$a0, \\$a1
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a2, \\$t6
     masknez         \\$t6, \\$a3, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selStack).exports.f(5n, 5n, 10n, 0n, 0n, 0n, 0n, 0n, 20n, 0n), 10n)
assertEq(wasmEvalText(selStack).exports.f(5n, 6n, 10n, 0n, 0n, 0n, 0n, 0n, 20n, 0n), 20n)

let selStackCmp = select10(2, 3, 8, 1);
codegenTestLOONG64_adhoc(
    selStackCmp,
    "f",
    `ld.d            \\$a0, \\$fp, 32
     xor             \\$t6, \\$a0, \\$a1
     sltui           \\$t6, \\$t6, 1
     maskeqz         \\$a0, \\$a2, \\$t6
     masknez         \\$t6, \\$a3, \\$t6
     or              \\$a0, \\$a0, \\$t6`);
assertEq(wasmEvalText(selStackCmp).exports.f(5n, 5n, 10n, 20n, 0n, 0n, 0n, 0n, 5n, 0n), 10n)
assertEq(wasmEvalText(selStackCmp).exports.f(5n, 6n, 10n, 20n, 0n, 0n, 0n, 0n, 5n, 0n), 20n)
