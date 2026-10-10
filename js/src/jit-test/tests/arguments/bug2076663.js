// |jit-test| --fast-warmup

// Ion bails out at the join point of the conditional before the SetArg. The
// result of the conditional must not be optimized out, because Baseline stores
// it into arg1, which is observable via Function.arguments.

function foo1(arg1, arg2) {
  if (arg1 == 0) {
    arg1 = arg2 ? 2 : 2;
    return foo1.arguments[0];
  }
  return arg2;
}
function foo2() {
  return foo1.apply(undefined, arguments);
}
function foo(arg) {
  return foo2(arg);
}
for (let i = 0; i < 2000; i++) {
  foo({});
}
assertEq(foo(0), 2);
