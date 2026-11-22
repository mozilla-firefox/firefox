// |jit-test| skip-if: helperThreadCount() === 0

// Two threads each store to their own location and then load the other's.
// With sequentially consistent accesses at least one of them must observe the
// other's store.  This test is not fail-safe: it may pass despite a bug.

const iterations = 20000;

// Each thread's elements are a cache line apart: [step, sawOther, value].
const mainBase = 0;
const workerBase = 64;
const doneIndex = 128;

const body = `
function run(ta, me, other, n) {
  let step = 0;
  let failures = 0;
  for (let i = 0; i < n; i++) {
    // Start both threads at the same time.
    step++;
    Atomics.store(ta, me, step);
    while (Atomics.load(ta, other) < step) {}

    Atomics.store(ta, me + 2, step);
    Atomics.store(ta, me + 1, Atomics.load(ta, other + 2) === step ? 1 : 0);

    step++;
    Atomics.store(ta, me, step);
    while (Atomics.load(ta, other) < step) {}

    if ((Atomics.load(ta, me + 1) | Atomics.load(ta, other + 1)) === 0) {
      failures++;
    }
  }
  return failures;
}
`;

const sab = new SharedArrayBuffer(4096);
const ta = new Int32Array(sab);
setSharedObject(sab);

evalInWorker(`
  const ta = new Int32Array(getSharedObject());
  ${body}
  const failures = run(ta, ${workerBase}, ${mainBase}, ${iterations});
  Atomics.store(ta, ${doneIndex}, failures + 1);
`);

eval(body);
const failures = run(ta, mainBase, workerBase, iterations);
while (Atomics.load(ta, doneIndex) === 0) {}

assertEq(failures, 0);
assertEq(Atomics.load(ta, doneIndex) - 1, 0);
