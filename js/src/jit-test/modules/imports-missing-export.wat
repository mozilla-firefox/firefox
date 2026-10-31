;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-missing-export.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-missing-export.wat")))
(module
  (import "module1.js" "missing" (func)))
