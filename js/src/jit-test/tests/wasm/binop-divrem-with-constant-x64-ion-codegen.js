// |jit-test| skip-if: !hasDisassembler() || wasmCompileMode() != "ion" || getBuildConfiguration("windows") || !getBuildConfiguration("x64") || getBuildConfiguration("simulator"); include:codegen-x64-test.js

// Windows is disallowed because the argument registers are different from on
// Linux, and matching both is both difficult and not of much value.

// Signed 32-bit division with constants.
const i32_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %edi, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %edi, %eax
               shr \\$0x1F, %eax
               add %edi, %eax
               sar \\$0x01, %eax`,
  },
  {
    divisor: 4,
    expected: `mov %edi, %eax
               sar \\$0x1F, %eax
               shr \\$0x1E, %eax
               add %edi, %eax
               sar \\$0x02, %eax`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `mov %edi, %eax
               neg %eax
               jno 0x${HEX}+
               ud2`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `movsxd %edi, %rax
                imul \\$0x55555556, %rax, %rax
                shr \\$0x20, %rax
                mov %edi, %ecx
                sar \\$0x1F, %ecx
                sub %ecx, %eax`,
  },
  {
    divisor: 5,
    expected: `movsxd %edi, %rax
               imul \\$0x66666667, %rax, %rax
               sar \\$0x21, %rax
               mov %edi, %ecx
               sar \\$0x1F, %ecx
               sub %ecx, %eax`,
  },
  {
    divisor: 7,
    expected: `movsxd %edi, %rax
               imul \\$-0x6DB6DB6D, %rax, %rax
               shr \\$0x20, %rax
               add %edi, %eax
               sar \\$0x02, %eax
               mov %edi, %ecx
               sar \\$0x1F, %ecx
               sub %ecx, %eax`,
  },
  {
    divisor: 9,
    expected: `movsxd %edi, %rax
               imul \\$0x38E38E39, %rax, %rax
               sar \\$0x21, %rax
               mov %edi, %ecx
               sar \\$0x1F, %ecx
               sub %ecx, %eax`,
  },
];

for (let {divisor, expected} of i32_div_s) {
  let divs32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_s (local.get 0) (i32.const ${divisor}))))`
  codegenTestX64_adhoc(divs32, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.div_s (local.get 0) (i32.const -${divisor}))))`
    codegenTestX64_adhoc(divs32, 'f', expected + `
      neg %eax`
    );
  }
}

// Unsigned 32-bit division with constants.
const i32_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %edi, %ecx
               mov %ecx, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               shr \\$0x01, %eax`,
  },
  {
    divisor: 4,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               shr \\$0x02, %eax`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov %edi, %eax
                mov \\$-0x55555555, %ecx
                imul %rcx, %rax
                shr \\$0x21, %rax`,
  },
  {
    divisor: 5,
    expected: `mov %edi, %eax
               mov \\$-0x33333333, %ecx
               imul %rcx, %rax
               shr \\$0x22, %rax`,
  },
  {
    divisor: 7,
    expected: `mov %edi, %eax
               imul \\$0x24924925, %rax, %rax
               shr \\$0x20, %rax
               mov %edi, %ecx
               sub %eax, %ecx
               shr \\$0x01, %ecx
               add %ecx, %eax
               shr \\$0x02, %eax`,
  },
  {
    divisor: 9,
    expected: `mov %edi, %eax
               imul \\$0x38E38E39, %rax, %rax
               shr \\$0x21, %rax`,
  },

  // Special case: Zero (additional) shift amount.
  {
    divisor: 641,
    expected: `mov %edi, %eax
               imul \\$0x663D81, %rax, %rax
               shr \\$0x20, %rax`,
  },
];

