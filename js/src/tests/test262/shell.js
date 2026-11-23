// GENERATED, DO NOT EDIT
// file: assert.js
// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of assertion functions used throughout test262
defines:
  - assert
  - compareArray
  - formatIdentityFreeValue
  - formatSimpleValue
  - isNegativeZero
  - isPrimitive
---*/


function isNegativeZero(value) {
  return value === 0 && 1 / value === -Infinity;
}

function isPrimitive(value) {
  return !value || (typeof value !== 'object' && typeof value !== 'function');
}

function formatIdentityFreeValue(value) {
  switch (value === null ? 'null' : typeof value) {
    case 'string':
      return typeof JSON !== "undefined" ? JSON.stringify(value) : '"' + value + '"';
    case 'bigint':
      return String(value) + "n";
    case 'number':
      if (isNegativeZero(value)) return '-0';
      // falls through
    case 'boolean':
    case 'undefined':
    case 'null':
      return String(value);
  }
}

function formatSimpleValue(value) {
  var basic = formatIdentityFreeValue(value);
  if (basic) return basic;
  try {
    return String(value);
  } catch (err) {
    if (err.name === 'TypeError') {
      return Object.prototype.toString.call(value);
    }
    throw err;
  }
}

function assert(mustBeTrue, message) {
  if (mustBeTrue === true) {
    return;
  }

  if (message === undefined) {
    message = 'Expected true but got ' + assert._toString(mustBeTrue);
  }
  throw new Test262Error(message);
}

assert._isSameValue = function (a, b) {
  if (a === b) {
    // Handle +/-0 vs. -/+0
    return a !== 0 || 1 / a === 1 / b;
  }

  // Handle NaN vs. NaN
  return a !== a && b !== b;
};

assert.sameValue = function (actual, expected, message) {
  try {
    if (assert._isSameValue(actual, expected)) {
      return;
    }
  } catch (error) {
    throw new Test262Error(message + ' (_isSameValue operation threw) ' + error);
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(expected) + '») to be true';

  throw new Test262Error(message);
};

assert.notSameValue = function (actual, unexpected, message) {
  if (!assert._isSameValue(actual, unexpected)) {
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(unexpected) + '») to be false';

  throw new Test262Error(message);
};

assert.throws = function (expectedErrorConstructor, func, message) {
  var expectedName, actualName;
  if (typeof func !== "function") {
    throw new Test262Error('assert.throws requires two arguments: the error constructor ' +
      'and a function to run');
    return;
  }
  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  try {
    func();
  } catch (thrown) {
    if (typeof thrown !== 'object' || thrown === null) {
      message += 'Thrown value was not an object!';
      throw new Test262Error(message);
    } else if (thrown.constructor !== expectedErrorConstructor) {
      expectedName = expectedErrorConstructor.name;
      actualName = thrown.constructor.name;
      if (expectedName === actualName) {
        message += 'Expected a ' + expectedName + ' but got a different error constructor with the same name';
      } else {
        message += 'Expected a ' + expectedName + ' but got a ' + actualName;
      }
      throw new Test262Error(message);
    }
    return;
  }

  message += 'Expected a ' + expectedErrorConstructor.name + ' to be thrown but no exception was thrown at all';
  throw new Test262Error(message);
};

assert.compareArray = function (actual, expected, message) {
  message = message === undefined ? '' : message;

  if (typeof message === 'symbol') {
    message = message.toString();
  }

  if (isPrimitive(actual)) {
    assert(false, "Actual argument [" + actual + "] shouldn't be primitive. " + String(message));
  } else if (isPrimitive(expected)) {
    assert(false, "Expected argument [" + expected + "] shouldn't be primitive. " + String(message));
  }
  var result = compareArray(actual, expected);
  if (result) return;

  var format = compareArray.format;
  assert(false, "Actual " + format(actual) + " and expected " + format(expected) + " should have the same contents. " + String(message));
};

function compareArray(a, b) {
  if (b.length !== a.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (!assert._isSameValue(b[i], a[i])) {
      return false;
    }
  }
  return true;
}

compareArray.format = function (arrayLike) {
  return "[" + Array.prototype.map.call(arrayLike, String).join(", ") + "]";
};

assert._formatIdentityFreeValue = formatIdentityFreeValue;

assert._toString = formatSimpleValue;

// file: compareArray.js
// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Deprecated now that compareArray is defined in assert.js.
allow_unused: true
---*/

// file: propertyHelper.js
// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to safely verify the correctness of
    property descriptors.
defines:
  - verifyProperty
  - verifyCallableProperty
  - verifyAccessorProperty
  - verifyEqualTo # deprecated
  - verifyWritable # deprecated
  - verifyNotWritable # deprecated
  - verifyEnumerable # deprecated
  - verifyNotEnumerable # deprecated
  - verifyConfigurable # deprecated
  - verifyNotConfigurable # deprecated
  - verifyPrimordialProperty
  - verifyPrimordialCallableProperty
  - verifyPrimordialAccessorProperty
---*/

// @ts-check

var INFINITY = 1 / 0;

// Capture primordial functions and receiver-uncurried primordial methods that
// are used in verification but might be destroyed *by* that process itself.
var __String = String;
var __isArray = Array.isArray;
var __max = Math.max;
var __defineProperty = Object.defineProperty;
var __getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
var __getOwnPropertyNames = Object.getOwnPropertyNames;
var __join = Function.prototype.call.bind(Array.prototype.join);
var __push = Function.prototype.call.bind(Array.prototype.push);
var __hasOwnProperty = Function.prototype.call.bind(Object.prototype.hasOwnProperty);
var __propertyIsEnumerable = Function.prototype.call.bind(Object.prototype.propertyIsEnumerable);
var __getTypedArrayByteLength =
  Object.getPrototypeOf &&
  typeof Uint8Array !== "undefined" &&
  Function.prototype.call.bind(
    __getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype), "byteLength").get
  );

