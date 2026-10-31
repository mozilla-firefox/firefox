;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("cycle-tdz-wasm.wasm",
;;     wasmTextToBinary(os.file.readFile("cycle-tdz-wasm.wat")))
(module
  (import "cycle-tdz-js.js" "fn" (func)))
