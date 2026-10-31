;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-grown-memory.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-grown-memory.wat")))
(module
  (import "exports-grown-memory.wasm" "mem" (memory 2)))
