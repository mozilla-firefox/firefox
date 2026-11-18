// Calling functions with many formals from CacheIR stubs near the stack limit.
if (helperThreadCount() === 0) {
  quit();
}
// Make sure the next worker doesn't reuse a larger cached thread stack.
evalInWorker("");
evalInWorker(`
  var params = [];
  for (var i = 0; i < 65535; i++) params.push("a" + i);
  params = params.join(",");
  function makeFun() { return eval("(function(" + params + ") { return {}; })"); }

  function probe(n, f) {
    try { if (n > 0) return probe(n - 1, f); } catch (e) {}
    return f();
  }
  function test(f) {
    for (var i = 0; i < 300; i++) f();
    try { probe(100000, f); } catch (e) {}
  }

  var target = makeFun();
  test(function() { return target(); });
  test(function() { return target.call(); });
  test(function() { return target.apply(null, arguments); });
  var bound = target.bind(null);
  test(function() { return bound(); });

  var obj = {};
  Object.defineProperty(obj, "x", { get: makeFun(), set: makeFun() });
  test(function() { return obj.x; });
  test(function() { obj.x = 1; });

  var proxy = new Proxy({}, { get: makeFun() });
  test(function() { return proxy.x; });

  var iterator = { next() { return { done: false }; }, return: makeFun() };
  var iterable = { [Symbol.iterator]() { return iterator; } };
  test(function() { for (var x of iterable) break; });

  var callee;
  function callMegamorphic() { return callee(); }
  for (var i = 0; i < 100; i++) {
    callee = eval("(function() { return " + i + "; })");
    callMegamorphic();
  }
  callee = target;
  test(callMegamorphic);
`);
