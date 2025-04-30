/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  // xpcshell doesn't handle idle callbacks well.
  idleTimeout: () => (Services.appinfo.name === "XPCShell" ? 500 : undefined),
});

// It would be nicer to go through `Services.appinfo`, but some tests need to be
// able to replace that field with a custom implementation before it is first
// called.
// eslint-disable-next-line mozilla/use-services
const appinfo = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime);

let nextId = 0;
const uniqueProcessID = appinfo.uniqueProcessID;
// Store the process ID in a 16 bit field left shifted to end of a
// double's mantissa.
// Note: We can't use bitwise ops here, since they truncate to a 32 bit
// integer and we need all 53 mantissa bits.
const processIDMask = (uniqueProcessID & 0xffff) * 2 ** 37;

function getUniqueId() {
  // Note: We can't use bitwise ops here, since they truncate to a 32 bit
  // integer and we need all 53 mantissa bits.
  return processIDMask + nextId++;
}

function promiseTimeout(delay) {
  return new Promise(resolve => lazy.setTimeout(resolve, delay));
}

/**
 * An Error subclass for which complete error messages are always passed
 * to extensions, rather than being interpreted as an unknown error.
 */
export class ExtensionError extends DOMException {
  constructor(message) {
    super(message, "ExtensionError");
  }
  // Custom JS classes can't survive IPC, so need to check error name.
  static [Symbol.hasInstance](e) {
    return DOMException.isInstance(e) && e.name === "ExtensionError";
  }
}

function filterStack(error) {
  return String(error.stack).replace(
    /(^.*(Task\.jsm|Promise-backend\.js).*\n)+/gm,
    "<Promise Chain>\n"
  );
}

/**
 * An Error subclass used to recognize the errors that should
 * to be forwarded to the worker thread and being accessible
 * to the extension worker script (vs. the errors that should be
 * only logged internally and raised to the worker script as
 * the generic unexpected error).
 */
export class WorkerExtensionError extends DOMException {
  constructor(message) {
    super(message, "Error");
  }
}

/**
 * Similar to a WeakMap, but creates a new key with the given
 * constructor if one is not present.
 */
// @ts-ignore (https://github.com/microsoft/TypeScript/issues/56664)
class DefaultWeakMap extends WeakMap {
  constructor(defaultConstructor = undefined, init = undefined) {
    super(init);
    if (defaultConstructor) {
      this.defaultConstructor = defaultConstructor;
    }
  }

  get(key) {
    let value = super.get(key);
    if (value === undefined && !this.has(key)) {
      value = this.defaultConstructor(key);
      this.set(key, value);
    }
    return value;
  }
}

class DefaultMap extends Map {
  constructor(defaultConstructor = undefined, init = undefined) {
    super(init);
    if (defaultConstructor) {
      this.defaultConstructor = defaultConstructor;
    }
  }

  get(key) {
    let value = super.get(key);
    if (value === undefined && !this.has(key)) {
      value = this.defaultConstructor(key);
      this.set(key, value);
    }
    return value;
  }
}

function getInnerWindowID(window) {
  return window.windowGlobalChild?.innerWindowId;
}

/**
 * A set with a limited number of slots, which flushes older entries as
 * newer ones are added.
 *
 * @param {number} limit
 *        The maximum size to trim the set to after it grows too large.
 * @param {number} [slop = limit * .25]
 *        The number of extra entries to allow in the set after it
 *        reaches the size limit, before it is truncated to the limit.
 * @param {Iterable} [iterable]
 *        An iterable of initial entries to add to the set.
 */
class LimitedSet extends Set {
  constructor(limit, slop = Math.round(limit * 0.25), iterable = undefined) {
    super(iterable);
    this.limit = limit;
    this.slop = slop;
  }

  truncate(limit) {
    for (let item of this) {
      // Live set iterators can ge relatively expensive, since they need
      // to be updated after every modification to the set. Since
      // breaking out of the loop early will keep the iterator alive
      // until the next full GC, we're currently better off finishing
      // the entire loop even after we're done truncating.
      if (this.size > limit) {
        this.delete(item);
      }
    }
  }

  add(item) {
    if (this.size >= this.limit + this.slop && !this.has(item)) {
      this.truncate(this.limit - 1);
    }
    return super.add(item);
  }
}

/**
 * Returns a Promise which resolves when the given document's DOM has
 * fully loaded.
 *
 * @param {Document} doc The document to await the load of.
 * @returns {Promise<Document>}
 */
