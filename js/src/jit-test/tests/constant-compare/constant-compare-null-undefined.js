load(libdir + "asserts.js");

function makeConstantCompareFn(val, op) {
  return new Function('val', `return val ${op} ${val};`);
}

{
  function testConstantCompareFn(fn, i, expectedOnSuccess) {
    assertEq(fn(i), expectedOnSuccess);

    // test with double values
    assertEq(fn(0.5), !expectedOnSuccess);
    assertEq(fn(-0.5), !expectedOnSuccess);

    // test with string values
    assertEq(fn(String(i)), !expectedOnSuccess);
    assertEq(fn(String(!i)), !expectedOnSuccess);

    // test with int values
    assertEq(fn(1), !expectedOnSuccess);
    assertEq(fn(0), !expectedOnSuccess);
    assertEq(fn(-1), !expectedOnSuccess);

    // test with BigInt values
    assertEq(fn(BigInt(1)), !expectedOnSuccess);

    // test with NaN
    assertEq(fn(NaN), !expectedOnSuccess);

    // test with Infinity
    assertEq(fn(Infinity), !expectedOnSuccess);
    assertEq(fn(-Infinity), !expectedOnSuccess);

    // test with object values
    assertEq(fn({}), !expectedOnSuccess);
    assertEq(fn([]), !expectedOnSuccess);
  }

  testConstantCompareFn(makeConstantCompareFn(null, '==='), null, true);
  testConstantCompareFn(makeConstantCompareFn(null, '!=='), null, false);

  testConstantCompareFn(makeConstantCompareFn(undefined, '==='), undefined, true);
  testConstantCompareFn(makeConstantCompareFn(undefined, '!=='), undefined, false);
}
