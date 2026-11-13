// |jit-test| --fast-warmup; --no-threads

// Mutable bindings loaded from an environment that's known at compile time.
function testMutable() {
  let x = 0;
  var y = 0;
  function f() { return 1; }
  function readAll() { return x + y + f(); }
  function g() { return readAll(); }
  for (let i = 0; i < 300; i++) {
    x = i;
    y = 2 * i;
    if (i === 150) {
      f = () => 2;
    }
    assertEq(g(), 3 * i + (i < 150 ? 1 : 2));
  }
}
testMutable();

// Enough bindings that some of them are stored in dynamic slots.
function testDynamicSlots() {
  let a0 = 0, a1 = 1, a2 = 2, a3 = 3, a4 = 4, a5 = 5, a6 = 6, a7 = 7;
  let a8 = 8, a9 = 9, a10 = 10, a11 = 11, a12 = 12, a13 = 13, a14 = 14;
  let a15 = 15, a16 = 16, a17 = 17, a18 = 18, a19 = 19;
  function read() {
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 +
           a13 + a14 + a15 + a16 + a17 + a18 + a19;
  }
  function g() { return read(); }
  for (let i = 0; i < 300; i++) {
    a19 = i;
    assertEq(g(), 171 + i);
  }
}
testDynamicSlots();

// Bindings two scopes out.
function testOuter() {
  let v = 1;
  function middle() {
    let w = 2;
    function inner() { return v + w; }
    function g() { return inner(); }
    let r = 0;
    for (let i = 0; i < 200; i++) {
      v = i;
      r += g();
    }
    return r;
  }
  assertEq(middle(), 199 * 100 + 400);
}
testOuter();
