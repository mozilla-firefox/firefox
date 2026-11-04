/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// MESSAGE_TYPE_LIST and MESSAGE_TYPE_HASH, and overrides importESModule
// to be a no-op (which can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { MESSAGE_TYPE_LIST, MESSAGE_TYPE_HASH: msg } =
  ChromeUtils.importESModule("resource:///modules/asrouter/ActorConstants.mjs");

const VALID_TYPES = new Set(MESSAGE_TYPE_LIST);

export class ASRouterChild extends JSWindowActorChild {
  constructor() {
    super();
    this.observers = new Set();
  }

  didDestroy() {
    this.observers.clear();
  }

  actorCreated() {
    // NOTE: DOMDocElementInserted may be called multiple times per
    // PWindowGlobal due to the initial about:blank document's window global
    // being re-used.
    const window = this.contentWindow;
    Cu.exportFunction(this.asRouterMessage.bind(this), window, {
      defineAs: "ASRouterMessage",
    });
    Cu.exportFunction(this.addParentListener.bind(this), window, {
      defineAs: "ASRouterAddParentListener",
    });
    Cu.exportFunction(this.removeParentListener.bind(this), window, {
      defineAs: "ASRouterRemoveParentListener",
    });
  }

  handleEvent() {
    // DOMDocElementCreated is only used to create the actor.
  }

  addParentListener(listener) {
    this.observers.add(listener);
  }

  removeParentListener(listener) {
    this.observers.delete(listener);
  }

  receiveMessage({ name, data }) {
    switch (name) {
      case "UpdateAdminState":
      case "ClearProviders": {
        this.observers.forEach(listener => {
          let result = Cu.cloneInto(
            {
              type: name,
              data,
            },
            this.contentWindow
          );
          listener(result);
        });
        break;
      }
    }
  }

  wrapPromise(promise) {
    return new this.contentWindow.Promise((resolve, reject) =>
      promise.then(resolve, reject)
    );
  }

  sendQuery(aName, aData = null) {
    return this.wrapPromise(
      new Promise(resolve => {
        super.sendQuery(aName, aData).then(result => {
          resolve(Cu.cloneInto(result, this.contentWindow));
        });
      })
    );
  }

  asRouterMessage({ type, data }) {
    // Some legacy privileged addons send this message, but it got removed from
    // VALID_TYPES in bug 1715158. Thankfully, these addons don't appear to
    // require any actions from this message - just a Promise that resolves.
    if (type === "NEWTAB_MESSAGE_REQUEST") {
      return this.wrapPromise(Promise.resolve());
    }

    if (VALID_TYPES.has(type)) {
      switch (type) {
        case msg.DISABLE_PROVIDER:
        case msg.ENABLE_PROVIDER:
        case msg.EXPIRE_QUERY_CACHE:
        case msg.FORCE_PRIVATE_BROWSING_WINDOW:
        case msg.IMPRESSION:
        case msg.RESET_PROVIDER_PREF:
        case msg.SET_PROVIDER_USER_PREF: {
          return this.sendAsyncMessage(type, data);
        }
        default: {
          // these messages need a response
          return this.sendQuery(type, data);
        }
      }
    }
    throw new Error(`Unexpected type "${type}"`);
  }
}
