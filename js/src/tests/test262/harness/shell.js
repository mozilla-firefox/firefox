// GENERATED, DO NOT EDIT
// file: assertRelativeDateMs.js
// Copyright (C) 2015 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Verify that the given date object's Number representation describes the
    correct number of milliseconds since the Unix epoch relative to the local
    time zone (as interpreted at the specified date).
defines: [assertRelativeDateMs]
---*/

/**
 * @param {Date} date
 * @param {Number} expectedMs
 */
function assertRelativeDateMs(date, expectedMs) {
  var actualMs = date.valueOf();
  var localOffset = date.getTimezoneOffset() * 60000;

  if (actualMs - localOffset !== expectedMs) {
    throw new Test262Error(
      'Expected ' + date + ' to be ' + expectedMs +
      ' milliseconds from the Unix epoch'
    );
  }
}

// file: asyncHelpers.js
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    A collection of assertion and wrapper functions for testing asynchronous built-ins.
defines: [asyncTest, assert.throwsAsync]
---*/

/**
 * Defines the **sole** asynchronous test of a file.
 * @see {@link ../rfcs/async-helpers.md} for background.
 *
 * @param {Function} testFunc a callback whose returned promise indicates test results
 *   (fulfillment for success, rejection for failure)
 * @returns {void}
 */
function asyncTest(testFunc) {
  if (!Object.prototype.hasOwnProperty.call(globalThis, "$DONE")) {
    throw new Test262Error("asyncTest called without async flag");
  }
  if (typeof testFunc !== "function") {
    $DONE(new Test262Error("asyncTest called with non-function argument"));
    return;
  }
  try {
    testFunc().then(
      function () {
        $DONE();
      },
      function (error) {
        $DONE(error);
      }
    );
  } catch (syncError) {
    $DONE(syncError);
  }
}

/**
 * Asserts that a callback asynchronously throws an instance of a particular
 * error (i.e., returns a promise whose rejection value is an object referencing
 * the constructor).
 *
 * @param {Function} expectedErrorConstructor the expected constructor of the
 *   rejection value
 * @param {Function} func the callback
 * @param {string} [message] the prefix to use for failure messages
 * @returns {Promise<void>} fulfills if the expected error is thrown,
 *   otherwise rejects
 */
assert.throwsAsync = function (expectedErrorConstructor, func, message) {
  return new Promise(function (resolve) {
    var fail = function (detail) {
      if (message === undefined) {
        throw new Test262Error(detail);
      }
      throw new Test262Error(message + " " + detail);
    };
    if (typeof expectedErrorConstructor !== "function") {
      fail("assert.throwsAsync called with an argument that is not an error constructor");
    }
    if (typeof func !== "function") {
      fail("assert.throwsAsync called with an argument that is not a function");
    }
    var expectedName = expectedErrorConstructor.name;
    var expectation = "Expected a " + expectedName + " to be thrown asynchronously";
    var res;
    try {
      res = func();
    } catch (thrown) {
      fail(expectation + " but the function threw synchronously");
    }
    if (res === null || typeof res !== "object" || typeof res.then !== "function") {
      fail(expectation + " but result was not a thenable");
    }
    var onResFulfilled, onResRejected;
    var resSettlementP = new Promise(function (onFulfilled, onRejected) {
      onResFulfilled = onFulfilled;
      onResRejected = onRejected;
    });
    try {
      res.then(onResFulfilled, onResRejected)
    } catch (thrown) {
      fail(expectation + " but .then threw synchronously");
    }
    resolve(resSettlementP.then(
      function () {
        fail(expectation + " but no exception was thrown at all");
      },
      function (thrown) {
        var actualName;
        if (thrown === null || typeof thrown !== "object") {
          fail(expectation + " but thrown value was not an object");
        } else if (thrown.constructor !== expectedErrorConstructor) {
          actualName = thrown.constructor.name;
          if (expectedName === actualName) {
            fail(expectation +
              " but got a different error constructor with the same name");
          }
          fail(expectation + " but got a " + actualName);
        }
      }
    ));
  });
};

// file: byteConversionValues.js
// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provide a list for original and expected values for different byte
    conversions.
    This helper is mostly used on tests for TypedArray and DataView, and each
    array from the expected values must match the original values array on every
    index containing its original value.
defines: [byteConversionValues]
---*/
var byteConversionValues = {
  values: [
    127,         // 2 ** 7 - 1
    128,         // 2 ** 7
    32767,       // 2 ** 15 - 1
    32768,       // 2 ** 15
    2147483647,  // 2 ** 31 - 1
    2147483648,  // 2 ** 31
    255,         // 2 ** 8 - 1
    256,         // 2 ** 8
    65535,       // 2 ** 16 - 1
    65536,       // 2 ** 16
    4294967295,  // 2 ** 32 - 1
    4294967296,  // 2 ** 32
    9007199254740991, // 2 ** 53 - 1
    9007199254740992, // 2 ** 53
    1.1,
    0.1,
    0.5,
    0.50000001,
    0.6,
    0.7,
    undefined,
    -1,
    -0,
    -0.1,
    -1.1,
    NaN,
    -127,        // - ( 2 ** 7 - 1 )
    -128,        // - ( 2 ** 7 )
    -32767,      // - ( 2 ** 15 - 1 )
    -32768,      // - ( 2 ** 15 )
    -2147483647, // - ( 2 ** 31 - 1 )
    -2147483648, // - ( 2 ** 31 )
    -255,        // - ( 2 ** 8 - 1 )
    -256,        // - ( 2 ** 8 )
    -65535,      // - ( 2 ** 16 - 1 )
    -65536,      // - ( 2 ** 16 )
    -4294967295, // - ( 2 ** 32 - 1 )
    -4294967296, // - ( 2 ** 32 )
    Infinity,
    -Infinity,
    0,
    2049,                         // an integer which rounds down under ties-to-even when cast to float16
    2051,                         // an integer which rounds up under ties-to-even when cast to float16
    0.00006103515625,             // smallest normal float16
    0.00006097555160522461,       // largest subnormal float16
    5.960464477539063e-8,         // smallest float16
    2.9802322387695312e-8,        // largest double which rounds to 0 when cast to float16
    2.980232238769532e-8,         // smallest double which does not round to 0 when cast to float16
    8.940696716308594e-8,         // a double which rounds up to a subnormal under ties-to-even when cast to float16
    1.4901161193847656e-7,        // a double which rounds down to a subnormal under ties-to-even when cast to float16
    1.490116119384766e-7,         // the next double above the one on the previous line one
    65504,                        // max finite float16
    65520,                        // smallest double which rounds to infinity when cast to float16
    65519.99999999999,            // largest double which does not round to infinity when cast to float16
    0.000061005353927612305,      // smallest double which rounds to a non-subnormal when cast to float16
    0.0000610053539276123         // largest double which rounds to a subnormal when cast to float16
  ],

  expected: {
    Int8: [
      127,  // 127
      -128, // 128
      -1,   // 32767
      0,    // 32768
      -1,   // 2147483647
      0,    // 2147483648
      -1,   // 255
      0,    // 256
      -1,   // 65535
      0,    // 65536
      -1,   // 4294967295
      0,    // 4294967296
      -1,   // 9007199254740991
      0,    // 9007199254740992
      1,    // 1.1
      0,    // 0.1
      0,    // 0.5
      0,    // 0.50000001,
      0,    // 0.6
      0,    // 0.7
      0,    // undefined
      -1,   // -1
      0,    // -0
      0,    // -0.1
      -1,   // -1.1
      0,    // NaN
      -127, // -127
      -128, // -128
      1,    // -32767
      0,    // -32768
      1,    // -2147483647
      0,    // -2147483648
      1,    // -255
      0,    // -256
      1,    // -65535
      0,    // -65536
      1,    // -4294967295
      0,    // -4294967296
      0,    // Infinity
      0,    // -Infinity
      0,    // 0
      1,    // 2049
      3,    // 2051
      0,    // 0.00006103515625
      0,    // 0.00006097555160522461
      0,    // 5.960464477539063e-8
      0,    // 2.9802322387695312e-8
      0,    // 2.980232238769532e-8
      0,    // 8.940696716308594e-8
      0,    // 1.4901161193847656e-7
      0,    // 1.490116119384766e-7
      -32,  // 65504
      -16,  // 65520
      -17,  // 65519.99999999999
      0,    // 0.000061005353927612305
      0     // 0.0000610053539276123
    ],
    Uint8: [
      127, // 127
      128, // 128
      255, // 32767
      0,   // 32768
      255, // 2147483647
      0,   // 2147483648
      255, // 255
      0,   // 256
      255, // 65535
      0,   // 65536
      255, // 4294967295
      0,   // 4294967296
      255, // 9007199254740991
      0,   // 9007199254740992
      1,   // 1.1
      0,   // 0.1
      0,   // 0.5
      0,   // 0.50000001,
      0,   // 0.6
      0,   // 0.7
      0,   // undefined
      255, // -1
      0,   // -0
      0,   // -0.1
      255, // -1.1
      0,   // NaN
      129, // -127
      128, // -128
      1,   // -32767
      0,   // -32768
      1,   // -2147483647
      0,   // -2147483648
      1,   // -255
      0,   // -256
      1,   // -65535
      0,   // -65536
      1,   // -4294967295
      0,   // -4294967296
      0,   // Infinity
      0,   // -Infinity
      0,   // 0
      1,   // 2049
      3,   // 2051
      0,   // 0.00006103515625
      0,   // 0.00006097555160522461
      0,   // 5.960464477539063e-8
      0,   // 2.9802322387695312e-8
      0,   // 2.980232238769532e-8
      0,   // 8.940696716308594e-8
      0,   // 1.4901161193847656e-7
      0,   // 1.490116119384766e-7
      224, // 65504
      240, // 65520
      239, // 65519.99999999999
      0,   // 0.000061005353927612305
      0    // 0.0000610053539276123
    ],
    Uint8Clamped: [
      127, // 127
      128, // 128
      255, // 32767
      255, // 32768
      255, // 2147483647
      255, // 2147483648
      255, // 255
      255, // 256
      255, // 65535
      255, // 65536
      255, // 4294967295
      255, // 4294967296
      255, // 9007199254740991
      255, // 9007199254740992
      1,   // 1.1,
      0,   // 0.1
      0,   // 0.5
      1,   // 0.50000001,
      1,   // 0.6
      1,   // 0.7
      0,   // undefined
      0,   // -1
      0,   // -0
      0,   // -0.1
      0,   // -1.1
      0,   // NaN
      0,   // -127
      0,   // -128
      0,   // -32767
      0,   // -32768
      0,   // -2147483647
      0,   // -2147483648
      0,   // -255
      0,   // -256
      0,   // -65535
      0,   // -65536
      0,   // -4294967295
      0,   // -4294967296
      255, // Infinity
      0,   // -Infinity
      0,   // 0
      255, // 2049
      255, // 2051
      0,   // 0.00006103515625
      0,   // 0.00006097555160522461
      0,   // 5.960464477539063e-8
      0,   // 2.9802322387695312e-8
      0,   // 2.980232238769532e-8
      0,   // 8.940696716308594e-8
      0,   // 1.4901161193847656e-7
      0,   // 1.490116119384766e-7
      255, // 65504
      255, // 65520
      255, // 65519.99999999999
      0,   // 0.000061005353927612305
      0    // 0.0000610053539276123
    ],
    Int16: [
      127,    // 127
      128,    // 128
      32767,  // 32767
      -32768, // 32768
      -1,     // 2147483647
      0,      // 2147483648
      255,    // 255
      256,    // 256
      -1,     // 65535
      0,      // 65536
      -1,     // 4294967295
      0,      // 4294967296
      -1,     // 9007199254740991
      0,      // 9007199254740992
      1,      // 1.1
      0,      // 0.1
      0,      // 0.5
      0,      // 0.50000001,
      0,      // 0.6
      0,      // 0.7
      0,      // undefined
      -1,     // -1
      0,      // -0
      0,      // -0.1
      -1,     // -1.1
      0,      // NaN
      -127,   // -127
      -128,   // -128
      -32767, // -32767
      -32768, // -32768
      1,      // -2147483647
      0,      // -2147483648
      -255,   // -255
      -256,   // -256
      1,      // -65535
      0,      // -65536
      1,      // -4294967295
      0,      // -4294967296
      0,      // Infinity
      0,      // -Infinity
      0,      // 0
      2049,   // 2049
      2051,   // 2051
      0,      // 0.00006103515625
      0,      // 0.00006097555160522461
      0,      // 5.960464477539063e-8
      0,      // 2.9802322387695312e-8
      0,      // 2.980232238769532e-8
      0,      // 8.940696716308594e-8
      0,      // 1.4901161193847656e-7
      0,      // 1.490116119384766e-7
      -32,    // 65504
      -16,    // 65520
      -17,    // 65519.99999999999
      0,      // 0.000061005353927612305
      0       // 0.0000610053539276123
    ],
    Uint16: [
      127,   // 127
      128,   // 128
      32767, // 32767
      32768, // 32768
      65535, // 2147483647
      0,     // 2147483648
      255,   // 255
      256,   // 256
      65535, // 65535
      0,     // 65536
      65535, // 4294967295
      0,     // 4294967296
      65535, // 9007199254740991
      0,     // 9007199254740992
      1,     // 1.1
      0,     // 0.1
      0,     // 0.5
      0,     // 0.50000001,
      0,     // 0.6
      0,     // 0.7
      0,     // undefined
      65535, // -1
      0,     // -0
      0,     // -0.1
      65535, // -1.1
      0,     // NaN
      65409, // -127
      65408, // -128
      32769, // -32767
      32768, // -32768
      1,     // -2147483647
      0,     // -2147483648
      65281, // -255
      65280, // -256
      1,     // -65535
      0,     // -65536
      1,     // -4294967295
      0,     // -4294967296
      0,     // Infinity
      0,     // -Infinity
      0,     // 0
      2049,  // 2049
      2051,  // 2051
      0,     // 0.00006103515625
      0,     // 0.00006097555160522461
      0,     // 5.960464477539063e-8
      0,     // 2.9802322387695312e-8
      0,     // 2.980232238769532e-8
      0,     // 8.940696716308594e-8
      0,     // 1.4901161193847656e-7
      0,     // 1.490116119384766e-7
      65504, // 65504
      65520, // 65520
      65519, // 65519.99999999999
      0,     // 0.000061005353927612305
      0      // 0.0000610053539276123
    ],
    Int32: [
      127,         // 127
      128,         // 128
      32767,       // 32767
      32768,       // 32768
      2147483647,  // 2147483647
      -2147483648, // 2147483648
      255,         // 255
      256,         // 256
      65535,       // 65535
      65536,       // 65536
      -1,          // 4294967295
      0,           // 4294967296
      -1,          // 9007199254740991
      0,           // 9007199254740992
      1,           // 1.1
      0,           // 0.1
      0,           // 0.5
      0,           // 0.50000001,
      0,           // 0.6
      0,           // 0.7
      0,           // undefined
      -1,          // -1
      0,           // -0
      0,           // -0.1
      -1,          // -1.1
      0,           // NaN
      -127,        // -127
      -128,        // -128
      -32767,      // -32767
      -32768,      // -32768
      -2147483647, // -2147483647
      -2147483648, // -2147483648
      -255,        // -255
      -256,        // -256
      -65535,      // -65535
      -65536,      // -65536
      1,           // -4294967295
      0,           // -4294967296
      0,           // Infinity
      0,           // -Infinity
      0,           // 0
      2049,        // 2049
      2051,        // 2051
      0,           // 0.00006103515625
      0,           // 0.00006097555160522461
      0,           // 5.960464477539063e-8
      0,           // 2.9802322387695312e-8
      0,           // 2.980232238769532e-8
      0,           // 8.940696716308594e-8
      0,           // 1.4901161193847656e-7
      0,           // 1.490116119384766e-7
      65504,       // 65504
      65520,       // 65520
      65519,       // 65519.99999999999
      0,           // 0.000061005353927612305
      0            // 0.0000610053539276123
    ],
    Uint32: [
      127,        // 127
      128,        // 128
      32767,      // 32767
      32768,      // 32768
      2147483647, // 2147483647
      2147483648, // 2147483648
      255,        // 255
      256,        // 256
      65535,      // 65535
      65536,      // 65536
      4294967295, // 4294967295
      0,          // 4294967296
      4294967295, // 9007199254740991
      0,          // 9007199254740992
      1,          // 1.1
      0,          // 0.1
      0,          // 0.5
      0,          // 0.50000001,
      0,          // 0.6
      0,          // 0.7
      0,          // undefined
      4294967295, // -1
      0,          // -0
      0,          // -0.1
      4294967295, // -1.1
      0,          // NaN
      4294967169, // -127
      4294967168, // -128
      4294934529, // -32767
      4294934528, // -32768
      2147483649, // -2147483647
      2147483648, // -2147483648
      4294967041, // -255
      4294967040, // -256
      4294901761, // -65535
      4294901760, // -65536
      1,          // -4294967295
      0,          // -4294967296
      0,          // Infinity
      0,          // -Infinity
      0,          // 0
      2049,       // 2049
      2051,       // 2051
      0,          // 0.00006103515625
      0,          // 0.00006097555160522461
      0,          // 5.960464477539063e-8
      0,          // 2.9802322387695312e-8
      0,          // 2.980232238769532e-8
      0,          // 8.940696716308594e-8
      0,          // 1.4901161193847656e-7
      0,          // 1.490116119384766e-7
      65504,      // 65504
      65520,      // 65520
      65519,      // 65519.99999999999
      0,          // 0.000061005353927612305
      0           // 0.0000610053539276123
    ],
    Float16: [
      127,                    // 127
      128,                    // 128
      32768,                  // 32767
      32768,                  // 32768
      Infinity,               // 2147483647
      Infinity,               // 2147483648
      255,                    // 255
      256,                    // 256
      Infinity,               // 65535
      Infinity,               // 65536
      Infinity,               // 4294967295
      Infinity,               // 4294967296
      Infinity,               // 9007199254740991
      Infinity,               // 9007199254740992
      1.099609375,            // 1.1
      0.0999755859375,        // 0.1
      0.5,                    // 0.5
      0.5,                    // 0.50000001,
      0.60009765625,          // 0.6
      0.7001953125,           // 0.7
      NaN,                    // undefined
      -1,                     // -1
      -0,                     // -0
      -0.0999755859375,       // -0.1
      -1.099609375,           // -1.1
      NaN,                    // NaN
      -127,                   // -127
      -128,                   // -128
      -32768,                 // -32767
      -32768,                 // -32768
      -Infinity,              // -2147483647
      -Infinity,              // -2147483648
      -255,                   // -255
      -256,                   // -256
      -Infinity,              // -65535
      -Infinity,              // -65536
      -Infinity,              // -4294967295
      -Infinity,              // -4294967296
      Infinity,               // Infinity
      -Infinity,              // -Infinity
      0,                      // 0
      2048,                   // 2049
      2052,                   // 2051
      0.00006103515625,       // 0.00006103515625
      0.00006097555160522461, // 0.00006097555160522461
      5.960464477539063e-8,   // 5.960464477539063e-8
      0,                      // 2.9802322387695312e-8
      5.960464477539063e-8,   // 2.980232238769532e-8
      1.1920928955078125e-7,  // 8.940696716308594e-8
      1.1920928955078125e-7,  // 1.4901161193847656e-7
      1.7881393432617188e-7,  // 1.490116119384766e-7
      65504,                  // 65504
      Infinity,               // 65520
      65504,                  // 65519.99999999999
      0.00006103515625,       // 0.000061005353927612305
      0.00006097555160522461  // 0.0000610053539276123
    ],
    Float32: [
      127,                     // 127
      128,                     // 128
      32767,                   // 32767
      32768,                   // 32768
      2147483648,              // 2147483647
      2147483648,              // 2147483648
      255,                     // 255
      256,                     // 256
      65535,                   // 65535
      65536,                   // 65536
      4294967296,              // 4294967295
      4294967296,              // 4294967296
      9007199254740992,        // 9007199254740991
      9007199254740992,        // 9007199254740992
      1.100000023841858,       // 1.1
      0.10000000149011612,     // 0.1
      0.5,                     // 0.5
      0.5,                     // 0.50000001,
      0.6000000238418579,      // 0.6
      0.699999988079071,       // 0.7
      NaN,                     // undefined
      -1,                      // -1
      -0,                      // -0
      -0.10000000149011612,    // -0.1
      -1.100000023841858,      // -1.1
      NaN,                     // NaN
      -127,                    // -127
      -128,                    // -128
      -32767,                  // -32767
      -32768,                  // -32768
      -2147483648,             // -2147483647
      -2147483648,             // -2147483648
      -255,                    // -255
      -256,                    // -256
      -65535,                  // -65535
      -65536,                  // -65536
      -4294967296,             // -4294967295
      -4294967296,             // -4294967296
      Infinity,                // Infinity
      -Infinity,               // -Infinity
      0,                       // 0
      2049,                    // 2049
      2051,                    // 2051
      0.00006103515625,        // 0.00006103515625
      0.00006097555160522461,  // 0.00006097555160522461
      5.960464477539063e-8,    // 5.960464477539063e-8
      2.9802322387695312e-8,   // 2.9802322387695312e-8
      2.9802322387695312e-8,   // 2.980232238769532e-8
      8.940696716308594e-8,    // 8.940696716308594e-8
      1.4901161193847656e-7,   // 1.4901161193847656e-7
      1.4901161193847656e-7,   // 1.490116119384766e-7
      65504,                   // 65504
      65520,                   // 65520
      65520,                   // 65519.99999999999
      0.000061005353927612305, // 0.000061005353927612305
      0.000061005353927612305  // 0.0000610053539276123
    ],
    Float64: [
      127,         // 127
      128,         // 128
      32767,       // 32767
      32768,       // 32768
      2147483647,  // 2147483647
      2147483648,  // 2147483648
      255,         // 255
      256,         // 256
      65535,       // 65535
      65536,       // 65536
      4294967295,  // 4294967295
      4294967296,  // 4294967296
      9007199254740991, // 9007199254740991
      9007199254740992, // 9007199254740992
      1.1,         // 1.1
      0.1,         // 0.1
      0.5,         // 0.5
      0.50000001,  // 0.50000001,
      0.6,         // 0.6
      0.7,         // 0.7
      NaN,         // undefined
      -1,          // -1
      -0,          // -0
      -0.1,        // -0.1
      -1.1,        // -1.1
      NaN,         // NaN
      -127,        // -127
      -128,        // -128
      -32767,      // -32767
      -32768,      // -32768
      -2147483647, // -2147483647
      -2147483648, // -2147483648
      -255,        // -255
      -256,        // -256
      -65535,      // -65535
      -65536,      // -65536
      -4294967295, // -4294967295
      -4294967296, // -4294967296
      Infinity,    // Infinity
      -Infinity,   // -Infinity
      0,           // 0
      2049,                    // 2049
      2051,                    // 2051
      0.00006103515625,        // 0.00006103515625
      0.00006097555160522461,  // 0.00006097555160522461
      5.960464477539063e-8,    // 5.960464477539063e-8
      2.9802322387695312e-8,   // 2.9802322387695312e-8
      2.980232238769532e-8,    // 2.980232238769532e-8
      8.940696716308594e-8,    // 8.940696716308594e-8
      1.4901161193847656e-7,   // 1.4901161193847656e-7
      1.490116119384766e-7,    // 1.490116119384766e-7
      65504,                   // 65504
      65520,                   // 65520
      65519.99999999999,       // 65519.99999999999
      0.000061005353927612305, // 0.000061005353927612305
      0.0000610053539276123    // 0.0000610053539276123
    ]
  }
};

