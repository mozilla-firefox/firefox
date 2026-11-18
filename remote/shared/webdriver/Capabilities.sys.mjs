/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  browserVersionMatches:
    "chrome://remote/content/shared/webdriver/BrowserVersion.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  truncate: "chrome://remote/content/shared/Format.sys.mjs",
  UserPromptHandler:
    "chrome://remote/content/shared/webdriver/UserPromptHandler.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "userAgent", () => {
  return Cc["@mozilla.org/network/protocol;1?name=http"].getService(
    Ci.nsIHttpProtocolHandler
  ).userAgent;
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "asyncShutdownCrashTimeout",
  "toolkit.asyncshutdown.crash_timeout"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "asyncShutdownCrashAdditionalWait",
  "toolkit.asyncshutdown.crash_timeout_additional_wait"
);

const TIMEOUT_SHUTDOWN_EXTRA_WAIT = 3000;

// List of capabilities which are only relevant for Webdriver Classic.
export const WEBDRIVER_CLASSIC_CAPABILITIES = [
  "pageLoadStrategy",
  "strictFileInteractability",
  "timeouts",
  "webSocketUrl",

  // Gecko specific capabilities
  "moz:accessibilityChecks",
  "moz:firefoxOptions",
  "moz:webdriverClick",

  // Extension capabilities
  "webauthn:extension:credBlob",
  "webauthn:extension:largeBlob",
  "webauthn:extension:prf",
  "webauthn:extension:uvm",
  "webauthn:virtualAuthenticators",
];

/** Representation of WebDriver session timeouts. */
export class Timeouts {
  constructor() {
    // disabled
    this.implicit = 0;
    // five minutes
    this.pageLoad = 300000;
    // 30 seconds
    this.script = 30000;
  }

  toString() {
    return "[object Timeouts]";
  }

  /** Marshals timeout durations to a JSON Object. */
  toJSON() {
    return {
      implicit: this.implicit,
      pageLoad: this.pageLoad,
      script: this.script,
    };
  }

  static fromJSON(json) {
    lazy.assert.object(
      json,
      lazy.pprint`Expected "timeouts" to be an object, got ${json}`
    );
    let t = new Timeouts();

    for (let [type, ms] of Object.entries(json)) {
      const supportedTimeouts = ["implicit", "pageLoad", "script"];
      if (!supportedTimeouts.includes(type)) {
        throw new lazy.error.InvalidArgumentError(
          `Expected type of timeout to be one of "${supportedTimeouts.join(", ")}", ` +
            lazy.pprint`got ${type}`
        );
      }

      if (ms !== null) {
        lazy.assert.positiveInteger(
          ms,
          `Expected "${type}" to be a positive integer, ` +
            lazy.pprint`got ${ms}`
        );
      }

      t[type] = ms;
    }

    return t;
  }
}

/**
 * Enum of page loading strategies.
 *
 * @enum
 */
export const PageLoadStrategy = {
  /** No page load strategy.  Navigation will return immediately. */
  None: "none",
  /**
   * Eager, causing navigation to complete when the document reaches
   * the <code>interactive</code> ready state.
   */
  Eager: "eager",
  /**
   * Normal, causing navigation to return when the document reaches the
   * <code>complete</code> ready state.
   */
  Normal: "normal",
};

/**
 * Enum of proxy types.
 *
 * @enum
 */
export const ProxyTypes = {
  Autodetect: "autodetect",
  Direct: "direct",
  Manual: "manual",
  Pac: "pac",
  System: "system",
};

/** Proxy configuration object representation. */
export class ProxyConfiguration {
  #previousValuesForPreferences;

  /** @class */
  constructor() {
    this.proxyType = null;
    this.httpProxy = null;
    this.httpProxyPort = null;
    this.noProxy = null;
    this.sslProxy = null;
    this.sslProxyPort = null;
    this.socksProxy = null;
    this.socksProxyPort = null;
    this.socksVersion = null;
    this.proxyAutoconfigUrl = null;

    // List of applied preferences to clean up on destroy.
    this.#previousValuesForPreferences = new Set();
  }

