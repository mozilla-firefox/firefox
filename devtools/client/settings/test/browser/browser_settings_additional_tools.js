/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_TOOL_PREF = "devtools.test-tool.enabled";
const TEST_WEBEXT_PREF = "devtools.webextensions.fakeExtId.enabled";

add_task(test_no_additional_tools);
add_task(test_additional_tools_listed);
add_task(test_additional_tools_toggle);
add_task(test_additional_tools_support);
add_task(test_additional_tools_refresh);

async function test_no_additional_tools() {
  const { root } = await openSettings();
  ok(
    !root.shadowRoot.querySelector("#settings-additional-tools"),
    "The additional tools section is hidden without additional tools"
  );
  await closeTabAndToolbox();
}

async function test_additional_tools_listed() {
  await pushPref("devtools.settings.redesign-enabled", true);
  await pushPref(TEST_TOOL_PREF, true);
  await pushPref(TEST_WEBEXT_PREF, true);
  gDevTools.registerTool({
    id: "test-tool",
    isToolSupported: () => true,
    url: "about:blank",
    label: "Test Tool",
  });

  const tab = await addTab("about:blank");
  const toolbox = await openToolboxForTab(tab, "webconsole");
  toolbox.registerWebExtension("fakeUUID", {
    name: "Fake WebExtension",
    pref: TEST_WEBEXT_PREF,
  });
  await toolbox.selectTool("options");
  const root = toolbox.getCurrentPanel().element;
  await root.updateComplete;

  const tool = root.shadowRoot.querySelector("#settings-tool-test-tool");
  const webext = root.shadowRoot.querySelector("#settings-webext-fakeUUID");
  ok(tool, "A registered additional tool is listed");
  is(tool.label, "Test Tool", "The tool uses its registered label");
  ok(webext, "A WebExtension with a devtools page is listed");
  is(webext.label, "Fake WebExtension", "The WebExtension uses its name");

  gDevTools.unregisterTool("test-tool");
  await closeTabAndToolbox();
}

async function test_additional_tools_toggle() {
  await pushPref("devtools.settings.redesign-enabled", true);
  await pushPref(TEST_TOOL_PREF, true);
  await pushPref(TEST_WEBEXT_PREF, true);
  gDevTools.registerTool({
    id: "test-tool",
    isToolSupported: () => true,
    url: "about:blank",
    label: "Test Tool",
  });

  const tab = await addTab("about:blank");
  const toolbox = await openToolboxForTab(tab, "webconsole");
  toolbox.registerWebExtension("fakeUUID", {
    name: "Fake WebExtension",
    pref: TEST_WEBEXT_PREF,
  });
  await toolbox.selectTool("options");
  const root = toolbox.getCurrentPanel().element;
  await root.updateComplete;
  const tool = root.shadowRoot.querySelector("#settings-tool-test-tool");
  const webext = root.shadowRoot.querySelector("#settings-webext-fakeUUID");
  await Promise.all([
    tool.closest("devtools-settings-pref").updateComplete,
    webext.closest("devtools-settings-pref").updateComplete,
  ]);
  await Promise.all([tool.updateComplete, webext.updateComplete]);
  ok(
    toolbox.doc.querySelector("#toolbox-tab-test-tool"),
    "The additional tool's tab starts visible"
  );

  tool.click();
  is(
    Services.prefs.getBoolPref(TEST_TOOL_PREF),
    false,
    "Clicking the tool updates its preference"
  );
  await waitFor(
    () => !toolbox.doc.querySelector("#toolbox-tab-test-tool"),
    "Clicking the tool removes its tab"
  );

  webext.click();
  is(
    Services.prefs.getBoolPref(TEST_WEBEXT_PREF),
    false,
    "Clicking the WebExtension updates its preference"
  );

  gDevTools.unregisterTool("test-tool");
  await closeTabAndToolbox();
}

async function test_additional_tools_support() {
  await pushPref(TEST_TOOL_PREF, true);
  gDevTools.registerTool({
    id: "test-tool",
    isToolSupported: () => false,
    url: "about:blank",
    label: "Test Tool",
  });

  const { root } = await openSettings();
  const tool = root.shadowRoot.querySelector("#settings-tool-test-tool");
  ok(tool.disabled, "An unsupported additional tool is disabled");

  gDevTools.unregisterTool("test-tool");
  await closeTabAndToolbox();
}

async function test_additional_tools_refresh() {
  await pushPref(TEST_TOOL_PREF, true);
  await pushPref(TEST_WEBEXT_PREF, true);
  const { toolbox, root } = await openSettings();
  ok(
    !root.shadowRoot.querySelector("#settings-additional-tools"),
    "The additional tools section starts hidden"
  );

  gDevTools.registerTool({
    id: "test-tool",
    isToolSupported: () => false,
    url: "about:blank",
    label: "Test Tool",
  });
  await waitFor(
    () => root.shadowRoot.querySelector("#settings-tool-test-tool"),
    "Registering an unsupported tool lists it"
  );
  ok(
    root.shadowRoot.querySelector("#settings-additional-tools"),
    "Registering a tool made the additional tools section visible"
  );

  toolbox.registerWebExtension("fakeUUID", {
    name: "Fake WebExtension",
    pref: TEST_WEBEXT_PREF,
  });
  await waitFor(
    () => root.shadowRoot.querySelector("#settings-webext-fakeUUID"),
    "Registering a WebExtension lists it"
  );

  gDevTools.unregisterTool("test-tool");
  await waitFor(
    () => !root.shadowRoot.querySelector("#settings-tool-test-tool"),
    "Unregistering a tool removes it"
  );

  toolbox.unregisterWebExtension("fakeUUID");
  await waitFor(
    () => !root.shadowRoot.querySelector("#settings-additional-tools"),
    "Unregistering the last WebExtension hides the section"
  );

  await closeTabAndToolbox();
}
