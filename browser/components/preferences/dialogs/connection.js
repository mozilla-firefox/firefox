/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from /browser/base/content/utilityOverlay.js */
/* import-globals-from /toolkit/content/preferencesBindings.js */
/* import-globals-from ../extensionControlled.js */

const proxyService = Ci.nsIProtocolProxyService;

const PROXY_TYPES_MAP_REVERSE = new Map([
  [proxyService.PROXYCONFIG_DIRECT, "DIRECT"],
  [proxyService.PROXYCONFIG_MANUAL, "MANUAL"],
  [proxyService.PROXYCONFIG_PAC, "PAC"],
  [proxyService.PROXYCONFIG_WPAD, "WPAD"],
  [proxyService.PROXYCONFIG_SYSTEM, "SYSTEM"],
]);

Preferences.addAll([
  // Add network.proxy.autoconfig_url before network.proxy.type so they're
  // both initialized when network.proxy.type initialization triggers a call to
  // gConnectionsDialog.updateReloadButton().
  { id: "network.proxy.autoconfig_url", type: "string" },
  { id: "network.proxy.system_wpad", type: "bool" },
  { id: "network.proxy.type", type: "int" },
  { id: "network.proxy.http", type: "string" },
  { id: "network.proxy.http_port", type: "int" },
  { id: "network.proxy.ssl", type: "string" },
  { id: "network.proxy.ssl_port", type: "int" },
  { id: "network.proxy.socks", type: "string" },
  { id: "network.proxy.socks_port", type: "int" },
  { id: "network.proxy.socks_version", type: "int" },
  { id: "network.proxy.socks_remote_dns", type: "bool" },
  { id: "network.proxy.socks5_remote_dns", type: "bool" },
  { id: "network.proxy.no_proxies_on", type: "string" },
  { id: "network.proxy.share_proxy_settings", type: "bool" },
  { id: "signon.autologin.proxy", type: "bool" },
  { id: "pref.advanced.proxies.disable_button.reload", type: "bool" },
  { id: "network.proxy.backup.ssl", type: "string" },
  { id: "network.proxy.backup.ssl_port", type: "int" },
]);

window.addEventListener(
  "DOMContentLoaded",
  () => {
    Preferences.get("network.proxy.type").on(
      "change",
      gConnectionsDialog.proxyTypeChanged.bind(gConnectionsDialog)
    );
    Preferences.get("network.proxy.socks_version").on(
      "change",
      gConnectionsDialog.updateDNSPref.bind(gConnectionsDialog)
    );

    document
      .getElementById("key_close")
      .addEventListener("command", event => Preferences.close(event));

    document
      .getElementById("disableProxyExtension")
      .addEventListener(
        "command",
        makeDisableControllingExtension(PREF_SETTING_TYPE, PROXY_KEY).bind(
          gConnectionsDialog
        )
      );
    document
      .getElementById("networkProxyAutoconfigURL")
      .addEventListener("input", () => gConnectionsDialog.updateReloadButton());
    document
      .getElementById("autoReload")
      .addEventListener("command", () => gConnectionsDialog.reloadPAC());

    gConnectionsDialog.checkForSystemProxy();
    gConnectionsDialog.updateProxySettingsUI();
    initializeProxyUI(gConnectionsDialog);
    gConnectionsDialog.registerSyncPrefListeners();
    document
      .getElementById("ConnectionsDialog")
      .addEventListener("beforeaccept", e =>
        gConnectionsDialog.beforeAccept(e)
      );
  },
  { once: true, capture: true }
);

