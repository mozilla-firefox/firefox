// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test: 0 exiting values, 1 exit target => can be unrolled.

let t = `
(module
  (memory (export "mem") 1)
  (func (export "my_memset")
        (param $start i32) (param $byte i32) (param $count i32)
    (local $limit i32)
    (local.set $limit (i32.add (local.get $start) (local.get $count)))
    (if (i32.lt_u (local.get $start) (local.get $limit))
      (then
        (loop $cont
          (i32.store8 (local.get $start) (local.get $byte))
          (local.set $start (i32.add (local.get $start) (i32.const 1)))
          (br_if $cont (i32.lt_u (local.get $start) (local.get $limit)))
        )
      )
    )
  )
)`;

let i = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

i.exports.my_memset(1, 22, 1);
i.exports.my_memset(3, 33, 2);
i.exports.my_memset(6, 44, 3);
i.exports.my_memset(10, 55, 4);
i.exports.my_memset(15, 66, 5);
i.exports.my_memset(21, 77, 6);
i.exports.my_memset(28, 88, 7);
i.exports.my_memset(36, 99, 8);

let buf = new Uint8Array(i.exports.mem.buffer);

let s = "";
for (let i = 0; i < 45; i++) {
    s = s + (buf[i] + " ");
}

let expected =
    "0 22 0 33 33 0 44 44 44 0 55 55 55 55 0 66 66 66 66 66 0 77 77 77 " +
    "77 77 77 0 88 88 88 88 88 88 88 0 99 99 99 99 99 99 99 99 0 ";

assertEq(s, expected);
