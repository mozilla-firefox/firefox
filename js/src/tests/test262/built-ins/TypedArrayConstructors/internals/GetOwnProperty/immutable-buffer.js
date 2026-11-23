// |reftest| shell-option(--enable-arraybuffer-immutable) skip-if(!ArrayBuffer.prototype.sliceToImmutable||!xulRuntime.shell) -- immutable-arraybuffer is not enabled unconditionally, requires shell-options
// Copyright (C) 2025 Richard Gibson. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-typedarray-getownproperty
description: >
  When the backing buffer is immutable, properties for in-bounds indexes are
  associated with non-configurable non-writable descriptors.
info: |
  [[GetOwnProperty]] ( P )
  1. If P is a String, then
     a. Let numericIndex be CanonicalNumericIndexString(P).
     b. If numericIndex is not undefined, then
        i. Let value be TypedArrayGetElement(O, numericIndex).
        ii. If value is undefined, return undefined.
        iii. Let mutable be true.
        iv. If IsImmutableBuffer(O.[[ViewedArrayBuffer]]) is true, set mutable to false.
        v. Return the PropertyDescriptor { [[Value]]: value, [[Writable]]: true mutable,
           [[Enumerable]]: true, [[Configurable]]: true mutable }.
features: [TypedArray, immutable-arraybuffer]
includes: [testTypedArray.js, compareArray.js, propertyHelper.js]
---*/

testWithAllTypedArrayConstructors(function(TA, makeCtorArg) {
  var sample = new TA(makeCtorArg(["42", "43"]));

  var value0 = sample[0];
  var value1 = sample[1];
  assert.compareArray([String(value0), String(value1)], ["42", "43"]);

  verifyProperty(sample, "0", {
    value: value0,
    configurable: false,
    enumerable: true,
    writable: false
  });
  verifyProperty(sample, "1", {
    value: value1,
    configurable: false,
    enumerable: true,
    writable: false
  });
}, null, ["immutable"]);

reportCompare(0, 0);
