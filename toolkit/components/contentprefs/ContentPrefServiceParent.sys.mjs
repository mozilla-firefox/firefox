/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  _methodsCallableFromChild:
    "moz-src:///toolkit/components/contentprefs/ContentPrefUtils.sys.mjs",
});

let loadContext = Cu.createLoadContext();
let privateLoadContext = Cu.createPrivateLoadContext();

function contextArg(context) {
  return context && context.usePrivateBrowsing
    ? privateLoadContext
    : loadContext;
}

export class ContentPrefsParent extends JSProcessActorParent {
  constructor() {
    super();

    // The names we're using this observer object for, used to keep track
    // of the number of names we care about as well as for removing this
    // observer if its associated process goes away.
    this._prefsToObserve = new Set();
    this._observer = null;
  }

  didDestroy() {
    if (this._observer) {
      for (let i of this._prefsToObserve) {
        lazy.cps2.removeObserverForName(i, this._observer);
      }
      this._observer = null;
    }
  }

  receiveMessage(msg) {
    switch (msg.name) {
      case "ContentPrefs:AddObserverForName": {
        // The child process is responsible for not adding multiple parent
        // observers for the same name.
        let actor = this;
        if (!this._observer) {
          this._observer = {
            onContentPrefSet(group, name, value, isPrivate) {
              actor.onContentPrefSet(group, name, value, isPrivate);
            },
            onContentPrefRemoved(group, name, isPrivate) {
              actor.onContentPrefRemoved(group, name, isPrivate);
            },
          };
        }

        let prefName = msg.data.name;
        this._prefsToObserve.add(prefName);
        lazy.cps2.addObserverForName(prefName, this._observer);
        break;
      }

      case "ContentPrefs:RemoveObserverForName": {
        let prefName = msg.data.name;
        this._prefsToObserve.delete(prefName);
        if (this._prefsToObserve.size == 0) {
          lazy.cps2.removeObserverForName(prefName, this._observer);
          this._observer = null;
        }
        break;
      }

      case "ContentPrefs:FunctionCall": {
        let data = msg.data;
        let signature;

        if (
          !lazy._methodsCallableFromChild.some(([method, args]) => {
            if (method == data.call) {
              signature = args;
              return true;
            }
            return false;
          })
        ) {
          throw new Error(`Can't call ${data.call} from child!`);
        }

        let actor = this;
        let args = data.args;

        return new Promise(() => {
          let listener = {
            handleResult(pref) {
              actor.sendAsyncMessage("ContentPrefs:HandleResult", {
                requestId: data.requestId,
                contentPref: {
                  domain: pref.domain,
                  name: pref.name,
                  value: pref.value,
                },
              });
            },

            handleError(error) {
              actor.sendAsyncMessage("ContentPrefs:HandleError", {
                requestId: data.requestId,
                error,
              });
            },
            handleCompletion(reason) {
              actor.sendAsyncMessage("ContentPrefs:HandleCompletion", {
                requestId: data.requestId,
                reason,
              });
            },
          };
          // Push our special listener.
          args.push(listener);

          // Process context argument for forwarding
          let contextIndex = signature.indexOf("context");
          if (contextIndex > -1) {
            args[contextIndex] = contextArg(args[contextIndex]);
          }

          // And call the function.
          lazy.cps2[data.call](...args);
        });
      }
    }

    return undefined;
  }

  onContentPrefSet(group, name, value, isPrivate) {
    this.sendAsyncMessage("ContentPrefs:NotifyObservers", {
      name,
      callback: "onContentPrefSet",
      args: [group, name, value, isPrivate],
    });
  }

  onContentPrefRemoved(group, name, isPrivate) {
    this.sendAsyncMessage("ContentPrefs:NotifyObservers", {
      name,
      callback: "onContentPrefRemoved",
      args: [group, name, isPrivate],
    });
  }
}

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "cps2",
  "@mozilla.org/content-pref/service;1",
  Ci.nsIContentPrefService2
);
