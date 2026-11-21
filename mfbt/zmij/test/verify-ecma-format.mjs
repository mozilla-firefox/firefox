#!/usr/bin/env node

// Checks that each third-column in baseline.txt is what a non-SpiderMonkey JS
// engine produces for the same double with String().

import fs from "node:fs";

let failed = false;
const view = new DataView(new ArrayBuffer(8));
const baseline = fs.readFileSync(
  new URL("baseline.txt", import.meta.url),
  "utf8"
);
for (const line of baseline.trimEnd().split("\n")) {
  const [hex, , actual] = line.split(" ");
  view.setBigUint64(0, BigInt(hex));
  const expected = String(view.getFloat64(0));
  if (actual !== expected) {
    console.log(`${hex}: expected ${expected}, got ${actual}`);
    failed = true;
  }
}

if (failed) throw new Error("Verification failed");
console.log("OK");
