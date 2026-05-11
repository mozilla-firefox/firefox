function checkObjectFuse(obj, expected) {
  var state = getObjectFuseState(obj);
  assertEq(JSON.stringify(state), JSON.stringify(expected));
}
function markConstant(obj, key) {
  assertEq(getObjectFuseState(obj).properties[key], "Untracked");
  // This relies on the fact that storing to an Untracked property marks it
  // Constant. Use getOwnPropertyDescriptor to prevent interacting with JIT
  // optimizations.
  obj[key] = Object.getOwnPropertyDescriptor(obj, key).value;
}
function testBasic() {
  var obj = {};
  addObjectFuse(obj);
  checkObjectFuse(obj, {generation:0,properties:{}});

  // Mutating a property changes the property state but not the generation.
  obj.x = 1;
  checkObjectFuse(obj, {generation:0,properties:{x:"Untracked"}});
  markConstant(obj, "x");
  checkObjectFuse(obj, {generation:0,properties:{x:"Constant"}});
  obj.x = 2;
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant"}});
  obj.x = 3;
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant"}});

  // Setting a property to the same value also changes the state.
  obj.a = undefined;
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant",a:"Untracked"}});
  markConstant(obj, "a");
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant",a:"Constant"}});
  assertEq(obj.a, undefined);
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant",a:"Constant"}});
  obj.a = undefined;
  checkObjectFuse(obj, {generation:0,properties:{x:"NotConstant",a:"NotConstant"}});
}
for (var i = 0; i < 20; i++) {
  testBasic();
}

function testRemove() {
  var obj = {};
  addObjectFuse(obj);
  checkObjectFuse(obj, {generation:0,properties:{}});

  // Removing an untracked property doesn't change the generation.
  obj.x = 1;
  checkObjectFuse(obj, {generation:0,properties:{x:"Untracked"}});
  delete obj.x;
  checkObjectFuse(obj, {generation:0,properties:{}});

  // Removing a constant property changes the generation.
  obj.x = 1;
  checkObjectFuse(obj, {generation:0,properties:{x:"Untracked"}});
  markConstant(obj, "x");
  checkObjectFuse(obj, {generation:0,properties:{x:"Constant"}});
  delete obj.x;
  checkObjectFuse(obj, {generation:1,properties:{}});

  // Removing a no-longer-constant property must also change the generation
  // because IC stubs may still have guards for this property.
  obj.z = undefined;
  markConstant(obj, "z");
  checkObjectFuse(obj, {generation:1,properties:{z:"Constant"}});
  obj.z = 2;
  checkObjectFuse(obj, {generation:1,properties:{z:"NotConstant"}});
  delete obj.z;
  checkObjectFuse(obj, {generation:2,properties:{}});

  // Test removing multiple properties.
  obj.a = 1;
  markConstant(obj, "a");
  obj.b = 1;
  markConstant(obj, "b");
  obj.c = 1;
  markConstant(obj, "c");
  obj.d = 1;
  markConstant(obj, "d");
  obj.e = 1;
  markConstant(obj, "e");
  delete obj.b;
  delete obj.d;
  delete obj.e;
  checkObjectFuse(obj, {generation:5,properties:{a:"Constant",c:"Constant"}});
}
for (var i = 0; i < 20; i++) {
  testRemove();
}

function testMany() {
  // Test adding and removing many properties.
  var o = {};
  addObjectFuse(o);
  for (var i = 0; i < 1000; i++) {
    o["prop" + i] = 1; // Untracked
    o["prop" + i] = 1; // Constant
  }
  // Mark every third property NotConstant.
  for (var i = 0; i < 1000; i += 3) {
    o["prop" + i] = 3;
  }
  var state = getObjectFuseState(o);
  assertEq(state.generation, 0);
  for (var i = 0; i < 1000; i++) {
    assertEq(state.properties["prop" + i], (i % 3) ? "Constant" : "NotConstant");
  }
  for (var i = 0; i < 1000; i += 2) {
    delete o["prop" + i];
  }
  state = getObjectFuseState(o);
  assertEq(state.generation, 500);
  assertEq(Object.keys(state.properties).length, 500);
}
testMany();
testMany();

