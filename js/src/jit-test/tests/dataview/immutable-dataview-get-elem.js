// |jit-test| --enable-arraybuffer-immutable; skip-if: !ArrayBuffer.prototype.transferToImmutable

load(libdir + "dataview.js");

const TypedArrays = [
  Int8Array,
  Uint8Array,
  Int16Array,
  Uint16Array,
  Int32Array,
  Uint32Array,
  Float16Array,
  Float32Array,
  Float64Array,
  BigInt64Array,
  BigUint64Array,
];

function test(TA) {
  const length = 4;
  const byteLength = length * TA.BYTES_PER_ELEMENT;

  let ab = new ArrayBuffer(byteLength);
  let actual = new TA(ab);
  let expected = new TA(length);
  let type = expected[0].constructor;

  for (let i = 0; i < length; ++i) {
    actual[i] = type(i * i);
    expected[i] = type(i * i);
  }

  let dv = new DataView(ab.transferToImmutable());
  for (let i = 0; i < 200; ++i) {
    let index = i % length;
    let byteIndex = index * TA.BYTES_PER_ELEMENT;

    assertEq(dv.getElem(byteIndex, nativeIsLittleEndian), expected[index]);
  }
}

for (let TA of TypedArrays) {
  let getter = "get" + typeName(TA);

  // Copy test function to ensure monomorphic ICs.
  let copy = Function(`return ${test}`.replaceAll("getElem", getter))();

  copy(TA);
}
