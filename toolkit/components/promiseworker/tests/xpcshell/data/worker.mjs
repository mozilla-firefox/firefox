/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Trivial worker definition

import { PromiseWorker } from "resource://gre/modules/workers/PromiseWorker.mjs";

var worker = new PromiseWorker.AbstractWorker();
worker.dispatch = function (method, args = []) {
  return Agent[method](...args);
};
worker.postMessage = function (...args) {
  self.postMessage(...args);
};
worker.close = function () {
  self.close();
};
worker.log = function (...args) {
  dump("Worker: " + args.join(" ") + "\n");
};
self.addEventListener("message", msg => worker.handleMessage(msg));
self.addEventListener("unhandledrejection", function (error) {
  throw error.reason;
});

var Agent = {
  bounce(...args) {
    return args;
  },

  throwError(msg) {
    throw new Error(msg);
  },

  throwDOMException(msg, name) {
    throw new DOMException(msg, name);
  },
};
