// |jit-test| --async-stacks-capture-debuggee-only
// Reactions to pending promises can be embedded in the promise. Check they
// behave like reaction records.

function pending() {
  let resolve, reject;
  let promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

// Fulfill and reject a promise with a single reaction.
{
  let log = [];
  let a = pending();
  let b = pending();
  a.promise.then(v => log.push("a:" + v));
  b.promise.catch(e => log.push("b:" + e));
  a.resolve(1);
  b.reject(2);
  drainJobQueue();
  assertEq(log.join(), "a:1,b:2");
}

// Unhandled sides of a reaction pass the value through to the derived promise.
{
  let log = [];
  let a = pending();
  let b = pending();
  a.promise.catch(() => log.push("unexpected")).then(v => log.push("a:" + v));
  b.promise.then(() => log.push("unexpected")).catch(e => log.push("b:" + e));
  a.resolve(1);
  b.reject(2);
  drainJobQueue();
  assertEq(log.join(), "a:1,b:2");
}

// Adding more reactions after the first one, and the order reactions run in.
{
  let log = [];
  let a = pending();
  let b = pending();
  a.promise.then(v => log.push("a1:" + v));
  b.promise.then(v => log.push("b1:" + v));
  b.promise.then(v => log.push("b2:" + v));
  b.promise.then(v => log.push("b3:" + v));
  b.resolve(1);
  a.resolve(2);
  drainJobQueue();
  assertEq(log.join(), "b1:1,b2:1,b3:1,a1:2");
}

// Reactions with a resolving handler: resolving a promise with a pending one.
{
  let log = [];
  let a = pending();
  let b = pending();
  b.promise.then(v => log.push("b:" + v));
  b.resolve(a.promise);
  drainJobQueue();
  assertEq(log.length, 0);
  a.resolve(3);
  drainJobQueue();
  assertEq(log.join(), "b:3");
}

// Await of a pending promise.
{
  let log = [];
  let a = pending();
  let b = pending();
  (async () => log.push("a:" + (await a.promise)))();
  (async () => {
    try {
      await b.promise;
    } catch (e) {
      log.push("b:" + e);
    }
  })();
  b.reject(4);
  a.resolve(5);
  drainJobQueue();
  assertEq(log.join(), "b:4,a:5");
}

// Async generators awaiting a pending promise.
{
  let log = [];
  let a = pending();
  async function* gen() {
    yield await a.promise;
  }
  gen().next().then(r => log.push(r.value));
  a.resolve(6);
  drainJobQueue();
  assertEq(log.join(), "6");
}

// Combinators.
{
  let log = [];
  let a = pending();
  let b = pending();
  Promise.all([a.promise, b.promise]).then(v => log.push("all:" + v));
  Promise.race([a.promise, b.promise]).then(v => log.push("race:" + v));
  b.resolve(7);
  a.resolve(8);
  drainJobQueue();
  assertEq(log.join(), "race:7,all:8,7");
}

// A thrown handler rejects the derived promise.
{
  let log = [];
  let a = pending();
  a.promise
    .then(() => {
      throw 9;
    })
    .catch(e => log.push("caught:" + e));
  a.resolve();
  drainJobQueue();
  assertEq(log.join(), "caught:9");
}

// Settling a promise without running its reactions drops the embedded reaction.
{
  let log = [];
  let a = pending();
  a.promise.then(() => log.push("unexpected"));
  settlePromiseNow(a.promise);
  drainJobQueue();
  assertEq(log.length, 0);
}

// Cross-realm uses, in the same compartment.
{
  let g = newGlobal({ sameCompartmentAs: this });
  g.pending = pending;
  let log = [];

  // A reaction from another realm.
  let a = g.eval("pending()");
  Promise.prototype.then.call(a.promise, v => log.push("a:" + v));

  // A handler from another realm.
  let b = pending();
  b.promise.then(g.eval("(log => v => log.push('b:' + v))")(log));

  // Settling a promise from another realm.
  let c = pending();
  c.promise.then(v => log.push("c:" + v));
  g.resolvePromise(c.promise, 3);

  a.resolve(1);
  b.resolve(2);
  drainJobQueue();
  assertEq(log.join(), "c:3,a:1,b:2");
}

// Running out of memory while settling from another realm still settles the
// promise. Use a promise with default resolving functions, which are not
// called in the promise's realm.
if ("oomAtAllocation" in this) {
  let g = newGlobal({ sameCompartmentAs: this });
  for (let i = 1; ; i++) {
    let derived = pending().promise.then(v => v);
    derived.then(v => v);
    oomAtAllocation(i);
    try {
      g.resolvePromise(derived, 1);
    } catch (e) {}
    let failed = resetOOMFailure();

    let settled = false;
    derived.then(() => {
      settled = true;
    });
    drainJobQueue();
    assertEq(settled, true);
    if (!failed) {
      break;
    }
  }
}

// Inflate at settlement when the selected handler cannot run inline.
for (let reject of [false, true]) {
  let g = newGlobal({ sameCompartmentAs: this });
  let handler = g.eval("new Proxy(() => globalThis, {})");
  let a = pending();
  let result;
  a.promise.then(handler, handler).then(v => {
    result = v;
  });
  (reject ? a.reject : a.resolve)(1);
  drainJobQueue();
  assertEq(result, g);
}

// A revoked handler must still reject the result promise after inflation.
for (let reject of [false, true]) {
  let { proxy, revoke } = Proxy.revocable(() => {}, {});
  let a = pending();
  let error;
  a.promise.then(proxy, proxy).catch(e => {
    error = e;
  });
  revoke();
  (reject ? a.reject : a.resolve)(1);
  drainJobQueue();
  assertEq(error instanceof TypeError, true);
}

// Running out of memory while moving an embedded reaction into a reaction record.
oomTest(() => {
  let log = [];
  let a = pending();
  a.promise.then(v => log.push(v));
  a.promise.then(v => log.push(v));
  a.resolve(1);
  drainJobQueue();
});
