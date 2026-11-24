;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-null-name.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-null-name.wat")))
(module
  (import "exports-null-name.wasm" "a\00b" (func $f (result i32)))
  (import "exports-null-name.wasm" "\c4\80\00b" (func $f2 (result i32)))
  (func $call (result i32)
    call $f)
  (func $call2 (result i32)
    call $f2)
  (export "call" (func $call))
  (export "call2" (func $call2)))
