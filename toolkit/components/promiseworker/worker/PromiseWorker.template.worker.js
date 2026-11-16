/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A wrapper around `self` with extended capabilities designed
 * to simplify main thread-to-worker thread asynchronous function calls.
 *
 * This wrapper:
 * - groups requests and responses as a method `post` that returns a `Promise`;
 * - ensures that exceptions thrown on the worker thread are correctly serialized;
 * - provides some utilities for benchmarking various operations.
 *
 * Generally, you should use PromiseWorker.js or PromiseWorker.mjs along with
 * its main thread-side counterpart PromiseWorker.sys.mjs.
 */

// #BEGIN_SCRIPT_ONLY
"use strict";
// #END_SCRIPT_ONLY

if (typeof Components != "undefined") {
  throw new Error("This module is meant to be used from the worker thread");
}
// #BEGIN_SCRIPT_ONLY
if (typeof require == "undefined" || typeof module == "undefined") {
  throw new Error(
    "this module is meant to be imported using the implementation of require() at resource://gre/modules/workers/require.js"
  );
}

/* import-globals-from /toolkit/components/workerloader/require.js */
importScripts("resource://gre/modules/workers/require.js");
// #END_SCRIPT_ONLY

/**
 * A constructor used to return data to the caller thread while
 * also executing some specific treatment (e.g. shutting down
 * the current thread, transmitting data instead of copying it).
 *
 * @param {object=} data The data to return to the caller thread.
 * @param {object=} meta Additional instructions, as an object
 * that may contain the following fields:
 * - {bool} shutdown If |true|, shut down the current thread after
 *   having sent the result.
 * - {Array} transfers An array of objects that should be transferred
 *   instead of being copied.
 *
 * @class
 */
function Meta(data, meta) {
  this.data = data;
  this.meta = meta;
}
// #EXPORT Meta

/**
 * Base class for a worker.
 *
 * Derived classes are expected to provide the following methods:
 * {
 *   dispatch: function(method, args) {
 *     // Dispatch a call to method `method` with args `args`
 *   },
 *   log: function(...msg) {
 *     // Log (or discard) messages (optional)
 *   },
 *   postMessage: function(message, ...transfers) {
 *     // Post a message to the main thread
 *   },
 *   close: function() {
 *     // Close the worker
 *   }
 * }
 *
 * By default, the AbstractWorker is not connected to a message port,
 * hence will not receive anything.
 *
 * To connect it, use `onmessage`, as follows:
 *   self.addEventListener("message", msg => myWorkerInstance.handleMessage(msg));
 * To handle rejected promises we receive from handleMessage, we must connect it to
 * the onError handler as follows:
 *   self.addEventListener("unhandledrejection", function(error) {
 *    throw error.reason;
 *   });
 */
function AbstractWorker(agent) {
  this._agent = agent;
  this._deferredJobs = new Map();
  this._deferredJobId = 0;
  // Built-in JavaScript exceptions that may be serialized without
  // loss of information.
  this._exceptionNames = {
    EvalError: "EvalError",
    InternalError: "InternalError",
    RangeError: "RangeError",
    ReferenceError: "ReferenceError",
    SyntaxError: "SyntaxError",
    TypeError: "TypeError",
    URIError: "URIError",
  };
}

