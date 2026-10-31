;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-reserved-name.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-reserved-name.wat")))
(module
  (func (export "wasm:f")))
