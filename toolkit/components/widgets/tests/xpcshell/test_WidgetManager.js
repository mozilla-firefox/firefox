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