  destroy() {
    for (const { type, name, value } of this.#previousValuesForPreferences) {
      if (type === "int") {
        Services.prefs.setIntPref(name, value);
      } else if (type === "string") {
        Services.prefs.setStringPref(name, value);
      }
    }

    this.#previousValuesForPreferences = new Set();
  }

  /**
   * Sets Firefox proxy settings.
   *
   * @returns {boolean}
   *     True if proxy settings were updated as a result of calling this
   *     function, or false indicating that this function acted as
   *     a no-op.
   */
  init() {
    switch (this.proxyType) {
      case ProxyTypes.Autodetect:
        this.#setPreference("network.proxy.type", 4);
        return true;

      case ProxyTypes.Direct:
        this.#setPreference("network.proxy.type", 0);
        return true;

      case ProxyTypes.Manual:
        this.#setPreference("network.proxy.type", 1);

        if (this.httpProxy) {
          this.#setPreference("network.proxy.http", this.httpProxy, "string");
          if (Number.isInteger(this.httpProxyPort)) {
            this.#setPreference("network.proxy.http_port", this.httpProxyPort);
          }
        }

        if (this.sslProxy) {
          this.#setPreference("network.proxy.ssl", this.sslProxy, "string");
          if (Number.isInteger(this.sslProxyPort)) {
            this.#setPreference("network.proxy.ssl_port", this.sslProxyPort);
          }
        }

        if (this.socksProxy) {
          this.#setPreference("network.proxy.socks", this.socksProxy, "string");
          if (Number.isInteger(this.socksProxyPort)) {
            this.#setPreference(
              "network.proxy.socks_port",
              this.socksProxyPort
            );
          }
          if (this.socksVersion) {
            this.#setPreference(
              "network.proxy.socks_version",
              this.socksVersion
            );
          }
        }

