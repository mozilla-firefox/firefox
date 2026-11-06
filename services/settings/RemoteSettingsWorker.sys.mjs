/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Interface to a dedicated thread handling for Remote Settings heavy operations.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

import { setTimeout, clearTimeout } from "resource://gre/modules/Timer.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "gMaxIdleMilliseconds",
  "services.settings.worker_idle_max_milliseconds",
  30 * 1000 // Default of 30 seconds.
);

ChromeUtils.defineESModuleGetters(lazy, {
  AsyncShutdown: "resource://gre/modules/AsyncShutdown.sys.mjs",
  SharedUtils: "resource://services-settings/SharedUtils.sys.mjs",
});

// Note: we currently only ever construct one instance of Worker.
// If it stops being a singleton, the AsyncShutdown code at the bottom
// of this file, as well as these globals, will need adjusting.
let gShutdown = false;
let gShutdownResolver = null;

function isShuttingDown() {
  return gShutdown || Services.startup.shuttingDown;
}

class RemoteSettingsWorkerError extends Error {
  constructor(message) {
    super(message);
    this.name = "RemoteSettingsWorkerError";
  }
}

class Worker {
  constructor(source) {
    if (gShutdown) {
      console.error("Can't create worker once shutdown has started");
    }
    this.source = source;
    this.worker = null;

    this.callbacks = new Map();
    this.lastCallbackId = 0;
    this.idleTimeoutId = null;
  }

  async _execute(method, args = [], options = {}) {
    // Check if we're shutting down.
    if (method != "prepareShutdown" && isShuttingDown()) {
      throw new RemoteSettingsWorkerError("Remote Settings has shut down.");
    }
    // Don't instantiate the worker to shut it down.
    if (method == "prepareShutdown" && !this.worker) {
      return null;
    }

    const { mustComplete = false } = options;
    // (Re)instantiate the worker if it was terminated.
    if (!this.worker) {
      this.worker = new ChromeWorker(this.source, { type: "module" });
      this.worker.onmessage = this._onWorkerMessage.bind(this);
      this.worker.onerror = error => {
        // Worker crashed. Reject each pending callback.
        for (const { reject } of this.callbacks.values()) {
          reject(error);
        }
        this.callbacks.clear();
        // And terminate it.
        this.stop();
      };
    }
    // New activity: reset the idle timer.
    if (this.idleTimeoutId) {
      clearTimeout(this.idleTimeoutId);
    }
    let identifier = method + "-";
    // Include the collection details in the importJSONDump case.
    if (identifier == "importJSONDump-") {
      identifier += `${args[0]}-${args[1]}-`;
    }
    return new Promise((resolve, reject) => {
      const callbackId = `${identifier}${++this.lastCallbackId}`;
      this.callbacks.set(callbackId, { resolve, reject, mustComplete });
      this.worker.postMessage({ callbackId, method, args });
    });
  }

  _onWorkerMessage(event) {
    const { callbackId, result, error } = event.data;
    // If we're shutting down, we may have already rejected this operation
    // and removed its callback from our map:
    if (!this.callbacks.has(callbackId)) {
      return;
    }
    const { resolve, reject } = this.callbacks.get(callbackId);
    if (error) {
      reject(new RemoteSettingsWorkerError(error));
    } else {
      resolve(result);
    }
    this.callbacks.delete(callbackId);

    // Terminate the worker when it's unused for some time.
    // But don't terminate it if an operation is pending.
    if (!this.callbacks.size) {
      if (gShutdown) {
        this.stop();
        if (gShutdownResolver) {
          gShutdownResolver();
        }
      } else {
        this.idleTimeoutId = setTimeout(() => {
          this.stop();
        }, lazy.gMaxIdleMilliseconds);
      }
    }
  }

  /**
   * Called at shutdown to abort anything the worker is doing that isn't
   * critical.
   */
  _abortCancelableRequests() {
    // End all tasks that we can.
    const callbackCopy = Array.from(this.callbacks.entries());
    const error = new Error("Shutdown, aborting read-only worker requests.");
    for (const [id, { reject, mustComplete }] of callbackCopy) {
      if (!mustComplete) {
        this.callbacks.delete(id);
        reject(error);
      }
    }
    // There might be nothing left now:
    if (!this.callbacks.size) {
      this.stop();
      if (gShutdownResolver) {
        gShutdownResolver();
      }
    }
    // If there was something left, we'll stop as soon as we get messages from
    // those tasks, too.
    // Let's hurry them along a bit:
    this._execute("prepareShutdown");
  }

  stop() {
    this.worker.terminate();
    this.worker = null;
    this.idleTimeoutId = null;
  }

  async canonicalStringify(records, timestamp) {
    return this._execute("canonicalStringify", [records, timestamp]);
  }

  async importJSONDump(bucket, collection) {
    return this._execute("importJSONDump", [bucket, collection], {
      mustComplete: true,
    });
  }

  async checkFileHash(filepath, size, hash) {
    return this._execute("checkFileHash", [filepath, size, hash]);
  }

  async checkContentHash(buffer, size, hash) {
    // The implementation does little work on the current thread, so run the
    // task on the current thread instead of the worker thread.
    return lazy.SharedUtils.checkContentHash(buffer, size, hash);
  }
}

// Now, first add a shutdown blocker. If that fails, we must have
// shut down already.
// We're doing this here rather than in the Worker constructor because in
// principle having just 1 shutdown blocker for the entire file should be
// fine. If we ever start creating more than one Worker instance, this
// code will need adjusting to deal with that.
try {
  lazy.AsyncShutdown.profileBeforeChange.addBlocker(
    "Remote Settings profile-before-change",
    async () => {
      // First, indicate we've shut down.
      gShutdown = true;
      // Then, if we have no worker or no callbacks, we're done.
      if (
        !RemoteSettingsWorker.worker ||
        !RemoteSettingsWorker.callbacks.size
      ) {
        return null;
      }
      // Otherwise, there's something left to do. Set up a promise:
      let finishedPromise = new Promise(resolve => {
        gShutdownResolver = resolve;
      });

      // Try to cancel most of the work:
      RemoteSettingsWorker._abortCancelableRequests();

      // Return a promise that the worker will resolve.
      return finishedPromise;
    },
    {
      fetchState() {
        const remainingCallbacks = RemoteSettingsWorker.callbacks;
        const details = Array.from(remainingCallbacks.keys()).join(", ");
        return `Remaining: ${remainingCallbacks.size} callbacks (${details}).`;
      },
    }
  );
} catch (ex) {
  console.error(
    "Couldn't add shutdown blocker, assuming shutdown has started."
  );
  console.error(ex);
  // If AsyncShutdown throws, `profileBeforeChange` has already fired. Ignore it
  // and mark shutdown. Constructing the worker will report an error and do
  // nothing.
  gShutdown = true;
}

export var RemoteSettingsWorker = new Worker(
  "resource://services-settings/RemoteSettings.worker.mjs"
);
