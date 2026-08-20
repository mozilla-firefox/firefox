/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var manifests = [do_get_file("data/test_data_protocol_registration.manifest")];
registerManifests(manifests);

function run_test() {
  const uuidGenerator = Services.uuid;

  let { newAppInfo } = ChromeUtils.importESModule(
    "resource://testing-common/AppInfo.sys.mjs"
  );
  let XULAppInfo = newAppInfo({
    name: "XPCShell",
    ID: "{39885e5f-f6b4-4e2a-87e5-6259ecf79011}",
    version: "5",
    platformVersion: "1.9",
  });

  let XULAppInfoFactory = {
    // These two are used when we register all our factories (and unregister)
    CID: uuidGenerator.generateUUID(),
    scheme: "XULAppInfo",
    contractID: XULAPPINFO_CONTRACTID,
    createInstance(iid) {
      return XULAppInfo.QueryInterface(iid);
    },
  };

  // Add our XULAppInfo factory
  let factories = [XULAppInfoFactory];
  let old_factories = [];
  let old_factories_inds = [];

  let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);

  // Register our factories
  for (let i = 0; i < factories.length; i++) {
    let factory = factories[i];

    // Make sure the class ID has not already been registered
    if (!registrar.isCIDRegistered(factory.CID)) {
      // Check to see if a contract was already registered and
      // register it if it is not. Otherwise, store the previous one
      // to be restored later and register the new one.
      if (registrar.isContractIDRegistered(factory.contractID)) {
        dump(
          factory.scheme +
            " is already registered. Storing currently registered object for restoration later."
        );
        old_factories.push({
          CID: registrar.contractIDToCID(factory.contractID),
        });
        old_factories_inds.push(true);
      } else {
        dump(factory.scheme + " has never been registered. Registering...");
        old_factories.push({ CID: "" });
        old_factories_inds.push(false);
      }

      registrar.registerFactory(
        factory.CID,
        "test-" + factory.scheme,
        factory.contractID,
        factory
      );
    } else {
      do_throw("CID " + factory.CID + " has already been registered!");
    }
  }

  // Check for new chrome
  let cr = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
    Ci.nsIChromeRegistry
  );
  cr.checkForNewChrome();

  // Check that our override worked
  let expectedURI = "data:application/xhtml+xml,";
  let sourceURI = "chrome://good-package/content/test.xul";
  try {
    sourceURI = Services.io.newURI(sourceURI);
    // this throws for packages that are not registered
    let uri = cr.convertChromeURL(sourceURI).spec;

    Assert.equal(expectedURI, uri);
  } catch (e) {
    dump(e + "\n");
    do_throw("Should have registered our URI!");
  }

  // Unregister our factories so we do not leak
  for (let i = 0; i < factories.length; i++) {
    let factory = factories[i];
    let ind = old_factories_inds[i];
    registrar.unregisterFactory(factory.CID, factory);

    if (ind) {
      let old_factory = old_factories[i];
      registrar.registerFactory(
        old_factory.CID,
        factory.scheme,
        factory.contractID,
        null
      );
    }
  }
}
