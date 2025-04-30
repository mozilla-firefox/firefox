load(libdir + "asserts.js");

function makeConstantCompareFn(val, op) {
  return new Function('val', `return val ${op} ${val};`);
}

{
  function testConstantCompareFn(fn, i, expectedOnSuccess) {
    // test with int values
    assertEq(fn(i), expectedOnSuccess);
    assertEq(fn(i + 1), !expectedOnSuccess);
    assertEq(fn(i - 1), !expectedOnSuccess);

    // test with double values
    assertEq(fn(i + 0.5), !expectedOnSuccess);
    assertEq(fn(i - 0.5), !expectedOnSuccess);

    // test with string values
    assertEq(fn(String(i)), !expectedOnSuccess);
    assertEq(fn(String(i + 1)), !expectedOnSuccess);
    assertEq(fn(String(i - 1)), !expectedOnSuccess);

    // test with boolean values
    assertEq(fn(true), !expectedOnSuccess);
    assertEq(fn(false), !expectedOnSuccess);

    // test with null values
    assertEq(fn(null), !expectedOnSuccess);

    // test with undefined values
    assertEq(fn(undefined), !expectedOnSuccess);

    // test with NaN values
    assertEq(fn(NaN), !expectedOnSuccess);

    // test with Infinity values
    assertEq(fn(Infinity), !expectedOnSuccess);
    assertEq(fn(-Infinity), !expectedOnSuccess);

    // test with object values
    assertEq(fn({}), !expectedOnSuccess);
    assertEq(fn([]), !expectedOnSuccess);

    if (i === 0) {
      // test signed zero values
      assertEq(fn(+0), expectedOnSuccess);
      assertEq(fn(-0), expectedOnSuccess);
      assertEq(fn(0.0), expectedOnSuccess);
      assertEq(fn(-0.0), expectedOnSuccess);
    }

    // test values outside int32 range
    assertEq(fn(9007199254740992), !expectedOnSuccess);
    assertEq(fn(-9007199254740993), !expectedOnSuccess);

    // test with bigint values
    assertEq(fn(BigInt(i)), !expectedOnSuccess);
    assertEq(fn(BigInt(i + 1)), !expectedOnSuccess);
    assertEq(fn(BigInt(i - 1)), !expectedOnSuccess);
  }

  // make all the int8 values that can be used as constant compare operands
  for (let i = -128; i <= 127; i++) {
    const fnEq = makeConstantCompareFn(i, '===');
    const fnNe = makeConstantCompareFn(i, '!==');

    testConstantCompareFn(fnEq, i, true);
    testConstantCompareFn(fnNe, i, false);
  }
}
