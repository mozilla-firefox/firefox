// Calling or resuming a function with many formals from C++ near the stack
// limit.
if (helperThreadCount() === 0) {
  quit();
}
// Make sure the next worker doesn't reuse a larger cached thread stack.
evalInWorker("");
evalInWorker(`
  var params = [];
  for (var i = 0; i < 65535; i++) params.push("a" + i);
  params = params.join(",");
  var target = eval("(function(" + params + ") { return 0; })");
  var asyncTarget = eval("(async function(" + params + ") { await 0; })");

  function probe(n, f) {
    try { if (n > 0) return probe(n - 1, f); } catch (e) {}
    return f();
  }

  function call() { return JSON.parse("1", target); }
  for (var i = 0; i < 300; i++) call();
  try { probe(100000, call); } catch (e) {}

  for (var i = 0; i < 300; i++) { asyncTarget(); drainJobQueue(); }
  asyncTarget();
  try { probe(100000, drainJobQueue); } catch (e) {}
`);
