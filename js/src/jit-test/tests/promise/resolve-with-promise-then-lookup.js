// Resolving a promise with a native promise may skip looking up its "then"
// property only while that lookup is unobservable. Each case runs in a fresh
// global so that popping the Promise lookup fuse doesn't affect the others.

function run(setup) {
  const g = newGlobal();
  g.log = [];
  g.evaluate(`
    var p = Promise.resolve(1);
    (${setup})(p);
    var result;
    Promise.resolve().then(() => p).then(v => { result = v; });
  `);
  g.drainJobQueue();
  return g;
}

// Unmodified: the original then runs and the chain is fulfilled.
{
  const g = run(p => {});
  assertEq(g.result, 1);
  assertEq(g.log.length, 0);
}

// Replacing Promise.prototype.then must be observed.
{
  const g = run(p => {
    const then = Promise.prototype.then;
    Promise.prototype.then = function(...args) {
      log.push("patched then");
      return then.apply(this, args);
    };
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("patched then"), true);
}

// A getter for Promise.prototype.then must be observed.
{
  const g = run(p => {
    const then = Promise.prototype.then;
    Object.defineProperty(Promise.prototype, "then", {
      get() {
        log.push("then getter");
        return then;
      },
      configurable: true,
    });
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("then getter"), true);
}

// An own "then" property on the resolution must be observed.
{
  const g = run(p => {
    const then = Promise.prototype.then;
    p.then = function(...args) {
      log.push("own then");
      return then.apply(this, args);
    };
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("own then"), true);
}

// A resolution whose prototype isn't the original Promise.prototype must be
// observed.
{
  const g = run(p => {
    class MyPromise extends Promise {
      then(...args) {
        log.push("subclass then");
        return super.then(...args);
      }
    }
    Object.setPrototypeOf(p, MyPromise.prototype);
  });
  assertEq(g.result, 1);
  assertEq(g.log.includes("subclass then"), true);
}

// A native promise from another realm uses its own realm's then.
{
  const g = newGlobal();
  const other = newGlobal({sameCompartmentAs: g});
  g.log = [];
  other.log = g.log;
  other.evaluate(`
    const then = Promise.prototype.then;
    Promise.prototype.then = function(...args) {
      log.push("other realm then");
      return then.apply(this, args);
    };
  `);
  g.other = other;
  g.evaluate(`
    var p = other.Promise.resolve(2);
    var result;
    Promise.resolve().then(() => p).then(v => { result = v; });
  `);
  g.drainJobQueue();
  assertEq(g.result, 2);
  assertEq(g.log.includes("other realm then"), true);
}

// The "then" property is looked up when resolving, so replacing
// Promise.prototype.then after that but before the thenable job runs isn't
// observed.
{
  const g = newGlobal();
  g.log = [];
  g.evaluate(`
    var p = Promise.resolve(1);
    var result;
    new Promise(resolve => resolve(p)).then(v => { result = v; });
    const then = Promise.prototype.then;
    Promise.prototype.then = function(...args) {
      log.push("patched then");
      return then.apply(this, args);
    };
  `);
  g.drainJobQueue();
  assertEq(g.result, 1);
  assertEq(g.log.length, 0);
}
