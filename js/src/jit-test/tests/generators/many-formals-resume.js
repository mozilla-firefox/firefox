// Resuming a generator with many formals from JIT code near the stack limit.
if (helperThreadCount() === 0) {
  quit();
}
// Make sure the next worker doesn't reuse a larger cached thread stack.
evalInWorker("");
evalInWorker(`
  var params = [];
  for (var i = 0; i < 65535; i++) params.push("a" + i);
  var gen = eval("(function*(" + params.join(",") + ") { while (true) yield 1; })");
  var it = gen();

  function resume() { return it.next(); }
  function probe(n) {
    try { if (n > 0) return probe(n - 1); } catch (e) {}
    return resume();
  }
  for (var i = 0; i < 300; i++) resume();
  try { probe(100000); } catch (e) {}
`);
