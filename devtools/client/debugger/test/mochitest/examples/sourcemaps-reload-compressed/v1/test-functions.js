/**
 * This file contains various test functions called by the test script via `await invokeInTab("functionName");`.
 */
function breakInEval(suffix = "") {
  eval(`
    debugger;

    window.evaledFunc = function() {
      var foo = 1;
      var bar = 2;
      return foo + bar;
    };
  ${suffix && `// ${suffix}`}`);
}
