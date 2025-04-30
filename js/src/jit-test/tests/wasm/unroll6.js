// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test: 0 exiting values, 2 exit targets => can be unrolled.
// This is a modified version of unroll2.js.

let t = `
(module
  (memory (export "mem") 1)
  (func (export "not_really_memset")
        (param $start i32) (param $byte i32) (param $count i32)
    (local $limit i32)
    (local.set $limit (i32.add (local.get $start) (local.get $count)))
    (loop $cont
      (if (i32.lt_u (local.get $start) (local.get $limit)) (then
        (i32.store8 (local.get $start)
                    (i32.xor (local.get $byte) (i32.const 0)))
        (local.set $start (i32.add (local.get $start) (i32.const 1)))
        (if (i32.lt_u (local.get $start) (local.get $limit)) (then
          (i32.store8 (local.get $start)
                      (i32.add (local.get $byte) (i32.const 100)))
          (local.set $start (i32.add (local.get $start) (i32.const 1)))
          ;; continue
          (br $cont)
        ))
        ;; implied else-clause exits the loop
      ))
      ;; implied else-clause exits the loop
    )
  )
)`;

let i = //wasmEvalText(t);
    new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

i.exports.not_really_memset(1, 22, 1);
i.exports.not_really_memset(3, 33, 2);
i.exports.not_really_memset(6, 44, 3);
i.exports.not_really_memset(10, 55, 4);
i.exports.not_really_memset(15, 66, 5);
i.exports.not_really_memset(21, 77, 6);
i.exports.not_really_memset(28, 88, 7);
i.exports.not_really_memset(36, 99, 8);

let buf = new Uint8Array(i.exports.mem.buffer);

let s = "";
for (let i = 0; i < 45; i++) {
    s = s + (buf[i] + " ");
}

// The last number (22, 133, 44, 155, etc) in each zero-bordered group is of
// the form XX if the loop terminates via its first exit edge, and 1XX if it
// terminates by its second exit edge.
let expected =
    "0 22 0 33 133 0 44 144 44 0 55 155 55 155 0 66 166 66 166 66 0 77 177 " +
    "77 177 77 177 0 88 188 88 188 88 188 88 0 99 199 99 199 99 199 99 199 0 ";

assertEq(s, expected);
