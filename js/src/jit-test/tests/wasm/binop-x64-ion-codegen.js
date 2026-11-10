// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("x64") || getBuildConfiguration("simulator"); include:codegen-x64-test.js

// There may be some redundant moves to set some operations up on x64 (that's
// bug 1701164) so we avoid those with no_prefix/no_suffix flags when the issue
// comes up.

// Handle calling convention differences.
function codegenTestX64_adhoc_call(module_text, export_name, expected, options = {}) {
    // Microsoft x64 calling convention passes the first four arguments in
    // registers rcx, rdx, r8, r9 (in that order).
    if (getBuildConfiguration("windows")) {
        // Arguments passed on the stack not yet supported.
        assertEq(
            expected.includes("%r8") || expected.includes("%r9"),
            false,
            "too many arguments"
        );
        expected = expected.replaceAll("%rcx", "%r9")
                           .replaceAll("%rdx", "%r8")
                           .replaceAll("%rsi", "%rdx")
                           .replaceAll("%rdi", "%rcx")
                           .replaceAll("%esi", "%edx")
                           .replaceAll("%edi", "%ecx");
    }
    codegenTestX64_adhoc(module_text, export_name, expected, options);
}

// Test that multiplication by -1 yields negation.

let neg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const -1))))`;
codegenTestX64_adhoc(
    neg32,
    'f',
    'neg %eax', {no_prefix:true});
assertEq(wasmEvalText(neg32).exports.f(-37), 37)
assertEq(wasmEvalText(neg32).exports.f(42), -42)

let neg64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const -1))))`;
codegenTestX64_adhoc(
    neg64,
    'f',
    'neg %rax', {no_prefix:true});
assertEq(wasmEvalText(neg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(neg64).exports.f(42000000000n), -42000000000n)

// Test that multiplication by zero yields zero

let zero32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 0))))`;
codegenTestX64_adhoc(
    zero32,
    'f',
    'xor %eax, %eax');
assertEq(wasmEvalText(zero32).exports.f(-37), 0)
assertEq(wasmEvalText(zero32).exports.f(42), 0)

let zero64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 0))))`
codegenTestX64_adhoc(
    zero64,
    'f',
    'xor %rax, %rax');
assertEq(wasmEvalText(zero64).exports.f(-37000000000n), 0n)
assertEq(wasmEvalText(zero64).exports.f(42000000000n), 0n)

// Test that multiplication by one yields no code

let one32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 1))))`;
codegenTestX64_adhoc(
    one32,
    'f',
    '', {no_prefix:true});
assertEq(wasmEvalText(one32).exports.f(-37), -37)
assertEq(wasmEvalText(one32).exports.f(42), 42)

let one64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 1))))`
codegenTestX64_adhoc(
    one64,
    'f',
    '', {no_prefix:true});
assertEq(wasmEvalText(one64).exports.f(-37000000000n), -37000000000n)
assertEq(wasmEvalText(one64).exports.f(42000000000n), 42000000000n)

// Test that multiplication by two yields lea

let double32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 2))))`;
codegenTestX64_adhoc_call(
    double32,
    'f',
    'lea \\(%rdi,%rdi,1\\), %eax');
assertEq(wasmEvalText(double32).exports.f(-37), -74)
assertEq(wasmEvalText(double32).exports.f(42), 84)

let double64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 2))))`
codegenTestX64_adhoc_call(
    double64,
    'f',
    'lea \\(%rdi,%rdi,1\\), %rax');
assertEq(wasmEvalText(double64).exports.f(-37000000000n), -74000000000n)
assertEq(wasmEvalText(double64).exports.f(42000000000n), 84000000000n)

// Test that multiplication by four yields lea

let quad32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 4))))`;
codegenTestX64_adhoc_call(
    quad32,
    'f',
    'lea \\(,%rdi,4\\), %eax');
assertEq(wasmEvalText(quad32).exports.f(-37), -148)
assertEq(wasmEvalText(quad32).exports.f(42), 168)

let quad64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 4))))`
codegenTestX64_adhoc_call(
    quad64,
    'f',
    'lea \\(,%rdi,4\\), %rax');
assertEq(wasmEvalText(quad64).exports.f(-37000000000n), -148000000000n)
assertEq(wasmEvalText(quad64).exports.f(42000000000n), 168000000000n)

