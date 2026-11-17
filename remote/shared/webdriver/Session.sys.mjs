/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",

  accessibility:
    "chrome://remote/content/shared/webdriver/Accessibility.sys.mjs",
  Capabilities: "chrome://remote/content/shared/webdriver/Capabilities.sys.mjs",
  Certificates: "chrome://remote/content/shared/webdriver/Certificates.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  FilePickerHandler:
    "chrome://remote/content/shared/webdriver/FilePickerHandler.sys.mjs",
  generateUUID: "chrome://remote/content/shared/UUID.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  NavigableManager: "chrome://remote/content/shared/NavigableManager.sys.mjs",
  registerProcessDataActor:
    "chrome://remote/content/shared/webdriver/process-actors/WebDriverProcessDataParent.sys.mjs",
  RootMessageHandler:
    "chrome://remote/content/shared/messagehandler/RootMessageHandler.sys.mjs",
  RootMessageHandlerRegistry:
    "chrome://remote/content/shared/messagehandler/RootMessageHandlerRegistry.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
  unregisterProcessDataActor:
    "chrome://remote/content/shared/webdriver/process-actors/WebDriverProcessDataParent.sys.mjs",
  UserContextManager:
    "chrome://remote/content/shared/UserContextManager.sys.mjs",
  WebDriverBiDiConnection:
    "chrome://remote/content/webdriver-bidi/WebDriverBiDiConnection.sys.mjs",
  WebSocketHandshake:
    "chrome://remote/content/server/WebSocketHandshake.sys.mjs",
  windowManager: "chrome://remote/content/shared/WindowManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () => lazy.Log.get());

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "aomStartup",
  "@mozilla.org/addons/addon-manager-startup;1",
  Ci.amIAddonManagerStartup
);

// Hidden pref, for tests only. Use the dedicated container for all sessions,
// including the ones created for browser automation.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "forceContainer",
  "remote.experimental.dynamicstart.force-container",
  false
);

// Global singleton that holds active WebDriver sessions
const webDriverSessions = new Map();

// Global singleton that maps the id of the MessageHandler network of a session
// to the WebDriver session itself.
const webDriverSessionsByInternalSessionId = new Map();

// Notification emitted when a session is created or destroyed.
const NOTIFY_WEBDRIVER_SESSION_CHANGED = "webdriver-session-changed";

/**
 * @typedef {Set} SessionConfigurationFlags
 *     A set of flags defining the features of a WebDriver session. It can be
 *     empty or contain entries as listed below. External specifications may
 *     define additional flags, or create sessions without the HTTP flag.
 *
 *     <dl>
 *       <dt><code>"bidi"</code> (string)
 *       <dd>Flag indicating a WebDriver BiDi session.
 *       <dt><code>"http"</code> (string)
 *       <dd>Flag indicating a WebDriver classic (HTTP) session.
 *     </dl>
 */

/**
 * Representation of WebDriver session.
 */
export class WebDriverSession {
  #bidi;
  #capabilities;
  #chromeProtocolHandles;
  #connections;
  #http;
  #id;
  #messageHandler;
  #navigableSeenNodes;
  #path;
  #userContext;

  static SESSION_FLAG_BIDI = "bidi";
  static SESSION_FLAG_HTTP = "http";

