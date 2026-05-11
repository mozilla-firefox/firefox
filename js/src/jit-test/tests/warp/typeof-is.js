// Test case |typeof| folding in simple comparison contexts.

// Create functions to test if |typeof x| is equal to a constant string.
// - Covers all possible |typeof| results, plus the invalid string "bad".
// - Covers all four possible equality comparison operators.
function createFunctions() {
  return [
    "undefined",
    "object",
    "function",
    "string",
    "number",
    "boolean",
    "symbol",
    "bigint",
    "bad",
  ].flatMap(type => [
    "==",
    "===",
    "!=",
    "!=="
  ].map(op => ({
    type,
    equal: op[0] === "=",
    fn: Function("thing", `return typeof thing ${op} "${type}"`)
  })));
}

let functions = createFunctions();

function test() {
  const ccwGlobal = newGlobal({newCompartment: true});
  const xs = [
    // "undefined"
    // Plain undefined and object emulating undefined, including various proxy wrapper cases.
    undefined,
    createIsHTMLDDA(),
    wrapWithProto(createIsHTMLDDA(), null),
    ccwGlobal.eval("createIsHTMLDDA()"),

    // "object"
    // Plain object and various proxy wrapper cases.
    {},
    this,
    new Proxy({}, {}),
    wrapWithProto({}, null),
    transplantableObject().object,
    ccwGlobal.Object(),

    // "function"
    // Plain function and various proxy wrapper cases.
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

  for (let i = 0; i < 200; ++i) {
    let x = xs[i % xs.length];
    for (let {type, equal, fn} of functions) {
      assertEq(fn(x), (typeof x === type) === equal);
    }
  }
}
for (let i = 0; i < 2; ++i) test();

// Fresh set of functions to gather new type info.
let functionsObject = createFunctions();

// Additional test when the input is always an object.
function testObject() {
  const ccwGlobal = newGlobal({newCompartment: true});

  // All elements of this array are objects to cover the case when |MTypeOf| has
  // a |MIRType::Object| input.
  const xs = [
    // "undefined"
    // Object emulating undefined, including various proxy wrapper cases.
    createIsHTMLDDA(),
    wrapWithProto(createIsHTMLDDA(), null),
    ccwGlobal.eval("createIsHTMLDDA()"),

    // "object"
    // Plain object and various proxy wrapper cases.
    {},
    this,
    new Proxy({}, {}),
    wrapWithProto({}, null),
    transplantableObject().object,
    ccwGlobal.Object(),

    // "function"
    // Plain function and various proxy wrapper cases.
    function(){},
    new Proxy(function(){}, {}),
    new Proxy(createIsHTMLDDA(), {}),
    wrapWithProto(function(){}, null),
    ccwGlobal.Function(),
  ];

  for (let i = 0; i < 200; ++i) {
    let x = xs[i % xs.length];
    for (let {type, equal, fn} of functionsObject) {
      assertEq(fn(x), (typeof x === type) === equal);
    }
  }
}
for (let i = 0; i < 2; ++i) testObject();
