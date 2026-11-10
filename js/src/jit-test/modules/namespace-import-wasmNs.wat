;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("namespace-import-wasmNs.wasm",
;;     wasmTextToBinary(os.file.readFile("namespace-import-wasmNs.wat")))
(module
  (global (import "namespace-import-reexport.js" "wasmNs") externref))
