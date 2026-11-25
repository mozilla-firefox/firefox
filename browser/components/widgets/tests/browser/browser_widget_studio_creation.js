/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { WidgetCreator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/widgets/WidgetCreator.sys.mjs"
);
const { MockEngineManager } = ChromeUtils.importESModule(
  "resource://testing-common/AIWindowTestUtils.sys.mjs"
);
const { PURPOSES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs"
);
const { openAIEngine } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs"
);
const { GeneratedWidgetsProvider } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/widgets/GeneratedWidgetsProvider.sys.mjs"
);

const PROVIDER_PREF = "extensions.widgets.generated.enabled";

const PROMPT = "weather in Paris";
const OTHER_PROMPT = "tides in Brest";

const FILES = {
  "manifest.json": JSON.stringify({
    manifest_version: 3,
    name: "Test weather",
  }),
  "widget.html": "<!doctype html>",
};

let mockEngine;
let createSpy;

add_setup(async function () {
  await mockAuthentication();
  setSignedIn(true);
  mockEngine = new MockEngineManager();
  let build = sinon.stub(openAIEngine, "build").callsFake(options =>
    build.wrappedMethod.call(openAIEngine, {
      ...options,
      purpose: PURPOSES.WIDGET_GENERATION,
    })
  );
  await setGeneratorRecords();
  createSpy = sinon.spy(WidgetCreator, "create");
  await SpecialPowers.pushPrefEnv({ set: [[PROVIDER_PREF, true]] });

  registerCleanupFunction(async () => {
    for (let addon of await GeneratedWidgetsProvider.getAddonsByTypes([
      "generatedwidget",
    ])) {
      await GeneratedWidgetsProvider.uninstall(addon.id);
    }
    build.restore();
    createSpy.restore();
    await mockEngine.rejectAllRequests();
    mockEngine.cleanupMocks();
  });
});

function waitForComposerFocus(panel) {
  return TestUtils.waitForCondition(
    () => panel.composer?.shadowRoot.activeElement?.localName == "textarea",
    "Composer focused"
  );
}

async function typePrompt(panel, prompt = PROMPT) {
  await panel.updateComplete;
  await waitForComposerFocus(panel);
  EventUtils.sendString(prompt, panel.documentGlobal);
}

/**
 * Submits the focused composer and waits for the model request.
 *
 * @param {Element} panel - the sidebar-widget-studio
 * @returns {Promise<{request: object, respond: Function, run: Promise}>} the
 *   captured request and the WidgetCreator.create call it belongs to
 */
async function submitPrompt(panel) {
  EventUtils.synthesizeKey("KEY_Enter", {}, panel.documentGlobal);
  let { request, respond } = await mockEngine.captureRequest({
    purpose: PURPOSES.WIDGET_GENERATION,
  });
  await panel.updateComplete;
  return { request, respond, run: createSpy.lastCall.returnValue };
}

/**
 * Answers a generation's model request with FILES and waits for the widget to
 * install.
 *
 * @param {{respond: Function, run: Promise}} submitted - from submitPrompt
 * @returns {Promise<object>} the installed widget's add-on
 */
async function finishCreation({ respond, run }) {
  respond(simulateWriteFileResponse(FILES));
  await mockEngine.respondTo({
    purpose: PURPOSES.WIDGET_GENERATION,
    response: "Done.",
  });
  return run;
}

add_task(async function test_submit_creates_a_widget() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let submitted = await submitPrompt(panel);
  is(
    submitted.request.args.findLast(message => message.role == "user").content,
    PROMPT,
    "Prompt sent"
  );
  ok(!panel.composer, "Composer hidden");
  ok(panel.composeButton.disabled, "Compose button disabled");

  let { id } = await finishCreation(submitted);
  ok(WebExtensionPolicy.getByID(id)?.active, "Widget installed");
  await panel.updateComplete;
  ok(!panel.composeButton.disabled, "Compose button enabled");
  SidebarController.hide();
});

add_task(async function test_failed_generation_enables_compose_button() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  ok(panel.composeButton.disabled, "Compose button disabled");

  await mockEngine.rejectAllRequests();
  await Assert.rejects(run, error => error.code == "service", "Failed");
  await panel.updateComplete;
  ok(!panel.composeButton.disabled, "Compose button enabled");
  SidebarController.hide();
});

add_task(async function test_generation_shown_in_every_window() {
  let panel = await openWidgetsSidebar();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  let otherPanel = await openWidgetsSidebar(win);
  await typePrompt(otherPanel, OTHER_PROMPT);

  await SimpleTest.promiseFocus(window);
  await typePrompt(panel);
  let submitted = await submitPrompt(panel);
  await otherPanel.updateComplete;
  let { composer } = otherPanel;
  let submit = composer.shadowRoot.querySelector(".submit");
  await composer.updateComplete;
  ok(submit.disabled, "Submit disabled in other window");

  await SimpleTest.promiseFocus(win);
  let callCount = createSpy.callCount;
  EventUtils.synthesizeKey("KEY_Enter", {}, otherPanel.documentGlobal);
  await otherPanel.updateComplete;
  is(otherPanel.composer, composer, "Composer kept");
  is(composer.value, OTHER_PROMPT, "Prompt kept");
  is(createSpy.callCount, callCount, "No second creation");

  await SimpleTest.promiseFocus(window);
  SidebarController.hide();
  panel = await openWidgetsSidebar();
  await panel.updateComplete;
  ok(!panel.composer, "Composer hidden after reopening");
  ok(panel.composeButton.disabled, "Disabled after reopening");

  await finishCreation(submitted);
  await otherPanel.updateComplete;
  await composer.updateComplete;
  ok(!submit.disabled, "Submit enabled in other window");
  await BrowserTestUtils.closeWindow(win);
  await SimpleTest.promiseFocus(window);
  SidebarController.hide();
});
