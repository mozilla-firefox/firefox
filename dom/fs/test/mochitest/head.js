/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

async function require_module(id) {
  if (!require_module.moduleLoader) {
    const { ModuleLoader } =
      await import("/tests/dom/quota/test/modules/ModuleLoader.mjs");

    const base = window.location.href;

    const depth = "../../../../";

    const { Assert } = await import("/tests/dom/quota/test/modules/Assert.mjs");

    const { Utils } = await import("/tests/dom/quota/test/modules/Utils.mjs");

    const proto = {
      Assert,
      Cr: SpecialPowers.Cr,
      navigator,
      TextEncoder,
      Utils,
    };

    require_module.moduleLoader = new ModuleLoader(base, depth, proto);
  }

  return require_module.moduleLoader.require(id);
}

async function run_test_in_worker(script) {
  const { runTestInWorker } =
    await import("/tests/dom/quota/test/modules/WorkerDriver.mjs");

  const base = window.location.href;

  const listener = {
    onOk(value, message) {
      ok(value, message);
    },
    onIs(a, b, message) {
      is(a, b, message);
    },
    onInfo(message) {
      info(message);
    },
  };

  await runTestInWorker(script, base, listener);
}

// XXX This can be removed once we use <profile>/storage. See bug 1798015.
async function removeAllEntries() {
  const root = await navigator.storage.getDirectory();
  for await (const value of root.values()) {
    root.removeEntry(value.name, { recursive: true });
  }
}

add_setup(async function () {
  const { setStoragePrefs, clearStoragesForOrigin } =
    await import("/tests/dom/quota/test/modules/StorageUtils.mjs");

  const optionalPrefsToSet = [
    ["dom.fs.enabled", true],
    ["dom.fs.testing", true],
  ];

  await setStoragePrefs(optionalPrefsToSet);

  SimpleTest.registerCleanupFunction(async function () {
    await removeAllEntries();

    await clearStoragesForOrigin(SpecialPowers.wrap(document).nodePrincipal);
  });
});
