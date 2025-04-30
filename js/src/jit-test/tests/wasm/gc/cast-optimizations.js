// Test situations where LICM can be performed
{
  const { test1, test2 } = wasmEvalText(`(module
    (type $a1 (sub (array (mut funcref))))
    (type $a2 (sub (array (mut i32))))

    (func (export "test1") (result anyref)
      (local anyref i32)
      loop
        (ref.i31 (i32.const 0))
        ref.cast anyref
        local.set 0

        (local.set 1 (i32.add (local.get 1) (i32.const 1)))
        (br_if 0 (i32.gt_u (local.get 1) (i32.const 10)))
      end
      local.get 0
    )
    (func (export "test2") (result anyref)
      (local anyref i32)
      loop
        (ref.i31 (i32.const 0))
        ref.cast anyref
        ref.cast (ref null $a2)
        local.tee 0

        ref.test (ref null $a1)
        br_if 0
      end
      local.get 0
    )
  )`).exports;

  assertEq(test1(), 0);
  assertErrorMessage(() => test2(), WebAssembly.RuntimeError, /bad cast/);
}