  /**
   * Construct a new WebDriver session.
   *
   * It is expected that the caller performs the necessary checks on
   * the requested capabilities to be WebDriver conforming.  The WebDriver
   * service offered by Marionette does not match or negotiate capabilities
   * beyond type- and bounds checks.
   *
   * <h3>Capabilities</h3>
   *
   * <dl>
   *  <dt><code>acceptInsecureCerts</code> (boolean)
   *  <dd>Indicates whether untrusted and self-signed TLS certificates
   *   are implicitly trusted on navigation for the duration of the session.
   *
   *  <dt><code>pageLoadStrategy</code> (string)
   *  <dd>(HTTP only) The page load strategy to use for the current session.  Must be
   *   one of "<tt>none</tt>", "<tt>eager</tt>", and "<tt>normal</tt>".
   *
   *  <dt><code>proxy</code> (Proxy object)
   *  <dd>Defines the proxy configuration.
   *
   *  <dt><code>setWindowRect</code> (boolean)
   *  <dd>(HTTP only) Indicates whether the remote end supports all of the resizing
   *   and repositioning commands.
   *
   *  <dt><code>strictFileInteractability</code> (boolean)
   *  <dd>(HTTP only) Defines the current session’s strict file interactability.
   *
   *  <dt><code>timeouts</code> (Timeouts object)
   *  <dd>(HTTP only) Describes the timeouts imposed on certain session operations.
   *
   *  <dt><code>unhandledPromptBehavior</code> (string)
   *  <dd>Describes the current session’s user prompt handler.  Must be one of
   *   "<tt>accept</tt>", "<tt>accept and notify</tt>", "<tt>dismiss</tt>",
   *   "<tt>dismiss and notify</tt>", and "<tt>ignore</tt>".  Defaults to the
   *   "<tt>dismiss and notify</tt>" state.
   *
   *  <dt><code>moz:accessibilityChecks</code> (boolean)
   *  <dd>(HTTP only) Run a11y checks when clicking elements.
   *
   *  <dt><code>moz:webdriverClick</code> (boolean)
   *  <dd>(HTTP only) Use a WebDriver conforming <i>WebDriver::ElementClick</i>.
   *
   *  <dt><code>moz:userContext</code> (string)
   *  <dd>Returned only for sessions which are restricted to a user context.
   *   The user context id the session is restricted to. This capability can
   *   only be returned, it is never accepted as an input capability.
   * </dl>
   *
   * <h4>WebAuthn</h4>
   *
   * <dl>
   *  <dt><code>webauthn:virtualAuthenticators</code> (boolean)
   *  <dd>Indicates whether the endpoint node supports all Virtual
   *   Authenticators commands.
   *
   *  <dt><code>webauthn:extension:uvm</code> (boolean)
   *  <dd>Indicates whether the endpoint node WebAuthn WebDriver
   *   implementation supports the User Verification Method extension.
   *
   *  <dt><code>webauthn:extension:prf</code> (boolean)
   *  <dd>Indicates whether the endpoint node WebAuthn WebDriver
   *   implementation supports the prf extension.
   *
   *  <dt><code>webauthn:extension:largeBlob</code> (boolean)
   *  <dd>Indicates whether the endpoint node WebAuthn WebDriver implementation
   *   supports the largeBlob extension.
   *
   *  <dt><code>webauthn:extension:credBlob</code> (boolean)
   *  <dd>Indicates whether the endpoint node WebAuthn WebDriver implementation
   *   supports the credBlob extension.
   * </dl>
   *
   * <h4>Timeouts object</h4>
   *
   * <dl>
   *  <dt><code>script</code> (number)
   *  <dd>Determines when to interrupt a script that is being evaluates.
   *
   *  <dt><code>pageLoad</code> (number)
   *  <dd>Provides the timeout limit used to interrupt navigation of the
   *   browsing context.
   *
   *  <dt><code>implicit</code> (number)
   *  <dd>Gives the timeout of when to abort when locating an element.
   * </dl>
   *
   * <h4>Proxy object</h4>
   *
   * <dl>
   *  <dt><code>proxyType</code> (string)
   *  <dd>Indicates the type of proxy configuration.  Must be one
   *   of "<tt>pac</tt>", "<tt>direct</tt>", "<tt>autodetect</tt>",
   *   "<tt>system</tt>", or "<tt>manual</tt>".
   *
   *  <dt><code>proxyAutoconfigUrl</code> (string)
   *  <dd>Defines the URL for a proxy auto-config file if
   *   <code>proxyType</code> is equal to "<tt>pac</tt>".
   *
   *  <dt><code>httpProxy</code> (string)
   *  <dd>Defines the proxy host for HTTP traffic when the
   *   <code>proxyType</code> is "<tt>manual</tt>".
   *
   *  <dt><code>noProxy</code> (string)
   *  <dd>Lists the address for which the proxy should be bypassed when
   *   the <code>proxyType</code> is "<tt>manual</tt>".  Must be a JSON
   *   List containing any number of any of domains, IPv4 addresses, or IPv6
   *   addresses.
   *
   *  <dt><code>sslProxy</code> (string)
   *  <dd>Defines the proxy host for encrypted TLS traffic when the
   *   <code>proxyType</code> is "<tt>manual</tt>".
   *
   *  <dt><code>socksProxy</code> (string)
   *  <dd>Defines the proxy host for a SOCKS proxy traffic when the
   *   <code>proxyType</code> is "<tt>manual</tt>".
   *
   *  <dt><code>socksVersion</code> (string)
   *  <dd>Defines the SOCKS proxy version when the <code>proxyType</code> is
   *   "<tt>manual</tt>".  It must be any integer between 0 and 255
   *   inclusive.
   * </dl>
   *
   * <h3>Example</h3>
   *
   * Input:
   *
   * <pre><code>
   *     {"capabilities": {"acceptInsecureCerts": true}}
   * </code></pre>
   *
   * @param {Record<string, *>=} capabilities
   *     JSON Object containing any of the recognized capabilities listed
   *     above.
   * @param {SessionConfigurationFlags} flags
   *     Session configuration flags.
   * @param {object=} options
   * @param {WebDriverBiDiConnection=} options.connection
   *     An optional existing WebDriver BiDi connection to associate with the
   *     new session.
   * @param {boolean=} options.useDedicatedContainer
   *     True if the session should use a dedicated container (typically for
   *     dynamically started servers). Defaults to false.
   *
   * @throws {SessionNotCreatedError}
   *     If, for whatever reason, a session could not be created.
   */
  constructor(capabilities, flags, options = {}) {
    const { connection, useDedicatedContainer = false } = options;

    // List of handles for registered chrome:// URLs
    this.#chromeProtocolHandles = new Map();

    // WebSocket connections that use this session. This also accounts for
    // possible disconnects due to network outages, which require clients
    // to reconnect.
    this.#connections = new Set();

    this.#id = lazy.generateUUID();

    // Flags for WebDriver session features
    this.#bidi = flags.has(WebDriverSession.SESSION_FLAG_BIDI);
    this.#http = flags.has(WebDriverSession.SESSION_FLAG_HTTP);

    if (this.#bidi == this.#http) {
      // Initially a WebDriver session can either be HTTP or BiDi. An upgrade of a
      // HTTP session to offer BiDi features is done after the constructor is run.
      throw new lazy.error.SessionNotCreatedError(
        `Initially the WebDriver session needs to be either HTTP or BiDi (bidi=${
          this.#bidi
        }, http=${this.#http})`
      );
    }

    // Define the HTTP path to query this session via WebDriver BiDi
    this.#path = `/session/${this.#id}`;

    try {
      this.#capabilities = lazy.Capabilities.fromJSON(capabilities, this.#bidi);
    } catch (e) {
      throw new lazy.error.SessionNotCreatedError(e);
    }

    this.#userContext =
      useDedicatedContainer || lazy.forceContainer
        ? getRemoteControlUserContextId()
        : null;
    if (this.#userContext !== null) {
      this.#capabilities.set("moz:userContext", this.#userContext);
      lazy.logger.debug(
        `Session restricted to user context ${this.#userContext}`
      );
    }

    if (this.proxy.init()) {
      lazy.logger.info(
        `Proxy settings initialized: ${JSON.stringify(this.proxy)}`
      );
    }

    if (this.acceptInsecureCerts) {
      lazy.logger.warn(
        "TLS certificate errors will be ignored for this session"
      );
      lazy.Certificates.disableSecurityChecks();
    }

    // If we are testing accessibility with marionette, start a11y service in
    // chrome first. This will ensure that we do not have any content-only
    // services hanging around.
    if (this.a11yChecks && lazy.accessibility.service) {
      lazy.logger.info("Preemptively starting accessibility service in Chrome");
    }

    // If a connection without an associated session has been specified
    // immediately register the newly created session for it.
    if (connection) {
      connection.registerSession(this);
      this.#connections.add(connection);
    }

    // Maps a Navigable (browsing context or content browser for top-level
    // browsing contexts) to a Set of nodeId's.
    this.#navigableSeenNodes = new WeakMap();

    lazy.registerProcessDataActor();

    // Start the tracking of browsing contexts to create Navigable ids.
    lazy.NavigableManager.startTracking();
    lazy.windowManager.startTracking();

    if (this.#shouldDismissFileDialog()) {
      lazy.FilePickerHandler.dismissFilePickers(this);
    }

    webDriverSessions.set(this.#id, this);

    Services.obs.notifyObservers(null, NOTIFY_WEBDRIVER_SESSION_CHANGED);
  }

  destroy() {
    webDriverSessions.delete(this.#id);

    // Stop the tracking of browsing contexts when no WebDriver
    // session exists anymore.
    lazy.NavigableManager.stopTracking();
    lazy.windowManager.stopTracking();

    lazy.unregisterProcessDataActor();

    this.#navigableSeenNodes = null;

    if (this.acceptInsecureCerts) {
      lazy.Certificates.enableSecurityChecks();
    }

    // Close all open connections which unregister themselves.
    this.#connections.forEach(connection => connection.close());
    if (this.#connections.size > 0) {
      lazy.logger.warn(
        `Failed to close ${this.#connections.size} WebSocket connections`
      );
    }

    // For the WebDriver BiDi session cleanup, the root network module is
    // responsible for resuming requests in the blocked request map.
    // See root NetworkModule.destroy().

    // Destroy the dedicated MessageHandler instance if we created one.
    if (this.#messageHandler) {
      webDriverSessionsByInternalSessionId.delete(
        this.#messageHandler.internalSessionId
      );
      this.#messageHandler.off(
        "message-handler-protocol-event",
        this._onMessageHandlerProtocolEvent
      );
      this.#messageHandler.destroy();

      // allowFilePickers(this) is safe to call. If there was no call to
      // dismissFilePickers(this), it will be a no-op.
      // Only needed if BiDi was enabled (and therefore a messageHandler
      // instance was created).
      lazy.FilePickerHandler.allowFilePickers(this);
    }

    for (const id of this.#chromeProtocolHandles.keys()) {
      this.unregisterChromeHandler(id);
    }

    Services.obs.notifyObservers(null, NOTIFY_WEBDRIVER_SESSION_CHANGED);
  }

  get a11yChecks() {
    return this.#capabilities.get("moz:accessibilityChecks");
  }

  get acceptInsecureCerts() {
    return this.#capabilities.get("acceptInsecureCerts");
  }

  get bidi() {
    return this.#bidi;
  }

  set bidi(value) {
    this.#bidi = value;
    if (this.#shouldDismissFileDialog()) {
      lazy.FilePickerHandler.dismissFilePickers(this);
    }
  }

  get capabilities() {
    return this.#capabilities;
  }

  get http() {
    return this.#http;
  }

  get id() {
    return this.#id;
  }

  get messageHandler() {
    if (!this.#messageHandler) {
      // The session id is a secret used by clients to authenticate against the
      // WebDriver endpoints, and as such must never be shared with content
      // processes. Identify the MessageHandler network, which is broadcast to
      // all content processes, with a dedicated id instead.
      const internalSessionId = lazy.generateUUID();

      this.#messageHandler =
        lazy.RootMessageHandlerRegistry.getOrCreateMessageHandler(
          internalSessionId
        );
      webDriverSessionsByInternalSessionId.set(internalSessionId, this);

      this._onMessageHandlerProtocolEvent =
        this._onMessageHandlerProtocolEvent.bind(this);
      this.#messageHandler.on(
        "message-handler-protocol-event",
        this._onMessageHandlerProtocolEvent
      );
    }

    return this.#messageHandler;
  }

