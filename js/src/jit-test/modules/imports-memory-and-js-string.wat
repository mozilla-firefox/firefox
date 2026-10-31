;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("imports-memory-and-js-string.wasm",
;;     wasmTextToBinary(os.file.readFile("imports-memory-and-js-string.wat")))
(module
  (import "exports-memory.wasm" "mem" (memory 1))
  (import "wasm:js-string" "length" (func $length (param externref) (result i32)))
  (import "wasm:js/string-constants" "hello" (global $str (ref extern)))
  (func (export "constantLength") (result i32)
    global.get $str
    call $length)
  (func (export "load") (param i32) (result i32)
    local.get 0
    i32.load))