// file: dateConstants.js
// Copyright (C) 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of date-centric values
defines:
  - date_1899_end
  - date_1900_start
  - date_1969_end
  - date_1970_start
  - date_1999_end
  - date_2000_start
  - date_2099_end
  - date_2100_start
  - start_of_time
  - end_of_time
---*/

var date_1899_end = -2208988800001;
var date_1900_start = -2208988800000;
var date_1969_end = -1;
var date_1970_start = 0;
var date_1999_end = 946684799999;
var date_2000_start = 946684800000;
var date_2099_end = 4102444799999;
var date_2100_start = 4102444800000;

var start_of_time = -8.64e15;
var end_of_time = 8.64e15;

// file: decimalToHexString.js
// Copyright (C) 2017 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to assert the correctness of various encoding operations.
defines: [decimalToHexString, decimalToPercentHexString]
---*/

function decimalToHexString(n) {
  var hex = "0123456789ABCDEF";
  n >>>= 0;
  var s = "";
  while (n) {
    s = hex[n & 0xf] + s;
    n >>>= 4;
  }
  while (s.length < 4) {
    s = "0" + s;
  }
  return s;
}

function decimalToPercentHexString(n) {
  var hex = "0123456789ABCDEF";
  return "%" + hex[(n >> 4) & 0xf] + hex[n & 0xf];
}

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

// file: fnGlobalObject.js
// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Produce a reliable global object
defines: [fnGlobalObject]
---*/

var __globalObject = Function("return this;")();
function fnGlobalObject() {
  return __globalObject;
}

// file: isConstructor.js
// Copyright (C) 2017 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: |
    Test if a given function is a constructor function.
defines: [isConstructor]
features: [Reflect.construct]
---*/

function isConstructor(f) {
    if (typeof f !== "function") {
      throw new Test262Error("isConstructor invoked with a non-function value");
    }

    try {
        Reflect.construct(function(){}, [], f);
    } catch (e) {
        return false;
    }
    return true;
}

// file: nans.js
// Copyright (C) 2016 the V8 project authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    A collection of NaN values produced from expressions that have been observed
    to create distinct bit representations on various platforms. These provide a
    weak basis for assertions regarding the consistent canonicalization of NaN
    values in Array buffers.
defines: [NaNs]
---*/

var NaNs = [
  NaN,
  Number.NaN,
  NaN * 0,
  0/0,
  Infinity/Infinity,
  -(0/0),
  Math.pow(-1, 0.5),
  -Math.pow(-1, 0.5),
  Number("Not-a-Number"),
];

// file: nativeFunctionMatcher.js
// Copyright (C) 2016 Michael Ficarra.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: Assert _NativeFunction_ Syntax
info: |
    NativeFunction :
      function _NativeFunctionAccessor_ opt _IdentifierName_ opt ( _FormalParameters_ ) { [ native code ] }
    NativeFunctionAccessor :
      get
      set
defines:
  - assertToStringOrNativeFunction
  - assertNativeFunction
  - validateNativeFunctionSource
---*/

