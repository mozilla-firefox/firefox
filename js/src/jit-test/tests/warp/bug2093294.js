function makeAccessors() {
  let v = 0;
  return {
    get() { return v; },
    set(x) { v = x; },
  };
}

function makeHolder() {
  let h = 1;
  function holder() { return h; }
  holder();
  return holder;
}

function testSetter() {
  let holder = makeHolder();
  let recv = Object.create(holder);
  function site(o, x, d) {
    if (d) {
      o.x = x;
    }
  }
  for (let i = 0; i < 200; i++) {
    site(null, 0, false);
  }

  let a1 = makeAccessors();
  Object.defineProperty(holder, "x", {set: a1.set, configurable: true});
  site(recv, 1, true);
  assertEq(a1.get(), 1);

  let a2 = makeAccessors();
  Object.defineProperty(holder, "x", {set: a2.set, configurable: true});
  for (let i = 0; i < 2000; i++) {
    site(recv, i, true);
    assertEq(a2.get(), i);
  }
  assertEq(a1.get(), 1);
  assertEq(holder(), 1);
}
testSetter();

function testGetter() {
  let holder = makeHolder();
  let recv = Object.create(holder);
  function site(o, d) {
    if (d) {
      return o.x;
    }
  }
  for (let i = 0; i < 200; i++) {
    site(null, false);
  }

  let a1 = makeAccessors();
  a1.set(1);
  Object.defineProperty(holder, "x", {get: a1.get, configurable: true});
  assertEq(site(recv, true), 1);

  let a2 = makeAccessors();
  Object.defineProperty(holder, "x", {get: a2.get, configurable: true});
  for (let i = 0; i < 2000; i++) {
    a2.set(i);
    assertEq(site(recv, true), i);
  }
  assertEq(holder(), 1);
}
testGetter();
