/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CREATION_ERRORS, WidgetCreator } = ChromeUtils.importESModule(
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

function clickPlaceholderButton(placeholder, l10nId) {
  EventUtils.synthesizeMouseAtCenter(
    placeholder.shadowRoot.querySelector(`[data-l10n-id="${l10nId}"]`),
    {},
    placeholder.documentGlobal
  );
}

async function failGeneration(panel, run) {
  await mockEngine.rejectAllRequests();
  await run.catch(() => {});
  await panel.updateComplete;
  let { placeholder } = panel;
  await placeholder.updateComplete;
  return placeholder;
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

add_task(async function test_failed_generation_blocks_composer() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  ok(panel.composeButton.disabled, "Compose button disabled");

  await mockEngine.rejectAllRequests();
  await Assert.rejects(run, error => error.code == "service", "Failed");
  await panel.updateComplete;
  ok(!panel.composeButton, "Compose button hidden");

  let { placeholder } = panel;
  await placeholder.updateComplete;
  clickPlaceholderButton(placeholder, "widget-placeholder-edit-button");
  await waitForComposerFocus(panel);
  ok(!panel.composer.submitDisabled, "Submit enabled after Try again");
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

add_task(async function test_submit_shows_placeholder() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let submitted = await submitPrompt(panel);
  let { placeholder } = panel;
  is(placeholder.prompt, PROMPT, "Placeholder prompt");
  await TestUtils.waitForCondition(
    () => panel.shadowRoot.activeElement == placeholder,
    "Placeholder focused"
  );

  await finishCreation(submitted);
  await TestUtils.waitForCondition(
    () => panel.shadowRoot.activeElement == panel.composeButton,
    "Compose button focused"
  );
  ok(!placeholder.isConnected, "Placeholder removed");
  SidebarController.hide();
});

add_task(async function test_placeholder_outlives_panel() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let submitted = await submitPrompt(panel);
  SidebarController.hide();

  panel = await openWidgetsSidebar();
  await panel.updateComplete;
  let { placeholder } = panel;
  is(placeholder?.prompt, PROMPT, "Placeholder kept");

  await finishCreation(submitted);
  await TestUtils.waitForCondition(
    () => !placeholder.isConnected,
    "Placeholder removed"
  );
  ok(panel.composeButton, "Compose button shown");
  SidebarController.hide();
});

add_task(async function test_placeholder_outlives_panel_while_installing() {
  let installing = Promise.withResolvers();
  let install = sinon
    .stub(GeneratedWidgetsProvider, "install")
    .callsFake(async path => {
      await installing.promise;
      return install.wrappedMethod.call(GeneratedWidgetsProvider, path);
    });
  try {
    let panel = await openWidgetsSidebar();
    await typePrompt(panel);
    let submitted = await submitPrompt(panel);
    let creating = finishCreation(submitted);
    await TestUtils.waitForCondition(() => install.called, "Installing");
    SidebarController.hide();

    panel = await openWidgetsSidebar();
    await panel.updateComplete;
    let { placeholder } = panel;
    is(placeholder?.prompt, PROMPT, "Placeholder kept");

    installing.resolve();
    await creating;
    await TestUtils.waitForCondition(
      () => !placeholder.isConnected,
      "Placeholder removed"
    );
  } finally {
    install.restore();
  }
  SidebarController.hide();
});

add_task(async function test_failed_generation() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  let placeholder = await failGeneration(panel, run);
  is(
    placeholder.dataset.l10nId,
    "widget-generation-error-service",
    "Service error"
  );
  await TestUtils.waitForCondition(
    () => placeholder.shadowRoot.activeElement?.classList.contains("content"),
    "Placeholder focused"
  );

  clickPlaceholderButton(placeholder, "widget-placeholder-edit-button");
  await waitForComposerFocus(panel);
  is(panel.composer.value, PROMPT, "Prompt restored");
  ok(!placeholder.isConnected, "Placeholder removed");
  SidebarController.hide();
});

add_task(async function test_failed_install() {
  await SpecialPowers.pushPrefEnv({ set: [[PROVIDER_PREF, false]] });
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let submitted = await submitPrompt(panel);
  await Assert.rejects(
    finishCreation(submitted),
    error => error.code == CREATION_ERRORS.INSTALL,
    "Install failed"
  );
  await TestUtils.waitForCondition(
    () => panel.placeholder?.error,
    "Placeholder error"
  );
  let { placeholder } = panel;
  is(placeholder.error, CREATION_ERRORS.INSTALL, "Install error");
  await placeholder.updateComplete;
  clickPlaceholderButton(placeholder, "widget-placeholder-edit-button");
  await waitForComposerFocus(panel);
  await SpecialPowers.popPrefEnv();
  SidebarController.hide();
});