const validateNativeFunctionSource = function(source) {
  // These regexes should be kept up to date with Unicode using `regexpu-core`.
  // `/\p{ID_Start}/u`
  const UnicodeIDStart = /(?:[A-Za-z\xAA\xB5\xBA\xC0-\xD6\xD8-\xF6\xF8-\u02C1\u02C6-\u02D1\u02E0-\u02E4\u02EC\u02EE\u0370-\u0374\u0376\u0377\u037A-\u037D\u037F\u0386\u0388-\u038A\u038C\u038E-\u03A1\u03A3-\u03F5\u03F7-\u0481\u048A-\u052F\u0531-\u0556\u0559\u0560-\u0588\u05D0-\u05EA\u05EF-\u05F2\u0620-\u064A\u066E\u066F\u0671-\u06D3\u06D5\u06E5\u06E6\u06EE\u06EF\u06FA-\u06FC\u06FF\u0710\u0712-\u072F\u074D-\u07A5\u07B1\u07CA-\u07EA\u07F4\u07F5\u07FA\u0800-\u0815\u081A\u0824\u0828\u0840-\u0858\u0860-\u086A\u08A0-\u08B4\u08B6-\u08C7\u0904-\u0939\u093D\u0950\u0958-\u0961\u0971-\u0980\u0985-\u098C\u098F\u0990\u0993-\u09A8\u09AA-\u09B0\u09B2\u09B6-\u09B9\u09BD\u09CE\u09DC\u09DD\u09DF-\u09E1\u09F0\u09F1\u09FC\u0A05-\u0A0A\u0A0F\u0A10\u0A13-\u0A28\u0A2A-\u0A30\u0A32\u0A33\u0A35\u0A36\u0A38\u0A39\u0A59-\u0A5C\u0A5E\u0A72-\u0A74\u0A85-\u0A8D\u0A8F-\u0A91\u0A93-\u0AA8\u0AAA-\u0AB0\u0AB2\u0AB3\u0AB5-\u0AB9\u0ABD\u0AD0\u0AE0\u0AE1\u0AF9\u0B05-\u0B0C\u0B0F\u0B10\u0B13-\u0B28\u0B2A-\u0B30\u0B32\u0B33\u0B35-\u0B39\u0B3D\u0B5C\u0B5D\u0B5F-\u0B61\u0B71\u0B83\u0B85-\u0B8A\u0B8E-\u0B90\u0B92-\u0B95\u0B99\u0B9A\u0B9C\u0B9E\u0B9F\u0BA3\u0BA4\u0BA8-\u0BAA\u0BAE-\u0BB9\u0BD0\u0C05-\u0C0C\u0C0E-\u0C10\u0C12-\u0C28\u0C2A-\u0C39\u0C3D\u0C58-\u0C5A\u0C60\u0C61\u0C80\u0C85-\u0C8C\u0C8E-\u0C90\u0C92-\u0CA8\u0CAA-\u0CB3\u0CB5-\u0CB9\u0CBD\u0CDE\u0CE0\u0CE1\u0CF1\u0CF2\u0D04-\u0D0C\u0D0E-\u0D10\u0D12-\u0D3A\u0D3D\u0D4E\u0D54-\u0D56\u0D5F-\u0D61\u0D7A-\u0D7F\u0D85-\u0D96\u0D9A-\u0DB1\u0DB3-\u0DBB\u0DBD\u0DC0-\u0DC6\u0E01-\u0E30\u0E32\u0E33\u0E40-\u0E46\u0E81\u0E82\u0E84\u0E86-\u0E8A\u0E8C-\u0EA3\u0EA5\u0EA7-\u0EB0\u0EB2\u0EB3\u0EBD\u0EC0-\u0EC4\u0EC6\u0EDC-\u0EDF\u0F00\u0F40-\u0F47\u0F49-\u0F6C\u0F88-\u0F8C\u1000-\u102A\u103F\u1050-\u1055\u105A-\u105D\u1061\u1065\u1066\u106E-\u1070\u1075-\u1081\u108E\u10A0-\u10C5\u10C7\u10CD\u10D0-\u10FA\u10FC-\u1248\u124A-\u124D\u1250-\u1256\u1258\u125A-\u125D\u1260-\u1288\u128A-\u128D\u1290-\u12B0\u12B2-\u12B5\u12B8-\u12BE\u12C0\u12C2-\u12C5\u12C8-\u12D6\u12D8-\u1310\u1312-\u1315\u1318-\u135A\u1380-\u138F\u13A0-\u13F5\u13F8-\u13FD\u1401-\u166C\u166F-\u167F\u1681-\u169A\u16A0-\u16EA\u16EE-\u16F8\u1700-\u170C\u170E-\u1711\u1720-\u1731\u1740-\u1751\u1760-\u176C\u176E-\u1770\u1780-\u17B3\u17D7\u17DC\u1820-\u1878\u1880-\u18A8\u18AA\u18B0-\u18F5\u1900-\u191E\u1950-\u196D\u1970-\u1974\u1980-\u19AB\u19B0-\u19C9\u1A00-\u1A16\u1A20-\u1A54\u1AA7\u1B05-\u1B33\u1B45-\u1B4B\u1B83-\u1BA0\u1BAE\u1BAF\u1BBA-\u1BE5\u1C00-\u1C23\u1C4D-\u1C4F\u1C5A-\u1C7D\u1C80-\u1C88\u1C90-\u1CBA\u1CBD-\u1CBF\u1CE9-\u1CEC\u1CEE-\u1CF3\u1CF5\u1CF6\u1CFA\u1D00-\u1DBF\u1E00-\u1F15\u1F18-\u1F1D\u1F20-\u1F45\u1F48-\u1F4D\u1F50-\u1F57\u1F59\u1F5B\u1F5D\u1F5F-\u1F7D\u1F80-\u1FB4\u1FB6-\u1FBC\u1FBE\u1FC2-\u1FC4\u1FC6-\u1FCC\u1FD0-\u1FD3\u1FD6-\u1FDB\u1FE0-\u1FEC\u1FF2-\u1FF4\u1FF6-\u1FFC\u2071\u207F\u2090-\u209C\u2102\u2107\u210A-\u2113\u2115\u2118-\u211D\u2124\u2126\u2128\u212A-\u2139\u213C-\u213F\u2145-\u2149\u214E\u2160-\u2188\u2C00-\u2C2E\u2C30-\u2C5E\u2C60-\u2CE4\u2CEB-\u2CEE\u2CF2\u2CF3\u2D00-\u2D25\u2D27\u2D2D\u2D30-\u2D67\u2D6F\u2D80-\u2D96\u2DA0-\u2DA6\u2DA8-\u2DAE\u2DB0-\u2DB6\u2DB8-\u2DBE\u2DC0-\u2DC6\u2DC8-\u2DCE\u2DD0-\u2DD6\u2DD8-\u2DDE\u3005-\u3007\u3021-\u3029\u3031-\u3035\u3038-\u303C\u3041-\u3096\u309B-\u309F\u30A1-\u30FA\u30FC-\u30FF\u3105-\u312F\u3131-\u318E\u31A0-\u31BF\u31F0-\u31FF\u3400-\u4DBF\u4E00-\u9FFC\uA000-\uA48C\uA4D0-\uA4FD\uA500-\uA60C\uA610-\uA61F\uA62A\uA62B\uA640-\uA66E\uA67F-\uA69D\uA6A0-\uA6EF\uA717-\uA71F\uA722-\uA788\uA78B-\uA7BF\uA7C2-\uA7CA\uA7F5-\uA801\uA803-\uA805\uA807-\uA80A\uA80C-\uA822\uA840-\uA873\uA882-\uA8B3\uA8F2-\uA8F7\uA8FB\uA8FD\uA8FE\uA90A-\uA925\uA930-\uA946\uA960-\uA97C\uA984-\uA9B2\uA9CF\uA9E0-\uA9E4\uA9E6-\uA9EF\uA9FA-\uA9FE\uAA00-\uAA28\uAA40-\uAA42\uAA44-\uAA4B\uAA60-\uAA76\uAA7A\uAA7E-\uAAAF\uAAB1\uAAB5\uAAB6\uAAB9-\uAABD\uAAC0\uAAC2\uAADB-\uAADD\uAAE0-\uAAEA\uAAF2-\uAAF4\uAB01-\uAB06\uAB09-\uAB0E\uAB11-\uAB16\uAB20-\uAB26\uAB28-\uAB2E\uAB30-\uAB5A\uAB5C-\uAB69\uAB70-\uABE2\uAC00-\uD7A3\uD7B0-\uD7C6\uD7CB-\uD7FB\uF900-\uFA6D\uFA70-\uFAD9\uFB00-\uFB06\uFB13-\uFB17\uFB1D\uFB1F-\uFB28\uFB2A-\uFB36\uFB38-\uFB3C\uFB3E\uFB40\uFB41\uFB43\uFB44\uFB46-\uFBB1\uFBD3-\uFD3D\uFD50-\uFD8F\uFD92-\uFDC7\uFDF0-\uFDFB\uFE70-\uFE74\uFE76-\uFEFC\uFF21-\uFF3A\uFF41-\uFF5A\uFF66-\uFFBE\uFFC2-\uFFC7\uFFCA-\uFFCF\uFFD2-\uFFD7\uFFDA-\uFFDC]|\uD800[\uDC00-\uDC0B\uDC0D-\uDC26\uDC28-\uDC3A\uDC3C\uDC3D\uDC3F-\uDC4D\uDC50-\uDC5D\uDC80-\uDCFA\uDD40-\uDD74\uDE80-\uDE9C\uDEA0-\uDED0\uDF00-\uDF1F\uDF2D-\uDF4A\uDF50-\uDF75\uDF80-\uDF9D\uDFA0-\uDFC3\uDFC8-\uDFCF\uDFD1-\uDFD5]|\uD801[\uDC00-\uDC9D\uDCB0-\uDCD3\uDCD8-\uDCFB\uDD00-\uDD27\uDD30-\uDD63\uDE00-\uDF36\uDF40-\uDF55\uDF60-\uDF67]|\uD802[\uDC00-\uDC05\uDC08\uDC0A-\uDC35\uDC37\uDC38\uDC3C\uDC3F-\uDC55\uDC60-\uDC76\uDC80-\uDC9E\uDCE0-\uDCF2\uDCF4\uDCF5\uDD00-\uDD15\uDD20-\uDD39\uDD80-\uDDB7\uDDBE\uDDBF\uDE00\uDE10-\uDE13\uDE15-\uDE17\uDE19-\uDE35\uDE60-\uDE7C\uDE80-\uDE9C\uDEC0-\uDEC7\uDEC9-\uDEE4\uDF00-\uDF35\uDF40-\uDF55\uDF60-\uDF72\uDF80-\uDF91]|\uD803[\uDC00-\uDC48\uDC80-\uDCB2\uDCC0-\uDCF2\uDD00-\uDD23\uDE80-\uDEA9\uDEB0\uDEB1\uDF00-\uDF1C\uDF27\uDF30-\uDF45\uDFB0-\uDFC4\uDFE0-\uDFF6]|\uD804[\uDC03-\uDC37\uDC83-\uDCAF\uDCD0-\uDCE8\uDD03-\uDD26\uDD44\uDD47\uDD50-\uDD72\uDD76\uDD83-\uDDB2\uDDC1-\uDDC4\uDDDA\uDDDC\uDE00-\uDE11\uDE13-\uDE2B\uDE80-\uDE86\uDE88\uDE8A-\uDE8D\uDE8F-\uDE9D\uDE9F-\uDEA8\uDEB0-\uDEDE\uDF05-\uDF0C\uDF0F\uDF10\uDF13-\uDF28\uDF2A-\uDF30\uDF32\uDF33\uDF35-\uDF39\uDF3D\uDF50\uDF5D-\uDF61]|\uD805[\uDC00-\uDC34\uDC47-\uDC4A\uDC5F-\uDC61\uDC80-\uDCAF\uDCC4\uDCC5\uDCC7\uDD80-\uDDAE\uDDD8-\uDDDB\uDE00-\uDE2F\uDE44\uDE80-\uDEAA\uDEB8\uDF00-\uDF1A]|\uD806[\uDC00-\uDC2B\uDCA0-\uDCDF\uDCFF-\uDD06\uDD09\uDD0C-\uDD13\uDD15\uDD16\uDD18-\uDD2F\uDD3F\uDD41\uDDA0-\uDDA7\uDDAA-\uDDD0\uDDE1\uDDE3\uDE00\uDE0B-\uDE32\uDE3A\uDE50\uDE5C-\uDE89\uDE9D\uDEC0-\uDEF8]|\uD807[\uDC00-\uDC08\uDC0A-\uDC2E\uDC40\uDC72-\uDC8F\uDD00-\uDD06\uDD08\uDD09\uDD0B-\uDD30\uDD46\uDD60-\uDD65\uDD67\uDD68\uDD6A-\uDD89\uDD98\uDEE0-\uDEF2\uDFB0]|\uD808[\uDC00-\uDF99]|\uD809[\uDC00-\uDC6E\uDC80-\uDD43]|[\uD80C\uD81C-\uD820\uD822\uD840-\uD868\uD86A-\uD86C\uD86F-\uD872\uD874-\uD879\uD880-\uD883][\uDC00-\uDFFF]|\uD80D[\uDC00-\uDC2E]|\uD811[\uDC00-\uDE46]|\uD81A[\uDC00-\uDE38\uDE40-\uDE5E\uDED0-\uDEED\uDF00-\uDF2F\uDF40-\uDF43\uDF63-\uDF77\uDF7D-\uDF8F]|\uD81B[\uDE40-\uDE7F\uDF00-\uDF4A\uDF50\uDF93-\uDF9F\uDFE0\uDFE1\uDFE3]|\uD821[\uDC00-\uDFF7]|\uD823[\uDC00-\uDCD5\uDD00-\uDD08]|\uD82C[\uDC00-\uDD1E\uDD50-\uDD52\uDD64-\uDD67\uDD70-\uDEFB]|\uD82F[\uDC00-\uDC6A\uDC70-\uDC7C\uDC80-\uDC88\uDC90-\uDC99]|\uD835[\uDC00-\uDC54\uDC56-\uDC9C\uDC9E\uDC9F\uDCA2\uDCA5\uDCA6\uDCA9-\uDCAC\uDCAE-\uDCB9\uDCBB\uDCBD-\uDCC3\uDCC5-\uDD05\uDD07-\uDD0A\uDD0D-\uDD14\uDD16-\uDD1C\uDD1E-\uDD39\uDD3B-\uDD3E\uDD40-\uDD44\uDD46\uDD4A-\uDD50\uDD52-\uDEA5\uDEA8-\uDEC0\uDEC2-\uDEDA\uDEDC-\uDEFA\uDEFC-\uDF14\uDF16-\uDF34\uDF36-\uDF4E\uDF50-\uDF6E\uDF70-\uDF88\uDF8A-\uDFA8\uDFAA-\uDFC2\uDFC4-\uDFCB]|\uD838[\uDD00-\uDD2C\uDD37-\uDD3D\uDD4E\uDEC0-\uDEEB]|\uD83A[\uDC00-\uDCC4\uDD00-\uDD43\uDD4B]|\uD83B[\uDE00-\uDE03\uDE05-\uDE1F\uDE21\uDE22\uDE24\uDE27\uDE29-\uDE32\uDE34-\uDE37\uDE39\uDE3B\uDE42\uDE47\uDE49\uDE4B\uDE4D-\uDE4F\uDE51\uDE52\uDE54\uDE57\uDE59\uDE5B\uDE5D\uDE5F\uDE61\uDE62\uDE64\uDE67-\uDE6A\uDE6C-\uDE72\uDE74-\uDE77\uDE79-\uDE7C\uDE7E\uDE80-\uDE89\uDE8B-\uDE9B\uDEA1-\uDEA3\uDEA5-\uDEA9\uDEAB-\uDEBB]|\uD869[\uDC00-\uDEDD\uDF00-\uDFFF]|\uD86D[\uDC00-\uDF34\uDF40-\uDFFF]|\uD86E[\uDC00-\uDC1D\uDC20-\uDFFF]|\uD873[\uDC00-\uDEA1\uDEB0-\uDFFF]|\uD87A[\uDC00-\uDFE0]|\uD87E[\uDC00-\uDE1D]|\uD884[\uDC00-\uDF4A])/;
  // `/\p{ID_Continue}/u`
  const UnicodeIDContinue = /(?:[0-9A-Z_a-z\xAA\xB5\xB7\xBA\xC0-\xD6\xD8-\xF6\xF8-\u02C1\u02C6-\u02D1\u02E0-\u02E4\u02EC\u02EE\u0300-\u0374\u0376\u0377\u037A-\u037D\u037F\u0386-\u038A\u038C\u038E-\u03A1\u03A3-\u03F5\u03F7-\u0481\u0483-\u0487\u048A-\u052F\u0531-\u0556\u0559\u0560-\u0588\u0591-\u05BD\u05BF\u05C1\u05C2\u05C4\u05C5\u05C7\u05D0-\u05EA\u05EF-\u05F2\u0610-\u061A\u0620-\u0669\u066E-\u06D3\u06D5-\u06DC\u06DF-\u06E8\u06EA-\u06FC\u06FF\u0710-\u074A\u074D-\u07B1\u07C0-\u07F5\u07FA\u07FD\u0800-\u082D\u0840-\u085B\u0860-\u086A\u08A0-\u08B4\u08B6-\u08C7\u08D3-\u08E1\u08E3-\u0963\u0966-\u096F\u0971-\u0983\u0985-\u098C\u098F\u0990\u0993-\u09A8\u09AA-\u09B0\u09B2\u09B6-\u09B9\u09BC-\u09C4\u09C7\u09C8\u09CB-\u09CE\u09D7\u09DC\u09DD\u09DF-\u09E3\u09E6-\u09F1\u09FC\u09FE\u0A01-\u0A03\u0A05-\u0A0A\u0A0F\u0A10\u0A13-\u0A28\u0A2A-\u0A30\u0A32\u0A33\u0A35\u0A36\u0A38\u0A39\u0A3C\u0A3E-\u0A42\u0A47\u0A48\u0A4B-\u0A4D\u0A51\u0A59-\u0A5C\u0A5E\u0A66-\u0A75\u0A81-\u0A83\u0A85-\u0A8D\u0A8F-\u0A91\u0A93-\u0AA8\u0AAA-\u0AB0\u0AB2\u0AB3\u0AB5-\u0AB9\u0ABC-\u0AC5\u0AC7-\u0AC9\u0ACB-\u0ACD\u0AD0\u0AE0-\u0AE3\u0AE6-\u0AEF\u0AF9-\u0AFF\u0B01-\u0B03\u0B05-\u0B0C\u0B0F\u0B10\u0B13-\u0B28\u0B2A-\u0B30\u0B32\u0B33\u0B35-\u0B39\u0B3C-\u0B44\u0B47\u0B48\u0B4B-\u0B4D\u0B55-\u0B57\u0B5C\u0B5D\u0B5F-\u0B63\u0B66-\u0B6F\u0B71\u0B82\u0B83\u0B85-\u0B8A\u0B8E-\u0B90\u0B92-\u0B95\u0B99\u0B9A\u0B9C\u0B9E\u0B9F\u0BA3\u0BA4\u0BA8-\u0BAA\u0BAE-\u0BB9\u0BBE-\u0BC2\u0BC6-\u0BC8\u0BCA-\u0BCD\u0BD0\u0BD7\u0BE6-\u0BEF\u0C00-\u0C0C\u0C0E-\u0C10\u0C12-\u0C28\u0C2A-\u0C39\u0C3D-\u0C44\u0C46-\u0C48\u0C4A-\u0C4D\u0C55\u0C56\u0C58-\u0C5A\u0C60-\u0C63\u0C66-\u0C6F\u0C80-\u0C83\u0C85-\u0C8C\u0C8E-\u0C90\u0C92-\u0CA8\u0CAA-\u0CB3\u0CB5-\u0CB9\u0CBC-\u0CC4\u0CC6-\u0CC8\u0CCA-\u0CCD\u0CD5\u0CD6\u0CDE\u0CE0-\u0CE3\u0CE6-\u0CEF\u0CF1\u0CF2\u0D00-\u0D0C\u0D0E-\u0D10\u0D12-\u0D44\u0D46-\u0D48\u0D4A-\u0D4E\u0D54-\u0D57\u0D5F-\u0D63\u0D66-\u0D6F\u0D7A-\u0D7F\u0D81-\u0D83\u0D85-\u0D96\u0D9A-\u0DB1\u0DB3-\u0DBB\u0DBD\u0DC0-\u0DC6\u0DCA\u0DCF-\u0DD4\u0DD6\u0DD8-\u0DDF\u0DE6-\u0DEF\u0DF2\u0DF3\u0E01-\u0E3A\u0E40-\u0E4E\u0E50-\u0E59\u0E81\u0E82\u0E84\u0E86-\u0E8A\u0E8C-\u0EA3\u0EA5\u0EA7-\u0EBD\u0EC0-\u0EC4\u0EC6\u0EC8-\u0ECD\u0ED0-\u0ED9\u0EDC-\u0EDF\u0F00\u0F18\u0F19\u0F20-\u0F29\u0F35\u0F37\u0F39\u0F3E-\u0F47\u0F49-\u0F6C\u0F71-\u0F84\u0F86-\u0F97\u0F99-\u0FBC\u0FC6\u1000-\u1049\u1050-\u109D\u10A0-\u10C5\u10C7\u10CD\u10D0-\u10FA\u10FC-\u1248\u124A-\u124D\u1250-\u1256\u1258\u125A-\u125D\u1260-\u1288\u128A-\u128D\u1290-\u12B0\u12B2-\u12B5\u12B8-\u12BE\u12C0\u12C2-\u12C5\u12C8-\u12D6\u12D8-\u1310\u1312-\u1315\u1318-\u135A\u135D-\u135F\u1369-\u1371\u1380-\u138F\u13A0-\u13F5\u13F8-\u13FD\u1401-\u166C\u166F-\u167F\u1681-\u169A\u16A0-\u16EA\u16EE-\u16F8\u1700-\u170C\u170E-\u1714\u1720-\u1734\u1740-\u1753\u1760-\u176C\u176E-\u1770\u1772\u1773\u1780-\u17D3\u17D7\u17DC\u17DD\u17E0-\u17E9\u180B-\u180D\u1810-\u1819\u1820-\u1878\u1880-\u18AA\u18B0-\u18F5\u1900-\u191E\u1920-\u192B\u1930-\u193B\u1946-\u196D\u1970-\u1974\u1980-\u19AB\u19B0-\u19C9\u19D0-\u19DA\u1A00-\u1A1B\u1A20-\u1A5E\u1A60-\u1A7C\u1A7F-\u1A89\u1A90-\u1A99\u1AA7\u1AB0-\u1ABD\u1ABF\u1AC0\u1B00-\u1B4B\u1B50-\u1B59\u1B6B-\u1B73\u1B80-\u1BF3\u1C00-\u1C37\u1C40-\u1C49\u1C4D-\u1C7D\u1C80-\u1C88\u1C90-\u1CBA\u1CBD-\u1CBF\u1CD0-\u1CD2\u1CD4-\u1CFA\u1D00-\u1DF9\u1DFB-\u1F15\u1F18-\u1F1D\u1F20-\u1F45\u1F48-\u1F4D\u1F50-\u1F57\u1F59\u1F5B\u1F5D\u1F5F-\u1F7D\u1F80-\u1FB4\u1FB6-\u1FBC\u1FBE\u1FC2-\u1FC4\u1FC6-\u1FCC\u1FD0-\u1FD3\u1FD6-\u1FDB\u1FE0-\u1FEC\u1FF2-\u1FF4\u1FF6-\u1FFC\u203F\u2040\u2054\u2071\u207F\u2090-\u209C\u20D0-\u20DC\u20E1\u20E5-\u20F0\u2102\u2107\u210A-\u2113\u2115\u2118-\u211D\u2124\u2126\u2128\u212A-\u2139\u213C-\u213F\u2145-\u2149\u214E\u2160-\u2188\u2C00-\u2C2E\u2C30-\u2C5E\u2C60-\u2CE4\u2CEB-\u2CF3\u2D00-\u2D25\u2D27\u2D2D\u2D30-\u2D67\u2D6F\u2D7F-\u2D96\u2DA0-\u2DA6\u2DA8-\u2DAE\u2DB0-\u2DB6\u2DB8-\u2DBE\u2DC0-\u2DC6\u2DC8-\u2DCE\u2DD0-\u2DD6\u2DD8-\u2DDE\u2DE0-\u2DFF\u3005-\u3007\u3021-\u302F\u3031-\u3035\u3038-\u303C\u3041-\u3096\u3099-\u309F\u30A1-\u30FA\u30FC-\u30FF\u3105-\u312F\u3131-\u318E\u31A0-\u31BF\u31F0-\u31FF\u3400-\u4DBF\u4E00-\u9FFC\uA000-\uA48C\uA4D0-\uA4FD\uA500-\uA60C\uA610-\uA62B\uA640-\uA66F\uA674-\uA67D\uA67F-\uA6F1\uA717-\uA71F\uA722-\uA788\uA78B-\uA7BF\uA7C2-\uA7CA\uA7F5-\uA827\uA82C\uA840-\uA873\uA880-\uA8C5\uA8D0-\uA8D9\uA8E0-\uA8F7\uA8FB\uA8FD-\uA92D\uA930-\uA953\uA960-\uA97C\uA980-\uA9C0\uA9CF-\uA9D9\uA9E0-\uA9FE\uAA00-\uAA36\uAA40-\uAA4D\uAA50-\uAA59\uAA60-\uAA76\uAA7A-\uAAC2\uAADB-\uAADD\uAAE0-\uAAEF\uAAF2-\uAAF6\uAB01-\uAB06\uAB09-\uAB0E\uAB11-\uAB16\uAB20-\uAB26\uAB28-\uAB2E\uAB30-\uAB5A\uAB5C-\uAB69\uAB70-\uABEA\uABEC\uABED\uABF0-\uABF9\uAC00-\uD7A3\uD7B0-\uD7C6\uD7CB-\uD7FB\uF900-\uFA6D\uFA70-\uFAD9\uFB00-\uFB06\uFB13-\uFB17\uFB1D-\uFB28\uFB2A-\uFB36\uFB38-\uFB3C\uFB3E\uFB40\uFB41\uFB43\uFB44\uFB46-\uFBB1\uFBD3-\uFD3D\uFD50-\uFD8F\uFD92-\uFDC7\uFDF0-\uFDFB\uFE00-\uFE0F\uFE20-\uFE2F\uFE33\uFE34\uFE4D-\uFE4F\uFE70-\uFE74\uFE76-\uFEFC\uFF10-\uFF19\uFF21-\uFF3A\uFF3F\uFF41-\uFF5A\uFF66-\uFFBE\uFFC2-\uFFC7\uFFCA-\uFFCF\uFFD2-\uFFD7\uFFDA-\uFFDC]|\uD800[\uDC00-\uDC0B\uDC0D-\uDC26\uDC28-\uDC3A\uDC3C\uDC3D\uDC3F-\uDC4D\uDC50-\uDC5D\uDC80-\uDCFA\uDD40-\uDD74\uDDFD\uDE80-\uDE9C\uDEA0-\uDED0\uDEE0\uDF00-\uDF1F\uDF2D-\uDF4A\uDF50-\uDF7A\uDF80-\uDF9D\uDFA0-\uDFC3\uDFC8-\uDFCF\uDFD1-\uDFD5]|\uD801[\uDC00-\uDC9D\uDCA0-\uDCA9\uDCB0-\uDCD3\uDCD8-\uDCFB\uDD00-\uDD27\uDD30-\uDD63\uDE00-\uDF36\uDF40-\uDF55\uDF60-\uDF67]|\uD802[\uDC00-\uDC05\uDC08\uDC0A-\uDC35\uDC37\uDC38\uDC3C\uDC3F-\uDC55\uDC60-\uDC76\uDC80-\uDC9E\uDCE0-\uDCF2\uDCF4\uDCF5\uDD00-\uDD15\uDD20-\uDD39\uDD80-\uDDB7\uDDBE\uDDBF\uDE00-\uDE03\uDE05\uDE06\uDE0C-\uDE13\uDE15-\uDE17\uDE19-\uDE35\uDE38-\uDE3A\uDE3F\uDE60-\uDE7C\uDE80-\uDE9C\uDEC0-\uDEC7\uDEC9-\uDEE6\uDF00-\uDF35\uDF40-\uDF55\uDF60-\uDF72\uDF80-\uDF91]|\uD803[\uDC00-\uDC48\uDC80-\uDCB2\uDCC0-\uDCF2\uDD00-\uDD27\uDD30-\uDD39\uDE80-\uDEA9\uDEAB\uDEAC\uDEB0\uDEB1\uDF00-\uDF1C\uDF27\uDF30-\uDF50\uDFB0-\uDFC4\uDFE0-\uDFF6]|\uD804[\uDC00-\uDC46\uDC66-\uDC6F\uDC7F-\uDCBA\uDCD0-\uDCE8\uDCF0-\uDCF9\uDD00-\uDD34\uDD36-\uDD3F\uDD44-\uDD47\uDD50-\uDD73\uDD76\uDD80-\uDDC4\uDDC9-\uDDCC\uDDCE-\uDDDA\uDDDC\uDE00-\uDE11\uDE13-\uDE37\uDE3E\uDE80-\uDE86\uDE88\uDE8A-\uDE8D\uDE8F-\uDE9D\uDE9F-\uDEA8\uDEB0-\uDEEA\uDEF0-\uDEF9\uDF00-\uDF03\uDF05-\uDF0C\uDF0F\uDF10\uDF13-\uDF28\uDF2A-\uDF30\uDF32\uDF33\uDF35-\uDF39\uDF3B-\uDF44\uDF47\uDF48\uDF4B-\uDF4D\uDF50\uDF57\uDF5D-\uDF63\uDF66-\uDF6C\uDF70-\uDF74]|\uD805[\uDC00-\uDC4A\uDC50-\uDC59\uDC5E-\uDC61\uDC80-\uDCC5\uDCC7\uDCD0-\uDCD9\uDD80-\uDDB5\uDDB8-\uDDC0\uDDD8-\uDDDD\uDE00-\uDE40\uDE44\uDE50-\uDE59\uDE80-\uDEB8\uDEC0-\uDEC9\uDF00-\uDF1A\uDF1D-\uDF2B\uDF30-\uDF39]|\uD806[\uDC00-\uDC3A\uDCA0-\uDCE9\uDCFF-\uDD06\uDD09\uDD0C-\uDD13\uDD15\uDD16\uDD18-\uDD35\uDD37\uDD38\uDD3B-\uDD43\uDD50-\uDD59\uDDA0-\uDDA7\uDDAA-\uDDD7\uDDDA-\uDDE1\uDDE3\uDDE4\uDE00-\uDE3E\uDE47\uDE50-\uDE99\uDE9D\uDEC0-\uDEF8]|\uD807[\uDC00-\uDC08\uDC0A-\uDC36\uDC38-\uDC40\uDC50-\uDC59\uDC72-\uDC8F\uDC92-\uDCA7\uDCA9-\uDCB6\uDD00-\uDD06\uDD08\uDD09\uDD0B-\uDD36\uDD3A\uDD3C\uDD3D\uDD3F-\uDD47\uDD50-\uDD59\uDD60-\uDD65\uDD67\uDD68\uDD6A-\uDD8E\uDD90\uDD91\uDD93-\uDD98\uDDA0-\uDDA9\uDEE0-\uDEF6\uDFB0]|\uD808[\uDC00-\uDF99]|\uD809[\uDC00-\uDC6E\uDC80-\uDD43]|[\uD80C\uD81C-\uD820\uD822\uD840-\uD868\uD86A-\uD86C\uD86F-\uD872\uD874-\uD879\uD880-\uD883][\uDC00-\uDFFF]|\uD80D[\uDC00-\uDC2E]|\uD811[\uDC00-\uDE46]|\uD81A[\uDC00-\uDE38\uDE40-\uDE5E\uDE60-\uDE69\uDED0-\uDEED\uDEF0-\uDEF4\uDF00-\uDF36\uDF40-\uDF43\uDF50-\uDF59\uDF63-\uDF77\uDF7D-\uDF8F]|\uD81B[\uDE40-\uDE7F\uDF00-\uDF4A\uDF4F-\uDF87\uDF8F-\uDF9F\uDFE0\uDFE1\uDFE3\uDFE4\uDFF0\uDFF1]|\uD821[\uDC00-\uDFF7]|\uD823[\uDC00-\uDCD5\uDD00-\uDD08]|\uD82C[\uDC00-\uDD1E\uDD50-\uDD52\uDD64-\uDD67\uDD70-\uDEFB]|\uD82F[\uDC00-\uDC6A\uDC70-\uDC7C\uDC80-\uDC88\uDC90-\uDC99\uDC9D\uDC9E]|\uD834[\uDD65-\uDD69\uDD6D-\uDD72\uDD7B-\uDD82\uDD85-\uDD8B\uDDAA-\uDDAD\uDE42-\uDE44]|\uD835[\uDC00-\uDC54\uDC56-\uDC9C\uDC9E\uDC9F\uDCA2\uDCA5\uDCA6\uDCA9-\uDCAC\uDCAE-\uDCB9\uDCBB\uDCBD-\uDCC3\uDCC5-\uDD05\uDD07-\uDD0A\uDD0D-\uDD14\uDD16-\uDD1C\uDD1E-\uDD39\uDD3B-\uDD3E\uDD40-\uDD44\uDD46\uDD4A-\uDD50\uDD52-\uDEA5\uDEA8-\uDEC0\uDEC2-\uDEDA\uDEDC-\uDEFA\uDEFC-\uDF14\uDF16-\uDF34\uDF36-\uDF4E\uDF50-\uDF6E\uDF70-\uDF88\uDF8A-\uDFA8\uDFAA-\uDFC2\uDFC4-\uDFCB\uDFCE-\uDFFF]|\uD836[\uDE00-\uDE36\uDE3B-\uDE6C\uDE75\uDE84\uDE9B-\uDE9F\uDEA1-\uDEAF]|\uD838[\uDC00-\uDC06\uDC08-\uDC18\uDC1B-\uDC21\uDC23\uDC24\uDC26-\uDC2A\uDD00-\uDD2C\uDD30-\uDD3D\uDD40-\uDD49\uDD4E\uDEC0-\uDEF9]|\uD83A[\uDC00-\uDCC4\uDCD0-\uDCD6\uDD00-\uDD4B\uDD50-\uDD59]|\uD83B[\uDE00-\uDE03\uDE05-\uDE1F\uDE21\uDE22\uDE24\uDE27\uDE29-\uDE32\uDE34-\uDE37\uDE39\uDE3B\uDE42\uDE47\uDE49\uDE4B\uDE4D-\uDE4F\uDE51\uDE52\uDE54\uDE57\uDE59\uDE5B\uDE5D\uDE5F\uDE61\uDE62\uDE64\uDE67-\uDE6A\uDE6C-\uDE72\uDE74-\uDE77\uDE79-\uDE7C\uDE7E\uDE80-\uDE89\uDE8B-\uDE9B\uDEA1-\uDEA3\uDEA5-\uDEA9\uDEAB-\uDEBB]|\uD83E[\uDFF0-\uDFF9]|\uD869[\uDC00-\uDEDD\uDF00-\uDFFF]|\uD86D[\uDC00-\uDF34\uDF40-\uDFFF]|\uD86E[\uDC00-\uDC1D\uDC20-\uDFFF]|\uD873[\uDC00-\uDEA1\uDEB0-\uDFFF]|\uD87A[\uDC00-\uDFE0]|\uD87E[\uDC00-\uDE1D]|\uD884[\uDC00-\uDF4A]|\uDB40[\uDD00-\uDDEF])/;
  // `/\p{Space_Separator}/u`
  const UnicodeSpaceSeparator = /[ \xA0\u1680\u2000-\u200A\u202F\u205F\u3000]/;

  const isNewline = (c) => /[\u000A\u000D\u2028\u2029]/.test(c);
  const isWhitespace = (c) => /[\u0009\u000B\u000C\u0020\u00A0\uFEFF]/.test(c) || UnicodeSpaceSeparator.test(c);

  let pos = 0;

  const eatWhitespace = () => {
    while (pos < source.length) {
      const c = source[pos];
      if (isWhitespace(c) || isNewline(c)) {
        pos += 1;
        continue;
      }

      if (c === '/') {
        if (source[pos + 1] === '/') {
          while (pos < source.length) {
            if (isNewline(source[pos])) {
              break;
            }
            pos += 1;
          }
          continue;
        }
        if (source[pos + 1] === '*') {
          const end = source.indexOf('*/', pos);
          if (end === -1) {
            throw new SyntaxError();
          }
          pos = end + '*/'.length;
          continue;
        }
      }

      break;
    }
  };

  const getIdentifier = () => {
    eatWhitespace();

    const start = pos;
    let end = pos;
    switch (source[end]) {
      case '_':
      case '$':
        end += 1;
        break;
      default:
        if (UnicodeIDStart.test(source[end])) {
          end += 1;
          break;
        }
        return null;
    }
    while (end < source.length) {
      const c = source[end];
      switch (c) {
        case '_':
        case '$':
          end += 1;
          break;
        default:
          if (UnicodeIDContinue.test(c)) {
            end += 1;
            break;
          }
          return source.slice(start, end);
      }
    }
    return source.slice(start, end);
  };

  const test = (s) => {
    eatWhitespace();

    if (/\w/.test(s)) {
      return getIdentifier() === s;
    }
    return source.slice(pos, pos + s.length) === s;
  };

  const eat = (s) => {
    if (test(s)) {
      pos += s.length;
      return true;
    }
    return false;
  };

  const eatIdentifier = () => {
    const n = getIdentifier();
    if (n !== null) {
      pos += n.length;
      return true;
    }
    return false;
  };

  const expect = (s) => {
    if (!eat(s)) {
      throw new SyntaxError();
    }
  };

  const eatString = () => {
    if (source[pos] === '\'' || source[pos] === '"') {
      const match = source[pos];
      pos += 1;
      while (pos < source.length) {
        if (source[pos] === match && source[pos - 1] !== '\\') {
          return;
        }
        if (isNewline(source[pos])) {
          throw new SyntaxError();
        }
        pos += 1;
      }
      throw new SyntaxError();
    }
  };

  // "Stumble" through source text until matching character is found.
  // Assumes ECMAScript syntax keeps `[]` and `()` balanced.
  const stumbleUntil = (c) => {
    const match = {
      ']': '[',
      ')': '(',
    }[c];
    let nesting = 1;
    while (pos < source.length) {
      eatWhitespace();
      eatString(); // Strings may contain unbalanced characters.
      if (source[pos] === match) {
        nesting += 1;
      } else if (source[pos] === c) {
        nesting -= 1;
      }
      pos += 1;
      if (nesting === 0) {
        return;
      }
    }
    throw new SyntaxError();
  };

  // function
  expect('function');

  // NativeFunctionAccessor
  eat('get') || eat('set');

  // PropertyName
  if (!eatIdentifier() && eat('[')) {
    stumbleUntil(']');
  }

  // ( FormalParameters )
  expect('(');
  stumbleUntil(')');

  // {
  expect('{');

  // [native code]
  expect('[');
  expect('native');
  expect('code');
  expect(']');

  // }
  expect('}');

  eatWhitespace();
  if (pos !== source.length) {
    throw new SyntaxError();
  }
};

