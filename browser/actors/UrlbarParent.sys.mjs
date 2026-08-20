/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { blobAsDataURL } from "moz-src:///toolkit/modules/FaviconUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  UrlbarParentController:
    "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs",
  UrlbarQueryContext: "chrome://browser/content/urlbar/UrlbarQueryContext.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
});

/**
 * @import {UrlbarParentController} from "moz-src:///browser/components/urlbar/UrlbarParentController.sys.mjs"
 * @import {SearchEngineStore} from "chrome://browser/content/urlbar/SearchEngineStore.mjs"
 */

/**
 * Parent-process endpoint of the `UrlbarChild` actor pair, for the message path.
 * A content-side `UrlbarChildController` (content-process `<moz-urlbar>`, or
 * chrome with `browser.urlbar.ipc.chromeMessagePassing`) holds a
 * `UrlbarParentControllerProxy` and trades actor messages with us: we build the
 * `UrlbarParentController` from the `Init` payload and retain it in a `Map` keyed
 * by the child-assigned `instanceId`, routing subsequent messages to it. There is
 * one actor per window global, so that `Map` holds a controller for each
 * message-path input in that global. The controller's notifications go back to
 * the child as `Notify` messages (dispatched through a parent-side
 * `UrlbarChildControllerProxy` stand-in). The controller is torn down on the
 * `Destroy` message the child sends when its input is collected (via a
 * `FinalizationRegistry`), and in `didDestroy` for a global that goes away whole.
 *
 * The direct path (in-process chrome `<moz-urlbar>`) doesn't go through this
 * actor at all: `UrlbarChildController` builds its `UrlbarParentController` in
 * place, since both live in the same process.
 */
export class UrlbarParent extends JSWindowActorParent {
  /** @type {Map<number, UrlbarParentController>} */
  #messageControllers = new Map();

