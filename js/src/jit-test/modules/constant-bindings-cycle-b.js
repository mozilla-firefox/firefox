import { readLate } from "constant-bindings-cycle-a.js";
export let threw = 0;
for (let i = 0; i < 200; i++) {
  try {
    readLate();
  } catch (e) {
    if (e instanceof ReferenceError) {
      threw++;
    }
  }
}
export const LATE = 42;