        if (this.noProxy) {
          this.#setPreference(
            "network.proxy.no_proxies_on",
            this.noProxy.join(", "),
            "string"
          );
        }
        return true;

      case ProxyTypes.Pac:
        this.#setPreference("network.proxy.type", 2);
        this.#setPreference(
          "network.proxy.autoconfig_url",
          this.proxyAutoconfigUrl,
          "string"
        );
        return true;

      case ProxyTypes.System:
        this.#setPreference("network.proxy.type", 5);
        return true;

      default:
        return false;
    }
  }

  /**
   * @param {Record<string, ?>} json
   *     JSON Object to unmarshal.
   *
   * @throws {InvalidArgumentError}
   *     When proxy configuration is invalid.
   */
  static fromJSON(json) {
    function stripBracketsFromIpv6Hostname(hostname) {
      return hostname.includes(":")
        ? hostname.replace(/[\[\]]/g, "")
        : hostname;
    }

    // Parse hostname and optional port from host
    function fromHost(scheme, host) {
      lazy.assert.string(
        host,
        lazy.pprint`Expected proxy "host" to be a string, got ${host}`
      );

      if (host.includes("://")) {
        throw new lazy.error.InvalidArgumentError(`${host} contains a scheme`);
      }

      // To parse the host a scheme has to be added temporarily.
      // If the returned value for the port is an empty string it
      // could mean no port or the default port for this scheme was
      // specified. In such a case parse again with a different
      // scheme to ensure we filter out the default port.
      let url;
      for (let _url of [`http://${host}`, `https://${host}`]) {
        url = URL.parse(_url);
        if (!url) {
          throw new lazy.error.InvalidArgumentError(
            lazy.truncate`Expected "url" to be a valid URL, got ${_url}`
          );
        }
        if (url.port != "") {
          break;
        }
      }

      if (
        url.username != "" ||
        url.password != "" ||
        url.pathname != "/" ||
        url.search != "" ||
        url.hash != ""
      ) {
        throw new lazy.error.InvalidArgumentError(
          `${host} was not of the form host[:port]`
        );
      }

      let hostname = stripBracketsFromIpv6Hostname(url.hostname);

      // If the port hasn't been set, use the default port of
      // the selected scheme (except for socks which doesn't have one).
      let port = parseInt(url.port);
      if (!Number.isInteger(port)) {
        if (scheme === "socks") {
          port = null;
        } else {
          port = Services.io.getDefaultPort(scheme);
        }
      }

      return [hostname, port];
    }

    let p = new ProxyConfiguration();
    if (typeof json == "undefined" || json === null) {
      return p;
    }

    lazy.assert.object(
      json,
      lazy.pprint`Expected "proxy" to be an object, got ${json}`
    );

    lazy.assert.in(
      "proxyType",
      json,
      lazy.pprint`Expected "proxyType" in "proxy" object, got ${json}`
    );
    p.proxyType = lazy.assert.string(
      json.proxyType,
      lazy.pprint`Expected "proxyType" to be a string, got ${json.proxyType}`
    );

    switch (p.proxyType) {
      case "autodetect":
      case "direct":
      case "system":
        break;

      case "pac":
        p.proxyAutoconfigUrl = lazy.assert.string(
          json.proxyAutoconfigUrl,
          `Expected "proxyAutoconfigUrl" to be a string, ` +
            lazy.pprint`got ${json.proxyAutoconfigUrl}`
        );
        break;

      case "manual":
        if (typeof json.httpProxy != "undefined") {
          [p.httpProxy, p.httpProxyPort] = fromHost("http", json.httpProxy);
        }
        if (typeof json.sslProxy != "undefined") {
          [p.sslProxy, p.sslProxyPort] = fromHost("https", json.sslProxy);
        }
        if (typeof json.socksProxy != "undefined") {
          [p.socksProxy, p.socksProxyPort] = fromHost("socks", json.socksProxy);
          p.socksVersion = lazy.assert.positiveInteger(
            json.socksVersion,
            lazy.pprint`Expected "socksVersion" to be a positive integer, got ${json.socksVersion}`
          );
        }
        if (
          typeof json.socksVersion != "undefined" &&
          typeof json.socksProxy == "undefined"
        ) {
          throw new lazy.error.InvalidArgumentError(
            `Expected "socksProxy" to be provided if "socksVersion" is provided, got ${json.socksProxy}`
          );
        }
        if (typeof json.noProxy != "undefined") {
          let entries = lazy.assert.array(
            json.noProxy,
            lazy.pprint`Expected "noProxy" to be an array, got ${json.noProxy}`
          );
          p.noProxy = entries.map(entry => {
            lazy.assert.string(
              entry,
              lazy.pprint`Expected "noProxy" entry to be a string, got ${entry}`
            );
            return stripBracketsFromIpv6Hostname(entry);
          });
        }
        break;

      default:
        throw new lazy.error.InvalidArgumentError(
          `Invalid type of proxy: ${p.proxyType}`
        );
    }

    return p;
  }

  /**
   * @returns {Record<string, (number | string)>}
   *     JSON serialisation of proxy object.
   */
  toJSON() {
    function addBracketsToIpv6Hostname(hostname) {
      return hostname.includes(":") ? `[${hostname}]` : hostname;
    }

    function toHost(hostname, port) {
      if (!hostname) {
        return null;
      }

      // Add brackets around IPv6 addresses
      hostname = addBracketsToIpv6Hostname(hostname);

      if (port != null) {
        return `${hostname}:${port}`;
      }

      return hostname;
    }

    let excludes = this.noProxy;
    if (excludes) {
      excludes = excludes.map(addBracketsToIpv6Hostname);
    }

    return marshal({
      proxyType: this.proxyType,
      httpProxy: toHost(this.httpProxy, this.httpProxyPort),
      noProxy: excludes,
      sslProxy: toHost(this.sslProxy, this.sslProxyPort),
      socksProxy: toHost(this.socksProxy, this.socksProxyPort),
      socksVersion: this.socksVersion,
      proxyAutoconfigUrl: this.proxyAutoconfigUrl,
    });
  }

  toString() {
    return "[object Proxy]";
  }

  #setPreference(name, value, type = "int") {
    let prevValue;

    if (type === "int") {
      if (Services.prefs.getPrefType(name) != Services.prefs.PREF_INVALID) {
        prevValue = Services.prefs.getIntPref(name);
      }

      Services.prefs.setIntPref(name, value);
    } else if (type === "string") {
      if (Services.prefs.getPrefType(name) != Services.prefs.PREF_INVALID) {
        prevValue = Services.prefs.getStringPref(name);
      }

      Services.prefs.setStringPref(name, value);
    }

    if (prevValue !== undefined) {
      this.#previousValuesForPreferences.add({ name, type, value: prevValue });
    }
  }
}