AbstractWorker.prototype = {
  // Default logger: discard all messages
  log() {},

  _generateDeferredJobId() {
    this._deferredJobId += 1;
    return "WorkerToThread-" + this._deferredJobId;
  },

  /**
   * Post and wait for an answer from the thread.
   */
  callMainThread(funcName, args) {
    const messageId = this._generateDeferredJobId();

    const message = {
      id: messageId,
      fun: funcName,
      args,
    };

    return new Promise((resolve, reject) => {
      this._deferredJobs.set(messageId, { resolve, reject });
      this.postMessage(message);
    });
  },

  /**
   * Handle a message.
   */
  async handleMessage(msg) {
    let data = msg.data;
    let id = data.id;

    // if the id is found in _deferredJobs, we proceed with the message
    if (this._deferredJobs.has(id)) {
      const { resolve, reject } = this._deferredJobs.get(id);

      if ("ok" in data) {
        resolve(data);
      } else if ("fail" in data) {
        reject(data);
      }
      this._deferredJobs.delete(id);
      return;
    }

    let start;
    let options;
    if (data.args) {
      options = data.args[data.args.length - 1];
    }
    // If |outExecutionDuration| option was supplied, start measuring the
    // duration of the operation.
    if (
      options &&
      typeof options === "object" &&
      "outExecutionDuration" in options
    ) {
      start = Date.now();
    }

    let result;
    let exn;
    let durationMs;
    let method = data.fun;
    try {
      this.log("Calling method", method);
      result = await this.dispatch(method, data.args);
      this.log("Method", method, "succeeded");
    } catch (ex) {
      exn = ex;
      this.log(
        "Error while calling agent method",
        method,
        exn,
        exn.moduleStack || exn.stack || ""
      );
    }

    if (start) {
      // Record duration
      durationMs = Date.now() - start;
      this.log("Method took", durationMs, "ms");
    }

    // Now, post a reply, possibly as an uncaught error.
    // We post this message from outside the |try ... catch| block
    // to avoid capturing errors that take place during |postMessage| and
    // built-in serialization.
    if (!exn) {
      this.log("Sending positive reply", result, "id is", id);
      if (result instanceof Meta) {
        if ("transfers" in result.meta) {
          // Take advantage of zero-copy transfers
          this.postMessage(
            { ok: result.data, id, durationMs },
            result.meta.transfers
          );
        } else {
          this.postMessage({ ok: result.data, id, durationMs });
        }
        if (result.meta.shutdown || false) {
          // Time to close the worker
          this.close();
        }
      } else {
        this.postMessage({ ok: result, id, durationMs });
      }
    } else if (exn.constructor.name == "DOMException") {
      // We can receive instances of DOMExceptions with file I/O.
      // DOMExceptions are not yet serializable (Bug 1561357) and must be
      // handled differently, as they only have a name and message
      this.log("Sending back DOM exception", exn.constructor.name);
      let error = {
        exn: exn.constructor.name,
        name: exn.name,
        message: exn.message,
      };
      this.postMessage({ fail: error, id, durationMs });
    } else if (exn.constructor.name in this._exceptionNames) {
      // Rather than letting the DOM mechanism [de]serialize built-in
      // JS errors, which loses lots of information (in particular,
      // the constructor name, the moduleName and the moduleStack),
      // we [de]serialize them manually with a little more care.
      this.log("Sending back exception", exn.constructor.name, "id is", id);
      let error = {
        exn: exn.constructor.name,
        message: exn.message,
        fileName: exn.moduleName || exn.fileName,
        lineNumber: exn.lineNumber,
        stack: exn.moduleStack,
      };
      this.postMessage({ fail: error, id, durationMs });
    } else if ("toMsg" in exn) {
      // Extension mechanism for exception [de]serialization. We
      // assume that any exception with a method `toMsg()` knows how
      // to serialize itself. The other side is expected to have
      // registered a deserializer using the `ExceptionHandlers`
      // object.
      this.log(
        "Sending back an error that knows how to serialize itself",
        exn,
        "id is",
        id
      );
      this.postMessage({ fail: exn.toMsg(), id, durationMs });
    } else {
      // If we encounter an exception for which we have no
      // serialization mechanism in place, we have no choice but to
      // let the DOM handle said [de]serialization. We can just
      // attempt to mitigate the data loss by injecting `moduleName` and
      // `moduleStack`.
      this.log(
        "Sending back regular error",
        exn,
        exn.moduleStack || exn.stack,
        "id is",
        id
      );

      try {
        // Attempt to introduce human-readable filename and stack
        exn.filename = exn.moduleName;
        exn.stack = exn.moduleStack;
      } catch (_) {
        // Nothing we can do
      }
      throw exn;
    }
  },
};
// #EXPORT AbstractWorker
