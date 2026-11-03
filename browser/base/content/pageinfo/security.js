/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { SiteDataManager } = ChromeUtils.importESModule(
  "resource:///modules/SiteDataManager.sys.mjs"
);
const { DownloadUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/DownloadUtils.sys.mjs"
);
const { QWACs } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/QWACs.sys.mjs"
);

/* import-globals-from pageInfo.js */

ChromeUtils.defineESModuleGetters(this, {
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
});

var security = {
  async init(uri, windowInfo, browsingContext) {
    this.uri = uri;
    this.windowInfo = windowInfo;
    this.securityInfo = await this._getSecurityInfo(browsingContext);
  },

  viewCert(certChain = this.securityInfo.certChain) {
    let certs = certChain.map(elem =>
      encodeURIComponent(elem.getBase64DERString())
    );
    let certsStringURL = certs.map(elem => `cert=${elem}`);
    certsStringURL = certsStringURL.join("&");
    let url = `about:certificate?${certsStringURL}`;
    let win = BrowserWindowTracker.getTopWindow();
    if (win) {
      win.switchToTabHavingURI(url, true, {});
    } else {
      URILoadingHelper.openTrustedLinkIn(window, url, "window");
    }
  },

  viewQWAC() {
    this.viewCert([this.securityInfo.qwac]);
  },

  async _getSecurityInfo(browsingContext) {
    // We don't have separate info for a frame, return null until further notice
    // (see bug 138479)
    if (!this.windowInfo.isTopWindow) {
      return null;
    }

    var ui = security._getSecurityUI();
    if (!ui) {
      return null;
    }

    var isBroken = ui.state & Ci.nsIWebProgressListener.STATE_IS_BROKEN;
    var isMixed =
      ui.state &
      (Ci.nsIWebProgressListener.STATE_LOADED_MIXED_ACTIVE_CONTENT |
        Ci.nsIWebProgressListener.STATE_LOADED_MIXED_DISPLAY_CONTENT);
    var isEV = ui.state & Ci.nsIWebProgressListener.STATE_IDENTITY_EV_TOPLEVEL;

    let retval = {
      cAName: "",
      encryptionAlgorithm: "",
      encryptionStrength: 0,
      version: "",
      isBroken,
      isMixed,
      isEV,
      cert: null,
      qwac: null,
      certificateTransparency: null,
    };

    // Only show certificate info for secure contexts. This prevents us from
    // showing certificate data for http origins when using a proxy.
    // https://searchfox.org/mozilla-central/rev/9c72508fcf2bba709a5b5b9eae9da35e0c707baa/security/manager/ssl/nsSecureBrowserUI.cpp#62-64
    if (!ui.isSecureContext) {
      return retval;
    }

    let secInfo = ui.secInfo;
    if (!secInfo) {
      return retval;
    }

    let cert = secInfo.serverCert;
    let issuerName = null;
    if (cert) {
      issuerName = cert.issuerOrganization || cert.issuerName;
    }

    let certChainArray = [];
    if (secInfo.succeededCertChain.length) {
      certChainArray = secInfo.succeededCertChain;
    } else {
      certChainArray = secInfo.handshakeCertificates;
    }

    let qwac = await QWACs.determineQWACStatus(
      secInfo,
      this.uri,
      browsingContext
    );

    retval = {
      cAName: issuerName,
      encryptionAlgorithm: undefined,
      encryptionStrength: undefined,
      version: undefined,
      isBroken,
      isMixed,
      isEV,
      cert,
      qwac,
      certChain: certChainArray,
      certificateTransparency: undefined,
    };

    var version;
    try {
      retval.encryptionAlgorithm = secInfo.cipherName;
      retval.encryptionStrength = secInfo.secretKeyLength;
      version = secInfo.protocolVersion;
    } catch (e) {}

    switch (version) {
      case Ci.nsITransportSecurityInfo.SSL_VERSION_3:
        retval.version = "SSL 3";
        break;
      case Ci.nsITransportSecurityInfo.TLS_VERSION_1:
        retval.version = "TLS 1.0";
        break;
      case Ci.nsITransportSecurityInfo.TLS_VERSION_1_1:
        retval.version = "TLS 1.1";
        break;
      case Ci.nsITransportSecurityInfo.TLS_VERSION_1_2:
        retval.version = "TLS 1.2";
        break;
      case Ci.nsITransportSecurityInfo.TLS_VERSION_1_3:
        retval.version = "TLS 1.3";
        break;
    }

    // Select the status text to display for Certificate Transparency.
    // Since we do not yet enforce the CT Policy on secure connections,
    // we must not complain on policy discompliance (it might be viewed
    // as a security issue by the user).
    switch (secInfo.certificateTransparencyStatus) {
      case Ci.nsITransportSecurityInfo.CERTIFICATE_TRANSPARENCY_NOT_APPLICABLE:
      case Ci.nsITransportSecurityInfo
        .CERTIFICATE_TRANSPARENCY_POLICY_NOT_ENOUGH_SCTS:
      case Ci.nsITransportSecurityInfo
        .CERTIFICATE_TRANSPARENCY_POLICY_NOT_DIVERSE_SCTS:
        retval.certificateTransparency = null;
        break;
      case Ci.nsITransportSecurityInfo
        .CERTIFICATE_TRANSPARENCY_POLICY_COMPLIANT:
        retval.certificateTransparency = "Compliant";
        break;
    }

    return retval;
  },

  // Find the secureBrowserUI object (if present)
  _getSecurityUI() {
    if (window.opener.gBrowser) {
      return window.opener.gBrowser.securityUI;
    }
    return null;
  },

  async _updateSiteDataInfo() {
    // Save site data info for deleting.
    this.siteData = await SiteDataManager.getSite(this.uri.host);

    let clearSiteDataButton = document.getElementById(
      "security-clear-sitedata"
    );
    let siteDataLabel = document.getElementById(
      "security-privacy-sitedata-value"
    );

    if (!this.siteData) {
      document.l10n.setAttributes(siteDataLabel, "security-site-data-no");
      clearSiteDataButton.setAttribute("disabled", "true");
      return;
    }

    let { usage } = this.siteData;
    if (usage > 0) {
      let size = DownloadUtils.convertByteUnits(usage);
      if (this.siteData.cookies.length) {
        document.l10n.setAttributes(
          siteDataLabel,
          "security-site-data-cookies",
          { value: size[0], unit: size[1] }
        );
      } else {
        document.l10n.setAttributes(siteDataLabel, "security-site-data-only", {
          value: size[0],
          unit: size[1],
        });
      }
    } else {
      // We're storing cookies, else getSite would have returned null.
      document.l10n.setAttributes(
        siteDataLabel,
        "security-site-data-cookies-only"
      );
    }

    clearSiteDataButton.removeAttribute("disabled");
  },

  /**
   * Clear Site Data and Cookies
   */
  clearSiteData() {
    if (this.siteData) {
      let { baseDomain } = this.siteData;
      if (SiteDataManager.promptSiteDataRemoval(window, [baseDomain])) {
        SiteDataManager.remove(baseDomain).then(() =>
          this._updateSiteDataInfo()
        );
      }
    }
  },

  /**
   * Open the login manager window
   */
  viewPasswords() {
    LoginHelper.openPasswordManager(window, {
      filterString: this.windowInfo.hostName,
      entryPoint: "Pageinfo",
    });
  },
};

