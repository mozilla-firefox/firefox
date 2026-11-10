;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("namespace-import-deferredNs.wasm",
;;     wasmTextToBinary(os.file.readFile("namespace-import-deferredNs.wat")))
(module
  (global (import "namespace-import-reexport.js" "deferredNs") externref))
