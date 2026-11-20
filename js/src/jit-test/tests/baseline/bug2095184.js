// |jit-test| --baseline-eager; skip-if: !getJitCompilerOptions()["baseline.enable"]

const g = newGlobal({ newCompartment: true });
g.eval(`
  function f(x) {
    return x + 1;
  }
  for (let i = 0; i < 20; i++) {
    f(i);
  }
`);
const dbg = new Debugger(g);
dbg.onEnterFrame = () => {};
g.f(1);
gc();
