/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  actions: "chrome://remote/content/shared/webdriver/Actions.sys.mjs",
  Addon: "chrome://remote/content/shared/Addon.sys.mjs",
  AnimationFramePromise: "chrome://remote/content/shared/Sync.sys.mjs",
  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  browser: "chrome://remote/content/marionette/browser.sys.mjs",
  capture: "chrome://remote/content/shared/Capture.sys.mjs",
  Context: "chrome://remote/content/marionette/browser.sys.mjs",
  cookie: "chrome://remote/content/marionette/cookie.sys.mjs",
  disableEventsActor:
    "chrome://remote/content/marionette/actors/MarionetteEventsParent.sys.mjs",
  dom: "chrome://remote/content/shared/DOM.sys.mjs",
  enableEventsActor:
    "chrome://remote/content/marionette/actors/MarionetteEventsParent.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  getMarionetteCommandsActorProxy:
    "chrome://remote/content/marionette/actors/MarionetteCommandsParent.sys.mjs",
  isPrivilegedContext:
    "chrome://remote/content/shared/BrowsingContextUtils.sys.mjs",
  isWebdriverSafeNavigationURL:
    "chrome://remote/content/shared/BrowsingContextUtils.sys.mjs",
  l10n: "chrome://remote/content/marionette/l10n.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  Marionette: "chrome://remote/content/components/Marionette.sys.mjs",
  MarionettePrefs: "chrome://remote/content/marionette/prefs.sys.mjs",
  modal: "chrome://remote/content/shared/Prompt.sys.mjs",
  NavigableManager: "chrome://remote/content/shared/NavigableManager.sys.mjs",
  navigate: "chrome://remote/content/marionette/navigate.sys.mjs",
  permissions: "chrome://remote/content/shared/Permissions.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  print: "chrome://remote/content/shared/PDF.sys.mjs",
  PollPromise: "chrome://remote/content/shared/Sync.sys.mjs",
  PromptHandlers:
    "chrome://remote/content/shared/webdriver/UserPromptHandler.sys.mjs",
  PromptListener:
    "chrome://remote/content/shared/listeners/PromptListener.sys.mjs",
  PromptTypes:
    "chrome://remote/content/shared/webdriver/UserPromptHandler.sys.mjs",
  quit: "chrome://remote/content/shared/Browser.sys.mjs",
  reftest: "chrome://remote/content/marionette/reftest.sys.mjs",
  registerCommandsActor:
    "chrome://remote/content/marionette/actors/MarionetteCommandsParent.sys.mjs",
  RemoteAgent: "chrome://remote/content/components/RemoteAgent.sys.mjs",
  ShadowRoot: "chrome://remote/content/marionette/web-reference.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
  Timeouts: "chrome://remote/content/shared/webdriver/Capabilities.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
  unregisterCommandsActor:
    "chrome://remote/content/marionette/actors/MarionetteCommandsParent.sys.mjs",
  waitForInitialNavigationCompleted:
    "chrome://remote/content/shared/Navigate.sys.mjs",
  webauthn: "chrome://remote/content/marionette/webauthn.sys.mjs",
  WebDriverSession: "chrome://remote/content/shared/webdriver/Session.sys.mjs",
  WebElement: "chrome://remote/content/marionette/web-reference.sys.mjs",
  windowManager: "chrome://remote/content/shared/WindowManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

/**
 * @typedef {import("chrome://remote/content/shared/WindowManager.sys.mjs").WindowRect} WindowRect
 */

const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

ChromeUtils.defineLazyGetter(
  lazy,
  "supportedStrategies",
  () =>
    new Set([
      lazy.dom.Strategy.ClassName,
      lazy.dom.Strategy.Selector,
      lazy.dom.Strategy.ID,
      lazy.dom.Strategy.Name,
      lazy.dom.Strategy.LinkText,
      lazy.dom.Strategy.PartialLinkText,
      lazy.dom.Strategy.TagName,
      lazy.dom.Strategy.XPath,
    ])
);

// Observer topic to wait for until the browser window is ready.
const TOPIC_BROWSER_READY = "browser-delayed-startup-finished";
// Observer topic to perform clean up when application quit is requested.
const TOPIC_QUIT_APPLICATION_REQUESTED = "quit-application-requested";

/**
 * The Marionette WebDriver services provides a standard conforming
 * implementation of the W3C WebDriver specification.
 *
 * @namespace driver
 * @see https://w3c.github.io/webdriver/webdriver-spec.html
 */

class ActionsHelper {
  #actionsOptions;
  #driver;

  constructor(driver) {
    this.#driver = driver;

    // Options for actions to pass through performActions and releaseActions.
    this.#actionsOptions = {
      // Callbacks as defined in the WebDriver specification.
      getElementOrigin: this.getElementOrigin.bind(this),
      isElementOrigin: this.isElementOrigin.bind(this),

      // Custom callbacks.
      assertInViewPort: this.assertInViewPort.bind(this),
      dispatchEvent: this.dispatchEvent.bind(this),
      getClientRects: this.getClientRects.bind(this),
      getInViewCentrePoint: this.getInViewCentrePoint.bind(this),
      toBrowserWindowCoordinates: this.toBrowserWindowCoordinates.bind(this),
    };
  }

  get actionsOptions() {
    return this.#actionsOptions;
  }

  /**
   * Assert that the target coordinates are within the visible viewport.
   *
   * @param {Array.<number>} target
   *     Coordinates [x, y] of the target relative to the viewport.
   * @param {BrowsingContext} browsingContext
   *     The browsing context to dispatch the event to.
   *
   * @returns {Promise<undefined>}
   *     Promise that rejects, if the coordinates are not within
   *     the visible viewport.
   *
   * @throws {MoveTargetOutOfBoundsError}
   *     If target is outside the viewport.
   */
  assertInViewPort(target, browsingContext) {
    return this.#getActor(browsingContext).assertInViewPort(target);
  }

  /**
   * Dispatch an event.
   *
   * @param {string} eventName
   *     Name of the event to be dispatched.
   * @param {BrowsingContext} browsingContext
   *     The browsing context to dispatch the event to.
   * @param {object} details
   *     Details of the event to be dispatched.
   *
   * @returns {Promise}
   *     Promise that resolves once the event is dispatched.
   */
  dispatchEvent(eventName, browsingContext, details) {
    if (
      (eventName === "synthesizeWheelAtPoint" &&
        lazy.actions.useAsyncWheelEvents) ||
      (eventName == "synthesizeMouseAtPoint" &&
        lazy.actions.useAsyncMouseEvents) ||
      (eventName == "synthesizeTouchAtPoint" &&
        lazy.actions.useAsyncTouchEvents)
    ) {
      browsingContext = browsingContext.topChromeWindow?.browsingContext;
      details.eventData.asyncEnabled = true;
    }

    return this.#getActor(browsingContext).dispatchEvent(eventName, details);
  }

  /**
   * Finalize an action command.
   *
   * @param {BrowsingContext} browsingContext
   *     The browsing context to dispatch the event to.
   */
  async finalizeAction(browsingContext) {
    try {
      await this.#getActor(browsingContext).finalizeAction();
    } catch (e) {
      // Ignore the error if the underlying browsing context is already gone.
      if (e.name !== lazy.error.NoSuchWindowError.name) {
        throw e;
      }
    }
  }

  /**
   * Retrieves the WebElement reference of the origin.
   *
   * @param {ElementOrigin} origin
   *     Reference to the element origin of the action.
   * @param {BrowsingContext} _browsingContext
   *     Not used by Marionette.
   *
   * @returns {WebElement}
   *     The WebElement reference.
   */
  getElementOrigin(origin, _browsingContext) {
    return origin;
  }

  /**
   * Retrieve the list of client rects for the element.
   *
   * @param {WebElement} element
   *     The web element reference to retrieve the rects from.
   * @param {BrowsingContext} browsingContext
   *     The browsing context to dispatch the event to.
   *
   * @returns {Promise<Array<Map.<string, number>>>}
   *     Promise that resolves to a list of DOMRect-like objects.
   */
  getClientRects(element, browsingContext) {
    return this.#getActor(browsingContext).getClientRects(element);
  }

  /**
   * Retrieve the in-view center point for the rect and visible viewport.
   *
   * @param {DOMRect} rect
   *     Size and position of the rectangle to check.
   * @param {BrowsingContext} browsingContext
   *     The browsing context to dispatch the event to.
   *
   * @returns {Promise<Map.<string, number>>}
   *     X and Y coordinates that denotes the in-view centre point of
   *     `rect`.
   */
  getInViewCentrePoint(rect, browsingContext) {
    return this.#getActor(browsingContext).getInViewCentrePoint(rect);
  }

  /**
   * Retrieves the action's input state.
   *
   * @param {BrowsingContext} browsingContext
   *     The Browsing Context to retrieve the input state for.
   *
   * @returns {Actions.InputState}
   *     The action's input state.
   */
  getInputState(browsingContext) {
    // Bug 1821460: Fetch top-level browsing context.
    let inputState = this.#driver.inputStates.get(browsingContext);

    if (inputState === undefined) {
      inputState = new lazy.actions.State();
      this.#driver.inputStates.set(browsingContext, inputState);
    }

    return inputState;
  }

  /**
   * Checks if the given object is a valid element origin.
   *
   * @param {object} origin
   *     The object to check.
   *
   * @returns {boolean}
   *     True, if it's a WebElement.
   */
  isElementOrigin(origin) {
    return lazy.WebElement.Identifier in origin;
  }

  /**
   * Resets the action's input state.
   *
   * @param {BrowsingContext} browsingContext
   *     The Browsing Context to reset the input state for.
   */
  resetInputState(browsingContext) {
    // Bug 1821460: Fetch top-level browsing context.
    if (this.#driver.inputStates.has(browsingContext)) {
      this.#driver.inputStates.delete(browsingContext);
    }
  }

  /**
   * Convert a position or rect in browser coordinates of CSS units.
   *
   * @param {object} position - Object with the coordinates to convert.
   * @param {number} position.x - X coordinate.
   * @param {number} position.y - Y coordinate.
   * @param {BrowsingContext} browsingContext - The Browsing Context to convert the
   *     coordinates for.
   */
  toBrowserWindowCoordinates(position, browsingContext) {
    return this.#getActor(browsingContext).toBrowserWindowCoordinates(position);
  }

  #getActor(browsingContext) {
    return lazy.getMarionetteCommandsActorProxy(() => browsingContext);
  }
}

/**
 * Implements (parts of) the W3C WebDriver protocol.  GeckoDriver lives
 * in chrome space and mediates calls to the current browsing context's actor.
 *
 * Throughout this class, methods with the argument <var>cmd</var>'s
 * documentation refers to the contents of the <code>cmd.parameter</code>
 * object.
 *
 * @class GeckoDriver
 *
 * @param {MarionetteServer} server
 *     The instance of Marionette server.
 */
export class GeckoDriver {
  #actionsHelper;
  #browsers = {};
  #context;
  #curBrowser;
  #currentSession;
  #dialog;
  #inputStates;
  #isShuttingDown;
  #mainFrame;
  #observer;
  #promptListener;
  #reftest;
  #server;
  #sessionConfigFlags;

  constructor(server) {
    this.#server = server;

    this.#browsers = {};

    // The current context, use content by default
    this.#context = lazy.Context.Content;

    this.#curBrowser = null;

    // Current WebDriver session
    this.#currentSession = null;
    this.#dialog = null;

    // Browsing context => input state.
    // Bug 1821460: Move to WebDriver Session and share with Remote Agent.
    this.#inputStates = new WeakMap();

    this.#isShuttingDown = false;

    // Top-most chrome window
    this.#mainFrame = null;

    this.#observer = {
      observe: async (subject, topic) => {
        switch (topic) {
          case TOPIC_BROWSER_READY:
            this.#registerWindow(subject);
            break;

          case TOPIC_QUIT_APPLICATION_REQUESTED:
            // Run Marionette specific cleanup steps before allowing
            // the application to shutdown
            await this.#server.setAcceptConnections(false);
            this.deleteSession();
            break;
        }
      },

      QueryInterface: ChromeUtils.generateQI([
        "nsIObserver",
        "nsISupportsWeakReference",
      ]),
    };

    this.#promptListener = null;
    this.#reftest = null;

    this.#actionsHelper = new ActionsHelper(this);

