/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "useSeparateFileUriProcess",
  "browser.tabs.remote.separateFileUriProcess",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "useSeparatePrivilegedAboutContentProcess",
  "browser.tabs.remote.separatePrivilegedContentProcess",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "separatePrivilegedMozillaWebContentProcess",
  "browser.tabs.remote.separatePrivilegedMozillaWebContentProcess",
  false
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "separatedMozillaDomains",
  "browser.tabs.remote.separatedMozillaDomains",
  "",
  false,
  val => val.split(",")
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "useCrossOriginOpenerPolicy",
  "browser.tabs.remote.useCrossOriginOpenerPolicy",
  false
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "serializationHelper",
  "@mozilla.org/network/serialization-helper;1",
  Ci.nsISerializationHelper
);
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "extProtService",
  "@mozilla.org/uriloader/external-protocol-service;1",
  Ci.nsIExternalProtocolService
);

function getOriginalReaderModeURI(aURI) {
  try {
    let searchParams = new URLSearchParams(aURI.query);
    if (searchParams.has("url")) {
      return Services.io.newURI(searchParams.get("url"));
    }
  } catch (e) {}
  return null;
}

const NOT_REMOTE = null;

// These must match the similar ones in RemoteTypes.h, ProcInfo.h, ChromeUtils.webidl and ChromeUtils.cpp
const WEB_REMOTE_TYPE = "web";
const FISSION_WEB_REMOTE_TYPE = "webIsolated";
const WEB_REMOTE_COOP_COEP_TYPE_PREFIX = "webCOOP+COEP=";
const FILE_REMOTE_TYPE = "file";
const EXTENSION_REMOTE_TYPE = "extension";
const PRIVILEGEDABOUT_REMOTE_TYPE = "privilegedabout";
const PRIVILEGEDMOZILLA_REMOTE_TYPE = "privilegedmozilla";
const SERVICEWORKER_REMOTE_TYPE = "webServiceWorker";
const DISABLE_JIT_REMOTE_TYPE_SUFFIX = "disableJit=1";

// This must start with the WEB_REMOTE_TYPE above.
const DEFAULT_REMOTE_TYPE = WEB_REMOTE_TYPE;

// This list is duplicated between Navigator.cpp and here because navigator
// is not accessible in this context. Please update both if the list changes.
const kSafeSchemes = [
  "bitcoin",
  "ftp",
  "ftps",
  "geo",
  "im",
  "irc",
  "ircs",
  "magnet",
  "mailto",
  "matrix",
  "mms",
  "news",
  "nntp",
  "openpgp4fpr",
  "sftp",
  "sip",
  "sms",
  "smsto",
  "ssh",
  "tel",
  "urn",
  "webcal",
  "wtai",
  "xmpp",
];

const STANDARD_SAFE_PROTOCOLS = kSafeSchemes;

// Note that even if the scheme fits the criteria for a web-handled scheme
// (ie it is compatible with the checks registerProtocolHandler uses), it may
// not be web-handled - it could still be handled via the OS by another app.
function hasPotentiallyWebHandledScheme({ scheme }) {
  // Note that `scheme` comes from a URI object so is already lowercase.
  if (kSafeSchemes.includes(scheme)) {
    return true;
  }
  if (!scheme.startsWith("web+") || scheme.length < 5) {
    return false;
  }
  // Check the rest of the scheme only consists of ascii a-z chars
  return /^[a-z]+$/.test(scheme.substr("web+".length));
}

