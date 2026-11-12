/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  ContentPref,
  _methodsCallableFromChild,
  cbHandleCompletion,
  cbHandleError,
  cbHandleResult,
  safeCallback,
} from "moz-src:///toolkit/components/contentprefs/ContentPrefUtils.sys.mjs";

// We only need one bit of information out of the context.
function contextArg(context) {
  return context && context.usePrivateBrowsing
    ? { usePrivateBrowsing: true }
    : null;
}

function NYI() {
  throw new Error("Do not add any new users of these functions");
}

function CallbackCaller(callback) {
  this._callback = callback;
}

CallbackCaller.prototype = {
  handleResult(contentPref) {
    cbHandleResult(
      this._callback,
      new ContentPref(contentPref.domain, contentPref.name, contentPref.value)
    );
  },

  handleError(result) {
    cbHandleError(this._callback, result);
  },

  handleCompletion(reason) {
    cbHandleCompletion(this._callback, reason);
  },
};

export class ContentPrefsChild extends JSProcessActorChild {
  constructor() {
    super();

    // Map from pref name -> set of observers
    this._observers = new Map();

    // Map from random ID string -> CallbackCaller, per request
    this._requests = new Map();
  }

  _getRandomId() {
    return Services.uuid.generateUUID().toString();
  }

  receiveMessage(msg) {
    let data = msg.data;
    let callback;
    switch (msg.name) {
      case "ContentPrefs:HandleResult":
        callback = this._requests.get(data.requestId);
        callback.handleResult(data.contentPref);
        break;

      case "ContentPrefs:HandleError":
        callback = this._requests.get(data.requestId);
        callback.handleError(data.error);
        break;

      case "ContentPrefs:HandleCompletion":
        callback = this._requests.get(data.requestId);
        this._requests.delete(data.requestId);
        callback.handleCompletion(data.reason);
        break;

      case "ContentPrefs:NotifyObservers": {
        let observerList = this._observers.get(data.name);
        if (!observerList) {
          break;
        }

        for (let observer of observerList) {
          safeCallback(observer, data.callback, data.args);
        }

        break;
      }
    }
  }

  callFunction(call, args, callback) {
    let requestId = this._getRandomId();
    let data = { call, args, requestId };

    this._requests.set(requestId, new CallbackCaller(callback));
    this.sendAsyncMessage("ContentPrefs:FunctionCall", data);
  }

  addObserverForName(name, observer) {
    let set = this._observers.get(name);
    if (!set) {
      set = new Set();

      // This is the first observer for this name. Start listening for changes
      // to it.
      this.sendAsyncMessage("ContentPrefs:AddObserverForName", {
        name,
      });
      this._observers.set(name, set);
    }

    set.add(observer);
  }

  removeObserverForName(name, observer) {
    let set = this._observers.get(name);
    if (!set) {
      return;
    }

    set.delete(observer);
    if (set.size === 0) {
      // This was the last observer for this name. Stop listening for changes.
      this.sendAsyncMessage("ContentPrefs:RemoveObserverForName", {
        name,
      });

      this._observers.delete(name);
    }
  }
}

export var ContentPrefServiceChild = {
  QueryInterface: ChromeUtils.generateQI(["nsIContentPrefService2"]),

  addObserverForName: (name, observer) => {
    ChromeUtils.domProcessChild
      .getActor("ContentPrefs")
      .addObserverForName(name, observer);
  },
  removeObserverForName: (name, observer) => {
    ChromeUtils.domProcessChild
      .getActor("ContentPrefs")
      .removeObserverForName(name, observer);
  },

  getCachedByDomainAndName: NYI,
  getCachedBySubdomainAndName: NYI,
  getCachedGlobal: NYI,
  extractDomain: NYI,
};

function forwardMethodToParent(method, signature, ...args) {
  // Ignore superfluous arguments
  args = args.slice(0, signature.length);

  // Process context argument for forwarding
  let contextIndex = signature.indexOf("context");
  if (contextIndex > -1) {
    args[contextIndex] = contextArg(args[contextIndex]);
  }
  // Take out the callback argument, if present.
  let callbackIndex = signature.indexOf("callback");
  let callback = null;
  if (callbackIndex > -1 && args.length > callbackIndex) {
    callback = args.splice(callbackIndex, 1)[0];
  }

  let actor = ChromeUtils.domProcessChild.getActor("ContentPrefs");
  actor.callFunction(method, args, callback);
}

for (let [method, signature] of _methodsCallableFromChild) {
  ContentPrefServiceChild[method] = forwardMethodToParent.bind(
    ContentPrefServiceChild,
    method,
    signature
  );
}
