load(libdir + "asserts.js");

var g = newGlobal({ newCompartment: true });
var dbg = new Debugger();
dbg.addDebuggee(g);

const seen = {};

g.eval(
  "" +
    function unaliased() {
      using x = { [Symbol.dispose]() {} };
      assertEq(typeof x, "object");
    }
);

g.eval(
  "" +
    function aliased() {
      using x = { [Symbol.dispose]() {} };
      assertEq(typeof x, "object");
      return () => x;
    }
);

dbg.onEnterFrame = frame => {
  frame.onStep = () => {
    if (typeof frame.environment.getVariable("x") === "object") {
      assertThrowsInstanceOf(
        () => frame.environment.setVariable("x", 99),
        TypeError
      );
      assertEq(typeof frame.environment.getVariable("x"), "object");
      seen[frame.callee.name] = true;
    }
  };
};

g.unaliased();
g.aliased();

assertEq(Object.keys(seen).length, 2);
