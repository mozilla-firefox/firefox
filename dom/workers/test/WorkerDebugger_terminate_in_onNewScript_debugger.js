"use strict";

var dbg = new Debugger(global);
dbg.onNewScript = function () {
  dbg.onNewScript = undefined;
  // This hook runs from inside JS::Compile() of the worker's main script. Tell
  // the test, which terminates the worker in response, and keep running until
  // the termination arrives: it is delivered through the worker's interrupt
  // callback, which aborts this loop with an uncatchable exception.
  postMessage("onNewScript");
  while (true) {
    // Spin until the worker is terminated.
  }
};
