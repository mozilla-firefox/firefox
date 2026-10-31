;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-ambiguous-export.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-ambiguous-export.wat")))
(module
  (import "ambiguous.js" "a" (global i32)))
