var x = { newCompartment: true, securityWrappers: true };
var ex;
try {
  newGlobal(x).hasSameBytecodeData(x, function () {});
} catch (e) {
  ex = e;
}
assertEq(ex.message, "The arguments must be interpreted functions.");
