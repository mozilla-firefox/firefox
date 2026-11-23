// GENERATED, DO NOT EDIT
// file: deepEqual.js
// Copyright 2019 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: >
  Compare two values structurally
defines: [assert.deepEqual]
---*/

assert.deepEqual = function(actual, expected, message) {
  var format = assert.deepEqual.format;
  var mustBeTrue = assert.deepEqual._compare(actual, expected);

  // format can be slow when `actual` or `expected` are large objects, like for
  // example the global object, so only call it when the assertion will fail.
  if (mustBeTrue !== true) {
    message = `Expected ${format(actual)} to be structurally equal to ${format(expected)}. ${(message || '')}`;
  }

  assert(mustBeTrue, message);
};

(function() {
let getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
let join = arr => arr.join(', ');
function stringFromTemplate(strings, subs) {
  let parts = strings.map((str, i) => `${i === 0 ? '' : subs[i - 1]}${str}`);
  return parts.join('');
}
function escapeKey(key) {
  if (typeof key === 'symbol') return `[${String(key)}]`;
  if (/^[a-zA-Z0-9_$]+$/.test(key)) return key;
  return assert._formatIdentityFreeValue(key);
}

assert.deepEqual.format = function(value, seen) {
  let basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  switch (value === null ? 'null' : typeof value) {
    case 'string':
    case 'bigint':
    case 'number':
    case 'boolean':
    case 'undefined':
    case 'null':
      assert(false, 'values without identity should use basic formatting');
      break;
    case 'symbol':
    case 'function':
    case 'object':
      break;
    default:
      return typeof value;
  }

  if (!seen) {
    seen = {
      counter: 0,
      map: new Map()
    };
  }
  let usage = seen.map.get(value);
  if (usage) {
    usage.used = true;
    return `ref #${usage.id}`;
  }
  usage = { id: ++seen.counter, used: false };
  seen.map.set(value, usage);

  // Properly communicating multiple references requires deferred rendering of
  // all identity-bearing values until the outermost format call finishes,
  // because the current value can also in appear in a not-yet-visited part of
  // the object graph (which, when visited, will update the usage object).
  //
  // To preserve readability of the desired output formatting, we accomplish
  // this deferral using tagged template literals.
  // Evaluation closes over the usage object and returns a function that accepts
  // "mapper" arguments for rendering the corresponding substitution values and
  // returns an object with only a toString method which will itself be invoked
  // when trying to use the result as a string in assert.deepEqual.
  //
  // For convenience, any absent mapper is presumed to be `String`, and the
  // function itself has a toString method that self-invokes with no mappers
  // (allowing returning the function directly when every mapper is `String`).
  function lazyResult(strings, ...subs) {
    function acceptMappers(...mappers) {
      function toString() {
        let renderings = subs.map((sub, i) => (mappers[i] || String)(sub));
        let rendered = stringFromTemplate(strings, renderings);
        if (usage.used) rendered += ` as #${usage.id}`;
        return rendered;
      }

      return { toString };
    }

    acceptMappers.toString = () => String(acceptMappers());
    return acceptMappers;
  }

  let format = assert.deepEqual.format;
  function lazyString(strings, ...subs) {
    return { toString: () => stringFromTemplate(strings, subs) };
  }

  if (typeof value === 'function') {
    return lazyResult`function${value.name ? ` ${String(value.name)}` : ''}`;
  }
  if (typeof value !== 'object') {
    // probably a symbol
    return lazyResult`${value}`;
  }
  if (Array.isArray ? Array.isArray(value) : value instanceof Array) {
    return lazyResult`[${value.map(value => format(value, seen))}]`(join);
  }
  if (value instanceof Date) {
    return lazyResult`Date(${format(value.toISOString(), seen)})`;
  }
  if (value instanceof Error) {
    return lazyResult`error ${value.name || 'Error'}(${format(value.message, seen)})`;
  }
  if (value instanceof RegExp) {
    return lazyResult`${value}`;
  }
  if (typeof Map !== "undefined" && value instanceof Map) {
    let contents = Array.from(value).map(pair => lazyString`${format(pair[0], seen)} => ${format(pair[1], seen)}`);
    return lazyResult`Map {${contents}}`(join);
  }
  if (typeof Set !== "undefined" && value instanceof Set) {
    let contents = Array.from(value).map(value => format(value, seen));
    return lazyResult`Set {${contents}}`(join);
  }

  let tag = Symbol.toStringTag && Symbol.toStringTag in value
    ? value[Symbol.toStringTag]
    : Object.getPrototypeOf(value) === null ? '[Object: null prototype]' : 'Object';
  let keys = Reflect.ownKeys(value).filter(key => getOwnPropertyDescriptor(value, key).enumerable);
  let contents = keys.map(key => lazyString`${escapeKey(key)}: ${format(value[key], seen)}`);
  return lazyResult`${tag ? `${tag} ` : ''}{${contents}}`(String, join);
};
})();

