new WebAssembly.Module(wasmTextToBinary(`(module
  (global $g (export "g") (mut funcref) ref.null func)
  (func
    global.get $g
    global.set $g
  )
)`));
new WebAssembly.Module(wasmTextToBinary(`(module
  (global $g (mut funcref) ref.null func)
  (func
    global.get $g
    global.set $g
  )
)`));
