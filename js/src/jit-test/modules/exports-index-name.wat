;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-index-name.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-index-name.wat")))
(module
  (func (result i32) i32.const 7)
  (export "0" (func 0)))
