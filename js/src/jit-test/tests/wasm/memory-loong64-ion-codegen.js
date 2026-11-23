// |jit-test| test-also=--wasm-compiler=optimizing --disable-wasm-huge-memory; skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("loong64"); include:codegen-loong64-test.js

// Test that loads/stores at constant offsets yield expected code.
//
// WASM HeapReg is "$s7".

// Friendly si12 offsets.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 1000))))`,
    "f",
    `ld\\.w         \\$a0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load offset=1000 (i32.const 1000))))`,
    "f",
    `ld\\.w         \\$a0, \\$s7, 2000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store (i32.const 1000) (local.get 0))))`,
    "f",
    `st\\.w         \\$a0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store offset=1000 (i32.const 1000) (local.get 0))))`,
    "f",
    `st\\.w         \\$a0, \\$s7, 2000`);

// Friendly aligned si16 offsets can use ldptr.w.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 4000))))`,
    "f",
    `ldptr\\.w      \\$a0, \\$s7, 4000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load offset=2000 (i32.const 4000))))`,
    "f",
    `ldptr\\.w      \\$a0, \\$s7, 6000`);

// Offsets representable as two si12 immediates.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load8_u (i32.const 4000))))`,
    "f",
    `addi\\.d       \\$a0, \\$s7, 2000
     ld\\.bu        \\$a0, \\$a0, 2000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 4001))))`,
    "f",
    `addi\\.d       \\$a0, \\$s7, 2000
     ld\\.w         \\$a0, \\$a0, 2001`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i64)
         (i64.load32_u (i32.const 4000))))`,
    "f",
    `addi\\.d       \\$a0, \\$s7, 2000
     ld\\.wu        \\$a0, \\$a0, 2000`);

// Larger offsets are materialized in a register.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 32768))))`,
    "f",
    `lu12i\\.w      \\$a0, 8
     ldx\\.w        \\$a0, \\$a0, \\$s7`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load offset=2000 (i32.const 32768))))`,
    "f",
    `lu12i\\.w      \\$a0, 8
     ori            \\$a0, \\$a0, 0x7d0
     ldx\\.w        \\$a0, \\$a0, \\$s7`);

// Stores

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store (i32.const 4000) (local.get 0))))`,
    "f",
    `stptr\\.w      \\$a0, \\$s7, 4000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store offset=1000 (i32.const 4000) (local.get 0))))`,
    "f",
    `stptr\\.w      \\$a0, \\$s7, 5000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store (i32.const 4001) (local.get 0))))`,
    "f",
    `addi\\.d       \\$t6, \\$s7, 2000
     st\\.w         \\$a0, \\$t6, 2001`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store (i32.const 32768) (local.get 0))))`,
    "f",
    `lu12i\\.w      \\$t6, 8
     stx\\.w        \\$a0, \\$t6, \\$s7`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i32)
         (i32.store offset=1000 (i32.const 32768) (local.get 0))))`,
    "f",
    `lu12i\\.w      \\$t6, 8
     ori            \\$t6, \\$t6, 0x3e8
     stx\\.w        \\$a0, \\$t6, \\$s7`);

// Various special cases.

// Offset is the largest positive Int32 materialized with only lu12i.w.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 0x7fff_f000))))`,
    "f",
    `lu12i\\.w      \\$a0, 524287
     ldx\\.w        \\$a0, \\$a0, \\$s7`,
    {no_prefix: true});

// Adding one to the offset also requires ori.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 0x7fff_f001))))`,
    "f",
    `lu12i\\.w      \\$a0, 524287
     ori            \\$a0, \\$a0, 0x1
     ldx\\.w        \\$a0, \\$a0, \\$s7`,
    {no_prefix: true});

// Offset is INT32_MAX.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 0x7fff_ffff))))`,
    "f",
    `lu12i\\.w      \\$a0, 524287
     ori            \\$a0, \\$a0, 0xfff
     ldx\\.w        \\$a0, \\$a0, \\$s7`,
    {no_prefix: true});

// Static offset and dynamic offset are INT32_MAX.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load offset=0x7fff_ffff (i32.const 0x7fff_ffff))))`,
    "f",
    `addi\\.w       \\$t6, \\$zero, -2
     lu32i\\.d      \\$t6, 0
     ldx\\.w        \\$a0, \\$t6, \\$s7`,
    {no_prefix: true});

// The combined address exceeds UINT32_MAX.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load offset=1 (i32.const -1))))`,
    "f",
    `lu12i\\.w      \\$t6, 1
     slli\\.d       \\$t6, \\$t6, 0x14
     ldx\\.w        \\$a0, \\$t6, \\$s7`,
    {no_prefix: true});

// Offset is negative.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const -1))))`,
    "f",
    `addi\\.w       \\$t6, \\$zero, -1
     lu32i\\.d      \\$t6, 0
     ldx\\.w        \\$a0, \\$t6, \\$s7`,
    {no_prefix: true});

// Offset is INT32_MIN.
codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load (i32.const 0x8000_0000))))`,
    "f",
    `lu12i\\.w      \\$t6, -524288
     lu32i\\.d      \\$t6, 0
     ldx\\.w        \\$a0, \\$t6, \\$s7`,
    {no_prefix: true});

// Other scalar types.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i64)
         (i64.load (i32.const 1000))))`,
    "f",
    `ld\\.d         \\$a0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i64)
         (i64.load32_u (i32.const 1000))))`,
    "f",
    `ld\\.wu        \\$a0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param i64)
         (i64.store (i32.const 1000) (local.get 0))))`,
    "f",
    `st\\.d         \\$a0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result f32)
         (f32.load (i32.const 1000))))`,
    "f",
    `fld\\.s        \\$f0, \\$s7, 1000`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param f64)
         (f64.store (i32.const 1000) (local.get 0))))`,
    "f",
    `fst\\.d        \\$f0, \\$s7, 1000`);

// Other scalar types with materialized offsets.

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i32)
         (i32.load8_u (i32.const 6000))))`,
    "f",
    `lu12i\\.w      \\$a0, 1
     ori            \\$a0, \\$a0, 0x770
     ldx\\.bu       \\$a0, \\$a0, \\$s7`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result i64)
         (i64.load32_u (i32.const 6000))))`,
    "f",
    `lu12i\\.w      \\$a0, 1
     ori            \\$a0, \\$a0, 0x770
     ldx\\.wu       \\$a0, \\$a0, \\$s7`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (result f32)
         (f32.load (i32.const 6000))))`,
    "f",
    `lu12i\\.w      \\$t6, 1
     ori            \\$t6, \\$t6, 0x770
     fldx\\.s       \\$f0, \\$t6, \\$s7`);

codegenTestLOONG64_adhoc(
    `(module
       (memory 1)
       (func (export "f") (param f64)
         (f64.store (i32.const 6000) (local.get 0))))`,
    "f",
    `lu12i\\.w      \\$t6, 1
     ori            \\$t6, \\$t6, 0x770
     fstx\\.d       \\$f0, \\$t6, \\$s7`);
