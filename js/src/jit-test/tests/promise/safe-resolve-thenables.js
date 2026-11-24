// JS::SafeResolve never runs user code on the caller's stack. A "then" getter
// or an exotic object defers Get(resolution, "then") to a microtask. A "then"
// data property is read synchronously, and, as with ordinary resolution, a
// callable "then" is called from a job.

// ---------------------------------------------------------------------------
// Plain thenable with a callable data "then" property on the object itself.
{
  const {promise} = Promise.withResolvers();

  const observations = [];
  const thenable = {
     then(onFulfilled, onRejected) {
        observations.push("then-called");
        onFulfilled(42);
    },
  };

  safeResolvePromise(promise, thenable);
  // "then" is called from a job, not synchronously.
  assertEq(observations.length, 0);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(observations[0], "then-called");
  assertEq(settled, 42);
}

// ---------------------------------------------------------------------------
// Thenable where "then" is an accessor (getter). Pure lookup can't read it;
// SafeResolve must defer and the getter runs in the job, not synchronously.
{
  const {promise} = Promise.withResolvers();

  const log = [];
  const thenable = {
    get then() {
      log.push("get-then");
      return function(onFulfilled) {
        log.push("call-then");
        onFulfilled("from-getter");
      };
    },
  };

  safeResolvePromise(promise, thenable);
  // The getter must not have run yet.
  assertEq(log.length, 0);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(log.join(","), "get-then,call-then");
  assertEq(settled, "from-getter");
}

// ---------------------------------------------------------------------------
// Thenable where "then" lives on the prototype (inherited). Still called from
// a job.
{
  const {promise} = Promise.withResolvers();

  const log = [];
  class Thenable {
    then(onFulfilled) {
      log.push("proto-then");
      onFulfilled("proto-value");
    }
  }
  const thenable = new Thenable();
  safeResolvePromise(promise, thenable);
  assertEq(log.length, 0);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(log[0], "proto-then");
  assertEq(settled, "proto-value");
}

// ---------------------------------------------------------------------------
// Proxy with a "then" trap. Proxies MUST force deferral regardless of trap
// behavior, because any MOP access could run user code.
{
  const {promise} = Promise.withResolvers();

  const log = [];
  const target = {};
  const handler = {
    get(t, name) {
      log.push("proxy-get:" + String(name));
      if (name === "then") {
        return function(onFulfilled) {
          log.push("proxy-then");
          onFulfilled("proxy-value");
        };
      }
      return t[name];
    },
  };
  const thenable = new Proxy(target, handler);
  safeResolvePromise(promise, thenable);
  // No MOP access should have happened yet.
  assertEq(log.length, 0);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  // In the microtask, Get(resolution, "then") happens exactly once, then the
  // assimilation job calls the returned function (a separate MOP access —
  // "get" trap is not invoked for calling a function already retrieved).
  assertEq(log[0], "proxy-get:then");
  assertEq(log[1], "proxy-then");
  assertEq(settled, "proxy-value");
}

// ---------------------------------------------------------------------------
// Revoked proxy: Get("then") throws. The rejection must happen in the job,
// not synchronously.
{
  const {promise} = Promise.withResolvers();
  const {proxy, revoke} = Proxy.revocable({}, {});
  revoke();

  safeResolvePromise(promise, proxy);
  let result = null;
  promise.then(v => { result = {fulfilled: v}; },
               e => { result = {rejected: e}; });
  drainJobQueue();
  assertEq(result !== null, true);
  assertEq("rejected" in result, true);
}

// ---------------------------------------------------------------------------
// Proxy whose "then" is NOT callable. Must still defer, then fulfill with the
// object itself (because IsCallable(then) is false in PerformPromiseResolution).
{
  const {promise} = Promise.withResolvers();
  const target = {};
  const proxy = new Proxy(target, {
    get(t, name) { return name === "then" ? 42 : t[name]; },
  });
  safeResolvePromise(promise, proxy);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(settled, proxy);
}

// ---------------------------------------------------------------------------
// A real Promise as the resolution value, with an observable own "then" so we
// can verify the call happens in a job, not on the caller's stack.
{
  const {promise} = Promise.withResolvers();
  const inner = Promise.resolve("inner-value");
  const observations = [];
  const originalThen = Promise.prototype.then;
  Object.defineProperty(inner, "then", {
    value: function(...args) {
      observations.push("then-called");
      return originalThen.apply(this, args);
    },
    writable: true, configurable: true,
  });

  safeResolvePromise(promise, inner);
  assertEq(observations.length, 0);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(observations[0], "then-called");
  assertEq(settled, "inner-value");
}

// ---------------------------------------------------------------------------
// Cross-compartment promise as the resolution value.
{
  const g = newGlobal({newCompartment: true});
  const {promise} = Promise.withResolvers();
  const inner = g.eval("Promise.resolve('cross-realm')");
  safeResolvePromise(promise, inner);

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(settled, "cross-realm");
}

// ---------------------------------------------------------------------------
// A "then" data property is read when SafeResolve is called, as for ordinary
// resolution: replacing it afterwards has no effect.
{
  const {promise} = Promise.withResolvers();
  const log = [];
  const thenable = {
    then(onFulfilled) {
      log.push("original-then");
      onFulfilled(1);
    },
  };

  safeResolvePromise(promise, thenable);
  thenable.then = onFulfilled => {
    log.push("replacement-then");
    onFulfilled(2);
  };

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(log.join(","), "original-then");
  assertEq(settled, 1);
}

// ---------------------------------------------------------------------------
// A "then" getter defers the lookup to the job, so a "then" installed after
// SafeResolve is the one called.
{
  const {promise} = Promise.withResolvers();
  const log = [];
  const thenable = {
    get then() {
      log.push("getter");
      return onFulfilled => onFulfilled(1);
    },
  };

  safeResolvePromise(promise, thenable);
  Object.defineProperty(thenable, "then", {
    value: onFulfilled => {
      log.push("replacement-then");
      onFulfilled(2);
    },
  });

  let settled;
  promise.then(v => { settled = v; });
  drainJobQueue();
  assertEq(log.join(","), "replacement-then");
  assertEq(settled, 2);
}
