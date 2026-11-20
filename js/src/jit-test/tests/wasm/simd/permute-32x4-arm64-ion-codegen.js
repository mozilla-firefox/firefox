// |jit-test| skip-if: !wasmSimdEnabled() || !hasDisassembler() || wasmCompileMode() != "ion" || !getBuildConfiguration("arm64"); include:codegen-arm64-test.js

// Single-operand 32x4 permutes that map to one NEON instruction don't need a
// tbl with a constant mask.

function shuffle32x4(lanes) {
  return lanes.flatMap(l => [4 * l, 4 * l + 1, 4 * l + 2, 4 * l + 3]).join(" ");
}

for (let [lanes, expected] of [
  [[0, 2, 0, 2], "uzp1    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[1, 3, 1, 3], "uzp2    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[0, 0, 2, 2], "trn1    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[1, 1, 3, 3], "trn2    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[0, 0, 1, 1], "zip1    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[2, 2, 3, 3], "zip2    v0\\.4s, v0\\.4s, v0\\.4s"],
  [[1, 0, 3, 2], "rev64   v0\\.4s, v0\\.4s"],
  [[0, 1, 0, 1], "dup     v0\\.2d, v0\\.d\\[0\\]"],
  [[2, 3, 2, 3], "dup     v0\\.2d, v0\\.d\\[1\\]"],
]) {
  codegenTestARM64_adhoc(
    `(module
       (func (export "f") (param v128) (result v128)
         (i8x16.shuffle ${shuffle32x4(lanes)} (local.get 0) (local.get 0))))`,
    "f",
    expected);
}

// Other permutes still use tbl.
codegenTestARM64_adhoc(
  `(module
     (func (export "f") (param v128) (result v128)
       (i8x16.shuffle ${shuffle32x4([3, 1, 2, 0])} (local.get 0) (local.get 0))))`,
  "f",
  `ldr     q31, pc\\+${HEX}+ \\(addr 0x${HEX}+\\)
tbl     v0\\.16b, \\{v0\\.16b\\}, v31\\.16b`);

// Check the results of the specialized permutes.
for (let lanes of [[0, 2, 0, 2], [1, 3, 1, 3], [0, 0, 2, 2], [1, 1, 3, 3],
                   [0, 0, 1, 1], [2, 2, 3, 3], [1, 0, 3, 2], [0, 1, 0, 1],
                   [2, 3, 2, 3]]) {
  let ins = wasmEvalText(`(module
    (memory (export "mem") 1)
    (func (export "f")
      (v128.store (i32.const 16)
        (i8x16.shuffle ${shuffle32x4(lanes)} (v128.load (i32.const 0))
                                             (v128.load (i32.const 0))))))`);
  let mem = new Uint32Array(ins.exports.mem.buffer);
  let input = [0x11111111, 0x22222222, 0x33333333, 0x44444444];
  mem.set(input);
  ins.exports.f();
  assertDeepEq(Array.from(mem.slice(4, 8)), lanes.map(l => input[l]));
}