  /**
   * Message path: routes child->parent messages to the controller identified
   * by `instanceId`, deserializing their payloads.
   *
   * @param {object} message
   *   The actor message, with `name` and `data`.
   */
  receiveMessage(message) {
    // The sender may be a content process (about:newtab), so treat the payload
    // as untrusted: every message carries a numeric instanceId, and only known
    // names and live controllers are acted on below.
    let { instanceId } = message.data ?? {};
    if (typeof instanceId != "number") {
      return undefined;
    }

    if (message.name == "Init") {
      let { sapName, isPrivate } = message.data;
      let controller = new lazy.UrlbarParentController({
        sapName,
        isPrivate,
        actor: this,
      });
      // The real child controller lives across the boundary, so hand the
      // parent controller a proxy that forwards its notifications over the
      // actor.
      controller.setChild(new UrlbarChildControllerProxy(this, instanceId));
      this.#messageControllers.set(instanceId, controller);
      return undefined;
    }

    if (message.name == "Destroy") {
      this.#messageControllers.get(instanceId)?.destroy();
      this.#messageControllers.delete(instanceId);
      return undefined;
    }

    let controller = this.#messageControllers.get(instanceId);
    if (!controller) {
      return undefined;
    }

    switch (message.name) {
      case "GetHeuristicResult":
        return controller
          .getHeuristicResult(
            lazy.UrlbarQueryContext.fromWire(message.data.queryContext)
          )
          .then(result => result?.toWire() ?? null);
      case "ResolveFallbackNavigation":
        return controller
          .resolveFallbackNavigation(message.data.details)
          .then(outcome =>
            outcome.heuristicResult
              ? { heuristicResult: outcome.heuristicResult.toWire() }
              : outcome
          );
      case "RecordEngagement":
        controller.recordEngagement(message.data.wire);
        break;
      case "ResetEngagement":
        controller.resetEngagement();
        break;
      case "HandleBounceTrigger":
        controller.handleBounceTrigger(message.data.payload);
        break;
      case "TrackBounceBrowser":
        controller.trackBounceBrowser(message.data.browserId);
        break;
      case "RecordAutofillBackspace":
        controller.recordAutofillBackspace(message.data.url);
        break;
      case "RecordAutofillDeletion":
        controller.recordAutofillDeletion();
        break;
      case "ClearAutofillBackspaceEntryForUrl":
        controller.clearAutofillBackspaceEntryForUrl(message.data.url);
        break;
      case "HandleAutofillReintegration":
        controller.handleAutofillReintegration(message.data.url);
        break;
      case "RecordSearchMode":
        controller.recordSearchMode(message.data.searchMode);
        break;
      case "RecordSearchForm":
        controller.recordSearchForm(message.data.engineName);
        break;
      case "RecordSearch":
        controller.recordSearch(message.data);
        break;
      case "RecordSearchInOpenedTab":
        controller.recordSearchInOpenedTab(message.data.searchData);
        break;
      case "CheckKeywordURIFixup":
        controller.checkKeywordURIFixup(
          message.data.searchString,
          message.data.browserId
        );
        break;
      case "StartQuery":
        // Round-trips so the proxy's startQuery resolves at true completion with
        // the finished context. The context's results keep their data in private
        // fields, so reduce it to wire form like the QUERY_RESULTS notifications.
        return controller
          .startQuery(
            lazy.UrlbarQueryContext.fromWire(message.data.queryContext)
          )
          .then(context => context.toWire());
      case "CancelQuery":
        controller.cancelQuery();
        break;
      case "SpeculativeConnect":
        controller.speculativeConnect(
          lazy.UrlbarResult.fromWire(message.data.result),
          lazy.UrlbarQueryContext.fromWire(message.data.queryContext),
          message.data.reason
        );
        break;
      case "DismissAutofill":
        return controller.dismissAutofill(
          message.data.url,
          message.data.action
        );
      case "LoadURL":
        return controller.loadURL(message.data.loadData);
      case "FocusBrowser":
        return controller.focusBrowser(message.data.browserId);
      case "SwitchToTab":
        controller.switchToTab(message.data.loadData);
        break;
      case "AddToInputHistory":
        controller.addToInputHistory(message.data.url, message.data.input, {
          whenReady: message.data.whenReady,
        });
        break;
      case "RemoveResult":
        controller.removeResult(
          lazy.UrlbarResult.fromWire(message.data.result),
          message.data.options
        );
        break;
      case "SetLastQueryContextCache":
        controller.setLastQueryContextCache(
          lazy.UrlbarQueryContext.fromWire(message.data.queryContext)
        );
        break;
      case "ClearLastQueryContextCache":
        controller.clearLastQueryContextCache();
        break;
      // onBeforeSelection/onSelection drop their second argument (the selected
      // DOM element), which can't cross the boundary. Only
      // UrlbarProviderQuickSuggestContextualOptIn reads it, and it isn't active
      // on the message path. Bug 2052166 removes the parameter with that
      // provider.
      case "OnBeforeSelection":
        controller.onBeforeSelection(
          lazy.UrlbarResult.fromWire(message.data.result)
        );
        break;
      case "OnSelection":
        controller.onSelection(lazy.UrlbarResult.fromWire(message.data.result));
        break;
      case "InitEngineStore":
        controller.initEngineStore();
        break;
      case "GetEngineIconURL":
        return this.#getSerializableEngineIcon(
          controller,
          message.data.engineId
        );
      case "MarkEngineAsUsed":
        controller.markEngineAsUsed(message.data.engineId);
        break;
      case "OpenSERP":
        controller.openSERP(
          message.data.engineId,
          message.data.searchTerms,
          message.data.where,
          message.data.inBackground,
          message.data.browserId
        );
        break;
      case "OpenSearchForm":
        controller.openSearchForm(
          message.data.engineId,
          message.data.where,
          message.data.inBackground,
          message.data.browserId
        );
        break;
    }
    return undefined;
  }

  /**
   * Returns an engine's icon URL in a form that resolves in the child's
   * process.
   *
   * @param {UrlbarParentController} controller
   * @param {string} engineId
   * @returns {Promise<?string>}
   *   The icon URL, or null if the engine or its icon could not be found.
   */
  async #getSerializableEngineIcon(controller, engineId) {
    let url = await controller.getEngineIconURL(engineId);

    // A blob URL only resolves in the process that created it, so the icon
    // travels as a data URL instead.
    if (!url?.startsWith("blob:")) {
      return url;
    }

    try {
      let response = await fetch(url);
      return await blobAsDataURL(await response.blob());
    } catch (ex) {
      console.error(`Could not read the icon of engine ${engineId}`, ex);
      return null;
    }
  }

  didDestroy() {
    // Every controller here belongs to an input in this window global, and the
    // child sends `Destroy` per input from a FinalizationRegistry that never
    // runs when the whole global goes away. So tear them all down.
    for (let controller of this.#messageControllers.values()) {
      controller.destroy();
    }
    this.#messageControllers.clear();
  }
}

