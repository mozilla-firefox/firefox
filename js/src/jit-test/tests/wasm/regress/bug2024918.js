var ins = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(`
(module
  (type $Inner (struct (field (mut anyref))))
  (type $Outer (struct (field (mut (ref null $Inner)))))
  (type $Container (struct (field (mut (ref null $Inner)))))

  (global $g (mut (ref null $Container)) (ref.null $Container))

  (func (export "run") (param $n i32)
    (local $s1 (ref $Outer))
    (local $s2 (ref $Inner))
    (local $container (ref $Container))
    (local $i i32)

    (local.set $s1 (struct.new $Outer (ref.null $Inner)))
    (local.set $s2 (struct.new $Inner (ref.i31 (i32.const 0xff))))
    (struct.set $Outer 0 (local.get $s1) (local.get $s2))
    (local.set $container (struct.new $Container (ref.null $Inner)))
    (global.set $g (local.get $container))

    (loop $L
      (struct.set $Container 0
        (local.get $container)
        (struct.get $Outer 0 (local.get $s1)))
      (struct.set $Outer 0 (local.get $s1) (local.get $s2))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $L (i32.lt_s (local.get $i) (local.get $n)))
    )
  )

  (func (export "read") (result anyref)
    (struct.get $Inner 0 (struct.get $Container 0 (global.get $g)))
  )
)`)));

for (let i = 0; i < 10000; i++) {
  ins.exports.run(3);
  assertEq(ins.exports.read(), 0xff);
}
