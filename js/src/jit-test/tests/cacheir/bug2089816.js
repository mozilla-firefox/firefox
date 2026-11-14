// |jit-test| --fast-warmup; --no-threads; --ion-limit-script-size=off
function f() {}
let bound = f.bind({});
for (let i = 0; i < 10; i++) {
  bound();
}
let g1 = Function("return Object.freeze({}," + "0,".repeat(21000) + "0)");
let g2 = Function("return bound(" + "0,".repeat(21000) + "0)");
for (let i = 0; i < 500; i++) {
  g1();
  g2();
}
