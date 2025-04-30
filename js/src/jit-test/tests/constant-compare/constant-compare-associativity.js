load(libdir + "asserts.js");

function testConstantCompareIsLeftAssociative(intVal, boolVal) {
  return 1 === intVal === boolVal;
}

assertEq(testConstantCompareIsLeftAssociative(1, true), true);
assertEq(testConstantCompareIsLeftAssociative(1, false), false);
assertEq(testConstantCompareIsLeftAssociative(0, true), false);
assertEq(testConstantCompareIsLeftAssociative(0, false), true);

function testConstantCompareMixedLeftAssociative(intVal, boolVal) {
  return 1 !== intVal === true !== boolVal;
}

assertEq(testConstantCompareMixedLeftAssociative(1, true), true);
assertEq(testConstantCompareMixedLeftAssociative(1, false), false);
assertEq(testConstantCompareMixedLeftAssociative(0, true), false);
assertEq(testConstantCompareMixedLeftAssociative(0, false), true);
