function makeIonCompiledScript(n) {
  let src = "\n".repeat(n) + "function f() {}";
  evaluate(src);
  f();
  return f;
}

for (var i = 0; i < 5010; i++) {
  makeIonCompiledScript(i);
}