for (let {divisor, expected} of i32_div_u) {
  let divu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.div_u (local.get 0) (i32.const ${divisor}))))`
  codegenTestX64_adhoc(divu32, 'f', expected);
}

// Signed 64-bit division with constants.
const i64_div_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %rdi, %rax`,
  },
  {
    divisor: 2,
    expected: `mov %rdi, %rax
               shr \\$0x3F, %rax
               add %rdi, %rax
               sar \\$0x01, %rax`,
  },
  {
    divisor: 4,
    expected: `mov %rdi, %rax
               sar \\$0x3F, %rax
               shr \\$0x3E, %rax
               add %rdi, %rax
               sar \\$0x02, %rax`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov %rdi, %rax
               sar \\$0x3F, %rax
               shr \\$0x20, %rax
               add %rdi, %rax
               sar \\$0x20, %rax`,
  },

  // Division by -1 needs an overflow check.
  {
    divisor: -1,
    expected: `mov %rdi, %rax
               neg %rax
               jno 0x${HEX}+
               ud2`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov \\$0x5555555555555556, %rax
                imul %rdi
                mov %rdi, %rax
                sar \\$0x3F, %rax
                sub %rax, %rdx`,
  },
  {
    divisor: 5,
    expected: `mov \\$0x6666666666666667, %rax
               imul %rdi
               sar \\$0x01, %rdx
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx`,
  },
  {
    divisor: 7,
    expected: `mov \\$0x4924924924924925, %rax
               imul %rdi
               sar \\$0x01, %rdx
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx`,
  },
  {
    divisor: 9,
    expected: `mov \\$0x1C71C71C71C71C72, %rax
               imul %rdi
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `mov \\$0x2AAAAAAAAAAAAAAB, %rax
               imul %rdi
               sar \\$0x1F, %rdx
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx`,
  },
];

for (let {divisor, expected} of i64_div_s) {
  let result = IsPowerOfTwo(divisor) ? "rax" : "rdx";
  let mov_rdx_to_rax = result !== "rax" ? `
    mov %rdx, %rax` : ``;

  let divs64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_s (local.get 0) (i64.const ${divisor}))))`
  codegenTestX64_adhoc(divs64, 'f', expected + mov_rdx_to_rax);

  // Test negative divisors, too.
  if (divisor > 1) {
    let divs64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.div_s (local.get 0) (i64.const -${divisor}))))`
    codegenTestX64_adhoc(divs64, 'f', expected + `
      neg %${result}` + mov_rdx_to_rax
    );
  }
}

function IsPowerOfTwo(x) {
  x = BigInt(x);
  if (x < 0) {
    x = -x;
  }
  return x && (x & (x - 1n)) === 0n;
}

// Unsigned 64-bit division with constants.
const i64_div_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2
               mov %rdx, %rax`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax`,
  },
  {
    divisor: 2,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               shr \\$0x01, %rax`,
  },
  {
    divisor: 4,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               shr \\$0x02, %rax`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               shr \\$0x20, %rax`,
  },

  // Other divisors.
  {
    divisor: 3,
    expected:  `mov \\$-0x5555555555555555, %rax
                mul %rdi
                shr \\$0x01, %rdx
                mov %rdx, %rax`,
  },
  {
    divisor: 5,
    expected: `mov \\$-0x3333333333333333, %rax
               mul %rdi
               shr \\$0x02, %rdx
               mov %rdx, %rax`,
  },
  {
    divisor: 7,
    expected: `mov \\$0x2492492492492493, %rax
               mul %rdi
               mov %rdi, %rax
               sub %rdx, %rax
               shr \\$0x01, %rax
               add %rax, %rdx
               shr \\$0x02, %rdx
               mov %rdx, %rax`,
  },
  {
    divisor: 9,
    expected: `mov \\$-0x1C71C71C71C71C71, %rax
               mul %rdi
               shr \\$0x03, %rdx
               mov %rdx, %rax`,
  },
  {
    divisor: 0x3_0000_0000,
    expected: `mov \\$-0x5555555555555555, %rax
               mul %rdi
               shr \\$0x21, %rdx
               mov %rdx, %rax`,
  },

  // Special case: Zero shift amount.
  {
    divisor: 274177,
    expected: `mov \\$0x3D30F19CD101, %rax
               mul %rdi
               mov %rdx, %rax`,
  },
];

for (let {divisor, expected} of i64_div_u) {
  let divu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.div_u (local.get 0) (i64.const ${divisor}))))`
  codegenTestX64_adhoc(divu64, 'f', expected);
}

//////////////



// Signed 32-bit remainder with constants.
const i32_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               xor %eax, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               test %eax, %eax
               js 0x${HEX}+
               and \\$0x01, %eax
               jmp 0x${HEX}+
               neg %eax
               and \\$0x01, %eax
               neg %eax`,
  },
  {
    divisor: 4,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               test %eax, %eax
               js 0x${HEX}+
               and \\$0x03, %eax
               jmp 0x${HEX}+
               neg %eax
               and \\$0x03, %eax
               neg %eax`,
  },
  {
    divisor: 0x100,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               test %eax, %eax
               js 0x${HEX}+
               movzx %al, %eax
               jmp 0x${HEX}+
               neg %eax
               movzx %al, %eax
               neg %eax`,
  },
  {
    divisor: 0x10000,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               test %eax, %eax
               js 0x${HEX}+
               movzx %ax, %eax
               jmp 0x${HEX}+
               neg %eax
               movzx %ax, %eax
               neg %eax`,
  },
  {
    divisor: 0x8000_0000,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               test %eax, %eax
               js 0x${HEX}+
               and \\$0x7FFFFFFF, %eax
               jmp 0x${HEX}+
               neg %eax
               and \\$0x7FFFFFFF, %eax
               neg %eax`,
  },
];

