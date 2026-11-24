;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("exports-null-name.wasm",
;;     wasmTextToBinary(os.file.readFile("exports-null-name.wat")))
(module
  (global $g i32 (i32.const 1))
  (func $f (result i32)
    i32.const 42)
  (func $f2 (result i32)
    i32.const 43)
  (export "a" (global $g))
  (export "a\00b" (func $f))
  (export "\c4\80" (global $g))
  (export "\c4\80\00b" (func $f2)))
