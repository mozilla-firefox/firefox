;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-dot-generator.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-dot-generator.wat")))
(module
  (func (result i32) i32.const 42)
  (export ".generator" (func 0)))
