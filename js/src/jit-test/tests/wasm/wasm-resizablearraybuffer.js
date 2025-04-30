// |jit-test| skip-if: !('toResizableBuffer' in WebAssembly.Memory.prototype)

let mem = new WebAssembly.Memory({initial: 20, maximum: 50});

// Utils for testing

let ins = wasmEvalText(`(module
    (import "" "mem" (memory 20 50))
    (func (export "check") (param i32 i32 i32) (result i32)
      block
       loop
        local.get 0
        i32.load
        local.get 2
        i32.eq
        i32.eqz
        br_if 1

        local.get 1
        i32.const 1
        i32.sub
        local.set 1
        local.get 0
        i32.const 4
        i32.add
        local.set 0
        local.get 1
        br_if 0
        i32.const 1
        return
       end
      end
      i32.const 0
    )
    (func (export "fill") (param i32 i32 i32)
       loop
        local.get 0
        local.get 2
        i32.store

        local.get 1
        i32.const 1
        i32.sub
        local.set 1
        local.get 0
        i32.const 4
        i32.add
        local.set 0
        local.get 1
        br_if 0
       end
    )
)`, {"": {mem,}});

function check(off, count, value) {
    const arr = new Int32Array(mem.buffer);
    for (let i = 0; i < count; i++) {
        assertEq(arr[(off >> 2) + i], value);
    }
}
function fill(off, count, value) {
    const arr = new Int32Array(mem.buffer);
    for (let i = 0; i < count; i++) {
        arr[i] = value;
    }
}

fill(0, 10, 1);
assertEq(ins.exports.check(0, 10, 1), 1);

// Convert to resizable array, back to fixed-length, again to resizable,
// and attempt to resize using JS.

let ab = mem.buffer;
assertEq(ab.resizable, false);

// Make .buffer resizable, detaching the old one.
let rab = mem.toResizableBuffer();
assertEq(rab.resizable, true);
assertEq(mem.buffer, rab);
assertEq(ab.detached, true);
assertEq(rab.maxByteLength, 50 << 16);

assertEq(ins.exports.check(0, 10, 1), 1);
ins.exports.fill(0, 10, 3);
check(0, 10, 3);

// We can go back if we choose.
let ab2 = mem.toFixedLengthBuffer();
assertEq(ab2.resizable, false);
assertEq(mem.buffer, ab2);
assertEq(ab2 !== ab, true);
assertEq(rab.detached, true);
assertEq(ab2.maxByteLength, 20 << 16);

assertEq(ins.exports.check(0, 10, 3), 1);
ins.exports.fill(0, 10, 2);
check(0, 10, 2);

assertThrowsInstanceOf(
    () => ins.exports.check(20 * 65536 - 4, 2, 0), WebAssembly.RuntimeError);
ins.exports.fill(20 * 65536 - 4, 1, 20);

// Let's go back to resizable. Memory#grow no longer detaches .buffer when it's resizable.
rab = mem.toResizableBuffer();
let oldLen = rab.byteLength;
mem.grow(8);
assertEq(rab.detached, false);
assertEq(rab.byteLength, oldLen + (8 * 65536))
assertEq(rab.maxByteLength, 50 << 16);

ins.exports.check(20 * 65536 - 4, 1, 20);
ins.exports.check(20 * 65536, 1, 0);
assertThrowsInstanceOf(
    () => ins.exports.check(28 * 65536 - 4, 2, 0), WebAssembly.RuntimeError);

assertEq(ins.exports.check(0, 10, 2), 1);
ins.exports.fill(0, 10, 5);
check(0, 10, 5);

// Try to resize JS way.
rab.resize(65536 * 30);
assertEq(rab.byteLength, 30 * 65536);
ins.exports.fill(30 * 65536 - 10*4, 10, 6);
check(30 * 65536 - 10 * 4, 10, 6);

// RAB#resize throws when trying to shrink or grow by non-page multiples
// for WebAssembly.Memory-vended RABs.
assertThrowsInstanceOf(() => rab.resize(rab.byteLength - 65536), RangeError);
assertThrowsInstanceOf(() => rab.resize(rab.byteLength + 10), RangeError);

// No allow to make buffer resizable when max in not specified.

mem = new WebAssembly.Memory({initial: 20});
assertThrowsInstanceOf(() => mem.toResizableBuffer(), TypeError);
