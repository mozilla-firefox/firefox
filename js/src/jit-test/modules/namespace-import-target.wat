;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("namespace-import-target.wasm",
;;     wasmTextToBinary(os.file.readFile("namespace-import-target.wat")))
(module
  (func (export "f") (result i32) i32.const 2))
