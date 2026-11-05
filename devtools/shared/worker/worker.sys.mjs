/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const logger = console.createInstance({
  logModule: "devtools_worker",
  maxLogLevel: "Warn",
});

let MESSAGE_COUNTER = 0;

/**
 * Creates a wrapper around a ChromeWorker, providing easy
 * communication to offload demanding tasks. The corresponding URL
 * must implement the interface provided by `devtools/shared/worker/helper`.
 *
 * @param {string} url
 *        The URL of the worker.
 * @param Object opts
 *        An option with the following optional fields:
 *        - name: a name that will be printed with logs
 */
export function DevToolsWorker(url, opts) {
  opts = opts || {};
  this._worker = new ChromeWorker(url);
  this._name = opts.name;

  this._worker.addEventListener("error", this.onError);
}

/**
 * Performs the given task in a chrome worker, passing in data.
 * Returns a promise that resolves when the task is completed, resulting in
 * the return value of the task.
 *
 * @param {string} task
 *        The name of the task to execute in the worker.
 * @param {any} data
 *        Data to be passed into the task implemented by the worker.
 * @param {undefined|Array} transfer
 *        Optional array of transferable objects to transfer ownership of.
 * @return {Promise}
 */
DevToolsWorker.prototype.performTask = function (task, data, transfer) {
  if (this._destroyed) {
    return Promise.reject(
      "Cannot call performTask on a destroyed DevToolsWorker"
    );
  }
  const worker = this._worker;
  const id = ++MESSAGE_COUNTER;
  const payload = { task, id, data };

  if (logger.shouldLog("Log")) {
    logger.log(
      "Sending message to worker" +
        (this._name ? " (" + this._name + ")" : "") +
        ": " +
        JSON.stringify(payload, null, 2)
    );
  }
  worker.postMessage(payload, transfer);

  return new Promise((resolve, reject) => {
    const listener = ({ data: result }) => {
      if (logger.shouldLog("Log")) {
        logger.log(
          "Received message from worker" +
            (this._name ? " (" + this._name + ")" : "") +
            ": " +
            JSON.stringify(result, null, 2)
        );
      }

      if (result.id !== id) {
        return;
      }
      worker.removeEventListener("message", listener);
      if (result.error) {
        reject(result.error);
      } else {
        resolve(result.response);
      }
    };

    worker.addEventListener("message", listener);
  });
};

/**
 * Terminates the underlying worker. Use when no longer needing the worker.
 */
DevToolsWorker.prototype.destroy = function () {
  this._worker.terminate();
  this._worker = null;
  this._destroyed = true;
};

DevToolsWorker.prototype.onError = function ({ message, filename, lineno }) {
  dump(new Error(message + " @ " + filename + ":" + lineno) + "\n");
};
