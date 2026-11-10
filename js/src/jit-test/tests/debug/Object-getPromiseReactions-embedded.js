// |jit-test| --async-stacks-capture-debuggee-only
// The debugger sees reactions which were embedded in a pending promise
// before the global became a debuggee.

const g = newGlobal({ newCompartment: true });
g.eval(`
  var resolve1, resolve2;
  var p1 = new Promise(r => { resolve1 = r; });
  function onFulfilled(v) { return v + 1; }
  var p1Then = p1.then(onFulfilled);

  var p2 = new Promise(r => { resolve2 = r; });
  var p2Then = p2.then(onFulfilled);

  var p3 = new Promise(() => {});
  async function f() { await p3; }
  f();
`);

const dbg = new Debugger();
const DOg = dbg.addDebuggee(g);
const [DOp1, DOp1Then, DOp2, DOp2Then, DOp3, DOonFulfilled] = [
  g.p1,
  g.p1Then,
  g.p2,
  g.p2Then,
  g.p3,
  g.onFulfilled,
].map(v => DOg.makeDebuggeeValue(v));

let reactions = DOp1.getPromiseReactions();
assertEq(reactions.length, 1);
assertEq(reactions[0].resolve, DOonFulfilled);
assertEq(reactions[0].reject, undefined);
assertEq(reactions[0].result, DOp1Then);

let dependents = DOp2.promiseDependentPromises;
assertEq(dependents.length, 1);
assertEq(dependents[0], DOp2Then);

reactions = DOp3.getPromiseReactions();
assertEq(reactions.length, 1);
assertEq(reactions[0] instanceof Debugger.Frame, true);

// The reactions still run.
g.resolve1(1);
g.resolve2(10);
g.eval("var log = []; p1Then.then(v => log.push(v)); p2Then.then(v => log.push(v));");
drainJobQueue();
assertEq(g.log.join(), "2,11");