const assertToStringOrNativeFunction = function(fn, expected) {
  const actual = "" + fn;
  try {
    assert.sameValue(actual, expected);
  } catch (unused) {
    assertNativeFunction(fn, expected);
  }
};

const assertNativeFunction = function(fn, special) {
  const actual = "" + fn;
  try {
    validateNativeFunctionSource(actual);
  } catch (unused) {
    throw new Test262Error('Conforms to NativeFunction Syntax: ' + JSON.stringify(actual) + (special ? ' (' + special + ')' : ''));
  }
};

// file: promiseHelper.js
// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Check that an array contains a numeric sequence starting at 1
    and incrementing by 1 for each entry in the array. Used by
    Promise tests to assert the order of execution in deep Promise
    resolution pipelines.
defines: [checkSequence, checkSettledPromises]
---*/

function checkSequence(arr, message) {
  arr.forEach(function(e, i) {
    if (e !== (i+1)) {
      throw new Test262Error((message ? message : "Steps in unexpected sequence:") +
             " '" + arr.join(',') + "'");
    }
  });

  return true;
}

function checkSettledPromises(settleds, expected, message) {
  const prefix = message ? `${message}: ` : '';

  assert.sameValue(Array.isArray(settleds), true, `${prefix}Settled values is an array`);

  assert.sameValue(
    settleds.length,
    expected.length,
    `${prefix}The settled values has a different length than expected`
  );

  settleds.forEach((settled, i) => {
    assert.sameValue(
      Object.prototype.hasOwnProperty.call(settled, 'status'),
      true,
      `${prefix}The settled value has a property status`
    );

    assert.sameValue(settled.status, expected[i].status, `${prefix}status for item ${i}`);

    if (settled.status === 'fulfilled') {
      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'value'),
        true,
        `${prefix}The fulfilled promise has a property named value`
      );

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'reason'),
        false,
        `${prefix}The fulfilled promise has no property named reason`
      );

      assert.sameValue(settled.value, expected[i].value, `${prefix}value for item ${i}`);
    } else {
      assert.sameValue(settled.status, 'rejected', `${prefix}Valid statuses are only fulfilled or rejected`);

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'value'),
        false,
        `${prefix}The fulfilled promise has no property named value`
      );

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'reason'),
        true,
        `${prefix}The fulfilled promise has a property named reason`
      );

      assert.sameValue(settled.reason, expected[i].reason, `${prefix}Reason value for item ${i}`);
    }
  });
}

// file: proxyTrapsHelper.js
// Copyright (C) 2016 Jordan Harband.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Used to assert the correctness of object behavior in the presence
    and context of Proxy objects.
defines: [allowProxyTraps]
---*/

function allowProxyTraps(overrides, label) {
  var prefix = typeof label === 'string' && label.length > 0 ? label + ': ' : '';
  function throwTest262Error(msg) {
    return function () { Test262Error.thrower(prefix + msg); };
  }
  if (!overrides) { overrides = {}; }
  return {
    getPrototypeOf: overrides.getPrototypeOf || throwTest262Error('[[GetPrototypeOf]] trap called'),
    setPrototypeOf: overrides.setPrototypeOf || throwTest262Error('[[SetPrototypeOf]] trap called'),
    isExtensible: overrides.isExtensible || throwTest262Error('[[IsExtensible]] trap called'),
    preventExtensions: overrides.preventExtensions || throwTest262Error('[[PreventExtensions]] trap called'),
    getOwnPropertyDescriptor: overrides.getOwnPropertyDescriptor || throwTest262Error('[[GetOwnProperty]] trap called'),
    has: overrides.has || throwTest262Error('[[HasProperty]] trap called'),
    get: overrides.get || throwTest262Error('[[Get]] trap called'),
    set: overrides.set || throwTest262Error('[[Set]] trap called'),
    deleteProperty: overrides.deleteProperty || throwTest262Error('[[Delete]] trap called'),
    defineProperty: overrides.defineProperty || throwTest262Error('[[DefineOwnProperty]] trap called'),
    enumerate: throwTest262Error('[[Enumerate]] trap called: this trap has been removed'),
    ownKeys: overrides.ownKeys || throwTest262Error('[[OwnPropertyKeys]] trap called'),
    apply: overrides.apply || throwTest262Error('[[Call]] trap called'),
    construct: overrides.construct || throwTest262Error('[[Construct]] trap called')
  };
}

// file: tcoHelper.js
// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    This defines the number of consecutive recursive function calls that must be
    made in order to prove that stack frames are properly destroyed according to
    ES2015 tail call optimization semantics.
defines: [$MAX_ITERATIONS]
---*/




var $MAX_ITERATIONS = 100000;

// file: testIntl.js
// Copyright (C) 2011 2012 Norbert Lindenberg. All rights reserved.
// Copyright (C) 2012 2013 Mozilla Corporation. All rights reserved.
// Copyright (C) 2020 Apple Inc. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    This file contains shared functions for the tests in the conformance test
    suite for the ECMAScript Internationalization API.
author: Norbert Lindenberg
defines:
  - testWithIntlConstructors
  - taintDataProperty
  - taintMethod
  - taintProperties
  - taintArray
  - getLocaleSupportInfo
  - getInvalidLanguageTags
  - isCanonicalizedStructurallyValidLanguageTag
  - getLocaleBaseName
  - getInvalidLocaleArguments
  - testOption
  - testForUnwantedRegExpChanges
  - allCalendars
  - allCollations
  - allNumberingSystems
  - isValidNumberingSystem
  - numberingSystemDigits
  - allSimpleSanctionedUnits
  - testNumberFormat
  - getDateTimeComponents
  - getDateTimeComponentValues
  - isCanonicalizedStructurallyValidTimeZoneName
  - partitionDurationFormatPattern
  - formatDurationFormatPattern
---*/
/**
 */


/**
 * @description Calls the provided function for every service constructor in
 * the Intl object.
 * @param {Function} f the function to call for each service constructor in
 *   the Intl object.
 *   @param {Function} Constructor the constructor object to test with.
 */
function testWithIntlConstructors(f) {
  var constructors = ["Collator", "NumberFormat", "DateTimeFormat"];

  // Optionally supported Intl constructors.
  // NB: Intl.Locale isn't an Intl service constructor!
  // Intl.DisplayNames cannot be called without type in options.
  ["PluralRules", "RelativeTimeFormat", "ListFormat"].forEach(function(constructor) {
    if (typeof Intl[constructor] === "function") {
      constructors[constructors.length] = constructor;
    }
  });

  constructors.forEach(function (constructor) {
    var Constructor = Intl[constructor];
    try {
      f(Constructor);
    } catch (e) {
      e.message += " (Testing with " + constructor + ".)";
      throw e;
    }
  });
}


/**
 * Taints a named data property of the given object by installing
 * a setter that throws an exception.
 * @param {object} obj the object whose data property to taint
 * @param {string} property the property to taint
 */
function taintDataProperty(obj, property) {
  Object.defineProperty(obj, property, {
    set: function(value) {
      throw new Test262Error("Client code can adversely affect behavior: setter for " + property + ".");
    },
    enumerable: false,
    configurable: true
  });
}


/**
 * Taints a named method of the given object by replacing it with a function
 * that throws an exception.
 * @param {object} obj the object whose method to taint
 * @param {string} property the name of the method to taint
 */
function taintMethod(obj, property) {
  Object.defineProperty(obj, property, {
    value: function() {
      throw new Test262Error("Client code can adversely affect behavior: method " + property + ".");
    },
    writable: true,
    enumerable: false,
    configurable: true
  });
}


/**
 * Taints the given properties (and similarly named properties) by installing
 * setters on Object.prototype that throw exceptions.
 * @param {Array} properties an array of property names to taint
 */
function taintProperties(properties) {
  properties.forEach(function (property) {
    var adaptedProperties = [property, "__" + property, "_" + property, property + "_", property + "__"];
    adaptedProperties.forEach(function (property) {
      taintDataProperty(Object.prototype, property);
    });
  });
}


/**
 * Taints the Array object by creating a setter for the property "0" and
 * replacing some key methods with functions that throw exceptions.
 */
function taintArray() {
  taintDataProperty(Array.prototype, "0");
  taintMethod(Array.prototype, "indexOf");
  taintMethod(Array.prototype, "join");
  taintMethod(Array.prototype, "push");
  taintMethod(Array.prototype, "slice");
  taintMethod(Array.prototype, "sort");
}


/**
 * Gets locale support info for the given constructor object, which must be one
 * of Intl constructors.
 * @param {object} Constructor the constructor for which to get locale support info
 * @param {object} options the options while calling the constructor
 * @return {object} locale support info with the following properties:
 *   supported: array of fully supported language tags
 *   byFallback: array of language tags that are supported through fallbacks
 *   unsupported: array of unsupported language tags
 */
function getLocaleSupportInfo(Constructor, options) {
  var languages = ["zh", "es", "en", "hi", "ur", "ar", "ja", "pa"];
  var scripts = ["Latn", "Hans", "Deva", "Arab", "Jpan", "Hant", "Guru"];
  var countries = ["CN", "IN", "US", "PK", "JP", "TW", "HK", "SG", "419"];

  var allTags = [];
  var i, j, k;
  var language, script, country;
  for (i = 0; i < languages.length; i++) {
    language = languages[i];
    allTags.push(language);
    for (j = 0; j < scripts.length; j++) {
      script = scripts[j];
      allTags.push(language + "-" + script);
      for (k = 0; k < countries.length; k++) {
        country = countries[k];
        allTags.push(language + "-" + script + "-" + country);
      }
    }
    for (k = 0; k < countries.length; k++) {
      country = countries[k];
      allTags.push(language + "-" + country);
    }
  }

  var supported = [];
  var byFallback = [];
  var unsupported = [];
  for (i = 0; i < allTags.length; i++) {
    var request = allTags[i];
    var result = new Constructor([request], options).resolvedOptions().locale;
    if (request === result) {
      supported.push(request);
    } else if (request.indexOf(result) === 0) {
      byFallback.push(request);
    } else {
      unsupported.push(request);
    }
  }

  return {
    supported: supported,
    byFallback: byFallback,
    unsupported: unsupported
  };
}


/**
 * Returns an array of strings for which IsStructurallyValidLanguageTag() returns false
 */
function getInvalidLanguageTags() {
  var invalidLanguageTags = [
    "", // empty tag
    "i", // singleton alone
    "x", // private use without subtag
    "u", // extension singleton in first place
    "419", // region code in first place
    "u-nu-latn-cu-bob", // extension sequence without language
    "hans-cmn-cn", // "hans" could theoretically be a 4-letter language code,
                   // but those can't be followed by extlang codes.
    "cmn-hans-cn-u-u", // duplicate singleton
    "cmn-hans-cn-t-u-ca-u", // duplicate singleton
    "de-gregory-gregory", // duplicate variant
    "*", // language range
    "de-*", // language range
    "中文", // non-ASCII letters
    "en-ß", // non-ASCII letters
    "ıd", // non-ASCII letters
    "es-Latn-latn", // two scripts
    "pl-PL-pl", // two regions
    "u-ca-gregory", // extension in first place
    "de-1996-1996", // duplicate numeric variant
    "pt-u-ca-gregory-u-nu-latn", // duplicate singleton subtag

    // Invalid tags starting with: https://github.com/tc39/ecma402/pull/289
    "no-nyn", // regular grandfathered in BCP47, but invalid in UTS35
    "i-klingon", // irregular grandfathered in BCP47, but invalid in UTS35
    "zh-hak-CN", // language with extlang in BCP47, but invalid in UTS35
    "sgn-ils", // language with extlang in BCP47, but invalid in UTS35
    "x-foo", // privateuse-only in BCP47, but invalid in UTS35
    "x-en-US-12345", // more privateuse-only variants.
    "x-12345-12345-en-US",
    "x-en-US-12345-12345",
    "x-en-u-foo",
    "x-en-u-foo-u-bar",
    "x-u-foo",

    // underscores in different parts of the language tag
    "de_DE",
    "DE_de",
    "cmn_Hans",
    "cmn-hans_cn",
    "es_419",
    "es-419-u-nu-latn-cu_bob",
    "i_klingon",
    "cmn-hans-cn-t-ca-u-ca-x_t-u",
    "enochian_enochian",
    "de-gregory_u-ca-gregory",

    "en\u0000", // null-terminator sequence
    " en", // leading whitespace
    "en ", // trailing whitespace
    "it-IT-Latn", // country before script tag
    "de-u", // incomplete Unicode extension sequences
    "de-u-",
    "de-u-ca-",
    "de-u-ca-gregory-",
    "si-x", // incomplete private-use tags
    "x-",
    "x-y-",
  ];

  // make sure the data above is correct
  for (var i = 0; i < invalidLanguageTags.length; ++i) {
    var invalidTag = invalidLanguageTags[i];
    assert(
      !isCanonicalizedStructurallyValidLanguageTag(invalidTag),
      "Test data \"" + invalidTag + "\" is a canonicalized and structurally valid language tag."
    );
  }

  return invalidLanguageTags;
}


/**
 * @description Tests whether locale is a String value representing a
 * structurally valid and canonicalized BCP 47 language tag, as defined in
 * sections 6.2.2 and 6.2.3 of the ECMAScript Internationalization API
 * Specification.
 * @param {String} locale the string to be tested.
 * @result {Boolean} whether the test succeeded.
 */
