// |reftest| shell-option(--enable-arraybuffer-immutable) skip-if(!ArrayBuffer.prototype.sliceToImmutable||!xulRuntime.shell) -- immutable-arraybuffer is not enabled unconditionally, requires shell-options
// Copyright (C) 2025 Richard Gibson. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-typedarray-defineownproperty
description: >
  When the backing buffer is immutable, properties for in-bounds indexes are
  validated to be non-configurable, enumerable, non-writable, and
  value-preserving.
info: |
  [[DefineOwnProperty]] ( P, Desc )
  1. If P is a String, then
     a. Let numericIndex be CanonicalNumericIndexString(P).
     b. If numericIndex is not undefined, then
        i. If IsValidIntegerIndex(O, numericIndex) is false, return false.
        ii. If IsImmutableBuffer(O.[[ViewedArrayBuffer]]) is true, then
            1. Let current be ! O.[[GetOwnProperty]](P).
            2. Assert: current.[[Configurable]] and current.[[Writable]] are both false.
            3. NOTE: Attempting to redefine an immutable value always fails, even if the new value would be cast to the current value.
            4. Return ValidateAndApplyPropertyDescriptor(O, P, false, Desc, current).
features: [TypedArray, immutable-arraybuffer]
includes: [testTypedArray.js, compareArray.js, deepEqual.js]
---*/

function reprDesc(desc) {
  var str = JSON.stringify(desc, function(_key, value) {
    if (typeof value === "bigint") return String(value) + "n";
    if (isNegativeZero(value)) return "-0";
    return value;
  });
  return str.replace(/"([0-9]+n|-0)"/, "$1");
}

testWithAllTypedArrayConstructors(function(TA, makeCtorArg) {
  var sample = new TA(makeCtorArg(["0", "1"]));
  var descriptor = Object.getOwnPropertyDescriptor(sample, "0");

  // Reuse of existing property attributes (possibly partial) succeeds but has
  // no effect.
  for (var v = 0; v < 2; v++) {
    for (var c = 0; c < 2; c++) {
      for (var e = 0; e < 2; e++) {
        for (var w = 0; w < 2; w++) {
          var newDesc = {};
          if (v) newDesc.value = descriptor.value;
          if (c) newDesc.configurable = descriptor.configurable;
          if (e) newDesc.enumerable = descriptor.enumerable;
          if (w) newDesc.writable = descriptor.writable;

          if (typeof Reflect !== "undefined" && Reflect.defineProperty) {
            assert.sameValue(Reflect.defineProperty(sample, "0", newDesc), true,
              "Reflect.defineProperty " + reprDesc(newDesc));
            assert.deepEqual(Object.getOwnPropertyDescriptor(sample, "0"), descriptor,
              "results of Reflect.defineProperty " + reprDesc(newDesc));
          }
          Object.defineProperty(sample, "0", newDesc);
          assert.deepEqual(Object.getOwnPropertyDescriptor(sample, "0"), descriptor,
            "results of Object.defineProperty " + reprDesc(newDesc));
        }
      }
    }
  }

  // Extension of each of the above to include one or invalidating attributes fails
  // with no effect.
  var badValues = ["0", -0];
  for (var v = 0; v < 2; v++) {
    for (var c = 0; c < 2; c++) {
      for (var e = 0; e < 2; e++) {
        for (var w = 0; w < 2; w++) {
          for (var vx = 0; vx <= badValues.length; vx++) {
            for (var cx = 0; cx < 2; cx++) {
              for (var ex = 0; ex < 2; ex++) {
                for (var wx = 0; wx < 2; wx++) {
                  // Require at least one bad attribute.
                  if (!vx && !cx && !ex && !wx) continue;
                  // To avoid redundancy, introduce bad attributes only additively
                  // (i.e., rather than by overwriting a good attribute).
                  if ((v && vx) || (c && cx) || (e && ex) || (w && wx)) continue;

                  var badDesc = {};

                  if (v) badDesc.value = descriptor.value;
                  if (c) badDesc.configurable = descriptor.configurable;
                  if (e) badDesc.enumerable = descriptor.enumerable;
                  if (w) badDesc.writable = descriptor.writable;

                  if (vx) badDesc.value = badValues[vx - 1];
                  if (cx) badDesc.configurable = !descriptor.configurable;
                  if (ex) badDesc.enumerable = !descriptor.enumerable;
                  if (wx) badDesc.writable = !descriptor.writable;

                  if (typeof Reflect !== "undefined" && Reflect.defineProperty) {
                    assert.sameValue(Reflect.defineProperty(sample, "0", badDesc), false,
                      "Reflect.defineProperty " + reprDesc(badDesc));
                    assert.deepEqual(Object.getOwnPropertyDescriptor(sample, "0"), descriptor,
                      "results of Reflect.defineProperty " + reprDesc(badDesc));
                  }
                  assert.throws(TypeError, function() {
                    Object.defineProperty(sample, "0", badDesc);
                  }, "Object.defineProperty " + reprDesc(badDesc));
                  assert.deepEqual(Object.getOwnPropertyDescriptor(sample, "0"), descriptor,
                    "results of Object.defineProperty " + reprDesc(badDesc));
                }
              }
            }
          }
        }
      }
    }
  }
}, null, ["immutable"]);

reportCompare(0, 0);
