/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarContentUtils } from "chrome://browser/content/urlbar/UrlbarContentUtils.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
  UrlbarShared: "chrome://browser/content/urlbar/UrlbarShared.mjs",
});

/**
 * The object the actor publishes as `window.UrlbarActorPort` to expose
 * certain actor functions and [ChromeOnly] capabilities to content.
 *
 * @typedef {ReturnType<UrlbarChild["createPort"]>} UrlbarActorPort
 */

/**
 * Child-process counterpart of `UrlbarParent`. `UrlbarChildController` builds its
 * own parent controller; this actor runs the message-path lifecycle.
 *
 * - Direct path (default for chrome `<moz-urlbar>`): both actors live in the
 *   parent process, so the child builds a real `UrlbarParentController` in place
 *   and invokes it synchronously.
 * - Message path: a content-process `<moz-urlbar>` (e.g. about:newtab), or
 *   chrome when `browser.urlbar.ipc.chromeMessagePassing` is set (so the message
 *   path runs in CI). The child builds a `UrlbarParentControllerProxy` that
 *   trades messages with the parent-side controller, identified by an
 *   `instanceId` this actor allocates. The parent's notifications are
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
    this.#maybeSendAsyncMessage("Destroy", { instanceId });
  });

  /**
   * Sends a message if the actor is not destroyed.
   *
   * @param {string} name
   * @param {any} data
   */
  #maybeSendAsyncMessage(name, data) {
    try {
      this.sendAsyncMessage(name, data);
    } catch (ex) {
      if (ex.name == "InvalidStateError") {
        // Actor was destroyed.
        return;
      }
      throw ex;
    }
  }

  /**
   * Sends a query. If the actor is destroyed, the returned promise won't ever
   * resolve.
   *
   * @param {string} name
   * @param {any} data
   * @returns {Promise<any>}
   */
  async #maybeSendQuery(name, data) {
    try {
      return await this.sendQuery(name, data);
    } catch (ex) {
      if (
        ex.name == "InvalidStateError" || // Destroyed before the query was sent.
        ex.name == "AbortError" // Destroyed while the query was in flight.
      ) {
        return new Promise(() => {});
      }
      throw ex;
    }
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
   * Exposes the actor's content-facing surface on the window for a message-path
   * `<moz-urlbar>`, which can't reach the `[ChromeOnly]`
   * `windowGlobalChild.getActor` nor hold the system-principal actor. Such an
   * input reads `window.UrlbarActorPort` and calls it in the actor's place (see
   * `UrlbarChildController`), and `UrlbarContentPrefs` reads the pref methods
   * from it.
   *
   * This is the single surface the content realm gets, so to give content
   * another capability, add it here.
   *
   * Object returns are `cloneInto`'d so content can read them; `sendQuery`
   * returns a content-realm promise (see `#wrapPromise`).
   */
  exposePort() {
    let unprivileged = !this.manager.parentActor;
    let win = unprivileged
      ? Cu.waiveXrays(this.contentWindow)
      : this.contentWindow;
    let port = this.createPort(win, unprivileged);
    win.UrlbarActorPort = unprivileged
      ? Cu.cloneInto(port, win, { cloneFunctions: true })
      : port;
  }

  /**
   * @param {Window} win
   *   The window the port's return values are cloned into.
   * @param {boolean} unprivileged
   *   Whether the window is a content-principal one.
   */
  createPort(win, unprivileged) {
    return {
      sendAsyncMessage: this.#maybeSendAsyncMessage.bind(this),
      /** @type {typeof this.sendQuery} */
      sendQuery: (name, data) =>
        unprivileged
          ? this.#wrapPromise(this.#maybeSendQuery(name, data), win)
          : this.sendQuery(name, data),
      registerMessagePathInput: this.registerMessagePathInput.bind(this),
      registerChildController: this.registerChildController.bind(this),
      whereToOpenLink: UrlbarContentUtils.whereToOpenLink,
      willLoadInBackground: UrlbarContentUtils.willLoadInBackground,
      /** @type {typeof UrlbarContentUtils.getFixupPrimitives} */
      getFixupPrimitives: (searchString, isPrivate) =>
        this.#forWindow(
          UrlbarContentUtils.getFixupPrimitives(searchString, isPrivate),
          win
        ),
      /** @type {(threshold: number) => Promise<?string>} */
      getClipboardTextIfSmallerThan: threshold => {
        let promise = UrlbarContentUtils.getClipboardTextIfSmallerThan(
          threshold,
          this.contentWindow
        );
        return unprivileged ? this.#wrapPromise(promise, win) : promise;
      },
      getDisplaySpec: UrlbarContentUtils.getDisplaySpec,
      getReaderOriginalDisplaySpec:
        UrlbarContentUtils.getReaderOriginalDisplaySpec,
      unEscapeURIForUI: UrlbarContentUtils.unEscapeURIForUI,
      hasKnownPublicSuffix: UrlbarContentUtils.hasKnownPublicSuffix,
      getSupportUrl: UrlbarContentUtils.getSupportUrl,
      getPlatform: UrlbarContentUtils.getPlatform,
      isWindowPrivate: unprivileged
        ? lazy.PrivateBrowsingUtils.isContentWindowPrivate(this.contentWindow)
        : lazy.PrivateBrowsingUtils.isWindowPrivate(this.contentWindow),
      isTextDirectionRTL: UrlbarContentUtils.isTextDirectionRTL,
      /** @type {typeof lazy.UrlbarPrefs.get} */
      getPref: name => this.#forWindow(lazy.UrlbarPrefs.get(name), win),
      addPrefObserver: lazy.UrlbarPrefs.addObserver.bind(lazy.UrlbarPrefs),
      removePrefObserver: lazy.UrlbarPrefs.removeObserver.bind(
        lazy.UrlbarPrefs
      ),
      toggleResultMenuKeyboardAccessible:
        lazy.UrlbarPrefs.toggleResultMenuKeyboardAccessible.bind(
          lazy.UrlbarPrefs
        ),
      hasPrefUserValue: lazy.UrlbarPrefs.hasUserValue.bind(lazy.UrlbarPrefs),
      prefAffectsResults: lazy.UrlbarPrefs.affectsResults.bind(
        lazy.UrlbarPrefs
      ),
    };
  }

  /**
   * Hands a value to the port's consumer, cloning it only for an unprivileged
   * one.
   *
   * @template T
   * @param {T} value
   * @param {Window} win
   * @returns {T}
   */
  #forWindow(value, win) {
    return this.manager.parentActor ? value : Cu.cloneInto(value, win);
  }

  /**
   * `DOMDocElementInserted`, the actor's only registered event, fires as the
   * document is created, which is early enough to publish the port before page
   * script runs.
   */
  handleEvent() {
    // The port is what the message path routes through, so a chrome window on
    // that path gets one too and exercises the same code an unprivileged input
    // does. A direct-path window reaches its privileged side itself.
    if (UrlbarContentUtils.usesMessagePath()) {
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
   * notifications can be dispatched to it. Held weakly so it (and its input)
   * stay collectable.
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
      case "InvokeContentAction":
        this.#invokeContentAction(message.data);
        break;
    }
  }

  /**
   * Invokes an allowed controller/input/view method the parent requested
   * (e.g. `view.close()`, `input.startQuery()`).
   *
   * @param {object} data The `InvokeContentAction` message data.
   * @param {number} data.instanceId The instance to act on.
   * @param {"controller"|"input"|"view"} data.target
   *   Which content object to invoke on.
   * @param {string} data.method The allowed method to call.
   * @param {any[]} data.args The method arguments.
   */
  #invokeContentAction({ instanceId, target, method, args }) {
    let child = this.#childControllers.get(instanceId)?.deref();
    if (!child) {
      this.#childControllers.delete(instanceId);
      return;
    }
    /** @type {readonly string[]} */
    let allowlist = lazy.UrlbarShared.INVOKABLE_CONTENT_ACTIONS[target];
    if (!allowlist?.includes(method)) {
      throw new Error(`Urlbar: disallowed content action ${target}.${method}`);
    }
    // In a content process `child.input`/`child.view` are content objects;
    // waive Xrays so their methods are callable from here.
    if (!this.manager.parentActor) {
      child = Cu.waiveXrays(child);
    }
    let receiver = target == "controller" ? child : child[target];
    receiver?.[method](...this.#forContent(args));
  }
}
