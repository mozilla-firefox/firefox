;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-reserved-module-name.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-reserved-module-name.wat")))
(module
  (import "wasm-js:foo" "f" (func)))
