const { nsIAboutModule } = Ci;

// Self-registered at runtime rather than via components.conf -- see the
// comment at the top of SiteAllowlist.sys.mjs, which imports this module.
export const AboutAllowlist = {
  classID: Components.ID("{89d7b204-6854-4319-b01d-ce0c4aef274a}"),
  contractID: "@mozilla.org/network/protocol/about;1?what=allowlist",

  QueryInterface: ChromeUtils.generateQI([nsIAboutModule]),

  createInstance(iid) {
    return this.QueryInterface(iid);
  },

  newChannel(_uri, loadInfo) {
    const chan = Services.io.newChannelFromURIWithLoadInfo(
      Services.io.newURI(
        "chrome://browser/content/allowlist/aboutAllowlist.html"
      ),
      loadInfo
    );
    chan.owner = Services.scriptSecurityManager.getSystemPrincipal();
    return chan;
  },

  getURIFlags() {
    // No URI_SAFE_FOR_UNTRUSTED_CONTENT: this must run at full chrome
    // privilege so its script can import SiteAllowlist.sys.mjs directly.
    return nsIAboutModule.ALLOW_SCRIPT | nsIAboutModule.IS_SECURE_CHROME_UI;
  },

  getChromeURI(_uri) {
    return Services.io.newURI(
      "chrome://browser/content/allowlist/aboutAllowlist.html"
    );
  },
};

const registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
if (!registrar.isContractIDRegistered(AboutAllowlist.contractID)) {
  registrar.registerFactory(
    AboutAllowlist.classID,
    "about:allowlist",
    AboutAllowlist.contractID,
    AboutAllowlist
  );
}
