// |jit-test| --fast-warmup; --no-threads

class Vec {
  #x;
  #y;
  constructor(x, y) {
    this.#x = x;
    this.#y = y;
  }
  #dot(o) { return this.#x * o.#x + this.#y * o.#y; }
  #scale(k) { return new Vec(this.#x * k, this.#y * k); }
  lengthSquared() { return this.#dot(this); }
  doubled() { return this.#scale(2).lengthSquared(); }
}

function test() {
  let v = new Vec(3, 4);
  for (let i = 0; i < 300; i++) {
    assertEq(v.lengthSquared(), 25);
    assertEq(v.doubled(), 100);
  }
}
test();

// The private method is stored after code in a computed key has run, so Warp
// must not use the binding's earlier value.
let getM;
function call(f, o) { return f(o); }
class C {
  [(() => {
    getM = o => o.#m;
    for (let i = 0; i < 2000; i++) {
      try {
        call(getM, {});
      } catch (e) {}
    }
    return "k";
  })()]() {}
  #m() { return 1; }
}
let c = new C();
for (let i = 0; i < 2000; i++) {
  assertEq(typeof call(getM, c), "function");
}
