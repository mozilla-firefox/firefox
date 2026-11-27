/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { _WidgetManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs"
);

const SRC = "moz-extension://11111111-2222-3333-4444-555555555555/widget.html";
const EXTENSION_ID = "extension@mozilla.org";

function makeOptions(overrides = {}) {
  return {
    id: EXTENSION_ID,
    src: SRC,
    name: "Tally",
    sizes: ["medium"],
    ...overrides,
  };
}

function createManager() {
  Services.prefs.deleteBranch("extensions.widgets.");
  const manager = new _WidgetManager();
  registerCleanupFunction(() => manager.uninit());
  return manager;
}

add_task(async function test_register_and_unregister() {
  const manager = createManager();
  const options = makeOptions({
    name: "Tally",
    sizes: ["small", "medium"],
  });
  const widget = manager.register(options);

  Assert.equal(manager.getWidgetByID(EXTENSION_ID), widget, "Registered");
  Assert.equal(widget.id, EXTENSION_ID, "Id");
  Assert.equal(widget.src, SRC, "Src");
  Assert.equal(widget.name, "Tally", "Name");
  Assert.deepEqual(widget.sizes, ["small", "medium"], "Sizes");
  Assert.equal(manager.getWidgetByID("other@mozilla.org"), null, "Other");

  manager.unregister(EXTENSION_ID);
  Assert.equal(manager.getWidgetByID(EXTENSION_ID), null, "No widget");
  Assert.notEqual(manager.register(makeOptions()), widget, "New widget");
});

add_task(async function test_pref_keys() {
  const manager = createManager();
  const cases = [
    ["counter-widget@mozilla.org", "counter-widget"],
    ["Widget@Example.com", "widget_example_com"],
    [
      "{8F3A2C1E-0000-4000-8000-000000000000}",
      "_8f3a2c1e-0000-4000-8000-000000000000_",
    ],
  ];
  for (const [id, prefKey] of cases) {
    manager.register(makeOptions({ id })).enabled = true;
    Assert.ok(
      Services.prefs.prefHasUserValue(`extensions.widgets.${prefKey}.enabled`),
      id
    );
  }
});

add_task(async function test_register_rejects_a_second_widget() {
  const manager = createManager();
  manager.register(makeOptions());
  Assert.throws(
    () => manager.register(makeOptions()),
    /is already registered/,
    "Same extension"
  );

  manager.register(makeOptions({ id: "a.b@example.com" }));
  Assert.throws(
    () => manager.register(makeOptions({ id: "a_b@example.com" })),
    /is used by "a.b@example.com"/,
    "Taken pref key"
  );
  manager.register(
    makeOptions({ id: "c@example.com", prefBranch: "test.widget" })
  );
  Assert.throws(
    () =>
      manager.register(
        makeOptions({ id: "d@example.com", prefBranch: "test.widget" })
      ),
    /is used by "c@example.com"/,
    "Taken pref"
  );
});

add_task(async function test_register_requires_options() {
  const manager = createManager();
  for (const key of ["id", "src", "name", "sizes"]) {
    Assert.throws(
      () => manager.register(makeOptions({ [key]: undefined })),
      new RegExp(`"${key}" is required`),
      key
    );
  }
});

add_task(async function test_change_event() {
  const manager = createManager();
  const changes = [];
  const onChange = (event, id) => changes.push(id);
  manager.on("change", onChange);

  function assertChanges(expected, description) {
    Assert.deepEqual(changes.splice(0), expected, description);
  }

  manager.register(makeOptions({ id: "a@mozilla.org" }));
  assertChanges([], "Held until init");
  manager.init();
  assertChanges(["a@mozilla.org"], "init");
  manager.register(makeOptions({ id: "b@mozilla.org" }));
  assertChanges(["b@mozilla.org"], "register");
  manager.unregister("a@mozilla.org");
  assertChanges(["a@mozilla.org"], "unregister");
  manager.unregister("a@mozilla.org");
  assertChanges([], "Unknown widget");
  manager.getWidgetByID("b@mozilla.org").enabled = true;
  assertChanges(["b@mozilla.org"], "enabled");
  Services.prefs.setBoolPref("extensions.widgets.a.enabled", true);
  assertChanges([], "Unregistered widget's pref");

  manager.off("change", onChange);
  manager.register(makeOptions({ id: "a@mozilla.org" }));
  assertChanges([], "Listener removed");
});

