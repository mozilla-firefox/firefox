/* globals stackPointerInfo */

var stackBottom = stackPointerInfo();
var stackTop = stackBottom;

function nearNativeStackLimit() {
  let evalCount = 0;
  function inner() {
    try {
      stackTop = stackPointerInfo();
      // Use eval to force a number of native stackframes to be created.
      // Each eval string is unique so that it is compiled every time,
      // rather than being reused from the eval cache.
      // eslint-disable-next-line no-eval
      var stepsFromLimit = eval(`inner(); // ${evalCount++}`);
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
