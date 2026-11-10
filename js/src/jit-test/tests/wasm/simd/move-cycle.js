// |jit-test| skip-if: !wasmSimdEnabled()

// A loop back edge that shifts a chain of v128 locals makes the register
// allocator emit a parallel move of SIMD128 values containing a cycle, which
// the move emitter must break.

const pattern = 0xdeadbeefcafebaben;
const lo = Number(pattern & 0xffffffffn);
const hi = Number(pattern >> 32n);

const { mem, f } = wasmEvalText(`(module
  (memory (export "mem") 1)
  (func (export "f") (param i32) (result i32)
    (local $i i32)
    (local $v0 v128)
    (local $v1 v128)
    (local $v2 v128)
    (local $v3 v128)
    (local $v4 v128)
    (local $v5 v128)
    (local.set $v0 (v128.load (i32.const 0)))
    (local.set $v1 (v128.load (i32.const 16)))
    (local.set $v2 (v128.load (i32.const 32)))
    (local.set $v3 (v128.load (i32.const 48)))
    (local.set $v4 (v128.load (i32.const 64)))
    (local.set $v5 (v128.load (i32.const 80)))
    (loop $L
      (local.set $v0 (local.get $v1))
      (local.set $v1 (local.get $v2))
      (local.set $v2 (local.get $v3))
      (local.set $v3 (local.get $v4))
      (local.set $v4 (local.get $v5))
      (local.set $v5 (v128.load (i32.const 128)))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $L (i32.lt_s (local.get $i) (i32.const 10))))
    (i32x4.extract_lane 0 (i32x4.add (local.get $v0) (local.get $v5)))))`)
  .exports;

const words = new Uint32Array(mem.buffer);
for (let i = 0; i < words.length; i += 2) {
  words[i] = lo;
  words[i + 1] = hi;
}

assertEq(f(0), (lo + lo) | 0);
