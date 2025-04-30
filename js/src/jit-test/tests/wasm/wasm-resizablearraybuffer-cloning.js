// |jit-test| skip-if: !wasmThreadsEnabled() || !('toResizableBuffer' in WebAssembly.Memory.prototype)

// When serializing and deserializing a SAB extracted from a memory, the length
// of the SAB should not change even if the memory was grown after serialization
// and before deserialization.

let mem = new WebAssembly.Memory({initial: 2, maximum: 4, shared: true});
let buf = mem.toResizableBuffer();
let clonedbuf = serialize(buf, [], {SharedArrayBuffer: 'allow'});
mem.grow(1);
let buf2 = deserialize(clonedbuf, {SharedArrayBuffer: 'allow'});
assertEq(buf.byteLength, buf2.byteLength);
