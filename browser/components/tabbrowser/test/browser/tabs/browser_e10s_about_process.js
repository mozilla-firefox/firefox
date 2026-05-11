const CHROME = {
  id: "cb34538a-d9da-40f3-b61a-069f0b2cb9fb",
  path: "test-chrome",
  flags: 0,
};
const CANREMOTE = {
  id: "2480d3e1-9ce4-4b84-8ae3-910b9a95cbb3",
  path: "test-allowremote",
  flags: Ci.nsIAboutModule.URI_CAN_LOAD_IN_CHILD,
};
const MUSTREMOTE = {
  id: "f849cee5-e13e-44d2-981d-0fb3884aaead",
  path: "test-mustremote",
  flags: Ci.nsIAboutModule.URI_MUST_LOAD_IN_CHILD,
};
const CANPRIVILEGEDREMOTE = {
  id: "a04ffafe-6c63-4266-acae-0f4b093165aa",
  path: "test-canprivilegedremote",
  flags:
    Ci.nsIAboutModule.URI_MUST_LOAD_IN_CHILD |
    Ci.nsIAboutModule.URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS,
};
const MUSTEXTENSION = {
  id: "f7a1798f-965b-49e9-be83-ec6ee4d7d675",
  path: "test-mustextension",
  flags: Ci.nsIAboutModule.URI_MUST_LOAD_IN_EXTENSION_PROCESS,
};

const TEST_MODULES = [
  CHROME,
  CANREMOTE,
  MUSTREMOTE,
  CANPRIVILEGEDREMOTE,
  MUSTEXTENSION,
];

function AboutModule() {}

AboutModule.prototype = {
  newChannel() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },

  getURIFlags(aURI) {
    for (let module of TEST_MODULES) {
      if (aURI.pathQueryRef.startsWith(module.path)) {
        return module.flags;
      }
    }

    ok(false, "Called getURIFlags for an unknown page " + aURI.spec);
    return 0;
  },

  getIndexedDBOriginPostfix() {
    return null;
  },

  QueryInterface: ChromeUtils.generateQI(["nsIAboutModule"]),
};

var AboutModuleFactory = {
  createInstance(aIID) {
    return new AboutModule().QueryInterface(aIID);
  },

  QueryInterface: ChromeUtils.generateQI(["nsIFactory"]),
};

add_setup(async function () {
  SpecialPowers.setBoolPref(
    "browser.tabs.remote.separatePrivilegedMozillaWebContentProcess",
    true
  );
  let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
  for (let module of TEST_MODULES) {
    registrar.registerFactory(
      Components.ID(module.id),
      "",
      "@mozilla.org/network/protocol/about;1?what=" + module.path,
      AboutModuleFactory
    );
  }
});

registerCleanupFunction(() => {
  SpecialPowers.clearUserPref(
    "browser.tabs.remote.separatePrivilegedMozillaWebContentProcess"
  );
  let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
  for (let module of TEST_MODULES) {
    registrar.unregisterFactory(Components.ID(module.id), AboutModuleFactory);
  }
});

add_task(async function test_chrome() {
  test_url_for_process_types({
    url: "about:" + CHROME.path,
    chromeResult: true,
    webContentResult: false,
    privilegedAboutContentResult: false,
    privilegedMozillaContentResult: false,
    extensionProcessResult: false,
  });
});

add_task(async function test_any() {
  test_url_for_process_types({
    url: "about:" + CANREMOTE.path,
    chromeResult: true,
    webContentResult: true,
    privilegedAboutContentResult: true,
    privilegedMozillaContentResult: true,
    extensionProcessResult: true,
  });
});

add_task(async function test_remote() {
  test_url_for_process_types({
    url: "about:" + MUSTREMOTE.path,
    chromeResult: false,
    webContentResult: true,
    privilegedAboutContentResult: false,
    privilegedMozillaContentResult: false,
    extensionProcessResult: false,
  });
});

add_task(async function test_privileged_remote_true() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.remote.separatePrivilegedContentProcess", true]],
  });

  // This shouldn't be taken literally. We will always use the privleged about
  // content type if the URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS flag is enabled and
  // the pref is turned on.
  test_url_for_process_types({
    url: "about:" + CANPRIVILEGEDREMOTE.path,
    chromeResult: false,
    webContentResult: false,
    privilegedAboutContentResult: true,
    privilegedMozillaContentResult: false,
    extensionProcessResult: false,
  });
});

add_task(async function test_privileged_remote_false() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.remote.separatePrivilegedContentProcess", false]],
  });

  // This shouldn't be taken literally. We will always use the privleged about
  // content type if the URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS flag is enabled and
  // the pref is turned on.
  test_url_for_process_types({
    url: "about:" + CANPRIVILEGEDREMOTE.path,
    chromeResult: false,
    webContentResult: true,
    privilegedAboutContentResult: false,
    privilegedMozillaContentResult: false,
    extensionProcessResult: false,
  });
});

add_task(async function test_extension() {
  test_url_for_process_types({
    url: "about:" + MUSTEXTENSION.path,
    chromeResult: false,
    webContentResult: false,
    privilegedAboutContentResult: false,
    privilegedMozillaContentResult: false,
    extensionProcessResult: true,
  });
});
