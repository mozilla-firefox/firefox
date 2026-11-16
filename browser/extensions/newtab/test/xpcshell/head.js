/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* exported assertNewTabResourceMapping */

/**
 * This head.js file is shared between the browser/extensions/newtab xpcshell
 * tests as well as browser/components/newtab xpcshell tests.
 */

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonManagerPrivate: "resource://gre/modules/AddonManager.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

do_get_profile();

// The following initializations are necessary in order to install the newtab
// built-in addon, if we're configured to do so.
ExtensionTestUtils.init(this);
AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();

/**
 * Finds the built-in newtab addon code in the runtime environment directory
 * and then installs it.
 *
 * @returns {Promise<undefined>}
 *   Resolves once the addon has been installed.
 */
async function loadExtension() {
  const scopes = AddonManager.SCOPE_PROFILE | AddonManager.SCOPE_APPLICATION;
  Services.prefs.setIntPref("extensions.enabledScopes", scopes);

  const EXTENSION_ID = "newtab@mozilla.org";
  const builtinsConfig = await fetch(
    "chrome://browser/content/built_in_addons.json"
  ).then(res => res.json());

  await AddonTestUtils.overrideBuiltIns({
    system: [],
    builtins: builtinsConfig.builtins.filter(
      entry => entry.addon_id === EXTENSION_ID
    ),
  });

  await AddonTestUtils.promiseShutdownManager({
    clearOverrides: false,
    clearL10nRegistry: false,
  });
  await AddonTestUtils.promiseStartupManager();

  const addon = await AddonManager.getAddonByID(EXTENSION_ID);
  Assert.ok(addon, "Expect newtab addon to be found");
}

add_setup(async function head_initialize() {
  AddonTestUtils.createAppInfo(
    "xpcshell@tests.mozilla.org",
    "XPCShell",
    "1",
    "142"
  );
  await AddonTestUtils.promiseStartupManager();

  Services.prefs.setBoolPref("extensions.experiments.enabled", true);
  await loadExtension();
  AboutNewTab.init();
});

/**
 * Asserts that New Tab resource and chrome URI have been
 * mapped to the expected rootURI.

 * @param {string} [expectedRootURISpec]
 *   A optional root URI spec to derive the expected resource://newtab
 *   and chrome://newtab resource mapping to expect to have been registered.
 *   Defaults to the built-in newtab add-on root URI.
 */
function assertNewTabResourceMapping(expectedRootURISpec = null) {
  const resProto = Cc[
    "@mozilla.org/network/protocol;1?name=resource"
  ].getService(Ci.nsIResProtocolHandler);
  const chromeRegistry = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
    Ci.nsIChromeRegistry
  );
  const expectedSpec =
    expectedRootURISpec ??
    `${resProto.getSubstitution("builtin-addons").spec}newtab/`;
  Assert.equal(
    resProto.getSubstitution("newtab")?.spec,
    expectedSpec,
    "Got the expected resource://newtab/ substitution"
  );
  Assert.equal(
    chromeRegistry.convertChromeURL(
      Services.io.newURI("chrome://newtab/content/css/")
    )?.spec,
    `${expectedSpec}data/css/`,
    "Got the expected chrome://newtab/content substitution"
  );
}

/**
 * Verify that newtabAddonVersion ASRouter targeting attribute is matching
 * the expected newtab add-on version (which is expected to be set to either
 * the version of the trainhop add-on XPI or the buil-in version, based on
 * where the newtab resources are being currently being mapped to).
 *
 * @param {string} expectedNewtabVersion
 *   newtab add-on version expected to be returned by the ASRouterTargeting
 *   Environment targeting attribute named newtabAddonVersion.
 */
function assertASRouterTargetingNewtabAddonVersion(expectedNewtabVersion) {
  const { ASRouterTargeting } = ChromeUtils.importESModule(
    "resource:///modules/asrouter/ASRouterTargeting.sys.mjs"
  );
  Assert.equal(
    ASRouterTargeting.Environment.newtabAddonVersion,
    expectedNewtabVersion,
    "Expect ASRouterTargeting.Environment.newtabAddonVersion to be set to the expected add-on version"
  );
}
