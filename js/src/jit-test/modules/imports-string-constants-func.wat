;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-string-constants-func.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-string-constants-func.wat")))
(module
  (import "wasm:js/string-constants" "x" (func)))
