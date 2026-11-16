/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async function () {
  await mockAuthentication();
  SidebarTestUtils.restoreStateAtCleanup(window);
  await SpecialPowers.pushPrefEnv({
    set: [["browser.widgets.generated.enabled", true]],
  });
  await SidebarTestUtils.ensureLauncherVisible(window);
  SidebarController.hide();
});

add_task(async function test_signed_out_shows_prompt() {
  setSignedIn(false);
  const panel = await openWidgetsSidebar();
  is(SidebarController.currentID, WIDGETS_VIEW, "Widget Studio is open");
  await panel.updateComplete;
  const prompt = panel.shadowRoot.querySelector("#sign-in-prompt");
  ok(prompt, "The sign-in prompt is shown");
  await panel.ownerDocument.l10n.translateFragment(prompt);

  const title = prompt.querySelector("#sign-in-title");
  const button = prompt.querySelector("#sign-in-button");
  const learnMore = prompt.querySelector("#sign-in-learn-more");
  const illustration = prompt.querySelector("#sign-in-illustration");
  await TestUtils.waitForCondition(
    () => illustration.complete && illustration.naturalWidth > 0,
    "The Kit image loads"
  );
  is(
    title.textContent,
    "Sign in to create custom widgets",
    "The title is translated"
  );
  is(button.label, "Sign in", "The button label is translated");
  is(illustration.getAttribute("alt"), "", "The image is decorative");
  ok(
    learnMore.href.endsWith("firefox-new-tab-widgets"),
    "The learn-more link opens the widgets support page"
  );
  ok(
    panel.shadowRoot.querySelector("slot"),
    "The slot stays for existing widgets"
  );
  SidebarController.hide();
});

add_task(async function test_learn_more_opens_support_page() {
  setSignedIn(false);
  const panel = await openWidgetsSidebar();
  await panel.updateComplete;
  const openTrustedLinkIn = sinon.stub(panel.topWindow, "openTrustedLinkIn");
  try {
    panel.shadowRoot.querySelector("#sign-in-learn-more").click();
    ok(openTrustedLinkIn.calledOnce, "Clicking Learn more opens a link");
    const [url, where] = openTrustedLinkIn.firstCall.args;
    ok(
      url.endsWith("firefox-new-tab-widgets"),
      "The link is the widgets support page"
    );
    is(where, "tab", "The page opens in a new tab");
  } finally {
    openTrustedLinkIn.restore();
    SidebarController.hide();
  }
});

add_task(async function test_prompt_follows_sign_in_state() {
  setSignedIn(true);
  const panel = await openWidgetsSidebar();
  await panel.updateComplete;
  ok(
    !panel.shadowRoot.querySelector("#sign-in-prompt"),
    "Signed-in users see no sign-in prompt"
  );
  ok(panel.shadowRoot.querySelector("slot"), "The slot is rendered");

  setSignedIn(false);
  await panel.updateComplete;
  ok(
    panel.shadowRoot.querySelector("#sign-in-prompt"),
    "The prompt appears when the account signs out"
  );

  setSignedIn(true);
  await panel.updateComplete;
  ok(
    !panel.shadowRoot.querySelector("#sign-in-prompt"),
    "The prompt goes away when the account signs in"
  );
  SidebarController.hide();
});

add_task(async function test_sign_in_button_starts_flow() {
  const { SpecialMessageActions } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
  );
  const fxaSignInFlow = sinon
    .stub(SpecialMessageActions, "fxaSignInFlow")
    .resolves(false);
  try {
    setSignedIn(false);
    const panel = await openWidgetsSidebar();
    await panel.updateComplete;
    panel.shadowRoot.querySelector("#sign-in-button").click();

    ok(fxaSignInFlow.calledOnce, "Clicking Sign in starts the FxA flow");
    const [data, browser] = fxaSignInFlow.firstCall.args;
    is(data.entrypoint, "widget_studio", "The entrypoint is Widget Studio");
    is(
      browser,
      gBrowser.selectedBrowser,
      "The flow starts from the selected tab"
    );
  } finally {
    SidebarController.hide();
    fxaSignInFlow.restore();
  }
});

add_task(async function test_sign_in_button_is_disabled_while_signing_in() {
  const { SpecialMessageActions } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
  );
  const flow = Promise.withResolvers();
  const fxaSignInFlow = sinon
    .stub(SpecialMessageActions, "fxaSignInFlow")
    .returns(flow.promise);
  try {
    setSignedIn(false);
    const panel = await openWidgetsSidebar();
    await panel.updateComplete;
    const button = panel.shadowRoot.querySelector("#sign-in-button");
    ok(!button.disabled, "The button is enabled before signing in");

    button.click();
    await panel.updateComplete;
    ok(button.disabled, "The button is disabled while signing in");
    await panel.authentication.signIn();
    ok(fxaSignInFlow.calledOnce, "A second call doesn't start another flow");

    flow.resolve(false);
    await TestUtils.waitForCondition(
      () => !button.disabled,
      "The button is enabled again when the flow ends"
    );
  } finally {
    SidebarController.hide();
    fxaSignInFlow.restore();
  }
});

add_task(async function test_sign_in_failure_is_logged() {
  const { SpecialMessageActions } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
  );
  const fxaSignInFlow = sinon
    .stub(SpecialMessageActions, "fxaSignInFlow")
    .rejects(new Error("FxA config unavailable"));
  try {
    setSignedIn(false);
    const panel = await openWidgetsSidebar();
    await panel.updateComplete;
    const logged = TestUtils.consoleMessageObserved(msg => {
      const arg = msg.wrappedJSObject.arguments?.[0];
      return typeof arg === "string" && arg.includes("sign-in flow");
    });
    const button = panel.shadowRoot.querySelector("#sign-in-button");
    button.click();
    await logged;

    ok(fxaSignInFlow.calledOnce, "A failed sign-in flow is logged");
    await TestUtils.waitForCondition(
      () => !button.disabled,
      "The button is enabled again after a failure"
    );
  } finally {
    SidebarController.hide();
    fxaSignInFlow.restore();
  }
});
