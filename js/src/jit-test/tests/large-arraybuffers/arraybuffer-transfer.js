// |jit-test| heavy
const gb = 1 * 1024 * 1024 * 1024;

const smallByteLength = 1024;
const largeByteLength = 4 * gb;

// Fast alternative to |assertEq| to avoid a slow VM-call.
// Should only be used when looping over the TypedArray contents to avoid
// unnecessary test slowdowns.
function assertEqNum(e, a) {
  if (e !== a) {
    assertEq(e, a);
  }
}

{
  let smallBuffer = new ArrayBuffer(smallByteLength);

  let i8 = new Uint8Array(smallBuffer);
  for (let i = 0; i < smallByteLength; ++i) {
    assertEqNum(i8[i], 0);
    i8[i] = i;
  }

  assertEq(smallBuffer.byteLength, smallByteLength);
  assertEq(smallBuffer.detached, false);

  // Copy from a small into a large buffer.
  let largeBuffer = smallBuffer.transfer(largeByteLength);

  assertEq(smallBuffer.byteLength, 0);
  assertEq(smallBuffer.detached, true);

  assertEq(largeBuffer.byteLength, largeByteLength);
  assertEq(largeBuffer.detached, false);

  i8 = new Uint8Array(largeBuffer);
  for (let i = 0; i < smallByteLength; ++i) {
    assertEqNum(i8[i], i & 0xff);
  }

  // Test the first 100 new bytes.
  for (let i = smallByteLength; i < smallByteLength + 100; ++i) {
    assertEqNum(i8[i], 0);
  }

  // And the last 100 new bytes.
  for (let i = largeByteLength - 100; i < largeByteLength; ++i) {
    assertEqNum(i8[i], 0);
  }

  // Copy back from a large into a small buffer.
  smallBuffer = largeBuffer.transfer(smallByteLength);

  assertEq(largeBuffer.byteLength, 0);
  assertEq(largeBuffer.detached, true);

  assertEq(smallBuffer.byteLength, smallByteLength);
  assertEq(smallBuffer.detached, false);

  i8 = new Uint8Array(smallBuffer);
  for (let i = 0; i < smallByteLength; ++i) {
    assertEqNum(i8[i], i & 0xff);
  }
}
