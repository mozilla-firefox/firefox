// Test the promise property on async generator function frames.

load(libdir + 'asserts.js');

var g = newGlobal({ newCompartment: true });
var dbg = Debugger(g);
g.eval(`
async function* f() {
  debugger;
  yield 42;
  debugger;
  return 50
}
`);

let frame;
const promises = [];
dbg.onEnterFrame = function(f) {
  frame = f;
  promises.push(["enter", frame.asyncPromise]);

  frame.onPop = function() {
    promises.push(["leave", frame.asyncPromise]);
  };

  dbg.onDebuggerStatement = function(f) {
    assertEq(f, frame);
    promises.push(["dbg", frame.asyncPromise]);
  };
};


(async () => {
  const it = g.f();

  // Promise is null until initial iteration.
  assertEq(frame.asyncPromise, null);

  assertEq(promises.length, 2);
  assertEq(promises[0][0], "enter");
  assertEq(promises[0][1], null);

  assertEq(promises[1][0], "leave");
  assertEq(promises[1][1], null);


  const result1Promise = it.next();

  const frameIt1Promise = frame.asyncPromise;
  assertEq(frameIt1Promise instanceof Debugger.Object, true);
  assertEq(frameIt1Promise.unsafeDereference(), result1Promise);

  assertEq(promises.length, 5);
  assertEq(promises[2][0], "enter");
  assertEq(promises[2][1], frameIt1Promise);

  assertEq(promises[3][0], "dbg");
  assertEq(promises[3][1], frameIt1Promise);

  assertEq(promises[4][0], "leave");
  assertEq(promises[4][1], frameIt1Promise);

  await result1Promise;

  // Iteration step has fully completed, so promise state has been cleared.
  // The frame isn't resumed for the yield's await.
  assertEq(frame.asyncPromise, null);
  assertEq(promises.length, 5);


  const result2Promise = it.next();

  const frameIt2Promise = frame.asyncPromise;
  assertEq(frameIt2Promise instanceof Debugger.Object, true);
  assertEq(frameIt2Promise.unsafeDereference(), result2Promise);
  assertEq(frameIt1Promise !== frameIt2Promise, true);

  assertEq(promises.length, 8);
  assertEq(promises[5][0], "enter");
  assertEq(promises[5][1], frameIt2Promise);

  assertEq(promises[6][0], "dbg");
  assertEq(promises[6][1], frameIt2Promise);

  assertEq(promises[7][0], "leave");
  assertEq(promises[7][1], frameIt2Promise);

  await result2Promise;

  // Frame has terminated, so accessing the property throws.
  assertThrowsInstanceOf(() => frame.asyncPromise, Error);

  // The frame is resumed for the return's await.
  assertEq(promises.length, 10);
  assertEq(promises[8][0], "enter");
  assertEq(promises[8][1], frameIt2Promise);

  assertEq(promises[9][0], "leave");
  assertEq(promises[9][1], frameIt2Promise);
})();
