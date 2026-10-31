;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-mutable-global.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-mutable-global.wat")))
(module
  (global $g (mut i32) (i32.const 1))
  (func $set (param $value i32)
    local.get $value
    global.set $g)
  (export "g" (global $g))
  (export "set" (func $set)))
