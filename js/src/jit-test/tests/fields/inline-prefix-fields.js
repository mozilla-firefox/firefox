// |jit-test| --fast-warmup

// Fields before the first non-inlinable field are initialized in the
// constructor. The remaining fields run through .initializers. Check that
// declaration order, values and side effects are the same either way.

const log = [];
function f(name, value) {
  log.push(name);
  return value;
}

function checkMixed() {
  class Mixed {
    a = 0;
    b = f("b", 1);
    c = 2;
    d = f("d", this.c + 10);
    e = 4;
  }
  log.length = 0;
  const o = new Mixed();
  assertEq(JSON.stringify(Object.keys(o)), '["a","b","c","d","e"]');
  assertEq(JSON.stringify(o), '{"a":0,"b":1,"c":2,"d":12,"e":4}');
  assertEq(log.join(), "b,d");
}

function checkKinds() {
  class Kinds {
    u;
    t = true;
    fl = false;
    n = null;
    neg = -1;
    c = f("c", 7);
    after;
  }
  log.length = 0;
  const o = new Kinds();
  assertEq(JSON.stringify(Object.keys(o)), '["u","t","fl","n","neg","c","after"]');
  assertEq(Object.hasOwn(o, "u"), true);
  assertEq(o.u, undefined);
  assertEq(o.t, true);
  assertEq(o.fl, false);
  assertEq(o.n, null);
  assertEq(o.neg, -1);
  assertEq(o.c, 7);
  assertEq(Object.hasOwn(o, "after"), true);
  assertEq(log.join(), "c");
}

function checkLaterField() {
  class Later {
    x = 1;
    y = this.z;
    z = 2;
  }
  const o = new Later();
  assertEq(o.y, undefined);
  assertEq(o.z, 2);
  assertEq(JSON.stringify(Object.keys(o)), '["x","y","z"]');
}

function checkFirstFieldNotInlined() {
  class FirstCall {
    a = f("a", 0);
    b = 1;
    c = 2;
  }
  log.length = 0;
  const o = new FirstCall();
  assertEq(JSON.stringify(o), '{"a":0,"b":1,"c":2}');
  assertEq(log.join(), "a");
}

function checkThrowingInitializer() {
  let seen;
  function thrower(o) {
    seen = o;
    throw "boom";
  }
  class Throws {
    a = 0;
    b = thrower(this);
    c = 2;
  }
  let caught;
  try {
    new Throws();
  } catch (e) {
    caught = e;
  }
  assertEq(caught, "boom");
  assertEq(JSON.stringify(Object.keys(seen)), '["a"]');
  assertEq("c" in seen, false);
}

function checkPrivateFields() {
  class Private {
    #p = 1;
    q = 2;
    r = f("r", 3);
    #s = 4;
    sum() {
      return this.#p + this.q + this.r + this.#s;
    }
  }
  log.length = 0;
  assertEq(new Private().sum(), 10);
  assertEq(log.join(), "r");
}

function checkManyFields() {
  const names = Array.from({ length: 12 }, (_, i) => `n${i}`);
  const Numbers = Function(
    `return class { ${names.map((n, i) => `${n} = ${i};`).join(" ")} };`
  )();
  const o = new Numbers();
  assertEq(JSON.stringify(Object.keys(o)), JSON.stringify(names));
  names.forEach((n, i) => assertEq(o[n], i));

  const WithCall = Function(
    "f",
    `return class { ${names.map((n, i) => `${n} = ${i};`).join(" ")} last = f(); };`
  )(() => "called");
  const w = new WithCall();
  assertEq(JSON.stringify(Object.keys(w)), JSON.stringify([...names, "last"]));
  assertEq(w.last, "called");
}

function checkConstructorAndSubclass() {
  class Base {
    a = 1;
    b = f("b", 2);
    constructor(x) {
      this.x = x;
    }
  }
  class Derived extends Base {
    c = 3;
    d = f("d", 4);
  }
  log.length = 0;
  const o = new Derived(9);
  assertEq(JSON.stringify(Object.keys(o)), '["a","b","x","c","d"]');
  assertEq(o.x, 9);
  assertEq(log.join(), "b,d");
}

function checkStaticMembersIgnored() {
  class WithStatic {
    static s = f("s", 0);
    a = 0;
    b = f("b", 1);
    method() {
      return 1;
    }
  }
  log.length = 0;
  assertEq(JSON.stringify(new WithStatic()), '{"a":0,"b":1}');
  assertEq(log.join(), "b");
}

for (let i = 0; i < 50; i++) {
  checkMixed();
  checkKinds();
  checkLaterField();
  checkFirstFieldNotInlined();
  checkThrowingInitializer();
  checkPrivateFields();
  checkManyFields();
  checkConstructorAndSubclass();
  checkStaticMembersIgnored();
}
