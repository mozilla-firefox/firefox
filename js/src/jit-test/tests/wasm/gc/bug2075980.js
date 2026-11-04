// |jit-test| skip-if: !wasmSimdEnabled()

const bodies = [
  `(drop (struct.new $s (i32.const 0)))`,
  `(drop (struct.new_default $s))`,
  `(drop (array.new $a (i32.const 0) (i32.const 4)))`,
  `(drop (array.new_default $a (i32.const 4)))`,
  `(drop (array.new_fixed $a 4 (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0)))`,
  `(global.set $g (local.get $any))`,
  `(struct.set $rs 0 (local.get $rs) (local.get $any))`,
  `(array.set $ra (local.get $ra) (i32.const 0) (local.get $any))`,
  `(array.fill $ra (local.get $ra) (i32.const 0) (local.get $any) (i32.const 4))`,
  `(block $b (try_table (catch $t $b)))`,
  `(drop (i64.div_s (local.get $i64) (local.get $i64)))`,
  `(drop (i64.rem_u (local.get $i64) (local.get $i64)))`,
];

for (const body of bodies) {
  try {
    new WebAssembly.Module(wasmTextToBinary(`(module
      (type $s (struct (field i32)))
      (type $a (array (mut i32)))
      (type $rs (struct (field (mut anyref))))
      (type $ra (array (mut anyref)))
      (global $g (mut anyref) (ref.null any))
      (tag $t)
      (func
        (param $any anyref)
        (param $rs (ref null $rs))
        (param $ra (ref null $ra))
        (param $i64 i64)
        (local ${"v128 ".repeat(32768)})
        ${body}
      )
    )`));
  } catch (e) {
    assertEq(e instanceof WebAssembly.CompileError, true);
    assertEq(e.message.includes("stack frame is too large"), true);
  }
}
