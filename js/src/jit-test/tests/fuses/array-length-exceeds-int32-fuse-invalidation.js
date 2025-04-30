function maybeInvalidate(arr, i) {
  with (this) {} // Don't Ion-compile.
  if (i === 1950) {
    arr.length = 2147483648;
  }
}
function test() {
  var arr = [];
  var result = 0;
  for (var i = 0; i < 2000; i++) {
    result += arr.length;
    maybeInvalidate(arr, i);
  }
  assertEq(result, 105226698752);
}
test();
