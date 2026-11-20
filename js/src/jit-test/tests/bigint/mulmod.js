// Check `(x * y) % m`, which Ion computes without allocating the product,
// against a product that is also used elsewhere.

function fused(x, y, m) {
  return (x * y) % m;
}

function unfused(x, y, m, out) {
  const product = x * y;
  out[0] = product;
  return product % m;
}

const moduli = [
  2n ** 255n - 19n,
  2n ** 256n - 2n ** 32n - 977n,
  2n ** 521n - 1n,
  0x1a0111ea397fe69a4b1ba7b6434bacd764774b84f38512bf6730d2a0f6b0f6241eabfffeb153ffffb9feffffffffaaabn,
  2n ** 128n + 1n,
  2n ** 64n,
  1000003n,
  1n,
].flatMap(m => [m, -m]);

let seed = 12345n;
function rnd(bits) {
  let x = 0n;
  for (let i = 0n; i < bits; i += 64n) {
    seed = (seed * 6364136223846793005n + 1442695040888963407n) &
           0xffffffffffffffffn;
    x = (x << 64n) | seed;
  }
  return x & ((1n << bits) - 1n);
}

const out = [0n];
for (let i = 0; i < 2000; i++) {
  const m = moduli[i % moduli.length];
  const bits = [1n, 63n, 64n, 200n, 256n, 521n, 1100n][i % 7];
  let x = rnd(bits);
  let y = rnd(bits > 4n ? bits - BigInt(i % 5) : bits);
  if (i & 1) {
    x = -x;
  }
  if (i & 2) {
    y = -y;
  }
  if (i % 11 == 0) {
    y = 0n;
  }
  if (i % 13 == 0) {
    x = m - 1n;
    y = m - 1n;
  }
  assertEq(fused(x, y, m), unfused(x, y, m, out));
}

for (let i = 0; i < 100; i++) {
  let threw = false;
  try {
    fused(i + 1 == 100 ? 0n : 3n, 5n, 0n);
  } catch (e) {
    threw = e instanceof RangeError;
  }
  assertEq(threw, true);
}
