/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

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

// This must start with the WEB_REMOTE_TYPE above.
const DEFAULT_REMOTE_TYPE = WEB_REMOTE_TYPE;

// This list is duplicated between Navigator.cpp and here because navigator
// is not accessible in this context. Please update both if the list changes.
const STANDARD_SAFE_PROTOCOLS = [
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
        logModule: "ProcessSwitch",
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
   * There are various types of remote types that are for web content processes, but
   * they all start with "web". The C++ version of this method is RemoteType::IsWeb().
   */
  isWebRemoteType(aRemoteType) {
    return aRemoteType.startsWith(WEB_REMOTE_TYPE);
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
