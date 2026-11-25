/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global browser */

const ENABLED_PREF = "extensions.widgets.generated.enabled";
Services.prefs.setBoolPref(ENABLED_PREF, true);
Services.prefs.setIntPref("dom.ipc.keepProcessesAlive.extension", 1);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

ExtensionTestUtils.init(this);
AddonTestUtils.init(this);
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "42",
  "42"
);

const { AddonManager } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);
const { GeneratedWidgetsProvider, widgetsDirectory } =
  ChromeUtils.importESModule(
    "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
  );
const { GeneratedWidgetExtension } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/GeneratedWidgetExtension.sys.mjs"
);
const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);
const { StartupCache } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionParent.sys.mjs"
);
const { ExtensionTestCommon } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionTestCommon.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);

const ID_SUFFIX = "@generated-widget.mozilla.org";
const GENERATED_DIR = widgetsDirectory();
const STORE = PathUtils.join(GENERATED_DIR, "widgets.json.mozlz4");
const SCRATCH_DIR = PathUtils.join(GENERATED_DIR, "scratch");
const NAME = "Test widget";
const PROMPT = "A test widget";

const FILES = {
  "widget.html": "<!doctype html><title>Test widget</title>",
};

const STORING_FILES = {
  "widget.html":
    '<!doctype html><script type="module" src="widget.js"></script>',
  "widget.js": ExtensionTestCommon.serializeScript(async () => {
    let { saved } = await browser.storage.local.get("saved");
    if (saved === undefined) {
      await browser.storage.local.set({ saved: true });
    }
    browser.test.sendMessage("widget-page-loaded", saved !== undefined);
  }),
};

const API_FILES = {
  "widget.html":
    '<!doctype html><script type="module" src="widget.js"></script>',
  "widget.js": ExtensionTestCommon.serializeScript(async () => {
    browser.test.sendMessage(
      "widget-page-loaded",
      await browser.management.getSelf()
    );
  }),
};

function widgetJSON({ name = NAME, prompt = PROMPT, ...extra } = {}) {
  return JSON.stringify({ name, prompt, ...extra });
}

async function stage(files) {
  await IOUtils.makeDirectory(SCRATCH_DIR);
  let path = await IOUtils.createUniqueFile(SCRATCH_DIR, "widget.zip");
  let file = ExtensionTestCommon.generateZipFile(files, "generated-widget.zip");
  await IOUtils.move(file.path, path);
  return path;
}

function writePackage({ files = FILES, ...widget } = {}) {
  return stage({ ...files, "widget.json": widgetJSON(widget) });
}

async function install(widget) {
  let { id } = await GeneratedWidgetsProvider.install(
    await writePackage(widget)
  );
  return id;
}

async function loadWidgetPage(widget) {
  widget.attachExtension(WebExtensionPolicy.getByID(widget.id).extension);
  let loaded = widget.awaitMessage("widget-page-loaded");
  let contentPage = await ExtensionTestUtils.loadContentPage(
    widget.extension.getURL("widget.html"),
    { extension: widget }
  );
  try {
    return await loaded;
  } finally {
    await contentPage.close();
  }
}

function zipPath(id, version = "1") {
  return PathUtils.join(GENERATED_DIR, `${id}-${version}.zip`);
}

function scratchFiles() {
  return IOUtils.getChildren(SCRATCH_DIR, {
    ignoreAbsent: true,
  });
}

async function packageFiles() {
  let children = await IOUtils.getChildren(GENERATED_DIR, {
    ignoreAbsent: true,
  });
  return children.filter(path => path.endsWith(".zip"));
}

function extensionSitePermissions() {
  return Services.perms.all
    .filter(perm => perm.principal.schemeIs("moz-extension"))
    .map(perm => `${perm.principal.origin} ${perm.type}`);
}

function knownUUIDs() {
  return JSON.parse(
    Services.prefs.getStringPref("extensions.webextensions.uuids", "{}")
  );
}

function readStore() {
  return IOUtils.readJSON(STORE, { decompress: true });
}

function writeStore(entries) {
  return IOUtils.writeJSON(STORE, entries, { compress: true });
}

async function storedVersion(id) {
  return (await readStore())[id]?.version;
}

async function restart(whileStopped = () => {}) {
  await GeneratedWidgetsProvider.shutdown();
  await whileStopped();
  await GeneratedWidgetsProvider.startup();
}

