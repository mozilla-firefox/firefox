function doEval(src) {
  var result;
  eval(src);
  return result;
}

function test(src) {
  let first = doEval(src);
  let second = doEval(src);
  assertEq(first == second, false);
}

test("let o = {x:1}; result = o;");
test("let arr = [1,2,3]; result = arr;");
test("let re = /[abc]/g; result = re;");
test("function outer() {}; result = outer;");
test("function outer() { function inner() {}; return inner; } result = outer();");

// CallSiteObj
test("function template(raw) { return raw; }; result = template`abc`");
test("function outer() { function template(raw) { return raw; }; return template`abc`}; result = outer()");
