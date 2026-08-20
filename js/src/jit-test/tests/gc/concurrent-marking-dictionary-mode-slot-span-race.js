// |jit-test| skip-if: !hasFunction["gczeal"] || helperThreadCount() === 0

const propsPerObject = 16;
const poolSize = 500;

// Long-lived objects, each holding several independently-allocated value
// objects reachable only via the holder's own slots (no other reference).
let pool = [];
for (let i = 0; i < poolSize; i++) {
  let holder = {};
  for (let j = 0; j < propsPerObject; j++) {
    holder["p" + j] = {tag: i * 1000 + j};
  }
  pool.push(holder);
}

// Inflate the heap so a full concurrent mark takes a while, widening the
// chance that it's still in progress while we convert pool objects below.
let filler = [];
for (let i = 0; i < 100000; i++) {
  filler.push({a: i, b: i + 1, c: [i, i]});
}

gczeal(0);
gczeal(16, 500); // ConcurrentMarking
gczeal(15);      // CheckHeapAfterGC
gczeal(27);      // ConcurrentMarkingDelays

// Deleting a non-last property forces NativeObject::toDictionaryMode.
const mid = propsPerObject >> 1;
for (let i = 0; i < poolSize; i++) {
  delete pool[i]["p" + mid];
}

gc();
gc();

let bad = 0;
for (let holder of pool) {
  for (let k of Object.keys(holder)) {
    let v = holder[k];
    if (typeof v !== "object" || v === null || typeof v.tag !== "number") {
      bad++;
    }
  }
}
if (bad > 0) {
  throw new Error("corruption detected in " + bad + " properties");
}
