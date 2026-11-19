// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
"use strict";

add_task(async function test_remote_process_given_empty_profile_path() {
  let libraryFile = Services.dirsvc.get("CurWorkD", Ci.nsIFile);
  libraryFile.append("pkcs11testmodule");
  libraryFile.append(ctypes.libraryName("pkcs11testmodule"));
  ok(libraryFile.exists(), "The pkcs11testmodule file should exist");

  let emptyPathDirectory = Cc["@mozilla.org/file/local;1"].createInstance(
    Ci.nsIFile
  );

  const directoryServiceProvider = {
    getFile(prop, persistent) {
      persistent.value = false;
      if (prop == "ProfD") {
        return emptyPathDirectory.clone();
      }
      return null;
    },
    QueryInterface: ChromeUtils.generateQI([Ci.nsIDirectoryServiceProvider]),
  };
  Services.dirsvc.registerProvider(directoryServiceProvider);

  let moduleDB = Cc["@mozilla.org/security/pkcs11moduledb;1"].getService(
    Ci.nsIPKCS11ModuleDB
  );

  let testClientCertificate = await findCertByCommonName("client cert rsa");
  ok(
    !testClientCertificate,
    "shouldn't be able to find the test client certificate"
  );

  let testModule = await findModuleByName(moduleDB, "PKCS11 Test Module");
  equal(testModule, null, "should not be able to find test module");

  // Loading the test module should succeed, but adding it to the permanent
  // database fails. Thus, the call to `addModule` should fail, but the module
  // will be loaded temporarily (i.e. for this session).
  let threw = false;
  try {
    await moduleDB.addModule("PKCS11 Test Module", libraryFile.path, 0, 0);
  } catch (_) {
    threw = true;
  }
  ok(threw, "addModule should fail");
  testModule = await findModuleByName(moduleDB, "PKCS11 Test Module");
  notEqual(
    testModule,
    null,
    "addModule threw, but the module should be loaded temporarily"
  );
  let testSlot = findSlotByName(testModule, "Test PKCS11 Slot");
  notEqual(testSlot, null, "should be able to find 'Test PKCS11 Slot'");
  equal(
    testSlot.tokenName,
    "Test PKCS11 Tokeñ Label",
    "Actual and expected token name should match"
  );
});
