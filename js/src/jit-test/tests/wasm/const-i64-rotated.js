// i64 constants that are a value LUI loads -- 32 bits, the low 12 clear --
// rotated, which riscv64 with Zbb builds with lui and rori, and their
// neighbours, which take the other ways of building a constant.

function rotr(v, r) {
  v = BigInt.asUintN(64, v);
  r = BigInt(r);
  return BigInt.asIntN(64, (v >> r) | (v << (64n - r)));
}

const values = [
  0xfffc000000000001n, // true, boxed
  0xfff9000000000001n,
  0xfffd000000000005n,
  0xffff8000000fffffn, // the complement of the GC chunk mask
  // The payload straddles bit 32, with the sign bits between its ends or
  // wrapping around bit 63.
  0x8007fffffffc0000n,
  0xfffc00008007ffffn,
];
for (let hi20 of [0x1n, 0x7fffn, 0x7ffffn, 0x80000n, 0xf8000n, 0xfffffn,
                  0x12345n, 0xabcden]) {
  // LUI sign-extends from bit 31.
  const lui = BigInt.asIntN(32, hi20 << 12n);
  for (let r = 1; r < 64; r++) {
    const v = rotr(lui, r);
    values.push(v, v + 1n, v - 1n);
  }
}

const expected = values.map(v => BigInt.asIntN(64, v));
const funcs = expected
  .map((v, i) => `(func (export "f${i}") (result i64) (i64.const ${v}))`)
  .join("\n");
const exports = wasmEvalText(`(module ${funcs})`).exports;
expected.forEach((v, i) => assertEq(exports[`f${i}`](), v));