/**
 * @param {object} obj
 * @param {string|symbol} name
 * @param {PropertyDescriptor|undefined} desc
 * @param {object} [options]
 * @param {boolean} [options.label]
 * @param {boolean} [options.restore] revert mutations from verifying writable/configurable
 */
function verifyProperty(obj, name, desc, options) {
  assert(
    arguments.length > 2,
    'verifyProperty should receive at least 3 arguments: obj, name, and descriptor'
  );
  var label = options && options.label || __String(name);

  var originalDesc = __getOwnPropertyDescriptor(obj, name);

  // Allows checking for undefined descriptor if it's explicitly given.
  if (desc === undefined) {
    assert.sameValue(
      originalDesc,
      undefined,
      label + " descriptor should be undefined"
    );

    // desc and originalDesc are both undefined, problem solved;
    return true;
  }

  assert(__hasOwnProperty(obj, name), label + " should be an own property");

  assert.notSameValue(
    desc,
    null,
    "The desc argument should be an object or undefined, null"
  );

  assert.sameValue(
    typeof desc,
    "object",
    "The desc argument should be an object or undefined, " + __String(desc)
  );

  var names = __getOwnPropertyNames(desc);
  for (var i = 0; i < names.length; i++) {
    assert(
      names[i] === "value" ||
        names[i] === "writable" ||
        names[i] === "enumerable" ||
        names[i] === "configurable" ||
        names[i] === "get" ||
        names[i] === "set",
      "Invalid descriptor field: " + names[i]
    );
  }

  var failures = [];

  if (__hasOwnProperty(desc, 'value')) {
    if (!isSameValue(desc.value, originalDesc.value)) {
      __push(failures, label + " descriptor value should be " + __String(desc.value));
    }
    if (!isSameValue(desc.value, obj[name])) {
      __push(failures, label + " value should be " + __String(desc.value));
    }
  }

  if (__hasOwnProperty(desc, 'enumerable') && desc.enumerable !== undefined) {
    if (desc.enumerable !== originalDesc.enumerable ||
        desc.enumerable !== isEnumerable(obj, name)) {
      __push(failures, label + " descriptor should " + (desc.enumerable ? '' : 'not ') + "be enumerable");
    }
  }

  // Operations past this point are potentially destructive!

  if (__hasOwnProperty(desc, 'writable') && desc.writable !== undefined) {
    if (desc.writable !== originalDesc.writable ||
        desc.writable !== isWritable(obj, name)) {
      __push(failures, label + " descriptor should " + (desc.writable ? '' : 'not ') + "be writable");
    }
  }

  if (__hasOwnProperty(desc, 'configurable') && desc.configurable !== undefined) {
    if (desc.configurable !== originalDesc.configurable ||
        desc.configurable !== isConfigurable(obj, name, originalDesc)) {
      __push(failures, label + " descriptor should " + (desc.configurable ? '' : 'not ') + "be configurable");
    }
  }

  if (failures.length) {
    assert(false, __join(failures, '; '));
  }

  if (options && options.restore) {
    __defineProperty(obj, name, originalDesc);
  }

  return true;
}