  get navigableSeenNodes() {
    return this.#navigableSeenNodes;
  }

  get pageLoadStrategy() {
    return this.#capabilities.get("pageLoadStrategy");
  }

  get path() {
    return this.#path;
  }

  get proxy() {
    return this.#capabilities.get("proxy");
  }

  get strictFileInteractability() {
    return this.#capabilities.get("strictFileInteractability");
  }

  get timeouts() {
    return this.#capabilities.get("timeouts");
  }

  set timeouts(timeouts) {
    this.#capabilities.set("timeouts", timeouts);
  }

  /**
   * The user context id of the container created for this session.
   * WebDriver BiDi commands and events are restricted to this user context
   * when remote.experimental.dynamicstart.container-isolation-enabled is true.
   *
   * @returns {string|null}
   *     The user context id, or null if no user context was created.
   */
  get userContext() {
    return this.#userContext;
  }

  get userPromptHandler() {
    return this.#capabilities.get("unhandledPromptBehavior");
  }

  get webSocketUrl() {
    return this.#capabilities.get("webSocketUrl");
  }

  /**
   * Implements the last steps of https://w3c.github.io/webdriver-bidi/#webdriver-bidi-file-dialog-opened
   *
   * Compared to the spec, this is only invoked to setup the FilePickerHandler and
   * not on each file dialog opened event. This allows to keep the FilePickerHandler
   * simple and simply cancel the picker.
   */
  #shouldDismissFileDialog = () => {
    // Only relevant for active BiDi sessions.
    if (!this.bidi) {
      return false;
    }

