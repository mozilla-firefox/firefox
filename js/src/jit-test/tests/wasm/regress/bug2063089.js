
const wat = `(module
  (type $s (struct (field $s1 (mut i32)) (field $s2 (mut i32))))

  (func $update (param $o (ref $s))
    (local $k i32)

    (local.set $k (i32.const 4))
    block
      loop
        ;; if ($k < 0) break
        (i32.le_s (local.get $k) (i32.const 0))
        br_if 1

        ;; o.s1 = o.s1 + 1
        (struct.set $s $s1 (local.get $o)
          (i32.add
            (struct.get $s $s1 (local.get $o))
            (i32.const 1)
          )
        )

        ;; o.s2 = o.s1   (must see the store above)
        (struct.set $s $s2 (local.get $o)
          (struct.get $s $s1 (local.get $o))
        )

        ;; $k--; continue
        (local.set $k (i32.sub (local.get $k) (i32.const 1)))
        br 0
      end
    end

    ;; o.s1 = o.s1 % 65521   (must see the last store in the loop)
    (struct.set $s $s1 (local.get $o)
      (i32.rem_s (struct.get $s $s1 (local.get $o)) (i32.const 65521))
    )
  )

  (func (export "run") (result i32 i32)
    (local $o (ref $s))
    (local.set $o (struct.new $s (i32.const 1) (i32.const 0)))
    (call $update (local.get $o))
    (struct.get $s $s1 (local.get $o))
    (struct.get $s $s2 (local.get $o))
  )
)`;

const { run } = wasmEvalText(wat).exports;

const [e1, e2] = [5, 5]; // "expected"
const [a1, a2] = run();  // "actual"

assertEq(e1, a1);
assertEq(e2, a2);
