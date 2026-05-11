// Test case |typeof| folding in switch-statement contexts.

function TypeOf(thing) {
  switch (typeof thing) {
  case "undefined":
    return "undefined";
  case "object":
    return "object";
  case "function":
    return "function";
  case "string":
    return "string";
  case "number":
    return "number";
  case "boolean":
    return "boolean";
  case "symbol":
    return "symbol";
  case "bigint":
    return "bigint";
  case "bad":
    return "bad";
  }
  return "bad2";
}

function test() {
  const ccwGlobal = newGlobal({newCompartment: true});
  const xs = [
    // "undefined"
    // Plain undefined and objects emulating undefined, including various
    // proxy wrapper cases.
    undefined,
    createIsHTMLDDA(),
    wrapWithProto(createIsHTMLDDA(), null),
    ccwGlobal.eval("createIsHTMLDDA()"),

    // "object"
    // Plain objects and various proxy wrapper cases.
    {},
    this,
    new Proxy({}, {}),
    wrapWithProto({}, null),
    transplantableObject().object,
    ccwGlobal.Object(),

    // "function"
    // Plain functions and various proxy wrapper cases.
    function(){},
    new Proxy(function(){}, {}),
    new Proxy(createIsHTMLDDA(), {}),
    wrapWithProto(function(){}, null),
    ccwGlobal.Function(),

    // "string"
    "",

    // "number"
    // Int32 and Double numbers.
    0,
    1.23,

    // "boolean"
    true,

    // "symbol"
    Symbol(),

    // "bigint"
    0n,
  ];

  for (let i = 0; i < 500; ++i) {
    let x = xs[i % xs.length];
    assertEq(TypeOf(x), typeof x);
  }
}
for (let i = 0; i < 2; ++i) test();
