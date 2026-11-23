// |jit-test| --enable-arraybuffer-immutable; skip-if: !ArrayBuffer.prototype.transferToImmutable
function test(arr) { return new Uint8Array(arr); }
for (var i = 0; i < 50; i++) test([1,2,3]);
var iab = new ArrayBuffer(16).transferToImmutable();
test(iab);