add_task(async function test_cancel_returns_prompt() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  let { placeholder } = panel;
  await placeholder.updateComplete;

  clickPlaceholderButton(placeholder, "widget-placeholder-cancel-button");
  await Assert.rejects(run, error => error.name == "AbortError", "Run aborted");
  await waitForComposerFocus(panel);
  is(panel.composer.value, PROMPT, "Prompt restored");
  ok(!placeholder.isConnected, "Placeholder removed");
  SidebarController.hide();
});

add_task(async function test_placeholder_shown_in_every_window() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let otherPanel = await openWidgetsSidebar(win);
  await otherPanel.updateComplete;
  is(otherPanel.placeholder?.prompt, PROMPT, "Shown in new window");

  await failGeneration(panel, run);
  await otherPanel.updateComplete;
  is(panel.placeholder.error, "service", "Error shown");
  is(otherPanel.placeholder.error, "service", "Error shown in new window");

  await SimpleTest.promiseFocus(win);
  await otherPanel.placeholder.updateComplete;
  clickPlaceholderButton(
    otherPanel.placeholder,
    "widget-placeholder-edit-button"
  );
  await waitForComposerFocus(otherPanel);
  is(otherPanel.composer.value, PROMPT, "Prompt restored");
  await panel.updateComplete;
  ok(!panel.placeholder, "Error dismissed in first window");

  let second = await submitPrompt(otherPanel);
  await panel.updateComplete;
  is(panel.placeholder.error, null, "Generating in first window");

  await SimpleTest.promiseFocus(window);
  await panel.placeholder.updateComplete;
  clickPlaceholderButton(panel.placeholder, "widget-placeholder-cancel-button");
  await Assert.rejects(
    second.run,
    error => error.name == "AbortError",
    "Canceled"
  );
  await otherPanel.updateComplete;
  ok(!otherPanel.placeholder, "Removed in new window");

  await BrowserTestUtils.closeWindow(win);
  await SimpleTest.promiseFocus(window);
  SidebarController.hide();
});

add_task(async function test_error_shown_in_later_window() {
  let panel = await openWidgetsSidebar();
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  await failGeneration(panel, run);

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let otherPanel = await openWidgetsSidebar(win);
  await otherPanel.updateComplete;
  let { placeholder } = otherPanel;
  is(placeholder?.error, "service", "Error shown");
  is(placeholder.prompt, PROMPT, "Failed prompt");
  ok(!otherPanel.composer, "Composer hidden");
  ok(!otherPanel.composeButton, "Compose button hidden");

  await placeholder.updateComplete;
  clickPlaceholderButton(placeholder, "widget-placeholder-edit-button");
  await waitForComposerFocus(otherPanel);
  is(otherPanel.composer.value, PROMPT, "Prompt restored");
  await panel.updateComplete;
  ok(!panel.placeholder, "Error dismissed in first window");

  await BrowserTestUtils.closeWindow(win);
  await SimpleTest.promiseFocus(window);
  SidebarController.hide();
});

add_task(async function test_try_again_keeps_draft() {
  let panel = await openWidgetsSidebar();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  let otherPanel = await openWidgetsSidebar(win);
  await typePrompt(otherPanel, OTHER_PROMPT);

  await SimpleTest.promiseFocus(window);
  await typePrompt(panel);
  let { run } = await submitPrompt(panel);
  await failGeneration(panel, run);
  await otherPanel.updateComplete;
  let { composer, placeholder } = otherPanel;
  is(placeholder?.error, "service", "Error shown");
  ok(composer.submitDisabled, "Submit disabled by the error");

  await SimpleTest.promiseFocus(win);
  await placeholder.updateComplete;
  clickPlaceholderButton(placeholder, "widget-placeholder-edit-button");
  await waitForComposerFocus(otherPanel);
  is(otherPanel.composer, composer, "Composer kept");
  is(composer.value, OTHER_PROMPT, "Draft kept");
  ok(!composer.submitDisabled, "Submit enabled");

  await BrowserTestUtils.closeWindow(win);
  await SimpleTest.promiseFocus(window);
  SidebarController.hide();
});
