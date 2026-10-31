;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("cycle-wasm.wasm",
;;     wasmTextToBinary(os.file.readFile("cycle-wasm.wat")))
(module
  (import "cycle-js.js" "jsGlob" (global $jsGlob (mut i32)))
  (func $wasmFunc (result i32)
    i32.const 43)
  (func $readJsGlob (result i32)
    global.get $jsGlob)
  (global $wasmGlob i32 (i32.const 24))
  (export "wasmGlob" (global $wasmGlob))
  (export "wasmFunc" (func $wasmFunc))
  (export "readJsGlob" (func $readJsGlob)))
