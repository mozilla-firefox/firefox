// |jit-test| --fast-warmup; --no-threads

class Point {
  #x;
  #y;
  static #count = 0;
  constructor(x, y) {
    this.#x = x;
    this.#y = y;
    Point.#count++;
  }
  get x() { return this.#x; }
  get y() { return this.#y; }
  #secret() { return this.#x * this.#y; }
  area() { return this.#secret(); }
  static count() { return Point.#count; }
  static hasX(o) { return #x in o; }
}

function test() {
  let sum = 0;
  for (let i = 0; i < 300; i++) {
    let p = new Point(i, 2);
    sum += p.x + p.y + p.area();
    assertEq(Point.hasX(p), true);
    assertEq(Point.hasX({}), false);
  }
  assertEq(sum, (299 * 300) / 2 + 600 + 299 * 300);
  assertEq(Point.count(), 300);
}
test();

// Each class evaluation creates new private names.
function makeClass() {
  return class {
    #v = 1;
    static read(o) { return o.#v; }
  };
}
function testDistinctClasses() {
  const A = makeClass();
  const B = makeClass();
  const a = new A();
  const b = new B();
  function readA(o) { return A.read(o); }
  for (let i = 0; i < 300; i++) {
    assertEq(readA(a), 1);
    let threw = false;
    try {
      readA(b);
    } catch (e) {
      threw = e instanceof TypeError;
    }
    assertEq(threw, true);
  }
}
testDistinctClasses();
