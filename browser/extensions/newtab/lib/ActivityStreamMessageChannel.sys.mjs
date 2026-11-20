/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutHomeStartupCache: "resource:///modules/AboutHomeStartupCache.sys.mjs",
  AboutNewTabParent: "resource:///actors/AboutNewTabParent.sys.mjs",
  CONTENT_PREF_RULES: "resource://newtab/lib/ActivityStream.sys.mjs",
});

import {
  actionCreators as ac,
  actionTypes as at,
  actionUtils as au,
} from "resource://newtab/common/Actions.mjs";

const ABOUT_NEW_TAB_URL = "about:newtab";

/**
 * Whether content may write `value` to the pref `name`, per that pref's
 * `allowSetFromContent` rule in PREFS_CONFIG. Prefs without a rule are only
 * writable from the parent process.
 *
 * @param {string} name Pref name, relative to the activity-stream branch
 * @param {any} value The value to write, or undefined when clearing the pref
 * @param {boolean} [clearing]
 * @returns {boolean}
 */
function isPrefChangeAllowedFromContent(name, value, clearing = false) {
  const rule = typeof name === "string" && lazy.CONTENT_PREF_RULES?.get(name);
  if (!rule) {
    return false;
  }
  if (clearing) {
    return true;
  }
  if (rule.oneOf) {
    return rule.oneOf.includes(value);
  }
  return typeof value === rule.type;
}

function isPrefActionAllowedFromContent(action) {
  switch (action.type) {
    case at.SET_PREF:
      return isPrefChangeAllowedFromContent(
        action.data?.name,
        action.data?.value
      );
    case at.CLEAR_PREF:
      return isPrefChangeAllowedFromContent(action.data?.name, undefined, true);
    case at.SET_MULTIPLE_PREFS: {
      const values = action.data?.values;
      return (
        !!values &&
        typeof values === "object" &&
        Object.entries(values).every(([name, value]) =>
          isPrefChangeAllowedFromContent(name, value)
        )
      );
    }
    default:
      return true;
  }
}

export const DEFAULT_OPTIONS = {
  dispatch(action) {
    throw new Error(
      `\nMessageChannel: Received action ${action.type}, but no dispatcher was defined.\n`
    );
  },
  pageURL: ABOUT_NEW_TAB_URL,
  outgoingMessageName: "ActivityStream:MainToContent",
  incomingMessageName: "ActivityStream:ContentToMain",
};

export class ActivityStreamMessageChannel {
  /** @type {Function} */
  dispatch;

  /** @type {string} */
  pageURL;

  /** @type {string} */
  outgoingMessageName;

  /** @type {string} */
  incomingMessageName;

  /**
   * ActivityStreamMessageChannel - This module connects a Redux store to the new tab page actor.
   *                  You should use the BroadcastToContent, AlsoToOneContent, and AlsoToMain action creators
   *                  in common/Actions.sys.mjs to help you create actions that will be automatically routed
   *                  to the correct location.
   *
   * @param  {object} [options] Overrides for DEFAULT_OPTIONS
   * @param  {function} [options.dispatch] The dispatch method from a Redux store
   * @param  {string} [options.pageURL] The URL to which the channel is attached, such as about:newtab.
   * @param  {string} [options.outgoingMessageName] The name of the message sent to child processes
   * @param  {string} [options.incomingMessageName] The name of the message received from child processes
   */
  constructor(options = {}) {
    Object.assign(this, DEFAULT_OPTIONS, options);

    this.middleware = this.middleware.bind(this);
    this.onMessage = this.onMessage.bind(this);
    this.onNewTabLoad = this.onNewTabLoad.bind(this);
    this.onNewTabUnload = this.onNewTabUnload.bind(this);
    this.onNewTabInit = this.onNewTabInit.bind(this);

    // Retain per-<browser> listener refs so we can remove them later.
    this._renderLayersListeners = new WeakMap();
  }

  /**
   * Get an iterator over the loaded tab objects.
   */
  get loadedTabs() {
    // In the test, AboutNewTabParent is not defined.
    return lazy.AboutNewTabParent?.loadedTabs || new Map();
  }

  /**
   * middleware - Redux middleware that looks for AlsoToOneContent and BroadcastToContent type
   *              actions, and sends them out.
   *
   * @return {function} Redux middleware
   */
  middleware() {
    return next => action => {
      const skipMain = action.meta && action.meta.skipMain;
      if (au.isSendToOneContent(action)) {
        this.send(action);
      } else if (au.isBroadcastToContent(action)) {
        this.broadcast(action);
      } else if (au.isSendToPreloaded(action)) {
        this.sendToPreloaded(action);
      }

      if (!skipMain) {
        next(action);
      }
    };
  }

