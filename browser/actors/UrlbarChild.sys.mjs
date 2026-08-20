/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  UrlbarContentUtils: "chrome://browser/content/urlbar/UrlbarContentUtils.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

// The content-side input/view methods a parent-side provider hook may invoke
// over the actor (see the `input`/`view` stand-ins on `UrlbarChildControllerProxy`).
// An allowlist, so an `InvokeContentAction` message can't reach arbitrary methods.
const INVOKABLE_CONTENT_ACTIONS = {
  input: new Set(["search", "setValue", "startQuery"]),
  view: new Set([
    "acknowledgeFeedback",
    "clearL10nCache",
    "clearTopSitesCache",
    "close",
    "updateResultMenuCommands",
    "startTail150",
  ]),
};

/**
 * @import {UrlbarParent} from "./UrlbarParent.sys.mjs"
 * @import {UrlbarParentController} from "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs"
 * @import {UrlbarChildController} from "chrome://browser/content/urlbar/UrlbarChildController.mjs"
 */

/**
 * Child-process counterpart of `UrlbarParent`. `UrlbarChildController` builds its
 * own parent controller; this actor tells the two transports apart
 * (`usesMessagePath`) and runs the message-path lifecycle.
 *
 * - Direct path (default for chrome `<moz-urlbar>`): both actors live in the
 *   parent process, so the child builds a real `UrlbarParentController` in place
 *   and invokes it synchronously.
 * - Message path: a content-process `<moz-urlbar>` (e.g. about:newtab), or
 *   chrome when `browser.urlbar.ipc.chromeMessagePassing` is set (so the message
 *   path runs in CI). The child builds a `UrlbarParentControllerProxy` that
 *   trades messages with the parent-side controller, identified by an
 *   `instanceId` this actor allocates. The parent's `Notify` messages are
 *   dispatched back to the paired child controller, held weakly (keyed by
 *   `instanceId`) so as not to pin its input.
 *
 * On the message path the parent retains its controller strongly (keyed by
 * `instanceId`), so we tie its lifetime to the input: the input is registered in
 * a `FinalizationRegistry` that sends `Destroy(instanceId)` when the input is
 * collected, letting the parent drop its entry.
 */
export class UrlbarChild extends JSWindowActorChild {
  #nextInstanceId = 0;

  /** @type {Map<number, WeakRef<UrlbarChildController>>} */
  #childControllers = new Map();