add_task(async function test_enabled() {
  const widget = createManager().register(makeOptions());
  Assert.ok(!widget.enabled, "Disabled by default");
  widget.enabled = true;
  Assert.ok(widget.enabled, "Enabled");
});

add_task(async function test_size() {
  const manager = createManager();
  const widget = manager.register(
    makeOptions({ sizes: ["small", "medium"], defaultSize: "medium" })
  );
  Assert.equal(widget.size, "medium", "Default size");
  widget.size = "small";
  Assert.equal(widget.size, "small", "Chosen size");
  widget.size = "large";
  Assert.equal(widget.size, "small", "Invalid size");
  Services.prefs.setStringPref("extensions.widgets.extension.size", "large");
  Assert.equal(widget.size, "medium", "Stored invalid size");

  const other = manager.register(
    makeOptions({ id: "other@mozilla.org", sizes: ["large", "medium"] })
  );
  Assert.equal(other.size, "large", "First size");
});

add_task(async function test_locked_prefs() {
  const widget = createManager().register(
    makeOptions({ sizes: ["small", "medium"] })
  );
  const branch = "extensions.widgets.extension.";
  const defaults = Services.prefs.getDefaultBranch(branch);
  defaults.setBoolPref("enabled", true);
  defaults.setStringPref("size", "medium");
  Services.prefs.lockPref(`${branch}enabled`);
  Services.prefs.lockPref(`${branch}size`);

  widget.enabled = false;
  widget.size = "small";
  Assert.ok(widget.enabled, "Locked enabled pref");
  Assert.equal(widget.size, "medium", "Locked size pref");

  Services.prefs.unlockPref(`${branch}enabled`);
  Services.prefs.unlockPref(`${branch}size`);
});

add_task(async function test_pref_branch() {
  Services.prefs.getDefaultBranch("").setStringPref("test.widget.size", "");
  registerCleanupFunction(() => Services.prefs.deleteBranch("test.widget."));
  const widget = createManager().register(
    makeOptions({
      sizes: ["small", "medium"],
      defaultSize: "medium",
      prefBranch: "test.widget",
    })
  );
  Assert.equal(widget.size, "medium", "Empty size pref");

  widget.enabled = true;
  widget.size = "small";
  Assert.ok(Services.prefs.getBoolPref("test.widget.enabled"), "Enabled pref");
  Assert.equal(
    Services.prefs.getStringPref("test.widget.size"),
    "small",
    "Size pref"
  );
  Assert.ok(
    !Services.prefs.prefHasUserValue("extensions.widgets.extension.enabled"),
    "Default pref unused"
  );
});

add_task(async function test_uninstall_deletes_prefs() {
  const manager = createManager();
  const uninstalled = [];
  manager.on("cleanupPrefsDone", (event, id) => uninstalled.push(id));
  const cases = [
    [EXTENSION_ID, undefined, "extensions.widgets.extension."],
    ["custom@mozilla.org", "test.uninstall", "test.uninstall."],
  ];
  for (const [id, prefBranch, branch] of cases) {
    const widget = manager.register(
      makeOptions({ id, prefBranch, sizes: ["small", "medium"] })
    );
    widget.enabled = true;
    widget.size = "small";
    Assert.ok(
      Services.prefs.prefHasUserValue(`${branch}enabled`),
      "Enabled pref set"
    );
    Assert.ok(
      Services.prefs.prefHasUserValue(`${branch}size`),
      "Size pref set"
    );

    manager.unregister(id);
    manager.cleanupPrefs(id);
    Assert.deepEqual(Services.prefs.getChildList(branch), [], "Prefs cleared");
    Assert.deepEqual(uninstalled.splice(0), [id], "Uninstall emitted");
  }

  Services.prefs.setBoolPref("extensions.widgets.unregistered.enabled", true);
  Services.prefs.setBoolPref("extensions.widgets.unregistered.other", true);
  manager.cleanupPrefs("unregistered@mozilla.org");
  Assert.deepEqual(
    Services.prefs.getChildList("extensions.widgets.unregistered."),
    ["extensions.widgets.unregistered.other"],
    "Unregistered widget prefs cleared"
  );
  Assert.deepEqual(uninstalled, ["unregistered@mozilla.org"], "Emitted");
});

