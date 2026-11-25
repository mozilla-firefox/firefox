// |jit-test| module; skip-if: !wasmIsSupported() || getBuildConfiguration("release_or_beta"); --enable-wasm-esm-integration; --fast-warmup

// Exported mutable Wasm globals are live bindings: every read, through an
// import, a namespace or eval, and in every JIT tier, observes the current
// value of the global.

import { g, set } from "exports-mutable-global.wasm";
import * as ns from "exports-mutable-global.wasm";

// Keep the Wasm import first so its JS provider executes first in the cycle.
import {
  big,
  bigAlias,
  ref,
  immutable,
  exception,
  setBig,
  setRef,
} from "live-mutable-globals.wasm";
import * as globals from "live-mutable-globals.wasm";
import {
  bigGlobal,
  refGlobal,
  payload,
  earlyReads,
  earlyReadBig,
  earlyReadNamespace,
} from "live-mutable-globals-provider.js";
import { big as reexportedBig } from "live-mutable-globals-reexport.js";
import * as reexports from "live-mutable-globals-reexport.js";

load(libdir + "asserts.js");

function read() {
  return g;
}

function readNamespace() {
  return ns.g;
}

function acrossCall(value) {
  const before = g;
  set(value);
  return before + g;
}

assertEq(g, 1);
for (let i = 0; i < 2000; i++) {
  set(i);
  assertEq(read(), i);
  assertEq(readNamespace(), i);
  assertEq(Object.getOwnPropertyDescriptor(ns, "g").value, i);
  assertEq(eval("g"), i);
  assertEq(acrossCall(i + 1), 2 * i + 1);
}

// Reads before the Wasm module executed, including from functions compiled
// then, see the bindings as uninitialized.
for (const error of earlyReads) {
  assertEq(error, "ReferenceError");
}

function readBig() {
  return big;
}

function readBigAlias() {
  return bigAlias;
}

function readReexportedBig() {
  return reexportedBig;
}

function readReexportsNamespace() {
  return reexports.big;
}

function readRef() {
  return ref;
}

// The globals are mutated from both JS and Wasm, and big has two exports, each
// with its own binding.
for (let i = 0; i < 2000; i++) {
  const bigValue = (1n << 60n) + BigInt(i);
  const refValue = { i };
  if (i % 2 === 0) {
    bigGlobal.value = bigValue;
    refGlobal.value = refValue;
  } else {
    setBig(bigValue);
    setRef(refValue);
  }
  assertEq(readBig(), bigValue);
  assertEq(readBigAlias(), bigValue);
  assertEq(readReexportedBig(), bigValue);
  assertEq(readReexportsNamespace(), bigValue);
  assertEq(earlyReadBig(), bigValue);
  assertEq(earlyReadNamespace(), bigValue);
  assertEq(readRef(), refValue);
}

for (const value of [null, undefined, "reference", 42n, payload]) {
  refGlobal.value = value;
  assertEq(readRef(), value);
}

// The global's value is kept alive through the binding.
refGlobal.value = { afterGC: true };
gc();
assertEq(readRef().afterGC, true);

// An immutable global holding a WebAssembly.Global is not a live binding of
// that Global.
assertEq(immutable, payload);

// A mutable global of a type that can't be exposed to JS stays uninitialized.
assertEq(Reflect.has(globals, "exception"), true);
assertThrowsInstanceOf(() => exception, ReferenceError);
assertThrowsInstanceOf(() => globals.exception, ReferenceError);
