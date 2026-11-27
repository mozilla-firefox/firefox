// The builtin thenable job, which runs when a promise is resolved with a native
// promise, may skip SpeciesConstructor only while it is unobservable. Each case
// runs in a fresh global so that popping the Promise lookup fuse doesn't
// affect the others.

function run(setup) {
  const g = newGlobal();
  g.log = [];
  g.evaluate(`
    var p = Promise.resolve(1);
    (${setup})(p);
    var result;
    new Promise(resolve => resolve(p)).then(v => { result = v; });
  `);
  g.drainJobQueue();
  return g;
}

// Unmodified.
{
  const g = run(p => {});
  assertEq(g.result, 1);
  assertEq(g.log.length, 0);
}

// A Symbol.species getter on Promise must be observed.
{
  const g = run(p => {
    Object.defineProperty(Promise, Symbol.species, {
      get() {
        log.push("species");
        return Promise;
      },
      configurable: true,
    });
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("species"), true);
}

// A getter for Promise.prototype.constructor must be observed.
{
  const g = run(p => {
    Object.defineProperty(Promise.prototype, "constructor", {
      get() {
        log.push("constructor");
        return Promise;
      },
      configurable: true,
    });
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("constructor"), true);
}

// An own "constructor" property on the thenable must be observed.
{
  const g = run(p => {
    Object.defineProperty(p, "constructor", {
      get() {
        log.push("own constructor");
        return Promise;
      },
    });
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("own constructor"), true);
}

// A species constructor which isn't %Promise% must be constructed.
{
  const g = run(p => {
    class MyPromise extends Promise {
      constructor(executor) {
        log.push("MyPromise");
        super(executor);
      }
    }
    p.constructor = MyPromise;
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("MyPromise"), true);
}

// SpeciesConstructor runs in the thenable job, so a Symbol.species getter
// installed after the resolution but before the job runs must be observed.
{
  const g = newGlobal();
  g.log = [];
  g.evaluate(`
    var p = Promise.resolve(1);
    var result;
    new Promise(resolve => resolve(p)).then(v => { result = v; });
    Object.defineProperty(Promise, Symbol.species, {
      get() {
        log.push("species");
        return Promise;
      },
      configurable: true,
    });
  `);
  g.drainJobQueue();
  assertEq(g.result, 1);
  assertEq(g.log.includes("species"), true);
}
