/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Trivial worker definition

/* import-globals-from /toolkit/components/workerloader/require.js */
importScripts("resource://gre/modules/workers/require.js");
var PromiseWorker = require("resource://gre/modules/workers/PromiseWorker.js");

var worker = new PromiseWorker.AbstractWorker();

worker.dispatch = async function (method, args = []) {
  return await Agent[method](...args);
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

  async bounceWithExtraCalls(...args) {
    let result = await worker.callMainThread("echo", [
      "Posting something unrelated",
    ]);
    args.push(result.ok);
    return args;
  },

  throwError(msg) {
    throw new Error(msg);
  },

  throwDOMException(msg, name) {
    throw new DOMException(msg, name);
  },
};