var gConnectionsDialog = {
  beforeAccept(event) {
    var proxyTypePref = Preferences.get("network.proxy.type");

    // collect "network.proxy.type" to glean metrics
    let proxyTypePrefStr =
      PROXY_TYPES_MAP_REVERSE.get(proxyTypePref.value) || "OTHER";
    Glean.networkProxySettings.proxyTypePreference.record({
      value: proxyTypePrefStr,
    });

    if (proxyTypePref.value == 2) {
      this.doAutoconfigURLFixup();
      return;
    }

    if (proxyTypePref.value != 1) {
      return;
    }

    var httpProxyURLPref = Preferences.get("network.proxy.http");
    var httpProxyPortPref = Preferences.get("network.proxy.http_port");
    var shareProxiesPref = Preferences.get(
      "network.proxy.share_proxy_settings"
    );

    // If the proxy server (when specified) is invalid or the port is set to 0 then cancel submission.
    for (let prefName of ["http", "ssl", "socks"]) {
      let proxyPortPref = Preferences.get(
        "network.proxy." + prefName + "_port"
      );
      let proxyPref = Preferences.get("network.proxy." + prefName);
      // Only worry about ports which are currently active. If the share option is on, then ignore
      // all ports except the HTTP and SOCKS port
      if (
        proxyPref.value != "" &&
        (prefName == "http" || prefName == "socks" || !shareProxiesPref.value)
      ) {
        if (proxyPortPref.value == 0) {
          document
            .getElementById("networkProxy" + prefName.toUpperCase() + "_Port")
            .focus();
          event.preventDefault();
          return;
        } else if (
          !Services.io.isValidHostname(proxyPref.value) &&
          !(prefName === "socks" && (proxyPref.value.startsWith("file://") || proxyPref.value.startsWith("npipe://")))
        ) {
          document
            .getElementById("networkProxy" + prefName.toUpperCase())
            .focus();
          event.preventDefault();
          return;
        }
      }
    }

    // In the case of a shared proxy preference, backup the current values and update with the HTTP value
    if (shareProxiesPref.value) {
      var proxyServerURLPref = Preferences.get("network.proxy.ssl");
      var proxyPortPref = Preferences.get("network.proxy.ssl_port");
      var backupServerURLPref = Preferences.get("network.proxy.backup.ssl");
      var backupPortPref = Preferences.get("network.proxy.backup.ssl_port");
      backupServerURLPref.value =
        backupServerURLPref.value || proxyServerURLPref.value;
      backupPortPref.value = backupPortPref.value || proxyPortPref.value;
      proxyServerURLPref.value = httpProxyURLPref.value;
      proxyPortPref.value = httpProxyPortPref.value;
    }

    this.sanitizeNoProxiesPref();
  },

  checkForSystemProxy() {
    if ("@mozilla.org/system-proxy-settings;1" in Cc) {
      document.getElementById("systemPref").removeAttribute("hidden");

      var systemWpadAllowed = Services.prefs.getBoolPref(
        "network.proxy.system_wpad.allowed",
        false
      );
      if (systemWpadAllowed && AppConstants.platform == "win") {
        document.getElementById("systemWpad").removeAttribute("hidden");
      }
    }
  },

  proxyTypeChanged() {
    var proxyTypePref = Preferences.get("network.proxy.type");
    var systemWpadPref = Preferences.get("network.proxy.system_wpad");
    systemWpadPref.updateControlDisabledState(proxyTypePref.value != 5);

    // Update http
    var httpProxyURLPref = Preferences.get("network.proxy.http");
    httpProxyURLPref.updateControlDisabledState(proxyTypePref.value != 1);
    var httpProxyPortPref = Preferences.get("network.proxy.http_port");
    httpProxyPortPref.updateControlDisabledState(proxyTypePref.value != 1);

    // Now update the other protocols
    this.updateProtocolPrefs();

    var shareProxiesPref = Preferences.get(
      "network.proxy.share_proxy_settings"
    );
    shareProxiesPref.updateControlDisabledState(proxyTypePref.value != 1);
    var autologinProxyPref = Preferences.get("signon.autologin.proxy");
    autologinProxyPref.updateControlDisabledState(proxyTypePref.value == 0);
    var noProxiesPref = Preferences.get("network.proxy.no_proxies_on");
    noProxiesPref.updateControlDisabledState(proxyTypePref.value == 0);

    var autoconfigURLPref = Preferences.get("network.proxy.autoconfig_url");
    autoconfigURLPref.updateControlDisabledState(proxyTypePref.value != 2);

    this.updateReloadButton();

    document.getElementById("networkProxyNoneLocalhost").hidden =
      Services.prefs.getBoolPref(
        "network.proxy.allow_hijacking_localhost",
        false
      );
  },

  updateDNSPref() {
    var socksVersionPref = Preferences.get("network.proxy.socks_version");
    var socks4DNSPref = Preferences.get("network.proxy.socks_remote_dns");
    var socks5DNSPref = Preferences.get("network.proxy.socks5_remote_dns");
    var proxyTypePref = Preferences.get("network.proxy.type");
    var isDefinitelySocks4 =
      proxyTypePref.value == 1 && socksVersionPref.value == 4;
    socks5DNSPref.updateControlDisabledState(
      isDefinitelySocks4 || proxyTypePref.value == 0
    );
    var isDefinitelySocks5 =
      proxyTypePref.value == 1 && socksVersionPref.value == 5;
    socks4DNSPref.updateControlDisabledState(
      isDefinitelySocks5 || proxyTypePref.value == 0
    );
    return undefined;
  },

  updateReloadButton() {
    // Disable the "Reload PAC" button if the selected proxy type is not PAC or
    // if the current value of the PAC input does not match the value stored
    // in prefs.  Likewise, disable the reload button if PAC is not configured
    // in prefs.

    var typedURL = document.getElementById("networkProxyAutoconfigURL").value;
    var proxyTypeCur = Preferences.get("network.proxy.type").value;

    var pacURL = Services.prefs.getCharPref("network.proxy.autoconfig_url");
    var proxyType = Services.prefs.getIntPref("network.proxy.type");

    var disableReloadPref = Preferences.get(
      "pref.advanced.proxies.disable_button.reload"
    );
    disableReloadPref.updateControlDisabledState(
      proxyTypeCur != 2 || proxyType != 2 || typedURL != pacURL
    );
  },

  readProxyType() {
    this.proxyTypeChanged();
    return undefined;
  },

  updateProtocolPrefs() {
    var proxyTypePref = Preferences.get("network.proxy.type");
    var shareProxiesPref = Preferences.get(
      "network.proxy.share_proxy_settings"
    );
    var proxyPrefs = ["ssl", "socks"];
    for (var i = 0; i < proxyPrefs.length; ++i) {
      var proxyServerURLPref = Preferences.get(
        "network.proxy." + proxyPrefs[i]
      );
      var proxyPortPref = Preferences.get(
        "network.proxy." + proxyPrefs[i] + "_port"
      );

      // Restore previous per-proxy custom settings, if present.
      if (proxyPrefs[i] != "socks" && !shareProxiesPref.value) {
        var backupServerURLPref = Preferences.get(
          "network.proxy.backup." + proxyPrefs[i]
        );
        var backupPortPref = Preferences.get(
          "network.proxy.backup." + proxyPrefs[i] + "_port"
        );
        if (backupServerURLPref.hasUserValue) {
          proxyServerURLPref.value = backupServerURLPref.value;
          backupServerURLPref.reset();
        }
        if (backupPortPref.hasUserValue) {
          proxyPortPref.value = backupPortPref.value;
          backupPortPref.reset();
        }
      }

      proxyServerURLPref.updateElements();
      proxyPortPref.updateElements();
      let prefIsShared = proxyPrefs[i] != "socks" && shareProxiesPref.value;
      proxyServerURLPref.updateControlDisabledState(
        proxyTypePref.value != 1 || prefIsShared
      );
      proxyPortPref.updateControlDisabledState(
        proxyTypePref.value != 1 || prefIsShared
      );
    }
    var socksVersionPref = Preferences.get("network.proxy.socks_version");
    socksVersionPref.updateControlDisabledState(proxyTypePref.value != 1);
    this.updateDNSPref();
    return undefined;
  },

  readProxyProtocolPref(aProtocol, aIsPort) {
    if (aProtocol != "socks") {
      var shareProxiesPref = Preferences.get(
        "network.proxy.share_proxy_settings"
      );
      if (shareProxiesPref.value) {
        var pref = Preferences.get(
          "network.proxy.http" + (aIsPort ? "_port" : "")
        );
        return pref.value;
      }

      var backupPref = Preferences.get(
        "network.proxy.backup." + aProtocol + (aIsPort ? "_port" : "")
      );
      return backupPref.hasUserValue ? backupPref.value : undefined;
    }
    return undefined;
  },

  reloadPAC() {
    Cc["@mozilla.org/network/protocol-proxy-service;1"]
      .getService()
      .reloadPAC();
  },

  doAutoconfigURLFixup() {
    var autoURL = document.getElementById("networkProxyAutoconfigURL");
    var autoURLPref = Preferences.get("network.proxy.autoconfig_url");
    try {
      autoURLPref.value = autoURL.value = Services.uriFixup.getFixupURIInfo(
        autoURL.value
      ).preferredURI.spec;
    } catch (ex) {}
  },

  sanitizeNoProxiesPref() {
    var noProxiesPref = Preferences.get("network.proxy.no_proxies_on");
    // replace substrings of ; and \n with commas if they're neither immediately
    // preceded nor followed by a valid separator character
    noProxiesPref.value = noProxiesPref.value.replace(
      /([^, \n;])[;\n]+(?![,\n;])/g,
      "$1,"
    );
    // replace any remaining ; and \n since some may follow commas, etc.
    noProxiesPref.value = noProxiesPref.value.replace(/[;\n]/g, "");
  },

  readHTTPProxyServer() {
    var shareProxiesPref = Preferences.get(
      "network.proxy.share_proxy_settings"
    );
    if (shareProxiesPref.value) {
      this.updateProtocolPrefs();
    }
    return undefined;
  },

  readHTTPProxyPort() {
    var shareProxiesPref = Preferences.get(
      "network.proxy.share_proxy_settings"
    );
    if (shareProxiesPref.value) {
      this.updateProtocolPrefs();
    }
    return undefined;
  },

  getProxyControls() {
    let controlGroup = document.getElementById("networkProxyType");
    return [
      ...controlGroup.querySelectorAll(":scope > radio"),
      ...controlGroup.querySelectorAll("label"),
      ...controlGroup.querySelectorAll("input"),
      ...controlGroup.querySelectorAll("checkbox"),
      ...document.querySelectorAll("#networkProxySOCKSVersion > radio"),
      ...document.querySelectorAll("#ConnectionsDialogPane > checkbox"),
    ];
  },

  // Update the UI to show/hide the extension controlled message for
  // proxy settings.
  async updateProxySettingsUI() {
    let isLocked = API_PROXY_PREFS.some(pref =>
      Services.prefs.prefIsLocked(pref)
    );

    function setInputsDisabledState(isControlled) {
      for (let element of gConnectionsDialog.getProxyControls()) {
        element.disabled = isControlled;
      }
      gConnectionsDialog.proxyTypeChanged();
    }

    if (isLocked) {
      // An extension can't control this setting if any pref is locked.
      hideControllingExtension(PROXY_KEY);
    } else {
      handleControllingExtension(PREF_SETTING_TYPE, PROXY_KEY).then(
        setInputsDisabledState
      );
    }
  },

  registerSyncPrefListeners() {
    function setSyncFromPrefListener(element_id, callback) {
      Preferences.addSyncFromPrefListener(
        document.getElementById(element_id),
        callback
      );
    }
    setSyncFromPrefListener("networkProxyType", () => this.readProxyType());
    setSyncFromPrefListener("networkProxyHTTP", () =>
      this.readHTTPProxyServer()
    );
    setSyncFromPrefListener("networkProxyHTTP_Port", () =>
      this.readHTTPProxyPort()
    );
    setSyncFromPrefListener("shareAllProxies", () =>
      this.updateProtocolPrefs()
    );
    setSyncFromPrefListener("networkProxySSL", () =>
      this.readProxyProtocolPref("ssl", false)
    );
    setSyncFromPrefListener("networkProxySSL_Port", () =>
      this.readProxyProtocolPref("ssl", true)
    );
    setSyncFromPrefListener("networkProxySOCKS", () =>
      this.readProxyProtocolPref("socks", false)
    );
    setSyncFromPrefListener("networkProxySOCKS_Port", () =>
      this.readProxyProtocolPref("socks", true)
    );
  },
};
