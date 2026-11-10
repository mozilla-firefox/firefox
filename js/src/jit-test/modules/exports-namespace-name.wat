;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-namespace-name.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-namespace-name.wat")))
(module
  (func (export "*namespace*") (result i32) i32.const 1)
  (func (export "*deferred-namespace*") (result i32) i32.const 2)
  (func (export "f") (result i32) i32.const 3))
