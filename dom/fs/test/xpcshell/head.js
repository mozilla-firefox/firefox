/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

async function require_module(id) {
  if (!require_module.moduleLoader) {
    const { ModuleLoader } = ChromeUtils.importESModule(
      "resource://testing-common/dom/quota/test/modules/ModuleLoader.sys.mjs"
    );

    const base = Services.io.newFileURI(do_get_file("")).spec;

    const depth = "../../../../";

    Cu.importGlobalProperties(["storage"]);

    const { Utils } = ChromeUtils.importESModule(
      "resource://testing-common/dom/quota/test/modules/Utils.sys.mjs"
    );

    const proto = {
      Assert,
      Cr,
      DOMException,
      navigator: {
        storage,
      },
      TextEncoder,
      Utils,
    };

    require_module.moduleLoader = new ModuleLoader(base, depth, proto);
  }

  return require_module.moduleLoader.require(id);
}

async function run_test_in_worker(script) {
  const { runTestInWorker } = ChromeUtils.importESModule(
    "resource://testing-common/dom/quota/test/modules/WorkerDriver.sys.mjs"
  );

  const base = "resource://testing-common/dom/fs/test/xpcshell/";

  const listener = {
    onOk(value, message) {
      ok(value, message);
    },
    onIs(a, b, message) {
      Assert.equal(a, b, message);
    },
    onInfo(message) {
      info(message);
    },
  };

  await runTestInWorker(script, base, listener);
}

add_setup(async function () {
  const {
    setStoragePrefs,
    clearStoragePrefs,
    clearStoragesForOrigin,
    resetStorage,
  } = ChromeUtils.importESModule(
    "resource://testing-common/dom/quota/test/modules/StorageUtils.sys.mjs"
  );

  const optionalPrefsToSet = [
    ["dom.fs.enabled", true],
    ["dom.fs.testing", true],
  ];

  setStoragePrefs(optionalPrefsToSet);

  registerCleanupFunction(async function () {
    const principal = Cc["@mozilla.org/systemprincipal;1"].createInstance(
      Ci.nsIPrincipal
    );

    await clearStoragesForOrigin(principal);

    Services.prefs.clearUserPref(
      "dom.quotaManager.temporaryStorage.fixedLimit"
    );

    await resetStorage();

    const optionalPrefsToClear = ["dom.fs.enabled", "dom.fs.testing"];

    clearStoragePrefs(optionalPrefsToClear);
  });

  do_get_profile();
});
