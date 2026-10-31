;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-memory.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-memory.wat")))
(module
  (memory $mem 1)
  (func $store (param $addr i32) (param $value i32)
    local.get $addr
    local.get $value
    i32.store)
  (export "mem" (memory $mem))
  (export "store" (func $store)))
