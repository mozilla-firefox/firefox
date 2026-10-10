// |jit-test| --disable-main-thread-wasm-denormals; skip-if: !getBuildConfiguration("can-disable-main-thread-wasm-denormals")

// Verify that --disable-main-thread-wasm-denormals scopes FTZ+DAZ to wasm
// JIT code only.

// Load a denormal out of a typed array in an obfuscated way to prevent
// compiler optimizations from fusing the multiply.
const tiny = new Float64Array([Number.MIN_VALUE]);
function jsFlushesDenormals() {
  return tiny[0] * 1 === 0;
}

const flushFn = wasmEvalText(`(module
  (func (export "flush") (param f64) (result f64)
    (f64.mul (local.get 0) (f64.const 1))))`).exports.flush;

// Wasm sees FTZ.
assertEq(flushFn(5e-324), 0);

// JS does not.
assertEq(jsFlushesDenormals(), false);

// Boundary conversion (JS Value -> wasm f32) are IEEE.
const f32EchoFn = wasmEvalText(`(module
  (func (export "echo") (param f32) (result f32)
    (local.get 0)))`).exports.echo;
assertEq(f32EchoFn(Math.pow(2, -140)) !== 0, true);

// JS import always sees IEEE, and wasm afterwards gets FTZ still.
{
  let sawFlushed = true;
  function checkImport() {
    sawFlushed = jsFlushesDenormals();
    return 0;
  }
  const { run } = wasmEvalText(`(module
    (import "" "checkImport" (func $ci (result i32)))
    (func (export "run") (param f64) (result f64)
      (drop (call $ci))
      (f64.mul (local.get 0) (f64.const 1))))`,
    { "": { checkImport } }).exports;
  assertEq(run(5e-324), 0);
  assertEq(sawFlushed, false);
}

// Direct call from Ion and the jit-exit path: hot loops on both sides.
{
  const { flush } = wasmEvalText(`(module
    (func (export "flush") (param f64) (result f64)
      (f64.mul (local.get 0) (f64.const 1))))`).exports;
  let bad = 0;
  for (let i = 0; i < 100000; i++) {
    if (flush(5e-324) !== 0) bad++;
  }
  assertEq(bad, 0);
  assertEq(jsFlushesDenormals(), false);
}

{
  let bad = 0;
  function checkImport() {
    if (jsFlushesDenormals()) bad++;
    return 0;
  }
  const { run } = wasmEvalText(`(module
    (import "" "checkImport" (func $ci (result i32)))
    (func (export "run") (result i32) (local i32)
      (loop $l
        (local.set 0 (call $ci))
        (br_if $l (i32.lt_s (local.get 0) (i32.const 0))))
      (local.get 0)))`,
    { "": { checkImport } }).exports;
  for (let i = 0; i < 20000; i++) run();
  assertEq(bad, 0);
}

// Throw across the boundary: JS import throws, caught in JS. IEEE must be
// restored after the unwind.
{
  function throwingImport() {
    throw new Error("boom");
  }
  const { run } = wasmEvalText(`(module
    (import "" "throwingImport" (func $ti))
    (func (export "run")
      (call $ti)))`,
    { "": { throwingImport } }).exports;
  let caught = false;
  try {
    run();
  } catch (e) {
    caught = true;
  }
  assertEq(caught, true);
  assertEq(jsFlushesDenormals(), false);
}

// Trap: OOB memory access, caught in JS.
{
  const { oob } = wasmEvalText(`(module
    (memory 1)
    (func (export "oob") (result i32)
      (i32.load (i32.const 100000))))`).exports;
  let caught = false;
  try {
    oob();
  } catch (e) {
    caught = true;
  }
  assertEq(caught, true);
  assertEq(jsFlushesDenormals(), false);
}

// Calls to builtins.
{
  const { copy } = wasmEvalText(`(module
    (memory 1)
    (func (export "copy")
      (memory.copy (i32.const 0) (i32.const 100) (i32.const 200))))`).exports;
  assertEq(jsFlushesDenormals(), false);
  copy();
  assertEq(jsFlushesDenormals(), false);
}

// Interrupt trap: the callback sees IEEE, and wasm resumes with FTZ.
{
  let callbackFlushed = true;
  setInterruptCallback(() => {
    callbackFlushed = jsFlushesDenormals();
    return true;
  });
  function requestInterrupt() {
    interruptIf(true);
  }
  // Keep many values live across the loop so that Ion allocates every GPR,
  // including the one that normally holds the instance.
  const locals = Array.from({ length: 8 }, (_, i) => `$l${i}`);
  const { run } = wasmEvalText(`(module
    (import "" "requestInterrupt" (func $ri))
    (func (export "run") (param f64) (result f64)
      (local $i i32) ${locals.map(l => `(local ${l} i32)`).join(" ")}
      (call $ri)
      (loop $l
        ${locals.map((l, i) =>
          `(local.set ${l} (i32.add (local.get ${l}) (i32.const ${i + 1})))`
        ).join("\n")}
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br_if $l (i32.lt_s (local.get $i) (i32.const 1000))))
      (drop ${locals.reduce((acc, l) => `(i32.add ${acc} (local.get ${l}))`,
                            "(i32.const 0)")})
      (f64.mul (local.get 0) (f64.const 1))))`,
    { "": { requestInterrupt } }).exports;
  for (let i = 0; i < 100; i++) {
    callbackFlushed = true;
    assertEq(run(5e-324), 0);
    assertEq(callbackFlushed, false);
  }
  setInterruptCallback(() => true);
}
