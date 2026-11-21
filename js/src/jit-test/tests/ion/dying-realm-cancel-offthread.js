// |jit-test| --fast-warmup

function test(callAfterHint) {
  var g = newGlobal({sameZoneAs: this});
  g.eval(`
    function f(x) {
      var s = 0;
      for (var i = 0; i < x; i++) {
        s += i * 2;
      }
      return s;
    }
    for (var k = 0; k < 100; k++) {
      f(10);
    }
  `);
  g.setRealmIsDyingHint();
  if (callAfterHint) {
    for (var k = 0; k < 100; k++) {
      assertEq(g.f(10), 90);
    }
  }
}
for (var i = 0; i < 10; i++) {
  test(i % 2 === 0);
}
gc();
