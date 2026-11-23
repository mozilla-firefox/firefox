// |reftest| shell-option(--enable-arraybuffer-immutable) skip-if(!ArrayBuffer.prototype.sliceToImmutable||!xulRuntime.shell) -- immutable-arraybuffer is not enabled unconditionally, requires shell-options
'use strict';
// Copyright (C) 2025 Richard Gibson. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-typedarray-set
description: >
  Rejects any attempt to set the value for a property whose name is a canonical
  numeric index string when the backing buffer is immutable.
info: |
  [[Set]] ( P, V, Receiver )
  1. If P is a String, then
     a. Let numericIndex be CanonicalNumericIndexString(P).
     b. If numericIndex is not undefined, then
        i. NOTE: TypedArray instances restrict own and inherited canonical numeric string properties to integer indices valid for their backing buffers, but assignment failures for canonical numeric string properties are only reported when the buffer is immutable.
        ii. If IsImmutableBuffer(O.[[ViewedArrayBuffer]]) is true, return false.
features: [TypedArray, immutable-arraybuffer]
flags: [onlyStrict]
includes: [testTypedArray.js, compareArray.js]
---*/

function makeSpy(calls, label, value) {
  return {
    toString() {
      calls.push(label + " toString");
      return value;
    },
    valueOf() {
      calls.push(label + " valueOf");
      return value;
    }
  };
}

function unloggedHasOwn(obj, key, calls) {
  var length = calls.length;
  var result = obj.hasOwnProperty(key);
  calls.length = length;
  return result;
}

function repr(value) {
  var t = typeof value;
  if (value === null || t === "undefined") return String(value);
  if (t === "object") return "object";
  if (isNegativeZero(value)) return "number -0";
  return t + " " + value;
}

var hasReflectSet = typeof Reflect !== "undefined" && Reflect.set;

testWithAllTypedArrayConstructors((TA, makeCtorArg) => {
  var calls = [];

  var ta = new TA(makeCtorArg(["1", "2"]));

  var badEntries = [
    ["0", "1"],
    [0, "1"],
    [makeSpy(calls, "key", "0"), makeSpy(calls, "value", "0")],
    ["1", ta[0]],
    [1, ta[0]],
    ["1", "0"],
    [1, "0"],
    ["10", "0"],
    [10, "0"],
    ["-0", "0"],
    ["-1", "0"],
    ["1.5", "0"],
    ["590295810358705700000", "0"],
    ["5e-324", "0"],
    ["1.7976931348623157e+308", "0"],
    ["Infinity", "0"],
    ["NaN", "0"]
  ];
  for (var i = 0; i < badEntries.length; i++) {
    var key = badEntries[i][0];
    var value = badEntries[i][1];
    var hasKey = unloggedHasOwn(ta, key, calls);
    var entryRepr = "[" + repr(key) + ", " + repr(value) + "]";
    var label = "canonical numeric index string entry " + entryRepr;
    if (hasReflectSet) {
      assert.sameValue(Reflect.set(ta, key, value), false,
        "Reflect.set must return false for " + label);
    }
    assert.throws(TypeError, function() {
      ta[key] = value;
    }, "Assignment must throw for " + label);
    assert.sameValue(unloggedHasOwn(ta, key, calls), hasKey,
      "Property must not be created for " + label);
  }

  assert.compareArray(ta, new TA(["1", "2"]), "Contents must not change");

  var expectCalls = hasReflectSet ? ["key toString", "key toString"] : ["key toString"];
  assert.compareArray(calls, expectCalls, "Recorded calls");
}, null, ["immutable"]);

reportCompare(0, 0);