export class Capabilities extends Map {
  #isBidi;

  /**
   * WebDriver session capabilities representation.
   *
   * @param {boolean} isBidi
   *     Flag indicating that it is a WebDriver BiDi session. Defaults to false.
   */
  constructor(isBidi = false) {
    // Default values for capabilities supported by both WebDriver protocols
    const defaults = [
      ["acceptInsecureCerts", false],
      ["browserName", getWebDriverBrowserName()],
      ["browserVersion", lazy.AppInfo.version],
      ["platformName", getWebDriverPlatformName()],
      ["proxy", new ProxyConfiguration()],
      ["setWindowRect", !lazy.AppInfo.isAndroid],
      ["unhandledPromptBehavior", new lazy.UserPromptHandler()],
      ["userAgent", lazy.userAgent],

      // Gecko specific capabilities
      ["moz:buildID", lazy.AppInfo.appBuildID],
      ["moz:headless", lazy.AppInfo.isHeadless],
      ["moz:platformVersion", Services.sysinfo.getProperty("version")],
      ["moz:processID", lazy.AppInfo.processID],
      ["moz:profile", maybeProfile()],
      ["moz:shutdownTimeout", getShutdownTimeout()],
    ];

    if (!isBidi) {
      // HTTP-only capabilities
      defaults.push(
        ["pageLoadStrategy", PageLoadStrategy.Normal],
        ["timeouts", new Timeouts()],
        ["strictFileInteractability", false],

        // Gecko specific capabilities
        ["moz:accessibilityChecks", false],
        ["moz:webdriverClick", true],
        ["moz:windowless", false]
      );
    }

    super(defaults);

    this.#isBidi = isBidi;
  }

  /**
   * @param {string} key
   *     Capability key.
   * @param {(string|number|boolean)} value
   *     JSON-safe capability value.
   */
  set(key, value) {
    if (key === "timeouts" && !(value instanceof Timeouts)) {
      throw new TypeError();
    } else if (key === "proxy" && !(value instanceof ProxyConfiguration)) {
      throw new TypeError();
    }

    return super.set(key, value);
  }

  toString() {
    return "[object Capabilities]";
  }

  /**
   * JSON serialization of capabilities object.
   *
   * @returns {Record<string, ?>}
   */
  toJSON() {
    let marshalled = marshal(this);

    if (!this.#isBidi) {
      // For classic WebDriver, proxy is always returned at the moment even when not configured.
      // See as well: https://github.com/w3c/webdriver/issues/1813
      if (!("proxy" in marshalled)) {
        marshalled.proxy = {};
      }

      // Don't add timeouts which is a WebDriver classic specific capability.
      marshalled.timeouts = super.get("timeouts");
    }

    marshalled.unhandledPromptBehavior = super.get("unhandledPromptBehavior");

    return marshalled;
  }