    // Unlike other prompt handlers, the default behavior is to allow the file
    // dialog to be opened. Only dismiss if the capability was explicitly set.
    if (this.userPromptHandler.activePromptHandlers === null) {
      return false;
    }

    return this.userPromptHandler.getPromptHandler("file").handler !== "ignore";
  };

  async execute(module, command, params) {
    // XXX: At the moment, commands do not describe consistently their destination,
    // so we will need a translation step based on a specific command and its params
    // in order to extract a destination that can be understood by the MessageHandler.
    //
    // For now, an option is to send all commands to ROOT, and all BiDi MessageHandler
    // modules will therefore need to implement this translation step in the root
    // implementation of their module.
    const destination = {
      type: lazy.RootMessageHandler.type,
    };
    if (!this.messageHandler.supportsCommand(module, command, destination)) {
      throw new lazy.error.UnknownCommandError(`${module}.${command}`);
    }

    return this.messageHandler.handleCommand({
      moduleName: module,
      commandName: command,
      params,
      destination,
    });
  }

  /**
   * Register a chrome protocol handler for a directory containing XHTML or XUL
   * files, allowing them to be loaded via the chrome:// protocol.
   *
   * @param {string} manifestPath
   *     The base manifest path for the entries. URL values are resolved
   *     relative to this path.
   * @param {Array<Array<string, string, string>>} entries
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
   */
  registerChromeHandler(manifestPath, entries) {
    const manifest = new lazy.FileUtils.File(manifestPath);
    const rootURI = Services.io.newFileURI(manifest.parent);
    const manifestURI = Services.io.newURI(manifest.leafName, null, rootURI);

    const handle = lazy.aomStartup.registerChrome(manifestURI, entries);
    const id = lazy.generateUUID();

    this.#chromeProtocolHandles.set(id, handle);

    return id;
  }

  /**
   * Unregister a previously registered chrome protocol handler.
   *
   * @param {string} id
   *     The identifier returned when the chrome protocol handler was registered.
   *
   * @throws {UnknownError}
   *     If there is no such registered chrome protocol handler.
   */
  unregisterChromeHandler(id) {
    if (!this.#chromeProtocolHandles.has(id)) {
      throw new lazy.error.UnknownError(
        `Id ${id} is not a known chrome protocol handler`
      );
    }

    const handle = this.#chromeProtocolHandles.get(id);
    this.#chromeProtocolHandles.delete(id);
    handle.destruct();
  }

  /**
   * Remove the specified WebDriver BiDi connection.
   *
   * @param {WebDriverBiDiConnection} connection
   */
  removeConnection(connection) {
    if (this.#connections.has(connection)) {
      this.#connections.delete(connection);
    } else {
      lazy.logger.warn("Trying to remove a connection that doesn't exist.");
    }
  }

  toString() {
    return `[object ${this.constructor.name} ${this.#id}]`;
  }

  // nsIHttpRequestHandler

  /**
   * Handle new WebSocket connection requests.
   *
   * WebSocket clients will attempt to connect to this session at
   * `/session/:id`.  Hereby a WebSocket upgrade will automatically
   * be performed.
   *
   * @param {Request} request
   *     HTTP request (httpd.js)
   * @param {Response} response
   *     Response to an HTTP request (httpd.js)
   */
  async handle(request, response) {
    const webSocket = await lazy.WebSocketHandshake.upgrade(request, response);
    const conn = new lazy.WebDriverBiDiConnection(
      webSocket,
      response._connection
    );
    conn.registerSession(this);
    this.#connections.add(conn);
  }

  _onMessageHandlerProtocolEvent(eventName, messageHandlerEvent) {
    const { name, data } = messageHandlerEvent;
    this.#connections.forEach(connection => connection.sendEvent(name, data));
  }

  // XPCOM

  QueryInterface = ChromeUtils.generateQI(["nsIHttpRequestHandler"]);
}

