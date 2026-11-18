// |jit-test| --fast-warmup; --ion-offthread-compile=off; --ion-inlining=off

// Ion apply calls to an unknown function with many formals near the stack
// limit.
if (helperThreadCount() === 0) {
  quit();
}
// Make sure the next worker doesn't reuse a larger cached thread stack.
evalInWorker("");
evalInWorker(`
  var params = [];
  for (var i = 0; i < 65535; i++) params.push("a" + i);
  var target = eval("(function(" + params.join(",") + ") { return {}; })");
  var funs = [];
  for (var i = 0; i < 10; i++) funs.push(eval("(function() { return {}; })"));

  var empty = [];
  function apply(f) { return f.apply(null, empty); }
  function spread(f) { return f(...empty); }
  function construct(f) { return new f(...empty); }

  function probe(n, g) {
    try { if (n > 0) return probe(n - 1, g); } catch (e) {}
    return g(target);
  }
  for (var g of [apply, spread, construct]) {
    for (var i = 0; i < 200; i++) g(funs[i % funs.length]);
    try { probe(100000, g); } catch (e) {}
  }
`);
