// |jit-test| skip-if: !(getBuildConfiguration('debug')||getBuildConfiguration('fuzzing-defined'))

function intact(name) {
  let state = getFuseState();
  if (!(name in state)) {
    throw "No such fuse " + name;
  }
  return state[name].intact
}

function testRealmChange() {
  let g = newGlobal();
  g.evaluate(intact.toString())

  // Get a mutating function which will affect the symbol.iterator fuse.
  let rdel = g.evaluate("function del(o) { delete o.prototype[Symbol.iterator] };del")
  // Fuse is still intact.
  g.evaluate(`assertEq(intact("ArrayPrototypeIteratorFuse"), true)`);

  // setup a new global,
  let g2 = newGlobal();
  g2.evaluate(intact.toString())

  // register the popping function.
  g2.rdel = rdel;

  // Pop the array fuse in the new global.
  g2.evaluate(`rdel(Array)`);

  // The realm of the original array should have a fuse still intact
  g.evaluate(`assertEq(intact("ArrayPrototypeIteratorFuse"), true)`);

  // The realm of the array proto should no longer be intact. Oh dear. This is
  // interesting. We currently ask the cx for the array iterator proto,
  g2.evaluate(`assertEq(intact("ArrayPrototypeIteratorFuse"), false)`);
}

testRealmChange();

function testInNewGlobal(pre, post) {
  g = newGlobal();
  g.evaluate(intact.toString());
  g.evaluate(pre)
  g.evaluate("assertRealmFuseInvariants()");
  g.evaluate(post);
}

testInNewGlobal("delete Array.prototype[Symbol.iterator]", `assertEq(intact("ArrayPrototypeIteratorFuse"), false)`)
testInNewGlobal("([])[Symbol.iterator]().__proto__['return'] = () => 10;", `assertEq(intact("ArrayIteratorPrototypeHasNoReturnProperty"), false)`)
testInNewGlobal("([])[Symbol.iterator]().__proto__.__proto__['return'] = () => 10;", `assertEq(intact("IteratorPrototypeHasNoReturnProperty"), false)`)
testInNewGlobal("Object.prototype['return'] = () => 10;", `assertEq(intact("ObjectPrototypeHasNoReturnProperty"), false)`)
testInNewGlobal(`assertEq(intact("ArrayIteratorPrototypeHasIteratorProto"), true); Object.setPrototypeOf(( ([])[Symbol.iterator]().__proto__ ), {a:10})`, `assertEq(intact("ArrayIteratorPrototypeHasIteratorProto"), false);`);
testInNewGlobal(`assertEq(intact("IteratorPrototypeHasObjectProto"), true); Object.setPrototypeOf( ( ([])[Symbol.iterator]().__proto__.__proto__ ), {a:10})`, `assertEq(intact("IteratorPrototypeHasObjectProto"), false);`);

testInNewGlobal(`assertEq(intact("hasSeenObjectEmulateUndefinedFuse"), true); createIsHTMLDDA()`, `assertEq(intact("hasSeenObjectEmulateUndefinedFuse"), false);`);
testInNewGlobal(`assertEq(intact("hasSeenArrayExceedsInt32LengthFuse"), true); const x = []; x[2147483649] = 1`, `assertEq(intact("hasSeenArrayExceedsInt32LengthFuse"), false);`);

// Runtime wide fuse.
assertEq(intact("hasSeenObjectEmulateUndefinedFuse"), false);
assertEq(intact("hasSeenArrayExceedsInt32LengthFuse"), false);
