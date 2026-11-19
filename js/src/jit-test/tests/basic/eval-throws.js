function foo(x) {
  try {
    return eval("if (x) throw x; 0");
  } catch (e) {
    return e;
  }
}
assertEq(foo(1), 1);
assertEq(foo(2), 2);
assertEq(foo(3), 3);