function _toNumber(value) {
  // https://tc39.es/ecma262/multipage/numbers-and-dates.html#sec-math.max
  return __max(value);
}

// https://tc39.es/ecma262/multipage/abstract-operations.html#sec-canonicalnumericindexstring
function _isCanonicalNumericIndexString(value) {
  if (typeof value !== "string") return false;
  if (value === "-0") return true;
  var n = _toNumber(value);
  return __String(n) === value;
}

function _isTypedArray(value) {
  try {
    // https://tc39.es/ecma262/multipage/indexed-collections.html#sec-get-%typedarray%.prototype.bytelength
    __getTypedArrayByteLength(value);
    return true;
  } catch (_err) {
    return false;
  }
}

function isConfigurable(obj, name, originalDesc) {
  try {
    delete obj[name];
  } catch (e) {
    if (!(e instanceof TypeError)) {
      throw new Test262Error("Expected TypeError, got " + e);
    }
  }
  var deleted = !__hasOwnProperty(obj, name);

  // TypedArray canonical numeric string properties are never *actually* deleted,
  // so we skip post-hoc verification for them
  // https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-typedarray-delete
  if (_isCanonicalNumericIndexString(name) && _isTypedArray(obj) && originalDesc) {
    if (deleted) throw new Test262Error("Expected TypedArray index deletion to be ignored");
    return originalDesc.configurable;
  }

  return deleted;
}

function isEnumerable(obj, name) {
  var stringCheck = false;

  if (typeof name === "string") {
    for (var x in obj) {
      if (x === name) {
        stringCheck = true;
        break;
      }
    }
  } else {
    // skip it if name is not string, works for Symbol names.
    stringCheck = true;
  }

  return stringCheck && __hasOwnProperty(obj, name) && __propertyIsEnumerable(obj, name);
}

function isSameValue(a, b) {
  if (a === 0 && b === 0) return 1 / a === 1 / b;
  if (a !== a && b !== b) return true;

  return a === b;
}

function isWritable(obj, name, verifyProp, value) {
  var hadValue = __hasOwnProperty(obj, name);
  var oldValue = obj[name];
  var newValue = value;
  if (arguments.length < 4) {
    // Initialize newValue, preserving a numeric type from oldValue with a
    // replacement that moves towards zero (to support the special behaviour of
    // Array and TypedArray "length" and index properties).
    switch (typeof oldValue) {
      case "number":
        // As a special case, never decrease array length (which would delete
        // properties not covered by the new length).
        if (oldValue <= 0 || __isArray(obj) && name === "length") {
          newValue = oldValue === -INFINITY ? 0 : oldValue + 1;
        } else if (oldValue < INFINITY) {
          newValue = oldValue - 1;
        } else {
          newValue = 0;
        }
        break;
      case "bigint":
        newValue = oldValue <= BigInt(0) ? oldValue + BigInt(1) : oldValue - BigInt(1);
        break;
      default:
        newValue = oldValue !== "unlikelyValue" ? "unlikelyValue" : "unlikelyValue2";
        break;
    }
  }
  var writeSucceeded;

  try {
    obj[name] = newValue;
  } catch (e) {
    if (!(e instanceof TypeError)) {
      throw new Test262Error("Expected TypeError, got " + e);
    }
  }

  writeSucceeded = isSameValue(obj[verifyProp || name], newValue);

  // Revert the change only if it was successful (in other cases, reverting
  // is unnecessary and may trigger exceptions for certain property
  // configurations)
  if (writeSucceeded) {
    if (hadValue) {
      obj[name] = oldValue;
    } else {
      delete obj[name];
    }
  }

  return writeSucceeded;
}