async function writeZip(id, files, version) {
  let path = zipPath(id, version);
  let zip = new FileUtils.File(path);
  Services.obs.notifyObservers(zip, "flush-cache-entry");
  let file = ExtensionTestCommon.generateZipFile(files, "generated-widget.zip");
  await IOUtils.makeDirectory(PathUtils.parent(path));
  await IOUtils.move(file.path, path);
  Services.obs.notifyObservers(zip, "flush-cache-entry");
}

function setExtensionSettings(settings) {
  return EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: { ExtensionSettings: settings },
  });
}

async function resetPolicies() {
  await setExtensionSettings({ "*": { installation_mode: "allowed" } });
  await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
}

async function assertStopped(id, label) {
  Assert.ok(!WebExtensionPolicy.getByID(id), `${label}: extension stopped`);
  Assert.equal(
    (await AddonManager.getAddonByID(id))?.isActive,
    false,
    `${label}: add-on inactive`
  );
  Assert.ok(await IOUtils.exists(zipPath(id)), `${label}: package kept`);
}

add_setup(async function () {
  Services.fog.initializeFOG();
  await AddonTestUtils.promiseStartupManager();
});

add_task(async function test_concurrent_installs() {
  let installing = [];
  let installed = [];
  let bothInstalling = AddonTestUtils.promiseAddonEvent(
    "onInstalling",
    addon => {
      if (addon.type == "generatedwidget") {
        installing.push(addon);
      }
      return installing.length == 2;
    }
  );
  let bothInstalled = AddonTestUtils.promiseAddonEvent("onInstalled", addon => {
    if (addon.type == "generatedwidget") {
      installed.push(addon);
    }
    return installed.length == 2;
  });
  let addons = await Promise.all(
    [1, 2].map(async () =>
      GeneratedWidgetsProvider.install(await writePackage())
    )
  );
  await Promise.all([bothInstalling, bothInstalled]);
  let ids = addons.map(addon => addon.id).sort();
  Assert.equal(new Set(ids).size, 2, "Distinct ids");
  Assert.deepEqual(
    installing.map(addon => [addon.id, addon.name, addon.description]).sort(),
    ids.map(id => [id, NAME, PROMPT]),
    "Installing notifications"
  );
  Assert.deepEqual(
    installed.map(addon => addon.id).sort(),
    ids,
    "Each concurrent creation emits an install notification"
  );
  Assert.ok(
    installed.every(addon => addon.isActive && addon.name === NAME),
    "Install notifications contain running add-ons"
  );

  let stopping = GeneratedWidgetsProvider.shutdown();
  let starting = GeneratedWidgetsProvider.startup();
  await stopping;
  await starting;
  for (let id of ids) {
    Assert.ok(
      WebExtensionPolicy.getByID(id)?.active,
      "A rapid restart preserves each concurrent installation"
    );
  }
  for (let id of ids) {
    await GeneratedWidgetsProvider.uninstall(id);
  }
});

add_task(async function test_install_listener_can_uninstall() {
  let id;
  let uninstall;
  let events = [];
  let listener = {
    onInstalled(addon) {
      if (addon.type === "generatedwidget") {
        id = addon.id;
        uninstall = addon.uninstall();
      }
    },
    onUninstalling(addon) {
      if (addon.id === id) {
        events.push("onUninstalling");
      }
    },
    onUninstalled(addon) {
      if (addon.id === id) {
        events.push("onUninstalled");
      }
    },
  };
  AddonManager.addAddonListener(listener);
  try {
    await GeneratedWidgetsProvider.install(await writePackage());
    Assert.ok(uninstall, "The install listener can request removal");
    await uninstall;
    Assert.deepEqual(
      events,
      ["onUninstalling", "onUninstalled"],
      "Uninstall notifications"
    );
    Assert.equal(await AddonManager.getAddonByID(id), null, "Widget removed");
    Assert.ok(!WebExtensionPolicy.getByID(id), "Extension stopped");
  } finally {
    AddonManager.removeAddonListener(listener);
  }
});

