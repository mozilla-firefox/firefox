// |jit-test| --fast-warmup
// Tenuring nursery objects in a pending off-thread Ion compilation's snapshot
// must not happen while holding the helper thread lock.
function makeClosure() {
  // 16 slots: no other object in this test has this size class, so tenuring
  // |captured| right after a major GC needs a new arena.
  const captured = {p0: 0, p1: 1, p2: 2, p3: 3, p4: 4, p5: 5, p6: 6, p7: 7,
                    p8: 8, p9: 9, p10: 10, p11: 11, p12: 12, p13: 13, p14: 14, p15: 15};
  return x => captured.p0 + x;
}

var callsToG = "sum += g(1);\n".repeat(20);
var g;

for (var calls = 20; calls <= 200; calls += 20) {
  var hot = new Function("g", "var sum = 0; for (var i = 0; i < 2; i++) { " + callsToG + " } return sum;");

  g = null;
  gc();

  g = makeClosure();
  for (var i = 0; i < calls; i++) {
    assertEq(hot(g), 40);
  }
  minorgc();
}