/**
 * Verify that there is a function of specified name, length, and containing
 * descriptor associated with `obj[name]` and following the conventions for
 * built-in objects.
 *
 * @param {object} obj
 * @param {string|symbol} name
 * @param {string} [functionName] defaults to name for strings, `[${name.description}]` for symbols
 * @param {number} functionLength
 * @param {PropertyDescriptor} [desc] defaults to data property conventions (writable, non-enumerable, configurable)
 * @param {object} [options]
 * @param {boolean} [options.label]
 * @param {typeof verifyProperty} [options.verifyProperty]
 * @param {boolean} [options.restore] revert mutations from verifying writable/configurable
 */
function verifyCallableProperty(obj, name, functionName, functionLength, desc, options) {
  var label = options && options.label || __String(name);
  var propertyVerifier = options && options.verifyProperty || verifyProperty;

  var value = obj && obj[name];

  assert.sameValue(typeof value, "function", label + " should be a function");

  // Every other data property described in clauses 19 through 28 and in
  // Annex B.2 has the attributes { [[Writable]]: true, [[Enumerable]]: false,
  // [[Configurable]]: true } unless otherwise specified.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html
  if (desc === undefined) {
    desc = {
      writable: true,
      enumerable: false,
      configurable: true,
      value: value
    };
  } else if (!__hasOwnProperty(desc, "value") && !__hasOwnProperty(desc, "get")) {
    desc.value = value;
  }

  propertyVerifier(obj, name, desc, options);

  if (functionName === undefined) {
    if (typeof name === "symbol") {
      functionName = "[" + name.description + "]";
    } else {
      functionName = name;
    }
  }
  // Unless otherwise specified, the "name" property of a built-in function
  // object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
  // [[Configurable]]: true }.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html#sec-ecmascript-standard-built-in-objects
  // https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-setfunctionname
  propertyVerifier(value, "name", {
    value: functionName,
    writable: false,
    enumerable: false,
    configurable: desc.configurable
  }, { label: label + " name", restore: options && options.restore });

  // Unless otherwise specified, the "length" property of a built-in function
  // object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
  // [[Configurable]]: true }.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html#sec-ecmascript-standard-built-in-objects
  // https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-setfunctionlength
  propertyVerifier(value, "length", {
    value: functionLength,
    writable: false,
    enumerable: false,
    configurable: desc.configurable
  }, { label: label + " length", restore: options && options.restore });
}

/**
 * Verify that there is an accessor property associated with `obj[name]` and
 * following the conventions for built-in objects.
 *
 * @param {object} obj
 * @param {string|symbol} name
 * @param {object} desc
 * @param {boolean} [desc.enumerable] defaults to accessor property conventions (non-enumerable)
 * @param {boolean} [desc.configurable] defaults to accessor property conventions (configurable)
 * @param {undefined | Function | {name?: string|symbol, length?: number}} [desc.get] if an object,
 *   absent fields default to getter conventions (name derived from the property key with a "get "
 *   prefix, length 0)
 * @param {undefined | Function | {name?: string|symbol, length?: number}} [desc.set] if an object,
 *   absent fields default to getter conventions (name derived from the property key with a "set "
 *   prefix, length 1)
 * @param {object} [options]
 * @param {boolean} [options.label]
 * @param {typeof verifyProperty} [options.verifyProperty]
 * @param {typeof verifyCallableProperty} [options.verifyCallableProperty]
 * @param {boolean} [options.restore] revert mutations from verifying property attributes
 */
