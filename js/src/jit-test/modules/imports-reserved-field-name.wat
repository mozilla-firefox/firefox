;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-reserved-field-name.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-reserved-field-name.wat")))
(module
  (import "module1.js" "wasm:a" (global i32)))