add_task(async function test_register_without_custom_branch_after_update() {
  const manager = createManager();
  const id = "custom@mozilla.org";
  const prefBranchPref = "extensions.widgets.custom.prefBranch";
  manager.register(makeOptions({ id, prefBranch: "test.update" }));
  Assert.equal(
    Services.prefs.getStringPref(prefBranchPref),
    "test.update",
    "Custom branch recorded"
  );
  manager.unregister(id);

  // The add-on is updated to a version without a custom pref branch.
  manager.register(makeOptions({ id })).enabled = true;
  Assert.equal(
    Services.prefs.getStringPref(prefBranchPref),
    "extensions.widgets.custom",
    "Default branch recorded"
  );
  manager.unregister(id);
  manager.cleanupPrefs(id);
  Assert.deepEqual(
    Services.prefs.getChildList("extensions.widgets.custom."),
    [],
    "Default branch prefs cleared"
  );
});

add_task(async function test_uninstall_custom_branch_after_restart() {
  const previousSession = createManager();
  const widget = previousSession.register(
    makeOptions({
      id: "custom@mozilla.org",
      prefBranch: "test.restart",
      sizes: ["small", "medium"],
    })
  );
  widget.enabled = true;
  widget.size = "small";
  // unregister the widget as when the extension is shutdown.
  previousSession.unregister("custom@mozilla.org");

  // Simulate a new app startup (where the extension is disabled
  // and so never re-registered to the WidgetManager in this
  // app session.
  const manager = new _WidgetManager();
  registerCleanupFunction(() => manager.uninit());
  Assert.ok(!manager.isReady, "Manager is not initialized");
  Assert.deepEqual(manager.getWidgets(), [], "No widget registered");
  Assert.ok(manager.hasWidgetPreferences("custom@mozilla.org"), "Widget known");
  manager.cleanupPrefs("custom@mozilla.org");
  Assert.deepEqual(
    Services.prefs.getChildList("test.restart."),
    [],
    "Custom branch prefs cleared"
  );
  Assert.deepEqual(
    Services.prefs.getChildList("extensions.widgets.custom."),
    [],
    "Custom branch pref cleared"
  );
  Assert.ok(!manager.hasWidgetPreferences("custom@mozilla.org"), "Widget gone");
});

add_task(async function test_has_widget_preferences() {
  const manager = createManager();
  Assert.ok(
    !manager.hasWidgetPreferences("other@mozilla.org"),
    "Unknown add-on"
  );
  manager.register(makeOptions());
  Assert.ok(manager.hasWidgetPreferences(EXTENSION_ID), "Registered widget");
  manager.unregister(EXTENSION_ID);
  manager.cleanupPrefs(EXTENSION_ID);
  Assert.ok(!manager.hasWidgetPreferences(EXTENSION_ID), "Prefs cleaned up");
});

add_task(async function test_has_widget_preferences_combinations() {
  const manager = createManager();
  const id = "combo@mozilla.org";
  const branch = "extensions.widgets.combo";
  for (const enabled of [false, true]) {
    for (const size of [false, true]) {
      for (const prefBranch of [false, true]) {
        if (enabled) {
          Services.prefs.setBoolPref(`${branch}.enabled`, true);
        }
        if (size) {
          Services.prefs.setStringPref(`${branch}.size`, "medium");
        }
        if (prefBranch) {
          Services.prefs.setStringPref(`${branch}.prefBranch`, branch);
        }
        Assert.equal(
          manager.hasWidgetPreferences(id),
          prefBranch,
          `enabled: ${enabled}, size: ${size}, prefBranch: ${prefBranch}`
        );
        Services.prefs.deleteBranch(`${branch}.`);
      }
    }
  }
});
