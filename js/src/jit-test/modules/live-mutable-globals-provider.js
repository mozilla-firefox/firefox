import { big } from "live-mutable-globals.wasm";
import * as ns from "live-mutable-globals.wasm";

export const bigGlobal = new WebAssembly.Global(
  { value: "i64", mutable: true },
  1n
);
export const refGlobal = new WebAssembly.Global(
  { value: "externref", mutable: true },
  null
);
export const payload = new WebAssembly.Global(
  { value: "i32", mutable: true },
  17
);
export const immutableGlobal = new WebAssembly.Global(
  { value: "externref" },
  payload
);

function errorName(read) {
  try {
    read();
    return null;
  } catch (error) {
    return error.name;
  }
}

export const earlyReads = [
  errorName(() => big),
  errorName(() => ns.bigAlias),
  errorName(() => Object.getOwnPropertyDescriptor(ns, "ref").value),
  errorName(() => eval("big")),
];

// These are warmed up before the Wasm module has executed, so they are
// compiled while the bindings are still uninitialized.
export function earlyReadBig() {
  return big;
}

export function earlyReadNamespace() {
  return ns.big;
}

for (let i = 0; i < 200; i++) {
  for (const read of [earlyReadBig, earlyReadNamespace]) {
    if (errorName(read) !== "ReferenceError") {
      throw new Error("Expected ReferenceError before Wasm execution");
    }
  }
}
