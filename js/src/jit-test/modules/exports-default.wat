;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-default.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-default.wat")))
(module
  (func $f (result i32) i32.const 1)
  (func $default (result i32) i32.const 2)
  (export "f" (func $f))
  (export "default" (func $default)))