    // Flag to indicate a WebDriver HTTP session
    this.#sessionConfigFlags = new Set([
      lazy.WebDriverSession.SESSION_FLAG_HTTP,
    ]);
  }

  /**
   * Get the current URL.
   *
   * @param {object} options
   * @param {boolean=} options.top
   *     If set to true return the window's top-level URL,
   *     otherwise the one from the currently selected frame. Defaults to true.
   *
   * @returns {string} The current URL.
   */
  _getCurrentURL(options = {}) {
    if (options.top === undefined) {
      options.top = true;
    }
    const browsingContext = this.getBrowsingContext(options);

    return new URL(browsingContext.currentURI.spec);
  }

  /**
   * The current context decides if commands are executed in chrome- or
   * content space.
   */
  get context() {
    return this.#context;
  }

  set context(context) {
    if (context === lazy.Context.Chrome) {
      lazy.assert.hasSystemAccess();
    }

    this.#context = lazy.Context.fromString(context);
  }

  /**
   * The current WebDriver Session.
   */
  get currentSession() {
    if (lazy.RemoteAgent.webDriverBiDi) {
      return lazy.RemoteAgent.webDriverBiDi.session;
    }

    return this.#currentSession;
  }

  get curBrowser() {
    return this.#curBrowser;
  }

  get inputStates() {
    return this.#inputStates;
  }

  get promptListener() {
    return this.#promptListener;
  }

  /**
   * Returns the title of the ChromeWindow or content browser,
   * depending on context.
   *
   * @returns {string}
   *     Read-only property containing the title of the loaded URL.
   */
  get title() {
    const browsingContext = this.getBrowsingContext({ top: true });
    return browsingContext.currentWindowGlobal.documentTitle;
  }

  get windowType() {
    return this.#curBrowser.window.document.documentElement.getAttribute(
      "windowtype"
    );
  }

  /**
   * Enables or disables accepting new socket connections.
   *
   * By calling this method with `false` the server will not accept any
   * further connections, but existing connections will not be forcible
   * closed. Use `true` to re-enable accepting connections.
   *
   * Please note that when closing the connection via the client you can
   * end-up in a non-recoverable state if it hasn't been enabled before.
   *
   * This method is used for custom in application shutdowns via
   * marionette.quit() or marionette.restart(), like File -> Quit.
   *
   * @param {object} cmd
   * @param {boolean} cmd.parameters.value
   *     True if the server should accept new socket connections.
   */
  async acceptConnections(cmd) {
    lazy.assert.boolean(
      cmd.parameters.value,
      lazy.pprint`Expected "value" to be a boolean, got ${cmd.parameters.value}`
    );
    await this.#server.setAcceptConnections(cmd.parameters.value);
  }

  /**
   * Accepts a currently displayed dialog modal, or returns no such alert if
   * no modal is displayed.
   *
   * @see https://w3c.github.io/webdriver/#accept-alert
   *
   * @throws {NoSuchAlertError}
   *     If there is no current user prompt.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async acceptAlert() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    this.#checkIfAlertIsPresent();

    const dialogClosed = this.#promptListener.dialogClosed();
    this.#dialog.accept();
    await dialogClosed;

    const win = this.getCurrentWindow();
    await new lazy.AnimationFramePromise(win);
  }

  /**
   * Add a single cookie to the cookie store associated with the active
   * document's address.
   *
   * @see https://w3c.github.io/webdriver/#add-cookie
   *
   * @param {object} cmd
   * @param {Map.<string, (string|number|boolean)>} cmd.parameters.cookie
   *     Cookie object.
   *
   * @throws {InvalidCookieDomainError}
   *     If <var>cookie</var> is for a different domain than the active
   *     document's host.
   * @throws {NoSuchWindowError}
   *     Bbrowsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async addCookie(cmd) {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let { protocol, hostname } = this._getCurrentURL({ top: false });

    const networkSchemes = ["http:", "https:"];
    if (!networkSchemes.includes(protocol)) {
      throw new lazy.error.InvalidCookieDomainError(
        "Document is cookie-averse"
      );
    }

    let newCookie = lazy.cookie.fromJSON(cmd.parameters.cookie);

    lazy.cookie.add(newCookie, { restrictToHost: hostname, protocol });
  }

  /**
   * Clear the text of an element.
   *
   * @see https://w3c.github.io/webdriver/#element-clear
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be cleared.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async clearElement(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    await this.#getActor().clearElement(webEl);
  }

  /**
   * Send click event to element.
   *
   * @see https://w3c.github.io/webdriver/#element-click
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be clicked.
   *
   * @throws {InvalidArgumentError}
   *     If element <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async clickElement(cmd) {
    const browsingContext = lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    const actor = this.#getActor();

    const loadEventExpected = lazy.navigate.isLoadEventExpected(
      this._getCurrentURL(),
      {
        browsingContext,
        target: await actor.getElementAttribute(webEl, "target"),
      }
    );

    await lazy.navigate.waitForNavigationCompleted(
      this,
      () => actor.clickElement(webEl, this.currentSession.capabilities),
      {
        loadEventExpected,
        // The click might trigger a navigation, so don't count on it.
        requireBeforeUnload: false,
      }
    );
  }

  /**
   * Close the currently selected tab/window.
   *
   * @see https://w3c.github.io/webdriver/#close-window
   *
   * With multiple open tabs present the currently selected tab will
   * be closed.  Otherwise the window itself will be closed. If it is the
   * last window currently open, the window will not be closed to prevent
   * a shutdown of the application. Instead the returned list of window
   * handles is empty.
   *
   * @returns {Array.<string>}
   *     Unique window handles of remaining windows.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async close() {
    lazy.assert.open(
      this.getBrowsingContext({ context: lazy.Context.Content, top: true })
    );
    await this.#handleUserPrompts();

    // If there is only one window left, do not close unless windowless mode is
    // enabled. Instead return a faked empty array of window handles.
    // This will instruct geckodriver to terminate the application.
    if (
      lazy.TabManager.getTabCount() === 1 &&
      !this.currentSession.capabilities.get("moz:windowless")
    ) {
      return [];
    }

    await this.#curBrowser.closeTab();
    this.currentSession.contentBrowsingContext = null;

    return lazy.TabManager.getBrowsers({ unloaded: true }).map(browser =>
      lazy.NavigableManager.getIdForBrowser(browser)
    );
  }

  /**
   * Close the currently selected chrome window.
   *
   * If it is the last window currently open, the chrome window will not be
   * closed to prevent a shutdown of the application. Instead the returned
   * list of chrome window handles is empty.
   *
   * @returns {Array.<string>}
   *     Unique chrome window handles of remaining chrome windows.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async closeChromeWindow() {
    lazy.assert.desktop();
    lazy.assert.open(
      this.getBrowsingContext({ context: lazy.Context.Chrome, top: true })
    );

    let nwins = 0;

    // eslint-disable-next-line
    for (let _ of lazy.windowManager.windows) {
      nwins++;
    }

    // If there is only one window left, do not close unless windowless mode is
    // enabled. Instead return a faked empty array of window handles.
    // This will instruct geckodriver to terminate the application.
    if (nwins == 1 && !this.currentSession.capabilities.get("moz:windowless")) {
      return [];
    }

    await this.#curBrowser.closeWindow();
    this.currentSession.chromeBrowsingContext = null;
    this.currentSession.contentBrowsingContext = null;

    return lazy.windowManager.windows.map(window =>
      lazy.NavigableManager.getIdForBrowsingContext(window.browsingContext)
    );
  }

  /**
   * Delete all cookies that are visible to a document.
   *
   * @see https://w3c.github.io/webdriver/#delete-all-cookies
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async deleteAllCookies() {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let { hostname, pathname } = this._getCurrentURL({ top: false });
    for (let toDelete of lazy.cookie.iter(
      hostname,
      this.getBrowsingContext(),
      pathname
    )) {
      lazy.cookie.remove(toDelete);
    }
  }

  /**
   * Delete a cookie by name.
   *
   * @see https://w3c.github.io/webdriver/#delete-cookie
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async deleteCookie(cmd) {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let { hostname, pathname } = this._getCurrentURL({ top: false });
    let name = lazy.assert.string(
      cmd.parameters.name,
      lazy.pprint`Expected "name" to be a string, got ${cmd.parameters.name}`
    );
    for (let c of lazy.cookie.iter(
      hostname,
      this.getBrowsingContext(),
      pathname
    )) {
      if (c.name === name) {
        lazy.cookie.remove(c);
      }
    }
  }

  /**
   * Delete Marionette session.
   *
   * @see https://w3c.github.io/webdriver/#delete-session
   */
  deleteSession() {
    if (!this.currentSession) {
      return;
    }

    for (let win of lazy.windowManager.windows) {
      this.#stopObservingWindow(win);
    }

    // reset to the top-most frame
    this.#mainFrame = null;

    if (!this.#isShuttingDown && this.#promptListener) {
      // Do not stop the prompt listener when quitting the browser to
      // allow us to also accept beforeunload prompts during shutdown.
      this.#promptListener.stopListening();
      this.#promptListener = null;
    }

    lazy.Addon.cleanupTemporaryAddonFiles();

    try {
      Services.obs.removeObserver(this.#observer, TOPIC_BROWSER_READY);
    } catch (e) {
      lazy.logger.debug(`Failed to remove observer "${TOPIC_BROWSER_READY}"`);
    }

    // Always unregister actors after all other observers
    // and listeners have been removed.
    lazy.unregisterCommandsActor();
    // MarionetteEvents actors are only disabled to avoid IPC errors if there are
    // in flight events being forwarded from the content process to the parent
    // process.
    lazy.disableEventsActor();

    if (lazy.RemoteAgent.webDriverBiDi) {
      lazy.RemoteAgent.webDriverBiDi.deleteSession();
    } else {
      this.currentSession.destroy();
      this.#currentSession = null;
    }
  }

  /**
   * Dismisses a currently displayed modal dialogs, or returns no such alert if
   * no modal is displayed.
   *
   * @see https://w3c.github.io/webdriver/#dismiss-alert
   *
   * @throws {NoSuchAlertError}
   *     If there is no current user prompt.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async dismissAlert() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    this.#checkIfAlertIsPresent();

    const dialogClosed = this.#promptListener.dialogClosed();
    this.#dialog.dismiss();
    await dialogClosed;

    const win = this.getCurrentWindow();
    await new lazy.AnimationFramePromise(win);
  }

  /**
   * Executes a JavaScript function in the context of the current browsing
   * context, if in content space, or in chrome space otherwise, and returns
   * the object passed to the callback.
   *
   * The callback is always the last argument to the <var>arguments</var>
   * list passed to the function scope of the script.  It can be retrieved
   * as such:
   *
   * <pre><code>
   *     let callback = arguments[arguments.length - 1];
   *     callback("foo");
   *     // "foo" is returned
   * </code></pre>
   *
   * It is important to note that if the <var>sandboxName</var> parameter
   * is left undefined, the script will be evaluated in a mutable sandbox,
   * causing any change it makes on the global state of the document to have
   * lasting side-effects.
   *
   * @see https://w3c.github.io/webdriver/#execute-async-script
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.script
   *     Script to evaluate as a function body.
   * @param {Array.<(string|boolean|number|object|WebReference)>} cmd.parameters.args
   *     Arguments exposed to the script in <code>arguments</code>.
   *     The array items must be serialisable to the WebDriver protocol.
   * @param {string=} cmd.parameters.sandbox
   *     Name of the sandbox to evaluate the script in.  The sandbox is
   *     cached for later reuse on the same Window object if
   *     <var>newSandbox</var> is false.  If the parameter is undefined,
   *     the script is evaluated in a mutable sandbox.  If the parameter
   *     is "system", it will be evaluated in a sandbox with elevated system
   *     privileges, equivalent to chrome space.
   * @param {boolean=} cmd.parameters.newSandbox
   *     Forces the script to be evaluated in a fresh sandbox.  Note that if
   *     it is undefined, the script will normally be evaluated in a fresh
   *     sandbox.
   * @param {string=} cmd.parameters.filename
   *     Filename of the client's program where this script is evaluated.
   * @param {number=} cmd.parameters.line
   *     Line in the client's program where this script is evaluated.
   *
   * @returns {(string|boolean|number|object|WebReference)}
   *     Return value from the script, or null which signifies either the
   *     JavaScript notion of null or undefined.
   *
   * @throws {JavaScriptError}
   *     If an Error was thrown whilst evaluating the script.
   * @throws {NoSuchElementError}
   *     If an element that was passed as part of <var>args</var> is unknown.
   * @throws {NoSuchFrameError}
   *     Child browsing context has been discarded.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {ScriptTimeoutError}
   *     If the script was interrupted due to reaching the session's
   *     script timeout.
   * @throws {StaleElementReferenceError}
   *     If an element that was passed as part of <var>args</var> or that is
   *     returned as result has gone stale.
   */
  executeAsyncScript(cmd) {
    let { script, args } = cmd.parameters;
    let opts = {
      script: cmd.parameters.script,
      args: cmd.parameters.args,
      sandboxName: cmd.parameters.sandbox,
      newSandbox: cmd.parameters.newSandbox,
      file: cmd.parameters.filename,
      line: cmd.parameters.line,
      async: true,
    };

    return this.#execute(script, args, opts);
  }

  /**
   * Executes a JavaScript function in the context of the current browsing
   * context, if in content space, or in chrome space otherwise, and returns
   * the return value of the function.
   *
   * It is important to note that if the <var>sandboxName</var> parameter
   * is left undefined, the script will be evaluated in a mutable sandbox,
   * causing any change it makes on the global state of the document to have
   * lasting side-effects.
   *
   * @see https://w3c.github.io/webdriver/#execute-script
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.script
   *     Script to evaluate as a function body.
   * @param {Array.<(string|boolean|number|object|WebReference)>} cmd.parameters.args
   *     Arguments exposed to the script in <code>arguments</code>.
   *     The array items must be serialisable to the WebDriver protocol.
   * @param {string=} cmd.parameters.sandbox
   *     Name of the sandbox to evaluate the script in.  The sandbox is
   *     cached for later reuse on the same Window object if
   *     <var>newSandbox</var> is false.  If the parameter is undefined,
   *     the script is evaluated in a mutable sandbox.  If the parameter
   *     is "system", it will be evaluated in a sandbox with elevated system
   *     privileges, equivalent to chrome space.
   * @param {boolean=} cmd.parameters.newSandbox
   *     Forces the script to be evaluated in a fresh sandbox.  Note that if
   *     it is undefined, the script will normally be evaluated in a fresh
   *     sandbox.
   * @param {string=} cmd.parameters.filename
   *     Filename of the client's program where this script is evaluated.
   * @param {number=} cmd.parameters.line
   *     Line in the client's program where this script is evaluated.
   *
   * @returns {(string|boolean|number|object|WebReference)}
   *     Return value from the script, or null which signifies either the
   *     JavaScript notion of null or undefined.
   *
   * @throws {JavaScriptError}
   *     If an {@link Error} was thrown whilst evaluating the script.
   * @throws {NoSuchElementError}
   *     If an element that was passed as part of <var>args</var> is unknown.
   * @throws {NoSuchFrameError}
   *     Child browsing context has been discarded.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {ScriptTimeoutError}
   *     If the script was interrupted due to reaching the session's
   *     script timeout.
   * @throws {StaleElementReferenceError}
   *     If an element that was passed as part of <var>args</var> or that is
   *     returned as result has gone stale.
   */
  executeScript(cmd) {
    let { script, args } = cmd.parameters;
    let opts = {
      script: cmd.parameters.script,
      args: cmd.parameters.args,
      sandboxName: cmd.parameters.sandbox,
      newSandbox: cmd.parameters.newSandbox,
      file: cmd.parameters.filename,
      line: cmd.parameters.line,
    };

    return this.#execute(script, args, opts);
  }

  /**
   * Find an element using the indicated search strategy.
   *
   * @see https://w3c.github.io/webdriver/#find-element
   *
   * @param {object} cmd
   * @param {string=} cmd.parameters.element
   *     Web element reference ID to the element that will be used as start node.
   * @param {string} cmd.parameters.using
   *     Indicates which search method to use.
   * @param {string} cmd.parameters.value
   *     Value the client is looking for.
   *
   * @returns {WebElement}
   *     Return the found element.
   *
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>element</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>element</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async findElement(cmd) {
    const { element: el, using, value } = cmd.parameters;

    if (!lazy.supportedStrategies.has(using)) {
      throw new lazy.error.InvalidSelectorError(
        `Strategy not supported: ${using}`
      );
    }

    lazy.assert.defined(value);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let startNode;
    if (typeof el != "undefined") {
      startNode = lazy.WebElement.fromUUID(el).toJSON();
    }

    let opts = {
      startNode,
      timeout: this.currentSession.timeouts.implicit,
      all: false,
    };

    return this.#getActor().findElement(using, value, opts);
  }

  /**
   * Find an element within shadow root using the indicated search strategy.
   *
   * @see https://w3c.github.io/webdriver/#find-element-from-shadow-root
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.shadowRoot
   *     Shadow root reference ID.
   * @param {string} cmd.parameters.using
   *     Indicates which search method to use.
   * @param {string} cmd.parameters.value
   *     Value the client is looking for.
   *
   * @returns {WebElement}
   *     Return the found element.
   *
   * @throws {DetachedShadowRootError}
   *     If shadow root represented by reference <var>id</var> is
   *     no longer attached to the DOM.
   * @throws {NoSuchElementError}
   *     If the element which is looked for with <var>value</var> was
   *     not found.
   * @throws {NoSuchShadowRoot}
   *     If shadow root represented by reference <var>shadowRoot</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async findElementFromShadowRoot(cmd) {
    const { shadowRoot, using, value } = cmd.parameters;

    if (!lazy.supportedStrategies.has(using)) {
      throw new lazy.error.InvalidSelectorError(
        `Strategy not supported: ${using}`
      );
    }

    lazy.assert.defined(value);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const opts = {
      all: false,
      startNode: lazy.ShadowRoot.fromUUID(shadowRoot).toJSON(),
      timeout: this.currentSession.timeouts.implicit,
    };

    return this.#getActor().findElement(using, value, opts);
  }

  /**
   * Find elements using the indicated search strategy.
   *
   * @see https://w3c.github.io/webdriver/#find-elements
   *
   * @param {object} cmd
   * @param {string=} cmd.parameters.element
   *     Web element reference ID to the element that will be used as start node.
   * @param {string} cmd.parameters.using
   *     Indicates which search method to use.
   * @param {string} cmd.parameters.value
   *     Value the client is looking for.
   *
   * @returns {Array<WebElement>}
   *     Return the array of found elements.
   *
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>element</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>element</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async findElements(cmd) {
    const { element: el, using, value } = cmd.parameters;

    if (!lazy.supportedStrategies.has(using)) {
      throw new lazy.error.InvalidSelectorError(
        `Strategy not supported: ${using}`
      );
    }

    lazy.assert.defined(value);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let startNode;
    if (typeof el != "undefined") {
      startNode = lazy.WebElement.fromUUID(el).toJSON();
    }

    let opts = {
      startNode,
      timeout: this.currentSession.timeouts.implicit,
      all: true,
    };

    return this.#getActor().findElements(using, value, opts);
  }

  /**
   * Find elements within shadow root using the indicated search strategy.
   *
   * @see https://w3c.github.io/webdriver/#find-elements-from-shadow-root
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.shadowRoot
   *     Shadow root reference ID.
   * @param {string} cmd.parameters.using
   *     Indicates which search method to use.
   * @param {string} cmd.parameters.value
   *     Value the client is looking for.
   *
   * @returns {Array<WebElement>}
   *     Return the array of found elements.
   *
   * @throws {DetachedShadowRootError}
   *     If shadow root represented by reference <var>id</var> is
   *     no longer attached to the DOM.
   * @throws {NoSuchShadowRoot}
   *     If shadow root represented by reference <var>shadowRoot</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async findElementsFromShadowRoot(cmd) {
    const { shadowRoot, using, value } = cmd.parameters;

    if (!lazy.supportedStrategies.has(using)) {
      throw new lazy.error.InvalidSelectorError(
        `Strategy not supported: ${using}`
      );
    }

    lazy.assert.defined(value);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const opts = {
      all: true,
      startNode: lazy.ShadowRoot.fromUUID(shadowRoot).toJSON(),
      timeout: this.currentSession.timeouts.implicit,
    };

    return this.#getActor().findElements(using, value, opts);
  }

  /**
   * Sets the window to full screen as if the user had done "View > Enter Full Screen".
   *
   * @see https://w3c.github.io/webdriver/#fullscreen-window
   *
   * Not supported on Android.
   *
   * @returns {Promise<WindowRect>}
   *     A promise that resolves to the window rect when the window is fullscreen.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available for current application.
   */
  async fullscreenWindow() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    lazy.assert.desktop();

    return lazy.windowManager.fullscreenWindow(this.getCurrentWindow());
  }

  /**
   * Gets the properties for this accessibility node.
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Id of the accessibility node for which the properties will be returned.
   *
   * @returns {object}
   *     The properties for this accessibility node
   */
  async getAccessibilityPropertiesForAccessibilityNode(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    return this.#getActor().getAccessibilityPropertiesForAccessibilityNode(id);
  }

  /**
   * Gets the accessibility properties for this DOM element.
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Web element reference ID to the element for which the accessibility
   *     properties will be returned.
   *
   * @returns {object}
   *     The Accessibility properties for this element
   */
  async getAccessibilityPropertiesForElement(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    const webEl = lazy.WebElement.fromUUID(id).toJSON();
    return this.#getActor().getAccessibilityPropertiesForElement(webEl);
  }

  /**
   * Return the active element in the document.
   *
   * @see https://w3c.github.io/webdriver/#get-active-element
   *
   * @returns {WebReference}
   *     Active element of the current browsing context's document
   *     element, if the document element is non-null.
   *
   * @throws {NoSuchElementError}
   *     If the document does not have an active element, i.e. if
   *     its document element has been deleted.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in chrome context.
   */
  async getActiveElement() {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    return this.#getActor().getActiveElement();
  }

  /**
   * Returns the message shown in a currently displayed modal, or returns
   * a no such alert error if no modal is currently displayed.
   *
   * @see https://w3c.github.io/webdriver/#get-alert-text
   *
   * @throws {NoSuchAlertError}
   *     If there is no current user prompt.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async getAlertText() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    this.#checkIfAlertIsPresent();
    const text = await this.#dialog.getText();
    return text;
  }

  /**
   * Get the selected BrowsingContext for the current context.
   *
   * @param {object} options
   * @param {Context=} options.context
   *     Context (content or chrome) for which to retrieve the browsing context.
   *     Defaults to the current one.
   * @param {boolean=} options.parent
   *     If set to true return the window's parent browsing context,
   *     otherwise the one from the currently selected frame. Defaults to false.
   * @param {boolean=} options.top
   *     If set to true return the window's top-level browsing context,
   *     otherwise the one from the currently selected frame. Defaults to false.
   *
   * @returns {BrowsingContext}
   *     The browsing context, or `null` if none is available
   */
  getBrowsingContext(options = {}) {
    const { context = this.context, parent = false, top = false } = options;

    let browsingContext = null;
    if (context === lazy.Context.Chrome) {
      browsingContext = this.currentSession?.chromeBrowsingContext;
    } else {
      browsingContext = this.currentSession?.contentBrowsingContext;
    }

    if (browsingContext && parent) {
      browsingContext = browsingContext.parent;
    }

    if (browsingContext && top) {
      browsingContext = browsingContext.top;
    }

    return browsingContext;
  }

  /**
   * Retrieve the handler for a given WebDriver command.
   *
   * @param {string} name
   *     Name of the WebDriver command (e.g. "WebDriver:Navigate").
   *
   * @returns {Function}
   *     Unbound method on this class that implements the command.
   *
   * @throws {UnknownCommandError}
   *     If <var>name</var> does not correspond to a known command.
   */
  getCommandHandler(name) {
    const handler = GeckoDriver.#commandHandlers[name];

    if (handler === undefined) {
      throw new lazy.error.UnknownCommandError(name);
    }

    return handler;
  }

  /**
   * Determines the Accessibility label for this element.
   *
   * @see https://w3c.github.io/webdriver/#get-computed-label
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Web element reference ID to the element for which the accessibility label
   *     will be returned.
   *
   * @returns {string}
   *     The Accessibility label for this element
   */
  async getComputedLabel(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getComputedLabel(webEl);
  }

  /**
   * Determines the Accessibility role for this element.
   *
   * @see https://w3c.github.io/webdriver/#get-computed-role
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Web element reference ID to the element for which the accessibility role
   *     will be returned.
   *
   * @returns {string}
   *     The Accessibility role for this element
   */
  async getComputedRole(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();
    return this.#getActor().getComputedRole(webEl);
  }

  /**
   * Gets the context type that is Marionette's current target for
   * browsing context scoped commands.
   *
   * You may choose a context through the {@link #setContext} command.
   *
   * The default browsing context is {@link Context.Content}.
   *
   * @returns {Context}
   *     Current context.
   */
  getContext() {
    return this.context;
  }

  /**
   * Get all the cookies for the current domain.
   *
   * @see https://w3c.github.io/webdriver/#get-all-cookies
   *
   * This is the equivalent of calling <code>document.cookie</code> and
   * parsing the result.
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async getCookies() {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let { hostname, pathname } = this._getCurrentURL({ top: false });
    return [...lazy.cookie.iter(hostname, this.getBrowsingContext(), pathname)];
  }

  /**
   * Get a string representing the current URL.
   *
   * @see https://w3c.github.io/webdriver/#get-current-url
   *
   * On Desktop this returns a string representation of the URL of the
   * current top level browsing context.  This is equivalent to
   * document.location.href.
   *
   * When in the context of the chrome, this returns the canonical URL
   * of the current resource.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getCurrentUrl() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    return this._getCurrentURL().href;
  }

  /**
   * Get the currently selected window.
   *
   * It will return the outer {@link ChromeWindow} previously selected by
   * window handle through {@link #switchToWindow}, or the first window that
   * was registered.
   *
   * @param {object} options
   * @param {Context=} options.context
   *     Optional name of the context to use for finding the window.
   *     It will be required if a command always needs a specific context,
   *     whether which context is currently set. Defaults to the current
   *     context.
   *
   * @returns {ChromeWindow}
   *     The current top-level browsing context.
   */
  getCurrentWindow(options = {}) {
    const { context = this.context } = options;

    let win = null;
    switch (context) {
      case lazy.Context.Chrome:
        if (this.#curBrowser) {
          win = this.#curBrowser.window;
        }
        break;

      case lazy.Context.Content:
        if (this.#curBrowser && this.#curBrowser.contentBrowser) {
          win = this.#curBrowser.window;
        }
        break;
    }

    return win;
  }

  /**
   * Get a given attribute of an element.
   *
   * @see https://w3c.github.io/webdriver/#get-element-attribute
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Web element reference ID to the element that will be inspected.
   * @param {string} cmd.parameters.name
   *     Name of the attribute which value to retrieve.
   *
   * @returns {string}
   *     Value of the attribute.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> or <var>name</var> are not strings.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementAttribute(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    const name = lazy.assert.string(
      cmd.parameters.name,
      lazy.pprint`Expected "name" to be a string, got ${cmd.parameters.name}`
    );
    const webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementAttribute(webEl, name);
  }

  /**
   * Returns the value of a property associated with given element.
   *
   * @see https://w3c.github.io/webdriver/#get-element-property
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Web element reference ID to the element that will be inspected.
   * @param {string} cmd.parameters.name
   *     Name of the property which value to retrieve.
   *
   * @returns {string}
   *     Value of the property.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> or <var>name</var> are not strings.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementProperty(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    const id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    const name = lazy.assert.string(
      cmd.parameters.name,
      lazy.pprint`Expected "name" to be a string, got ${cmd.parameters.name}`
    );
    const webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementProperty(webEl, name);
  }

  /**
   * Returns the dimensions and coordinates of the given web element.
   *
   * @see https://w3c.github.io/webdriver/#get-element-rect
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementRect(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementRect(webEl);
  }

  /**
   * Get the tag name of the element.
   *
   * @see https://w3c.github.io/webdriver/#get-element-tag-name
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be inspected.
   *
   * @returns {string}
   *     Qualified name of the element.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementTagName(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementTagName(webEl);
  }

  /**
   * Get the text of an element, if any.  Includes the text of all child
   * elements.
   *
   * @see https://w3c.github.io/webdriver/#get-element-text
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be inspected.
   *
   * @returns {string}
   *     Element's text "as rendered".
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementText(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementText(webEl);
  }

  /**
   * Return the property of the computed style of an element.
   *
   * @see https://w3c.github.io/webdriver/#get-element-css-value
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be checked.
   * @param {string} cmd.parameters.propertyName
   *     CSS rule that is being requested.
   *
   * @returns {string}
   *     Value of |propertyName|.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> or <var>propertyName</var> are not strings.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getElementValueOfCssProperty(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let prop = lazy.assert.string(
      cmd.parameters.propertyName,
      lazy.pprint`Expected "propertyName" to be a string, got ${cmd.parameters.propertyName}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getElementValueOfCssProperty(webEl, prop);
  }

  /**
   * Gets the page source of the content document.
   *
   * @see https://w3c.github.io/webdriver/#get-page-source
   *
   * @returns {string}
   *     String serialisation of the DOM of the current browsing context's
   *     active document.
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getPageSource() {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    return this.#getActor().getPageSource();
  }

  /**
   * Get the current browser orientation.
   *
   * Will return one of the valid primary orientation values
   * portrait-primary, landscape-primary, portrait-secondary, or
   * landscape-secondary.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  getScreenOrientation() {
    lazy.assert.mobile();
    lazy.assert.open(this.getBrowsingContext({ top: true }));

    const win = this.getCurrentWindow();

    return win.screen.orientation.type;
  }

  /**
   * Return the shadow root of an element in the document.
   *
   * @see https://w3c.github.io/webdriver/#get-element-shadow-root
   *
   * @param {object} cmd
   * @param {id} cmd.parameters.id
   *     A web element id reference.
   * @returns {ShadowRoot}
   *     ShadowRoot of the element.
   *
   * @throws {InvalidArgumentError}
   *     If element <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchShadowRoot}
   *     Element does not have a shadow root attached.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in chrome current context.
   */
  async getShadowRoot(cmd) {
    // Bug 1743541: Add support for chrome scope.
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().getShadowRoot(webEl);
  }

  /**
   * Returns the timeouts for page loading, searching, and scripts.
   *
   * @see https://w3c.github.io/webdriver/#get-timeouts
   */
  getTimeouts() {
    return this.currentSession.timeouts;
  }

  /**
   * Gets the current title of the window.
   *
   * @see https://w3c.github.io/webdriver/#get-title
   *
   * @returns {string}
   *     Document title of the top-level browsing context.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getTitle() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    return this.title;
  }

  /**
   * Get the current window's handle. On desktop this typically corresponds
   * to the currently selected tab.
   *
   * For chrome scope it returns the window identifier for the current chrome
   * window for tests interested in managing the chrome window and tab separately.
   *
   * Return an opaque server-assigned identifier to this window that
   * uniquely identifies it within this Marionette instance.  This can
   * be used to switch to this window at a later point.
   *
   * @see https://w3c.github.io/webdriver/#get-window-handle
   *
   * @returns {string}
   *     Unique window handle.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  getWindowHandle() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));

    if (this.context == lazy.Context.Chrome) {
      return lazy.NavigableManager.getIdForBrowsingContext(
        this.currentSession.chromeBrowsingContext
      );
    }

    return this.#curBrowser.contentBrowserId;
  }

  /**
   * Get a list of top-level browsing contexts. On desktop this typically
   * corresponds to the set of open tabs for browser windows, or the window
   * itself for non-browser chrome windows.
   *
   * For chrome scope it returns identifiers for each open chrome window for
   * tests interested in managing a set of chrome windows and tabs separately.
   *
   * Each window handle is assigned by the server and is guaranteed unique,
   * however the return array does not have a specified ordering.
   *
   * @see https://w3c.github.io/webdriver/#get-window-handles
   *
   * @returns {Array.<string>}
   *     Unique window handles.
   */
  getWindowHandles() {
    if (this.context == lazy.Context.Chrome) {
      return lazy.windowManager.windows.map(window =>
        lazy.NavigableManager.getIdForBrowsingContext(window.browsingContext)
      );
    }

    return lazy.TabManager.getBrowsers({ unloaded: true }).map(browser =>
      lazy.NavigableManager.getIdForBrowser(browser)
    );
  }

  /**
   * A set of properties that describe a window and allow it to be uniquely
   * identified. The described window can either be a Chrome Window or a
   * Content Window.
   *
   * @typedef {object} WindowProperties
   * @property {Window} win
   *     The Chrome Window containing the window. When describing
   *     a Chrome Window, this is the window itself.
   * @property {string} id
   *     The unique id of the containing Chrome Window.
   * @property {boolean} hasTabBrowser
   *     `true` if the Chrome Window has a tabBrowser.
   * @property {number=} tabIndex
   *     Optional, the index of the specific tab within the window.
   */

  /**
   * Returns a WindowProperties object, that can be used with :js:func:`GeckoDriver#setWindowHandle`.
   *
   * @param {Window} win
   *     The Chrome Window for which we want to create a properties object.
   * @param {object=} options
   * @param {number} options.tabIndex
   *     Tab index of a specific Content Window in the specified Chrome Window.
   *
   * @returns {WindowProperties}
   *     A window properties object.
   */
  getWindowProperties(win, options = {}) {
    const { tabIndex } = options;

    if (!Window.isInstance(win)) {
      throw new TypeError("Invalid argument, expected a Window object");
    }

    return {
      win,
      id: lazy.NavigableManager.getIdForBrowsingContext(win.browsingContext),
      hasTabBrowser: !!lazy.TabManager.getTabBrowser(win),
      tabIndex,
    };
  }

  /**
   * Get the current position and size of the browser window currently in focus.
   *
   * Will return the current browser window size in pixels. Refers to
   * window outerWidth and outerHeight values, which include scroll bars,
   * title bars, etc.
   *
   * @see https://w3c.github.io/webdriver/#get-window-rect
   *
   * @returns {Promise<WindowRect>}
   *     A promise that resolves to the window rect.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async getWindowRect() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    return lazy.windowManager.getWindowRect(this.getCurrentWindow());
  }

  /**
   * Gets the current type of the window.
   *
   * @returns {string}
   *     Type of window
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  getWindowType() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));

    return this.windowType;
  }

  /**
   * Cause the browser to traverse one step backward in the joint history
   * of the current browsing context.
   *
   * @see https://w3c.github.io/webdriver/#back
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async goBack() {
    lazy.assert.content(this.context);
    const browsingContext = lazy.assert.open(
      this.getBrowsingContext({ top: true })
    );
    await this.#handleUserPrompts();

    // If there is no history, just return
    if (!browsingContext.embedderElement?.canGoBackIgnoringUserInteraction) {
      return;
    }

    if (!lazy.RemoteAgent.allowSystemAccess) {
      const { sessionHistory } = browsingContext;
      const targetEntry = sessionHistory.getEntryAtIndex(
        sessionHistory.index - 1
      );

      // Disallow navigating back to privileged URLs
      // unless system access is enabled.
      if (
        !lazy.isWebdriverSafeNavigationURL(targetEntry.URI, browsingContext)
      ) {
        throw new lazy.error.UnsupportedOperationError(
          lazy.truncate`Navigation to "${targetEntry.URI.spec}" is not allowed in this context`
        );
      }
    }

    await lazy.navigate.waitForNavigationCompleted(this, () => {
      browsingContext.goBack();
    });
  }

  /**
   * Cause the browser to traverse one step forward in the joint history
   * of the current browsing context.
   *
   * @see https://w3c.github.io/webdriver/#forward
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async goForward() {
    lazy.assert.content(this.context);
    const browsingContext = lazy.assert.open(
      this.getBrowsingContext({ top: true })
    );
    await this.#handleUserPrompts();

    // If there is no history, just return
    if (!browsingContext.embedderElement?.canGoForward) {
      return;
    }

    if (!lazy.RemoteAgent.allowSystemAccess) {
      const { sessionHistory } = browsingContext;
      const targetEntry = sessionHistory.getEntryAtIndex(
        sessionHistory.index + 1
      );

      // Disallow navigating forward to privileged URLs
      // unless system access is enabled.
      if (
        !lazy.isWebdriverSafeNavigationURL(targetEntry.URI, browsingContext)
      ) {
        throw new lazy.error.UnsupportedOperationError(
          lazy.truncate`Navigation to "${targetEntry.URI.spec}" is not allowed in this context`
        );
      }
    }

    await lazy.navigate.waitForNavigationCompleted(this, () => {
      browsingContext.goForward();
    });
  }

  installAddon(cmd) {
    const {
      addon = null,
      allowPrivateBrowsing = false,
      path = null,
      temporary = false,
    } = cmd.parameters;

    lazy.assert.boolean(
      allowPrivateBrowsing,
      lazy.pprint`Expected "allowPrivateBrowsing" to be a boolean, got ${allowPrivateBrowsing}`
    );

    lazy.assert.boolean(
      temporary,
      lazy.pprint`Expected "temporary" to be a boolean, got ${temporary}`
    );

    if (addon !== null) {
      if (path !== null) {
        throw new lazy.error.InvalidArgumentError(
          `Expected only one of "addon" or "path" to be specified`
        );
      }

      lazy.assert.string(
        addon,
        lazy.pprint`Expected "addon" to be a string, got ${addon}`
      );

      return lazy.Addon.installWithBase64(
        addon,
        temporary,
        allowPrivateBrowsing
      );
    }

    if (path !== null) {
      lazy.assert.string(
        path,
        lazy.pprint`Expected "path" to be a string, got ${path}`
      );

      return lazy.Addon.installWithPath(path, temporary, allowPrivateBrowsing);
    }

    throw new lazy.error.InvalidArgumentError(
      `Expected "addon" or "path" argument to be specified`
    );
  }

  /**
   * Check if element is displayed.
   *
   * @see https://w3c.github.io/webdriver/#element-displayedness
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be inspected.
   *
   * @returns {boolean}
   *     True if displayed, false otherwise.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async isElementDisplayed(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().isElementDisplayed(
      webEl,
      this.currentSession.capabilities
    );
  }

  /**
   * Check if element is enabled.
   *
   * @see https://w3c.github.io/webdriver/#is-element-enabled
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be checked.
   *
   * @returns {boolean}
   *     True if enabled, false if disabled.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async isElementEnabled(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().isElementEnabled(
      webEl,
      this.currentSession.capabilities
    );
  }

  /**
   * Check if element is selected.
   *
   * @see https://w3c.github.io/webdriver/#is-element-selected
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be checked.
   *
   * @returns {boolean}
   *     True if selected, false if unselected.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async isElementSelected(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().isElementSelected(
      webEl,
      this.currentSession.capabilities
    );
  }

  isReftestBrowser(element) {
    return (
      this.#reftest &&
      element &&
      element.tagName === "xul:browser" &&
      element.parentElement &&
      element.parentElement.id === "reftest"
    );
  }

  /**
   * Retrieve the localized string for the specified property id.
   *
   * Example:
   *
   *     localizeProperty(
   *         ["chrome://global/locale/findbar.properties"], "FastFind");
   *
   * @param {object} cmd
   * @param {Array.<string>} cmd.parameters.urls
   *     Array of .properties URLs.
   * @param {string} cmd.parameters.id
   *     The ID of the property to retrieve the localized string for.
   *
   * @returns {string}
   *     The localized string for the requested property.
   */
  localizeProperty(cmd) {
    let { urls, id } = cmd.parameters;

    if (!Array.isArray(urls)) {
      throw new lazy.error.InvalidArgumentError(
        "Value of `urls` should be of type 'Array'"
      );
    }
    if (typeof id != "string") {
      throw new lazy.error.InvalidArgumentError(
        "Value of `id` should be of type 'string'"
      );
    }

    return lazy.l10n.localizeProperty(urls, id);
  }

  /**
   * Maximizes the window as if the user pressed the maximize button.
   *
   * Not supported on Android.
   *
   * @see https://w3c.github.io/webdriver/#maximize-window
   *
   * @returns {Promise<WindowRect>}
   *     A promise that resolves to the window rect when the window is maximized.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available for current application.
   */
  async maximizeWindow() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    lazy.assert.desktop();

    return lazy.windowManager.maximizeWindow(this.getCurrentWindow());
  }

  /**
   * Minimizes the window as if the user pressed the minimize button.
   *
   * Not supported on Android.
   *
   * @see https://w3c.github.io/webdriver/#minimize-window
   *
   * @returns {Promise<WindowRect>}
   *     A promise that resolves to the window rect when the window is minimized.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available for current application.
   */
  async minimizeWindow() {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    lazy.assert.desktop();

    return lazy.windowManager.minimizeWindow(this.getCurrentWindow());
  }

  /**
   * Navigate to given URL.
   *
   * Navigates the current browsing context to the given URL and waits for
   * the document to load or the session's page timeout duration to elapse
   * before returning.
   *
   * The command will return with a failure if there is an error loading
   * the document or the URL is blocked.  This can occur if it fails to
   * reach host, the URL is malformed, or if there is a certificate issue
   * to name some examples.
   *
   * The document is considered successfully loaded when the
   * DOMContentLoaded event on the frame element associated with the
   * current window triggers and document.readyState is "complete".
   *
   * In chrome context it will change the current window's location to
   * the supplied URL and wait until document.readyState equals "complete"
   * or the page timeout duration has elapsed.
   *
   * @see https://w3c.github.io/webdriver/#navigate-to
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.url
   *     URL to navigate to.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async navigateTo(cmd) {
    lazy.assert.content(this.context);
    const browsingContext = lazy.assert.open(
      this.getBrowsingContext({ top: true })
    );
    await this.#handleUserPrompts();

    let { url } = cmd.parameters;

    const targetURL = URL.parse(url);
    if (!targetURL) {
      throw new lazy.error.InvalidArgumentError(
        lazy.truncate`Expected "url" to be a valid URL, got ${url}`
      );
    }

    // Disallow navigations to unsafe URLs unless
    // system access is explicitly allowed.
    if (
      !lazy.RemoteAgent.allowSystemAccess &&
      !lazy.isWebdriverSafeNavigationURL(targetURL.URI, browsingContext)
    ) {
      throw new lazy.error.UnsupportedOperationError(
        lazy.truncate`Navigation to "${targetURL.href}" is not allowed in this context`
      );
    }

    // Switch to the top-level browsing context before navigating
    this.currentSession.contentBrowsingContext = browsingContext;

    const loadEventExpected = lazy.navigate.isLoadEventExpected(
      this._getCurrentURL(),
      {
        future: targetURL,
      }
    );

    await lazy.navigate.waitForNavigationCompleted(
      this,
      () => lazy.navigate.navigateTo(browsingContext, targetURL),
      { loadEventExpected }
    );

    this.#curBrowser.contentBrowser.focus();
  }

  /**
   * Create a new WebDriver session.
   *
   * @see https://w3c.github.io/webdriver/#new-session
   *
   * @param {object} cmd
   * @param {Record<string, *>=} cmd.parameters
   *     JSON Object containing any of the recognised capabilities as listed
   *     on the `WebDriverSession` class.
   *
   * @returns {object}
   *     Session ID and capabilities offered by the WebDriver service.
   *
   * @throws {SessionNotCreatedError}
   *     If, for whatever reason, a session could not be created.
   */
  async newSession(cmd) {
    if (this.currentSession) {
      throw new lazy.error.SessionNotCreatedError(
        "Maximum number of active sessions"
      );
    }

    const { parameters: capabilities } = cmd;

    try {
      if (lazy.RemoteAgent.webDriverBiDi) {
        // If the WebDriver BiDi protocol is active always use the Remote Agent
        // to handle the WebDriver session.
        await lazy.RemoteAgent.webDriverBiDi.createSession(
          capabilities,
          this.#sessionConfigFlags
        );
      } else {
        // If it's not the case then Marionette itself needs to handle it, and
        // has to nullify the "webSocketUrl" capability.
        this.#currentSession = new lazy.WebDriverSession(
          capabilities,
          this.#sessionConfigFlags
        );
        this.#currentSession.capabilities.delete("webSocketUrl");
      }

      // Don't wait for the initial window when Marionette is in windowless mode
      if (!this.currentSession.capabilities.get("moz:windowless")) {
        // Creating a WebDriver session too early can cause issues with
        // clients in not being able to find any available window handle.
        // Also when closing the application while it's still starting up can
        // cause shutdown hangs. As such Marionette will return a new session
        // once the initial application window has finished initializing.
        lazy.logger.debug(`Waiting for initial application window`);
        await lazy.Marionette.browserStartupFinished;

        // This call includes a fallback to "mail:3pane" as well.
        const appWin = Services.wm.getMostRecentBrowserWindow();
        await lazy.windowManager.waitForChromeWindowLoaded(appWin);

        if (lazy.MarionettePrefs.clickToStart) {
          Services.prompt.alert(
            appWin,
            "",
            "Click to start execution of marionette tests"
          );
        }

        this.#addBrowser(appWin);
        this.#mainFrame = appWin;

        // Setup observer for modal dialogs
        this.#promptListener = new lazy.PromptListener(() => this.#curBrowser);
        this.#promptListener.on(
          "closed",
          this.#handleClosedModalDialog.bind(this)
        );
        this.#promptListener.on(
          "opened",
          this.#handleOpenModalDialog.bind(this)
        );
        this.#promptListener.startListening();

        for (let win of lazy.windowManager.windows) {
          this.#registerWindow(win, { registerBrowsers: true });
        }

        if (this.#mainFrame) {
          this.currentSession.chromeBrowsingContext =
            this.#mainFrame.browsingContext;
          this.#mainFrame.focus();
        }

        if (this.#curBrowser.tab) {
          const browsingContext =
            this.#curBrowser.contentBrowser.browsingContext;
          this.currentSession.contentBrowsingContext = browsingContext;

          // Bug 1838381 - Only use a longer unload timeout for desktop, because
          // on Android only the initial document is loaded, and loading a
          // specific page during startup doesn't succeed.
          const options = {};
          if (!lazy.AppInfo.isAndroid) {
            options.unloadTimeout = 5000;
          }

          await lazy.waitForInitialNavigationCompleted(
            browsingContext.webProgress,
            options
          );

          this.#curBrowser.contentBrowser.focus();
        }

        // Check if there is already an open dialog for the selected browser window.
        this.#dialog = lazy.modal.findPrompt(this.#curBrowser);
      }

      lazy.registerCommandsActor(this.currentSession.id);
      lazy.enableEventsActor();

      Services.obs.addObserver(this.#observer, TOPIC_BROWSER_READY);
    } catch (e) {
      throw new lazy.error.SessionNotCreatedError(e);
    }

    return {
      sessionId: this.currentSession.id,
      capabilities: this.currentSession.capabilities,
    };
  }

  /**
   * Open a new top-level browsing context.
   *
   * @see https://w3c.github.io/webdriver/#new-window
   *
   * @param {object} cmd
   * @param {string=} cmd.parameters.type
   *     Optional type of the new top-level browsing context. Can be one of
   *     `tab` or `window`. Defaults to `tab`.
   * @param {boolean=} cmd.parameters.focus
   *     Optional flag if the new top-level browsing context should be opened
   *     in foreground (focused) or background (not focused). Defaults to false.
   * @param {boolean=} cmd.parameters.private
   *     Optional flag, which gets only evaluated for type `window`. True if the
   *     new top-level browsing context should be a private window.
   *     Defaults to false.
   *
   * @returns {Record<string, string>}
   *     Handle and type of the new browsing context.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async newWindow(cmd) {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    let focus = false;
    if (typeof cmd.parameters.focus != "undefined") {
      focus = lazy.assert.boolean(
        cmd.parameters.focus,
        lazy.pprint`Expected "focus" to be a boolean, got ${cmd.parameters.focus}`
      );
    }

    let isPrivate = false;
    if (typeof cmd.parameters.private != "undefined") {
      isPrivate = lazy.assert.boolean(
        cmd.parameters.private,
        lazy.pprint`Expected "private" to be a boolean, got ${cmd.parameters.private}`
      );
    }

    let type;
    if (typeof cmd.parameters.type != "undefined") {
      type = lazy.assert.string(
        cmd.parameters.type,
        lazy.pprint`Expected "type" to be a string, got ${cmd.parameters.type}`
      );
    }

    // If an invalid or no type has been specified default to a tab.
    // On Android always use a new tab instead because the application has a
    // single window only.
    if (
      typeof type == "undefined" ||
      !["tab", "window"].includes(type) ||
      lazy.AppInfo.isAndroid
    ) {
      if (lazy.TabManager.supportsTabs()) {
        type = "tab";
      } else if (lazy.windowManager.supportsWindows()) {
        type = "window";
      } else {
        throw new lazy.error.UnsupportedOperationError(
          `Not supported in ${lazy.AppInfo.name}`
        );
      }
    }

    let contentBrowser;

    switch (type) {
      case "window": {
        if (lazy.windowManager.supportsWindows()) {
          let win = await this.#curBrowser.openBrowserWindow(focus, isPrivate);
          contentBrowser = lazy.TabManager.getTabBrowser(win).selectedBrowser;
        } else {
          throw new lazy.error.UnsupportedOperationError(
            `Not supported in ${lazy.AppInfo.name}`
          );
        }
        break;
      }
      default: {
        // To not fail if a new type gets added in the future, make opening
        // a new tab the default action.
        if (lazy.TabManager.supportsTabs()) {
          let tab = await this.#curBrowser.openTab(focus);
          contentBrowser = lazy.TabManager.getBrowserForTab(tab);
        } else {
          throw new lazy.error.UnsupportedOperationError(
            `Not supported in ${lazy.AppInfo.name}`
          );
        }
      }
    }

    // Actors need the new window to be loaded to safely execute queries.
    // Wait until the initial page load has been finished.
    await lazy.waitForInitialNavigationCompleted(
      contentBrowser.browsingContext.webProgress,
      {
        unloadTimeout: 5000,
      }
    );

    const id = lazy.NavigableManager.getIdForBrowser(contentBrowser);

    return { handle: id.toString(), type };
  }

  /**
   * Perform a series of grouped actions at the specified points in time.
   *
   * @see https://w3c.github.io/webdriver/#perform-actions
   *
   * @param {object} cmd
   * @param {Array<?>} cmd.parameters.actions
   *     Array of objects that each represent an action sequence.
   *
   * @throws {NoSuchElementError}
   *     If an element that is used as part of the action chain is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If an element that is used as part of the action chain has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not yet available in current context.
   */
  async performActions(cmd) {
    const { actions } = cmd.parameters;

    const browsingContext = lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    // Bug 1821460: Fetch top-level browsing context.
    const inputState = this.#actionsHelper.getInputState(browsingContext);
    const actionsOptions = {
      ...this.#actionsHelper.actionsOptions,
      context: browsingContext,
    };

    const actionChain = await lazy.actions.Chain.fromJSON(
      inputState,
      actions,
      actionsOptions
    );

    // Enqueue to serialize access to input state.
    await inputState.enqueueAction(() =>
      actionChain.dispatch(inputState, actionsOptions)
    );

    // Process async follow-up tasks in content before the reply is sent.
    await this.#actionsHelper.finalizeAction(browsingContext);
  }

  /**
   * Print page as PDF.
   *
   * @see https://w3c.github.io/webdriver/#print-page
   *
   * @param {object} cmd
   * @param {boolean=} cmd.parameters.background
   *     Whether or not to print background colors and images.
   *     Defaults to false, which prints without background graphics.
   * @param {number=} cmd.parameters.margin.bottom
   *     Bottom margin in cm. Defaults to 1cm (~0.4 inches).
   * @param {number=} cmd.parameters.margin.left
   *     Left margin in cm. Defaults to 1cm (~0.4 inches).
   * @param {number=} cmd.parameters.margin.right
   *     Right margin in cm. Defaults to 1cm (~0.4 inches).
   * @param {number=} cmd.parameters.margin.top
   *     Top margin in cm. Defaults to 1cm (~0.4 inches).
   * @param {('landscape'|'portrait')=} cmd.parameters.options.orientation
   *     Paper orientation. Defaults to 'portrait'.
   * @param {Array.<string|number>=} cmd.parameters.pageRanges
   *     Paper ranges to print, e.g., ['1-5', 8, '11-13'].
   *     Defaults to the empty array, which means print all pages.
   * @param {number=} cmd.parameters.page.height
   *     Paper height in cm. Defaults to US letter height (27.94cm / 11 inches)
   * @param {number=} cmd.parameters.page.width
   *     Paper width in cm. Defaults to US letter width (21.59cm / 8.5 inches)
   * @param {number=} cmd.parameters.scale
   *     Scale of the webpage rendering. Defaults to 1.0.
   * @param {boolean=} cmd.parameters.shrinkToFit
   *     Whether or not to override page size as defined by CSS.
   *     Defaults to true, in which case the content will be scaled
   *     to fit the paper size.
   *
   * @returns {string}
   *     Base64 encoded PDF representing printed document
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in chrome context.
   */
  async print(cmd) {
    lazy.assert.content(this.context);
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    const settings = lazy.print.addDefaultSettings(cmd.parameters);
    for (const prop of ["top", "bottom", "left", "right"]) {
      lazy.assert.positiveNumber(
        settings.margin[prop],
        lazy.pprint`Expected "margin.${prop}" to be a positive number, got ${settings.margin[prop]}`
      );
    }
    for (const prop of ["width", "height"]) {
      lazy.assert.positiveNumber(
        settings.page[prop],
        lazy.pprint`Expected "page.${prop}" to be a positive number, got ${settings.page[prop]}`
      );
    }
    lazy.assert.positiveNumber(
      settings.scale,
      lazy.pprint`Expected "scale" to be a positive number, got ${settings.scale}`
    );
    lazy.assert.that(
      s =>
        s >= lazy.print.minScaleValue &&
        settings.scale <= lazy.print.maxScaleValue,
      lazy.pprint`scale ${settings.scale} is outside the range ${lazy.print.minScaleValue}-${lazy.print.maxScaleValue}`
    )(settings.scale);
    lazy.assert.boolean(
      settings.shrinkToFit,
      lazy.pprint`Expected "shrinkToFit" to be a boolean, got ${settings.shrinkToFit}`
    );
    lazy.assert.that(
      orientation => lazy.print.defaults.orientationValue.includes(orientation),
      lazy.pprint`orientation ${
        settings.orientation
      } doesn't match allowed values "${lazy.print.defaults.orientationValue.join(
        "/"
      )}"`
    )(settings.orientation);
    lazy.assert.boolean(
      settings.background,
      lazy.pprint`Expected "background" to be a boolean, got ${settings.background}`
    );
    lazy.assert.array(
      settings.pageRanges,
      lazy.pprint`Expected "pageRanges" to be an array, got ${settings.pageRanges}`
    );

    const browsingContext = this.#curBrowser.tab.linkedBrowser.browsingContext;
    const printSettings = await lazy.print.getPrintSettings(settings);
    const binaryString = await lazy.print.printToBinaryString(
      browsingContext,
      printSettings
    );

    return btoa(binaryString);
  }

  /**
   * Quits the application with the provided flags.
   *
   * Marionette will stop accepting new connections before ending the
   * current session, and finally attempting to quit the application.
   *
   * Optional {@link nsIAppStartup} flags may be provided as
   * an array of masks, and these will be combined by ORing
   * them with a bitmask.  The available masks are defined in
   * https://developer.mozilla.org/en-US/docs/Mozilla/Tech/XPCOM/Reference/Interface/nsIAppStartup.
   *
   * Crucially, only one of the *Quit flags can be specified. The |eRestart|
   * flag may be bit-wise combined with one of the *Quit flags to cause
   * the application to restart after it quits.
   *
   * @param {object} cmd
   * @param {Array.<string>=} cmd.parameters.flags
   *     Constant name of masks to pass to |Services.startup.quit|.
   *     If empty or undefined, |nsIAppStartup.eAttemptQuit| is used.
   * @param {boolean=} cmd.parameters.safeMode
   *     Optional flag to indicate that the application has to
   *     be restarted in safe mode.
   *
   * @returns {Record<string,boolean>}
   *     Dictionary containing information that explains the shutdown reason.
   *     The value for `cause` contains the shutdown kind like "shutdown" or
   *     "restart", while `forced` will indicate if it was a normal or forced
   *     shutdown of the application. "in_app" is always set to indicate that
   *     it is a shutdown triggered from within the application.
   *
   * @throws {InvalidArgumentError}
   *     If <var>flags</var> contains unknown or incompatible flags,
   *     for example multiple Quit flags.
   */
  async quit(cmd) {
    const { flags = [], safeMode = false } = cmd.parameters;

    lazy.assert.array(
      flags,
      lazy.pprint`Expected "flags" to be an array, got ${flags}`
    );
    lazy.assert.boolean(
      safeMode,
      lazy.pprint`Expected "safeMode" to be a boolean, got ${safeMode}`
    );

    if (safeMode && !flags.includes("eRestart")) {
      throw new lazy.error.InvalidArgumentError(
        `"safeMode" only works with restart flag`
      );
    }

    // Register handler to run Marionette specific shutdown code.
    Services.obs.addObserver(this.#observer, TOPIC_QUIT_APPLICATION_REQUESTED);

    let quitApplicationResponse;
    try {
      this.#isShuttingDown = true;
      quitApplicationResponse = await lazy.quit(
        flags,
        safeMode,
        this.currentSession.capabilities.get("moz:windowless")
      );
    } catch (e) {
      this.#isShuttingDown = false;
      if (e instanceof TypeError) {
        throw new lazy.error.InvalidArgumentError(e.message);
      }
      throw new lazy.error.UnsupportedOperationError(e.message);
    } finally {
      Services.obs.removeObserver(
        this.#observer,
        TOPIC_QUIT_APPLICATION_REQUESTED
      );
    }

    return quitApplicationResponse;
  }

  /**
   * Causes the browser to reload the page in current top-level browsing
   * context.
   *
   * @see https://w3c.github.io/webdriver/#refresh
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async refresh() {
    lazy.assert.content(this.context);
    const browsingContext = lazy.assert.open(
      this.getBrowsingContext({ top: true })
    );
    await this.#handleUserPrompts();

    // Disallow refreshing privileged URLs
    // unless system access is enabled.
    if (
      !lazy.RemoteAgent.allowSystemAccess &&
      !lazy.isWebdriverSafeNavigationURL(
        browsingContext.currentURI,
        browsingContext
      )
    ) {
      throw new lazy.error.UnsupportedOperationError(
        lazy.truncate`Refreshing "${browsingContext.currentURI.spec}" is not allowed in this context`
      );
    }

    // Switch to the top-level browsing context before navigating
    this.currentSession.contentBrowsingContext = browsingContext;

    await lazy.navigate.waitForNavigationCompleted(this, () =>
      lazy.navigate.refresh(browsingContext)
    );
  }

  /**
   * Register a chrome protocol handler for a directory containing XHTML or XUL
   * files, allowing them to be loaded via the chrome:// protocol.
   *
   * @param {obj} cmd
   * @param {string} cmd.parameters.manifestPath
   *     The base manifest path for the entries. URL values are resolved
   *     relative to this path.
   * @param {Array<Array<string, string, string>>} cmd.parameters.entries
   *     An array of arrays, each containing a registry entry (type, namespace,
   *     path, options) as it would appear in a chrome.manifest file. Only the
   *     following entry types are currently accepted:
   *
   *         - "content" A URL entry. Must be a 3-element array.
   *         - "override" A URL override entry. Must be a 3-element array.
   *         - "locale" A locale package entry. Must be a 4-element array.
   *
   * @returns {string} id
   *     The identifier for the registered chrome protocol handler.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {UnknownError}
   *     If there is no such registered chrome protocol handler.
   */
  registerChromeHandler(cmd) {
    const manifestPath = lazy.assert.string(
      cmd.parameters.manifestPath,
      lazy.pprint`Expected "path" to be a string, got ${cmd.parameters.manifestPath}`
    );

    const entries = lazy.assert.array(
      cmd.parameters.entries,
      lazy.pprint`Expected "entries" to be an array, got ${cmd.parameters.entries}`
    );
    entries.forEach(entry => {
      const [type, namespace, directory, options] = lazy.assert.array(
        entry,
        lazy.pprint`Expected values of "entries" to be an array, got ${entries}`
      );
      lazy.assert.string(
        type,
        lazy.pprint`Expected "type" of entry to be a string, got ${type}`
      );
      lazy.assert.string(
        namespace,
        lazy.pprint`Expected "namespace" of entry to be a string, got ${namespace}`
      );
      lazy.assert.string(
        directory,
        lazy.pprint`Expected "directory" of entry to be a string, got ${directory}`
      );
      if (options !== undefined) {
        lazy.assert.string(
          options,
          lazy.pprint`Expected "options" of entry to be a string, got ${options}`
        );
      }
    });

    lazy.assert.hasSystemAccess();

    return this.currentSession.registerChromeHandler(manifestPath, entries);
  }

  /**
   * Release all the keys and pointer buttons that are currently depressed.
   *
   * @see https://w3c.github.io/webdriver/#release-actions
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not available in current context.
   */
  async releaseActions() {
    const browsingContext = lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    // Bug 1821460: Fetch top-level browsing context.
    const inputState = this.#actionsHelper.getInputState(browsingContext);
    const actionsOptions = {
      ...this.#actionsHelper.actionsOptions,
      context: browsingContext,
    };

    // Enqueue to serialize access to input state.
    await inputState.enqueueAction(() => {
      const undoActions = inputState.inputCancelList.reverse();
      return undoActions.dispatch(inputState, actionsOptions);
    });

    this.#actionsHelper.resetInputState(browsingContext);

    // Process async follow-up tasks in content before the reply is sent.
    await this.#actionsHelper.finalizeAction(browsingContext);
  }

  /** Run a reftest. */
  runReftest(cmd) {
    let { test, references, expected, timeout, width, height, pageRanges } =
      cmd.parameters;

    if (!this.#reftest) {
      throw new lazy.error.UnsupportedOperationError(
        "Called reftest:run before reftest:start"
      );
    }

    lazy.assert.string(
      test,
      lazy.pprint`Expected "test" to be a string, got ${test}`
    );
    lazy.assert.string(
      expected,
      lazy.pprint`Expected "expected" to be a string, got ${expected}`
    );
    lazy.assert.array(
      references,
      lazy.pprint`Expected "references" to be an array, got ${references}`
    );

    return this.#reftest.run(
      test,
      references,
      expected,
      timeout,
      pageRanges,
      width,
      height
    );
  }

  /**
   * Set the user prompt's value field.
   *
   * Sends keys to the input field of a currently displayed modal, or
   * returns a no such alert error if no modal is currently displayed. If
   * a modal dialog is currently displayed but has no means for text input,
   * an element not visible error is returned.
   *
   * @see https://w3c.github.io/webdriver/#send-alert-text
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.text
   *     Input to the user prompt's value field.
   *
   * @throws {ElementNotInteractableError}
   *     If the current user prompt is an alert or confirm.
   * @throws {NoSuchAlertError}
   *     If there is no current user prompt.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnsupportedOperationError}
   *     If the current user prompt is something other than an alert,
   *     confirm, or a prompt.
   */
  async sendKeysToDialog(cmd) {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    this.#checkIfAlertIsPresent();

    let text = lazy.assert.string(
      cmd.parameters.text,
      lazy.pprint`Expected "text" to be a string, got ${cmd.parameters.text}`
    );
    let promptType = this.#dialog.args.promptType;

    switch (promptType) {
      case "alert":
      case "confirm":
        throw new lazy.error.ElementNotInteractableError(
          `User prompt of type ${promptType} is not interactable`
        );
      case "prompt":
        break;
      default:
        await this.dismissAlert();
        throw new lazy.error.UnsupportedOperationError(
          `User prompt of type ${promptType} is not supported`
        );
    }
    this.#dialog.text = text;
  }

  /**
   * Send key presses to element after focusing on it.
   *
   * @see https://w3c.github.io/webdriver/#element-send-keys
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.id
   *     Reference ID to the element that will be checked.
   * @param {string} cmd.parameters.text
   *     Value to send to the element.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> or <var>text</var> are not strings.
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async sendKeysToElement(cmd) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    let id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );
    let text = lazy.assert.string(
      cmd.parameters.text,
      lazy.pprint`Expected "text" to be a string, got ${cmd.parameters.text}`
    );
    let webEl = lazy.WebElement.fromUUID(id).toJSON();

    return this.#getActor().sendKeysToElement(
      webEl,
      text,
      this.currentSession.capabilities
    );
  }

  /**
   * Sets the context of the subsequent commands.
   *
   * All subsequent requests to commands that in some way involve
   * interaction with a browsing context will target the chosen browsing
   * context.
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.value
   *     Name of the context to be switched to.  Must be one of "chrome" or
   *     "content".
   *
   * @throws {InvalidArgumentError}
   *     If <var>value</var> is not a string.
   * @throws {WebDriverError}
   *     If <var>value</var> is not a valid browsing context.
   */
  setContext(cmd) {
    let value = lazy.assert.string(
      cmd.parameters.value,
      lazy.pprint`Expected "value" to be a string, got ${cmd.parameters.value}`
    );

    this.context = value;
  }

  /**
   * Set the current browser orientation.
   *
   * The supplied orientation should be given as one of the valid
   * orientation values.  If the orientation is unknown, an error will
   * be raised.
   *
   * Valid orientations are "portrait" and "landscape", which fall
   * back to "portrait-primary" and "landscape-primary" respectively,
   * and "portrait-secondary" as well as "landscape-secondary".
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async setScreenOrientation(cmd) {
    lazy.assert.mobile();
    lazy.assert.open(this.getBrowsingContext({ top: true }));

    const ors = [
      "portrait",
      "landscape",
      "portrait-primary",
      "landscape-primary",
      "portrait-secondary",
      "landscape-secondary",
    ];

    let or = String(cmd.parameters.orientation);
    lazy.assert.string(
      or,
      lazy.pprint`Expected "or" to be a string, got ${or}`
    );
    let mozOr = or.toLowerCase();
    if (!ors.includes(mozOr)) {
      throw new lazy.error.InvalidArgumentError(
        `Unknown screen orientation: ${or}`
      );
    }

    const win = this.getCurrentWindow();

    try {
      await win.screen.orientation.lock(mozOr);
    } catch (e) {
      throw new lazy.error.WebDriverError(
        `Unable to set screen orientation: ${or}`
      );
    }
  }

  /**
   * Set timeout for page loading, searching, and scripts.
   *
   * @see https://w3c.github.io/webdriver/#set-timeouts
   *
   * @param {object} cmd
   * @param {Record<string, number>} cmd.parameters
   *     Dictionary of timeout types and their new value, where all timeout
   *     types are optional.
   *
   * @throws {InvalidArgumentError}
   *     If timeout type key is unknown, or the value provided with it is
   *     not an integer.
   */
  setTimeouts(cmd) {
    // merge with existing timeouts
    let merged = Object.assign(
      this.currentSession.timeouts.toJSON(),
      cmd.parameters
    );

    this.currentSession.timeouts = lazy.Timeouts.fromJSON(merged);
  }

  /**
   * Initialize the reftest mode
   */
  async setupReftest(cmd) {
    if (this.#reftest) {
      throw new lazy.error.UnsupportedOperationError(
        "Called reftest:setup with a reftest session already active"
      );
    }

    let {
      urlCount = {},
      screenshot = "unexpected",
      isPrint = false,
      cacheScreenshots = true,
    } = cmd.parameters;
    if (!["always", "fail", "unexpected"].includes(screenshot)) {
      throw new lazy.error.InvalidArgumentError(
        "Value of `screenshot` should be 'always', 'fail' or 'unexpected'"
      );
    }

    this.#reftest = new lazy.reftest.Runner(this);
    this.#reftest.setup(urlCount, screenshot, isPrint, cacheScreenshots);
  }

  /**
   * Switch the marionette window to a given window. If the browser in
   * the window is unregistered, register that browser and wait for
   * the registration is complete. If |focus| is true then set the focus
   * on the window.
   *
   * @param {object} winProperties
   *     Object containing window properties such as returned from
   *     :js:func:`GeckoDriver#getWindowProperties`
   * @param {boolean=} focus
   *     A boolean value which determines whether to focus the window.
   *     Defaults to true.
   */
  async setWindowHandle(winProperties, focus = true) {
    if (!(winProperties.id in this.#browsers)) {
      // Initialise Marionette if the current chrome window has not been seen
      // before. Also register the initial tab, if one exists.
      this.#addBrowser(winProperties.win);
      this.#mainFrame = winProperties.win;

      this.currentSession.chromeBrowsingContext =
        this.#mainFrame.browsingContext;

      if (!winProperties.hasTabBrowser) {
        this.currentSession.contentBrowsingContext = null;
      } else {
        const tabBrowser = lazy.TabManager.getTabBrowser(winProperties.win);

        // For chrome windows such as a reftest window, `getTabBrowser` is not
        // a tabbrowser, it is the content browser which should be used here.
        const contentBrowser = tabBrowser.tabs
          ? tabBrowser.selectedBrowser
          : tabBrowser;

        this.currentSession.contentBrowsingContext =
          contentBrowser.browsingContext;
        this.#registerBrowser(contentBrowser);
      }
    } else {
      // Otherwise switch to the known chrome window
      this.#curBrowser = this.#browsers[winProperties.id];
      this.#mainFrame = this.#curBrowser.window;

      // Activate the tab if it's a content window.
      let tab = null;
      if (winProperties.hasTabBrowser) {
        tab = await this.#curBrowser.switchToTab(
          winProperties.tabIndex,
          winProperties.win,
          focus
        );
      }

      this.currentSession.chromeBrowsingContext =
        this.#mainFrame.browsingContext;
      this.currentSession.contentBrowsingContext =
        tab?.linkedBrowser.browsingContext;
    }

    // Check for an existing dialog for the new window
    this.#dialog = lazy.modal.findPrompt(this.#curBrowser);

    // If there is an open window modal dialog the underlying chrome window
    // cannot be focused.
    if (focus && !this.#dialog?.isWindowModal) {
      await this.#curBrowser.focusWindow();
    }
  }

  /**
   * Set the window position and size on the operating system window manager.
   *
   * The supplied `width` and `height` values refer to the window `outerWidth`
   * and `outerHeight` values, which include browser chrome and OS-level
   * window borders.
   *
   * @see https://w3c.github.io/webdriver/#set-window-rect
   *
   * @param {object} cmd
   * @param {number} cmd.parameters.x
   *     X coordinate of the top/left of the window that it will be moved to.
   * @param {number} cmd.parameters.y
   *     Y coordinate of the top/left of the window that it will be moved to.
   * @param {number} cmd.parameters.width
   *     Width to resize the window to.
   * @param {number} cmd.parameters.height
   *     Height to resize the window to.
   *
   * @returns {WindowRect<string, number>}
   *     A promise that resolves to the window rect when the window
   *     geometry has been adjusted.
   *
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   * @throws {UnsupportedOperationError}
   *     Not applicable to application.
   */
  async setWindowRect(cmd) {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    lazy.assert.desktop();

    const { x = null, y = null, width = null, height = null } = cmd.parameters;
    if (x !== null) {
      lazy.assert.integer(
        x,
        lazy.pprint`Expected "x" to be an integer value, got ${x}`
      );
    }
    if (y !== null) {
      lazy.assert.integer(
        y,
        lazy.pprint`Expected "y" to be an integer value, got ${y}`
      );
    }
    if (height !== null) {
      lazy.assert.positiveInteger(
        height,
        lazy.pprint`Expected "height" to be a positive integer value, got ${height}`
      );
    }
    if (width !== null) {
      lazy.assert.positiveInteger(
        width,
        lazy.pprint`Expected "width" to be a positive integer value, got ${width}`
      );
    }

    return lazy.windowManager.adjustWindowGeometry(
      this.getCurrentWindow(),
      x,
      y,
      width,
      height
    );
  }

  /**
   * Switch to a given frame within the current window.
   *
   * @see https://w3c.github.io/webdriver/#switch-to-frame
   *
   * @param {object} cmd
   * @param {(string | object)=} cmd.parameters.element
   *     A web element reference of the frame or its element id.
   * @param {number=} cmd.parameters.id
   *     The index of the frame to switch to.
   *     If both element and id are not defined, switch to top-level frame.
   *
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>element</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>element</var> has gone stale.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async switchToFrame(cmd) {
    const { element: el, id } = cmd.parameters;

    if (typeof id == "number") {
      lazy.assert.unsignedShort(
        id,
        lazy.pprint`Expected "id" to be an unsigned short, got ${id}`
      );
    }

    const top = id == null && el == null;
    lazy.assert.open(this.getBrowsingContext({ top }));
    await this.#handleUserPrompts();

    // Bug 1495063: Elements should be passed as WebReference reference
    let byFrame;
    if (typeof el == "string") {
      byFrame = lazy.WebElement.fromUUID(el).toJSON();
    } else if (el) {
      byFrame = el;
    }

    // If the current context changed during the switchToFrame call, attempt to
    // call switchToFrame again until the browsing context remains stable.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1786640#c11
    let browsingContext;
    for (let i = 0; i < 5; i++) {
      const currentBrowsingContext = this.currentSession.contentBrowsingContext;
      ({ browsingContext } = await this.#getActor({ top }).switchToFrame(
        byFrame || id
      ));

      if (
        currentBrowsingContext == this.currentSession.contentBrowsingContext
      ) {
        break;
      }
    }

    this.currentSession.contentBrowsingContext = browsingContext;
  }

  /**
   * Set the current browsing context for future commands to the parent
   * of the current browsing context.
   *
   * @see https://w3c.github.io/webdriver/#switch-to-parent-frame
   *
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {UnexpectedAlertOpenError}
   *     A modal dialog is open, blocking this operation.
   */
  async switchToParentFrame() {
    let browsingContext = this.getBrowsingContext();
    if (browsingContext && !browsingContext.parent) {
      return;
    }

    browsingContext = lazy.assert.open(browsingContext?.parent);

    this.currentSession.contentBrowsingContext = browsingContext;
  }

  /**
   * Switch current top-level browsing context by name or server-assigned
   * ID.  Searches for windows by name, then ID.  Content windows take
   * precedence.
   *
   * @see https://w3c.github.io/webdriver/#switch-to-window
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.handle
   *     Handle of the window to switch to.
   * @param {boolean=} cmd.parameters.focus
   *     A boolean value which determines whether to focus
   *     the window. Defaults to true.
   *
   * @throws {InvalidArgumentError}
   *     If <var>handle</var> is not a string or <var>focus</var> not a boolean.
   * @throws {NoSuchWindowError}
   *     Top-level browsing context has been discarded.
   */
  async switchToWindow(cmd) {
    const { focus = true, handle } = cmd.parameters;

    lazy.assert.string(
      handle,
      lazy.pprint`Expected "handle" to be a string, got ${handle}`
    );
    lazy.assert.boolean(
      focus,
      lazy.pprint`Expected "focus" to be a boolean, got ${focus}`
    );

    const found = this.#findWindowByHandle(handle);

    let selected = false;
    if (found) {
      try {
        await this.setWindowHandle(found, focus);
        selected = true;
      } catch (e) {
        lazy.logger.error(e);
      }
    }

    if (!selected) {
      throw new lazy.error.NoSuchWindowError(
        `Unable to locate window: ${handle}`
      );
    }
  }

  /**
   * Takes a screenshot of a web element, current frame, or viewport.
   *
   * The screen capture is returned as a lossless PNG image encoded as
   * a base 64 string.
   *
   * If called in the content context, the |id| argument is not null and
   * refers to a present and visible web element's ID, the capture area will
   * be limited to the bounding box of that element.  Otherwise, the capture
   * area will be the bounding box of the current frame.
   *
   * If called in the chrome context, the screenshot will always represent
   * the entire viewport.
   *
   * @see https://w3c.github.io/webdriver/#take-screenshot
   *
   * @param {object} cmd
   * @param {string=} cmd.parameters.id
   *     Optional web element reference to take a screenshot of.
   *     If undefined, a screenshot will be taken of the document element.
   * @param {boolean=} cmd.parameters.full
   *     True to take a screenshot of the entire document element. Is only
   *     considered if <var>id</var> is not defined. Defaults to true.
   * @param {boolean=} cmd.parameters.hash
   *     True if the user requests a hash of the image data. Defaults to false.
   * @param {boolean=} cmd.parameters.scroll
   *     Scroll to element if |id| is provided. Defaults to true.
   *
   * @returns {string}
   *     If <var>hash</var> is false, PNG image encoded as Base64 encoded
   *     string.  If <var>hash</var> is true, hex digest of the SHA-256
   *     hash of the Base64 encoded string.
   *
   * @throws {NoSuchElementError}
   *     If element represented by reference <var>id</var> is unknown.
   * @throws {NoSuchWindowError}
   *     Browsing context has been discarded.
   * @throws {StaleElementReferenceError}
   *     If element represented by reference <var>id</var> has gone stale.
   */
  async takeScreenshot(cmd) {
    lazy.assert.open(this.getBrowsingContext({ top: true }));
    await this.#handleUserPrompts();

    let { id, full, hash, scroll } = cmd.parameters;
    let format = hash ? lazy.capture.Format.Hash : lazy.capture.Format.Base64;

    full = typeof full == "undefined" ? true : full;
    scroll = typeof scroll == "undefined" ? true : scroll;

    let webEl = id ? lazy.WebElement.fromUUID(id).toJSON() : null;

    // Only consider full screenshot if no element has been specified
    full = webEl ? false : full;

    return this.#getActor().takeScreenshot(webEl, format, full, scroll);
  }

  /**
   * End a reftest run.
   *
   * Closes the reftest window (without changing the current window handle),
   * and removes cached canvases.
   */
  teardownReftest() {
    if (!this.#reftest) {
      throw new lazy.error.UnsupportedOperationError(
        "Called reftest:teardown before reftest:start"
      );
    }

    this.#reftest.teardown();
    this.#reftest = null;
  }

  uninstallAddon(cmd) {
    let id = cmd.parameters.id;
    if (typeof id == "undefined" || typeof id != "string") {
      throw new lazy.error.InvalidArgumentError();
    }

    return lazy.Addon.uninstall(id);
  }

  /**
   * Unregister a previously registered chrome protocol handler.
   *
   * @param {obj} cmd
   * @param {string} cmd.parameters.id
   *     The identifier returned when the chrome handler was registered.
   *
   * @throws {InvalidArgumentError}
   *     If <var>id</var> is not a string.
   * @throws {UnknownError}
   *     If there is no such registered chrome protocol handler.
   */
  unregisterChromeHandler(cmd) {
    const id = lazy.assert.string(
      cmd.parameters.id,
      lazy.pprint`Expected "id" to be a string, got ${cmd.parameters.id}`
    );

    lazy.assert.hasSystemAccess();

    this.currentSession.unregisterChromeHandler(id);
  }

  // Commands from external specifications.

  /**
   * Returns the do-not-sell-or-share preference for the current session.
   *
   * @see https://w3c.github.io/gpc/#get-global-privacy-control
   */
  gpc_getGlobalPrivacyControl() {
    const gpc = Services.prefs.getBoolPref(
      "privacy.globalprivacycontrol.enabled",
      true
    );

    return { gpc };
  }

  /**
   * Modifies the do-not-sell-or-share preference for the current session.
   *
   * @see https://w3c.github.io/gpc/#set-global-privacy-control
   */
  gpc_setGlobalPrivacyControl(cmd) {
    const { gpc } = cmd.parameters;

    if (typeof gpc != "boolean") {
      throw new lazy.error.InvalidArgumentError(
        "Value of `gpc` should be of type 'boolean'"
      );
    }

    Services.prefs.setBoolPref("privacy.globalprivacycontrol.enabled", gpc);

    return { gpc };
  }

  /**
   * Simulates user modification of a PermissionDescriptor's permission state.
   *
   * @see https://www.w3.org/TR/permissions/#webdriver-command-set-permission
   */
  async permissions_setPermission(cmd) {
    const { descriptor, oneRealm = false, state } = cmd.parameters;
    const browsingContext = lazy.assert.open(this.getBrowsingContext());

    lazy.permissions.validateDescriptor(descriptor);
    lazy.permissions.validateState(state);

    let params;
    try {
      params =
        await this.#curBrowser.window.navigator.permissions.parseSetParameters({
          descriptor,
          state,
        });
    } catch (err) {
      throw new lazy.error.InvalidArgumentError(
        `setPermission: ${err.message}`
      );
    }

    lazy.assert.boolean(
      oneRealm,
      lazy.pprint`Expected "oneRealm" to be a boolean, got ${oneRealm}`
    );

    let origin = browsingContext.currentURI.prePath;

    // storage-access is a special case.
    if (descriptor.name === "storage-access") {
      origin = browsingContext.top.currentURI.prePath;

      params = {
        type: lazy.permissions.getStorageAccessPermissionsType(
          browsingContext.currentWindowGlobal.documentURI
        ),
      };
    }

    lazy.permissions.set(params, state, origin);
  }

  /**
   * Implements the GenerateTestReport functionality of the Reporting API.
   *
   * @see https://w3c.github.io/reporting/#generate-test-report-command *
   *
   * @param {object} cmd
   * @param {string} cmd.parameters.message
   *     The message contents of the report being generated.
   * @param {string=} cmd.parameters.group
   *     The name of the reporting endpoint that the report should be sent to.
   *     @see https://www.w3.org/TR/reporting-1/#endpoint
   *
   * @throws {InvalidArgumentError}
   *     If a message argument wasn't passed in the parameters.
   */
  async reporting_generateTestReport(cmd) {
    const { message, group = "default" } = cmd.parameters;

    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    lazy.assert.string(
      message,
      lazy.pprint(`Expected "message" to be a string, got ${message}`)
    );

    lazy.assert.string(
      group,
      lazy.pprint(`Expected "group" to be a string, got ${group}`)
    );

    await this.#getActor().generateTestReport(message, group);
  }

  /**
   * Add a credential to a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to add the credential to.
   * @param {object} cmd.parameters.credentials
   *     The credential to add.
   * @param {string} cmd.parameters.credentials.credentialId
   *     A probabilistically-unique byte sequence identifying a public key
   *     credential source and its authentication assertions, encoded using
   *     Base64url Encoding.
   * @param {boolean} cmd.parameters.credentials.isResidentCredential
   *     If true, a client-side discoverable credential is created. If false,
   *     a server-side credential is created instead.
   * @param {string} cmd.parameters.credentials.rpId
   *     The Relying Party ID the credential is scoped to.
   * @param {string} cmd.parameters.credentials.privateKey
   *     An asymmetric key package containing a single private key per RFC5958,
   *     encoded using Base64url Encoding.
   * @param {string} [cmd.parameters.credentials.userHandle]
   *     The userHandle associated with the credential, encoded using Base64url
   *     Encoding.
   * @param {number} cmd.parameters.credentials.signCount
   *     The initial value for a signature counter associated with the public
   *     key credential source.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-add-credential
   */
  webAuthn_addCredential(cmd) {
    const { authenticatorId, credentials } = cmd.parameters;
    const {
      credentialId,
      isResidentCredential,
      rpId,
      privateKey,
      userHandle,
      signCount,
    } = credentials;

    this.#assertVirtualAuthenticator(authenticatorId);

    lazy.assert.string(
      credentialId,
      lazy.pprint`Expected "credentialId" to be a string, got ${credentialId}`
    );

    lazy.assert.boolean(
      isResidentCredential,
      lazy.pprint`Expected "isResidentCredential" to be a boolean, got ${isResidentCredential}`
    );

    lazy.assert.string(
      rpId,
      lazy.pprint`Expected "rpId" to be a string, got ${rpId}`
    );

    lazy.assert.string(
      privateKey,
      lazy.pprint`Expected "privateKey" to be a string, got ${privateKey}`
    );

    if (userHandle) {
      lazy.assert.string(
        userHandle,
        lazy.pprint`Expected "userHandle" to be a string, got ${userHandle}`
      );
    }

    lazy.assert.number(
      signCount,
      lazy.pprint`Expected "signCount" to be a number, got ${signCount}`
    );

    lazy.webauthn.addCredential(authenticatorId, {
      credentialId,
      isResidentCredential,
      rpId,
      privateKey,
      userHandle,
      signCount,
    });
  }

  /**
   * Add a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {ProtocolType} cmd.parameters.protocol
   *     The protocol this authenticator speaks.
   * @param {TransportType} cmd.parameters.transport
   *     The transport this authenticator uses.
   * @param {boolean=} cmd.parameters.hasResidentKey
   *     Whether the authenticator supports client-side discoverable credentials.
   *     Defaults to false.
   * @param {boolean=} cmd.parameters.hasUserVerification
   *     Whether the authenticator supports user verification. Defaults to false.
   * @param {boolean=} cmd.parameters.isUserConsenting
   *     Whether the authenticator will simulate user consent for all operations.
   *     Defaults to false.
   * @param {boolean=} cmd.parameters.isUserVerified
   *     Whether the authenticator simulates always passing user verification.
   *     Defaults to false.
   *
   * @returns {string}
   *     The ID of the added virtual authenticator.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-add-virtual-authenticator
   */
  webAuthn_addVirtualAuthenticator(cmd) {
    const {
      protocol,
      transport,
      hasResidentKey = false,
      hasUserVerification = false,
      isUserConsenting = true,
      isUserVerified = false,
    } = cmd.parameters;

    const protocolTypes = Object.values(lazy.webauthn.ProtocolType);
    lazy.assert.that(
      value => protocolTypes.includes(value),
      lazy.pprint`Expected "protocol" to be one of ${protocolTypes}, got ${protocol}`
    )(protocol);

    const transportTypes = Object.values(lazy.webauthn.TransportType);
    lazy.assert.that(
      value => transportTypes.includes(value),
      lazy.pprint`Expected "protocol" to be one of ${transportTypes}, got ${transport}`
    )(transport);

    lazy.assert.boolean(
      hasResidentKey,
      lazy.pprint`Expected "hasResidentKey" to be a boolean, got ${hasResidentKey}`
    );
    lazy.assert.boolean(
      hasUserVerification,
      lazy.pprint`Expected "hasUserVerification" to be a boolean, got ${hasUserVerification}`
    );
    lazy.assert.boolean(
      isUserConsenting,
      lazy.pprint`Expected "isUserConsenting" to be a boolean, got ${isUserConsenting}`
    );
    lazy.assert.boolean(
      isUserVerified,
      lazy.pprint`Expected "isUserVerified" to be a boolean, got ${isUserVerified}`
    );

    return lazy.webauthn.addVirtualAuthenticator({
      protocol,
      transport,
      hasResidentKey,
      hasUserVerification,
      isUserConsenting,
      isUserVerified,
    });
  }

  /**
   * Get credentials stored in a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to retrieve credentials from.
   *
   * @returns {object}
   *     The credentials stored on the virtual authenticator.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-get-credentials
   */
  webAuthn_getCredentials(cmd) {
    const { authenticatorId } = cmd.parameters;

    this.#assertVirtualAuthenticator(authenticatorId);

    return lazy.webauthn.getCredentials(authenticatorId);
  }

  /**
   * Remove a credential from a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to remove the credential from.
   * @param {string} cmd.parameters.credentialId
   *     The ID of the credential to remove.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-remove-credential
   */
  webAuthn_removeCredential(cmd) {
    const { authenticatorId, credentialId } = cmd.parameters;

    this.#assertVirtualAuthenticator(authenticatorId);
    this.#assertCredential(authenticatorId, credentialId);

    lazy.webauthn.removeCredential(authenticatorId, credentialId);
  }

  /**
   * Remove all credentials from a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to remove all credentials from.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-remove-all-credentials
   */
  webAuthn_removeAllCredentials(cmd) {
    const { authenticatorId } = cmd.parameters;

    this.#assertVirtualAuthenticator(authenticatorId);

    lazy.webauthn.removeAllCredentials(authenticatorId);
  }

  /**
   * Remove a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to remove.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-remove-virtual-authenticator
   */
  webAuthn_removeVirtualAuthenticator(cmd) {
    const { authenticatorId } = cmd.parameters;

    this.#assertVirtualAuthenticator(authenticatorId);

    lazy.webauthn.removeVirtualAuthenticator(authenticatorId);
  }

  /**
   * Set the user verified flag on a virtual authenticator.
   *
   * @param {object} cmd
   * @param {object} cmd.parameters
   * @param {string} cmd.parameters.authenticatorId
   *     The ID of the virtual authenticator to update.
   * @param {boolean} cmd.parameters.isUserVerified
   *     The value to set the "isUserVerified" bit to on the authenticator.
   *
   * @see https://www.w3.org/TR/webauthn-3/#sctn-automation-set-user-verified
   */
  webAuthn_setUserVerified(cmd) {
    const { authenticatorId, isUserVerified } = cmd.parameters;

    this.#assertVirtualAuthenticator(authenticatorId);

    lazy.assert.boolean(
      isUserVerified,
      lazy.pprint`Expected "isUserVerified" to be a boolean, got ${isUserVerified}`
    );

    lazy.webauthn.setUserVerified(authenticatorId, isUserVerified);
  }

  /**
   * Create a new browsing context for window and add to known browsers.
   *
   * @param {ChromeWindow} win
   *     Window for which we will create a browsing context.
   *
   * @returns {string}
   *     Returns the unique server-assigned ID of the window.
   */
  #addBrowser(win) {
    let context = new lazy.browser.Context(win, this);
    let winId = lazy.NavigableManager.getIdForBrowsingContext(
      win.browsingContext
    );

    this.#browsers[winId] = context;
    this.#curBrowser = this.#browsers[winId];
  }

  /**
   * Assert that a credential with the given id is stored in the virtual
   * authenticator identified by authenticatorId.
   *
   * @param {string} authenticatorId
   *     The ID of the virtual authenticator to look up. It must refer to an
   *     existing authenticator.
   * @param {string} credentialId
   *     The ID of the credential to look up.
   *
   * @throws {InvalidArgumentError}
   *     If credentialId is not a string, or no credential with the given id is
   *     stored in the authenticator.
   */
  #assertCredential(authenticatorId, credentialId) {
    lazy.assert.string(
      credentialId,
      lazy.pprint`Expected "credentialId" to be a string, got ${credentialId}`
    );

    const credentials = lazy.webauthn.getCredentials(authenticatorId);
    if (
      !credentials.some(credential => credential.credentialId === credentialId)
    ) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`No credential found with id ${credentialId}`
      );
    }
  }

  /**
   * Assert that the given id refers to a virtual authenticator stored in the
   * Virtual Authenticator Database.
   *
   * @param {string} authenticatorId
   *     The ID of the virtual authenticator to look up.
   *
   * @throws {InvalidArgumentError}
   *     If authenticatorId is not a string, or no virtual authenticator with
   *     the given id is stored in the Virtual Authenticator Database.
   */
  #assertVirtualAuthenticator(authenticatorId) {
    lazy.assert.string(
      authenticatorId,
      lazy.pprint`Expected "authenticatorId" to be a string, got ${authenticatorId}`
    );

    if (!lazy.webauthn.hasVirtualAuthenticator(authenticatorId)) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`No virtual authenticator found with id ${authenticatorId}`
      );
    }
  }

  #checkIfAlertIsPresent() {
    if (!this.#dialog || !this.#dialog.isOpen) {
      throw new lazy.error.NoSuchAlertError();
    }
  }

  async #execute(
    script,
    args = [],
    {
      sandboxName = null,
      newSandbox = false,
      file = "",
      line = 0,
      async = false,
    } = {}
  ) {
    lazy.assert.open(this.getBrowsingContext());
    await this.#handleUserPrompts();

    lazy.assert.string(
      script,
      lazy.pprint`Expected "script" to be a string, got ${script}`
    );
    lazy.assert.array(
      args,
      lazy.pprint`Expected "args" to be an array, got ${args}`
    );
    if (sandboxName !== null) {
      lazy.assert.string(
        sandboxName,
        lazy.pprint`Expected "sandboxName" to be a string, got ${sandboxName}`
      );
    }
    lazy.assert.boolean(
      newSandbox,
      lazy.pprint`Expected "newSandbox" to be boolean, got ${newSandbox}`
    );
    lazy.assert.string(
      file,
      lazy.pprint`Expected "file" to be a string, got ${file}`
    );
    lazy.assert.number(
      line,
      lazy.pprint`Expected "line" to be a number, got ${line}`
    );

    let opts = {
      timeout: this.currentSession.timeouts.script,
      sandboxName,
      newSandbox,
      file,
      line,
      async,
    };

    // Script evaluation against privileged contexts should only be allowed
    // if allowSystemAccess is true.
    const context = this.getBrowsingContext();
    if (
      !lazy.RemoteAgent.allowSystemAccess &&
      lazy.isPrivilegedContext(context)
    ) {
      throw new lazy.error.UnsupportedOperationError(
        `ExecuteScript and ExecuteAsyncScript are not supported for privileged browsing contexts: ${context.id}`
      );
    }

    return this.#getActor().executeScript(script, args, opts);
  }

  /**
   * Find a specific window matching the provided window handle.
   *
   * @param {string} handle
   *     The unique handle of either a chrome window or a content browser, as
   *     returned by :js:func:`#getIdForBrowser` or :js:func:`#getIdForWindow`.
   *
   * @returns {object|null}
   *     A window properties object, or `null` if a window cannot be found.
   *.    @see :js:func:`WindowManager#getWindowProperties`
   */
  #findWindowByHandle(handle) {
    for (const win of lazy.windowManager.windows) {
      const chromeWindowId = lazy.NavigableManager.getIdForBrowsingContext(
        win.browsingContext
      );
      if (chromeWindowId == handle) {
        return this.getWindowProperties(win);
      }

      // Otherwise check if the chrome window has a tab browser, and that it
      // contains a tab with the wanted window handle.
      const tabBrowser = lazy.TabManager.getTabBrowser(win);
      if (tabBrowser && tabBrowser.tabs) {
        for (let i = 0; i < tabBrowser.tabs.length; ++i) {
          let contentBrowser = lazy.TabManager.getBrowserForTab(
            tabBrowser.tabs[i]
          );
          let contentWindowId =
            lazy.NavigableManager.getIdForBrowser(contentBrowser);

          if (contentWindowId == handle) {
            return this.getWindowProperties(win, { tabIndex: i });
          }
        }
      }
    }

    return null;
  }

  /**
   * Get the current "MarionetteCommands" parent actor.
   *
   * @param {object} options
   * @param {boolean=} options.top
   *     If set to true use the window's top-level browsing context for the actor,
   *     otherwise the one from the currently selected frame. Defaults to false.
   *
   * @returns {MarionetteCommandsParent}
   *     The parent actor.
   */
  #getActor(options = {}) {
    return lazy.getMarionetteCommandsActorProxy(() =>
      this.getBrowsingContext(options)
    );
  }

  /**
   * Callback used to observe the closing of modal dialogs
   * during the session's lifetime.
   */
  #handleClosedModalDialog(_eventName, data) {
    const { contentBrowser, detail } = data;

    this.#trace(
      `Prompt closed (type: "${detail.promptType}", accepted: "${detail.accepted}")`,
      contentBrowser.browsingContext
    );

    this.#dialog = null;
  }

  #handleEvent = ({ target, type }) => {
    switch (type) {
      case "XULFrameLoaderCreated":
        if (target === this.#curBrowser.contentBrowser) {
          lazy.logger.trace(
            "Remoteness change detected. Set new top-level browsing context " +
              `to ${target.browsingContext.id}`
          );

          this.currentSession.contentBrowsingContext = target.browsingContext;
        }
        break;
    }
  };

  /**
   * Callback used to observe the creation of new modal dialogs
   * during the session's lifetime.
   */
  #handleOpenModalDialog(_eventName, data) {
    const { contentBrowser, prompt } = data;

    prompt.getText().then(text => {
      // We need the text to identify a user prompt when it gets
      // randomly opened. Because on Android the text is asynchronously
      // retrieved lets delay the logging without making the handler async.
      this.#trace(
        `Prompt opened (type: "${prompt.promptType}", text: "${text}")`,
        contentBrowser.browsingContext
      );
    });

    this.#dialog = prompt;

    if (
      this.#dialog.promptType === "beforeunload" &&
      !this.currentSession?.bidi
    ) {
      // Only implicitly accept the prompt when its not a BiDi session.
      this.#trace(`Implicitly accepted "beforeunload" prompt`);
      this.#dialog.accept();
      return;
    }

    if (!this.#isShuttingDown) {
      this.#getActor().notifyDialogOpened(this.#dialog);
    }
  }

  async #handleUserPrompts() {
    if (!this.#dialog || !this.#dialog.isOpen) {
      return;
    }

    const promptType = this.#dialog.promptType;
    const textContent = await this.#dialog.getText();

    if (promptType === "beforeunload" && !this.currentSession.bidi) {
      // In an HTTP-only session, this prompt will be automatically accepted.
      // Since this occurs asynchronously, we need to wait until it closes
      // to prevent race conditions, particularly in slow builds.
      await lazy.PollPromise((resolve, reject) => {
        this.#dialog?.isOpen ? reject() : resolve();
      });
      return;
    }

    let type = lazy.PromptTypes.Default;
    switch (promptType) {
      case "alert":
        type = lazy.PromptTypes.Alert;
        break;
      case "beforeunload":
        type = lazy.PromptTypes.BeforeUnload;
        break;
      case "confirm":
        type = lazy.PromptTypes.Confirm;
        break;
      case "prompt":
        type = lazy.PromptTypes.Prompt;
        break;
    }

    const userPromptHandler = this.currentSession.userPromptHandler;
    const handlerConfig = userPromptHandler.getPromptHandler(type);

    switch (handlerConfig.handler) {
      case lazy.PromptHandlers.Accept:
        await this.acceptAlert();
        break;
      case lazy.PromptHandlers.Dismiss:
        await this.dismissAlert();
        break;
      case lazy.PromptHandlers.Ignore:
        break;
    }

    if (handlerConfig.notify) {
      throw new lazy.error.UnexpectedAlertOpenError(
        `Unexpected ${promptType} dialog detected. Performed handler "${handlerConfig.handler}"`,
        {
          text: textContent,
        }
      );
    }
  }

  /**
   * Handles registration of new content browsers.  Depending on
   * their type they are either accepted or ignored.
   *
   * @param {XULBrowser} browserElement
   */
  #registerBrowser(browserElement) {
    // We want to ignore frames that are XUL browsers that aren't in the "main"
    // tabbrowser, but accept things on Fennec (which doesn't have a
    // xul:tabbrowser), and accept HTML iframes (because tests depend on it),
    // as well as XUL frames. Ideally this should be cleaned up and we should
    // keep track of browsers a different way.
    if (
      !lazy.AppInfo.isFirefox ||
      browserElement.namespaceURI != XUL_NS ||
      browserElement.nodeName != "browser" ||
      browserElement.getTabBrowser()
    ) {
      this.#curBrowser.register(browserElement);
    }
  }

  /**
   * Start observing the specified window.
   *
   * @param {ChromeWindow} win
   *     Chrome window to register event listeners for.
   * @param {object=} options
   * @param {boolean=} options.registerBrowsers
   *     If true, register all content browsers of found tabs. Defaults to false.
   */
  #registerWindow(win, options = {}) {
    const { registerBrowsers = false } = options;
    const tabBrowser = lazy.TabManager.getTabBrowser(win);

    if (registerBrowsers && tabBrowser) {
      for (const tab of tabBrowser.tabs) {
        const contentBrowser = lazy.TabManager.getBrowserForTab(tab);
        this.#registerBrowser(contentBrowser);
      }
    }

    // Listen for any kind of top-level process switch
    tabBrowser?.addEventListener("XULFrameLoaderCreated", this.#handleEvent);
  }

  /**
   * Stop observing the specified window.
   *
   * @param {ChromeWindow} win
   *     Chrome window to unregister event listeners for.
   */
  #stopObservingWindow(win) {
    const tabBrowser = lazy.TabManager.getTabBrowser(win);

    tabBrowser?.removeEventListener("XULFrameLoaderCreated", this.#handleEvent);
  }

  #trace(message, browsingContext = null) {
    if (browsingContext !== null) {
      lazy.logger.trace(`[${browsingContext.id}] ${message}`);
    } else {
      lazy.logger.trace(message);
    }
  }

  static #commandHandlers = {
    // Custom commands for addon support
    "Addon:Install": GeckoDriver.prototype.installAddon,
    "Addon:Uninstall": GeckoDriver.prototype.uninstallAddon,

    // Custom commands for localization
    "L10n:LocalizeProperty": GeckoDriver.prototype.localizeProperty,

    // Custom commands for Marionette
    "Marionette:AcceptConnections": GeckoDriver.prototype.acceptConnections,
    "Marionette:GetAccessibilityPropertiesForAccessibilityNode":
      GeckoDriver.prototype.getAccessibilityPropertiesForAccessibilityNode,
    "Marionette:GetAccessibilityPropertiesForElement":
      GeckoDriver.prototype.getAccessibilityPropertiesForElement,
    "Marionette:GetContext": GeckoDriver.prototype.getContext,
    "Marionette:GetScreenOrientation":
      GeckoDriver.prototype.getScreenOrientation,
    "Marionette:GetWindowType": GeckoDriver.prototype.getWindowType,
    "Marionette:Quit": GeckoDriver.prototype.quit,
    "Marionette:RegisterChromeHandler":
      GeckoDriver.prototype.registerChromeHandler,
    "Marionette:SetContext": GeckoDriver.prototype.setContext,
    "Marionette:SetScreenOrientation":
      GeckoDriver.prototype.setScreenOrientation,
    "Marionette:UnregisterChromeHandler":
      GeckoDriver.prototype.unregisterChromeHandler,

    // Custom commands for reftests
    "reftest:run": GeckoDriver.prototype.runReftest,
    "reftest:setup": GeckoDriver.prototype.setupReftest,
    "reftest:teardown": GeckoDriver.prototype.teardownReftest,

    // Commands for WebDriver classic
    "WebDriver:AcceptAlert": GeckoDriver.prototype.acceptAlert,
    "WebDriver:AddCookie": GeckoDriver.prototype.addCookie,
    "WebDriver:Back": GeckoDriver.prototype.goBack,
    "WebDriver:CloseChromeWindow": GeckoDriver.prototype.closeChromeWindow,
    "WebDriver:CloseWindow": GeckoDriver.prototype.close,
    "WebDriver:DeleteAllCookies": GeckoDriver.prototype.deleteAllCookies,
    "WebDriver:DeleteCookie": GeckoDriver.prototype.deleteCookie,
    "WebDriver:DeleteSession": GeckoDriver.prototype.deleteSession,
    "WebDriver:DismissAlert": GeckoDriver.prototype.dismissAlert,
    "WebDriver:ElementClear": GeckoDriver.prototype.clearElement,
    "WebDriver:ElementClick": GeckoDriver.prototype.clickElement,
    "WebDriver:ElementSendKeys": GeckoDriver.prototype.sendKeysToElement,
    "WebDriver:ExecuteAsyncScript": GeckoDriver.prototype.executeAsyncScript,
    "WebDriver:ExecuteScript": GeckoDriver.prototype.executeScript,
    "WebDriver:FindElement": GeckoDriver.prototype.findElement,
    "WebDriver:FindElementFromShadowRoot":
      GeckoDriver.prototype.findElementFromShadowRoot,
    "WebDriver:FindElements": GeckoDriver.prototype.findElements,
    "WebDriver:FindElementsFromShadowRoot":
      GeckoDriver.prototype.findElementsFromShadowRoot,
    "WebDriver:Forward": GeckoDriver.prototype.goForward,
    "WebDriver:FullscreenWindow": GeckoDriver.prototype.fullscreenWindow,
    "WebDriver:GetActiveElement": GeckoDriver.prototype.getActiveElement,
    "WebDriver:GetAlertText": GeckoDriver.prototype.getAlertText,
    "WebDriver:GetComputedLabel": GeckoDriver.prototype.getComputedLabel,
    "WebDriver:GetComputedRole": GeckoDriver.prototype.getComputedRole,
    "WebDriver:GetCookies": GeckoDriver.prototype.getCookies,
    "WebDriver:GetCurrentURL": GeckoDriver.prototype.getCurrentUrl,
    "WebDriver:GetElementAttribute": GeckoDriver.prototype.getElementAttribute,
    "WebDriver:GetElementCSSValue":
      GeckoDriver.prototype.getElementValueOfCssProperty,
    "WebDriver:GetElementProperty": GeckoDriver.prototype.getElementProperty,
    "WebDriver:GetElementRect": GeckoDriver.prototype.getElementRect,
    "WebDriver:GetElementTagName": GeckoDriver.prototype.getElementTagName,
    "WebDriver:GetElementText": GeckoDriver.prototype.getElementText,
    "WebDriver:GetPageSource": GeckoDriver.prototype.getPageSource,
    "WebDriver:GetShadowRoot": GeckoDriver.prototype.getShadowRoot,
    "WebDriver:GetTimeouts": GeckoDriver.prototype.getTimeouts,
    "WebDriver:GetTitle": GeckoDriver.prototype.getTitle,
    "WebDriver:GetWindowHandle": GeckoDriver.prototype.getWindowHandle,
    "WebDriver:GetWindowHandles": GeckoDriver.prototype.getWindowHandles,
    "WebDriver:GetWindowRect": GeckoDriver.prototype.getWindowRect,
    "WebDriver:IsElementDisplayed": GeckoDriver.prototype.isElementDisplayed,
    "WebDriver:IsElementEnabled": GeckoDriver.prototype.isElementEnabled,
    "WebDriver:IsElementSelected": GeckoDriver.prototype.isElementSelected,
    "WebDriver:MaximizeWindow": GeckoDriver.prototype.maximizeWindow,
    "WebDriver:MinimizeWindow": GeckoDriver.prototype.minimizeWindow,
    "WebDriver:Navigate": GeckoDriver.prototype.navigateTo,
    "WebDriver:NewSession": GeckoDriver.prototype.newSession,
    "WebDriver:NewWindow": GeckoDriver.prototype.newWindow,
    "WebDriver:PerformActions": GeckoDriver.prototype.performActions,
    "WebDriver:Print": GeckoDriver.prototype.print,
    "WebDriver:Refresh": GeckoDriver.prototype.refresh,
    "WebDriver:ReleaseActions": GeckoDriver.prototype.releaseActions,
    "WebDriver:SendAlertText": GeckoDriver.prototype.sendKeysToDialog,
    "WebDriver:SetTimeouts": GeckoDriver.prototype.setTimeouts,
    "WebDriver:SetWindowRect": GeckoDriver.prototype.setWindowRect,
    "WebDriver:SwitchToFrame": GeckoDriver.prototype.switchToFrame,
    "WebDriver:SwitchToParentFrame": GeckoDriver.prototype.switchToParentFrame,
    "WebDriver:SwitchToWindow": GeckoDriver.prototype.switchToWindow,
    "WebDriver:TakeScreenshot": GeckoDriver.prototype.takeScreenshot,

    // External commands for Global Privacy Control
    "GPC:GetGlobalPrivacyControl":
      GeckoDriver.prototype.gpc_getGlobalPrivacyControl,
    "GPC:SetGlobalPrivacyControl":
      GeckoDriver.prototype.gpc_setGlobalPrivacyControl,

    // External commands for Permissions
    // @backward-compat { version 150 } Rename "WebDriver:SetPermission" to
    // "Permissions:SetPermission" in geckodriver after Firefox 150 is no
    // longer supported.
    "WebDriver:SetPermission": GeckoDriver.prototype.permissions_setPermission,
    "Permissions:SetPermission":
      GeckoDriver.prototype.permissions_setPermission,

    // External commands for Reporting API test generation of reports
    "Reporting:GenerateTestReport":
      GeckoDriver.prototype.reporting_generateTestReport,

    // External commands for WebAuthn
    "WebAuthn:AddCredential": GeckoDriver.prototype.webAuthn_addCredential,
    "WebAuthn:AddVirtualAuthenticator":
      GeckoDriver.prototype.webAuthn_addVirtualAuthenticator,
    "WebAuthn:GetCredentials": GeckoDriver.prototype.webAuthn_getCredentials,
    "WebAuthn:RemoveCredential":
      GeckoDriver.prototype.webAuthn_removeCredential,
    "WebAuthn:RemoveAllCredentials":
      GeckoDriver.prototype.webAuthn_removeAllCredentials,
    "WebAuthn:RemoveVirtualAuthenticator":
      GeckoDriver.prototype.webAuthn_removeVirtualAuthenticator,
    "WebAuthn:SetUserVerified": GeckoDriver.prototype.webAuthn_setUserVerified,
  };
}