function verifyAccessorProperty(obj, name, desc, options) {
  var checkGet = __hasOwnProperty(desc, "get");
  var checkSet = __hasOwnProperty(desc, "set");
  assert(
    checkGet || checkSet,
    'verifyAccessorProperty requires at least one of "get" and "set"'
  );
  var label = options && options.label || __String(name);
  var propertyVerifier = options && options.verifyProperty || verifyProperty;
  var callabilityVerifier = options && options.verifyCallableProperty || verifyCallableProperty;

  var originalDesc = __getOwnPropertyDescriptor(obj, name);

  // Every built-in function object, including constructors, has a "name"
  // property whose value is a String. Unless otherwise specified, this value is
  // the name that is given to the function in this specification. Functions
  // that are identified as anonymous functions use the empty String as the
  // value of the "name" property. For functions that are specified as
  // properties of objects, the name value is the property name string used to
  // access the function. Functions that are specified as get or set accessor
  // functions of built-in properties have "get" or "set" (respectively) passed
  // to the prefix parameter when calling CreateBuiltinFunction.
  //
  // The value of the "name" property is explicitly specified for each built-in
  // functions whose property key is a Symbol value. If such an explicitly
  // specified value starts with the prefix "get " or "set " and the function
  // for which it is specified is a get or set accessor function of a built-in
  // property, the value without the prefix is passed to the name parameter, and
  // the value "get" or "set" (respectively) is passed to the prefix parameter
  // when calling CreateBuiltinFunction.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html
  if (checkGet) {
    var expectGetter = desc.get;
    var getterLabel = label + " getter";
    if (expectGetter === undefined || typeof expectGetter === "function") {
      assert.sameValue(originalDesc.get, expectGetter, getterLabel);
    } else {
      var getterName = expectGetter.name;
      if (getterName === undefined) {
        getterName = "get " + (typeof name === "symbol" ? "[" + name.description + "]" : name);
      }
      var getterLength = expectGetter.length !== undefined ? expectGetter.length : 0;
      var getterOptions = { label: getterLabel };
      callabilityVerifier(originalDesc, "get", getterName, getterLength, {}, getterOptions);
    }
  }
  if (checkSet) {
    var expectSetter = desc.set;
    var setterLabel = label + " setter";
    if (expectSetter === undefined || typeof expectSetter === "function") {
      assert.sameValue(originalDesc.set, expectSetter, setterLabel);
    } else {
      var setterName = expectSetter.name;
      if (setterName === undefined) {
        setterName = "set " + (typeof name === "symbol" ? "[" + name.description + "]" : name);
      }
      var setterLength = expectSetter.length !== undefined ? expectSetter.length : 1;
      var setterOptions = { label: setterLabel };
      callabilityVerifier(originalDesc, "set", setterName, setterLength, {}, setterOptions);
    }
  }

  // Every accessor property described in clauses 19 through 28 and in Annex B.2
  // has the attributes { [[Enumerable]]: false, [[Configurable]]: true } unless
  // otherwise specified.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html
  var resolvedDesc = { get: originalDesc.get, set: originalDesc.set };
  if (!__hasOwnProperty(desc, "enumerable")) {
    resolvedDesc.enumerable = false;
  } else if (desc.enumerable !== undefined) {
    resolvedDesc.enumerable = desc.enumerable;
  }
  if (!__hasOwnProperty(desc, "configurable")) {
    resolvedDesc.configurable = true;
  } else if (desc.configurable !== undefined) {
    resolvedDesc.configurable = desc.configurable;
  }
  propertyVerifier(obj, name, resolvedDesc, options);
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyEqualTo(obj, name, value) {
  if (!isSameValue(obj[name], value)) {
    throw new Test262Error("Expected obj[" + __String(name) + "] to equal " + value +
           ", actually " + obj[name]);
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyWritable(obj, name, verifyProp, value) {
  if (!verifyProp) {
    assert(__getOwnPropertyDescriptor(obj, name).writable,
         "Expected obj[" + __String(name) + "] to have writable:true.");
  }
  var result = arguments.length < 4
    ? isWritable(obj, name, verifyProp)
    : isWritable(obj, name, verifyProp, value);
  if (!result) {
    throw new Test262Error("Expected obj[" + __String(name) + "] to be writable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotWritable(obj, name, verifyProp, value) {
  if (!verifyProp) {
    assert(!__getOwnPropertyDescriptor(obj, name).writable,
         "Expected obj[" + __String(name) + "] to have writable:false.");
  }
  var result = arguments.length < 4
    ? isWritable(obj, name, verifyProp)
    : isWritable(obj, name, verifyProp, value);
  if (result) {
    throw new Test262Error("Expected obj[" + __String(name) + "] NOT to be writable, but was.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyEnumerable(obj, name) {
  assert(__getOwnPropertyDescriptor(obj, name).enumerable,
       "Expected obj[" + __String(name) + "] to have enumerable:true.");
  if (!isEnumerable(obj, name)) {
    throw new Test262Error("Expected obj[" + __String(name) + "] to be enumerable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotEnumerable(obj, name) {
  assert(!__getOwnPropertyDescriptor(obj, name).enumerable,
       "Expected obj[" + __String(name) + "] to have enumerable:false.");
  if (isEnumerable(obj, name)) {
    throw new Test262Error("Expected obj[" + __String(name) + "] NOT to be enumerable, but was.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyConfigurable(obj, name) {
  assert(__getOwnPropertyDescriptor(obj, name).configurable,
       "Expected obj[" + __String(name) + "] to have configurable:true.");
  if (!isConfigurable(obj, name)) {
    throw new Test262Error("Expected obj[" + __String(name) + "] to be configurable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotConfigurable(obj, name) {
  assert(!__getOwnPropertyDescriptor(obj, name).configurable,
       "Expected obj[" + __String(name) + "] to have configurable:false.");
  if (isConfigurable(obj, name)) {
    throw new Test262Error("Expected obj[" + __String(name) + "] NOT to be configurable, but was.");
  }
}

/**
 * Use this function to verify the properties of a primordial object.
 * For non-primordial objects, use verifyProperty.
 * See: https://github.com/tc39/how-we-work/blob/main/terminology.md#primordial
 */
var verifyPrimordialProperty = verifyProperty;

/**
 * Use this function to verify the primordial function-valued properties.
 * For non-primordial functions, use verifyCallableProperty.
 * See: https://github.com/tc39/how-we-work/blob/main/terminology.md#primordial
 *
 * @type {typeof verifyCallableProperty}
 */
function verifyPrimordialCallableProperty(obj, name, functionName, functionLength, desc, options) {
  var resolvedOptions = {
    verifyProperty: options && options.verifyProperty !== undefined
      ? options.verifyProperty
      : verifyPrimordialProperty
  };
  if (options && options.label !== undefined) resolvedOptions.label = options.label;
  if (options && options.restore !== undefined) resolvedOptions.restore = options.restore;

  return verifyCallableProperty(obj, name, functionName, functionLength, desc, resolvedOptions);
}

/**
 * Use this function to verify the primordial accessor properties.
 * For non-primordial functions, use verifyAccessorProperty.
 * See: https://github.com/tc39/how-we-work/blob/main/terminology.md#primordial
 *
 * @type {typeof verifyAccessorProperty}
 */
function verifyPrimordialAccessorProperty(obj, name, desc, options) {
  var resolvedOptions = {
    verifyProperty: options && options.verifyProperty !== undefined
      ? options.verifyProperty
      : verifyPrimordialProperty,
    verifyCallableProperty: options && options.verifyCallableProperty !== undefined
      ? options.verifyCallableProperty
      : verifyPrimordialCallableProperty
  };
  if (options && options.label !== undefined) resolvedOptions.label = options.label;
  if (options && options.restore !== undefined) resolvedOptions.restore = options.restore;

  return verifyAccessorProperty(obj, name, desc, resolvedOptions);
}

// file: sta.js
// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provides both:

    - An error class to avoid false positives when testing for thrown exceptions
    - A function to explicitly throw an exception using the Test262Error class
defines: [Test262Error, $DONOTEVALUATE]
---*/


function Test262Error(message) {
  if (!(this instanceof Test262Error)) return new Test262Error(message);
  this.message = message || "";
}

Test262Error.prototype.toString = function () {
  return "Test262Error: " + this.message;
};

Test262Error.thrower = function (message) {
  throw new Test262Error(message);
};

function $DONOTEVALUATE() {
  throw "Test262: This statement should not be evaluated.";
}

// file: test262-host.js
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// https://github.com/tc39/test262/blob/main/INTERPRETING.md#host-defined-functions
;(function createHostObject(global) {
    "use strict";

    // Save built-in functions and constructors.
    var FunctionToString = global.Function.prototype.toString;
    var ReflectApply = global.Reflect.apply;
    var Atomics = global.Atomics;
    var Error = global.Error;
    var SharedArrayBuffer = global.SharedArrayBuffer;
    var Int32Array = global.Int32Array;

    // Save built-in shell functions.
    var NewGlobal = global.newGlobal;
    var setSharedArrayBuffer = global.setSharedArrayBuffer;
    var getSharedArrayBuffer = global.getSharedArrayBuffer;
    var evalInWorker = global.evalInWorker;
    var monotonicNow = global.monotonicNow;
    var gc = global.gc;
    var clearKeptObjects = global.clearKeptObjects;

    var hasCreateIsHTMLDDA = "createIsHTMLDDA" in global;
    var hasThreads = ("helperThreadCount" in global ? global.helperThreadCount() > 0 : true);
    var hasMailbox = typeof setSharedArrayBuffer === "function" && typeof getSharedArrayBuffer === "function";
    var hasEvalInWorker = typeof evalInWorker === "function";

    if (!hasCreateIsHTMLDDA && !("document" in global && "all" in global.document))
        throw new Error("no [[IsHTMLDDA]] object available for testing");

    var IsHTMLDDA = hasCreateIsHTMLDDA
                    ? global.createIsHTMLDDA()
                    : global.document.all;

    // The $262.agent framework is not appropriate for browsers yet, and some
    // test cases can't work in browsers (they block the main thread).

    var shellCode = hasMailbox && hasEvalInWorker;
    var sabTestable = Atomics && SharedArrayBuffer && hasThreads && shellCode;

    global.$262 = {
        __proto__: null,
        createRealm() {
            var newGlobalObject = NewGlobal();
            var createHostObjectFn = ReflectApply(FunctionToString, createHostObject, []);
            newGlobalObject.Function(`${createHostObjectFn} createHostObject(this);`)();
            return newGlobalObject.$262;
        },
        detachArrayBuffer: global.detachArrayBuffer,
        evalScript: global.evaluateScript || global.evaluate,
        global,
        IsHTMLDDA,
        AbstractModuleSource: global.getAbstractModuleSource?.(),
        gc() {
            gc();
        },
        clearKeptObjects() {
            clearKeptObjects();
        },
        agent: (function () {

            // SpiderMonkey complication: With run-time argument --no-threads
            // our test runner will not properly filter test cases that can't be
            // run because agents can't be started, and so we do a little
            // filtering here: We will quietly succeed and exit if an agent test
            // should not have been run because threads cannot be started.
            //
            // Firefox complication: The test cases that use $262.agent can't
            // currently work in the browser, so for now we rely on them not
            // being run at all.

            if (!sabTestable) {
                let {reportCompare, quit} = global;

                function notAvailable() {
                    // See comment above.
                    if (!hasThreads && shellCode) {
                        reportCompare(0, 0);
                        quit(0);
                    }
                    throw new Error("Agents not available");
                }

                return {
                    start(script) { notAvailable() },
                    broadcast(sab, id) { notAvailable() },
                    getReport() { notAvailable() },
                    sleep(s) { notAvailable() },
                    monotonicNow,
                }
            }

            // The SpiderMonkey implementation uses a designated shared buffer _ia
            // for coordination, and spinlocks for everything except sleeping.

            var _MSG_LOC = 0;           // Low bit set: broadcast available; High bits: seq #
            var _ID_LOC = 1;            // ID sent with broadcast
            var _ACK_LOC = 2;           // Worker increments this to ack that broadcast was received
            var _RDY_LOC = 3;           // Worker increments this to ack that worker is up and running
            var _LOCKTXT_LOC = 4;       // Writer lock for the text buffer: 0=open, 1=closed
            var _NUMTXT_LOC = 5;        // Count of messages in text buffer
            var _NEXT_LOC = 6;          // First free location in the buffer
            var _SLEEP_LOC = 7;         // Used for sleeping

            var _FIRST = 10;            // First location of first message

            var _ia = new Int32Array(new SharedArrayBuffer(65536));
            _ia[_NEXT_LOC] = _FIRST;

            var _worker_prefix =
// BEGIN WORKER PREFIX
`if (typeof $262 === 'undefined')
    $262 = {};
$262.agent = (function (global) {
    var ReflectApply = global.Reflect.apply;
    var StringCharCodeAt = global.String.prototype.charCodeAt;
    var {
        add: Atomics_add,
        compareExchange: Atomics_compareExchange,
        load: Atomics_load,
        store: Atomics_store,
        wait: Atomics_wait,
    } = global.Atomics;

    var {getSharedArrayBuffer} = global;

    var _finished = { done: false };

    var _ia = new Int32Array(getSharedArrayBuffer());
    var agent = {
        receiveBroadcast(receiver) {
            var k;
            while (((k = Atomics_load(_ia, ${_MSG_LOC})) & 1) === 0)
                ;
            var received_sab = getSharedArrayBuffer();
            var received_id = Atomics_load(_ia, ${_ID_LOC});
            Atomics_add(_ia, ${_ACK_LOC}, 1);
            while (Atomics_load(_ia, ${_MSG_LOC}) === k)
                ;
            receiver(received_sab, received_id);
            waitForDone(_finished);
        },

        report(msg) {
            while (Atomics_compareExchange(_ia, ${_LOCKTXT_LOC}, 0, 1) === 1)
                ;
            msg = "" + msg;
            var i = _ia[${_NEXT_LOC}];
            _ia[i++] = msg.length;
            for ( let j=0 ; j < msg.length ; j++ )
                _ia[i++] = ReflectApply(StringCharCodeAt, msg, [j]);
            _ia[${_NEXT_LOC}] = i;
            Atomics_add(_ia, ${_NUMTXT_LOC}, 1);
            Atomics_store(_ia, ${_LOCKTXT_LOC}, 0);
        },

        sleep(s) {
            Atomics_wait(_ia, ${_SLEEP_LOC}, 0, s);
        },

        leaving() {
            _finished.done = true;
        },

        monotonicNow: global.monotonicNow,
    };
    Atomics_add(_ia, ${_RDY_LOC}, 1);
    return agent;
})(this);`;
// END WORKER PREFIX

            var _numWorkers = 0;
            var _numReports = 0;
            var _reportPtr = _FIRST;
            var {
                add: Atomics_add,
                load: Atomics_load,
                store: Atomics_store,
                wait: Atomics_wait,
            } = Atomics;
            var StringFromCharCode = global.String.fromCharCode;

            return {
                start(script) {
                    setSharedArrayBuffer(_ia.buffer);
                    var oldrdy = Atomics_load(_ia, _RDY_LOC);
                    evalInWorker(_worker_prefix + script);
                    while (Atomics_load(_ia, _RDY_LOC) === oldrdy)
                        ;
                    _numWorkers++;
                },

                broadcast(sab, id) {
                    setSharedArrayBuffer(sab);
                    Atomics_store(_ia, _ID_LOC, id);
                    Atomics_store(_ia, _ACK_LOC, 0);
                    Atomics_add(_ia, _MSG_LOC, 1);
                    while (Atomics_load(_ia, _ACK_LOC) < _numWorkers)
                        ;
                    Atomics_add(_ia, _MSG_LOC, 1);
                },

                getReport() {
                    if (_numReports === Atomics_load(_ia, _NUMTXT_LOC))
                        return null;
                    var s = "";
                    var i = _reportPtr;
                    var len = _ia[i++];
                    for ( let j=0 ; j < len ; j++ )
                        s += StringFromCharCode(_ia[i++]);
                    _reportPtr = i;
                    _numReports++;
                    return s;
                },

                sleep(s) {
                    Atomics_wait(_ia, _SLEEP_LOC, 0, s);
                },

                monotonicNow,
            };
        })()
    };
})(this);

var $mozAsyncTestDone = false;
function $DONE(failure) {
    // This function is generally called from within a Promise handler, so any
    // exception thrown by this method will be swallowed and most likely
    // ignored by the Promise machinery.
    if ($mozAsyncTestDone) {
        reportFailure("$DONE() already called");
        return;
    }
    $mozAsyncTestDone = true;

    if (failure)
        reportFailure(failure);
    else
        reportCompare(0, 0);

    if (typeof jsTestDriverEnd === "function") {
        gDelayTestDriverEnd = false;
        jsTestDriverEnd();
    }
}

// Some tests in test262 leave promise rejections unhandled.
if ("ignoreUnhandledRejections" in this) {
  ignoreUnhandledRejections();
}
