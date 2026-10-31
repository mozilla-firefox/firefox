;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-grown-memory.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-grown-memory.wat")))
(module
  (memory $mem 1)
  (func $grow
    i32.const 1
    memory.grow
    drop)
  (start $grow)
  (export "mem" (memory $mem)))