/**
 * Sends a message-path proxy's message to the child, dropping it if the child's
 * window global is gone.
 *
 * A controller can still be called after its window global has closed: work it
 * started -- a query, a provider's engagement hook -- resolves independently of
 * teardown, and sending on a closed global throws.
 *
 * @param {UrlbarParent} actor
 *   The actor to send through.
 * @param {string} name
 *   The message name.
 * @param {object} data
 *   The message payload.
 */
function sendToChild(actor, name, data) {
  if (!actor.manager || actor.manager.isClosed) {
    return;
  }
  actor.sendAsyncMessage(name, data);
}

/**
 * Parent-side stand-in for the `UrlbarChildController`, mirroring
 * `UrlbarParentControllerProxy` on the content side. The parent controller
 * calls `notify()` here, and we forward each notification to the real child
 * controller across the boundary as a `Notify` message, serializing any
 * `UrlbarQueryContext` argument.
 */
class UrlbarChildControllerProxy {
  /** @type {UrlbarParent} */
  #actor;

  /** @type {number} */
  #instanceId;

  constructor(actor, instanceId) {
    this.#actor = actor;
    this.#instanceId = instanceId;
  }

  notify(name, ...params) {
    sendToChild(this.#actor, "Notify", {
      instanceId: this.#instanceId,
      name,
      params: params.map(param =>
        param instanceof lazy.UrlbarQueryContext
          ? { serializedQueryContext: param.toWire() }
          : param
      ),
    });
  }

  /**
   * @type {typeof SearchEngineStore.prototype.receive}
   */
  updateEngineStore(...args) {
    sendToChild(this.#actor, "UpdateEngineStore", {
      instanceId: this.#instanceId,
      args,
    });
  }

  get input() {
    return new InputProxy(this.#actor, this.#instanceId);
  }

  get view() {
    return new ViewProxy(this.#actor, this.#instanceId);
  }
}

/**
 * Parent-side stand-in for the content `UrlbarView` a provider engagement hook
 * reaches through `controller.view` on the message path. Each method forwards
 * to the real view as an `InvokeContentAction` message; the content side runs
 * the genuine method (so its normal notifications fire once, no loops).
 */
class ViewProxy {
  /** @type {UrlbarParent} */
  #actor;

  /** @type {number} */
  #instanceId;

  constructor(actor, instanceId) {
    this.#actor = actor;
    this.#instanceId = instanceId;
  }

  #invoke(method, args) {
    sendToChild(this.#actor, "InvokeContentAction", {
      instanceId: this.#instanceId,
      target: "view",
      method,
      args,
    });
  }

  acknowledgeFeedback(result) {
    this.#invoke("acknowledgeFeedback", [result.toWire()]);
  }

  clearL10nCache() {
    this.#invoke("clearL10nCache", []);
  }

  clearTopSitesCache() {
    this.#invoke("clearTopSitesCache", []);
  }

  close(options) {
    this.#invoke("close", options ? [options] : []);
  }

  startTail150() {
    this.#invoke("startTail150", []);
  }

  updateResultMenuCommands(resultId, commands) {
    this.#invoke("updateResultMenuCommands", [resultId, commands]);
  }
}

/**
 * Parent-side stand-in for the content `UrlbarInput` a provider engagement hook
 * reaches through `controller.input` on the message path. See `ViewProxy`; args
 * must be structured-cloneable (search strings, plain option objects).
 */
class InputProxy {
  /** @type {UrlbarParent} */
  #actor;

  /** @type {number} */
  #instanceId;

  constructor(actor, instanceId) {
    this.#actor = actor;
    this.#instanceId = instanceId;
  }

  #invoke(method, args) {
    sendToChild(this.#actor, "InvokeContentAction", {
      instanceId: this.#instanceId,
      target: "input",
      method,
      args,
    });
  }

  search(value, options) {
    this.#invoke("search", [value, options]);
  }

  setValue(value) {
    this.#invoke("setValue", [value]);
  }

  startQuery(options) {
    this.#invoke("startQuery", [options]);
  }

  get view() {
    return new ViewProxy(this.#actor, this.#instanceId);
  }
}