function testTeleporting() {
  // Proto chain: a => b => c => d => null
  var d = Object.create(null);
  addObjectFuse(d);
  var c = Object.create(d);
  addObjectFuse(c);
  var b = Object.create(c);
  addObjectFuse(b);
  var a = Object.create(b);
  addObjectFuse(a);

  d.x = 1;
  checkObjectFuse(d, {generation:0,properties:{x:"Untracked"}});

  // Shadowing an untracked property doesn't change the generation.
  a.x = 1;
  checkObjectFuse(a, {generation:0,properties:{x:"Untracked"}});
  checkObjectFuse(d, {generation:0,properties:{x:"Untracked"}});
  b.x = 1;
  checkObjectFuse(b, {generation:0,properties:{x:"Untracked"}});
  checkObjectFuse(d, {generation:0,properties:{x:"Untracked"}});

  // Shadowing a constant property changes the generation.
  d.y = 1;
  markConstant(d, "y");
  b.y = 1;
  checkObjectFuse(b, {generation:0,properties:{x:"Untracked",y:"Untracked"}});
  checkObjectFuse(d, {generation:1,properties:{x:"Untracked",y:"Constant"}});

  // Shadowing a no-longer-constant property must also change the generation
  // because IC stubs may still have guards for this property.
  d.y = 3;
  checkObjectFuse(d, {generation:1,properties:{x:"Untracked",y:"NotConstant"}});
  c.y = 1;
  checkObjectFuse(d, {generation:2,properties:{x:"Untracked",y:"NotConstant"}});

  // Mutating the prototype of a non-prototype object doesn't change anything.
  Object.setPrototypeOf(a, {});
  checkObjectFuse(a, {generation:0,properties:{x:"Untracked"}});
  checkObjectFuse(b, {generation:0,properties:{x:"Untracked",y:"Untracked"}});
  checkObjectFuse(c, {generation:0,properties:{y:"Untracked"}});
  checkObjectFuse(d, {generation:2,properties:{x:"Untracked",y:"NotConstant"}});

  // Mutating the prototype of a prototype object bumps the generation counter.
  Object.setPrototypeOf(b, {});
  checkObjectFuse(a, {generation:0,properties:{x:"Untracked"}});
  checkObjectFuse(b, {generation:1,properties:{x:"Untracked",y:"Untracked"}});
  checkObjectFuse(c, {generation:1,properties:{y:"Untracked"}});
  checkObjectFuse(d, {generation:3,properties:{x:"Untracked",y:"NotConstant"}});

  Object.setPrototypeOf(c, {});
  checkObjectFuse(a, {generation:0,properties:{x:"Untracked"}});
  checkObjectFuse(b, {generation:1,properties:{x:"Untracked",y:"Untracked"}});
  checkObjectFuse(c, {generation:2,properties:{y:"Untracked"}});
  checkObjectFuse(d, {generation:4,properties:{x:"Untracked",y:"NotConstant"}});
}
for (var i = 0; i < 15; i++) {
  testTeleporting();
}

function testAccessor() {
  var o = {};
  addObjectFuse(o);

  // Add a data property.
  o.x = 1;
  markConstant(o, "x");
  checkObjectFuse(o, {generation:0,properties:{x:"Constant"}});

  // Change it to an accessor and check this marks the property non-constant.
  Object.defineProperty(o, "x", {get: function() { return 1; }});
  checkObjectFuse(o, {generation:0,properties:{x:"NotConstant"}});
  Object.defineProperty(o, "x", {get: function() { return 1; }});
  checkObjectFuse(o, {generation:0,properties:{x:"NotConstant"}});

  // The same thing for accessor property => data property.
  o = {};
  addObjectFuse(o);
  Object.defineProperty(o, "x", {get: function() { return 1; }, configurable: true});
  checkObjectFuse(o, {generation:0,properties:{x:"Untracked"}});
  Object.defineProperty(o, "x", {get: function() { return 1; }, configurable: true});
  checkObjectFuse(o, {generation:0,properties:{x:"Constant"}});
  Object.defineProperty(o, "x", {value: 3});
  checkObjectFuse(o, {generation:0,properties:{x:"NotConstant"}});
}
for (var i = 0; i < 15; i++) {
  testAccessor();
}

