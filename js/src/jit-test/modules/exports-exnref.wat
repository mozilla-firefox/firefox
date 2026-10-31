;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-exnref.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-exnref.wat")))
(module
  (global (export "e") exnref (ref.null exn)))
