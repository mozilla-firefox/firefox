// |jit-test| module; --fast-warmup; --no-threads

import { NUM, OBJ, STR, mutable, setMutable } from "constant-bindings-exports.js";
import { threw } from "constant-bindings-cycle-b.js";
import { readLate } from "constant-bindings-cycle-a.js";

function f() {
  return NUM + OBJ.x + STR.length + mutable;
}
for (let i = 0; i < 200; i++) {
  if (i === 100) {
    setMutable(2);
    OBJ.x = 8;
  }
  assertEq(f(), i < 100 ? 14 : 16);
}

// Ion code compiled while the import was uninitialized must still see it after
// initialization.
assertEq(threw, 200);
for (let i = 0; i < 200; i++) {
  assertEq(readLate(), 42);
}
