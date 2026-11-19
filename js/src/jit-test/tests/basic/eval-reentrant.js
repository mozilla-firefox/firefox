function recursiveEval(n) { return eval("n ? recursiveEval(n - 1) + 1 : 0"); }
assertEq(recursiveEval(5), 5);