function validatedWebRemoteType(
  aPreferredRemoteType,
  aTargetUri,
  aCurrentUri,
  aResultPrincipal,
  aRemoteSubframes,
  aOriginAttributes = {}
) {
  // To load into the Privileged Mozilla Content Process you must be https,
  // and be an exact match or a subdomain of an allowlisted domain.
  // This code is duplicated in ProcessIolation.cpp, please update both.
  if (
    lazy.separatePrivilegedMozillaWebContentProcess &&
    aTargetUri.asciiHost &&
    aTargetUri.scheme == "https" &&
    lazy.separatedMozillaDomains.some(function (val) {
      return (
        aTargetUri.asciiHost == val || aTargetUri.asciiHost.endsWith("." + val)
      );
    })
  ) {
    return PRIVILEGEDMOZILLA_REMOTE_TYPE;
  }

  // If we're in the parent and we were passed a web-handled scheme,
  // transform it now to avoid trying to load it in the wrong process.
  if (aRemoteSubframes && hasPotentiallyWebHandledScheme(aTargetUri)) {
    if (
      Services.appinfo.processType != Services.appinfo.PROCESS_TYPE_DEFAULT &&
      Services.appinfo.remoteType.startsWith(FISSION_WEB_REMOTE_TYPE + "=")
    ) {
      // If we're in a child process, assume we're OK to load this non-web
      // URL for now. We'll either load it externally or re-evaluate once
      // we know the "real" URL to which we'll redirect.
      return Services.appinfo.remoteType;
    }

    // This doesn't work (throws) in the child - see
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1589082
    // Even if it did, it'd cause sync IPC
    // ( https://bugzilla.mozilla.org/show_bug.cgi?id=1589085 ), and this code
    // can get called several times per page load so that seems like something
    // we'd want to avoid.
    let handlerInfo = lazy.extProtService.getProtocolHandlerInfo(
      aTargetUri.scheme
    );
    try {
      if (!handlerInfo.alwaysAskBeforeHandling) {
        let app = handlerInfo.preferredApplicationHandler;
        app.QueryInterface(Ci.nsIWebHandlerApp);
        // If we get here, the default handler is a web app.
        // Target to the origin of that web app:
        let uriStr = app.uriTemplate.replace(/%s/, aTargetUri.spec);
        aTargetUri = Services.io.newURI(uriStr);
      }
    } catch (ex) {
      // It's not strange for this to throw, we just ignore it and fall through.
    }
  }

  // If the domain is allow listed to allow it to use file:// URIs, then we have
  // to run it in a file content process, in case it uses file:// sub-resources.
  const sm = Services.scriptSecurityManager;
  if (sm.inFileURIAllowlist(aTargetUri)) {
    return FILE_REMOTE_TYPE;
  }

  // If we're within a fission window, extract site information from the URI in
  // question, and use it to generate an isolated origin.
  if (aRemoteSubframes) {
    let originAttributes = {};
    // Only use specific properties of OriginAttributes in our remoteType
    let { userContextId, privateBrowsingId, geckoViewSessionContextId } =
      aOriginAttributes;
    originAttributes = {
      userContextId,
      privateBrowsingId,
      geckoViewSessionContextId,
    };

    let jitAllowed;
    try {
      jitAllowed = Services.policies.isAllowedForURI("jit", aTargetUri);
    } catch (e) {
      jitAllowed = true;
    }

    // Get a principal to use for isolation.
    let targetPrincipal;
    if (aResultPrincipal) {
      targetPrincipal = sm.principalWithOA(aResultPrincipal, originAttributes);
    } else {
      targetPrincipal = sm.createContentPrincipal(aTargetUri, originAttributes);
    }

    let suffix = targetPrincipal.originSuffix;
    if (!jitAllowed) {
      if (suffix) {
        suffix += `&${DISABLE_JIT_REMOTE_TYPE_SUFFIX}`;
      } else {
        suffix = `^${DISABLE_JIT_REMOTE_TYPE_SUFFIX}`;
      }
    }
    let siteKey = `${targetPrincipal.siteOriginNoSuffix}${suffix}`;

    // If this is a special webCOOP+COEP= remote type that matches the
    // principal's siteOrigin, we don't want to override it with webIsolated=
    // as it's already isolated.
    if (
      aPreferredRemoteType &&
      aPreferredRemoteType == `${WEB_REMOTE_COOP_COEP_TYPE_PREFIX}${siteKey}`
    ) {
      return aPreferredRemoteType;
    }

    return `${FISSION_WEB_REMOTE_TYPE}=${siteKey}`;
    // else fall through and probably return WEB_REMOTE_TYPE
  }

  if (!aPreferredRemoteType) {
    return WEB_REMOTE_TYPE;
  }

  if (aPreferredRemoteType.startsWith(WEB_REMOTE_TYPE)) {
    return aPreferredRemoteType;
  }

  return WEB_REMOTE_TYPE;
}