function promiseDocumentReady(doc) {
  if (doc.readyState == "interactive" || doc.readyState == "complete") {
    return Promise.resolve(doc);
  }

  return new Promise(resolve => {
    doc.addEventListener(
      "DOMContentLoaded",
      function onReady(event) {
        if (event.target === event.currentTarget) {
          doc.removeEventListener("DOMContentLoaded", onReady, true);
          resolve(doc);
        }
      },
      true
    );
  });
}

/**
  * Returns a Promise which resolves when the given window's document's DOM has
  * fully loaded, the <head> stylesheets have fully loaded, and we have hit an
  * idle time.
  *
  * @param {Window} window The window whose document we will await
                           the readiness of.
  * @returns {Promise<IdleDeadline>}
  */
function promiseDocumentIdle(window) {
  return window.document.documentReadyForIdle.then(() => {
    return new Promise(resolve =>
      window.requestIdleCallback(resolve, { timeout: lazy.idleTimeout })
    );
  });
}

/**
 * Returns a Promise which resolves when the given document is fully
 * loaded.
 *
 * @param {Document} doc The document to await the load of.
 * @returns {Promise<Document>}
 */
function promiseDocumentLoaded(doc) {
  if (doc.readyState == "complete") {
    return Promise.resolve(doc);
  }

  return new Promise(resolve => {
    doc.defaultView.addEventListener("load", () => resolve(doc), {
      once: true,
    });
  });
}

/**
 * Returns a Promise which resolves when the given event is dispatched to the
 * given element.
 *
 * @param {Element} element
 *        The element on which to listen.
 * @param {string} eventName
 *        The event to listen for.
 * @param {boolean} [useCapture = true]
 *        If true, listen for the even in the capturing rather than
 *        bubbling phase.
 * @param {function(Event): boolean} [test]
 *        An optional test function which, when called with the
 *        observer's subject and data, should return true if this is the
 *        expected event, false otherwise.
 * @returns {Promise<Event>}
 */
function promiseEvent(
  element,
  eventName,
  useCapture = true,
  test = () => true
) {
  return new Promise(resolve => {
    function listener(event) {
      if (test(event)) {
        element.removeEventListener(eventName, listener, useCapture);
        resolve(event);
      }
    }
    element.addEventListener(eventName, listener, useCapture);
  });
}

/**
 * Returns a Promise which resolves the given observer topic has been
 * observed.
 *
 * @param {string} topic
 *        The topic to observe.
 * @param {function(any, string): boolean} [test]
 *        An optional test function which, when called with the
 *        observer's subject and data, should return true if this is the
 *        expected notification, false otherwise.
 * @returns {Promise<object>}
 */
function promiseObserved(topic, test = () => true) {
  return new Promise(resolve => {
    let observer = (subject, topic, data) => {
      if (test(subject, data)) {
        Services.obs.removeObserver(observer, topic);
        resolve({ subject, data });
      }
    };
    Services.obs.addObserver(observer, topic);
  });
}

function getMessageManager(target) {
  if (target.frameLoader) {
    return target.frameLoader.messageManager;
  }
  return target;
}

function flushJarCache(jarPath) {
  Services.obs.notifyObservers(null, "flush-cache-entry", jarPath);
}
function parseMatchPatterns(patterns, options) {
  try {
    return new MatchPatternSet(patterns, options);
  } catch (e) {
    let pattern;
    for (pattern of patterns) {
      try {
        new MatchPattern(pattern, options);
      } catch (e) {
        throw new ExtensionError(`Invalid url pattern: ${pattern}`);
      }
    }
    // Unexpectedly MatchPatternSet threw, but MatchPattern did not.
    throw e;
  }
}

/**
 * Fetch icon content and convert it to a data: URI.
 *
 * @param {string} iconUrl Icon url to fetch.
 * @returns {Promise<string>}
 */
async function makeDataURI(iconUrl) {
  let response;
  try {
    response = await fetch(iconUrl);
  } catch (e) {
    // Failed to fetch, ignore engine's favicon.
    Cu.reportError(e);
    return;
  }
  let buffer = await response.arrayBuffer();
  let contentType = response.headers.get("content-type");
  let bytes = new Uint8Array(buffer);
  let str = String.fromCharCode.apply(null, bytes);
  return `data:${contentType};base64,${btoa(str)}`;
}

export var ExtensionUtils = {
  flushJarCache,
  getInnerWindowID,
  getMessageManager,
  getUniqueId,
  filterStack,
  makeDataURI,
  parseMatchPatterns,
  promiseDocumentIdle,
  promiseDocumentLoaded,
  promiseDocumentReady,
  promiseEvent,
  promiseObserved,
  promiseTimeout,
  DefaultMap,
  DefaultWeakMap,
  ExtensionError,
  LimitedSet,
};
