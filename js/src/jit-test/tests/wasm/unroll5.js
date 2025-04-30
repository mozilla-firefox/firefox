// |jit-test| test-also=--setpref=wasm_unroll_loops=true

// Loop unrolling test.  Seen to be causing assertion failures to do with
// incorrect header phi arg remapping -- the loop header contains a phi,
// neither arg of which is from inside the loop.

let t = `
(module
  (memory 0)
  (func (export "f1")
        (param $local0 i32) (param $local1 i32) (param $local2 i32)
        (result i32)
    (local $local3 i32) (local $local4 i32)
    (local $local5 i32) (local $local6 i32)
    (local $local7 i32) (local $local8 i32)
    (i32.load (local.get $local0))
    if
      (local.set $local8 (i32.shl (local.get $local2) (i32.const 2)))
      loop $again
        local.get $local8
        i32.load
        local.set $local4
        local.get $local5
        local.get $local2
        i32.const 4
        i32.add
        local.tee $local3
        i32.store
        local.get $local6
        local.set $local4
        i32.const 2
        local.set $local6
        local.get $local4
        br_if $again
      end
    end
    i32.const 0
  )
)`;

let i = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(t)));

// We only care that we can compile this without asserting (in the unroller).
assertEq(i + "", "[object WebAssembly.Instance]");
