// Array.prototype.sort with a comparator with many formals near the stack
// limit.
if (helperThreadCount() === 0) {
  quit();
}
// Make sure the next worker doesn't reuse a larger cached thread stack.
evalInWorker("");
evalInWorker(`
  var params = [];
  for (var i = 0; i < 65535; i++) params.push("a" + i);
  var cmp = eval("(function(" + params.join(",") + ") { return 0; })");
  var arr = [3, 2, 1];

  function sort() { return arr.sort(cmp); }
  function probe(n) {
    try { if (n > 0) return probe(n - 1); } catch (e) {}
    return sort();
  }
  for (var i = 0; i < 300; i++) sort();
  try { probe(100000); } catch (e) {}
`);