add_task(async function test_shutdown_reports_failures() {
  let id = await install();
  let extension = WebExtensionPolicy.getByID(id).extension;
  let shutdown = extension.shutdown;
  extension.shutdown = async () => {
    throw new Error("Test shutdown failure");
  };
  try {
    await Assert.rejects(
      GeneratedWidgetsProvider.shutdown(),
      error =>
        error.name === "AggregateError" &&
        error.errors[0].message === "Test shutdown failure",
      "The provider reports extension shutdown failures"
    );
    await GeneratedWidgetsProvider.startup();
    Assert.equal(
      WebExtensionPolicy.getByID(id).extension,
      extension,
      "Restart does not replace the extension that failed to stop"
    );
  } finally {
    extension.shutdown = shutdown;
    await GeneratedWidgetsProvider.shutdown();
    await GeneratedWidgetsProvider.startup();
    await GeneratedWidgetsProvider.uninstall(id);
  }
});

add_task(async function test_a_stored_api_grant_stops_the_widget() {
  let id = await install();
  let { principal } = WebExtensionPolicy.getByID(id).extension;
  await restart(() =>
    ExtensionPermissions.add(id, { permissions: ["geolocation"], origins: [] })
  );
  await assertStopped(id, "Stored API grant");
  Assert.equal(
    Services.perms.testPermissionFromPrincipal(principal, "geo"),
    Services.perms.UNKNOWN_ACTION,
    "Geolocation site permission"
  );
  await ExtensionPermissions.removeAll(id);
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_stored_origin_and_data_grants_stop_the_widget() {
  for (let [grant, label] of [
    [{ permissions: [], origins: ["https://example.com/*"] }, "Origin grant"],
    [
      { permissions: [], origins: [], data_collection: ["locationInfo"] },
      "Data collection grant",
    ],
  ]) {
    let id = await install();
    await restart(() => ExtensionPermissions.add(id, grant));
    await assertStopped(id, label);
    await ExtensionPermissions.removeAll(id);
    await GeneratedWidgetsProvider.uninstall(id);
  }
});

add_task(async function test_a_planted_startup_cache_entry_is_ignored() {
  let id = await install();
  let { extension } = WebExtensionPolicy.getByID(id);
  let planted = {
    id,
    type: extension.type,
    manifest: { ...extension.manifest, name: "Planted widget" },
    permissions: new Set([...extension.permissions, "tabs"]),
    originPermissions: extension.allowedOrigins.patterns.map(
      pattern => pattern.pattern
    ),
    dataCollectionPermissions: extension.dataCollectionPermissions,
    apiNames: extension.apiNames,
    dependencies: extension.dependencies,
    contentScripts: [],
    webAccessibleResources: [],
    modules: null,
    schemaURLs: null,
    originControls: false,
  };
  await restart(() =>
    StartupCache.manifests.set(
      [id, "1", Services.locale.appLocaleAsBCP47],
      planted
    )
  );
  let policy = WebExtensionPolicy.getByID(id);
  Assert.ok(policy?.active, "Widget started");
  Assert.equal(policy.extension.manifest.name, NAME, "Manifest name");
  Assert.ok(!policy.hasPermission("tabs"), "Planted permission");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_a_stopped_widget_can_be_uninstalled() {
  let stopped = await install();
  let untouched = await install();
  await restart(() =>
    ExtensionPermissions.add(stopped, { permissions: ["tabs"], origins: [] })
  );
  await assertStopped(stopped, "Stored API grant");
  Assert.ok(WebExtensionPolicy.getByID(untouched)?.active, "Untouched widget");

  let uninstalled = AddonTestUtils.promiseAddonEvent(
    "onUninstalled",
    addon => addon.id == stopped
  );
  await GeneratedWidgetsProvider.uninstall(stopped);
  let [uninstalledAddon] = await uninstalled;
  Assert.deepEqual(
    { name: uninstalledAddon.name, description: uninstalledAddon.description },
    { name: NAME, description: PROMPT },
    "Uninstalled add-on"
  );
  Assert.ok(!(await IOUtils.exists(zipPath(stopped))), "Package deleted");
  await ExtensionPermissions.removeAll(stopped);
  await GeneratedWidgetsProvider.uninstall(untouched);
});

add_task(async function test_a_widget_is_an_unprivileged_addon() {
  let id = await install();
  let policy = WebExtensionPolicy.getByID(id);
  Assert.deepEqual(
    policy.permissions.toSorted(),
    ["idle", "storage", "unlimitedStorage"],
    "Permissions"
  );
  Assert.ok(!policy.isPrivileged, "Unprivileged");
  Assert.ok(!policy.privateBrowsingAllowed, "Private browsing disallowed");
  Assert.ok(policy.extension.isHidden, "Hidden");

  let addon = await AddonManager.getAddonByID(id);
  Assert.equal(addon?.type, "generatedwidget", "Add-on type");
  Assert.equal(addon.version, "1", "Version");
  Assert.equal(await AddonManager.getAddonByID(id), addon, "Same add-on");
  Assert.equal(await storedVersion(id), "1", "Stored version");
  Assert.greater(addon.installDate.getTime(), 0, "Install date");
  Assert.deepEqual(
    (await AddonManager.getAddonsByTypes(["generatedwidget"])).map(
      widget => widget.id
    ),
    [id],
    "Widget list"
  );
  for (let [types, message] of [
    [null, "Out of all add-ons"],
    [["extension"], "Out of the extension list"],
  ]) {
    Assert.ok(
      !(await AddonManager.getAddonsByTypes(types)).some(
        listed => listed.id == id
      ),
      message
    );
  }
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_uninstall_removes_the_widget() {
  let id = await install();
  let addon = await AddonManager.getAddonByID(id);
  await addon.uninstall();
  Assert.ok(!(await IOUtils.exists(zipPath(id))), "Package deleted");
  Assert.ok(!(id in (await readStore())), "Store entry");
  Assert.ok(!(id in knownUUIDs()), "UUID forgotten");
});

add_task(async function test_uninstall_records_no_telemetry() {
  let { AMTelemetry } = ChromeUtils.importESModule(
    "resource://gre/modules/AddonManager.sys.mjs"
  );
  Assert.ok(AMTelemetry.telemetrySetupDone, "AMTelemetry listening");
  Services.fog.testResetFOG();
  let id = await install();
  await (await AddonManager.getAddonByID(id)).uninstall();
  Assert.equal(
    Glean.addonsManager.manage.testGetValue(),
    null,
    "No manage events"
  );
});

add_task(async function test_storage_survives_a_restart() {
  let id = await install({ files: STORING_FILES });
  let widget = ExtensionTestUtils.expectExtension(id);
  Assert.equal(await loadWidgetPage(widget), false, "Nothing saved yet");

  await AddonTestUtils.promiseRestartManager();
  await widget.awaitStartup();
  Assert.equal(await loadWidgetPage(widget), true, "Saved state restored");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_the_pref_stops_and_starts_widgets() {
  let id = await install();
  let widget = ExtensionTestUtils.expectExtension(id);

  Services.prefs.setBoolPref(ENABLED_PREF, false);
  await GeneratedWidgetsProvider.shutdown();
  Assert.ok(!WebExtensionPolicy.getByID(id), "Widget stopped");
  Assert.equal(await AddonManager.getAddonByID(id), null, "Hidden");

  Services.prefs.setBoolPref(ENABLED_PREF, true);
  await widget.awaitStartup();
  Assert.ok(await AddonManager.getAddonByID(id), "Listed again");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_an_update_keeps_the_origin_and_data() {
  let id = await install({ files: STORING_FILES });
  let widget = ExtensionTestUtils.expectExtension(id);
  Assert.equal(await loadWidgetPage(widget), false, "Nothing saved yet");
  let host = WebExtensionPolicy.getByID(id).mozExtensionHostname;
  let installed = AddonTestUtils.promiseAddonEvent(
    "onInstalled",
    addon => addon.id == id
  );
  await GeneratedWidgetsProvider.update(
    id,
    await writePackage({ name: "Updated widget", files: STORING_FILES })
  );
  let [installedAddon] = await installed;
  Assert.deepEqual(
    {
      id: installedAddon.id,
      name: installedAddon.name,
      version: installedAddon.version,
      isActive: installedAddon.isActive,
    },
    { id, name: "Updated widget", version: "2", isActive: true },
    "Install notification"
  );
  Assert.equal(
    WebExtensionPolicy.getByID(id).mozExtensionHostname,
    host,
    "Origin kept"
  );
  Assert.equal(await loadWidgetPage(widget), true, "Stored data kept");
  Assert.deepEqual(await scratchFiles(), [], "Scratch files");
  Assert.ok(!(await IOUtils.exists(zipPath(id))), "Previous package deleted");
  Assert.equal(await storedVersion(id), "2", "Stored version");

  await GeneratedWidgetsProvider.update(id, await writePackage());
  Assert.equal(await storedVersion(id), "3", "Next version");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_a_failed_update_restores_the_widget() {
  let id = await install();
  let digest = await IOUtils.computeHexDigest(zipPath(id), "sha256");
  await Assert.rejects(
    GeneratedWidgetsProvider.update(
      id,
      await writePackage({
        files: { ...FILES, "_locales/en/messages.json": "{}" },
      })
    ),
    /_locales/,
    "Update rejected"
  );
  Assert.equal(
    await IOUtils.computeHexDigest(zipPath(id), "sha256"),
    digest,
    "Package restored"
  );
  Assert.ok(WebExtensionPolicy.getByID(id)?.active, "Widget restarted");
  Assert.deepEqual(await scratchFiles(), [], "Scratch files");
  Assert.ok(!(await IOUtils.exists(zipPath(id, "2"))), "New package deleted");
  Assert.equal((await AddonManager.getAddonByID(id)).version, "1", "Version");
  Assert.equal(await storedVersion(id), "1", "Stored version");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_startup_keeps_unlisted_files() {
  let id = await install();
  let unlisted = `00000000-0000-4000-8000-00000000001b${ID_SUFFIX}`;
  await restart(async () => {
    await writeZip(id, FILES, "2");
    await writeZip(unlisted, FILES);
  });
  Assert.ok(WebExtensionPolicy.getByID(id)?.active, "Listed widget");
  Assert.equal(await storedVersion(id), "1", "Stored version");
  Assert.ok(!WebExtensionPolicy.getByID(unlisted), "Unlisted widget");
  Assert.ok(await IOUtils.exists(zipPath(id, "2")), "Unlisted version");
  Assert.ok(await IOUtils.exists(zipPath(unlisted)), "Unlisted package");
  await IOUtils.remove(zipPath(id, "2"));
  await IOUtils.remove(zipPath(unlisted));
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_a_missing_or_unreadable_store_deletes_nothing() {
  let id = await install();
  let store = await readStore();
  for (let [label, replace] of [
    ["Missing store", () => IOUtils.remove(STORE)],
    ["Corrupt store", () => IOUtils.writeUTF8(STORE, "corrupt")],
  ]) {
    await restart(replace);
    Assert.ok(!WebExtensionPolicy.getByID(id), `${label}: widget stopped`);
    Assert.ok(await IOUtils.exists(zipPath(id)), `${label}: package kept`);
    Assert.ok(id in knownUUIDs(), `${label}: UUID kept`);
  }
  await restart(() => writeStore(store));
  Assert.ok(WebExtensionPolicy.getByID(id)?.active, "Restored store");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_invalid_store_entries_are_dropped() {
  let id = await install();
  let store = await readStore();
  let entry = store[id];
  let fakeId = last => `00000000-0000-4000-8000-0000000000${last}${ID_SUFFIX}`;
  let invalid = [
    ["Uppercase UUID", fakeId("A1"), entry],
    ["Invalid suffix", "foo@bar", entry],
    ["Slash in the id", fakeId("a2/x"), entry],
    ["Missing version", fakeId("a3"), { ...entry, version: undefined }],
    ["Non-integer version", fakeId("a4"), { ...entry, version: "1.5" }],
    ["Path in the version", fakeId("a5"), { ...entry, version: "../x" }],
    ["Numeric version", fakeId("a6"), { ...entry, version: 1 }],
    ["String install date", fakeId("a7"), { ...entry, installDate: "1000" }],
    ["Missing update date", fakeId("a8"), { ...entry, updateDate: undefined }],
    ["Null entry", fakeId("a9"), null],
    ["Missing name", fakeId("aa"), { ...entry, name: undefined }],
    ["Numeric name", fakeId("ab"), { ...entry, name: 1 }],
    ["Missing prompt", fakeId("ac"), { ...entry, prompt: undefined }],
    ["Null prompt", fakeId("ad"), { ...entry, prompt: null }],
  ];
  let withPackage = invalid
    .map(([, badId]) => badId)
    .filter(badId => !badId.includes("/"));
  await restart(async () => {
    await writeStore({
      ...store,
      ...Object.fromEntries(
        invalid.map(([, badId, badEntry]) => [badId, badEntry])
      ),
    });
    for (let badId of withPackage) {
      await writeZip(badId, { ...FILES, "widget.json": widgetJSON() });
    }
  });
  Assert.equal(
    (await AddonManager.getAddonByID(id))?.version,
    "1",
    "Valid widget kept"
  );
  for (let [description, badId] of invalid) {
    Assert.ok(!WebExtensionPolicy.getByID(badId), description);
  }
  for (let badId of withPackage) {
    await IOUtils.remove(zipPath(badId));
  }
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_dates_come_from_the_store() {
  let id = await install();
  await restart(() =>
    writeStore({
      [id]: {
        version: "1",
        name: NAME,
        prompt: PROMPT,
        installDate: 1000,
        updateDate: 2000,
      },
    })
  );
  let addon = await AddonManager.getAddonByID(id);
  Assert.equal(addon.installDate.getTime(), 1000, "Install date");
  Assert.equal(addon.updateDate.getTime(), 2000, "Update date");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_the_name_and_prompt_come_from_the_store() {
  let id = await install();
  for (let [label, files] of [
    [
      "Rewritten widget.json",
      {
        ...FILES,
        "widget.json": widgetJSON({
          name: "Rewritten widget",
          prompt: "A rewritten widget",
        }),
      },
    ],
    ["No widget.json", FILES],
  ]) {
    await restart(() => writeZip(id, files));
    let { extension } = WebExtensionPolicy.getByID(id) ?? {};
    Assert.ok(extension, `${label}: widget started`);
    Assert.equal(extension.manifest.name, NAME, `${label}: name`);
    Assert.equal(
      extension.manifest.description,
      PROMPT,
      `${label}: description`
    );
  }
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_a_leftover_next_version_is_replaced() {
  let id = await install();
  await IOUtils.writeUTF8(zipPath(id, "2"), "");
  await GeneratedWidgetsProvider.update(id, await writePackage());
  Assert.equal(await storedVersion(id), "2", "Update");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_management_lists_the_widget_only_to_itself() {
  let id = await install({ files: API_FILES });
  let widget = ExtensionTestUtils.expectExtension(id);
  let self = await loadWidgetPage(widget);
  info(`management.getSelf(): ${JSON.stringify(self)}`);
  Assert.equal(self.id, id, "getSelf() id");
  Assert.equal(self.description, PROMPT, "getSelf() description");
  Assert.equal(self.type, "extension", "getSelf() type");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["management"] },
    async background() {
      browser.test.onMessage.addListener(async (message, widgetId) => {
        let errors = {};
        for (let [method, args] of [
          ["get", [widgetId]],
          ["setEnabled", [widgetId, false]],
        ]) {
          try {
            await browser.management[method](...args);
            errors[method] = null;
          } catch (e) {
            errors[method] = e.message;
          }
        }
        browser.test.sendMessage("call-methods:done", errors);
      });
      let addons = await browser.management.getAll();
      browser.test.sendMessage(
        "addons",
        addons.map(addon => [addon.id, addon.type])
      );
    },
  });
  await extension.startup();
  let addons = await extension.awaitMessage("addons");
  extension.sendMessage("call-methods", id);
  let errors = await extension.awaitMessage("call-methods:done");
  await extension.unload();
  Assert.deepEqual(
    errors,
    { get: `No such addon ${id}`, setEnabled: `No such addon ${id}` },
    "Same rejection as an unknown id"
  );
  info(`management.getAll(): ${JSON.stringify(addons)}`);
  Assert.ok(
    !addons.some(
      ([addonId, type]) => addonId == id || type == "generatedwidget"
    ),
    "Out of getAll()"
  );
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_management_events_skip_the_widget() {
  let observer = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["management"] },
    background() {
      let events = [];
      for (let name of ["onInstalled", "onUninstalled"]) {
        browser.management[name].addListener(info =>
          events.push([name, info.id])
        );
      }
      browser.test.onMessage.addListener(() => {
        browser.test.sendMessage("events", events);
      });
      browser.test.sendMessage("ready");
    },
  });
  await observer.startup();
  await observer.awaitMessage("ready");

  let id = await install();
  await GeneratedWidgetsProvider.uninstall(id);

  let sentinel = ExtensionTestUtils.loadExtension({
    useAddonManager: "temporary",
    manifest: {
      browser_specific_settings: { gecko: { id: "sentinel@test" } },
    },
  });
  await sentinel.startup();
  await sentinel.unload();

  observer.sendMessage("report");
  let events = await observer.awaitMessage("events");
  await observer.unload();
  Assert.deepEqual(
    events,
    [
      ["onInstalled", "sentinel@test"],
      ["onUninstalled", "sentinel@test"],
    ],
    "Only the sentinel caused management events"
  );
});

add_task(async function test_shutdown_skips_pending_launches() {
  let id = await install();
  await GeneratedWidgetsProvider.shutdown();
  let launch = sinon.spy(GeneratedWidgetExtension.prototype, "startup");
  GeneratedWidgetsProvider.startup();
  await GeneratedWidgetsProvider.shutdown();
  launch.restore();
  Assert.equal(launch.callCount, 0, "No launch");
  Assert.ok(!WebExtensionPolicy.getByID(id), "Widget stopped");
  await GeneratedWidgetsProvider.startup();
  Assert.ok(WebExtensionPolicy.getByID(id)?.active, "Widget restarted");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_uninstall_aborts_pending_work() {
  let id = await install();
  let path = await writePackage();
  let reload = GeneratedWidgetsProvider.reload(id);
  let update = GeneratedWidgetsProvider.update(id, path);
  await GeneratedWidgetsProvider.uninstall(id);
  await Assert.rejects(reload, /AbortError/, "Reload aborted");
  await Assert.rejects(update, /AbortError/, "Update aborted");
  Assert.ok(!WebExtensionPolicy.getByID(id), "Widget stopped");
  Assert.equal(await AddonManager.getAddonByID(id), null, "Uninstalled");
  Assert.deepEqual(await packageFiles(), [], "Packages deleted");
  await IOUtils.remove(path);
});

add_task(async function test_uninstall_stops_a_launching_widget() {
  let id = await install();
  let starting = Promise.withResolvers();
  let startup = sinon
    .stub(GeneratedWidgetExtension.prototype, "startup")
    .callsFake(function () {
      starting.resolve();
      return startup.wrappedMethod.call(this);
    });
  let reload = GeneratedWidgetsProvider.reload(id);
  await starting.promise;
  startup.restore();
  await GeneratedWidgetsProvider.uninstall(id);
  await Assert.rejects(reload, /AbortError/, "Reload aborted");
  Assert.ok(!WebExtensionPolicy.getByID(id), "Widget stopped");
  Assert.equal(await AddonManager.getAddonByID(id), null, "Uninstalled");
});

add_task(async function test_package_limits_at_rest() {
  let id = await install();
  await restart(() =>
    writeZip(id, {
      ...FILES,
      "widget.json": widgetJSON({ permissions: ["tabs"] }),
      "manifest.json": JSON.stringify({ permissions: ["tabs"] }),
    })
  );
  let policy = WebExtensionPolicy.getByID(id);
  Assert.ok(policy?.active, "Widget started");
  Assert.ok(!policy.hasPermission("tabs"), "Planted permission");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_install_checks_the_package() {
  let installing = [];
  let uninstalls = [];
  let listener = {
    onInstalling: addon => installing.push(addon.id),
    onUninstalling: addon => uninstalls.push(addon.id),
    onUninstalled: addon => uninstalls.push(addon.id),
  };
  AddonManager.addAddonListener(listener);
  try {
    for (let [label, files, error] of [
      ["No widget.json", FILES, /NS_ERROR_FILE_NOT_FOUND/],
      [
        "No name",
        { ...FILES, "widget.json": JSON.stringify({ prompt: PROMPT }) },
        /invalid widget\.json/,
      ],
      [
        "Prompt not a string",
        { ...FILES, "widget.json": JSON.stringify({ name: NAME, prompt: 1 }) },
        /invalid widget\.json/,
      ],
    ]) {
      await Assert.rejects(
        GeneratedWidgetsProvider.install(await stage(files)),
        error,
        `${label}: install rejected`
      );
    }
  } finally {
    AddonManager.removeAddonListener(listener);
  }
  Assert.deepEqual(await packageFiles(), [], "Packages");
  Assert.deepEqual(await readStore(), {}, "Store entries");
  Assert.deepEqual(installing, [], "No installing notifications");
  Assert.deepEqual(uninstalls, [], "No uninstall notifications");
});

add_task(async function test_install_truncates_the_name() {
  let name = "n".repeat(76);
  let id = await install({ name });
  let { manifest } = WebExtensionPolicy.getByID(id).extension;
  Assert.equal(manifest.name, name.slice(0, 75), "Name");
  Assert.equal((await readStore())[id].name, name, "Stored name");
  Assert.equal(
    (await AddonManager.getAddonByID(id)).name,
    manifest.name,
    "Add-on name"
  );
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(async function test_install_rejects_a_long_prompt() {
  await Assert.rejects(
    GeneratedWidgetsProvider.install(
      await writePackage({ prompt: "p".repeat(2001) })
    ),
    /description/,
    "Long prompt rejected"
  );
  Assert.deepEqual(await packageFiles(), [], "Packages");
  Assert.deepEqual(await readStore(), {}, "Store entries");
});

add_task(async function test_install_checks_the_manifest_keys() {
  let original = GeneratedWidgetExtension.prototype.initializeAddonTypeAndID;
  let stub = sinon
    .stub(GeneratedWidgetExtension.prototype, "initializeAddonTypeAndID")
    .callsFake(async function () {
      await original.call(this);
      this.rawManifest.builtin_widget = this.rawManifest.generated_widget;
    });
  try {
    await Assert.rejects(
      GeneratedWidgetsProvider.install(await writePackage()),
      /Unexpected property "builtin_widget"/,
      "builtin_widget rejected"
    );
  } finally {
    stub.restore();
  }
});

// Starting an extension adds site permissions for some of its API
// permissions, such as WebExtensions-unlimitedStorage and persistent-storage
// for unlimitedStorage. A failed install has to remove them.
add_task(async function test_a_failed_install_leaves_no_site_permissions() {
  let before = extensionSitePermissions();
  await Assert.rejects(
    GeneratedWidgetsProvider.install(
      await writePackage({
        files: { ...FILES, "_locales/en/messages.json": "{}" },
      })
    ),
    /_locales/,
    "Install rejected"
  );
  Assert.deepEqual(extensionSitePermissions(), before, "Site permissions");
});

add_task(async function test_install_honors_blocking_policy() {
  await setExtensionSettings({ "*": { installation_mode: "blocked" } });
  let path = await writePackage();
  await Assert.rejects(
    GeneratedWidgetsProvider.install(path),
    /blocked by enterprise policy/,
    "Install rejected"
  );
  Assert.ok(await IOUtils.exists(path), "File kept");
  await IOUtils.remove(path);
  await resetPolicies();
});

add_task(async function test_install_moves_the_file_into_place() {
  let path = await writePackage();
  let { id } = await GeneratedWidgetsProvider.install(path);
  Assert.ok(!(await IOUtils.exists(path)), "File moved");
  Assert.ok(await IOUtils.exists(zipPath(id)), "Package");
  await GeneratedWidgetsProvider.uninstall(id);
});

async function outsidePackage() {
  let path = await IOUtils.createUniqueFile(
    PathUtils.parent(PathUtils.profileDir),
    "generated-widget.zip"
  );
  registerCleanupFunction(() => IOUtils.remove(path, { ignoreAbsent: true }));
  let file = ExtensionTestCommon.generateZipFile(
    { ...FILES, "widget.json": widgetJSON() },
    "generated-widget.zip"
  );
  await IOUtils.move(file.path, path);
  return path;
}

add_task(async function test_install_needs_a_file_in_the_profile() {
  let path = await outsidePackage();
  await Assert.rejects(
    GeneratedWidgetsProvider.install(path),
    /not a file in the profile directory/,
    "Install rejected"
  );
  let id = await install();
  await Assert.rejects(
    GeneratedWidgetsProvider.update(id, path),
    /not a file in the profile directory/,
    "Update rejected"
  );
  Assert.ok(await IOUtils.exists(path), "File kept");
  await GeneratedWidgetsProvider.uninstall(id);
});

add_task(
  { skip_if: () => AppConstants.platform == "win" },
  async function test_install_resolves_symlinks() {
    await IOUtils.makeDirectory(SCRATCH_DIR);
    let link = PathUtils.join(SCRATCH_DIR, "link.zip");
    let ln = new FileUtils.File("/bin/ln");
    let process = Cc["@mozilla.org/process/util;1"].createInstance(
      Ci.nsIProcess
    );
    process.init(ln);
    let args = ["-s", await outsidePackage(), link];
    process.run(true, args, args.length);
    Assert.equal(process.exitValue, 0, "Link created");
    await Assert.rejects(
      GeneratedWidgetsProvider.install(link),
      /not a file in the profile directory/,
      "Install rejected"
    );
    await IOUtils.remove(link);
  }
);