for (let {divisor, expected} of i32_rem_s) {
  let rems32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_s (local.get 0) (i32.const ${divisor}))))`
  codegenTestX64_adhoc(rems32, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems32 =
      `(module
         (func (export "f") (param i32) (result i32)
           (i32.rem_s (local.get 0) (i32.const -${divisor}))))`
    codegenTestX64_adhoc(rems32, 'f', expected);
  }
}

// Unigned 32-bit remainder with constants.
const u32_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               xor %eax, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               and \\$0x01, %eax`,
  },
  {
    divisor: 4,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               and \\$0x03, %eax`,
  },
  {
    divisor: 0x100,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               movzx %al, %eax`,
  },
  {
    divisor: 0x10000,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               movzx %ax, %eax`,
  },
  {
    divisor: 0x8000_0000,
    expected: `mov %edi, %ecx
               mov %ecx, %eax
               and \\$0x7FFFFFFF, %eax`,
  },
];

for (let {divisor, expected} of u32_rem_s) {
  let remu32 =
    `(module
       (func (export "f") (param i32) (result i32)
         (i32.rem_u (local.get 0) (i32.const ${divisor}))))`
  codegenTestX64_adhoc(remu32, 'f', expected);
}

// Signed 64-bit remainder with constants.
const i64_rem_s = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               xor %eax, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               and \\$0x01, %eax
               jmp 0x${HEX}+
               neg %rax
               and \\$0x01, %eax
               neg %rax`,
  },
  {
    divisor: 4,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               and \\$0x03, %eax
               jmp 0x${HEX}+
               neg %rax
               and \\$0x03, %eax
               neg %rax`,
  },
  {
    divisor: 0x100,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               movzx %al, %eax
               jmp 0x${HEX}+
               neg %rax
               movzx %al, %eax
               neg %rax`,
  },
  {
    divisor: 0x10000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               movzx %ax, %eax
               jmp 0x${HEX}+
               neg %rax
               movzx %ax, %eax
               neg %rax`,
  },
  {
    divisor: 0x8000_0000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               and \\$0x7FFFFFFF, %eax
               jmp 0x${HEX}+
               neg %rax
               and \\$0x7FFFFFFF, %eax
               neg %rax`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               mov %eax, %eax
               jmp 0x${HEX}+
               neg %rax
               mov %eax, %eax
               neg %rax`,
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               test %rax, %rax
               js 0x${HEX}+
               mov \\$0x7FFFFFFFFFFFFFFF, %r11
               and %r11, %rax
               jmp 0x${HEX}+
               neg %rax
               mov \\$0x7FFFFFFFFFFFFFFF, %r11
               and %r11, %rax
               neg %rax`,
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `mov \\$0x2AAAAAAAAAAAAAAB, %rax
               imul %rdi
               sar \\$0x1F, %rdx
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx
               mov \\$0x300000000, %r11
               imul %r11, %rdx
               mov %rdi, %rax
               sub %rdx, %rax`,
    negative: `mov \\$0x2AAAAAAAAAAAAAAB, %rax
               imul %rdi
               sar \\$0x1F, %rdx
               mov %rdi, %rax
               sar \\$0x3F, %rax
               sub %rax, %rdx
               neg %rdx
               mov \\$-0x300000000, %r11
               imul %r11, %rdx
               mov %rdi, %rax
               sub %rdx, %rax`,
  },
];

for (let {divisor, expected, negative = expected} of i64_rem_s) {
  let rems64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_s (local.get 0) (i64.const ${divisor}))))`
  codegenTestX64_adhoc(rems64, 'f', expected);

  // Test negative divisors, too.
  if (divisor > 0) {
    let rems64 =
      `(module
         (func (export "f") (param i64) (result i64)
           (i64.rem_s (local.get 0) (i64.const -${divisor}))))`
    codegenTestX64_adhoc(rems64, 'f', negative);
  }
}

// Unsigned 64-bit remainder with constants.
const i64_rem_u = [
  // Division by zero.
  {
    divisor: 0,
    expected: `ud2`,
  },

  // Power of two divisor
  {
    divisor: 1,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               xor %eax, %eax`,
  },
  {
    divisor: 2,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               and \\$0x01, %eax`,
  },
  {
    divisor: 4,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               and \\$0x03, %eax`,
  },
  {
    divisor: 0x100,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               movzx %al, %eax`,
  },
  {
    divisor: 0x10000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               movzx %ax, %eax`,
  },
  {
    divisor: 0x8000_0000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               and \\$0x7FFFFFFF, %eax`,
  },
  {
    divisor: 0x1_0000_0000,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               mov %eax, %eax`,
  },
  {
    divisor: 0x8000_0000_0000_0000n,
    expected: `mov %rdi, %rcx
               mov %rcx, %rax
               mov \\$0x7FFFFFFFFFFFFFFF, %r11
               and %r11, %rax`,
  },

  // Other divisors.
  {
    divisor: 0x3_0000_0000,
    expected: `mov \\$-0x5555555555555555, %rax
               mul %rdi
               shr \\$0x21, %rdx
               mov \\$0x300000000, %r11
               imul %r11, %rdx
               mov %rdi, %rax
               sub %rdx, %rax`,
  },
];

for (let {divisor, expected} of i64_rem_u) {
  let remu64 =
    `(module
       (func (export "f") (param i64) (result i64)
         (i64.rem_u (local.get 0) (i64.const ${divisor}))))`
  codegenTestX64_adhoc(remu64, 'f', expected);
}
