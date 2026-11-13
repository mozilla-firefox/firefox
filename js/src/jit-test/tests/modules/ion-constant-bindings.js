// |jit-test| module; --fast-warmup; --no-threads

const NUM = 3;
const OBJ = {x: 7};
const STR = ["a", "b", "c"].join("") + String(Math.random() < 2);
const SYM = Symbol("sym");
const BIG = 12345678901234567890n;
const LONG = "x".repeat(2000) + String(Math.random() < 2);

function getNum() { return NUM; }
function getObjX() { return OBJ.x; }
function getStrLength() { return STR.length; }
function getSym() { return SYM; }
function getBig() { return BIG; }
function getLongLength() { return LONG.length; }

function testValues() {
  let sum = 0;
  for (let i = 0; i < 200; i++) {
    sum += getNum() + getObjX() + getStrLength();
    assertEq(getSym(), SYM);
    assertEq(getBig(), 12345678901234567890n);
    assertEq(getLongLength(), 2004);
  }
  assertEq(sum, 200 * (3 + 7 + 7));
  assertEq(STR, "abctrue");
  assertEq(LONG, "x".repeat(2000) + "true");
  OBJ.x = 1;
  assertEq(getObjX(), 1);
}
testValues();

function testNursery() {
  for (let i = 0; i < 200; i++) {
    assertEq(getNursery().i, 0);
  }
}
function getNursery() { return NURSERY; }

// Read a const before it's initialized.
function getLater() { return LATER; }
function testTDZ() {
  for (let i = 0; i < 200; i++) {
    let threw = false;
    try {
      getLater();
    } catch (e) {
      threw = e instanceof ReferenceError;
    }
    assertEq(threw, true);
  }
}
testTDZ();

const LATER = 5;
for (let i = 0; i < 200; i++) {
  assertEq(getLater(), 5);
}

const NURSERY = {i: 0};
testNursery();

// The module's top-level code reads its own bindings as well.
let total = 0;
for (let i = 0; i < 200; i++) {
  total += NUM + LATER;
}
assertEq(total, 200 * 8);
