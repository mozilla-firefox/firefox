// JS::SafeResolve latches the promise's resolving functions on return, so any
// subsequent resolve/reject (including a second SafeResolve) is a silent
// no-op. The promise also stays pending through the deferral window.

// ---------------------------------------------------------------------------
// Fast path latches synchronously. A follow-up resolve() must be ignored.
{
  const {promise, resolve, reject} = Promise.withResolvers();
  safeResolvePromise(promise, "first");
  resolve("second");  // should no-op, promise already fulfilled.
  reject("third");    // should no-op.

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(settled, "first");
}

// ---------------------------------------------------------------------------
// Deferred path latches synchronously too: before the microtask runs, the
// resolving functions are already no-ops, and a direct resolve() on the
// capability is swallowed.
{
  const {promise, resolve, reject} = Promise.withResolvers();

  const thenable = {
    get then() {
      return onFulfilled => onFulfilled("from-thenable");
    },
  };

  safeResolvePromise(promise, thenable);
  // Capability is latched even though the promise is still pending.
  resolve("racing-resolve");
  reject("racing-reject");

  let settled = null, failed = null;
  promise.then(v => { settled = v; }, e => { failed = e; });
  drainJobQueue();
  assertEq(failed, null);
  assertEq(settled, "from-thenable");
}

// ---------------------------------------------------------------------------
// Two SafeResolve calls: second is a no-op. First-wins semantics.
{
  const {promise} = Promise.withResolvers();
  const t1 = {then(r) { r("first"); }};
  const t2 = {then(r) { r("second"); }};
  safeResolvePromise(promise, t1);
  safeResolvePromise(promise, t2);  // silent no-op

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(settled, "first");
}

// ---------------------------------------------------------------------------
// SafeResolve after the promise has already settled is a no-op (the top-level
// pending check short-circuits before we touch anything).
{
  const {promise, resolve} = Promise.withResolvers();
  resolve("preset");
  safeResolvePromise(promise, {then(r){ r("ignored"); }});

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(settled, "preset");
}

// ---------------------------------------------------------------------------
// Promise stays observably pending during the deferral window: .then
// callbacks are NOT fired before drainJobQueue().
{
  const {promise} = Promise.withResolvers();
  const thenable = {get then() { return r => r("eventually"); }};

  let fired = false;
  promise.then(() => { fired = true; });

  safeResolvePromise(promise, thenable);
  // Latched, but promise is still pending; reaction must not have fired yet.
  assertEq(fired, false);

  drainJobQueue();
  assertEq(fired, true);
}

// Can't resolve promsise with itself, even via SafeResolve.
{
  const {promise} = Promise.withResolvers();
  safeResolvePromise(promise, promise);

  let result = null;
  promise.then(v => { result = {fulfilled: v}; },
               e => { result = {rejected: e}; });
  drainJobQueue();
  assertEq(result !== null, true);
  assertEq("rejected" in result, true);
  assertEq(result.rejected instanceof TypeError, true);
}
