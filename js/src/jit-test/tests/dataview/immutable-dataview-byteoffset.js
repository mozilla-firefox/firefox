// |jit-test| --enable-arraybuffer-immutable; skip-if: !ArrayBuffer.prototype.transferToImmutable

function testImmutableArrayBufferDefaultLength() {
  for (let i = 0; i < 4; ++i) {
    let ab = new ArrayBuffer(i).transferToImmutable();
    let ta = new DataView(ab);
    for (let j = 0; j < 100; ++j) {
      assertEq(ta.byteOffset, 0);
    }
  }
}
for (let i = 0; i < 2; ++i) testImmutableArrayBufferDefaultLength();

function testImmutableArrayBufferDefaultLengthNonZeroOffset() {
  for (let i = 1; i < 4 + 1; ++i) {
    let ab = new ArrayBuffer(i).transferToImmutable();
    let ta = new DataView(ab, 1);
    for (let j = 0; j < 100; ++j) {
      assertEq(ta.byteOffset, 1);
    }
  }
}
for (let i = 0; i < 2; ++i) testImmutableArrayBufferDefaultLengthNonZeroOffset();

function testImmutableArrayBufferNonZeroOffset() {
  for (let i = 2; i < 4 + 2; ++i) {
    let ab = new ArrayBuffer(i).transferToImmutable();
    let ta = new DataView(ab, 1, 1);
    for (let j = 0; j < 100; ++j) {
      assertEq(ta.byteOffset, 1);
    }
  }
}
for (let i = 0; i < 2; ++i) testImmutableArrayBufferNonZeroOffset();