  /**
   * Unmarshal a JSON object representation of WebDriver capabilities.
   *
   * @param {Record<string, *>=} json
   *     WebDriver capabilities.
   * @param {boolean=} isBidi
   *     Flag indicating that it is a WebDriver BiDi session. Defaults to false.
   *
   * @returns {Capabilities}
   *     Internal representation of WebDriver capabilities.
   */
  static fromJSON(json, isBidi = false) {
    if (typeof json == "undefined" || json === null) {
      json = {};
    }
    lazy.assert.object(
      json,
      lazy.pprint`Expected "capabilities" to be an object, got ${json}"`
    );

    const capabilities = new Capabilities(isBidi);

    // TODO: Bug 1823907. We can start using here spec compliant method `validate`,
    // as soon as `desiredCapabilities` and `requiredCapabilities` are not supported.
    for (let [k, v] of Object.entries(json)) {
      if (isBidi && WEBDRIVER_CLASSIC_CAPABILITIES.includes(k)) {
        // Ignore any WebDriver classic capability for a WebDriver BiDi session.
        continue;
      }

      switch (k) {
        case "acceptInsecureCerts":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "pageLoadStrategy":
          lazy.assert.string(
            v,
            `Expected "${k}" to be a string, ` + lazy.pprint`got ${v}`
          );
          if (!Object.values(PageLoadStrategy).includes(v)) {
            throw new lazy.error.InvalidArgumentError(
              "Unknown page load strategy: " + v
            );
          }
          break;

        case "proxy":
          v = ProxyConfiguration.fromJSON(v);
          break;

        case "setWindowRect":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          if (!lazy.AppInfo.isAndroid && !v) {
            throw new lazy.error.InvalidArgumentError(
              "setWindowRect cannot be disabled"
            );
          } else if (lazy.AppInfo.isAndroid && v) {
            throw new lazy.error.InvalidArgumentError(
              "setWindowRect is only supported on desktop"
            );
          }
          break;

        case "timeouts":
          v = Timeouts.fromJSON(v);
          break;

        case "strictFileInteractability":
          v = lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "unhandledPromptBehavior":
          v = lazy.UserPromptHandler.fromJSON(v);
          break;

        case "webSocketUrl":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );

          if (!v) {
            throw new lazy.error.InvalidArgumentError(
              `Expected "${k}" to be true, ` + lazy.pprint`got ${v}`
            );
          }
          break;

        case "webauthn:virtualAuthenticators":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "webauthn:extension:uvm":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "webauthn:extension:prf":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "webauthn:extension:largeBlob":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "webauthn:extension:credBlob":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "moz:accessibilityChecks":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "moz:webdriverClick":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );
          break;

        case "moz:windowless":
          lazy.assert.boolean(
            v,
            `Expected "${k}" to be a boolean, ` + lazy.pprint`got ${v}`
          );

          // Only supported on MacOS
          if (v && !lazy.AppInfo.isMac) {
            throw new lazy.error.InvalidArgumentError(
              "moz:windowless only supported on MacOS"
            );
          }
          break;

        case "moz:userContext":
          throw new lazy.error.InvalidArgumentError(
            `"${k}" is a readonly capability and cannot be requested`
          );
      }
      capabilities.set(k, v);
    }

    return capabilities;
  }

  /**
   * Validate WebDriver capability.
   *
   * @param {string} name
   *    The name of capability.
   * @param {string} value
   *    The value of capability.
   *
   * @throws {InvalidArgumentError}
   *   If <var>value</var> doesn't pass validation,
   *   which depends on <var>name</var>.
   *
   * @returns {string}
   *     The validated capability value.
   */
  static validate(name, value) {
    if (value === null) {
      return value;
    }
    switch (name) {
      case "acceptInsecureCerts":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );
        return value;

      case "browserName":
      case "browserVersion":
      case "platformName":
        return lazy.assert.string(
          value,
          `Expected "${name}" to be a string, ` + lazy.pprint`got ${value}`
        );

      case "pageLoadStrategy":
        lazy.assert.string(
          value,
          `Expected "${name}" to be a string, ` + lazy.pprint`got ${value}`
        );
        if (!Object.values(PageLoadStrategy).includes(value)) {
          throw new lazy.error.InvalidArgumentError(
            "Unknown page load strategy: " + value
          );
        }
        return value;

      case "proxy":
        return ProxyConfiguration.fromJSON(value);

      case "strictFileInteractability":
        return lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );

      case "timeouts":
        return Timeouts.fromJSON(value);

      case "unhandledPromptBehavior":
        return lazy.UserPromptHandler.fromJSON(value);

      case "webSocketUrl":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );

        if (!value) {
          throw new lazy.error.InvalidArgumentError(
            `Expected "${name}" to be true, ` + lazy.pprint`got ${value}`
          );
        }
        return value;

      case "webauthn:virtualAuthenticators":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );
        return value;

      case "webauthn:extension:uvm":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );
        return value;

      case "webauthn:extension:largeBlob":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );
        return value;

      case "moz:firefoxOptions":
        return lazy.assert.object(
          value,
          `Expected "${name}" to be an object, ` + lazy.pprint`got ${value}`
        );

      case "moz:accessibilityChecks":
        return lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );

      case "moz:webdriverClick":
        return lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );

      case "moz:windowless":
        lazy.assert.boolean(
          value,
          `Expected "${name}" to be a boolean, ` + lazy.pprint`got ${value}`
        );

        // Only supported on MacOS
        if (value && !lazy.AppInfo.isMac) {
          throw new lazy.error.InvalidArgumentError(
            "moz:windowless only supported on MacOS"
          );
        }
        return value;

      case "moz:userContext":
        throw new lazy.error.InvalidArgumentError(
          `"${name}" is a readonly capability and cannot be requested`
        );

      default:
        lazy.assert.string(
          name,
          `Expected capability "name" to be a string, ` +
            lazy.pprint`got ${name}`
        );
        if (name.includes(":")) {
          const [prefix] = name.split(":");
          if (prefix !== "moz") {
            return value;
          }
        }
        throw new lazy.error.InvalidArgumentError(
          `${name} is not the name of a known capability or extension capability`
        );
    }
  }
}

