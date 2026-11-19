gczeal(0);

var g = newGlobal({newCompartment: true});
var dbg = new Debugger(g);
var evalScripts = 0;
dbg.onNewScript = script => {
  if (script.source.introductionType === "eval") {
    evalScripts++;
  }
};

// Make a string of the form:
// a + ("xxxxx...b...xxxxx").indexOf("c")
// where `a` and `c` are sampled by the eval hasher, but `b` is not.
function makeSource(a, b, c) {
  let xPrefix = 'x'.repeat(10000);
  let xSuffix = 'x'.repeat(256);
  let src = `'${a} + ("${xPrefix}${b}${xSuffix}").indexOf("${c}")'`;
  return src;
}

function test(src1, src2) {
  evalScripts = 0;
  let result = g.evaluate(`
    function foo(s) { return eval(s); }
    foo(${src1}) + foo(${src2});
  `);
  assertEq(evalScripts, (src1 == src2) ? 1 : 2);
  return result;
}

let C = '\u1000'; // Force UTF16.

// Same string
assertEq(test(makeSource(0,"b","c"), makeSource(0,"b","c")), -2)
assertEq(test(makeSource(0,"b",C), makeSource(0,"b",C)), -2)

// Difference at start
assertEq(test(makeSource(0,"b","c"), makeSource(1,"b","c")), -1)
assertEq(test(makeSource(0,"b",C), makeSource(1,"b",C)), -1)

// Difference in middle
assertEq(test(makeSource(0,"b","c"), makeSource(0,"c","c")), 9999)
assertEq(test(makeSource(0,"b",C), makeSource(0,C,C)), 9999)

// Difference at end
assertEq(test(makeSource(0,"b","b"), makeSource(0,"b","c")), 9999)
assertEq(test(makeSource(0,"b","b"), makeSource(0,"b",C)), 9999)