export var E10SUtils = {
  DEFAULT_REMOTE_TYPE,
  NOT_REMOTE,
  WEB_REMOTE_TYPE,
  WEB_REMOTE_COOP_COEP_TYPE_PREFIX,
  FILE_REMOTE_TYPE,
  EXTENSION_REMOTE_TYPE,
  PRIVILEGEDABOUT_REMOTE_TYPE,
  PRIVILEGEDMOZILLA_REMOTE_TYPE,
  FISSION_WEB_REMOTE_TYPE,
  SERVICEWORKER_REMOTE_TYPE,
  STANDARD_SAFE_PROTOCOLS,

  /**
   * @param aURI The URI of the about page
   * @return The instance of the nsIAboutModule related to this uri
   */
  getAboutModule(aURL) {
    // Needs to match NS_GetAboutModuleName
    let moduleName = aURL.pathQueryRef.replace(/[#?].*/, "").toLowerCase();
    let contract = "@mozilla.org/network/protocol/about;1?what=" + moduleName;
    try {
      return Cc[contract].getService(Ci.nsIAboutModule);
    } catch (e) {
      // Either the about module isn't defined or it is broken. In either case
      // ignore it.
      return null;
    }
  },

  useCrossOriginOpenerPolicy() {
    return lazy.useCrossOriginOpenerPolicy;
  },

  _log: null,
  _uriStr: function uriStr(aUri) {
    return aUri ? aUri.spec : "undefined";
  },

  log: function log() {
    if (!this._log) {
      this._log = console.createInstance({
        prefix: "ProcessSwitch",
        maxLogLevel: "Error", // Change to "Debug" the process switching code
      });

      this._log.debug("Setup logger");
    }

    return this._log;
  },

  /**
   * Serialize csp data.
   *
   * @param {nsIContentSecurity} csp. The csp to serialize.
   * @return {string} The base64 encoded csp data.
   */
  serializeCSP(csp) {
    let serializedCSP = null;

    try {
      if (csp) {
        serializedCSP = lazy.serializationHelper.serializeToString(csp);
      }
    } catch (e) {
      this.log().error(`Failed to serialize csp '${csp}' ${e}`);
    }
    return serializedCSP;
  },

  /**
   * Deserialize a base64 encoded csp (serialized with
   * Utils::serializeCSP).
   *
   * @param {string} csp_b64 A base64 encoded serialized csp.
   * @return {nsIContentSecurityPolicy} A deserialized csp.
   */
  deserializeCSP(csp_b64) {
    if (!csp_b64) {
      return null;
    }

    try {
      let csp = lazy.serializationHelper.deserializeObject(csp_b64);
      csp.QueryInterface(Ci.nsIContentSecurityPolicy);
      return csp;
    } catch (e) {
      this.log().error(`Failed to deserialize csp_b64 '${csp_b64}' ${e}`);
    }
    return null;
  },

  /**
   * Serialize policyContainer data.
   *
   * @param {nsIPolicyContainer} policyContainer. The policyContainer to serialize.
   * @return {string} The base64 encoded policyContainer data.
   */
  serializePolicyContainer(policyContainer) {
    let serializedPolicyContainer = null;

    try {
      if (policyContainer) {
        serializedPolicyContainer =
          lazy.serializationHelper.serializeToString(policyContainer);
      }
    } catch (e) {
      this.log().error(
        `Failed to serialize policyContainer '${policyContainer}' ${e}`
      );
    }
    return serializedPolicyContainer;
  },

  /**
   * Deserialize a base64 encoded policyContainer (serialized with
   * Utils::serializePolicyContainer).
   *
   * @param {string} policyContainerB64 A base64 encoded serialized policyContainer.
   * @return {nsIPolicyContainer} A deserialized policyContainer.
   */
  deserializePolicyContainer(policyContainerB64) {
    if (!policyContainerB64) {
      return null;
    }

    try {
      let policyContainer =
        lazy.serializationHelper.deserializeObject(policyContainerB64);
      policyContainer.QueryInterface(Ci.nsIPolicyContainer);
      return policyContainer;
    } catch (e) {
      this.log().error(
        `Failed to deserialize policyContainerB64 '${policyContainerB64}' ${e}`
      );
    }
    return null;
  },

  canLoadURIInRemoteType(
    aURL,
    aRemoteSubframes,
    aRemoteType = DEFAULT_REMOTE_TYPE,
    aOriginAttributes = {}
  ) {
    // aRemoteType cannot be undefined, as that would cause it to default to
    // `DEFAULT_REMOTE_TYPE`. This means any falsy remote types are
    // intentionally `NOT_REMOTE`.

    return (
      aRemoteType ==
      this.getRemoteTypeForURI(
        aURL,
        true,
        aRemoteSubframes,
        aRemoteType,
        null,
        aOriginAttributes
      )
    );
  },

  getRemoteTypeForURI(
    aURL,
    aMultiProcess,
    aRemoteSubframes,
    aPreferredRemoteType = DEFAULT_REMOTE_TYPE,
    aCurrentUri,
    aOriginAttributes = {}
  ) {
    if (!aMultiProcess) {
      return NOT_REMOTE;
    }

    // loadURI in browser.js treats null as about:blank
    if (!aURL) {
      aURL = "about:blank";
    }

    let uri;
    try {
      uri = Services.uriFixup.getFixupURIInfo(aURL).preferredURI;
    } catch (e) {
      // If we have an invalid URI, it's still possible that it might get
      // fixed-up into a valid URI later on. However, we don't want to return
      // aPreferredRemoteType here, in case the URI gets fixed-up into
      // something that wouldn't normally run in that process.
      return DEFAULT_REMOTE_TYPE;
    }

    return this.getRemoteTypeForURIObject(uri, {
      multiProcess: aMultiProcess,
      remoteSubFrames: aRemoteSubframes,
      preferredRemoteType: aPreferredRemoteType,
      currentURI: aCurrentUri,
      originAttributes: aOriginAttributes,
    });
  },

  getRemoteTypeForURIObject(aURI, options) {
    let {
      multiProcess = Services.appinfo.browserTabsRemoteAutostart,
      remoteSubFrames = Services.appinfo.fissionAutostart,
      preferredRemoteType = DEFAULT_REMOTE_TYPE,
      currentURI = null,
      resultPrincipal = null,
      originAttributes = {},
    } = options;
    if (!multiProcess) {
      return NOT_REMOTE;
    }

    switch (aURI.scheme) {
      case "javascript":
        // javascript URIs can load in any, they apply to the current document.
        return preferredRemoteType;

      case "data":
      case "blob":
        // We need data: and blob: URIs to load in any remote process, because
        // they need to be able to load in whatever is the current process
        // unless it is non-remote. In that case we don't want to load them in
        // the parent process, so we load them in the default remote process,
        // which is sandboxed and limits any risk.
        return preferredRemoteType == NOT_REMOTE
          ? DEFAULT_REMOTE_TYPE
          : preferredRemoteType;

      case "file":
        return lazy.useSeparateFileUriProcess
          ? FILE_REMOTE_TYPE
          : DEFAULT_REMOTE_TYPE;

      case "about": {
        let module = this.getAboutModule(aURI);
        // If the module doesn't exist then an error page will be loading, that
        // should be ok to load in any process
        if (!module) {
          return preferredRemoteType;
        }

        let flags = module.getURIFlags(aURI);
        if (flags & Ci.nsIAboutModule.URI_MUST_LOAD_IN_EXTENSION_PROCESS) {
          return WebExtensionPolicy.useRemoteWebExtensions
            ? EXTENSION_REMOTE_TYPE
            : NOT_REMOTE;
        }

        if (flags & Ci.nsIAboutModule.URI_MUST_LOAD_IN_CHILD) {
          if (
            flags & Ci.nsIAboutModule.URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS &&
            (lazy.useSeparatePrivilegedAboutContentProcess ||
              aURI.filePath == "logins" ||
              // Force about:welcome and about:home into the privileged content process to
              // workaround code coverage test failures which result from the
              // workaround in bug 161269. Once that bug is fixed for real,
              // the about:welcome and about:home case below can be removed.
              aURI.filePath == "welcome" ||
              aURI.filePath == "home")
          ) {
            return PRIVILEGEDABOUT_REMOTE_TYPE;
          }

          // When loading about:reader, try to display the document in the same
          // web remote type as the document it's loading.
          if (aURI.filePath == "reader") {
            let readerModeURI = getOriginalReaderModeURI(aURI);
            if (readerModeURI) {
              let innerRemoteType = this.getRemoteTypeForURIObject(
                readerModeURI,
                options
              );
              if (
                innerRemoteType &&
                innerRemoteType.startsWith(WEB_REMOTE_TYPE)
              ) {
                return innerRemoteType;
              }
            }
          }

          return DEFAULT_REMOTE_TYPE;
        }

        // If the about page can load in parent or child, it should be safe to
        // load in any remote type.
        if (flags & Ci.nsIAboutModule.URI_CAN_LOAD_IN_CHILD) {
          return preferredRemoteType;
        }

        return NOT_REMOTE;
      }

      case "chrome": {
        let chromeReg = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
          Ci.nsIXULChromeRegistry
        );
        if (chromeReg.mustLoadURLRemotely(aURI)) {
          return DEFAULT_REMOTE_TYPE;
        }

        if (
          chromeReg.canLoadURLRemotely(aURI) &&
          preferredRemoteType != NOT_REMOTE
        ) {
          return DEFAULT_REMOTE_TYPE;
        }

        return NOT_REMOTE;
      }

      case "moz-extension":
        // Extension iframes should load in the same process
        // as their outer frame, but that's handled elsewhere.
        return WebExtensionPolicy.useRemoteWebExtensions
          ? EXTENSION_REMOTE_TYPE
          : NOT_REMOTE;

      case "imap":
      case "mailbox":
      case "news":
      case "nntp":
      case "snews":
      case "x-moz-ews":
      case "x-moz-graph":
        // Protocols used by Thunderbird to display email messages.
        return NOT_REMOTE;

      default:
        // WebExtensions may set up protocol handlers for protocol names
        // beginning with ext+.  These may redirect to http(s) pages or to
        // moz-extension pages.  We can't actually tell here where one of
        // these pages will end up loading but Talos tests use protocol
        // handlers that redirect to extension pages that rely on this
        // behavior so a pageloader frame script is injected correctly.
        // Protocols that redirect to http(s) will just flip back to a
        // regular content process after the redirect.
        if (aURI.scheme.startsWith("ext+")) {
          return WebExtensionPolicy.useRemoteWebExtensions
            ? EXTENSION_REMOTE_TYPE
            : NOT_REMOTE;
        }

        // For any other nested URIs, we use the innerURI to determine the
        // remote type. In theory we should use the innermost URI, but some URIs
        // have fake inner URIs (e.g. about URIs with inner moz-safe-about) and
        // if such URIs are wrapped in other nested schemes like view-source:,
        // we don't want to "skip" past "about:" by going straight to the
        // innermost URI. Any URIs like this will need to be handled in the
        // cases above, so we don't still end up using the fake inner URI here.
        if (aURI instanceof Ci.nsINestedURI) {
          let innerURI = aURI.QueryInterface(Ci.nsINestedURI).innerURI;
          return this.getRemoteTypeForURIObject(innerURI, options);
        }

        var log = this.log();
        log.debug("validatedWebRemoteType()");
        log.debug(`  aPreferredRemoteType: ${preferredRemoteType}`);
        log.debug(`  aTargetUri: ${this._uriStr(aURI)}`);
        log.debug(`  aCurrentUri: ${this._uriStr(currentURI)}`);
        var remoteType = validatedWebRemoteType(
          preferredRemoteType,
          aURI,
          currentURI,
          resultPrincipal,
          remoteSubFrames,
          originAttributes
        );
        log.debug(`  validatedWebRemoteType() returning: ${remoteType}`);
        return remoteType;
    }
  },

  makeInputStream(data) {
    if (typeof data == "string") {
      let stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
        Ci.nsISupportsCString
      );
      stream.data = data;
      return stream; // XPConnect will QI this to nsIInputStream for us.
    }

    let stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsISupportsCString
    );
    stream.data = data.content;

    if (data.headers) {
      let mimeStream = Cc[
        "@mozilla.org/network/mime-input-stream;1"
      ].createInstance(Ci.nsIMIMEInputStream);

      mimeStream.setData(stream);
      for (let [name, value] of data.headers) {
        mimeStream.addHeader(name, value);
      }
      return mimeStream;
    }

    return stream; // XPConnect will QI this to nsIInputStream for us.
  },

  /**
   * Serialize principal data.
   *
   * @param {nsIPrincipal} principal The principal to serialize.
   * @return {string} The serialized principal data.
   */
  serializePrincipal(principal) {
    let serializedPrincipal = null;

    try {
      if (principal) {
        serializedPrincipal =
          Services.scriptSecurityManager.principalToJSON(principal);
      }
    } catch (e) {
      this.log().error(`Failed to serialize principal '${principal}' ${e}`);
    }

    return serializedPrincipal;
  },

  /**
   * Deserialize a principal (serialized with serializePrincipal).
   *
   * @param {string} serializedPincipal A serialized principal.
   * @return {nsIPrincipal} A deserialized principal.
   */
  deserializePrincipal(serializedPincipal, fallbackPrincipalCallback = null) {
    if (!serializedPincipal) {
      if (!fallbackPrincipalCallback) {
        this.log().warn(
          "No principal passed to deserializePrincipal and no fallbackPrincipalCallback"
        );
        return null;
      }

      return fallbackPrincipalCallback();
    }

    try {
      let principal;
      // The current JSON representation of principal is not stored as base64. We start by checking
      // if the serialized data starts with '{' to determine if we're using the new JSON representation.
      // If it doesn't we try the two legacy formats, old JSON and nsISerializable.
      if (serializedPincipal.startsWith("{")) {
        principal =
          Services.scriptSecurityManager.JSONToPrincipal(serializedPincipal);
      } else {
        // Both the legacy and legacy  JSON representation of principals are stored as base64
        // The legacy JSON kind are the only ones that will start with "{" when decoded.
        // We check here for the legacy JSON serialized, if it doesn't start with that continue using nsISerializable.
        // JSONToPrincipal accepts a *non* base64 encoded string and returns a principal or a null.
        let tmpa = atob(serializedPincipal);
        if (tmpa.startsWith("{")) {
          principal = Services.scriptSecurityManager.JSONToPrincipal(tmpa);
        } else {
          principal =
            lazy.serializationHelper.deserializeObject(serializedPincipal);
        }
      }
      principal.QueryInterface(Ci.nsIPrincipal);
      return principal;
    } catch (e) {
      this.log().error(
        `Failed to deserialize serializedPincipal '${serializedPincipal}' ${e}`
      );
    }
    if (!fallbackPrincipalCallback) {
      this.log().warn(
        "No principal passed to deserializePrincipal and no fallbackPrincipalCallback"
      );
      return null;
    }
    return fallbackPrincipalCallback();
  },

  /**
   * Serialize cookieJarSettings.
   *
   * @param {nsICookieJarSettings} cookieJarSettings The cookieJarSettings to
   *   serialize.
   * @return {string} The base64 encoded cookieJarSettings data.
   */
  serializeCookieJarSettings(cookieJarSettings) {
    let serialized = null;
    if (cookieJarSettings) {
      try {
        serialized =
          lazy.serializationHelper.serializeToString(cookieJarSettings);
      } catch (e) {
        this.log().error(
          `Failed to serialize cookieJarSettings '${cookieJarSettings}' ${e}`
        );
      }
    }
    return serialized;
  },

  /**
   * Deserialize a base64 encoded cookieJarSettings
   *
   * @param {string} cookieJarSettings_b64 A base64 encoded serialized cookieJarSettings.
   * @return {nsICookieJarSettings} A deserialized cookieJarSettings.
   */
  deserializeCookieJarSettings(cookieJarSettings_b64) {
    let deserialized = null;
    if (cookieJarSettings_b64) {
      try {
        deserialized = lazy.serializationHelper.deserializeObject(
          cookieJarSettings_b64
        );
        deserialized.QueryInterface(Ci.nsICookieJarSettings);
      } catch (e) {
        this.log().error(
          `Failed to deserialize cookieJarSettings_b64 '${cookieJarSettings_b64}' ${e}`
        );
      }
    }
    return deserialized;
  },

  wrapHandlingUserInput(aWindow, aIsHandling, aCallback) {
    var handlingUserInput;
    try {
      handlingUserInput = aWindow.windowUtils.setHandlingUserInput(aIsHandling);
      aCallback();
    } finally {
      handlingUserInput.destruct();
    }
  },

  /**
   * Serialize referrerInfo.
   *
   * @param {nsIReferrerInfo} The referrerInfo to serialize.
   * @return {string} The base64 encoded referrerInfo.
   */
  serializeReferrerInfo(referrerInfo) {
    let serialized = null;
    if (referrerInfo) {
      try {
        serialized = lazy.serializationHelper.serializeToString(referrerInfo);
      } catch (e) {
        this.log().error(
          `Failed to serialize referrerInfo '${referrerInfo}' ${e}`
        );
      }
    }
    return serialized;
  },
  /**
   * Deserialize a base64 encoded referrerInfo
   *
   * @param {string} referrerInfo_b64 A base64 encoded serialized referrerInfo.
   * @return {nsIReferrerInfo} A deserialized referrerInfo.
   */
  deserializeReferrerInfo(referrerInfo_b64) {
    let deserialized = null;
    if (referrerInfo_b64) {
      try {
        deserialized =
          lazy.serializationHelper.deserializeObject(referrerInfo_b64);
        deserialized.QueryInterface(Ci.nsIReferrerInfo);
      } catch (e) {
        this.log().error(
          `Failed to deserialize referrerInfo_b64 '${referrerInfo_b64}' ${e}`
        );
      }
    }
    return deserialized;
  },

  /**
   * Returns the pids for a remote browser and its remote subframes.
   */
  getBrowserPids(aBrowser, aRemoteSubframes) {
    if (!aBrowser.isRemoteBrowser || !aBrowser.frameLoader) {
      return [];
    }
    let tabPid = aBrowser.frameLoader.remoteTab.osPid;
    let pids = new Set();
    if (aRemoteSubframes) {
      let stack = [aBrowser.browsingContext];
      while (stack.length) {
        let bc = stack.pop();
        stack.push(...bc.children);
        if (bc.currentWindowGlobal) {
          let pid = bc.currentWindowGlobal.osPid;
          if (pid != tabPid) {
            pids.add(pid);
          }
        }
      }
    }
    return [tabPid, ...pids];
  },

  /**
   * The suffix after a `=` in a remoteType is dynamic, and used to control the
   * process pool to use. The C++ version of this method is mozilla::dom::RemoteTypePrefix().
   */
  remoteTypePrefix(aRemoteType) {
    return aRemoteType.split("=")[0];
  },

  /**
   * There are various types of remote types that are for web content processes, but
   * they all start with "web". The C++ version of this method is
   * mozilla::dom::IsWebRemoteType().
   */
  isWebRemoteType(aRemoteType) {
    return aRemoteType.startsWith(WEB_REMOTE_TYPE);
  },

  /**
   * Assemble or predict originAttributes from available arguments.
   */
  predictOriginAttributes({
    window,
    browser,
    userContextId,
    geckoViewSessionContextId,
    privateBrowsingId,
  }) {
    if (browser) {
      if (browser.browsingContext) {
        return browser.browsingContext.originAttributes;
      }
      if (!window) {
        window = browser.contentDocument?.defaultView;
      }
      if (!userContextId) {
        userContextId = browser.getAttribute("usercontextid") || 0;
      }
      if (!geckoViewSessionContextId) {
        geckoViewSessionContextId =
          browser.getAttribute("geckoViewSessionContextId") || "";
      }
    }

    if (window && !privateBrowsingId) {
      privateBrowsingId = window.browsingContext.usePrivateBrowsing ? 1 : 0;
    }
    return { privateBrowsingId, userContextId, geckoViewSessionContextId };
  },
};

ChromeUtils.defineLazyGetter(
  E10SUtils,
  "SERIALIZED_SYSTEMPRINCIPAL",
  function () {
    return btoa(
      E10SUtils.serializePrincipal(
        Services.scriptSecurityManager.getSystemPrincipal()
      )
    );
  }
);