function isCanonicalizedStructurallyValidLanguageTag(locale) {

  /**
   * Regular expression defining Unicode BCP 47 Locale Identifiers.
   *
   * Spec: https://unicode.org/reports/tr35/#Unicode_locale_identifier
   */
  var alpha = "[a-z]",
    digit = "[0-9]",
    alphanum = "[a-z0-9]",
    variant = "(" + alphanum + "{5,8}|(?:" + digit + alphanum + "{3}))",
    region = "(" + alpha + "{2}|" + digit + "{3})",
    script = "(" + alpha + "{4})",
    language = "(" + alpha + "{2,3}|" + alpha + "{5,8})",
    privateuse = "(x(-[a-z0-9]{1,8})+)",
    singleton = "(" + digit + "|[a-wy-z])",
    attribute= "(" + alphanum + "{3,8})",
    keyword = "(" + alphanum + alpha + "(-" + alphanum + "{3,8})*)",
    unicode_locale_extensions = "(u((-" + keyword + ")+|((-" + attribute + ")+(-" + keyword + ")*)))",
    tlang = "(" + language + "(-" + script + ")?(-" + region + ")?(-" + variant + ")*)",
    tfield = "(" + alpha + digit + "(-" + alphanum + "{3,8})+)",
    transformed_extensions = "(t((-" + tlang + "(-" + tfield + ")*)|(-" + tfield + ")+))",
    other_singleton = "(" + digit + "|[a-sv-wy-z])",
    other_extensions = "(" + other_singleton + "(-" + alphanum + "{2,8})+)",
    extension = "(" + unicode_locale_extensions + "|" + transformed_extensions + "|" + other_extensions + ")",
    locale_id = language + "(-" + script + ")?(-" + region + ")?(-" + variant + ")*(-" + extension + ")*(-" + privateuse + ")?",
    languageTag = "^(" + locale_id + ")$",
    languageTagRE = new RegExp(languageTag, "i");

  var duplicateSingleton = "-" + singleton + "-(.*-)?\\1(?!" + alphanum + ")",
    duplicateSingletonRE = new RegExp(duplicateSingleton, "i"),
    duplicateVariant = "(" + alphanum + "{2,8}-)+" + variant + "-(" + alphanum + "{2,8}-)*\\2(?!" + alphanum + ")",
    duplicateVariantRE = new RegExp(duplicateVariant, "i");

  var transformKeyRE = new RegExp("^" + alpha + digit + "$", "i");

  /**
   * Verifies that the given string is a well-formed Unicode BCP 47 Locale Identifier
   * with no duplicate variant or singleton subtags.
   *
   * Spec: ECMAScript Internationalization API Specification, draft, 6.2.2.
   */
  function isStructurallyValidLanguageTag(locale) {
    if (!languageTagRE.test(locale)) {
      return false;
    }
    locale = locale.split(/-x-/)[0];
    return !duplicateSingletonRE.test(locale) && !duplicateVariantRE.test(locale);
  }


  /**
   * Mappings from complete tags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __tagMappings = {
    // property names must be in lower case; values in canonical form

    "art-lojban": "jbo",
    "cel-gaulish": "xtg",
    "zh-guoyu": "zh",
    "zh-hakka": "hak",
    "zh-xiang": "hsn",
  };


  /**
   * Mappings from language subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __languageMappings = {
    // property names and values must be in canonical case

    "aam": "aas",
    "aar": "aa",
    "abk": "ab",
    "adp": "dz",
    "afr": "af",
    "aju": "jrb",
    "aka": "ak",
    "alb": "sq",
    "als": "sq",
    "amh": "am",
    "ara": "ar",
    "arb": "ar",
    "arg": "an",
    "arm": "hy",
    "asd": "snz",
    "asm": "as",
    "aue": "ktz",
    "ava": "av",
    "ave": "ae",
    "aym": "ay",
    "ayr": "ay",
    "ayx": "nun",
    "aze": "az",
    "azj": "az",
    "bak": "ba",
    "bam": "bm",
    "baq": "eu",
    "bcc": "bal",
    "bcl": "bik",
    "bel": "be",
    "ben": "bn",
    "bgm": "bcg",
    "bh": "bho",
    "bih": "bho",
    "bis": "bi",
    "bjd": "drl",
    "bod": "bo",
    "bos": "bs",
    "bre": "br",
    "bul": "bg",
    "bur": "my",
    "bxk": "luy",
    "bxr": "bua",
    "cat": "ca",
    "ccq": "rki",
    "ces": "cs",
    "cha": "ch",
    "che": "ce",
    "chi": "zh",
    "chu": "cu",
    "chv": "cv",
    "cjr": "mom",
    "cka": "cmr",
    "cld": "syr",
    "cmk": "xch",
    "cmn": "zh",
    "cor": "kw",
    "cos": "co",
    "coy": "pij",
    "cqu": "quh",
    "cre": "cr",
    "cwd": "cr",
    "cym": "cy",
    "cze": "cs",
    "dan": "da",
    "deu": "de",
    "dgo": "doi",
    "dhd": "mwr",
    "dik": "din",
    "diq": "zza",
    "dit": "dif",
    "div": "dv",
    "drh": "mn",
    "dut": "nl",
    "dzo": "dz",
    "ekk": "et",
    "ell": "el",
    "emk": "man",
    "eng": "en",
    "epo": "eo",
    "esk": "ik",
    "est": "et",
    "eus": "eu",
    "ewe": "ee",
    "fao": "fo",
    "fas": "fa",
    "fat": "ak",
    "fij": "fj",
    "fin": "fi",
    "fra": "fr",
    "fre": "fr",
    "fry": "fy",
    "fuc": "ff",
    "ful": "ff",
    "gav": "dev",
    "gaz": "om",
    "gbo": "grb",
    "geo": "ka",
    "ger": "de",
    "gfx": "vaj",
    "ggn": "gvr",
    "gla": "gd",
    "gle": "ga",
    "glg": "gl",
    "glv": "gv",
    "gno": "gon",
    "gre": "el",
    "grn": "gn",
    "gti": "nyc",
    "gug": "gn",
    "guj": "gu",
    "guv": "duz",
    "gya": "gba",
    "hat": "ht",
    "hau": "ha",
    "hdn": "hai",
    "hea": "hmn",
    "heb": "he",
    "her": "hz",
    "him": "srx",
    "hin": "hi",
    "hmo": "ho",
    "hrr": "jal",
    "hrv": "hr",
    "hun": "hu",
    "hye": "hy",
    "ibi": "opa",
    "ibo": "ig",
    "ice": "is",
    "ido": "io",
    "iii": "ii",
    "ike": "iu",
    "iku": "iu",
    "ile": "ie",
    "ilw": "gal",
    "in": "id",
    "ina": "ia",
    "ind": "id",
    "ipk": "ik",
    "isl": "is",
    "ita": "it",
    "iw": "he",
    "jav": "jv",
    "jeg": "oyb",
    "ji": "yi",
    "jpn": "ja",
    "jw": "jv",
    "kal": "kl",
    "kan": "kn",
    "kas": "ks",
    "kat": "ka",
    "kau": "kr",
    "kaz": "kk",
    "kgc": "tdf",
    "kgh": "kml",
    "khk": "mn",
    "khm": "km",
    "kik": "ki",
    "kin": "rw",
    "kir": "ky",
    "kmr": "ku",
    "knc": "kr",
    "kng": "kg",
    "knn": "kok",
    "koj": "kwv",
    "kom": "kv",
    "kon": "kg",
    "kor": "ko",
    "kpv": "kv",
    "krm": "bmf",
    "ktr": "dtp",
    "kua": "kj",
    "kur": "ku",
    "kvs": "gdj",
    "kwq": "yam",
    "kxe": "tvd",
    "kzj": "dtp",
    "kzt": "dtp",
    "lao": "lo",
    "lat": "la",
    "lav": "lv",
    "lbk": "bnc",
    "lii": "raq",
    "lim": "li",
    "lin": "ln",
    "lit": "lt",
    "llo": "ngt",
    "lmm": "rmx",
    "ltz": "lb",
    "lub": "lu",
    "lug": "lg",
    "lvs": "lv",
    "mac": "mk",
    "mah": "mh",
    "mal": "ml",
    "mao": "mi",
    "mar": "mr",
    "may": "ms",
    "meg": "cir",
    "mhr": "chm",
    "mkd": "mk",
    "mlg": "mg",
    "mlt": "mt",
    "mnk": "man",
    "mo": "ro",
    "mol": "ro",
    "mon": "mn",
    "mri": "mi",
    "msa": "ms",
    "mst": "mry",
    "mup": "raj",
    "mwj": "vaj",
    "mya": "my",
    "myd": "aog",
    "myt": "mry",
    "nad": "xny",
    "nau": "na",
    "nav": "nv",
    "nbl": "nr",
    "ncp": "kdz",
    "nde": "nd",
    "ndo": "ng",
    "nep": "ne",
    "nld": "nl",
    "nno": "nn",
    "nns": "nbr",
    "nnx": "ngv",
    "no": "nb",
    "nob": "nb",
    "nor": "nb",
    "npi": "ne",
    "nts": "pij",
    "nya": "ny",
    "oci": "oc",
    "ojg": "oj",
    "oji": "oj",
    "ori": "or",
    "orm": "om",
    "ory": "or",
    "oss": "os",
    "oun": "vaj",
    "pan": "pa",
    "pbu": "ps",
    "pcr": "adx",
    "per": "fa",
    "pes": "fa",
    "pli": "pi",
    "plt": "mg",
    "pmc": "huw",
    "pmu": "phr",
    "pnb": "lah",
    "pol": "pl",
    "por": "pt",
    "ppa": "bfy",
    "ppr": "lcq",
    "pry": "prt",
    "pus": "ps",
    "puz": "pub",
    "que": "qu",
    "quz": "qu",
    "rmy": "rom",
    "roh": "rm",
    "ron": "ro",
    "rum": "ro",
    "run": "rn",
    "rus": "ru",
    "sag": "sg",
    "san": "sa",
    "sca": "hle",
    "scc": "sr",
    "scr": "hr",
    "sin": "si",
    "skk": "oyb",
    "slk": "sk",
    "slo": "sk",
    "slv": "sl",
    "sme": "se",
    "smo": "sm",
    "sna": "sn",
    "snd": "sd",
    "som": "so",
    "sot": "st",
    "spa": "es",
    "spy": "kln",
    "sqi": "sq",
    "src": "sc",
    "srd": "sc",
    "srp": "sr",
    "ssw": "ss",
    "sun": "su",
    "swa": "sw",
    "swe": "sv",
    "swh": "sw",
    "tah": "ty",
    "tam": "ta",
    "tat": "tt",
    "tdu": "dtp",
    "tel": "te",
    "tgk": "tg",
    "tgl": "fil",
    "tha": "th",
    "thc": "tpo",
    "thx": "oyb",
    "tib": "bo",
    "tie": "ras",
    "tir": "ti",
    "tkk": "twm",
    "tl": "fil",
    "tlw": "weo",
    "tmp": "tyj",
    "tne": "kak",
    "ton": "to",
    "tsf": "taj",
    "tsn": "tn",
    "tso": "ts",
    "ttq": "tmh",
    "tuk": "tk",
    "tur": "tr",
    "tw": "ak",
    "twi": "ak",
    "uig": "ug",
    "ukr": "uk",
    "umu": "del",
    "uok": "ema",
    "urd": "ur",
    "uzb": "uz",
    "uzn": "uz",
    "ven": "ve",
    "vie": "vi",
    "vol": "vo",
    "wel": "cy",
    "wln": "wa",
    "wol": "wo",
    "xba": "cax",
    "xho": "xh",
    "xia": "acn",
    "xkh": "waw",
    "xpe": "kpe",
    "xsj": "suj",
    "xsl": "den",
    "ybd": "rki",
    "ydd": "yi",
    "yid": "yi",
    "yma": "lrr",
    "ymt": "mtm",
    "yor": "yo",
    "yos": "zom",
    "yuu": "yug",
    "zai": "zap",
    "zha": "za",
    "zho": "zh",
    "zsm": "ms",
    "zul": "zu",
    "zyb": "za",
  };


  /**
   * Mappings from region subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __regionMappings = {
    // property names and values must be in canonical case

    "004": "AF",
    "008": "AL",
    "010": "AQ",
    "012": "DZ",
    "016": "AS",
    "020": "AD",
    "024": "AO",
    "028": "AG",
    "031": "AZ",
    "032": "AR",
    "036": "AU",
    "040": "AT",
    "044": "BS",
    "048": "BH",
    "050": "BD",
    "051": "AM",
    "052": "BB",
    "056": "BE",
    "060": "BM",
    "062": "034",
    "064": "BT",
    "068": "BO",
    "070": "BA",
    "072": "BW",
    "074": "BV",
    "076": "BR",
    "084": "BZ",
    "086": "IO",
    "090": "SB",
    "092": "VG",
    "096": "BN",
    "100": "BG",
    "104": "MM",
    "108": "BI",
    "112": "BY",
    "116": "KH",
    "120": "CM",
    "124": "CA",
    "132": "CV",
    "136": "KY",
    "140": "CF",
    "144": "LK",
    "148": "TD",
    "152": "CL",
    "156": "CN",
    "158": "TW",
    "162": "CX",
    "166": "CC",
    "170": "CO",
    "174": "KM",
    "175": "YT",
    "178": "CG",
    "180": "CD",
    "184": "CK",
    "188": "CR",
    "191": "HR",
    "192": "CU",
    "196": "CY",
    "203": "CZ",
    "204": "BJ",
    "208": "DK",
    "212": "DM",
    "214": "DO",
    "218": "EC",
    "222": "SV",
    "226": "GQ",
    "230": "ET",
    "231": "ET",
    "232": "ER",
    "233": "EE",
    "234": "FO",
    "238": "FK",
    "239": "GS",
    "242": "FJ",
    "246": "FI",
    "248": "AX",
    "249": "FR",
    "250": "FR",
    "254": "GF",
    "258": "PF",
    "260": "TF",
    "262": "DJ",
    "266": "GA",
    "268": "GE",
    "270": "GM",
    "275": "PS",
    "276": "DE",
    "278": "DE",
    "280": "DE",
    "288": "GH",
    "292": "GI",
    "296": "KI",
    "300": "GR",
    "304": "GL",
    "308": "GD",
    "312": "GP",
    "316": "GU",
    "320": "GT",
    "324": "GN",
    "328": "GY",
    "332": "HT",
    "334": "HM",
    "336": "VA",
    "340": "HN",
    "344": "HK",
    "348": "HU",
    "352": "IS",
    "356": "IN",
    "360": "ID",
    "364": "IR",
    "368": "IQ",
    "372": "IE",
    "376": "IL",
    "380": "IT",
    "384": "CI",
    "388": "JM",
    "392": "JP",
    "398": "KZ",
    "400": "JO",
    "404": "KE",
    "408": "KP",
    "410": "KR",
    "414": "KW",
    "417": "KG",
    "418": "LA",
    "422": "LB",
    "426": "LS",
    "428": "LV",
    "430": "LR",
    "434": "LY",
    "438": "LI",
    "440": "LT",
    "442": "LU",
    "446": "MO",
    "450": "MG",
    "454": "MW",
    "458": "MY",
    "462": "MV",
    "466": "ML",
    "470": "MT",
    "474": "MQ",
    "478": "MR",
    "480": "MU",
    "484": "MX",
    "492": "MC",
    "496": "MN",
    "498": "MD",
    "499": "ME",
    "500": "MS",
    "504": "MA",
    "508": "MZ",
    "512": "OM",
    "516": "NA",
    "520": "NR",
    "524": "NP",
    "528": "NL",
    "531": "CW",
    "533": "AW",
    "534": "SX",
    "535": "BQ",
    "540": "NC",
    "548": "VU",
    "554": "NZ",
    "558": "NI",
    "562": "NE",
    "566": "NG",
    "570": "NU",
    "574": "NF",
    "578": "NO",
    "580": "MP",
    "581": "UM",
    "583": "FM",
    "584": "MH",
    "585": "PW",
    "586": "PK",
    "591": "PA",
    "598": "PG",
    "600": "PY",
    "604": "PE",
    "608": "PH",
    "612": "PN",
    "616": "PL",
    "620": "PT",
    "624": "GW",
    "626": "TL",
    "630": "PR",
    "634": "QA",
    "638": "RE",
    "642": "RO",
    "643": "RU",
    "646": "RW",
    "652": "BL",
    "654": "SH",
    "659": "KN",
    "660": "AI",
    "662": "LC",
    "663": "MF",
    "666": "PM",
    "670": "VC",
    "674": "SM",
    "678": "ST",
    "682": "SA",
    "686": "SN",
    "688": "RS",
    "690": "SC",
    "694": "SL",
    "702": "SG",
    "703": "SK",
    "704": "VN",
    "705": "SI",
    "706": "SO",
    "710": "ZA",
    "716": "ZW",
    "720": "YE",
    "724": "ES",
    "728": "SS",
    "729": "SD",
    "732": "EH",
    "736": "SD",
    "740": "SR",
    "744": "SJ",
    "748": "SZ",
    "752": "SE",
    "756": "CH",
    "760": "SY",
    "762": "TJ",
    "764": "TH",
    "768": "TG",
    "772": "TK",
    "776": "TO",
    "780": "TT",
    "784": "AE",
    "788": "TN",
    "792": "TR",
    "795": "TM",
    "796": "TC",
    "798": "TV",
    "800": "UG",
    "804": "UA",
    "807": "MK",
    "818": "EG",
    "826": "GB",
    "830": "JE",
    "831": "GG",
    "832": "JE",
    "833": "IM",
    "834": "TZ",
    "840": "US",
    "850": "VI",
    "854": "BF",
    "858": "UY",
    "860": "UZ",
    "862": "VE",
    "876": "WF",
    "882": "WS",
    "886": "YE",
    "887": "YE",
    "891": "RS",
    "894": "ZM",
    "958": "AA",
    "959": "QM",
    "960": "QN",
    "962": "QP",
    "963": "QQ",
    "964": "QR",
    "965": "QS",
    "966": "QT",
    "967": "EU",
    "968": "QV",
    "969": "QW",
    "970": "QX",
    "971": "QY",
    "972": "QZ",
    "973": "XA",
    "974": "XB",
    "975": "XC",
    "976": "XD",
    "977": "XE",
    "978": "XF",
    "979": "XG",
    "980": "XH",
    "981": "XI",
    "982": "XJ",
    "983": "XK",
    "984": "XL",
    "985": "XM",
    "986": "XN",
    "987": "XO",
    "988": "XP",
    "989": "XQ",
    "990": "XR",
    "991": "XS",
    "992": "XT",
    "993": "XU",
    "994": "XV",
    "995": "XW",
    "996": "XX",
    "997": "XY",
    "998": "XZ",
    "999": "ZZ",
    "BU": "MM",
    "CS": "RS",
    "CT": "KI",
    "DD": "DE",
    "DY": "BJ",
    "FQ": "AQ",
    "FX": "FR",
    "HV": "BF",
    "JT": "UM",
    "MI": "UM",
    "NH": "VU",
    "NQ": "AQ",
    "PU": "UM",
    "PZ": "PA",
    "QU": "EU",
    "RH": "ZW",
    "TP": "TL",
    "UK": "GB",
    "VD": "VN",
    "WK": "UM",
    "YD": "YE",
    "YU": "RS",
    "ZR": "CD",
  };


  /**
   * Complex mappings from language subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __complexLanguageMappings = {
    // property names and values must be in canonical case

    "cnr": {language: "sr", region: "ME"},
    "drw": {language: "fa", region: "AF"},
    "hbs": {language: "sr", script: "Latn"},
    "prs": {language: "fa", region: "AF"},
    "sh": {language: "sr", script: "Latn"},
    "swc": {language: "sw", region: "CD"},
    "tnf": {language: "fa", region: "AF"},
  };


  /**
   * Complex mappings from region subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __complexRegionMappings = {
    // property names and values must be in canonical case

    "172": {
      default: "RU",
      "ab": "GE",
      "az": "AZ",
      "be": "BY",
      "crh": "UA",
      "gag": "MD",
      "got": "UA",
      "hy": "AM",
      "ji": "UA",
      "ka": "GE",
      "kaa": "UZ",
      "kk": "KZ",
      "ku-Yezi": "GE",
      "ky": "KG",
      "os": "GE",
      "rue": "UA",
      "sog": "UZ",
      "tg": "TJ",
      "tk": "TM",
      "tkr": "AZ",
      "tly": "AZ",
      "ttt": "AZ",
      "ug-Cyrl": "KZ",
      "uk": "UA",
      "und-Armn": "AM",
      "und-Chrs": "UZ",
      "und-Geor": "GE",
      "und-Goth": "UA",
      "und-Sogd": "UZ",
      "und-Sogo": "UZ",
      "und-Yezi": "GE",
      "uz": "UZ",
      "xco": "UZ",
      "xmf": "GE",
    },
    "200": {
      default: "CZ",
      "sk": "SK",
    },
    "530": {
      default: "CW",
      "vic": "SX",
    },
    "532": {
      default: "CW",
      "vic": "SX",
    },
    "536": {
      default: "SA",
      "akk": "IQ",
      "ckb": "IQ",
      "ku-Arab": "IQ",
      "mis": "IQ",
      "syr": "IQ",
      "und-Hatr": "IQ",
      "und-Syrc": "IQ",
      "und-Xsux": "IQ",
    },
    "582": {
      default: "FM",
      "mh": "MH",
      "pau": "PW",
    },
    "810": {
      default: "RU",
      "ab": "GE",
      "az": "AZ",
      "be": "BY",
      "crh": "UA",
      "et": "EE",
      "gag": "MD",
      "got": "UA",
      "hy": "AM",
      "ji": "UA",
      "ka": "GE",
      "kaa": "UZ",
      "kk": "KZ",
      "ku-Yezi": "GE",
      "ky": "KG",
      "lt": "LT",
      "ltg": "LV",
      "lv": "LV",
      "os": "GE",
      "rue": "UA",
      "sgs": "LT",
      "sog": "UZ",
      "tg": "TJ",
      "tk": "TM",
      "tkr": "AZ",
      "tly": "AZ",
      "ttt": "AZ",
      "ug-Cyrl": "KZ",
      "uk": "UA",
      "und-Armn": "AM",
      "und-Chrs": "UZ",
      "und-Geor": "GE",
      "und-Goth": "UA",
      "und-Sogd": "UZ",
      "und-Sogo": "UZ",
      "und-Yezi": "GE",
      "uz": "UZ",
      "vro": "EE",
      "xco": "UZ",
      "xmf": "GE",
    },
    "890": {
      default: "RS",
      "bs": "BA",
      "hr": "HR",
      "mk": "MK",
      "sl": "SI",
    },
    "AN": {
      default: "CW",
      "vic": "SX",
    },
    "NT": {
      default: "SA",
      "akk": "IQ",
      "ckb": "IQ",
      "ku-Arab": "IQ",
      "mis": "IQ",
      "syr": "IQ",
      "und-Hatr": "IQ",
      "und-Syrc": "IQ",
      "und-Xsux": "IQ",
    },
    "PC": {
      default: "FM",
      "mh": "MH",
      "pau": "PW",
    },
    "SU": {
      default: "RU",
      "ab": "GE",
      "az": "AZ",
      "be": "BY",
      "crh": "UA",
      "et": "EE",
      "gag": "MD",
      "got": "UA",
      "hy": "AM",
      "ji": "UA",
      "ka": "GE",
      "kaa": "UZ",
      "kk": "KZ",
      "ku-Yezi": "GE",
      "ky": "KG",
      "lt": "LT",
      "ltg": "LV",
      "lv": "LV",
      "os": "GE",
      "rue": "UA",
      "sgs": "LT",
      "sog": "UZ",
      "tg": "TJ",
      "tk": "TM",
      "tkr": "AZ",
      "tly": "AZ",
      "ttt": "AZ",
      "ug-Cyrl": "KZ",
      "uk": "UA",
      "und-Armn": "AM",
      "und-Chrs": "UZ",
      "und-Geor": "GE",
      "und-Goth": "UA",
      "und-Sogd": "UZ",
      "und-Sogo": "UZ",
      "und-Yezi": "GE",
      "uz": "UZ",
      "vro": "EE",
      "xco": "UZ",
      "xmf": "GE",
    },
  };


  /**
   * Mappings from variant subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __variantMappings = {
    // property names and values must be in canonical case

    "aaland": {type: "region", replacement: "AX"},
    "arevela": {type: "language", replacement: "hy"},
    "arevmda": {type: "language", replacement: "hyw"},
    "heploc": {type: "variant", replacement: "alalc97"},
    "polytoni": {type: "variant", replacement: "polyton"},
  };


  /**
   * Mappings from Unicode extension subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __unicodeMappings = {
    // property names and values must be in canonical case

    "ca": {
      "ethiopic-amete-alem": "ethioaa",
      "islamicc": "islamic-civil",
    },
    "kb": {
      "yes": "true",
    },
    "kc": {
      "yes": "true",
    },
    "kh": {
      "yes": "true",
    },
    "kk": {
      "yes": "true",
    },
    "kn": {
      "yes": "true",
    },
    "ks": {
      "primary": "level1",
      "tertiary": "level3",
    },
    "ms": {
      "imperial": "uksystem",
    },
    "rg": {
      "cn11": "cnbj",
      "cn12": "cntj",
      "cn13": "cnhe",
      "cn14": "cnsx",
      "cn15": "cnmn",
      "cn21": "cnln",
      "cn22": "cnjl",
      "cn23": "cnhl",
      "cn31": "cnsh",
      "cn32": "cnjs",
      "cn33": "cnzj",
      "cn34": "cnah",
      "cn35": "cnfj",
      "cn36": "cnjx",
      "cn37": "cnsd",
      "cn41": "cnha",
      "cn42": "cnhb",
      "cn43": "cnhn",
      "cn44": "cngd",
      "cn45": "cngx",
      "cn46": "cnhi",
      "cn50": "cncq",
      "cn51": "cnsc",
      "cn52": "cngz",
      "cn53": "cnyn",
      "cn54": "cnxz",
      "cn61": "cnsn",
      "cn62": "cngs",
      "cn63": "cnqh",
      "cn64": "cnnx",
      "cn65": "cnxj",
      "cz10a": "cz110",
      "cz10b": "cz111",
      "cz10c": "cz112",
      "cz10d": "cz113",
      "cz10e": "cz114",
      "cz10f": "cz115",
      "cz611": "cz663",
      "cz612": "cz632",
      "cz613": "cz633",
      "cz614": "cz634",
      "cz615": "cz635",
      "cz621": "cz641",
      "cz622": "cz642",
      "cz623": "cz643",
      "cz624": "cz644",
      "cz626": "cz646",
      "cz627": "cz647",
      "czjc": "cz31",
      "czjm": "cz64",
      "czka": "cz41",
      "czkr": "cz52",
      "czli": "cz51",
      "czmo": "cz80",
      "czol": "cz71",
      "czpa": "cz53",
      "czpl": "cz32",
      "czpr": "cz10",
      "czst": "cz20",
      "czus": "cz42",
      "czvy": "cz63",
      "czzl": "cz72",
      "fra": "frges",
      "frb": "frnaq",
      "frc": "frara",
      "frd": "frbfc",
      "fre": "frbre",
      "frf": "frcvl",
      "frg": "frges",
      "frh": "frcor",
      "fri": "frbfc",
      "frj": "fridf",
      "frk": "frocc",
      "frl": "frnaq",
      "frm": "frges",
      "frn": "frocc",
      "fro": "frhdf",
      "frp": "frnor",
      "frq": "frnor",
      "frr": "frpdl",
      "frs": "frhdf",
      "frt": "frnaq",
      "fru": "frpac",
      "frv": "frara",
      "laxn": "laxs",
      "lud": "lucl",
      "lug": "luec",
      "lul": "luca",
      "mrnkc": "mr13",
      "no23": "no50",
      "nzn": "nzauk",
      "nzs": "nzcan",
      "omba": "ombj",
      "omsh": "omsj",
      "plds": "pl02",
      "plkp": "pl04",
      "pllb": "pl08",
      "plld": "pl10",
      "pllu": "pl06",
      "plma": "pl12",
      "plmz": "pl14",
      "plop": "pl16",
      "plpd": "pl20",
      "plpk": "pl18",
      "plpm": "pl22",
      "plsk": "pl26",
      "plsl": "pl24",
      "plwn": "pl28",
      "plwp": "pl30",
      "plzp": "pl32",
      "tteto": "tttob",
      "ttrcm": "ttmrc",
      "ttwto": "tttob",
      "twkhq": "twkhh",
      "twtnq": "twtnn",
      "twtpq": "twnwt",
      "twtxq": "twtxg",
    },
    "sd": {
      "cn11": "cnbj",
      "cn12": "cntj",
      "cn13": "cnhe",
      "cn14": "cnsx",
      "cn15": "cnmn",
      "cn21": "cnln",
      "cn22": "cnjl",
      "cn23": "cnhl",
      "cn31": "cnsh",
      "cn32": "cnjs",
      "cn33": "cnzj",
      "cn34": "cnah",
      "cn35": "cnfj",
      "cn36": "cnjx",
      "cn37": "cnsd",
      "cn41": "cnha",
      "cn42": "cnhb",
      "cn43": "cnhn",
      "cn44": "cngd",
      "cn45": "cngx",
      "cn46": "cnhi",
      "cn50": "cncq",
      "cn51": "cnsc",
      "cn52": "cngz",
      "cn53": "cnyn",
      "cn54": "cnxz",
      "cn61": "cnsn",
      "cn62": "cngs",
      "cn63": "cnqh",
      "cn64": "cnnx",
      "cn65": "cnxj",
      "cz10a": "cz110",
      "cz10b": "cz111",
      "cz10c": "cz112",
      "cz10d": "cz113",
      "cz10e": "cz114",
      "cz10f": "cz115",
      "cz611": "cz663",
      "cz612": "cz632",
      "cz613": "cz633",
      "cz614": "cz634",
      "cz615": "cz635",
      "cz621": "cz641",
      "cz622": "cz642",
      "cz623": "cz643",
      "cz624": "cz644",
      "cz626": "cz646",
      "cz627": "cz647",
      "czjc": "cz31",
      "czjm": "cz64",
      "czka": "cz41",
      "czkr": "cz52",
      "czli": "cz51",
      "czmo": "cz80",
      "czol": "cz71",
      "czpa": "cz53",
      "czpl": "cz32",
      "czpr": "cz10",
      "czst": "cz20",
      "czus": "cz42",
      "czvy": "cz63",
      "czzl": "cz72",
      "fra": "frges",
      "frb": "frnaq",
      "frc": "frara",
      "frd": "frbfc",
      "fre": "frbre",
      "frf": "frcvl",
      "frg": "frges",
      "frh": "frcor",
      "fri": "frbfc",
      "frj": "fridf",
      "frk": "frocc",
      "frl": "frnaq",
      "frm": "frges",
      "frn": "frocc",
      "fro": "frhdf",
      "frp": "frnor",
      "frq": "frnor",
      "frr": "frpdl",
      "frs": "frhdf",
      "frt": "frnaq",
      "fru": "frpac",
      "frv": "frara",
      "laxn": "laxs",
      "lud": "lucl",
      "lug": "luec",
      "lul": "luca",
      "mrnkc": "mr13",
      "no23": "no50",
      "nzn": "nzauk",
      "nzs": "nzcan",
      "omba": "ombj",
      "omsh": "omsj",
      "plds": "pl02",
      "plkp": "pl04",
      "pllb": "pl08",
      "plld": "pl10",
      "pllu": "pl06",
      "plma": "pl12",
      "plmz": "pl14",
      "plop": "pl16",
      "plpd": "pl20",
      "plpk": "pl18",
      "plpm": "pl22",
      "plsk": "pl26",
      "plsl": "pl24",
      "plwn": "pl28",
      "plwp": "pl30",
      "plzp": "pl32",
      "tteto": "tttob",
      "ttrcm": "ttmrc",
      "ttwto": "tttob",
      "twkhq": "twkhh",
      "twtnq": "twtnn",
      "twtpq": "twnwt",
      "twtxq": "twtxg",
    },
    "tz": {
      "aqams": "nzakl",
      "cnckg": "cnsha",
      "cnhrb": "cnsha",
      "cnkhg": "cnurc",
      "cuba": "cuhav",
      "egypt": "egcai",
      "eire": "iedub",
      "est": "utcw05",
      "gmt0": "gmt",
      "hongkong": "hkhkg",
      "hst": "utcw10",
      "iceland": "isrey",
      "iran": "irthr",
      "israel": "jeruslm",
      "jamaica": "jmkin",
      "japan": "jptyo",
      "libya": "lytip",
      "mst": "utcw07",
      "navajo": "usden",
      "poland": "plwaw",
      "portugal": "ptlis",
      "prc": "cnsha",
      "roc": "twtpe",
      "rok": "krsel",
      "turkey": "trist",
      "uct": "utc",
      "usnavajo": "usden",
      "zulu": "utc",
    },
  };


  /**
   * Mappings from Unicode extension subtags to preferred values.
   *
   * Spec: http://unicode.org/reports/tr35/#Identifiers
   * Version: CLDR, version 36.1
   */
  var __transformMappings = {
    // property names and values must be in canonical case

    "d0": {
      "name": "charname",
    },
    "m0": {
      "names": "prprname",
    },
  };

  /**
   * Canonicalizes the given well-formed BCP 47 language tag, including regularized case of subtags.
   *
   * Spec: ECMAScript Internationalization API Specification, draft, 6.2.3.
   * Spec: RFC 5646, section 4.5.
   */
  function canonicalizeLanguageTag(locale) {

    // start with lower case for easier processing, and because most subtags will need to be lower case anyway
    locale = locale.toLowerCase();

    // handle mappings for complete tags
    if (__tagMappings.hasOwnProperty(locale)) {
      return __tagMappings[locale];
    }

    var subtags = locale.split("-");
    var i = 0;

    // handle standard part: all subtags before first variant or singleton subtag
    var language;
    var script;
    var region;
    while (i < subtags.length) {
      var subtag = subtags[i];
      if (i === 0) {
        language = subtag;
      } else if (subtag.length === 2 || subtag.length === 3) {
        region = subtag.toUpperCase();
      } else if (subtag.length === 4 && !("0" <= subtag[0] && subtag[0] <= "9")) {
        script = subtag[0].toUpperCase() + subtag.substring(1).toLowerCase();
      } else {
        break;
      }
      i++;
    }

    if (__languageMappings.hasOwnProperty(language)) {
      language = __languageMappings[language];
    } else if (__complexLanguageMappings.hasOwnProperty(language)) {
      var mapping = __complexLanguageMappings[language];

      language = mapping.language;
      if (script === undefined && mapping.hasOwnProperty("script")) {
        script = mapping.script;
      }
      if (region === undefined && mapping.hasOwnProperty("region")) {
        region = mapping.region;
      }
    }

    if (region !== undefined) {
      if (__regionMappings.hasOwnProperty(region)) {
        region = __regionMappings[region];
      } else if (__complexRegionMappings.hasOwnProperty(region)) {
        var mapping = __complexRegionMappings[region];

        var mappingKey = language;
        if (script !== undefined) {
          mappingKey += "-" + script;
        }

        if (mapping.hasOwnProperty(mappingKey)) {
          region = mapping[mappingKey];
        } else {
          region = mapping.default;
        }
      }
    }

    // handle variants
    var variants = [];
    while (i < subtags.length && subtags[i].length > 1) {
      var variant = subtags[i];

      if (__variantMappings.hasOwnProperty(variant)) {
        var mapping = __variantMappings[variant];
        switch (mapping.type) {
          case "language":
            language = mapping.replacement;
            break;

          case "region":
            region = mapping.replacement;
            break;

          case "variant":
            variants.push(mapping.replacement);
            break;

          default:
            throw new Error("illegal variant mapping type");
        }
      } else {
        variants.push(variant);
      }

      i += 1;
    }
    variants.sort();

    // handle extensions
    var extensions = [];
    while (i < subtags.length && subtags[i] !== "x") {
      var extensionStart = i;
      i++;
      while (i < subtags.length && subtags[i].length > 1) {
        i++;
      }

      var extension;
      var extensionKey = subtags[extensionStart];
      if (extensionKey === "u") {
        var j = extensionStart + 1;

        // skip over leading attributes
        while (j < i && subtags[j].length > 2) {
          j++;
        }

        extension = subtags.slice(extensionStart, j).join("-");

        while (j < i) {
          var keyStart = j;
          j++;

          while (j < i && subtags[j].length > 2) {
            j++;
          }

          var key = subtags[keyStart];
          var value = subtags.slice(keyStart + 1, j).join("-");

          if (__unicodeMappings.hasOwnProperty(key)) {
            var mapping = __unicodeMappings[key];
            if (mapping.hasOwnProperty(value)) {
              value = mapping[value];
            }
          }

          extension += "-" + key;
          if (value !== "" && value !== "true") {
            extension += "-" + value;
          }
        }
      } else if (extensionKey === "t") {
        var j = extensionStart + 1;

        while (j < i && !transformKeyRE.test(subtags[j])) {
          j++;
        }

        extension = "t";

        var transformLanguage = subtags.slice(extensionStart + 1, j).join("-");
        if (transformLanguage !== "") {
          extension += "-" + canonicalizeLanguageTag(transformLanguage).toLowerCase();
        }

        while (j < i) {
          var keyStart = j;
          j++;

          while (j < i && subtags[j].length > 2) {
            j++;
          }

          var key = subtags[keyStart];
          var value = subtags.slice(keyStart + 1, j).join("-");

          if (__transformMappings.hasOwnProperty(key)) {
            var mapping = __transformMappings[key];
            if (mapping.hasOwnProperty(value)) {
              value = mapping[value];
            }
          }

          extension += "-" + key + "-" + value;
        }
      } else {
        extension = subtags.slice(extensionStart, i).join("-");
      }

      extensions.push(extension);
    }
    extensions.sort();

    // handle private use
    var privateUse;
    if (i < subtags.length) {
      privateUse = subtags.slice(i).join("-");
    }

    // put everything back together
    var canonical = language;
    if (script !== undefined) {
      canonical += "-" + script;
    }
    if (region !== undefined) {
      canonical += "-" + region;
    }
    if (variants.length > 0) {
      canonical += "-" + variants.join("-");
    }
    if (extensions.length > 0) {
      canonical += "-" + extensions.join("-");
    }
    if (privateUse !== undefined) {
      if (canonical.length > 0) {
        canonical += "-" + privateUse;
      } else {
        canonical = privateUse;
      }
    }

    return canonical;
  }

  return typeof locale === "string" && isStructurallyValidLanguageTag(locale) &&
      canonicalizeLanguageTag(locale) === locale;
}