const REMOTE_CONTROL_CONTAINER_NAME = "remote-control-container";

/**
 * Retrieve the user context id for the dedicated container created for sessions
 * using dynamically started servers.
 *
 * @returns {string}
 *     The user context id.
 */
function getRemoteControlUserContextId() {
  const existingUserContexts = lazy.UserContextManager.getUserContextIdsByName(
    REMOTE_CONTROL_CONTAINER_NAME
  );

  // If any container already matches the hardcoded REMOTE_CONTROL_CONTAINER_NAME
  // pick the first one, users should be able to reuse the same container across
  // sessions.
  if (existingUserContexts.length) {
    return existingUserContexts[0];
  }

  return lazy.UserContextManager.createContext({
    // Use a color reminiscent of the color applied to the URL bar.
    color: "red",
    icon: "robot",
    name: REMOTE_CONTROL_CONTAINER_NAME,
  });
}

/**
 * Get the list of seen nodes for the given browsing context unique to a
 * WebDriver session.
 *
 * @param {WebDriverSession} session
 *     The WebDriver session to use.
 * @param {BrowsingContext} browsingContext
 *     Browsing context the node is part of.
 *
 * @returns {Set}
 *     The list of seen nodes.
 */
export function getSeenNodesForBrowsingContext(session, browsingContext) {
  if (!lazy.TabManager.isValidCanonicalBrowsingContext(browsingContext)) {
    // If browsingContext is not a valid Browsing Context, return an empty set.
    return new Set();
  }

  const navigable =
    lazy.NavigableManager.getNavigableForBrowsingContext(browsingContext);

  if (!session.navigableSeenNodes.has(navigable)) {
    // The navigable hasn't been seen yet.
    session.navigableSeenNodes.set(navigable, new Set());
  }

  return session.navigableSeenNodes.get(navigable);
}

/**
 *
 * @param {string} internalSessionId
 *     The id of the MessageHandler network of the WebDriver session to
 *     retrieve.
 *
 * @returns {WebDriverSession|undefined}
 *     The WebDriver session or undefined if the id is not known.
 */
export function getWebDriverSessionByInternalSessionId(internalSessionId) {
  return webDriverSessionsByInternalSessionId.get(internalSessionId);
}

/**
 * Check if at least one WebDriver session is currently active, which means that
 * a remote application is connected to this browser instance.
 *
 * @returns {boolean}
 *     True if a WebDriver session is active, false otherwise.
 */
export function hasActiveWebDriverSession() {
  return webDriverSessions.size > 0;
}
