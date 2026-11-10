// |jit-test| --enable-stub-frame-pointers; --fast-warmup; --no-threads

// Exercise every path through the Ion generic call trampoline with stub frame
// pointers enabled.

with ({}) {}

const ITERS = 300;

function assertArrayEq(a, b) {
  assertEq(a.length, b.length);
  for (var i = 0; i < a.length; i++) {
    assertEq(a[i], b[i]);
  }
}

function assertThrowsTypeError(f) {
  var caught = false;
  try {
    f();
  } catch (e) {
    assertEq(e instanceof TypeError, true);
    caught = true;
  }
  assertEq(caught, true);
}

function call1(f) { return f(1); }
function call2(f) { return f(1, 2); }
function construct1(F) { return new F(1); }
function callM0(o) { return o.m(); }
function callM2(o, a, b) { return o.m(a, b); }

function s1(x) { return x; }
function s2(x, y) { return [x, y]; }
function s3(x, y, z) { return [x, y, z]; }
function s10(a, b, c, d, e, f, g, h, i, j) { return [a, b, c, d, e, f, g, h, i, j]; }

var manyParams = [];
for (var i = 0; i < 400; i++) {
  manyParams.push("p" + i);
}
var s400 = new Function(...manyParams, "return arguments.length + ':' + p399");

function testScripted() {
  for (var i = 0; i < ITERS; i++) {
    // No underflow.
    assertEq(call1(s1), 1);
    // Underflow by one (odd) and two (even) arguments.
    assertArrayEq(call1(s2), [1, undefined]);
    assertArrayEq(call1(s3), [1, undefined, undefined]);
    assertArrayEq(call2(s3), [1, 2, undefined]);
    // Underflow by many arguments.
    assertArrayEq(call1(s10), [1, undefined, undefined, undefined, undefined,
                               undefined, undefined, undefined, undefined,
                               undefined]);
    // Underflow by more than JIT_ARGS_LENGTH_MAX: VM call.
    assertEq(call1(s400), "1:undefined");
  }
}
testScripted();

function C1(x) { this.x = x; this.nt = new.target; }
function C2(x, y) { this.args = [x, y]; this.nt = new.target; }
function C3(x, y, z) { this.args = [x, y, z]; this.nt = new.target; }
class K { constructor(x) { this.x = x; } }
var arrow = x => x;
var method = { m(x) { return x; } }.m;

function testConstruct() {
  for (var i = 0; i < ITERS; i++) {
    var o = construct1(C1);
    assertEq(o.x, 1);
    assertEq(o.nt, C1);
    // Constructing with underflow must keep new.target in the right slot.
    o = construct1(C2);
    assertArrayEq(o.args, [1, undefined]);
    assertEq(o.nt, C2);
    o = construct1(C3);
    assertArrayEq(o.args, [1, undefined, undefined]);
    assertEq(o.nt, C3);
    assertEq(construct1(K).x, 1);
    // Native constructors.
    assertEq(construct1(Array).length, 1);
    assertEq(construct1(Uint8Array)[0], 0);
    // Not constructors: VM call, which throws.
    assertThrowsTypeError(() => construct1(arrow));
    assertThrowsTypeError(() => construct1(method));
    // Class constructors can't be called: VM call, which throws.
    assertThrowsTypeError(() => call1(K));
  }
}
testConstruct();

// Make the super() call site polymorphic, then pass a new.target that isn't a
// JSFunction, so that CreateThis returns null and we take the VM call.
function makeDerived(base) {
  return class extends base {
    constructor() {
      super(1);
    }
  };
}
var deriveds = [makeDerived(C1), makeDerived(C2), makeDerived(C3),
                makeDerived(K)];
var proxyNewTarget = new Proxy(function() {}, {});

function testCreateThisNull() {
  for (var i = 0; i < ITERS; i++) {
    var D = deriveds[i % deriveds.length];
    assertEq(new D() instanceof D, true);
    var o = Reflect.construct(D, [], proxyNewTarget);
    assertEq(Object.getPrototypeOf(o), proxyNewTarget.prototype);
  }
}
testCreateThisNull();

function testNative() {
  for (var i = 0; i < ITERS; i++) {
    assertEq(call1(Math.abs), 1);
    assertEq(call2(Math.max), 2);
    assertEq(callM2({m: Math.min}, 3, 4), 3);
  }
}
testNative();

var funCall = Function.prototype.call;
var otherGlobal = newGlobal({sameCompartmentAs: this});
var otherFunCall = otherGlobal.Function.prototype.call;

function tgt(x) { return [this, x]; }
tgt.m = funCall;
function tgt0() { "use strict"; return this; }
tgt0.m = funCall;
function tgtOther(x) { return x; }
tgtOther.m = otherFunCall;
Math.max.m = funCall;
var boundTgt = s2.bind(null, 5);
boundTgt.m = funCall;
funCall.m = funCall;
var funCallTgt = { m: funCall };
var zeroArgReceivers = [tgt0, {m: () => 1}, {m: Math.abs.bind(null, 1)}];
String.prototype.m = funCall;

