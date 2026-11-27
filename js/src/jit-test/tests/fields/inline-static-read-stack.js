// |jit-test| --fast-warmup

// A static getter read by a field initializer sees the field's initializer
// frame between the getter and the constructor.

class A {
  static get g() {
    return new Error().stack;
  }
  y = A.g;
}

function stackFrames() {
  return new A().y.split("\n").filter(Boolean);
}

for (let i = 0; i < 100; i++) {
  const frames = stackFrames();
  assertEq(frames.length, 5);
  assertEq(frames[0].startsWith("get g@"), true);
  assertEq(frames[1].startsWith("@"), true);
  assertEq(frames[2].startsWith("A@"), true);
  assertEq(frames[3].startsWith("stackFrames@"), true);
}
