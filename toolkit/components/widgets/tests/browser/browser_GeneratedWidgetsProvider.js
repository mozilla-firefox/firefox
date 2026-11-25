/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global browser */

const { GeneratedWidgetsProvider, widgetsDirectory } =
  ChromeUtils.importESModule(
    "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
  );
ChromeUtils.defineESModuleGetters(this, {
  AddonTestUtils: "resource://testing-common/AddonTestUtils.sys.mjs",
  ExtensionTestCommon: "resource://testing-common/ExtensionTestCommon.sys.mjs",
});

const ENABLED_PREF = "extensions.widgets.generated.enabled";
const GENERATED_DIR = widgetsDirectory();
const SCRATCH_DIR = PathUtils.join(GENERATED_DIR, "scratch");

const RELOADING_FILES = {
  "widget.html": '<!doctype html><script src="widget.js"></script>',
  "widget.js": ExtensionTestCommon.serializeScript(() => {
    if (location.hash === "#reload") {
      browser.runtime.reload();
    }
  }),
};

const UNINSTALL_URL_FILES = {
  "widget.html":
    '<!doctype html><script type="module" src="widget.js"></script>',
  "widget.js": ExtensionTestCommon.serializeScript(async () => {
    await browser.runtime.setUninstallURL("https://example.com/");
    document.dispatchEvent(new CustomEvent("uninstall-url-set"));
  }),
};

async function install(files) {
  await IOUtils.makeDirectory(SCRATCH_DIR);
  let path = await IOUtils.createUniqueFile(SCRATCH_DIR, "widget.zip");
  let file = ExtensionTestCommon.generateZipFile(
    {
      ...files,
      "widget.json": JSON.stringify({
        name: "Test widget",
        prompt: "A test widget",
      }),
    },
    "generated-widget.zip"
  );
  await IOUtils.move(file.path, path);
  let { id } = await GeneratedWidgetsProvider.install(path);
  return id;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: [[ENABLED_PREF, true]] });

  registerCleanupFunction(async () => {
    await GeneratedWidgetsProvider.shutdown();
    await IOUtils.remove(GENERATED_DIR, {
      recursive: true,
      ignoreAbsent: true,
    });
  });
});

add_task(async function test_reload_restarts_the_widget() {
  let id = await install(RELOADING_FILES);
  let policy = WebExtensionPolicy.getByID(id);
  let host = policy.mozExtensionHostname;
  await (await AddonManager.getAddonByID(id)).reload();
  let reloaded = WebExtensionPolicy.getByID(id);
  Assert.ok(reloaded?.active, "Widget restarted");
  Assert.notEqual(reloaded.extension, policy.extension, "New extension");
  Assert.equal(reloaded.mozExtensionHostname, host, "Origin kept");

  let restarted = AddonTestUtils.promiseWebExtensionStartup(id);
  let tab = BrowserTestUtils.addTab(
    gBrowser,
    reloaded.getURL("widget.html#reload")
  );
  Assert.notEqual(
    await restarted,
    reloaded.extension,
    "runtime.reload() started a new extension"
  );
  if (tab.isConnected) {
    BrowserTestUtils.removeTab(tab);
  }
  Assert.ok((await AddonManager.getAddonByID(id))?.isActive, "Add-on active");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_uninstall_opens_no_uninstall_url() {
  let id = await install(UNINSTALL_URL_FILES);
  let tab = BrowserTestUtils.addTab(
    gBrowser,
    WebExtensionPolicy.getByID(id).getURL("widget.html")
  );
  await BrowserTestUtils.waitForContentEvent(
    tab.linkedBrowser,
    "uninstall-url-set",
    true,
    null,
    true
  );
  BrowserTestUtils.removeTab(tab);
  Assert.equal(
    WebExtensionPolicy.getByID(id).extension.uninstallURL,
    "https://example.com/",
    "Uninstall URL set"
  );
  let tabs = gBrowser.tabs.length;
  await GeneratedWidgetsProvider.uninstall(id);
  Assert.equal(gBrowser.tabs.length, tabs, "Tabs");
});
