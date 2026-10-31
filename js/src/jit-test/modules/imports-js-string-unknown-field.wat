;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-js-string-unknown-field.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-js-string-unknown-field.wat")))
(module
  (import "wasm:js-string" "notABuiltin" (func)))
