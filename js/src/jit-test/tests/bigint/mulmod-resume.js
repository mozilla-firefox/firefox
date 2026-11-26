// |jit-test| --ion-eager; --no-threads

function f(x, y, count) {
  let r;
  while (count-- > 0) {
    let m = ({m: 18446744073709551629n}).m;
    r = (x * y) % m;
  }
  return r;
}

// Cause f to be Ion-compiled.  Warp initially sees the divisor as
// variable and thus possibly zero, so the modulo gets a resume point.
// Scalar replacement then makes the divisor constant, Ion fuses the
// modular multiplication, marks the mulmod as effect-free because of
// the nonzero divisor, and then LICM hoists the whole thing out of the
// loop.  This regression test triggered a compile-time assertion if an
// effect-free mulmod erroneously stole its modulo's resume point.
for (let i = 0; i < 100; i++) {
  let x = (1n << 100n) + BigInt(i);
  let y = (1n << 160n) + BigInt(i);
  assertEq(f(x, y, 5), (x * y) % 18446744073709551629n);
}
