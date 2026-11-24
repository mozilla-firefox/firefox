// JS::SafeResolve of a thenable settles the outer promise at the SAME
// microtask depth as an ordinary resolve(thenable). Under the updated
// thenable-curtailment spec, PerformPromiseResolution runs in ~deferred~
// mode: when it finds a callable "then" it invokes PerformPromiseResolveThenable
// synchronously rather than enqueuing a further job, so the deferred resolution
// does not add an extra tick relative to normal assimilation. Only a "then"
// getter or an exotic object defers; a "then" data property resolves exactly
// as resolve(thenable) does.

// Measure at which microtask "tick" the outer reaction fires, by racing it
// against a chain of native-promise reactions that bump `tick`.
function measure(installResolution) {
  const {promise} = Promise.withResolvers();

  let ticksAtFulfill = -1;
  let tick = 0;
  promise.then(() => { ticksAtFulfill = tick; });

  installResolution(promise);

  let pending = Promise.resolve();
  for (let i = 0; i < 6; i++) {
    pending = pending.then(() => { tick++; });
  }
  drainJobQueue();
  assertEq(ticksAtFulfill >= 0, true, "reaction fired at some tick");
  return ticksAtFulfill;
}

// Baseline: a normal resolve(thenable). The resolve function lives on the
// capability, so we need the resolvers; reproduce measure() inline to get it.
function measureNormal(resolution) {
  const {promise, resolve} = Promise.withResolvers();

  let ticksAtFulfill = -1;
  let tick = 0;
  promise.then(() => { ticksAtFulfill = tick; });

  resolve(resolution);

  let pending = Promise.resolve();
  for (let i = 0; i < 6; i++) {
    pending = pending.then(() => { tick++; });
  }
  drainJobQueue();
  assertEq(ticksAtFulfill >= 0, true, "reaction fired at some tick");
  return ticksAtFulfill;
}

// Plain callable-"then" thenable.
{
  const baseline =
      measureNormal({ then(onFulfilled) { onFulfilled("normal"); } });
  const safe = measure(p => {
    safeResolvePromise(p, { then(onFulfilled) { onFulfilled("safe"); } });
  });
  assertEq(safe, baseline,
           "SafeResolve(thenable) settles at the same tick as resolve(thenable)");
}

// A "then" getter: SafeResolve defers, and the deferred job calls "then"
// directly, so it still settles at the same tick.
{
  const makeThenable = value => ({
    get then() { return onFulfilled => onFulfilled(value); },
  });
  const baseline = measureNormal(makeThenable("normal"));
  const safe = measure(p => safeResolvePromise(p, makeThenable("safe")));
  assertEq(safe, baseline,
           "SafeResolve(getter thenable) settles at the same tick as resolve()");
}

// A real Promise as the resolution value: same invariant.
{
  let baseTick;
  {
    const {promise, resolve} = Promise.withResolvers();
    let ticksAtFulfill = -1, tick = 0;
    promise.then(() => { ticksAtFulfill = tick; });
    resolve(Promise.resolve("inner"));
    let pending = Promise.resolve();
    for (let i = 0; i < 7; i++) pending = pending.then(() => { tick++; });
    drainJobQueue();
    baseTick = ticksAtFulfill;
  }

  let safeTick;
  {
    const {promise} = Promise.withResolvers();
    let ticksAtFulfill = -1, tick = 0;
    promise.then(() => { ticksAtFulfill = tick; });
    safeResolvePromise(promise, Promise.resolve("inner"));
    let pending = Promise.resolve();
    for (let i = 0; i < 7; i++) pending = pending.then(() => { tick++; });
    drainJobQueue();
    safeTick = ticksAtFulfill;
  }

  assertEq(safeTick, baseTick,
           "promise-with-promise via SafeResolve settles at the same tick");
}

// An accessor without a getter returns undefined without running user code.
const makeSetterOnlyThenObject = () =>
  Object.defineProperty({}, "then", {set(_) {}});

for (const value of [
  makeSetterOnlyThenObject(),
  Object.create(makeSetterOnlyThenObject()),
]) {
  const {promise} = Promise.withResolvers();
  const order = [];
  safeResolvePromise(promise, value);
  promise.then(() => order.push("promise"));
  Promise.resolve().then(() => order.push("next"));
  drainJobQueue();

  assertEq(order.join(","), "promise,next");
}
