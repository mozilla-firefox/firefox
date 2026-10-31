;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-mutable-global.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-mutable-global.wat")))
(module
  (import "exports-mutable-global.wasm" "g" (global $g (mut i32)))
  (func $get (result i32)
    global.get $g)
  (export "get" (func $get)))
