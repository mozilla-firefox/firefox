;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("namespace-import-jsNs.wasm",
;;     wasmTextToBinary(os.file.readFile("namespace-import-jsNs.wat")))
(module
  (global (import "namespace-import-reexport.js" "jsNs") externref))
