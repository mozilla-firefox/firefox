/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AddonSettings } = ChromeUtils.importESModule(
  "resource://gre/modules/addons/AddonSettings.sys.mjs"
);
const { Extension } = ChromeUtils.importESModule(
  "resource://gre/modules/Extension.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { WidgetManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/WidgetManager.sys.mjs"
);

const EXTENSION_ID = "widget@mochi.test";

const WIDGET = {
  page: "widget.html",
  sizes: ["small", "medium"],
  default_size: "medium",
  pref_branch: "test.widget",
};

const GENERATED_WIDGET = {
  page: "widget.html",
  sizes: ["small", "medium"],
};

function loadWidgetExtension({
  id = EXTENSION_ID,
  isPrivileged = true,
  temporarilyInstalled = false,
  manifestKey = "builtin_widget",
  widget = WIDGET,
  extraManifest = {},
} = {}) {
  return ExtensionTestUtils.loadExtension({
    isPrivileged,
    temporarilyInstalled,
    manifest: {
      name: "Widget Extension",
      browser_specific_settings: { gecko: { id } },
      [manifestKey]: widget,
      ...extraManifest,
    },
    files: { "widget.html": "" },
  });
}

add_task(async function test_extension_registers_widget() {
  const extension = loadWidgetExtension();
  await extension.startup();

  const widget = WidgetManager.getWidgetByID(EXTENSION_ID);
  Assert.equal(widget.id, EXTENSION_ID, "Id");
  Assert.equal(
    widget.src,
    extension.extension.baseURI.resolve("widget.html"),
    "Src"
  );
  Assert.equal(widget.name, "Widget Extension", "Name");
  Assert.deepEqual(widget.sizes, ["small", "medium"], "Sizes");
  Assert.equal(widget.size, "medium", "Default size");

  widget.enabled = true;
  widget.size = "small";
  Assert.ok(Services.prefs.getBoolPref("test.widget.enabled"), "Enabled pref");
  Assert.equal(
    Services.prefs.getStringPref("test.widget.size"),
    "small",
    "Size pref"
  );
  Services.prefs.deleteBranch("test.widget.");

  await extension.unload();
  Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "Unregistered");
});

add_task(async function test_unprivileged_extension_is_ignored() {
  ExtensionTestUtils.failOnSchemaWarnings(false);
  const extension = loadWidgetExtension({ isPrivileged: false });
  const { messages } = await promiseConsoleOutput(() => extension.startup());
  ExtensionTestUtils.failOnSchemaWarnings(true);
  Assert.equal(extension.extension.manifest.builtin_widget, null, "Key nulled");
  Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "No widget");
  Assert.ok(
    messages.some(({ message }) => /Ignoring 'builtin_widget'/.test(message)),
    "Warning logged"
  );
  await extension.unload();
});

add_task(
  { pref_set: [["extensions.experiments.enabled", false]] },
  async function test_temporary_install_is_rejected() {
    ExtensionTestUtils.failOnSchemaWarnings(false);
    const extension = loadWidgetExtension({
      id: "rejected-widget@mochi.test",
      isPrivileged: false,
      temporarilyInstalled: true,
    });
    await Assert.rejects(
      extension.startup(),
      /Using 'builtin_widget' requires a privileged add-on/,
      "Startup rejected"
    );
    ExtensionTestUtils.failOnSchemaWarnings(true);
  }
);

add_task(
  { pref_set: [["extensions.experiments.enabled", true]] },
  async function test_temporary_install_with_experiments() {
    if (!AddonSettings.EXPERIMENTS_ENABLED) {
      info("Experiments cannot be enabled on this build");
      return;
    }
    const extension = loadWidgetExtension({
      isPrivileged: false,
      temporarilyInstalled: true,
    });
    await extension.startup();
    Assert.ok(WidgetManager.getWidgetByID(EXTENSION_ID), "Registered");
    await extension.unload();
  }
);

add_task(async function test_generated_widget_is_ignored() {
  for (const isPrivileged of [false, true]) {
    ExtensionTestUtils.failOnSchemaWarnings(false);
    const extension = loadWidgetExtension({
      isPrivileged,
      manifestKey: "generated_widget",
      widget: GENERATED_WIDGET,
    });
    const { messages } = await promiseConsoleOutput(() => extension.startup());
    ExtensionTestUtils.failOnSchemaWarnings(true);
    Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "No widget");
    Assert.ok(
      messages.some(({ message }) =>
        /Ignoring 'generated_widget'/.test(message)
      ),
      "Warning logged"
    );
    await extension.unload();
  }
});