function getShutdownTimeout() {
  // Clients should wait at least this long before forcefully terminating
  // Firefox. This gives the shutdown hang monitor enough time to detect
  // and terminate the process in case of a shutdown hang.
  return (
    lazy.asyncShutdownCrashTimeout +
    lazy.asyncShutdownCrashAdditionalWait +
    TIMEOUT_SHUTDOWN_EXTRA_WAIT
  );
}

function getWebDriverBrowserName() {
  // Similar to chromedriver which reports "chrome" as browser name for all
  // WebView apps, we will report "firefox" for all GeckoView apps.
  if (lazy.AppInfo.isAndroid) {
    return "firefox";
  }

  return lazy.AppInfo.name?.toLowerCase();
}

function getWebDriverPlatformName() {
  let name = Services.sysinfo.getProperty("name");

  if (lazy.AppInfo.isAndroid) {
    return "android";
  }

  switch (name) {
    case "Windows_NT":
      return "windows";

    case "Darwin":
      return "mac";

    default:
      return name.toLowerCase();
  }
}

// Specialisation of |JSON.stringify| that produces JSON-safe object
// literals, dropping empty objects and entries which values are undefined
// or null. Objects are allowed to produce their own JSON representations
// by implementing a |toJSON| function.
function marshal(obj) {
  let rv = Object.create(null);

  function* iter(mapOrObject) {
    if (mapOrObject instanceof Map) {
      for (const [k, v] of mapOrObject) {
        yield [k, v];
      }
    } else {
      for (const k of Object.keys(mapOrObject)) {
        yield [k, mapOrObject[k]];
      }
    }
  }

  for (let [k, v] of iter(obj)) {
    // Skip empty values when serialising to JSON.
    if (typeof v == "undefined" || v === null) {
      continue;
    }

    // Recursively marshal objects that are able to produce their own
    // JSON representation.
    if (typeof v.toJSON == "function") {
      v = marshal(v.toJSON());

      // Or do the same for object literals.
    } else if (isObject(v)) {
      v = marshal(v);
    }

    // And finally drop (possibly marshaled) objects which have no
    // entries.
    if (!isObjectEmpty(v)) {
      rv[k] = v;
    }
  }

  return rv;
}

function isObject(obj) {
  return Object.prototype.toString.call(obj) == "[object Object]";
}

function isObjectEmpty(obj) {
  return isObject(obj) && Object.keys(obj).length === 0;
}

// Services.dirsvc is not accessible from JSWindowActor child,
// but we should not panic about that.
function maybeProfile() {
  try {
    return Services.dirsvc.get("ProfD", Ci.nsIFile).path;
  } catch (e) {
    return "<protected>";
  }
}

