/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var manifests = [do_get_file("data/test_no_remote_registration.manifest")];
registerManifests(manifests);

function ProtocolHandler(aScheme) {
  this.scheme = aScheme;
}

ProtocolHandler.prototype = {
  allowPort: () => false,
  newChannel() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
  QueryInterface: ChromeUtils.generateQI(["nsIProtocolHandler"]),
};

var testProtocols = [
  // It doesn't matter if it has this flag - the only flag we accept is
  // URI_IS_LOCAL_RESOURCE.
  {
    scheme: "moz-protocol-ui-resource",
    flags: Ci.nsIProtocolHandler.URI_IS_UI_RESOURCE,
    shouldRegister: false,
  },
  // It doesn't matter if it has this flag - the only flag we accept is
  // URI_IS_LOCAL_RESOURCE.
  {
    scheme: "moz-protocol-local-file",
    flags: Ci.nsIProtocolHandler.URI_IS_LOCAL_FILE,
    shouldRegister: false,
  },
  // This clearly is non-local
  {
    scheme: "moz-protocol-loadable-by-anyone",
    flags: Ci.nsIProtocolHandler.URI_LOADABLE_BY_ANYONE,
    shouldRegister: false,
  },
  // This should always be last (unless we add more flags that are OK)
  {
    scheme: "moz-protocol-local-resource",
    flags: Ci.nsIProtocolHandler.URI_IS_LOCAL_RESOURCE,
    shouldRegister: true,
  },
];
function run_test() {
  const { newAppInfo } = ChromeUtils.importESModule(
    "resource://testing-common/AppInfo.sys.mjs"
  );
  let XULAppInfo = newAppInfo({
    name: "XPCShell",
    ID: "{39885e5f-f6b4-4e2a-87e5-6259ecf79011}",
    version: "5",
    platformVersion: "1.9",
  });

  const uuidGenerator = Services.uuid;

  let XULAppInfoFactory = {
    // These two are used when we register all our factories (and unregister)
    CID: uuidGenerator.generateUUID(),
    scheme: "XULAppInfo",
    contractID: "@mozilla.org/xre/app-info;1",
    createInstance(iid) {
      return XULAppInfo.QueryInterface(iid);
    },
  };

  for (let protocol of testProtocols) {
    Services.io.registerProtocolHandler(
      protocol.scheme,
      new ProtocolHandler(protocol.scheme),
      protocol.flags,
      -1
    );
  }

  let registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);

  // Register the XULAppInfoFactory
  // Make sure the class ID has not already been registered
  let old_factory = { CID: "" };
  if (!registrar.isCIDRegistered(XULAppInfoFactory.CID)) {
    // Check to see if a contract was already registered and
    // register it if it is not. Otherwise, store the previous one
    // to be restored later and register the new one.
    if (registrar.isContractIDRegistered(XULAppInfoFactory.contractID)) {
      dump(
        XULAppInfoFactory.scheme +
          " is already registered. Storing currently registered object for restoration later.\n"
      );
      old_factory.CID = registrar.contractIDToCID(XULAppInfoFactory.contractID);
    } else {
      dump(
        XULAppInfoFactory.scheme +
          " has never been registered. Registering...\n"
      );
    }

    registrar.registerFactory(
      XULAppInfoFactory.CID,
      "test-" + XULAppInfoFactory.scheme,
      XULAppInfoFactory.contractID,
      XULAppInfoFactory
    );
  } else {
    do_throw("CID " + XULAppInfoFactory.CID + " has already been registered!");
  }

  // Check for new chrome
  let cr = Cc["@mozilla.org/chrome/chrome-registry;1"].getService(
    Ci.nsIChromeRegistry
  );
  cr.checkForNewChrome();

  // See if our various things were able to register
  let registrationTypes = ["content", "locale", "skin", "override", "resource"];
  for (let i = 0; i < testProtocols.length; i++) {
    let protocol = testProtocols[i];
    for (let j = 0; j < registrationTypes.length; j++) {
      let type = registrationTypes[j];
      dump(
        "Testing protocol '" + protocol.scheme + "' with type '" + type + "'\n"
      );
      let expectedURI = protocol.scheme + "://foo/";
      let sourceURI = "chrome://" + protocol.scheme + "/" + type + "/";
      switch (type) {
        case "content":
          expectedURI += protocol.scheme + ".xul";
          break;
        case "locale":
          expectedURI += protocol.scheme + ".dtd";
          break;
        case "skin":
          expectedURI += protocol.scheme + ".css";
          break;
        case "override":
          sourceURI =
            "chrome://good-package/content/override-" +
            protocol.scheme +
            ".xul";
          break;
        case "resource":
          sourceURI = "resource://" + protocol.scheme + "/";
          break;
      }
      try {
        sourceURI = Services.io.newURI(sourceURI);
        let uri;
        if (type == "resource") {
          // resources go about a slightly different way than everything else
          let rph = Services.io
            .getProtocolHandler("resource")
            .QueryInterface(Ci.nsIResProtocolHandler);
          // this throws for packages that are not registered
          uri = rph.resolveURI(sourceURI);
        } else {
          // this throws for packages that are not registered
          uri = cr.convertChromeURL(sourceURI).spec;
        }

        if (protocol.shouldRegister) {
          Assert.equal(expectedURI, uri);
        } else {
          // Overrides will not throw, so we'll get to here.  We want to make
          // sure that the two strings are not the same in this situation.
          Assert.notEqual(expectedURI, uri);
        }
      } catch (e) {
        if (protocol.shouldRegister) {
          dump(e + "\n");
          do_throw(
            "Should have registered our URI for protocol " + protocol.scheme
          );
        }
      }
    }
  }

  // Unregister our protocol handlers so we do not leak
  for (let protocol of testProtocols) {
    Services.io.unregisterProtocolHandler(protocol.scheme);
  }

  // Unregister XULAppInfoFactory
  registrar.unregisterFactory(XULAppInfoFactory.CID, XULAppInfoFactory);
  if (old_factory.CID != "") {
    registrar.registerFactory(
      old_factory.CID,
      "",
      XULAppInfoFactory.contractID,
      null
    );
  }
}
