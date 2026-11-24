const alphabets = {
  base64: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
  base64url: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
};

function reference(u8, chars, omitPadding) {
  let result = "";
  for (let i = 0; i < u8.length; i += 3) {
    // Bytes past the end read as zero.
    let u24 = (u8[i] << 16) | (u8[i + 1] << 8) | u8[i + 2];
    let chunk = chars[u24 >> 18] + chars[(u24 >> 12) & 63] +
                chars[(u24 >> 6) & 63] + chars[u24 & 63];
    let padding = Math.max(0, i + 3 - u8.length);
    result += chunk.slice(0, 4 - padding);
    if (!omitPadding) {
      result += "=".repeat(padding);
    }
  }
  return result;
}

function test(Buffer) {
  const maxLength = 192;

  for (let length = 0; length <= maxLength; ++length) {
    // Rotate the alphabet by the length, so that every character gets encoded
    // at every position of a 64 character block. Vary the alignment, too.
    let rotation = length & 63;
    let rotated = alphabets.base64.slice(rotation) +
                  alphabets.base64.slice(0, rotation);

    let offset = length & 3;
    let bytes = new Uint8Array(new Buffer(offset + maxLength), offset);
    bytes.setFromBase64(rotated.repeat(maxLength / 48));

    let u8 = bytes.subarray(0, length);
    for (let [alphabet, chars] of Object.entries(alphabets)) {
      for (let omitPadding of [false, true]) {
        assertEq(u8.toBase64({alphabet, omitPadding}),
                 reference(u8, chars, omitPadding));
      }
    }
  }
}
test(ArrayBuffer);
if (typeof SharedArrayBuffer === "function")
  test(SharedArrayBuffer);

if (typeof reportCompare === "function")
  reportCompare(true, true);
