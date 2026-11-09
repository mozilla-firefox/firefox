// Modulo by a divisor of the form 2^k - c uses Crandall reduction.

const P25519 = 2n ** 255n - 19n;
const P256K1 = 2n ** 256n - 2n ** 32n - 977n;
const M127 = 2n ** 127n - 1n;
const P1305 = 2n ** 130n - 5n;
const NOT = 2n ** 192n - 2n ** 64n - 1n;

function mod25519(a) { return a % P25519; }
function mod256k1(a) { return a % P256K1; }
function mod127(a) { return a % M127; }
function mod1305(a) { return a % P1305; }
function modNeg25519(a) { return a % -P25519; }
function modNot(a) { return a % NOT; }

function mod(a, b) { return a - (a / b) * b; }

let seed = 1;
function rnd32() {
  seed = (Math.imul(seed, 1103515245) + 12345) | 0;
  return seed >>> 0;
}
function rndBig(digits) {
  let x = 0n;
  for (let i = 0; i < digits * 2; i++) {
    x = (x << 32n) | BigInt(rnd32());
  }
  return (rnd32() & 1) ? -x : x;
}

let xs = [0n, 1n, -1n, 1n << 1024n];
for (let m of [P25519, P256K1, M127, P1305, NOT]) {
  xs.push(m - 1n, m, m + 1n, -m, 2n * m - 1n, (m - 1n) ** 2n, m ** 2n,
          m ** 3n + 1n);
}
for (let i = 0; i < 200; i++) {
  xs.push(rndBig(i % 20));
}

for (let k = 65; k <= 600; k += 7) {
  const cs = [1n, 19n, (1n << 32n) + 977n,
              (1n << BigInt(Math.min(64, k >> 1))) - 1n];
  for (let c of cs) {
    const m = (1n << BigInt(k)) - c;
    const ys = [m - 1n, m, m + 1n, m * m - 1n, -(m * m) - 1n];
    for (let i = 0; i < 4; i++) {
      ys.push(rndBig(Math.ceil(k / 64) * (i + 1)));
    }
    for (let y of ys) {
      assertEq(y % m, mod(y, m));
      assertEq(y % -m, mod(y, -m));
    }
  }
}

for (let i = 0; i < 50; i++) {
  for (let x of xs) {
    assertEq(mod25519(x), mod(x, P25519));
    assertEq(mod256k1(x), mod(x, P256K1));
    assertEq(mod127(x), mod(x, M127));
    assertEq(mod1305(x), mod(x, P1305));
    assertEq(modNeg25519(x), mod(x, -P25519));
    assertEq(modNot(x), mod(x, NOT));
  }
}