assert.deepEqual._compare = (function () {
  var EQUAL = 1;
  var NOT_EQUAL = -1;
  var UNKNOWN = 0;

  function deepEqual(a, b) {
    return compareEquality(a, b) === EQUAL;
  }

  function compareEquality(a, b, cache) {
    return compareIf(a, b, isOptional, compareOptionality)
      || compareIf(a, b, isPrimitiveEquatable, comparePrimitiveEquality)
      || compareIf(a, b, isObjectEquatable, compareObjectEquality, cache)
      || NOT_EQUAL;
  }

  function compareIf(a, b, test, compare, cache) {
    return !test(a)
      ? !test(b) ? UNKNOWN : NOT_EQUAL
      : !test(b) ? NOT_EQUAL : cacheComparison(a, b, compare, cache);
  }

  function tryCompareStrictEquality(a, b) {
    return a === b ? EQUAL : UNKNOWN;
  }

  function tryCompareTypeOfEquality(a, b) {
    return typeof a !== typeof b ? NOT_EQUAL : UNKNOWN;
  }

  function tryCompareToStringTagEquality(a, b) {
    var aTag = Symbol.toStringTag in a ? a[Symbol.toStringTag] : undefined;
    var bTag = Symbol.toStringTag in b ? b[Symbol.toStringTag] : undefined;
    return aTag !== bTag ? NOT_EQUAL : UNKNOWN;
  }

  function isOptional(value) {
    return value === undefined
      || value === null;
  }

  function compareOptionality(a, b) {
    return tryCompareStrictEquality(a, b)
      || NOT_EQUAL;
  }

  function isPrimitiveEquatable(value) {
    switch (typeof value) {
      case 'string':
      case 'number':
      case 'bigint':
      case 'boolean':
      case 'symbol':
        return true;
      default:
        return isBoxed(value);
    }
  }

  function comparePrimitiveEquality(a, b) {
    if (isBoxed(a)) a = a.valueOf();
    if (isBoxed(b)) b = b.valueOf();
    return tryCompareStrictEquality(a, b)
      || tryCompareTypeOfEquality(a, b)
      || compareIf(a, b, isNaNEquatable, compareNaNEquality)
      || NOT_EQUAL;
  }

  function isNaNEquatable(value) {
    return typeof value === 'number';
  }

  function compareNaNEquality(a, b) {
    return isNaN(a) && isNaN(b) ? EQUAL : NOT_EQUAL;
  }

  function isObjectEquatable(value) {
    return typeof value === 'object' || typeof value === 'function';
  }

  function compareObjectEquality(a, b, cache) {
    if (!cache) cache = new Map();
    return getCache(cache, a, b)
      || setCache(cache, a, b, EQUAL) // consider equal for now
      || cacheComparison(a, b, tryCompareStrictEquality, cache)
      || cacheComparison(a, b, tryCompareToStringTagEquality, cache)
      || compareIf(a, b, isValueOfEquatable, compareValueOfEquality)
      || compareIf(a, b, isToStringEquatable, compareToStringEquality)
      || compareIf(a, b, isArrayLikeEquatable, compareArrayLikeEquality, cache)
      || compareIf(a, b, isStructurallyEquatable, compareStructuralEquality, cache)
      || compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
      || cacheComparison(a, b, fail, cache);
  }

  function isBoxed(value) {
    return value instanceof String
      || value instanceof Number
      || value instanceof Boolean
      || typeof Symbol === 'function' && value instanceof Symbol
      || typeof BigInt === 'function' && value instanceof BigInt;
  }

  function isValueOfEquatable(value) {
    return value instanceof Date;
  }

  function compareValueOfEquality(a, b) {
    return compareIf(a.valueOf(), b.valueOf(), isPrimitiveEquatable, comparePrimitiveEquality)
      || NOT_EQUAL;
  }

  function isToStringEquatable(value) {
    return value instanceof RegExp;
  }

  function compareToStringEquality(a, b) {
    return compareIf(a.toString(), b.toString(), isPrimitiveEquatable, comparePrimitiveEquality)
      || NOT_EQUAL;
  }

  function isArrayLikeEquatable(value) {
    return (Array.isArray ? Array.isArray(value) : value instanceof Array)
      || (typeof Uint8Array === 'function' && value instanceof Uint8Array)
      || (typeof Uint8ClampedArray === 'function' && value instanceof Uint8ClampedArray)
      || (typeof Uint16Array === 'function' && value instanceof Uint16Array)
      || (typeof Uint32Array === 'function' && value instanceof Uint32Array)
      || (typeof Int8Array === 'function' && value instanceof Int8Array)
      || (typeof Int16Array === 'function' && value instanceof Int16Array)
      || (typeof Int32Array === 'function' && value instanceof Int32Array)
      || (typeof Float32Array === 'function' && value instanceof Float32Array)
      || (typeof Float64Array === 'function' && value instanceof Float64Array)
      || (typeof BigUint64Array === 'function' && value instanceof BigUint64Array)
      || (typeof BigInt64Array === 'function' && value instanceof BigInt64Array);
  }

  function compareArrayLikeEquality(a, b, cache) {
    if (a.length !== b.length) return NOT_EQUAL;
    for (var i = 0; i < a.length; i++) {
      if (compareEquality(a[i], b[i], cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
    }
    return EQUAL;
  }

  function isStructurallyEquatable(value) {
    return !(typeof Promise === 'function' && value instanceof Promise // only comparable by reference
      || typeof WeakMap === 'function' && value instanceof WeakMap // only comparable by reference
      || typeof WeakSet === 'function' && value instanceof WeakSet // only comparable by reference
      || typeof Map === 'function' && value instanceof Map // comparable via @@iterator
      || typeof Set === 'function' && value instanceof Set); // comparable via @@iterator
  }

  function compareStructuralEquality(a, b, cache) {
    var aKeys = [];
    for (var key in a) aKeys.push(key);

    var bKeys = [];
    for (var key in b) bKeys.push(key);

    if (aKeys.length !== bKeys.length) {
      return NOT_EQUAL;
    }

    aKeys.sort();
    bKeys.sort();

    for (var i = 0; i < aKeys.length; i++) {
      var aKey = aKeys[i];
      var bKey = bKeys[i];
      if (compareEquality(aKey, bKey, cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
      if (compareEquality(a[aKey], b[bKey], cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
    }

    return compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
      || EQUAL;
  }

  function isIterableEquatable(value) {
    return typeof Symbol === 'function'
      && typeof value[Symbol.iterator] === 'function';
  }

  function compareIteratorEquality(a, b, cache) {
    if (typeof Map === 'function' && a instanceof Map && b instanceof Map ||
      typeof Set === 'function' && a instanceof Set && b instanceof Set) {
      if (a.size !== b.size) return NOT_EQUAL; // exit early if we detect a difference in size
    }

    var ar, br;
    while (true) {
      ar = a.next();
      br = b.next();
      if (ar.done) {
        if (br.done) return EQUAL;
        if (b.return) b.return();
        return NOT_EQUAL;
      }
      if (br.done) {
        if (a.return) a.return();
        return NOT_EQUAL;
      }
      if (compareEquality(ar.value, br.value, cache) === NOT_EQUAL) {
        if (a.return) a.return();
        if (b.return) b.return();
        return NOT_EQUAL;
      }
    }
  }

  function compareIterableEquality(a, b, cache) {
    return compareIteratorEquality(a[Symbol.iterator](), b[Symbol.iterator](), cache);
  }

  function cacheComparison(a, b, compare, cache) {
    var result = compare(a, b, cache);
    if (cache && (result === EQUAL || result === NOT_EQUAL)) {
      setCache(cache, a, b, /** @type {EQUAL | NOT_EQUAL} */(result));
    }
    return result;
  }

  function fail() {
    return NOT_EQUAL;
  }

  function setCache(cache, left, right, result) {
    var otherCache;

    otherCache = cache.get(left);
    if (!otherCache) cache.set(left, otherCache = new Map());
    otherCache.set(right, result);

    otherCache = cache.get(right);
    if (!otherCache) cache.set(right, otherCache = new Map());
    otherCache.set(left, result);
  }

  function getCache(cache, left, right) {
    var otherCache;
    var result;

    otherCache = cache.get(left);
    result = otherCache && otherCache.get(right);
    if (result) return result;

    otherCache = cache.get(right);
    result = otherCache && otherCache.get(left);
    if (result) return result;

    return UNKNOWN;
  }

  return deepEqual;
})();

// file: detachArrayBuffer.js
// Copyright (C) 2016 the V8 project authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    A function used in the process of asserting correctness of TypedArray objects.

    $262.detachArrayBuffer is defined by a host.
defines: [$DETACHBUFFER]
---*/

function $DETACHBUFFER(buffer) {
  if (!$262 || typeof $262.detachArrayBuffer !== "function") {
    throw new Test262Error("No method available to detach an ArrayBuffer");
  }
  $262.detachArrayBuffer(buffer);
}

// file: assertThrowsValue.js
/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*---
defines: [assertThrowsValue]
---*/

function assertThrowsValue(f, val, msg) {
  try {
    f();
  } catch (exc) {
    assert.sameValue(exc, val, msg);
    return;
  }

  var fullmsg = "Assertion failed: expected exception, no exception thrown";
  if (msg !== void 0) {
    fullmsg += " - " + msg;
  }
  throw new Test262Error(fullmsg);
}

// file: non262-TypedArray-shell.js
/*---
defines: [typedArrayConstructors, sharedTypedArrayConstructors, anyTypedArrayConstructors, isSharedConstructor, isFloatConstructor, isUnsignedConstructor]
---*/
(function(global) {
    "use strict";

    const {
        Float16Array, Float32Array, Float64Array, Object, Reflect, SharedArrayBuffer, WeakMap,
    } = global;
    const {
        apply: Reflect_apply,
        construct: Reflect_construct,
    } = Reflect;
    const {
        get: WeakMap_prototype_get,
        has: WeakMap_prototype_has,
    } = WeakMap.prototype;

    const sharedConstructors = new WeakMap();

    // Synthesize a constructor for a shared memory array from the constructor
    // for unshared memory. This has "good enough" fidelity for many uses. In
    // cases where it's not good enough, call isSharedConstructor for local
    // workarounds.
    function sharedConstructor(baseConstructor) {
        // Create SharedTypedArray as a subclass of %TypedArray%, following the
        // built-in %TypedArray% subclasses.
        class SharedTypedArray extends Object.getPrototypeOf(baseConstructor) {
            constructor(...args) {
                var array = Reflect_construct(baseConstructor, args);
                var {buffer, byteOffset, length} = array;
                var sharedBuffer = new SharedArrayBuffer(buffer.byteLength);
                var sharedArray = Reflect_construct(baseConstructor,
                                                    [sharedBuffer, byteOffset, length],
                                                    new.target);
                for (var i = 0; i < length; i++)
                    sharedArray[i] = array[i];
                assert.sameValue(sharedArray.buffer, sharedBuffer);
                return sharedArray;
            }
        }

        // 22.2.5.1 TypedArray.BYTES_PER_ELEMENT
        Object.defineProperty(SharedTypedArray, "BYTES_PER_ELEMENT",
                              {__proto__: null, value: baseConstructor.BYTES_PER_ELEMENT});

        // 22.2.6.1 TypedArray.prototype.BYTES_PER_ELEMENT
        Object.defineProperty(SharedTypedArray.prototype, "BYTES_PER_ELEMENT",
                              {__proto__: null, value: baseConstructor.BYTES_PER_ELEMENT});

        // Share the same name with the base constructor to avoid calling
        // isSharedConstructor() in multiple places.
        Object.defineProperty(SharedTypedArray, "name",
                              {__proto__: null, value: baseConstructor.name});

        sharedConstructors.set(SharedTypedArray, baseConstructor);

        return SharedTypedArray;
    }

    /**
     * All TypedArray constructors for unshared memory.
     */
    const typedArrayConstructors = Object.freeze([
        Int8Array,
        Uint8Array,
        Uint8ClampedArray,
        Int16Array,
        Uint16Array,
        Int32Array,
        Uint32Array,
        Float32Array,
        Float64Array,
    ].concat(Float16Array ?? []));

    /**
     * All TypedArray constructors for shared memory.
     */
    const sharedTypedArrayConstructors = Object.freeze(
        typeof SharedArrayBuffer === "function"
        ? typedArrayConstructors.map(sharedConstructor)
        : []
    );

    /**
     * All TypedArray constructors for unshared and shared memory.
     */
    const anyTypedArrayConstructors = Object.freeze([
        ...typedArrayConstructors, ...sharedTypedArrayConstructors,
    ]);

    /**
     * Returns `true` if `constructor` is a TypedArray constructor for shared
     * memory.
     */
    function isSharedConstructor(constructor) {
        return Reflect_apply(WeakMap_prototype_has, sharedConstructors, [constructor]);
    }

    /**
     * Returns `true` if `constructor` is a TypedArray constructor for shared
     * or unshared memory, with an underlying element type of one of Float16, Float32
     * or Float64.
     */
    function isFloatConstructor(constructor) {
        if (isSharedConstructor(constructor))
            constructor = Reflect_apply(WeakMap_prototype_get, sharedConstructors, [constructor]);
        return constructor == Float32Array || constructor == Float64Array || (Float16Array && constructor == Float16Array);
    }

    /**
     * Returns `true` if `constructor` is a TypedArray constructor for shared
     * or unshared memory, with an underlying element type of one of Uint8,
     * Uint8Clamped, Uint16, or Uint32.
     */
    function isUnsignedConstructor(constructor) {
        if (isSharedConstructor(constructor))
            constructor = Reflect_apply(WeakMap_prototype_get, sharedConstructors, [constructor]);
        return constructor == Uint8Array ||
               constructor == Uint8ClampedArray ||
               constructor == Uint16Array ||
               constructor == Uint32Array;
    }

    global.typedArrayConstructors = typedArrayConstructors;
    global.sharedTypedArrayConstructors = sharedTypedArrayConstructors;
    global.anyTypedArrayConstructors = anyTypedArrayConstructors;
    global.isSharedConstructor = isSharedConstructor;
    global.isFloatConstructor = isFloatConstructor;
    global.isUnsignedConstructor = isUnsignedConstructor;
})(this);

// file: testTypedArray.js
// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to assert the correctness of TypedArray objects.
defines:
  - floatArrayConstructors
  - nonClampedIntArrayConstructors
  - intArrayConstructors
  - typedArrayConstructors
  - TypedArray
  - testWithAllTypedArrayConstructors
  - testWithTypedArrayConstructors
  - testWithBigIntTypedArrayConstructors
  - nonAtomicsFriendlyTypedArrayConstructors
  - testWithAtomicsFriendlyTypedArrayConstructors
  - testWithNonAtomicsFriendlyTypedArrayConstructors
  - testTypedArrayConversions
---*/

var floatArrayConstructors = [
  Float64Array,
  Float32Array
];

var nonClampedIntArrayConstructors = [
  Int32Array,
  Int16Array,
  Int8Array,
  Uint32Array,
  Uint16Array,
  Uint8Array
];

var intArrayConstructors = nonClampedIntArrayConstructors.concat([Uint8ClampedArray]);

// Float16Array is a newer feature
// adding it to this list unconditionally would cause implementations lacking it to fail every test which uses it
if (typeof Float16Array !== "undefined") {
  floatArrayConstructors.push(Float16Array);
}

var bigIntArrayConstructors = [];
if (typeof BigInt64Array !== "undefined") {
  bigIntArrayConstructors.push(BigInt64Array);
}
if (typeof BigUint64Array !== "undefined") {
  bigIntArrayConstructors.push(BigUint64Array);
}

/**
 * Array containing every non-bigint typed array constructor.
 */
var typedArrayConstructors = floatArrayConstructors.concat(intArrayConstructors);

/**
 * Array containing every typed array constructor, including those with bigint values.
 */
var allTypedArrayConstructors = typedArrayConstructors.concat(bigIntArrayConstructors);

/**
 * The %TypedArray% intrinsic constructor function.
 */
var TypedArray = Object.getPrototypeOf(Int8Array);

function makePassthrough(TA, primitiveOrIterable) {
  return primitiveOrIterable;
}

function makeArray(TA, primitiveOrIterable) {
  if (isPrimitive(primitiveOrIterable)) {
    var n = Number(primitiveOrIterable);
    // Only values between 0 and 2**53 - 1 inclusive can get mapped into TA contents.
    if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
    return Array.from({ length: n }, function() { return "0"; });
  }
  return Array.from(primitiveOrIterable);
}

function makeArrayLike(TA, primitiveOrIterable) {
  var arr = makeArray(TA, primitiveOrIterable);
  if (isPrimitive(arr)) return arr;
  var obj = { length: arr.length };
  for (var i = 0; i < obj.length; i++) obj[i] = arr[i];
  return obj;
}

var makeIterable;
if (typeof Symbol !== "undefined" && Symbol.iterator) {
  makeIterable = function makeIterable(TA, primitiveOrIterable) {
    var src = makeArray(TA, primitiveOrIterable);
    if (isPrimitive(src)) return src;
    var obj = {};
    obj[Symbol.iterator] = function() { return src[Symbol.iterator](); };
    return obj;
  };
}

function makeArrayBuffer(TA, primitiveOrIterable) {
  var arr = makeArray(TA, primitiveOrIterable);
  if (isPrimitive(arr)) return arr;
  return new TA(arr).buffer;
}

var makeResizableArrayBuffer, makeGrownArrayBuffer, makeShrunkArrayBuffer, makeImmutableArrayBuffer;
if (ArrayBuffer.prototype.resize) {
  var copyIntoArrayBuffer = function(destBuffer, srcBuffer) {
    var destView = new Uint8Array(destBuffer);
    var srcView = new Uint8Array(srcBuffer);
    for (var i = 0; i < srcView.length; i++) destView[i] = srcView[i];
    return destBuffer;
  };

  makeResizableArrayBuffer = function makeResizableArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      return new ArrayBuffer(n, { maxByteLength: n * 2 });
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var resizable = new ArrayBuffer(byteLength, { maxByteLength: byteLength * 2 });
    return copyIntoArrayBuffer(resizable, fixed);
  };

  makeGrownArrayBuffer = function makeGrownArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      var grown = new ArrayBuffer(Math.floor(n / 2), { maxByteLength: n });
      grown.resize(n);
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var grown = new ArrayBuffer(Math.floor(byteLength / 2), { maxByteLength: byteLength });
    grown.resize(byteLength);
    return copyIntoArrayBuffer(grown, fixed);
  };

  makeShrunkArrayBuffer = function makeShrunkArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      var shrunk = new ArrayBuffer(n * 2, { maxByteLength: n * 2 });
      shrunk.resize(n);
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var shrunk = new ArrayBuffer(byteLength * 2, { maxByteLength: byteLength * 2 });
    copyIntoArrayBuffer(shrunk, fixed);
    shrunk.resize(byteLength);
    return shrunk;
  };
}
if (ArrayBuffer.prototype.transferToImmutable) {
  makeImmutableArrayBuffer = function makeImmutableArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      return (new ArrayBuffer(n)).transferToImmutable();
    }
    var mutable = makeArrayBuffer(TA, primitiveOrIterable);
    return mutable.transferToImmutable();
  };
}

var typedArrayCtorArgFactories = [makePassthrough, makeArray, makeArrayLike];
if (makeIterable) typedArrayCtorArgFactories.push(makeIterable);
typedArrayCtorArgFactories.push(makeArrayBuffer);
if (makeResizableArrayBuffer) typedArrayCtorArgFactories.push(makeResizableArrayBuffer);
if (makeGrownArrayBuffer) typedArrayCtorArgFactories.push(makeGrownArrayBuffer);
if (makeShrunkArrayBuffer) typedArrayCtorArgFactories.push(makeShrunkArrayBuffer);
if (makeImmutableArrayBuffer) typedArrayCtorArgFactories.push(makeImmutableArrayBuffer);

/**
 * @typedef {"passthrough" | "arraylike" | "iterable" | "arraybuffer" | "resizable" | "immutable"} typedArrayArgFactoryFeature
 */

/**
 * A predicate for testing whether a TypedArray argument factory from this file
 * matches any of the provided features.
 *
 * @param {Function} argFactory
 * @param {typedArrayArgFactoryFeature[]} features
 * @returns {boolean}
 */
function ctorArgFactoryMatchesSome(argFactory, features) {
  for (var i = 0; i < features.length; ++i) {
    switch (features[i]) {
      case "passthrough":
        if (argFactory === makePassthrough) return true;
        break;
      case "arraylike":
        if (argFactory === makeArray || argFactory === makeArrayLike) return true;
        break;
      case "iterable":
        if (argFactory === makeIterable) return true;
        break;
      case "arraybuffer":
        if (
          argFactory === makeArrayBuffer ||
          argFactory === makeResizableArrayBuffer ||
          argFactory === makeGrownArrayBuffer ||
          argFactory === makeShrunkArrayBuffer ||
          argFactory === makeImmutableArrayBuffer
        ) {
          return true;
        }
        break;
      case "resizable":
        if (
          argFactory === makeResizableArrayBuffer ||
          argFactory === makeGrownArrayBuffer ||
          argFactory === makeShrunkArrayBuffer
        ) {
          return true;
        }
        break;
      case "immutable":
        if (argFactory === makeImmutableArrayBuffer) return true;
        break;
      default:
        throw new Test262Error("unknown feature: " + features[i]);
    }
  }
  return false;
}

/**
 * Callback for testing a typed array constructor.
 *
 * @callback typedArrayConstructorCallback
 * @param {Function} TypedArrayConstructor the constructor object to test with
 * @param {Function} [TypedArrayConstructorArgFactory] a function for making
 *   a TypedArrayConstructor argument from a primitive (usually a number) or
 *   iterable (usually an array)
 */

/**
 * Calls the provided function with (typedArrayCtor, typedArrayCtorArgFactory)
 * pairs, where typedArrayCtor is Uint8Array/Int8Array/BigInt64Array/etc. and
 * typedArrayCtorArgFactory is a function for mapping a primitive (usually a
 * number) or iterable (usually an array) into a value suitable as the first
 * argument of typedArrayCtor (an Array, arraylike, iterable, or ArrayBuffer).
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithAllTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || allTypedArrayConstructors;
  var ctorArgFactories = typedArrayCtorArgFactories;
  if (includeArgFactories) {
    ctorArgFactories = [];
    for (var i = 0; i < typedArrayCtorArgFactories.length; ++i) {
      if (ctorArgFactoryMatchesSome(typedArrayCtorArgFactories[i], includeArgFactories)) {
        ctorArgFactories.push(typedArrayCtorArgFactories[i]);
      }
    }
  }
  if (excludeArgFactories) {
    ctorArgFactories = ctorArgFactories.slice();
    for (var i = ctorArgFactories.length - 1; i >= 0; --i) {
      if (ctorArgFactoryMatchesSome(ctorArgFactories[i], excludeArgFactories)) {
        ctorArgFactories.splice(i, 1);
      }
    }
  }
  if (ctorArgFactories.length === 0) {
    throw new Test262Error("no arg factories match include " + includeArgFactories + " and exclude " + excludeArgFactories);
  }
  for (var k = 0; k < ctorArgFactories.length; ++k) {
    var argFactory = ctorArgFactories[k];
    for (var i = 0; i < ctors.length; ++i) {
      var constructor = ctors[i];
      var boundArgFactory = argFactory.bind(undefined, constructor);
      try {
        f(constructor, boundArgFactory);
      } catch (e) {
        e.message += " (Testing with " + constructor.name + " and " + argFactory.name + ".)";
        throw e;
      }
    }
  }
}

/**
 * Calls the provided function with (typedArrayCtor, typedArrayCtorArgFactory)
 * pairs, where typedArrayCtor is Uint8Array/Int8Array/BigInt64Array/etc. and
 * typedArrayCtorArgFactory is a function for mapping a primitive (usually a
 * number) or iterable (usually an array) into a value suitable as the first
 * argument of typedArrayCtor (an Array, arraylike, iterable, or ArrayBuffer).
 *
 * typedArrayCtor will not be BigInt64Array or BigUint64Array unless one or both
 * of those are explicitly provided.
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || typedArrayConstructors;
  testWithAllTypedArrayConstructors(f, ctors, includeArgFactories, excludeArgFactories);
}

/**
 * Calls the provided function for every BigInt typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithBigIntTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || [BigInt64Array, BigUint64Array];
  testWithAllTypedArrayConstructors(f, ctors, includeArgFactories, excludeArgFactories);
}

var nonAtomicsFriendlyTypedArrayConstructors = floatArrayConstructors.concat([Uint8ClampedArray]);
/**
 * Calls the provided function for every non-"Atomics Friendly" typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call for each typed array constructor.
 * @param {Array} selected - An optional Array with filtered typed arrays
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithNonAtomicsFriendlyTypedArrayConstructors(f, includeArgFactories, excludeArgFactories) {
  testWithAllTypedArrayConstructors(
    f,
    nonAtomicsFriendlyTypedArrayConstructors,
    includeArgFactories,
    excludeArgFactories
  );
}

/**
 * Calls the provided function for every "Atomics Friendly" typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call for each typed array constructor.
 * @param {Array} selected - An optional Array with filtered typed arrays
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithAtomicsFriendlyTypedArrayConstructors(f, includeArgFactories, excludeArgFactories) {
  testWithAllTypedArrayConstructors(
    f,
    [
      Int32Array,
      Int16Array,
      Int8Array,
      Uint32Array,
      Uint16Array,
      Uint8Array,
    ],
    includeArgFactories,
    excludeArgFactories
  );
}

/**
 * Helper for conversion operations on TypedArrays, the expected values
 * properties are indexed in order to match the respective value for each
 * TypedArray constructor
 * @param  {Function} fn - the function to call for each constructor and value.
 *                         will be called with the constructor, value, expected
 *                         value, and a initial value that can be used to avoid
 *                         a false positive with an equivalent expected value.
 */
function testTypedArrayConversions(byteConversionValues, fn) {
  var values = byteConversionValues.values;
  var expected = byteConversionValues.expected;

  testWithTypedArrayConstructors(function(TA) {
    var name = TA.name.slice(0, -5);

    return values.forEach(function(value, index) {
      var exp = expected[name][index];
      var initial = 0;
      if (exp === 0) {
        initial = 1;
      }
      fn(TA, value, exp, initial);
    });
  }, null, ["passthrough"]);
}

/**
 * Checks if the given argument is one of the float-based TypedArray constructors.
 *
 * @param {constructor} ctor - the value to check
 * @returns {boolean}
 */
function isFloatTypedArrayConstructor(arg) {
  return floatArrayConstructors.indexOf(arg) !== -1;
}

/**
 * Determines the precision of the given float-based TypedArray constructor.
 *
 * @param {constructor} ctor - the value to check
 * @returns {string} "half", "single", or "double" for Float16Array, Float32Array, and Float64Array respectively.
 */
function floatTypedArrayConstructorPrecision(FA) {
  if (typeof Float16Array !== "undefined" && FA === Float16Array) {
    return "half";
  } else if (FA === Float32Array) {
    return "single";
  } else if (FA === Float64Array) {
    return "double";
  } else {
    throw new Error("Malformed test - floatTypedArrayConstructorPrecision called with non-float TypedArray");
  }
}
