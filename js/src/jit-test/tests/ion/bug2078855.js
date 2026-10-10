// |jit-test| --fast-warmup; --no-threads
var G = {q: 0, z: 1};
function g(a, b, e) {
  var x = a & b;
  var t = !!x;
  G.q = a;
  G.z | 0;
  if (t) {
    x = e;
  }
  return x;
}
function f(a, b, e) {
  if (g(a, b, e)) {
    return 1;
  }
  return 2;
}
function main() {
  with ({}) {}
  var r = 0;
  for (var i = 0; i < 1000; i++) {
    r += f(i & 1, 1, "s");
  }
  return r;
}
assertEq(main(), 1500);
G.z = "str";
assertEq(f(0, 1, "s"), 2);