function testFunCall() {
  var thisv = {};
  for (var i = 0; i < ITERS; i++) {
    // fun_call with arguments.
    var r = callM2(tgt, thisv, 1);
    assertEq(r[0], thisv);
    assertEq(r[1], 1);
    // fun_call without arguments.
    var zeroArgReceiver = zeroArgReceivers[i % zeroArgReceivers.length];
    assertEq(callM0(zeroArgReceiver), zeroArgReceiver === tgt0 ? undefined : 1);
    // fun_call with a native, bound, or fun_call target.
    assertEq(callM2(Math.max, null, 7), 7);
    assertArrayEq(callM2(boundTgt, null, 1), [5, 1]);
    assertEq(callM2(funCall, tgt0, thisv), thisv);
    // Cross-realm fun_call: VM call.
    assertEq(callM2(tgtOther, null, 1), 1);
    // Non-object `this`: VM call, which throws.
    assertThrowsTypeError(() => callM2("str", 1, 2));
    // Non-callable object `this`: VM call, which throws.
    assertThrowsTypeError(() => callM2(funCallTgt, 1, 2));
  }
}
testFunCall();

var bthis = {};
function bf() { return [this, ...arguments]; }
var b0 = bf.bind(bthis);
var b1 = bf.bind(bthis, 10);
var b2 = bf.bind(bthis, 10, 11);
var b3 = bf.bind(bthis, 10, 11, 12);
var b5 = bf.bind(bthis, 10, 11, 12, 13, 14);
var bNested = b1.bind({}, 20).bind({}, 30);
var bNative = Math.max.bind(null, 100);
var bUnderflow = s10.bind(null, 0);
var bFunCall = funCall.bind(s2, null, 7);
var manyBoundArgs = [];
for (var i = 0; i < 400; i++) {
  manyBoundArgs.push(i);
}
function argsLength() { return arguments.length; }
var bTooMany = argsLength.bind(null, ...manyBoundArgs);

function testBound() {
  for (var i = 0; i < ITERS; i++) {
    assertArrayEq(call2(b0), [bthis, 1, 2]);
    assertArrayEq(call2(b1), [bthis, 10, 1, 2]);
    assertArrayEq(call2(b2), [bthis, 10, 11, 1, 2]);
    assertArrayEq(call2(b3), [bthis, 10, 11, 12, 1, 2]);
    // More than MaxInlineBoundArgs bound arguments.
    assertArrayEq(call2(b5), [bthis, 10, 11, 12, 13, 14, 1, 2]);
    assertArrayEq(call2(bNested), [bthis, 10, 20, 30, 1, 2]);
    assertEq(call2(bNative), 100);
    assertArrayEq(call1(bUnderflow), [0, 1, undefined, undefined, undefined,
                                      undefined, undefined, undefined,
                                      undefined, undefined]);
    assertArrayEq(call1(bFunCall), [7, 1]);
    // Too many arguments: VM call.
    assertEq(call2(bTooMany), 402);
    // Constructing a bound function: VM call.
    assertEq(construct1(C1.bind(null)).x, 1);
  }
}
testBound();

var proxyFun = new Proxy(s1, {});
var ccwFun = newGlobal({newCompartment: true}).evaluate("(function(x) { return x; })");

function testNonFunctionCallables() {
  for (var i = 0; i < ITERS; i++) {
    assertEq(call1(proxyFun), 1);
    assertEq(call1(ccwFun), 1);
    assertEq(construct1(proxyFun) instanceof s1, true);
    assertThrowsTypeError(() => call1({}));
    assertThrowsTypeError(() => construct1({}));
  }
}
testNonFunctionCallables();

// Call a bound function with many bound arguments at increasing stack depths.
// Close to the stack limit, the stack check in the trampoline fails and we take
// the VM call. The call site must be in the recursive function itself, so that
// trial inlining doesn't make it monomorphic.
var bManyLength = argsLength.bind(null, ...manyBoundArgs.slice(0, 370));
var depthCallees = [bManyLength, bManyLength, Math.max, argsLength];
function callAtEveryDepth(n) {
  var f = depthCallees[n & 3];
  try {
    f(1, 2);
  } catch (e) {}
  return callAtEveryDepth(n + 1);
}
try {
  callAtEveryDepth(0);
} catch (e) {}

// Megamorphic getter calls go through the trampoline with argc == 0.
function getX(o) { return o.x; }
function testMegamorphicGetter() {
  var getters = [
    function() { return this.v; },
    function(a, b) { return [a, b, this.v]; },
    Object.prototype.toString,
    function(a) { return [this.v, a]; }.bind({v: "bound"}),
  ];
  var objs = [];
  for (var i = 0; i < 40; i++) {
    var o = {v: i};
    o["p" + i] = i;
    Object.defineProperty(o, "x", {get: getters[i % getters.length]});
    objs.push(o);
  }
  for (var i = 0; i < ITERS * 4; i++) {
    var o = objs[i % objs.length];
    var r = getX(o);
    switch (i % getters.length) {
      case 0:
        assertEq(r, o.v);
        break;
      case 1:
        assertArrayEq(r, [undefined, undefined, o.v]);
        break;
      case 2:
        assertEq(r, "[object Object]");
        break;
      case 3:
        assertArrayEq(r, ["bound", undefined]);
        break;
    }
  }
}
testMegamorphicGetter();