/**
 * Merge WebDriver capabilities.
 *
 * @see https://w3c.github.io/webdriver/#dfn-merging-capabilities
 *
 * @param {object} primary
 *     Required capabilities which need to be merged with <var>secondary</var>.
 * @param {object=} secondary
 *     Secondary capabilities.
 *
 * @returns {object} Merged capabilities.
 *
 * @throws {InvalidArgumentError}
 *     If <var>primary</var> and <var>secondary</var> have the same keys.
 */
export function mergeCapabilities(primary, secondary) {
  const result = { ...primary };

  if (secondary === undefined) {
    return result;
  }

  Object.entries(secondary).forEach(([name, value]) => {
    if (primary[name] !== undefined) {
      // Since at the moment we always pass as `primary` `alwaysMatch` object
      // and as `secondary` an item from `firstMatch` array from `capabilities`,
      // we can make this error message more specific.
      throw new lazy.error.InvalidArgumentError(
        `firstMatch key ${name} shadowed a value in alwaysMatch`
      );
    }
    result[name] = value;
  });

  return result;
}

/**
 * Validate WebDriver capabilities.
 *
 * @see https://w3c.github.io/webdriver/#dfn-validate-capabilities
 *
 * @param {object} capabilities
 *     Capabilities which need to be validated.
 *
 * @returns {object} Validated capabilities.
 *
 * @throws {InvalidArgumentError}
 *     If <var>capabilities</var> is not an object.
 */
export function validateCapabilities(capabilities) {
  lazy.assert.object(
    capabilities,
    lazy.pprint`Expected "capabilities" to be an object, got ${capabilities}`
  );

  const result = {};

  Object.entries(capabilities).forEach(([name, value]) => {
    const deserialized = Capabilities.validate(name, value);
    if (deserialized !== null) {
      if (["proxy", "timeouts", "unhandledPromptBehavior"].includes(name)) {
        // Return pure values for objects that will be setup during session creation.
        result[name] = value;
      } else {
        result[name] = deserialized;
      }
    }
  });

  return result;
}

/**
 * Process WebDriver capabilities.
 *
 * @see https://w3c.github.io/webdriver/#processing-capabilities
 *
 * @param {object} params
 * @param {object} params.capabilities
 *     Capabilities which need to be processed.
 *
 * @returns {object|null} Processed capabilities, or null if none match.
 *
 * @throws {InvalidArgumentError}
 *     If <var>capabilities</var> do not satisfy the criteria.
 */
export function processCapabilities(params) {
  const { capabilities } = params;
  lazy.assert.object(
    capabilities,
    lazy.pprint`Expected "capabilities" to be an object, got ${capabilities}`
  );

  let {
    alwaysMatch: requiredCapabilities = {},
    firstMatch: allFirstMatchCapabilities = [{}],
  } = capabilities;

  requiredCapabilities = validateCapabilities(requiredCapabilities);

  lazy.assert.isNonEmptyArray(
    allFirstMatchCapabilities,
    lazy.pprint`Expected "firstMatch" to be a non-empty array, got ${allFirstMatchCapabilities}`
  );

  const validatedFirstMatchCapabilities =
    allFirstMatchCapabilities.map(validateCapabilities);

  const mergedCapabilities = [];
  validatedFirstMatchCapabilities.forEach(firstMatchCapabilities => {
    const merged = mergeCapabilities(
      requiredCapabilities,
      firstMatchCapabilities
    );
    mergedCapabilities.push(merged);
  });

  const browserName = getWebDriverBrowserName();
  const platformName = getWebDriverPlatformName();
  const browserVersion = lazy.AppInfo.version;
  for (const candidate of mergedCapabilities) {
    if (
      ("browserName" in candidate && candidate.browserName !== browserName) ||
      ("platformName" in candidate && candidate.platformName !== platformName)
    ) {
      continue;
    }

    if ("browserVersion" in candidate) {
      if (
        !lazy.browserVersionMatches(browserVersion, candidate.browserVersion)
      ) {
        continue;
      }
      candidate.browserVersion = browserVersion;
    }

    // TODO Bug 1862018. Reconsider when supporting multiple sessions.
    // With only one session allowed, no existing prompt handler can conflict
    // with a validated unhandledPromptBehavior capability.
    return candidate;
  }
  return null;
}
