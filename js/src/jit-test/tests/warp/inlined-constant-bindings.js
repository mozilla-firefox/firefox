// |jit-test| --fast-warmup; --no-threads

function testConst() {
  const K = 3;
  const O = {x: 7};
  const S = ["a", "b", "c"].join("") + String(Math.random() < 2);
  function getK() { return K; }
  function getOx() { return O.x; }
  function getSLength() { return S.length; }
  function f(i) { return getK() + getOx() + getSLength() + i; }
  let sum = 0;
  for (let i = 0; i < 200; i++) {
    sum += f(i);
  }
  assertEq(sum, 200 * 17 + 199 * 100);
  O.x = 1;
  assertEq(f(0), 11);
}
testConst();

function testLet() {
  let v = 0;
  function getV() { return v; }
  function f() { return getV(); }
  for (let i = 0; i < 200; i++) {
    v = i;
    assertEq(f(), i);
  }
}
testLet();

function testTDZ() {
  function f() { return getC(); }
  function getC() { return C; }
  for (let i = 0; i < 100; i++) {
    let threw = false;
    try {
      f();
    } catch (e) {
      threw = e instanceof ReferenceError;
    }
    assertEq(threw, true);
  }
  const C = 5;
  for (let i = 0; i < 100; i++) {
    assertEq(f(), 5);
  }
}
testTDZ();

// Closures with the same script but different environments.
function makeGetter(n) {
  const N = n;
  return function () { return N; };
}
function testClosures() {
  const getters = [];
  for (let i = 0; i < 4; i++) {
    getters.push(makeGetter(i));
  }
  for (let g of getters) {
    let call = () => g();
    for (let i = 0; i < 100; i++) {
      assertEq(call(), getters.indexOf(g));
    }
  }
  function callAll() {
    let s = 0;
    for (let g of getters) {
      s += g();
    }
    return s;
  }
  for (let i = 0; i < 200; i++) {
    assertEq(callAll(), 6);
  }
}
testClosures();

// A const in a loop body has a different environment for each iteration.
function testLoopBody() {
  for (let j = 0; j < 20; j++) {
    const obj = {j};
    function getObj() { return obj; }
    function f() { return getObj().j; }
    for (let i = 0; i < 50; i++) {
      assertEq(f(), j);
    }
  }
}
testLoopBody();

function testAccessors() {
  const K = 4;
  let stored = 0;
  const o = {
    get k() { return K; },
    set k(v) { stored = v + K; },
  };
  function f(i) {
    o.k = i;
    return o.k + stored;
  }
  for (let i = 0; i < 200; i++) {
    assertEq(f(i), 4 + i + 4);
  }
}
testAccessors();

function testBound() {
  const K = 5;
  function getK(x) { return K + x; }
  const bound = getK.bind(null);
  function f(i) { return bound(i); }
  for (let i = 0; i < 200; i++) {
    assertEq(f(i), 5 + i);
  }
}
testBound();

// Sloppy direct eval can add bindings to the function's environment.
function testSloppyEval() {
  const K = 6;
  eval("var added = 1");
  function getK() { return K + added; }
  function f() { return getK(); }
  for (let i = 0; i < 200; i++) {
    assertEq(f(), 7);
  }
}
testSloppyEval();

// Named lambdas get their own environment when called.
function testNamedLambda() {
  const K = 7;
  const g = function named() {
    return () => named;
  };
  const h = function getK() { return K + (typeof getK); };
  function f() { return h(); }
  for (let i = 0; i < 200; i++) {
    assertEq(f(), "7function");
    assertEq(typeof g()(), "function");
  }
}
testNamedLambda();

// The named lambda environment is created when the function is called, so it's
// one of the function's own environments.
function testNamedLambdaEnvironment() {
  const K = 8;
  const h = function getK() {
    const self = () => getK;
    return K + (self() === getK ? 0 : 100);
  };
  function f() { return h(); }
  for (let i = 0; i < 200; i++) {
    assertEq(f(), 8);
  }
}
testNamedLambdaEnvironment();
