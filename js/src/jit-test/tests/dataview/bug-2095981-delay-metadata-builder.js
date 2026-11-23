//|jit-test| --enable-arraybuffer-immutable

gczeal(2,1);
enableTrackAllocations();

new DataView(new ArrayBuffer(16));
new DataView(new ArrayBuffer(16, {maxByteLength: 32}));
if (ArrayBuffer.prototype.transferToImmutable) {
	new DataView(new ArrayBuffer(16).transferToImmutable());
}
