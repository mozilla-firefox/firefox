// Indirect evals and non-function direct evals are cached, keyed on the source
// location that will be recorded as their introducer.

gczeal(0);

var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);
var evalScripts = [];
dbg.onNewScript = script => {
  if (script.source.introductionType === "eval") {
    evalScripts.push(script);
  }
};

function countEvalCompiles(src) {
  evalScripts = [];
  g.evaluate(src);
  return evalScripts.length;
}

assertEq(countEvalCompiles(`
  var ie = eval;
  var count = 0;
  function h() { return ie("function q() { return ++count; } q()"); }
  if (h() !== 1 || h() !== 2) throw new Error("indirect");
`), 1);

// Top-level direct eval.
assertEq(countEvalCompiles(`
  for (var i = 0; i < 3; i++) {
    if (eval("i") !== i) throw new Error("top-level direct");
  }
`), 1);

// Direct eval inside an eval.
assertEq(countEvalCompiles(`
  var r = [];
  for (var i = 0; i < 3; i++) {
    r.push(eval("eval('i * 2')"));
  }
  if (r.join() !== "0,2,4") throw new Error("nested");
`), 2);

// Different call sites get different scripts, even on the same line.
assertEq(countEvalCompiles(`
  var ie = eval;
  var s = "new Error().stack";
  var a = ie(s);
  var b = ie(s);
  var c = ie(s), d = ie(s);
  if (!/line 4 > eval:1/.test(a) || !/line 5 > eval:1/.test(b)) {
    throw new Error("stack: " + a + b);
  }
`), 4);
var offsets = evalScripts.map(s => s.source.introductionOffset);
assertEq(new Set(offsets).size, 4);

// The same call site evaluating in different globals.
var g1 = newGlobal();
var g2 = newGlobal();
g1.x = 1;
g2.x = 2;
var results = [];
for (var i = 0; i < 2; i++) {
  for (var gg of [g1, g2]) {
    results.push(gg.eval("x"));
  }
}
assertEq(results.join(), "1,2,1,2");

// A JSOp::Eval site can perform either a direct or an indirect eval, depending
// on the callee. The two must not share a cache entry.
g.evaluate(`
  var where = "global";
  var boundEval = eval.bind(null);
  function f(e) {
    var eval = e;
    var where = "local";
    return eval("where");
  }
`);
assertEq(g.f(g.boundEval), "global");
assertEq(g.f(g.eval), "local");
assertEq(g.f(g.boundEval), "global");
assertEq(g.f(g.eval), "local");

// Indirect eval is never strict because of its caller, while direct eval in
// strict code is.
g.evaluate(`
  "use strict";
  var ie = eval;
  function s() { return ie("var leaked = 1; typeof leaked"); }
  function d() { return eval("var notLeaked = 1; typeof notLeaked"); }
`);
for (var i = 0; i < 2; i++) {
  assertEq(g.s(), "number");
  assertEq(g.leaked, 1);
  assertEq(g.d(), "number");
  assertEq("notLeaked" in g, false);
}

// If every frame is hidden from the target realm by principals, there is no
// introducer and evals from different call sites can share a script.
var content = newGlobal({principal: 0x1, newCompartment: true});
var contentDbg = new Debugger(content);
var contentEvalScripts = 0;
contentDbg.onNewScript = script => {
  if (script.source.introductionType === "eval") {
    contentEvalScripts++;
  }
};
var s1 = content.eval("new Error().stack");
var s2 = content.eval("new Error().stack");
assertEq(s1, s2);
assertEq(contentEvalScripts, 1);

// A wasm caller can't be described by a script and pc, so it isn't cached.
if (wasmIsSupported()) {
  assertEq(countEvalCompiles(`
    var bytes = wasmTextToBinary(\`(module
      (import "" "eval" (func $eval (param externref) (result externref)))
      (func (export "f") (param externref) (result externref)
        local.get 0
        call $eval))\`);
    var ins = new WebAssembly.Instance(new WebAssembly.Module(bytes),
                                       {"": {eval}});
    if (ins.exports.f("1 + 1") !== 2 || ins.exports.f("1 + 1") !== 2) {
      throw new Error("wasm");
    }
  `), 2);
}
