// |jit-test| --ion-eager; skip-if: !getJitCompilerOptions()["ion.enable"]

function f(x) {
  return x + 1;
}
for (let i = 0; i < 20; i++) {
  f(i);
  f({});
}
gc();