async function securityOnLoad(uri, windowInfo, browsingContext) {
  await security.init(uri, windowInfo, browsingContext);

  let info = security.securityInfo;
  if (
    !info ||
    (uri.scheme === "about" && !uri.spec.startsWith("about:certerror"))
  ) {
    document.getElementById("securityTab").hidden = true;
    return;
  }
  document.getElementById("securityTab").hidden = false;

  /* Set Identity section text */
  setText("security-identity-domain-value", windowInfo.hostName);

  var validity;
  if (info.cert && !info.isBroken) {
    validity = info.cert.validity.notAfterLocalDay;

    // Try to pull out meaningful values.  Technically these fields are optional
    // so we'll employ fallbacks where appropriate.  The EV spec states that Org
    // fields must be specified for subject and issuer so that case is simpler.
    if (info.qwac) {
      setText("security-identity-owner-value", info.qwac.organization);
      setText("security-identity-verifier-value", info.qwac.issuerOrganization);
    } else if (info.isEV) {
      setText("security-identity-owner-value", info.cert.organization);
      setText("security-identity-verifier-value", info.cAName);
    } else {
      // Technically, a non-EV cert might specify an owner in the O field or not,
      // depending on the CA's issuing policies.  However we don't have any programmatic
      // way to tell those apart, and no policy way to establish which organization
      // vetting standards are good enough (that's what EV is for) so we default to
      // treating these certs as domain-validated only.
      document.l10n.setAttributes(
        document.getElementById("security-identity-owner-value"),
        "page-info-security-no-owner"
      );
      setText(
        "security-identity-verifier-value",
        info.cAName || info.cert.issuerCommonName || info.cert.issuerName
      );
    }
  } else {
    // We don't have valid identity credentials.
    document.l10n.setAttributes(
      document.getElementById("security-identity-owner-value"),
      "page-info-security-no-owner"
    );
    document.l10n.setAttributes(
      document.getElementById("security-identity-verifier-value"),
      "page-info-not-specified"
    );
  }

  if (validity) {
    setText("security-identity-validity-value", validity);
  } else {
    document.getElementById("security-identity-validity-row").hidden = true;
  }

  /* Manage the View Cert button*/
  var viewCert = document.getElementById("security-view-cert");
  viewCert.collapsed = !info.cert;

  let viewQWAC = document.getElementById("security-view-qwac");
  viewQWAC.collapsed = !info.qwac;

  /* Set Privacy & History section text */

  // Only show quota usage data for websites, not internal sites.
  if (uri.scheme == "http" || uri.scheme == "https") {
    SiteDataManager.updateSites().then(() => security._updateSiteDataInfo());
  } else {
    document.getElementById("security-privacy-sitedata-row").hidden = true;
  }

  if (await realmHasPasswords(uri)) {
    document.l10n.setAttributes(
      document.getElementById("security-privacy-passwords-value"),
      "saved-passwords-yes"
    );
  } else {
    document.l10n.setAttributes(
      document.getElementById("security-privacy-passwords-value"),
      "saved-passwords-no"
    );
  }

  document.l10n.setAttributes(
    document.getElementById("security-privacy-history-value"),
    "security-visits-number",
    { visits: previousVisitCount(windowInfo.hostName) }
  );

  /* Set the Technical Detail section messages */
  const header = document.getElementById("security-technical-shortform");
  const message1 = document.getElementById("security-technical-longform1");
  const message2 = document.getElementById("security-technical-longform2");
  const encryptionArgs = {
    algorithm: info.encryptionAlgorithm,
    keySize: info.encryptionStrength,
    protocol: info.version,
  };

  if (info.isBroken) {
    if (info.isMixed) {
      document.l10n.setAttributes(header, "security-technical-mixed-content");
      document.l10n.setAttributes(
        message1,
        "security-technical-mixed-content-description"
      );
    } else {
      document.l10n.setAttributes(
        header,
        "security-technical-broken-encryption",
        encryptionArgs
      );
      document.l10n.setAttributes(message1, "security-technical-weak-cipher");
    }
    document.l10n.setAttributes(
      message2,
      "security-technical-no-encryption-transit"
    );
  } else if (info.encryptionStrength > 0) {
    document.l10n.setAttributes(
      header,
      "security-technical-encrypted",
      encryptionArgs
    );
    document.l10n.setAttributes(message1, "security-technical-encrypted-page");
    document.l10n.setAttributes(
      message2,
      "security-technical-encrypted-description"
    );
  } else {
    document.l10n.setAttributes(header, "security-technical-no-encryption");
    if (windowInfo.hostName != null) {
      document.l10n.setAttributes(
        message1,
        "security-technical-no-encryption-website",
        { website: windowInfo.hostName }
      );
    } else {
      document.l10n.setAttributes(
        message1,
        "security-technical-no-encryption-page"
      );
    }
    document.l10n.setAttributes(
      message2,
      "security-technical-no-encryption-transit"
    );
  }

  const ctStatus = document.getElementById(
    "security-technical-certificate-transparency"
  );
  if (info.certificateTransparency == "Compliant") {
    ctStatus.hidden = false;
    document.l10n.setAttributes(ctStatus, "security-technical-ct-compliant");
  } else {
    ctStatus.hidden = true;
  }
}

function setText(id, value) {
  var element = document.getElementById(id);
  if (!element) {
    return;
  }
  if (element.localName == "input" || element.localName == "label") {
    element.value = value;
  } else {
    element.textContent = value;
  }
}

/**
 * Return true iff realm (proto://host:port) (extracted from uri) has
 * saved passwords
 */
async function realmHasPasswords(uri) {
  return (await Services.logins.countLoginsAsync(uri.prePath, "", "")) > 0;
}

/**
 * Return the number of previous visits recorded for host before today.
 *
 * @param host - the domain name to look for in history
 */
function previousVisitCount(host) {
  if (!host) {
    return 0;
  }

  var historyService = Cc[
    "@mozilla.org/browser/nav-history-service;1"
  ].getService(Ci.nsINavHistoryService);

  var options = historyService.getNewQueryOptions();
  options.resultType = options.RESULTS_AS_VISIT;

  // Search for visits to this host before today
  var query = historyService.getNewQuery();
  query.endTimeReference = query.TIME_RELATIVE_TODAY;
  query.endTime = 0;
  query.domain = host;

  var result = historyService.executeQuery(query, options);
  result.root.containerOpen = true;
  var cc = result.root.childCount;
  result.root.containerOpen = false;
  return cc;
}
