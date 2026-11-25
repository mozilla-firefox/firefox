;; Assembled with the JS shell:
;;   os.file.writeTypedArrayToFile("live-mutable-globals.wasm",
;;     wasmTextToBinary(os.file.readFile("live-mutable-globals.wat")))
(module
  (import "live-mutable-globals-provider.js" "bigGlobal"
    (global $big (mut i64)))
  (import "live-mutable-globals-provider.js" "refGlobal"
    (global $ref (mut externref)))
  (import "live-mutable-globals-provider.js" "immutableGlobal"
    (global $immutable externref))
  (global $exception (mut exnref) (ref.null exn))
  (func (export "setBig") (param i64)
    local.get 0
    global.set $big)
  (func (export "setRef") (param externref)
    local.get 0
    global.set $ref)
  (export "big" (global $big))
  (export "bigAlias" (global $big))
  (export "ref" (global $ref))
  (export "refAlias" (global $ref))
  (export "immutable" (global $immutable))
  (export "exception" (global $exception)))
