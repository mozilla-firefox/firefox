/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventEmitter: "resource://gre/modules/EventEmitter.sys.mjs",

  getFramesFromStack: "chrome://remote/content/shared/Stack.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

/**
 * The ConsoleListener can be used to listen for console messages related to
 * Javascript errors, certain warnings which all happen within a specific
 * windowGlobal. Consumers can listen for the message types "error",
 * "warn" and "info".
 *
 * Example:
 * ```
 * const onJavascriptError = (eventName, data = {}) => {
 *   const { level, message, stacktrace, timestamp } = data;
 *   ...
 * };
 *
 * const listener = new ConsoleListener(innerWindowId);
 * listener.on("error", onJavascriptError);
 * listener.startListening();
 * ...
 * listener.stopListening();
 * ```
 *
 * @fires message
 *    The ConsoleListener emits "error", "warn" and "info" events, with the
 *    following object as payload:
 *      - {String} level - Importance, one of `info`, `warn`, `error`,
 *        `debug`, `trace`.
 *      - {String} message - Actual message from the console entry.
 *      - {Array<StackFrame>} stacktrace - List of stack frames,
 *        starting from most recent.
 *      - {Number} timeStamp - Timestamp when the method was called.
 */
export class ConsoleListener {
  #emittedMessages;
  #innerWindowId;
  #listening;

  /**
   * Create a new ConsoleListener instance.
   *
   * @param {number} innerWindowId
   *     The inner window id to filter the messages for.
   */
  constructor(innerWindowId) {
    lazy.EventEmitter.decorate(this);

    this.#emittedMessages = new Set();
    this.#innerWindowId = innerWindowId;
    this.#listening = false;
  }

  get listening() {
    return this.#listening;
  }

  destroy() {
    this.stopListening();
    this.#emittedMessages = null;
  }

  startListening() {
    if (this.#listening) {
      return;
    }

    Services.console.registerListener(this.#onConsoleMessage);

    // Emit cached messages after registering the listener, to make sure we
    // don't miss any message.
    this.#emitCachedMessages();

    this.#listening = true;
  }

  stopListening() {
    if (!this.#listening) {
      return;
    }

    Services.console.unregisterListener(this.#onConsoleMessage);
    this.#listening = false;
  }

  #emitCachedMessages() {
    const cachedMessages = Services.console.getMessageArray() || [];

    for (const message of cachedMessages) {
      this.#onConsoleMessage(message);
    }
  }

  #onConsoleMessage = message => {
    if (!(message instanceof Ci.nsIScriptError)) {
      // For now ignore basic nsIConsoleMessage instances, which are only
      // relevant to Chrome code and do not have a valid window reference.
      return;
    }

    if (message.innerWindowID !== this.#innerWindowId) {
      // If the message doesn't match the innerWindowId of the current context
      // ignore it.
      return;
    }

    // Bail if this message was already emitted, useful to filter out cached
    // messages already received by the consumer.
    if (this.#emittedMessages.has(message)) {
      return;
    }

    this.#emittedMessages.add(message);

    const { errorFlag, warningFlag, infoFlag } = Ci.nsIScriptError;
    let level;

    if ((message.flags & warningFlag) == warningFlag) {
      level = "warn";
    } else if ((message.flags & infoFlag) == infoFlag) {
      level = "info";
    } else if ((message.flags & errorFlag) == errorFlag) {
      level = "error";
    } else {
      lazy.logger.warn(
        `Not able to process console message with unknown flags ${message.flags}`
      );
      return;
    }

    // Send event when actively listening.
    this.emit(level, {
      level,
      message: message.errorMessage,
      stacktrace: lazy.getFramesFromStack(message.stack),
      timeStamp: message.timeStamp,
    });
  };

  // XPCOM

  QueryInterface = ChromeUtils.generateQI(["nsIConsoleListener"]);
}