  // Sends `Destroy(instanceId)` to the parent once a message-path input is
  // collected, so the parent can drop the controller it holds for it.
  #destroyRegistry = new FinalizationRegistry(instanceId => {
    this.#childControllers.delete(instanceId);
    try {
      this.sendAsyncMessage("Destroy", { instanceId });
    } catch (ex) {
      // The actor is already gone (e.g. the window global was torn down), so
      // the parent's controllers went with it; nothing left to clean up.
    }
  });

  /**
   * Whether this `<moz-urlbar>` uses the actor message path -- a content-process
   * input (no in-process parent global), or chrome with
   * `browser.urlbar.ipc.chromeMessagePassing` -- rather than the in-process
   * direct path. `UrlbarChildController` keys its controller construction on
   * this: direct builds a real `UrlbarParentController` in place, message builds
   * a `UrlbarParentControllerProxy`.
   *
   * @type {boolean}
   */
  get usesMessagePath() {
    return (
      !this.manager.parentActor ||
      lazy.UrlbarPrefs.get("ipc.chromeMessagePassing")
    );
  }

  /**
   * Converts a privileged promise into one the content realm can consume: the
   * resolution is cloned in, and a rejection is re-created as a content-realm
   * `Error` carrying only its message, so neither a system-principal object nor
   * a chrome stack crosses the boundary.
   *
   * @param {Promise} promise
   *   The privileged promise to convert.
   * @param {Window} win
   *   The waived content window to build the content-realm promise in.
   * @returns {Promise}
   */
  #wrapPromise(promise, win) {
    return new win.Promise((resolve, reject) =>
      promise.then(
        result => resolve(Cu.cloneInto(result, win)),
        ex => {
          try {
            reject(new win.Error(ex?.message ?? String(ex)));
          } catch {
            // The content window went away, so there's no realm left to build
            // an error in.
            reject();
          }
        }
      )
    );
  }

  /**
   * Clones a payload the parent sent into the content realm, so content code can
   * read it: an object left in this realm reaches content as an Xray, which
   * denies even `Symbol.iterator`. In the parent both sides share a realm and the
   * payload passes through as it is.
   *
   * @param {any[]} args
   *   The arguments to hand to content.
   * @returns {any[]}
   */
  #forContent(args) {
    return this.manager.parentActor
      ? args
      : Cu.cloneInto(args, Cu.waiveXrays(this.contentWindow));
  }

  /**
   * Exposes the actor's content-facing surface on the window for a content-realm
   * `<moz-urlbar>`, which can't reach the `[ChromeOnly]`
   * `windowGlobalChild.getActor` nor hold the system-principal actor. Such an
   * input reads `window.UrlbarActorPort` and calls it in the actor's place (see
   * `UrlbarChildController`), and `UrlbarContentPrefs` reads the pref methods
   * from it. Only meaningful in a child process; in the parent both reach their
   * privileged side directly.
   *
   * This is the single surface the content realm gets, so to give content
   * another capability, add it here.
   *
   * Object returns are `cloneInto`'d so content can read them; `sendQuery`
   * returns a content-realm promise (see `#wrapPromise`).
   */
  exposePort() {
    let win = Cu.waiveXrays(this.contentWindow);
    win.UrlbarActorPort = Cu.cloneInto(
      {
        sendAsyncMessage: (name, data) => this.sendAsyncMessage(name, data),
        sendQuery: (name, data) =>
          this.#wrapPromise(this.sendQuery(name, data), win),
        registerMessagePathInput: input => this.registerMessagePathInput(input),
        registerChildController: (instanceId, child) =>
          this.registerChildController(instanceId, child),
        whereToOpenLink: event =>
          lazy.UrlbarContentUtils.whereToOpenLink(event),
        willLoadInBackground: (where, params) =>
          lazy.UrlbarContentUtils.willLoadInBackground(where, params),
        getFixupPrimitives: (searchString, isPrivate) =>
          Cu.cloneInto(
            lazy.UrlbarContentUtils.getFixupPrimitives(searchString, isPrivate),
            win
          ),
        getDisplaySpec: url => lazy.UrlbarContentUtils.getDisplaySpec(url),
        unEscapeURIForUI: uri => lazy.UrlbarContentUtils.unEscapeURIForUI(uri),
        getSupportUrl: topic => lazy.UrlbarContentUtils.getSupportUrl(topic),
        getPlatform: () => lazy.UrlbarContentUtils.getPlatform(),
        isWindowPrivate: lazy.PrivateBrowsingUtils.isContentWindowPrivate(
          this.contentWindow
        ),
        isTextDirectionRTL: (value, window) =>
          lazy.UrlbarContentUtils.isTextDirectionRTL(value, window),
        getPref: name => Cu.cloneInto(lazy.UrlbarPrefs.get(name), win),
        addPrefObserver: observer => lazy.UrlbarPrefs.addObserver(observer),
        removePrefObserver: observer =>
          lazy.UrlbarPrefs.removeObserver(observer),
      },
      win,
      { cloneFunctions: true }
    );
  }

  /**
   * `DOMDocElementInserted`, the actor's only registered event, fires as the
   * document is created, which is early enough to publish the port before page
   * script runs.
   */
  handleEvent() {
    // Only a content realm reads the port; chrome holds the actor and imports
    // UrlbarPrefs directly, so don't publish it on every chrome window.
    if (!this.manager.parentActor) {
      this.exposePort();
    }
  }

  /**
   * Registers a message-path `<moz-urlbar>` input for teardown -- so the parent
   * drops the controller once the input is collected -- and returns the instance
   * id the child controller pairs with the proxy it builds.
   *
   * @param {object} input
   *   The `UrlbarInput`/`SmartbarInput` that owns the child controller.
   * @returns {number} The instance id to construct the proxy with.
   */
  registerMessagePathInput(input) {
    let instanceId = ++this.#nextInstanceId;
    this.#destroyRegistry.register(input, instanceId);
    return instanceId;
  }

  /**
   * Records the message-path child controller for an instance so the parent's
   * `Notify` messages can be dispatched to it. Held weakly so it (and its
   * input) stay collectable.
   *
   * @param {number} instanceId
   *   The instance the controller was created for.
   * @param {UrlbarChildController} child
   *   The paired child controller.
   */
  registerChildController(instanceId, child) {
    this.#childControllers.set(instanceId, new WeakRef(child));
  }

  receiveMessage(message) {
    switch (message.name) {
      case "Notify":
        this.#receiveNotify(message.data);
        break;
      case "InvokeContentAction":
        this.#invokeContentAction(message.data);
        break;
      case "UpdateEngineStore":
        this.#updateEngineStore(message.data);
        break;
    }
  }

  /**
   * Dispatches a parent-side `notify()` to the paired child controller.
   *
   * @param {object} data The `Notify` message data.
   * @param {number} data.instanceId The instance whose child controller to notify.
   * @param {string} data.name The notification (listener method) name.
   * @param {any[]} data.params The notification arguments.
   */
  #receiveNotify({ instanceId, name, params }) {
    let child = this.#childControllers.get(instanceId)?.deref();
    if (!child) {
      this.#childControllers.delete(instanceId);
      return;
    }
    // In a content process the child controller is a content object; waive
    // Xrays so its methods (and `input`/`view`) are callable from here.
    if (!this.manager.parentActor) {
      child = Cu.waiveXrays(child);
    }
    // The wire form crosses as plain data and the child controller builds the
    // query context from it, so the object is born in the realm that reads it.
    // Deserializing here would leave content an Xray over it, whose properties
    // all read `undefined`.
    child.notifyFromWire(name, ...this.#forContent(params));
  }

  /**
   * Invokes an allowed input/view method a parent-side provider hook
   * requested (e.g. `view.close()`, `input.startQuery()`), on the real
   * content-side objects.
   *
   * @param {object} data The `InvokeContentAction` message data.
   * @param {number} data.instanceId The instance whose input/view to act on.
   * @param {"input"|"view"} data.target Which content object to invoke on.
   * @param {string} data.method The allowed method to call.
   * @param {any[]} data.args The method arguments.
   */
  #invokeContentAction({ instanceId, target, method, args }) {
    let child = this.#childControllers.get(instanceId)?.deref();
    if (!child) {
      this.#childControllers.delete(instanceId);
      return;
    }
    if (!INVOKABLE_CONTENT_ACTIONS[target]?.has(method)) {
      console.error(`Urlbar: disallowed content action ${target}.${method}`);
      return;
    }
    // In a content process `child.input`/`child.view` are content objects;
    // waive Xrays so their methods are callable from here.
    if (!this.manager.parentActor) {
      child = Cu.waiveXrays(child);
    }
    child[target]?.[method](...this.#forContent(args));
  }

  #updateEngineStore({ instanceId, args }) {
    let child = this.#childControllers.get(instanceId)?.deref();
    if (!child) {
      this.#childControllers.delete(instanceId);
      return;
    }
    // In a content process the child controller is a content object; waive
    // Xrays so its methods are callable from here.
    if (!this.manager.parentActor) {
      child = Cu.waiveXrays(child);
    }
    child.updateEngineStore(...this.#forContent(args));
  }
}