  /**
   * onActionFromContent - Handler for actions from a content processes
   *
   * @param  {object} action  A Redux action
   * @param  {string} targetId The portID of the port that sent the message
   */
  onActionFromContent(action, targetId) {
    if (!isPrefActionAllowedFromContent(action)) {
      console.error(
        `Dropped ${action.type} from content for a pref it may not change`,
        action.data
      );
      return;
    }
    this.dispatch(ac.AlsoToMain(action, this.validatePortID(targetId)));
  }

  /**
   * broadcast - Sends an action to all ports
   *
   * @param  {object} action A Redux action
   */
  broadcast(action) {
    // We're trying to update all tabs, so signal the AboutHomeStartupCache
    // that its likely time to refresh the cache.
    lazy.AboutHomeStartupCache.onPreloadedNewTabMessage();

    for (let { actor } of this.loadedTabs.values()) {
      try {
        actor.sendAsyncMessage(this.outgoingMessageName, action);
      } catch (e) {
        // The target page is closed/closing by the user or test, so just ignore.
      }
    }
  }

  /**
   * send - Sends an action to a specific port
   *
   * @param  {object} action A redux action; it should contain a portID in the meta.toTarget property
   */
  send(action) {
    const targetId = action.meta && action.meta.toTarget;
    const target = this.getTargetById(targetId);
    try {
      target.sendAsyncMessage(this.outgoingMessageName, action);
    } catch (e) {
      // The target page is closed/closing by the user or test, so just ignore.
    }
  }

  /**
   * A valid portID is a combination of process id and a port number.
   * It is generated in AboutNewTabChild.sys.mjs.
   */
  validatePortID(id) {
    if (typeof id !== "string" || !id.includes(":")) {
      console.error("Invalid portID");
    }

    return id;
  }

  /**
   * getTargetById - Retrieve the message target by portID, if it exists
   *
   * @param  {string} id A portID
   * @return {object|null} The message target, if it exists.
   */
  getTargetById(id) {
    this.validatePortID(id);

    for (let { portID, actor } of this.loadedTabs.values()) {
      if (portID === id) {
        return actor;
      }
    }
    return null;
  }

  /**
   * sendToPreloaded - Sends an action to each preloaded browser, if any
   *
   * @param  {object} action A redux action
   */
  sendToPreloaded(action) {
    // We're trying to update the preloaded about:newtab, so signal
    // the AboutHomeStartupCache that its likely time to refresh
    // the cache.
    lazy.AboutHomeStartupCache.onPreloadedNewTabMessage();

    const preloadedActors = this.getPreloadedActors();
    if (preloadedActors && action.data) {
      for (let preloadedActor of preloadedActors) {
        try {
          preloadedActor.sendAsyncMessage(this.outgoingMessageName, action);
        } catch (e) {
          // The preloaded page is no longer available, so just ignore.
        }
      }
    }
  }

  /**
   * getPreloadedActors - Retrieve the preloaded actors
   *
   * @return {Array|null} An array of actors belonging to the preloaded browsers, or null
   *                      if there aren't any preloaded browsers
   */
  getPreloadedActors() {
    let preloadedActors = [];
    for (let { actor, browser } of this.loadedTabs.values()) {
      if (this.isPreloadedBrowser(browser)) {
        preloadedActors.push(actor);
      }
    }
    return preloadedActors.length ? preloadedActors : null;
  }

  /**
   * isPreloadedBrowser - Returns true if the passed browser has been preloaded
   *                      for faster rendering of new tabs.
   *
   * @param {object} browser A <browser> element to check.
   * @return {boolean} True if the browser is preloaded.
   *                      if there aren't any preloaded browsers
   */
  isPreloadedBrowser(browser) {
    return browser.getAttribute("preloadedState") === "preloaded";
  }

  simulateMessagesForExistingTabs() {
    // Some pages might have already loaded, so we won't get the usual message
    for (const loadedTab of this.loadedTabs.values()) {
      let simulatedDetails = {
        actor: loadedTab.actor,
        browser: loadedTab.browser,
        browsingContext: loadedTab.browsingContext,
        portID: loadedTab.portID,
        url: loadedTab.url,
        simulated: true,
      };

      this.onActionFromContent(
        {
          type: at.NEW_TAB_INIT,
          data: simulatedDetails,
        },
        loadedTab.portID
      );

      if (loadedTab.loaded) {
        this.tabLoaded(simulatedDetails);
      }
    }

    // It's possible that those existing tabs had sent some messages up
    // to us before the feeds / ActivityStreamMessageChannel was ready.
    //
    // AboutNewTabParent takes care of queueing those for us, so
    // now that we're ready, we can flush these queued messages.
    lazy.AboutNewTabParent.flushQueuedMessagesFromContent();
  }

