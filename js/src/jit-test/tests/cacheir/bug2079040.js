function f(o, a, b, h1, h2) {
  var k = a + b;
  h1.w = k;
  var r = o.hasOwnProperty(k);
  h2.x = k;
  return r;
}
with ({}) {}
var N = 9000;
var hs1 = [], hs2 = [];
for (var i = 0; i < N; i++) {
  hs1.push({a: i});
  hs2.push({a: i});
}
var objs = [];
for (var i = 0; i < 5; i++) {
  var o = {};
  o["p" + i] = 1;
  objs.push(o);
}
var small = {abcdefgh1: 1, abcdefgh2: 2};
gc();
for (var i = 0; i < N; i++) {
  var o = small, b = "h1";
  if (i >= 600 && i < 603) {
    o = "primitive";
  } else if (i >= 6000 && i < 6005) {
    o = objs[i - 6000];
  } else if (i >= 6005) {
    b = "h2";
  }
  f(o, "abcdefg", b, hs1[i], hs2[i]);
}
minorgc();
var sum = 0;
for (var i = 6006; i < N; i++) {
  var s = hs2[i].x;
  sum += s.length + s.charCodeAt(0);
}
assertEq(sum, 317364);