/**
 * Returns the given language tag without any extension or private use
 * subtags, like Intl.Locale.prototype.baseName. Use this before appending a
 * Unicode extension sequence to a locale obtained from resolvedOptions():
 * some hosts report a default locale that already carries one, such as
 * "en-US-u-va-posix" under the POSIX locale, and a language tag with two "-u-"
 * singletons is not structurally valid.
 * @param {string} locale a canonicalized language tag
 * @return {string} the language, script, region and variant subtags of locale
 */
function getLocaleBaseName(locale) {
  var subtags = locale.split("-");
  // Every subtag after the language is at least two characters long, except
  // for the singletons that introduce extension and private use sequences.
  for (var i = 1; i < subtags.length; i++) {
    if (subtags[i].length === 1) {
      return subtags.slice(0, i).join("-");
    }
  }
  return locale;
}

/**
 * Returns an array of error cases handled by CanonicalizeLocaleList().
 */
function getInvalidLocaleArguments() {
  function CustomError() {}

  var topLevelErrors = [
    // fails ToObject
    [null, TypeError],

    // fails Get
    [{ get length() { throw new CustomError(); } }, CustomError],

    // fail ToLength
    [{ length: Symbol.toPrimitive }, TypeError],
    [{ length: { get [Symbol.toPrimitive]() { throw new CustomError(); } } }, CustomError],
    [{ length: { [Symbol.toPrimitive]() { throw new CustomError(); } } }, CustomError],
    [{ length: { get valueOf() { throw new CustomError(); } } }, CustomError],
    [{ length: { valueOf() { throw new CustomError(); } } }, CustomError],
    [{ length: { get toString() { throw new CustomError(); } } }, CustomError],
    [{ length: { toString() { throw new CustomError(); } } }, CustomError],

    // fail type check
    [[undefined], TypeError],
    [[null], TypeError],
    [[true], TypeError],
    [[Symbol.toPrimitive], TypeError],
    [[1], TypeError],
    [[0.1], TypeError],
    [[NaN], TypeError],
  ];

  var invalidLanguageTags = [
    "", // empty tag
    "i", // singleton alone
    "x", // private use without subtag
    "u", // extension singleton in first place
    "419", // region code in first place
    "u-nu-latn-cu-bob", // extension sequence without language
    "hans-cmn-cn", // "hans" could theoretically be a 4-letter language code,
                   // but those can't be followed by extlang codes.
    "abcdefghi", // overlong language
    "cmn-hans-cn-u-u", // duplicate singleton
    "cmn-hans-cn-t-u-ca-u", // duplicate singleton
    "de-gregory-gregory", // duplicate variant
    "*", // language range
    "de-*", // language range
    "中文", // non-ASCII letters
    "en-ß", // non-ASCII letters
    "ıd" // non-ASCII letters
  ];

  return topLevelErrors.concat(
    invalidLanguageTags.map(tag => [tag, RangeError]),
    invalidLanguageTags.map(tag => [[tag], RangeError]),
    invalidLanguageTags.map(tag => [["en", tag], RangeError]),
  )
}

/**
 * Tests whether the named options property is correctly handled by the given constructor.
 * @param {object} Constructor the constructor to test.
 * @param {string} property the name of the options property to test.
 * @param {string} type the type that values of the property are expected to have
 * @param {Array} [values] an array of allowed values for the property. Not needed for boolean.
 * @param {any} fallback the fallback value that the property assumes if not provided.
 * @param {object} testOptions additional options:
 *   @param {boolean} isOptional whether support for this property is optional for implementations.
 *   @param {boolean} noReturn whether the resulting value of the property is not returned.
 *   @param {boolean} isILD whether the resulting value of the property is implementation and locale dependent.
 *   @param {object} extra additional option to pass along, properties are value -> {option: value}.
 */