add_task(async function test_builtin_widget_is_used_with_generated_widget() {
  ExtensionTestUtils.failOnSchemaWarnings(false);
  const extension = loadWidgetExtension({
    extraManifest: { generated_widget: GENERATED_WIDGET },
  });
  const { messages } = await promiseConsoleOutput(() => extension.startup());
  ExtensionTestUtils.failOnSchemaWarnings(true);
  const widget = WidgetManager.getWidgetByID(EXTENSION_ID);
  Assert.equal(widget.size, "medium", "Size");
  Assert.equal(widget.generated, false, "Not generated");
  Assert.ok(
    messages.some(({ message }) => /Ignoring 'generated_widget'/.test(message)),
    "Warning logged"
  );
  await extension.unload();
});

add_task(async function test_builtin_widget_wins_when_both_are_allowed() {
  const stub = sinon
    .stub(Extension.prototype, "canUseGeneratedWidgets")
    .returns(true);
  const register = sinon.spy(WidgetManager, "register");
  const extension = loadWidgetExtension({
    extraManifest: { generated_widget: GENERATED_WIDGET },
  });
  await extension.startup();
  Assert.ok(register.calledOnce, "Registered once");
  const widget = WidgetManager.getWidgetByID(EXTENSION_ID);
  Assert.equal(widget.size, "medium", "Size");
  Assert.equal(widget.generated, false, "Not generated");
  await extension.unload();
  Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "Unregistered");
  register.restore();
  stub.restore();
});

add_task(async function test_generated_widget_temporary_install_is_rejected() {
  ExtensionTestUtils.failOnSchemaWarnings(false);
  const extension = loadWidgetExtension({
    id: "rejected-generated-widget@mochi.test",
    isPrivileged: false,
    temporarilyInstalled: true,
    manifestKey: "generated_widget",
    widget: GENERATED_WIDGET,
  });
  await Assert.rejects(
    extension.startup(),
    /Using 'generated_widget' is reserved for generated widgets/,
    "Startup rejected"
  );
  ExtensionTestUtils.failOnSchemaWarnings(true);
});

add_task(async function test_generated_widget_needs_generated_extension() {
  const stub = sinon
    .stub(Extension.prototype, "canUseGeneratedWidgets")
    .returns(true);
  const id = "ungenerated-widget@mochi.test";
  const extension = loadWidgetExtension({
    id,
    manifestKey: "generated_widget",
    widget: GENERATED_WIDGET,
  });
  await Assert.rejects(
    extension.startup(),
    /Only generated widgets can use 'generated_widget'/,
    "Startup rejected"
  );
  Assert.equal(WidgetManager.getWidgetByID(id), null, "No widget");
  stub.restore();
});

add_task(async function test_invalid_pref_branch() {
  ExtensionTestUtils.failOnSchemaWarnings(false);
  for (const pref_branch of ["", ".test", "test.", "test..widget"]) {
    const extension = loadWidgetExtension({
      id: `invalid-${pref_branch}@mochi.test`,
      widget: { ...WIDGET, pref_branch },
    });
    await Assert.rejects(
      extension.startup(),
      /builtin_widget\.pref_branch/,
      `"${pref_branch}"`
    );
  }
  ExtensionTestUtils.failOnSchemaWarnings(true);
});

add_task(async function test_failed_startup_registers_nothing() {
  const failed = loadWidgetExtension();
  const stub = sinon
    .stub(failed.extension, "runManifest")
    .callsFake(async function (manifest) {
      await stub.wrappedMethod.call(this, manifest);
      throw new Error("Startup failed");
    });
  await Assert.rejects(failed.startup(), /Startup failed/, "Startup rejected");
  Assert.ok(stub.calledOnce, "runManifest called");
  Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "No widget");
  stub.restore();

  const extension = loadWidgetExtension();
  await extension.startup();
  Assert.ok(WidgetManager.getWidgetByID(EXTENSION_ID), "Registered on retry");
  await extension.unload();
});

add_task(async function test_shutdown_before_ready() {
  const extension = loadWidgetExtension();
  const { runManifest } = extension.extension;
  extension.extension.runManifest = async function (manifest) {
    await runManifest.call(this, manifest);
    await this.shutdown();
    Assert.ok(!this.emitter.has("ready"), "No ready listener");
  };
  await extension.startup();
  Assert.equal(WidgetManager.getWidgetByID(EXTENSION_ID), null, "No widget");
  await extension.unload();
});
