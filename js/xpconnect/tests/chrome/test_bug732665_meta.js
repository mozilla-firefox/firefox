/* globals stackPointerInfo */

var stackBottom = stackPointerInfo();
var stackTop = stackBottom;

function nearNativeStackLimit() {
  function inner() {
    try {
      stackTop = stackPointerInfo();
      // eslint-disable-next-line no-eval
      var stepsFromLimit = eval("inner()"); // Use eval to force a number of native stackframes to be created.
      return stepsFromLimit + 1;
    } catch (e) {
      // It would be nice to check here that the exception is actually an
      // over-recursion here. But doing so would require toString()ing the
      // exception, which we may not have the stack space to do.
      return 1;
    }
  }
  return inner();
}

var nbFrames = nearNativeStackLimit();
var frameSize = stackBottom - stackTop;
print(
  "Max stack size:",
  frameSize,
  "bytes",
  "\nMaximum number of frames:",
  nbFrames,
  "\nAverage frame size:",
  Math.ceil(frameSize / nbFrames),
  "bytes"
);