function testOption(Constructor, property, type, values, fallback, testOptions) {
  var isOptional = testOptions !== undefined && testOptions.isOptional === true;
  var noReturn = testOptions !== undefined && testOptions.noReturn === true;
  var isILD = testOptions !== undefined && testOptions.isILD === true;

  function addExtraOptions(options, value, testOptions) {
    if (testOptions !== undefined && testOptions.extra !== undefined) {
      var extra;
      if (value !== undefined && testOptions.extra[value] !== undefined) {
        extra = testOptions.extra[value];
      } else if (testOptions.extra.any !== undefined) {
        extra = testOptions.extra.any;
      }
      if (extra !== undefined) {
        Object.getOwnPropertyNames(extra).forEach(function (prop) {
          options[prop] = extra[prop];
        });
      }
    }
  }

  var testValues, options, obj, expected, actual, error;

  // test that the specified values are accepted. Also add values that convert to specified values.
  if (type === "boolean") {
    if (values === undefined) {
      values = [true, false];
    }
    testValues = values.slice(0);
    testValues.push(888);
    testValues.push(0);
  } else if (type === "string") {
    testValues = values.slice(0);
    testValues.push({toString: function () { return values[0]; }});
  }
  testValues.forEach(function (value) {
    options = {};
    options[property] = value;
    addExtraOptions(options, value, testOptions);
    obj = new Constructor(undefined, options);
    if (noReturn) {
      if (obj.resolvedOptions().hasOwnProperty(property)) {
        throw new Test262Error("Option property " + property + " is returned, but shouldn't be.");
      }
    } else {
      actual = obj.resolvedOptions()[property];
      if (isILD) {
        if (actual !== undefined && values.indexOf(actual) === -1) {
          throw new Test262Error("Invalid value " + actual + " returned for property " + property + ".");
        }
      } else {
        if (type === "boolean") {
          expected = Boolean(value);
        } else if (type === "string") {
          expected = String(value);
        }
        if (actual !== expected && !(isOptional && actual === undefined)) {
          throw new Test262Error("Option value " + value + " for property " + property +
            " was not accepted; got " + actual + " instead.");
        }
      }
    }
  });

  // test that invalid values are rejected
  if (type === "string") {
    var invalidValues = ["invalidValue", -1, null];
    // assume that we won't have values in caseless scripts
    if (values[0].toUpperCase() !== values[0]) {
      invalidValues.push(values[0].toUpperCase());
    } else {
      invalidValues.push(values[0].toLowerCase());
    }
    invalidValues.forEach(function (value) {
      options = {};
      options[property] = value;
      addExtraOptions(options, value, testOptions);
      error = undefined;
      try {
        obj = new Constructor(undefined, options);
      } catch (e) {
        error = e;
      }
      if (error === undefined) {
        throw new Test262Error("Invalid option value " + value + " for property " + property + " was not rejected.");
      } else if (error.name !== "RangeError") {
        throw new Test262Error("Invalid option value " + value + " for property " + property + " was rejected with wrong error " + error.name + ".");
      }
    });
  }

  // test that fallback value or another valid value is used if no options value is provided
  if (!noReturn) {
    options = {};
    addExtraOptions(options, undefined, testOptions);
    obj = new Constructor(undefined, options);
    actual = obj.resolvedOptions()[property];
    if (!(isOptional && actual === undefined)) {
      if (fallback !== undefined) {
        if (actual !== fallback) {
          throw new Test262Error("Option fallback value " + fallback + " for property " + property +
            " was not used; got " + actual + " instead.");
        }
      } else {
        if (values.indexOf(actual) === -1 && !(isILD && actual === undefined)) {
          throw new Test262Error("Invalid value " + actual + " returned for property " + property + ".");
        }
      }
    }
  }
}


/**
 * Properties of the RegExp constructor that may be affected by use of regular
 * expressions, and the default values of these properties. Properties are from
 * https://developer.mozilla.org/en-US/docs/JavaScript/Reference/Deprecated_and_obsolete_features#RegExp_Properties
 */
var regExpProperties = ["$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9",
  "$_", "$*", "$&", "$+", "$`", "$'",
  "input", "lastMatch", "lastParen", "leftContext", "rightContext"
];

var regExpPropertiesDefaultValues = (function () {
  var values = Object.create(null);
  (/(?:)/).test("");
  regExpProperties.forEach(function (property) {
    values[property] = RegExp[property];
  });
  return values;
}());


/**
 * Tests that executing the provided function (which may use regular expressions
 * in its implementation) does not create or modify unwanted properties on the
 * RegExp constructor.
 */
function testForUnwantedRegExpChanges(testFunc) {
  (/(?:)/).test("");
  testFunc();
  regExpProperties.forEach(function (property) {
    if (RegExp[property] !== regExpPropertiesDefaultValues[property]) {
      throw new Test262Error("RegExp has unexpected property " + property + " with value " +
        RegExp[property] + ".");
    }
  });
}


/**
 * Returns an array of all known calendars.
 */
function allCalendars() {
  // source: CLDR file common/bcp47/number.xml; version CLDR 39.
  // https://github.com/unicode-org/cldr/blob/master/common/bcp47/calendar.xml
  return [
    "buddhist",
    "chinese",
    "coptic",
    "dangi",
    "ethioaa",
    "ethiopic",
    "gregory",
    "hebrew",
    "indian",
    "islamic",
    "islamic-umalqura",
    "islamic-tbla",
    "islamic-civil",
    "islamic-rgsa",
    "iso8601",
    "japanese",
    "persian",
    "roc",
  ];
}


/**
 * Returns an array of all known collations.
 */
function allCollations() {
  // source: CLDR file common/bcp47/collation.xml; version CLDR 39.
  // https://github.com/unicode-org/cldr/blob/master/common/bcp47/collation.xml
  return [
    "big5han",
    "compat",
    "dict",
    "direct",
    "ducet",
    "emoji",
    "eor",
    "gb2312",
    "phonebk",
    "phonetic",
    "pinyin",
    "reformed",
    "search",
    "searchjl",
    "standard",
    "stroke",
    "trad",
    "unihan",
    "zhuyin",
  ];
}


/**
 * Returns an array of all known numbering systems.
 */
function allNumberingSystems() {
  // source: CLDR file common/bcp47/number.xml; version CLDR 48 & new in Unicode 17.0
  // https://github.com/unicode-org/cldr/blob/master/common/bcp47/number.xml
  return [
    "adlm",
    "ahom",
    "arab",
    "arabext",
    "armn",
    "armnlow",
    "bali",
    "beng",
    "bhks",
    "brah",
    "cakm",
    "cham",
    "cyrl",
    "deva",
    "diak",
    "ethi",
    "finance",
    "fullwide",
    "gara",
    "geor",
    "gong",
    "gonm",
    "grek",
    "greklow",
    "gujr",
    "gukh",
    "guru",
    "hanidays",
    "hanidec",
    "hans",
    "hansfin",
    "hant",
    "hantfin",
    "hebr",
    "hmng",
    "hmnp",
    "java",
    "jpan",
    "jpanfin",
    "jpanyear",
    "kali",
    "kawi",
    "khmr",
    "knda",
    "krai",
    "lana",
    "lanatham",
    "laoo",
    "latn",
    "lepc",
    "limb",
    "mathbold",
    "mathdbl",
    "mathmono",
    "mathsanb",
    "mathsans",
    "mlym",
    "modi",
    "mong",
    "mroo",
    "mtei",
    "mymr",
    "mymrepka",
    "mymrpao",
    "mymrshan",
    "mymrtlng",
    "nagm",
    "native",
    "newa",
    "nkoo",
    "olck",
    "onao",
    "orya",
    "osma",
    "outlined",
    "rohg",
    "roman",
    "romanlow",
    "saur",
    "segment",
    "shrd",
    "sind",
    "sinh",
    "sora",
    "sund",
    "sunu",
    "takr",
    "talu",
    "taml",
    "tamldec",
    "tnsa",
    "telu",
    "thai",
    "tirh",
    "tibt",
    "tols",
    "traditio",
    "vaii",
    "wara",
    "wcho",
  ];
}


/**
 * Tests whether name is a valid BCP 47 numbering system name
 * and not excluded from use in the ECMAScript Internationalization API.
 * @param {string} name the name to be tested.
 * @return {boolean} whether name is a valid BCP 47 numbering system name and
 *   allowed for use in the ECMAScript Internationalization API.
 */

function isValidNumberingSystem(name) {

  var numberingSystems = allNumberingSystems();

  var excluded = [
    "finance",
    "native",
    "traditio"
  ];


  return numberingSystems.indexOf(name) !== -1 && excluded.indexOf(name) === -1;
}


/**
 * Provides the digits of numbering systems with simple digit mappings,
 * as specified in <https://tc39.es/ecma402/#table-numbering-system-digits>.
 */

var numberingSystemDigits = {
  adlm: "𞥐𞥑𞥒𞥓𞥔𞥕𞥖𞥗𞥘𞥙",
  ahom: "𑜰𑜱𑜲𑜳𑜴𑜵𑜶𑜷𑜸𑜹",
  arab: "٠١٢٣٤٥٦٧٨٩",
  arabext: "۰۱۲۳۴۵۶۷۸۹",
  bali: "\u1B50\u1B51\u1B52\u1B53\u1B54\u1B55\u1B56\u1B57\u1B58\u1B59",
  beng: "০১২৩৪৫৬৭৮৯",
  bhks: "𑱐𑱑𑱒𑱓𑱔𑱕𑱖𑱗𑱘𑱙",
  brah: "𑁦𑁧𑁨𑁩𑁪𑁫𑁬𑁭𑁮𑁯",
  cakm: "𑄶𑄷𑄸𑄹𑄺𑄻𑄼𑄽𑄾𑄿",
  cham: "꩐꩑꩒꩓꩔꩕꩖꩗꩘꩙",
  deva: "०१२३४५६७८९",
  diak: "𑥐𑥑𑥒𑥓𑥔𑥕𑥖𑥗𑥘𑥙",
  fullwide: "０１２３４５６７８９",
  gara: "\u{10D40}\u{10D41}\u{10D42}\u{10D43}\u{10D44}\u{10D45}\u{10D46}\u{10D47}\u{10D48}\u{10D49}",
  gong: "𑶠𑶡𑶢𑶣𑶤𑶥𑶦𑶧𑶨𑶩",
  gonm: "𑵐𑵑𑵒𑵓𑵔𑵕𑵖𑵗𑵘𑵙",
  gujr: "૦૧૨૩૪૫૬૭૮૯",
  gukh: "\u{16130}\u{16131}\u{16132}\u{16133}\u{16134}\u{16135}\u{16136}\u{16137}\u{16138}\u{16139}",
  guru: "੦੧੨੩੪੫੬੭੮੯",
  hanidec: "〇一二三四五六七八九",
  hmng: "𖭐𖭑𖭒𖭓𖭔𖭕𖭖𖭗𖭘𖭙",
  hmnp: "𞅀𞅁𞅂𞅃𞅄𞅅𞅆𞅇𞅈𞅉",
  java: "꧐꧑꧒꧓꧔꧕꧖꧗꧘꧙",
  kali: "꤀꤁꤂꤃꤄꤅꤆꤇꤈꤉",
  kawi: "\u{11F50}\u{11F51}\u{11F52}\u{11F53}\u{11F54}\u{11F55}\u{11F56}\u{11F57}\u{11F58}\u{11F59}",
  khmr: "០១២៣៤៥៦៧៨៩",
  knda: "೦೧೨೩೪೫೬೭೮೯",
  krai: "\u{16D70}\u{16D71}\u{16D72}\u{16D73}\u{16D74}\u{16D75}\u{16D76}\u{16D77}\u{16D78}\u{16D79}",
  lana: "᪀᪁᪂᪃᪄᪅᪆᪇᪈᪉",
  lanatham: "᪐᪑᪒᪓᪔᪕᪖᪗᪘᪙",
  laoo: "໐໑໒໓໔໕໖໗໘໙",
  latn: "0123456789",
  lepc: "᱀᱁᱂᱃᱄᱅᱆᱇᱈᱉",
  limb: "\u1946\u1947\u1948\u1949\u194A\u194B\u194C\u194D\u194E\u194F",
  mathbold: "𝟎𝟏𝟐𝟑𝟒𝟓𝟔𝟕𝟖𝟗",
  mathdbl: "𝟘𝟙𝟚𝟛𝟜𝟝𝟞𝟟𝟠𝟡",
  mathmono: "𝟶𝟷𝟸𝟹𝟺𝟻𝟼𝟽𝟾𝟿",
  mathsanb: "𝟬𝟭𝟮𝟯𝟰𝟱𝟲𝟳𝟴𝟵",
  mathsans: "𝟢𝟣𝟤𝟥𝟦𝟧𝟨𝟩𝟪𝟫",
  mlym: "൦൧൨൩൪൫൬൭൮൯",
  modi: "𑙐𑙑𑙒𑙓𑙔𑙕𑙖𑙗𑙘𑙙",
  mong: "᠐᠑᠒᠓᠔᠕᠖᠗᠘᠙",
  mroo: "𖩠𖩡𖩢𖩣𖩤𖩥𖩦𖩧𖩨𖩩",
  mtei: "꯰꯱꯲꯳꯴꯵꯶꯷꯸꯹",
  mymr: "၀၁၂၃၄၅၆၇၈၉",
  mymrepka: "\u{116DA}\u{116DB}\u{116DC}\u{116DD}\u{116DE}\u{116DF}\u{116E0}\u{116E1}\u{116E2}\u{116E3}",
  mymrpao: "\u{116D0}\u{116D1}\u{116D2}\u{116D3}\u{116D4}\u{116D5}\u{116D6}\u{116D7}\u{116D8}\u{116D9}",
  mymrshan: "႐႑႒႓႔႕႖႗႘႙",
  mymrtlng: "꧰꧱꧲꧳꧴꧵꧶꧷꧸꧹",
  nagm: "\u{1E4F0}\u{1E4F1}\u{1E4F2}\u{1E4F3}\u{1E4F4}\u{1E4F5}\u{1E4F6}\u{1E4F7}\u{1E4F8}\u{1E4F9}",
  newa: "𑑐𑑑𑑒𑑓𑑔𑑕𑑖𑑗𑑘𑑙",
  nkoo: "߀߁߂߃߄߅߆߇߈߉",
  olck: "᱐᱑᱒᱓᱔᱕᱖᱗᱘᱙",
  onao: "\u{1E5F1}\u{1E5F2}\u{1E5F3}\u{1E5F4}\u{1E5F5}\u{1E5F6}\u{1E5F7}\u{1E5F8}\u{1E5F9}\u{1E5FA}",
  orya: "୦୧୨୩୪୫୬୭୮୯",
  osma: "𐒠𐒡𐒢𐒣𐒤𐒥𐒦𐒧𐒨𐒩",
  outlined: "\u{1CCF0}\u{1CCF1}\u{1CCF2}\u{1CCF3}\u{1CCF4}\u{1CCF5}\u{1CCF6}\u{1CCF7}\u{1CCF8}\u{1CCF9}",
  rohg: "𐴰𐴱𐴲𐴳𐴴𐴵𐴶𐴷𐴸𐴹",
  saur: "꣐꣑꣒꣓꣔꣕꣖꣗꣘꣙",
  segment: "🯰🯱🯲🯳🯴🯵🯶🯷🯸🯹",
  shrd: "𑇐𑇑𑇒𑇓𑇔𑇕𑇖𑇗𑇘𑇙",
  sind: "𑋰𑋱𑋲𑋳𑋴𑋵𑋶𑋷𑋸𑋹",
  sinh: "෦෧෨෩෪෫෬෭෮෯",
  sora: "𑃰𑃱𑃲𑃳𑃴𑃵𑃶𑃷𑃸𑃹",
  sund: "᮰᮱᮲᮳᮴᮵᮶᮷᮸᮹",
  sunu: "\u{11BF0}\u{11BF1}\u{11BF2}\u{11BF3}\u{11BF4}\u{11BF5}\u{11BF6}\u{11BF7}\u{11BF8}\u{11BF9}",
  takr: "𑛀𑛁𑛂𑛃𑛄𑛅𑛆𑛇𑛈𑛉",
  talu: "᧐᧑᧒᧓᧔᧕᧖᧗᧘᧙",
  tamldec: "௦௧௨௩௪௫௬௭௮௯",
  telu: "౦౧౨౩౪౫౬౭౮౯",
  thai: "๐๑๒๓๔๕๖๗๘๙",
  tibt: "༠༡༢༣༤༥༦༧༨༩",
  tirh: "𑓐𑓑𑓒𑓓𑓔𑓕𑓖𑓗𑓘𑓙",
  tnsa: "\u{16AC0}\u{16AC1}\u{16AC2}\u{16AC3}\u{16AC4}\u{16AC5}\u{16AC6}\u{16AC7}\u{16AC8}\u{16AC9}",
  tols: "\u{11DE0}\u{11DE1}\u{11DE2}\u{11DE3}\u{11DE4}\u{11DE5}\u{11DE6}\u{11DE7}\u{11DE8}\u{11DE9}",
  vaii: "꘠꘡꘢꘣꘤꘥꘦꘧꘨꘩",
  wara: "𑣠𑣡𑣢𑣣𑣤𑣥𑣦𑣧𑣨𑣩",
  wcho: "𞋰𞋱𞋲𞋳𞋴𞋵𞋶𞋷𞋸𞋹",
};


/**
 * Returns an array of all simple, sanctioned unit identifiers.
 */
function allSimpleSanctionedUnits() {
  // https://tc39.es/ecma402/#table-sanctioned-simple-unit-identifiers
  return [
    "acre",
    "bit",
    "byte",
    "celsius",
    "centimeter",
    "day",
    "degree",
    "fahrenheit",
    "fluid-ounce",
    "foot",
    "gallon",
    "gigabit",
    "gigabyte",
    "gram",
    "hectare",
    "hour",
    "inch",
    "kilobit",
    "kilobyte",
    "kilogram",
    "kilometer",
    "liter",
    "megabit",
    "megabyte",
    "meter",
    "microsecond",
    "mile",
    "mile-scandinavian",
    "milliliter",
    "millimeter",
    "millisecond",
    "minute",
    "month",
    "nanosecond",
    "ounce",
    "percent",
    "petabyte",
    "pound",
    "second",
    "stone",
    "terabit",
    "terabyte",
    "week",
    "yard",
    "year",
  ];
}


/**
 * Tests that number formatting is handled correctly. The function checks that the
 * digit sequences in formatted output are as specified, converted to the
 * selected numbering system, and embedded in consistent localized patterns.
 * @param {Array} locales the locales to be tested.
 * @param {Array} numberingSystems the numbering systems to be tested.
 * @param {Object} options the options to pass to Intl.NumberFormat. Options
 *   must include {useGrouping: false}, and must cause 1.1 to be formatted
 *   pre- and post-decimal digits.
 * @param {Object} testData maps input data (in ES5 9.3.1 format) to expected output strings
 *   in unlocalized format with Western digits.
 * @param {Object} [testOptions] optional settings for the test.
 * @param {Function} [testOptions.makeInput] converts each input string in
 *   testData to the value that is formatted, e.g. Number. By default the
 *   string itself is formatted.
 */

