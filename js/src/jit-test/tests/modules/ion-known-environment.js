// |jit-test| module; --fast-warmup; --no-threads

let counter = 0;
var total = 0;
function bump() { counter++; total += counter; }
function read() { return counter + total; }

for (let i = 1; i <= 300; i++) {
  bump();
  assertEq(read(), i + (i * (i + 1)) / 2);
}