// Test that multiplication by five yields lea

let quint32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 5))))`;
codegenTestX64_adhoc_call(
    quint32,
    'f',
    'lea \\(%rdi,%rdi,4\\), %eax');
assertEq(wasmEvalText(quint32).exports.f(-37), -37*5)
assertEq(wasmEvalText(quint32).exports.f(42), 42*5)

let quint64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 5))))`
codegenTestX64_adhoc_call(
    quint64,
    'f',
    `lea \\(%rdi,%rdi,4\\), %rax`)
assertEq(wasmEvalText(quint64).exports.f(-37000000000n), -37000000000n*5n)
assertEq(wasmEvalText(quint64).exports.f(42000000000n), 42000000000n*5n)

// Test that multiplication by six yields imul

let sext32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.mul (local.get 0) (i32.const 6))))`;
codegenTestX64_adhoc_call(
    sext32,
    'f',
    'imul \\$0x06, %edi, %eax');
assertEq(wasmEvalText(sext32).exports.f(-37), -37*6)
assertEq(wasmEvalText(sext32).exports.f(42), 42*6)

let sext64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 6))))`
codegenTestX64_adhoc_call(
    sext64,
    'f',
    `imul \\$0x06, %rdi, %rax`)
assertEq(wasmEvalText(sext64).exports.f(-37000000000n), -37000000000n*6n)
assertEq(wasmEvalText(sext64).exports.f(42000000000n), 42000000000n*6n)

// Test that multiplication by UINT32_MAX yields imul

let uint32max64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const 0xffffffff))))`
codegenTestX64_adhoc(
    uint32max64,
    'f',
    `mov \\$-0x01, %ebx
     imul %rbx, %rax`, {no_prefix:true})
assertEq(wasmEvalText(uint32max64).exports.f(-37000000000n), BigInt.asIntN(64, -37000000000n*0xffffffffn))
assertEq(wasmEvalText(uint32max64).exports.f(42000000000n), BigInt.asIntN(64, 42000000000n*0xffffffffn))

// Test that multiplication by a power of two that doesn't fit in an int32
// yields a shift.

for (let [shift, hex] of [[31, "1F"], [40, "28"], [62, "3E"]]) {
    let pow2_64 = `(module
       (func (export "f") (param i64) (result i64)
         (i64.mul (local.get 0) (i64.const ${1n << BigInt(shift)}))))`
    codegenTestX64_adhoc(
        pow2_64,
        'f',
        `shl \\$0x${hex}, %rax`, {no_prefix:true})
    for (let x of [-37000000000n, 42000000000n]) {
        assertEq(wasmEvalText(pow2_64).exports.f(x),
                 BigInt.asIntN(64, x << BigInt(shift)))
    }
}

// Test that 0-n yields negation.

let subneg32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.sub (i32.const 0) (local.get 0))))`
codegenTestX64_adhoc(
    subneg32,
    'f',
    'neg %eax', {no_prefix:true});
assertEq(wasmEvalText(subneg32).exports.f(-37), 37)
assertEq(wasmEvalText(subneg32).exports.f(42), -42)

let subneg64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.sub (i64.const 0) (local.get 0))))`
codegenTestX64_adhoc(
    subneg64,
    'f',
    'neg %rax', {no_prefix:true});
assertEq(wasmEvalText(subneg64).exports.f(-37000000000n), 37000000000n)
assertEq(wasmEvalText(subneg64).exports.f(42000000000n), -42000000000n)

// AND{32,64} followed by `== 0`: check the two operations are merged into a
// single 'test' insn, and no 'and' insn.  The merging isn't done for
// {OR,XOR}{32,64}.  This is for both arguments being non-constant.

for ( [ty, expect_test] of
      [['i32',   'test %e.., %e..'],
       ['i64',   'test %r.., %r..']] ) {
   codegenTestX64_adhoc(
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
    `${expect_test}
     jnz 0x00000000000000..
     mov \\$0x11D7, %eax
     jmp 0x00000000000000..
     mov \\$0x4D2, %eax`
   );
}

// AND64 followed by `== 0`, with one of the args being a constant.  Depending
// on the constant we can get one of three forms.

for ( [imm, expect1, expect2] of
      [ // in signed-32 range => imm in insn
        ['0x17654321',
         'test \\$0x17654321, %e..', // edi or ecx
         ''],
        // in unsigned-32 range => imm in reg via movl
        ['0x87654321',
         'mov \\$-0x789ABCDF, %ebx',
         'test %rbx, %r..'], // rdi or rcx
        // not in either range => imm in reg via mov(absq)
        ['0x187654321',
         'mov \\$0x187654321, %rbx',
         'test %rbx, %r..']] // rdi or rcx
      ) {
   codegenTestX64_adhoc(
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
     jnz 0x00000000000000..
     mov \\$0x11D7, %eax
     jmp 0x00000000000000..
     mov \\$0x4D2, %eax`
   );
}

