// |jit-test| skip-if: !wasmStackSwitchingEnabled()

// An exception unwinding across back-to-back stack switches must not free a
// continuation stack while the activation's exit FP still points into it.

let { throwOuter, catchMain } = wasmEvalText(`(module
  (type $ft (func))
  (type $ct (cont $ft))
  (tag $exn (param i32))

  (func $thrower (type $ft)
    (throw $exn (i32.const 42))
  )
  (func $middle (type $ft)
    (resume $ct (cont.new $ct (ref.func $thrower)))
  )
  (func $outer (type $ft)
    (resume $ct (cont.new $ct (ref.func $middle)))
  )
  (elem declare func $thrower $middle $outer)

  (func (export "throwOuter")
    (resume $ct (cont.new $ct (ref.func $outer)))
  )
  (func (export "catchMain") (result i32)
    try (result i32)
      (resume $ct (cont.new $ct (ref.func $outer)))
      (i32.const 0)
    catch $exn
    end
  )
)`).exports;

for (let i = 0; i < 10; i++) {
  assertEq(catchMain(), 42);
  assertThrowsInstanceOf(() => throwOuter(), WebAssembly.Exception);
}