function testNumberFormat(locales, numberingSystems, options, testData, testOptions) {
  var optionsStr = JSON.stringify(options);
  var makeInput = (testOptions && testOptions.makeInput) || function (input) {
    return input;
  };

  locales.forEach(function (locale) {
    assert.sameValue(
      locale,
      getLocaleBaseName(locale),
      "Unexpected extension in locale: '" + locale + "'"
    );

    numberingSystems.forEach(function (numbering) {
      var digits = numberingSystemDigits[numbering];
      var formatter =
        new Intl.NumberFormat(locale + "-u-nu-" + numbering, options);
      var resolved = formatter.resolvedOptions();
      if (resolved.numberingSystem !== numbering) {
        return;
      }

      // Capture static parts (prefix/infix/suffix) for positive and negative values.
      var digitsPatt = "[" + digits + "]+";
      var nonDigitsPatt = "([^" + digits + "]*)";
      var partsPatt = nonDigitsPatt + digitsPatt + nonDigitsPatt + digitsPatt + nonDigitsPatt;
      var posFormatted = formatter.format(1.1);
      var negFormatted = formatter.format(-1.1);
      var posStaticParts = posFormatted.match(new RegExp(partsPatt));
      var negStaticParts = negFormatted.match(new RegExp(partsPatt));
      assert(posStaticParts && negStaticParts && true,
        "Unexpected output in " + resolved.locale + " with options " + optionsStr +
        ": 1.1 -> " + posFormatted + ", -1.1 -> " + negFormatted);

      function transliterateDigits(raw) {
        return raw.replace(/[0-9]/g, function (asciiDigit) {
          return digits[asciiDigit];
        });
      }

      function buildExpected(raw, staticParts) {
        var sepIdx = raw.indexOf(".");
        var middle = sepIdx >= 0
          ? transliterateDigits(raw.substring(0, sepIdx)) +
            staticParts[2] +
            transliterateDigits(raw.substring(sepIdx + 1))
          : transliterateDigits(raw);
        return staticParts[1] + middle + staticParts[3];
      }

      Object.getOwnPropertyNames(testData).forEach(function (input) {
        var rawExpected = testData[input];
        var staticParts = posStaticParts;
        if (rawExpected[0] === "-") {
          rawExpected = rawExpected.substring(1);
          staticParts = negStaticParts;
        }
        var expected = buildExpected(rawExpected, staticParts);

        var actual = formatter.format(makeInput(input));
        assert.sameValue(actual, expected,
          "Formatted " + input + " in " + resolved.locale + " with options " + optionsStr +
          " is " + actual + "; expected " + expected + ".");
      });
    });
  });
}


/**
 * Return the components of date-time formats.
 * @return {Array} an array with all date-time components.
 */

function getDateTimeComponents() {
  return ["weekday", "era", "year", "month", "day", "hour", "minute", "second", "timeZoneName"];
}


/**
 * Return the valid values for the given date-time component, as specified
 * by the table in section 12.1.1.
 * @param {string} component a date-time component.
 * @return {Array} an array with the valid values for the component.
 */

function getDateTimeComponentValues(component) {

  var components = {
    weekday: ["narrow", "short", "long"],
    era: ["narrow", "short", "long"],
    year: ["2-digit", "numeric"],
    month: ["2-digit", "numeric", "narrow", "short", "long"],
    day: ["2-digit", "numeric"],
    hour: ["2-digit", "numeric"],
    minute: ["2-digit", "numeric"],
    second: ["2-digit", "numeric"],
    timeZoneName: ["short", "long"]
  };

  var result = components[component];
  if (result === undefined) {
    throw new Test262Error("Internal error: No values defined for date-time component " + component + ".");
  }
  return result;
}


/**
 * @description Tests whether timeZone is a String value representing a
 * structurally valid and canonicalized time zone name, as defined in
 * sections 6.4.1 and 6.4.2 of the ECMAScript Internationalization API
 * Specification.
 * @param {String} timeZone the string to be tested.
 * @result {Boolean} whether the test succeeded.
 */

function isCanonicalizedStructurallyValidTimeZoneName(timeZone) {
  /**
   * Regular expression defining IANA Time Zone names.
   *
   * Spec: IANA Time Zone Database, Theory file
   */
  var fileNameComponent = "(?:[A-Za-z_]|\\.(?!\\.?(?:/|$)))[A-Za-z.\\-_]{0,13}";
  var fileName = fileNameComponent + "(?:/" + fileNameComponent + ")*";
  var etcName = "(?:Etc/)?GMT[+-]\\d{1,2}";
  var systemVName = "SystemV/[A-Z]{3}\\d{1,2}(?:[A-Z]{3})?";
  var legacyName = etcName + "|" + systemVName + "|CST6CDT|EST5EDT|MST7MDT|PST8PDT|NZ";
  var zoneNamePattern = new RegExp("^(?:" + fileName + "|" + legacyName + ")$");

  if (typeof timeZone !== "string") {
    return false;
  }
  // 6.4.2 CanonicalizeTimeZoneName (timeZone), step 3
  if (timeZone === "UTC") {
    return true;
  }
  // 6.4.2 CanonicalizeTimeZoneName (timeZone), step 3
  if (timeZone === "Etc/UTC" || timeZone === "Etc/GMT") {
    return false;
  }
  return zoneNamePattern.test(timeZone);
}


/**
 * @description Simplified PartitionDurationFormatPattern implementation which
 * only supports the "en" locale.
 * @param {Object} durationFormat the duration format object
 * @param {Object} duration the duration record
 * @result {Array} an array with formatted duration parts
 */

function partitionDurationFormatPattern(durationFormat, duration) {
  function durationToFractional(duration, exponent) {
    let {
      seconds = 0,
      milliseconds = 0,
      microseconds = 0,
      nanoseconds = 0,
    } = duration;

    // Directly return the duration amount when no sub-seconds are present.
    switch (exponent) {
      case 9: {
        if (milliseconds === 0 && microseconds === 0 && nanoseconds === 0) {
          return seconds;
        }
        break;
      }
      case 6: {
        if (microseconds === 0 && nanoseconds === 0) {
          return milliseconds;
        }
        break;
      }
      case 3: {
        if (nanoseconds === 0) {
          return microseconds;
        }
        break;
      }
    }

    // Otherwise compute the overall amount of nanoseconds using BigInt to avoid
    // loss of precision.
    let ns = BigInt(nanoseconds);
    switch (exponent) {
      case 9:
        ns += BigInt(seconds) * 1_000_000_000n;
        // fallthrough
      case 6:
        ns += BigInt(milliseconds) * 1_000_000n;
        // fallthrough
      case 3:
        ns += BigInt(microseconds) * 1_000n;
        // fallthrough
    }

    let e = BigInt(10 ** exponent);

    // Split the nanoseconds amount into an integer and its fractional part.
    let q = ns / e;
    let r = ns % e;

    // Pad fractional part, without any leading negative sign, to |exponent| digits.
    if (r < 0) {
      r = -r;
    }
    r = String(r).padStart(exponent, "0");

    // Remove trailing zeros. The spec's ComputeFractionalDigits returns a
    // mathematical value, which carries no trailing zeros, but NumberFormat
    // may treat zeros spelled in a string as significant.
    r = r.replace(/0+$/, "");

    // Return the result as a decimal string.
    return r === "" ? `${q}` : `${q}.${r}`;
  }

  const units = [
    "years",
    "months",
    "weeks",
    "days",
    "hours",
    "minutes",
    "seconds",
    "milliseconds",
    "microseconds",
    "nanoseconds",
  ];

  let options = durationFormat.resolvedOptions();

  // Only "en" is supported.
  const locale = "en";
  const numberingSystem = "latn";
  const timeSeparator = ":";

  let result = [];
  let needSeparator = false;
  let displayNegativeSign = true;

  for (let unit of units) {
    // Absent units default to zero.
    let value = duration[unit] ?? 0;

    let style = options[unit];
    let display = options[unit + "Display"];

    // NumberFormat requires singular unit names.
    let numberFormatUnit = unit.slice(0, -1);

    // Compute the matching NumberFormat options.
    let nfOpts = Object.create(null);

    // Numeric seconds and sub-seconds are combined into a single value.
    let done = false;
    if (unit === "seconds" || unit === "milliseconds" || unit === "microseconds") {
      let nextStyle = options[units[units.indexOf(unit) + 1]];
      if (nextStyle === "numeric") {
        if (unit === "seconds") {
          value = durationToFractional(duration, 9);
        } else if (unit === "milliseconds") {
          value = durationToFractional(duration, 6);
        } else {
          value = durationToFractional(duration, 3);
        }

        nfOpts.maximumFractionDigits = options.fractionalDigits ?? 9;
        nfOpts.minimumFractionDigits = options.fractionalDigits ?? 0;
        nfOpts.roundingMode = "trunc";

        done = true;
      }
    }

    // Display zero numeric minutes when seconds will be displayed.
    let displayRequired = false;
    if (unit === "minutes" && needSeparator) {
      displayRequired = options.secondsDisplay === "always" ||
                        (duration.seconds ?? 0) !== 0 ||
                        (duration.milliseconds ?? 0) !== 0 ||
                        (duration.microseconds ?? 0) !== 0 ||
                        (duration.nanoseconds ?? 0) !== 0;
    }

    // "auto" display omits zero units.
    if (value !== 0 || display !== "auto" || displayRequired) {
      // Display only the first negative value.
      if (displayNegativeSign) {
        displayNegativeSign = false;

        // Set to negative zero to ensure the sign is displayed.
        if (value === 0) {
          let negative = units.some(unit => (duration[unit] ?? 0) < 0);
          if (negative) {
            value = -0;
          }
        }
      } else {
        nfOpts.signDisplay = "never";
      }

      nfOpts.numberingSystem = options.numberingSystem;

      // If the value is formatted as a 2-digit numeric value.
      if (style === "2-digit") {
        nfOpts.minimumIntegerDigits = 2;
      }

      // If the value is formatted as a standalone unit.
      if (style !== "numeric" && style !== "2-digit") {
        nfOpts.style = "unit";
        nfOpts.unit = numberFormatUnit;
        nfOpts.unitDisplay = style;
      } else {
        nfOpts.useGrouping = false;
      }

      let nf = new Intl.NumberFormat(locale, nfOpts);

      let list;
      if (!needSeparator) {
        list = [];
      } else {
        list = result[result.length - 1];

        // Prepend the time separator before the formatted number.
        list.push({
          type: "literal",
          value: timeSeparator,
        });
      }

      // Format the numeric value.
      let parts = nf.formatToParts(value);

      // Add |numberFormatUnit| to the formatted number.
      for (let {value, type} of parts) {
        list.push({type, value, unit: numberFormatUnit});
      }

      if (!needSeparator) {
        // Prepend the separator before the next numeric unit.
        if (style === "2-digit" || style === "numeric") {
          needSeparator = true;
        }

        // Append the formatted number to |result|.
        result.push(list);
      }
    }

    if (done) {
      break;
    }
  }

  let listStyle = options.style;
  if (listStyle === "digital") {
    listStyle = "short";
  }

  let lf = new Intl.ListFormat(locale, {
    type: "unit",
    style: listStyle,
  });

  // Collect all formatted units into a list of strings.
  let strings = [];
  for (let parts of result) {
    let string = "";
    for (let {value} of parts) {
      string += value;
    }
    strings.push(string);
  }

  // Format the list of strings and compute the overall result.
  let flattened = [];
  for (let {type, value} of lf.formatToParts(strings)) {
    if (type === "element") {
      flattened.push(...result.shift());
    } else {
      flattened.push({type, value});
    }
  }
  return flattened;
}


/**
 * @description Return the formatted string from partitionDurationFormatPattern.
 * @param {Object} durationFormat the duration format object
 * @param {Object} duration the duration record
 * @result {String} a string containing the formatted duration
 */

function formatDurationFormatPattern(durationFormat, duration) {
  let parts = partitionDurationFormatPattern(durationFormat, duration);
  return parts.reduce((acc, e) => acc + e.value, "");
}

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

// file: wellKnownIntrinsicObjects.js
// Copyright (C) 2018 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    An Array of all representable Well-Known Intrinsic Objects
defines: [WellKnownIntrinsicObjects, getWellKnownIntrinsicObject]
---*/

const WellKnownIntrinsicObjects = [
  {
    name: '%AggregateError%',
    source: 'AggregateError',
  },
  {
    name: '%Array%',
    source: 'Array',
  },
  {
    name: '%ArrayBuffer%',
    source: 'ArrayBuffer',
  },
  {
    name: '%ArrayIteratorPrototype%',
    source: 'Object.getPrototypeOf([][Symbol.iterator]())',
  },
  {
    // Not currently accessible to ECMAScript user code
    name: '%AsyncFromSyncIteratorPrototype%',
    source: '',
  },
  {
    name: '%AsyncFunction%',
    source: '(async function() {}).constructor',
  },
  {
    name: '%AsyncGeneratorFunction%',
    source: '(async function* () {}).constructor',
  },
  {
    name: '%AsyncGeneratorPrototype%',
    source: 'Object.getPrototypeOf(async function* () {}).prototype',
  },
  {
    name: '%AsyncIteratorPrototype%',
    source: 'Object.getPrototypeOf(Object.getPrototypeOf(async function* () {}).prototype)',
  },
  {
    name: '%Atomics%',
    source: 'Atomics',
  },
  {
    name: '%BigInt%',
    source: 'BigInt',
  },
  {
    name: '%BigInt64Array%',
    source: 'BigInt64Array',
  },
  {
    name: '%BigUint64Array%',
    source: 'BigUint64Array',
  },
  {
    name: '%Boolean%',
    source: 'Boolean',
  },
  {
    name: '%DataView%',
    source: 'DataView',
  },
  {
    name: '%Date%',
    source: 'Date',
  },
  {
    name: '%decodeURI%',
    source: 'decodeURI',
  },
  {
    name: '%decodeURIComponent%',
    source: 'decodeURIComponent',
  },
  {
    name: '%encodeURI%',
    source: 'encodeURI',
  },
  {
    name: '%encodeURIComponent%',
    source: 'encodeURIComponent',
  },
  {
    name: '%Error%',
    source: 'Error',
  },
  {
    name: '%eval%',
    source: 'eval',
  },
  {
    name: '%EvalError%',
    source: 'EvalError',
  },
  {
    name: '%FinalizationRegistry%',
    source: 'FinalizationRegistry',
  },
  {
    name: '%Float32Array%',
    source: 'Float32Array',
  },
  {
    name: '%Float64Array%',
    source: 'Float64Array',
  },
  {
    // Not currently accessible to ECMAScript user code
    name: '%ForInIteratorPrototype%',
    source: '',
  },
  {
    name: '%Function%',
    source: 'Function',
  },
  {
    name: '%GeneratorFunction%',
    source: '(function* () {}).constructor',
  },
  {
    name: '%GeneratorPrototype%',
    source: 'Object.getPrototypeOf(function * () {}).prototype',
  },
  {
    name: '%Int8Array%',
    source: 'Int8Array',
  },
  {
    name: '%Int16Array%',
    source: 'Int16Array',
  },
  {
    name: '%Int32Array%',
    source: 'Int32Array',
  },
  {
    name: '%isFinite%',
    source: 'isFinite',
  },
  {
    name: '%isNaN%',
    source: 'isNaN',
  },
  {
    name: '%Iterator%',
    source: 'typeof Iterator !== "undefined" ? Iterator : Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())).constructor',
  },
  {
    name: '%IteratorHelperPrototype%',
    source: 'Object.getPrototypeOf(Iterator.from([]).drop(0))',
  },
  {
    name: '%JSON%',
    source: 'JSON',
  },
  {
    name: '%Map%',
    source: 'Map',
  },
  {
    name: '%MapIteratorPrototype%',
    source: 'Object.getPrototypeOf(new Map()[Symbol.iterator]())',
  },
  {
    name: '%Math%',
    source: 'Math',
  },
  {
    name: '%Number%',
    source: 'Number',
  },
  {
    name: '%Object%',
    source: 'Object',
  },
  {
    name: '%parseFloat%',
    source: 'parseFloat',
  },
  {
    name: '%parseInt%',
    source: 'parseInt',
  },
  {
    name: '%Promise%',
    source: 'Promise',
  },
  {
    name: '%Proxy%',
    source: 'Proxy',
  },
  {
    name: '%RangeError%',
    source: 'RangeError',
  },
  {
    name: '%ReferenceError%',
    source: 'ReferenceError',
  },
  {
    name: '%Reflect%',
    source: 'Reflect',
  },
  {
    name: '%RegExp%',
    source: 'RegExp',
  },
  {
    name: '%RegExpStringIteratorPrototype%',
    source: 'Object.getPrototypeOf(RegExp.prototype[Symbol.matchAll](""))',
  },
  {
    name: '%Set%',
    source: 'Set',
  },
  {
    name: '%SetIteratorPrototype%',
    source: 'Object.getPrototypeOf(new Set()[Symbol.iterator]())',
  },
  {
    name: '%SharedArrayBuffer%',
    source: 'SharedArrayBuffer',
  },
  {
    name: '%String%',
    source: 'String',
  },
  {
    name: '%StringIteratorPrototype%',
    source: 'Object.getPrototypeOf(new String()[Symbol.iterator]())',
  },
  {
    name: '%Symbol%',
    source: 'Symbol',
  },
  {
    name: '%SyntaxError%',
    source: 'SyntaxError',
  },
  {
    name: '%ThrowTypeError%',
    source: '(function() { "use strict"; return Object.getOwnPropertyDescriptor(arguments, "callee").get })()',
  },
  {
    name: '%TypedArray%',
    source: 'Object.getPrototypeOf(Uint8Array)',
  },
  {
    name: '%TypeError%',
    source: 'TypeError',
  },
  {
    name: '%Uint8Array%',
    source: 'Uint8Array',
  },
  {
    name: '%Uint8ClampedArray%',
    source: 'Uint8ClampedArray',
  },
  {
    name: '%Uint16Array%',
    source: 'Uint16Array',
  },
  {
    name: '%Uint32Array%',
    source: 'Uint32Array',
  },
  {
    name: '%URIError%',
    source: 'URIError',
  },
  {
    name: '%WeakMap%',
    source: 'WeakMap',
  },
  {
    name: '%WeakRef%',
    source: 'WeakRef',
  },
  {
    name: '%WeakSet%',
    source: 'WeakSet',
  },
  {
    name: '%WrapForValidIteratorPrototype%',
    source: 'Object.getPrototypeOf(Iterator.from({ [Symbol.iterator](){ return {}; } }))',
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/ecma262/#sec-additional-properties-of-the-global-object
  {
    name: "%escape%",
    source: "escape",
  },
  {
    name: "%unescape%",
    source: "unescape",
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/ecma402/#sec-402-well-known-intrinsic-objects
  {
    name: "%Intl%",
    source: "Intl",
  },
  {
    name: "%Intl.Collator%",
    source: "Intl.Collator",
  },
  {
    name: "%Intl.DateTimeFormat%",
    source: "Intl.DateTimeFormat",
  },
  {
    name: "%Intl.DisplayNames%",
    source: "Intl.DisplayNames",
  },
  {
    name: "%Intl.DurationFormat%",
    source: "Intl.DurationFormat",
  },
  {
    name: "%Intl.ListFormat%",
    source: "Intl.ListFormat",
  },
  {
    name: "%Intl.Locale%",
    source: "Intl.Locale",
  },
  {
    name: "%Intl.NumberFormat%",
    source: "Intl.NumberFormat",
  },
  {
    name: "%Intl.PluralRules%",
    source: "Intl.PluralRules",
  },
  {
    name: "%Intl.RelativeTimeFormat%",
    source: "Intl.RelativeTimeFormat",
  },
  {
    name: "%Intl.Segmenter%",
    source: "Intl.Segmenter",
  },
  {
    name: "%IntlSegmentIteratorPrototype%",
    source: "Object.getPrototypeOf(new Intl.Segmenter().segment()[Symbol.iterator]())",
  },
  {
    name: "%IntlSegmentsPrototype%",
    source: "Object.getPrototypeOf(new Intl.Segmenter().segment())",
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/proposal-temporal/#sec-well-known-intrinsic-objects
  {
    name: "%Temporal%",
    source: "Temporal",
  },
];

WellKnownIntrinsicObjects.forEach((wkio) => {
  var actual;

  try {
    actual = new Function("return " + wkio.source)();
  } catch (exception) {
    // Nothing to do here.
  }

  wkio.value = actual;
});

/**
 * Returns a well-known intrinsic object, if the implementation provides it.
 * Otherwise, throws.
 * @param {string} key - the specification's name for the intrinsic, for example
 *   "%Array%"
 * @returns {object} the well-known intrinsic object.
 */
function getWellKnownIntrinsicObject(key) {
  for (var ix = 0; ix < WellKnownIntrinsicObjects.length; ix++) {
    if (WellKnownIntrinsicObjects[ix].name === key) {
      var value = WellKnownIntrinsicObjects[ix].value;
      if (value !== undefined)
        return value;
      throw new Test262Error('this implementation could not obtain ' + key);
    }
  }
  throw new Test262Error('unknown well-known intrinsic ' + key);
}