// For integer comparison followed by select, check that the comparison result
// isn't materialised into a register, for specific types.

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
if (getBuildConfiguration("windows")) {
    for ( [cmpTy, cmpOp, selTy, insn1, insn2, insn3] of
          [ ['i32', 'le_s', 'i32',  'mov %esi, %eax',
                                    'cmp %edx, %ecx',
                                    'cmovnle %r9d, %eax'],
            ['i32', 'lt_u', 'i64',  'mov %rsi, %rax',
                                    'cmp %edx, %ecx',
                                    'cmovnb %r9, %rax'],
            ['i64', 'le_s', 'i32',  'mov %esi, %eax',
                                    'cmp %rdx, %rcx',
                                    'cmovnle %r9d, %eax'],
            ['i64', 'lt_u', 'i64',  'mov %rsi, %rax',
                                    'cmp %rdx, %rcx',
                                    'cmovnb %r9, %rax']
          ] ) {
        codegenTestX64_adhoc(cmpSel32vs64(cmpTy, cmpOp, selTy), 'f',
          `mov %r8.*, %.si
           ${insn1}
           ${insn2}
           ${insn3}`
        );
    }
} else {
    for ( [cmpTy, cmpOp, selTy, insn1, insn2, insn3] of
          [ ['i32', 'le_s', 'i32',  'mov %edx, %eax',
                                    'cmp %esi, %edi',
                                    'cmovnle %ecx, %eax'],
            ['i32', 'lt_u', 'i64',  'mov %rdx, %rax',
                                    'cmp %esi, %edi',
                                    'cmovnb %rcx, %rax'],
            ['i64', 'le_s', 'i32',  'mov %edx, %eax',
                                    'cmp %rsi, %rdi',
                                    'cmovnle %ecx, %eax'],
            ['i64', 'lt_u', 'i64',  'mov %rdx, %rax',
                                    'cmp %rsi, %rdi',
                                    'cmovnb %rcx, %rax']
          ] ) {
        codegenTestX64_adhoc(cmpSel32vs64(cmpTy, cmpOp, selTy), 'f',
          `${insn1}
           ${insn2}
           ${insn3}`
        );
    }
}

// For integer comparison followed by select, check correct use of operands in
// registers vs memory.  At least for the 64-bit-cmp/64-bit-sel case.

for ( [pAnyCmp, pAnySel, cmpArgL, cmovArgL ] of
      [ // r, r
        ['$pReg1', '$pReg2',
         '%r.+', '%r.+'],
        // r, m
        ['$pReg1', '$pMem2',
         '%r.+', '0x..\\(%rbp\\)'],
        // m, r
        ['$pMem1', '$pReg2',
         '0x..\\(%rbp\\)', '%r.+'],
        // m, m
        ['$pMem1', '$pMem2',
         '0x..\\(%rbp\\)', '0x..\\(%rbp\\)']
      ] ) {
   codegenTestX64_adhoc(
    `(module
       (func (export "f")
             (param $p1 i64)    (param $p2 i64)     ;; in regs for both ABIs
             (param $pReg1 i64) (param $pReg2 i64)  ;; in regs for both ABIs
             (param i64)        (param i64)         ;; ignored
             (param $pMem1 i64) (param $pMem2 i64)  ;; in mem for both ABIs
             (result i64)
         (select (local.get $p1)
                 (local.get ${pAnySel})
                 (i64.eq (local.get $p2) (local.get ${pAnyCmp})))
       )
    )`,
    'f',
    // 'q*' because the disassembler shows 'q' only for the memory cases
    `mov %r.+, %r.+
     cmpq*    ${cmpArgL}, %r.+
     cmovnzq* ${cmovArgL}, %r.+`
   );
}
