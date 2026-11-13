// |jit-test| --fast-warmup; --no-threads

// Stores to bindings in an environment that's known at compile time. All
// bindings are captured, so a19 is stored in a dynamic slot.
function testStores() {
  let x = 0;
  let a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0, a8 = 0;
  let a9 = 0, a10 = 0, a11 = 0, a12 = 0, a13 = 0, a14 = 0, a15 = 0, a16 = 0;
  let a17 = 0, a18 = 0, a19 = 0;
  function set(v) {
    x = v;
    a19 = v + 1;
  }
  function get() {
    return x + a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 +
           a12 + a13 + a14 + a15 + a16 + a17 + a18 + a19;
  }
  function f(v) { set(v); return get(); }
  for (let i = 0; i < 300; i++) {
    assertEq(f(i), 2 * i + 1);
  }
  assertEq(x, 299);
  assertEq(a19, 300);
}
testStores();

// Nursery objects stored in a tenured environment need a post barrier.
function testPostBarrier() {
  let obj = null;
  function set(v) { obj = {v}; }
  function f(v) { set(v); }
  gc();
  for (let i = 0; i < 300; i++) {
    f(i);
    if (i % 50 === 0) {
      minorgc();
    }
    assertEq(obj.v, i);
  }
  minorgc();
  assertEq(obj.v, 299);
}
testPostBarrier();