  /**
   * onNewTabInit - Handler for special RemotePage:Init message fired
   * on initialization.
   *
   * @param  {object} msg The messsage from a page that was just initialized
   * @param  {object} tabDetails details about a loaded tab
   *
   * tabDetails contains:
   *   actor, browser, browsingContext, portID, url
   */
  onNewTabInit(msg, tabDetails) {
    this.onActionFromContent(
      {
        type: at.NEW_TAB_INIT,
        data: tabDetails,
      },
      msg.data.portID
    );
  }

  /**
   * onNewTabLoad - Handler for special RemotePage:Load message fired on page load.
   *
   * @param  {object} msg The messsage from a page that was just loaded
   * @param  {object} tabDetails details about a loaded tab, similar to onNewTabInit
   */
  onNewTabLoad(msg, tabDetails) {
    this.tabLoaded(tabDetails);
  }

  setRenderLayers(browser) {
    if (this.isPreloadedBrowser(browser)) {
      const win = browser.documentGlobal;
      if (!win || win.closed) {
        return;
      }
      // As a perceived performance optimization, if this loaded Activity Stream
      // happens to be a preloaded browser in a window that is not minimized or
      // occluded, have it render its layers to the compositor now to increase
      // the odds that by the time we switch to the tab, the layers are already
      // ready to present to the user.
      const shouldRender =
        win.windowState !== win.STATE_MINIMIZED && !win.isFullyOccluded;
      if (browser.renderLayers !== shouldRender) {
        browser.renderLayers = shouldRender;
      }
    }
  }

  tabLoaded(tabDetails) {
    tabDetails.loaded = true;
    const { browser } = tabDetails;

    this.setRenderLayers(browser);
    // Avoid double-registering if we ever re-enter tabLoaded for the same browser.
    if (
      this.isPreloadedBrowser(browser) &&
      !this._renderLayersListeners.has(browser)
    ) {
      const browserRef = new WeakRef(browser);
      const onSizeModeChange = () => {
        const b = browserRef.deref();
        if (b) {
          this.setRenderLayers(b);
        }
      };
      const onOcclusionStateChange = () => {
        const b = browserRef.deref();
        if (b) {
          this.setRenderLayers(b);
        }
      };

      const cleanup = () => {
        const b = browserRef.deref();
        const w = b?.documentGlobal;
        w?.removeEventListener("sizemodechange", onSizeModeChange);
        w?.removeEventListener("occlusionstatechange", onOcclusionStateChange);
        if (b) {
          this._renderLayersListeners.delete(b);
        }
      };

      const win = browser.documentGlobal;
      win.addEventListener("sizemodechange", onSizeModeChange);
      win.addEventListener("occlusionstatechange", onOcclusionStateChange);
      win.addEventListener("unload", cleanup, { once: true });

      this._renderLayersListeners.set(browser, {
        onSizeModeChange,
        onOcclusionStateChange,
        cleanup,
      });
    }

    this.onActionFromContent({ type: at.NEW_TAB_LOAD }, tabDetails.portID);
  }

  /**
   * onNewTabUnloadLoad - Handler for special RemotePage:Unload message fired
   * on page unload.
   *
   * @param  {object} msg The messsage from a page that was just unloaded
   * @param  {object} tabDetails details about a loaded tab, similar to onNewTabInit
   */
  onNewTabUnload(msg, tabDetails) {
    const { browser } = tabDetails;

    const listeners = this._renderLayersListeners.get(browser);
    if (listeners) {
      const { onSizeModeChange, onOcclusionStateChange, cleanup } = listeners;
      const win = browser.documentGlobal;

      if (win && !win.closed) {
        win.removeEventListener("sizemodechange", onSizeModeChange);
        win.removeEventListener("occlusionstatechange", onOcclusionStateChange);
        win.removeEventListener("unload", cleanup);
      }

      this._renderLayersListeners.delete(browser);
    }

    this.onActionFromContent(
      {
        type: at.NEW_TAB_UNLOAD,
        _target: { browser, window: browser.documentGlobal },
      },
      tabDetails.portID
    );
  }

  /**
   * onMessage - Handles custom messages from content. It expects all messages to
   *             be formatted as Redux actions, and dispatches them to this.store
   *
   * @param  {object} msg A custom message from content
   * @param  {object} msg.data A Redux action (e.g. {type: "HELLO_WORLD"})
   * @param  {object} msg.target A message target
   * @param  {object} tabDetails details about a loaded tab, similar to onNewTabInit
   */
  onMessage(msg, tabDetails) {
    if (!msg.data || !msg.data.type) {
      console.error(
        new Error(
          `Received an improperly formatted message from ${tabDetails.portID}`
        )
      );
      return;
    }
    let action = {};
    Object.assign(action, msg.data);
    // target is used to access a browser reference that came from the content
    // and should only be used in feeds (not reducers)
    action._target = {
      browser: tabDetails.browser,
      window: tabDetails.browser.documentGlobal,
    };

    this.onActionFromContent(action, tabDetails.portID);
  }
}
