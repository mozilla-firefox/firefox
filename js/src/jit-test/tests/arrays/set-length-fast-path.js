// |jit-test| --fast-warmup

function testNonExtensible() {
  for (var i = 0; i < 100; i++) {
    var arr = Object.preventExtensions([1, 2, 3, 4, 5]);
    arr.length = 3;
    assertEq(arr.length, 3);
    assertEq(arr.join(), "1,2,3");
    assertEq(3 in arr, false);

    assertEq(arr.pop(), 3);
    assertEq(arr.length, 2);

    assertEq(arr.splice(0, 1).join(), "1");
    assertEq(arr.length, 1);
    assertEq(arr[0], 2);

    arr.length = 10;
    assertEq(arr.length, 10);
    arr[5] = 1;
    assertEq(5 in arr, false);
    assertEq(Object.isExtensible(arr), false);
  }
}
testNonExtensible();

function testSealed() {
  "use strict";
  for (var i = 0; i < 100; i++) {
    var arr = Object.seal([1, 2, 3]);
    var ex = null;
    try {
      arr.length = 1;
    } catch (e) {
      ex = e;
    }
    assertEq(ex instanceof TypeError, true);
    assertEq(arr.length, 3);
    assertEq(arr.join(), "1,2,3");

    ex = null;
    try {
      arr.pop();
    } catch (e) {
      ex = e;
    }
    assertEq(ex instanceof TypeError, true);
    assertEq(arr.length, 3);
  }
}
testSealed();

function testForIn() {
  for (var i = 0; i < 100; i++) {
    var arr = [1, 2, 3, 4];
    var seen = [];
    for (var k in arr) {
      seen.push(k);
      if (k === "0") {
        arr.length = 1;
      }
    }
    assertEq(seen.join(), "0");
  }
}
testForIn();

function testTooLarge() {
  for (var i = 0; i < 100; i++) {
    var arr = [];
    arr.length = 2 ** 32 - 1;
    var ex = null;
    try {
      arr.push(1);
    } catch (e) {
      ex = e;
    }
    assertEq(ex instanceof RangeError, true);
    assertEq(arr.length, 2 ** 32 - 1);
  }
}
testTooLarge();

function testIndexed() {
  for (var i = 0; i < 100; i++) {
    var arr = [1, 2, 3];
    arr[1000] = 4;
    arr.length = 2;
    assertEq(arr.length, 2);
    assertEq(1000 in arr, false);

    arr[1000] = 4;
    var removed = arr.splice(1);
    assertEq(removed.length, 1000);
    assertEq(removed[999], 4);
    assertEq(arr.length, 1);
    assertEq(1000 in arr, false);
  }
}
testIndexed();

function testNonWritable() {
  for (var i = 0; i < 100; i++) {
    var arr = Object.defineProperty([1, 2, 3], "length", {writable: false});
    var ex = null;
    try {
      arr.pop();
    } catch (e) {
      ex = e;
    }
    assertEq(ex instanceof TypeError, true);
    assertEq(arr.length, 3);
  }
}
testNonWritable();
