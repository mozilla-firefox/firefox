// |reftest| shell-option(--enable-arraybuffer-immutable) skip-if(!ArrayBuffer.prototype.sliceToImmutable||!xulRuntime.shell) -- immutable-arraybuffer is not enabled unconditionally, requires shell-options
// Copyright (C) 2025 Richard Gibson. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-typedarray-set
description: >
  Rejects any attempt to set the value for a property whose name is a canonical
  numeric index string that reaches a TypedArray backed by an immutable buffer
  in the prototype chain.
info: |
  [[Set]] ( P, V, Receiver )
  1. If P is a String, then
     a. Let numericIndex be CanonicalNumericIndexString(P).
     b. If numericIndex is not undefined, then
        i. NOTE: TypedArray instances restrict own and inherited canonical numeric string properties to integer indices valid for their backing buffers, but assignment failures for canonical numeric string properties are only reported when the buffer is immutable.
        ii. If IsImmutableBuffer(O.[[ViewedArrayBuffer]]) is true, return false.
features: [TypedArray, immutable-arraybuffer]
flags: [noStrict]
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

  var ta = new TA(makeCtorArg(["1", "2", "3"]));
  var receiver = Object.create(ta, { "2": { writable: true }, "3": { writable: true } });

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
      assert.sameValue(Reflect.set(ta, key, value, receiver), false,
        "Reflect.set must return false for " + label);
    }
    receiver[key] = value;
    assert.sameValue(unloggedHasOwn(ta, key, calls), hasKey,
      "Property must not be created on TypedArray for " + label);
    assert.sameValue(unloggedHasOwn(receiver, key, calls), false,
      "Property must not be created on receiver for " + label);
  }

  var overrideEntries = [
    ["coercible", "0"],
    ["non-coercible", "foo"]
  ];
  for (var key = 2; key <= 3; key++) {
    for (var i = 0; i < overrideEntries.length; i++) {
      var label = overrideEntries[i][0];
      var value = overrideEntries[i][1];
      if (hasReflectSet) {
        assert.sameValue(Reflect.set(receiver, key, value), true,
          "Reflect.set must return true for setting overridden property to " + label);
        assert.sameValue(receiver[key], value,
          "Reflect.set must succeed for setting overridden property to " + label);
      }
      receiver[key] = value;
      assert.sameValue(receiver[key], value,
        "Assignment must succeed for setting overridden property to " + label);
    }
  }

  assert.compareArray(ta, new TA(["1", "2", "3"]), "Contents must not change");

  var expectCalls = hasReflectSet ? ["key toString", "key toString"] : ["key toString"];
  assert.compareArray(calls, expectCalls, "Recorded calls");
}, null, ["immutable"]);

reportCompare(0, 0);
