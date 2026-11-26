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
  const description = prompt.querySelector("#sign-in-description");
  const exampleHeading = prompt.querySelector("#sign-in-example-heading");
  const kit = prompt.querySelector("#sign-in-kit");
  await TestUtils.waitForCondition(
    () => kit.complete && kit.naturalWidth > 0,
    "The Kit image loads"
  );
  is(
    title.textContent,
    "Sign in to create custom widgets",
    "The title is translated"
  );
  is(button.label, "Sign in", "The button label is translated");
  is(
    description.textContent,
    "Build helpful tools for your homepage or sidebar.",
    "The description is translated"
  );
  is(
    exampleHeading.textContent,
    "Describe your widget. Watch it appear.",
    "The example heading is translated"
  );
  is(
    prompt.querySelector("#sign-in-calendar strong").textContent,
    "Morning workout streak: 4 days",
    "The calendar title is translated with its streak count"
  );
  is(
    Array.from(
      prompt.querySelectorAll("#sign-in-calendar-weekdays span"),
      span => span.textContent
    ).join(""),
    "SMTWTFS",
    "The calendar shows the weekday initials, starting on Sunday"
  );
  is(kit.getAttribute("alt"), "", "The image is decorative");
  for (const id of ["#sign-in-example-prompt", "#sign-in-illustration"]) {
    is(
      prompt.querySelector(id).getAttribute("aria-hidden"),
      "true",
      `${id} is hidden from assistive technology`
    );
  }
  ok(
    learnMore.href.endsWith("firefox-custom-widgets"),
    "The learn-more link opens the custom widgets support page"
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
      url.endsWith("firefox-custom-widgets"),
      "The link is the custom widgets support page"
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

add_task(async function test_startup_shows_nothing_until_status_known() {
  const lookup = holdSignedInUser();
  const panel = await openWidgetsSidebar(window, { waitForStatus: false });
  try {
    await panel.updateComplete;
    ok(panel.authentication.isStatusPending, "The status is unknown");
    ok(!panel.shadowRoot.querySelector("#sign-in-prompt"), "No prompt yet");
    ok(!panel.composer && !panel.composeButton, "No composer yet");
    ok(SidebarController.panelHeader.closeButton, "The close button shows");
    ok(panel.shadowRoot.querySelector("slot"), "The slot is rendered");

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    await panel.updateComplete;
    ok(panel.authentication.isStatusPending, "The status is still unknown");
    ok(
      !panel.shadowRoot.querySelector("#sign-in-prompt"),
      "An update with the default state doesn't show the prompt"
    );
    ok(!panel.composer && !panel.composeButton, "There is still no composer");

    lookup.resolve({ verified: true });
    await waitForSignInStatus(panel);
    ok(panel.composer, "The composer shows once signed in is known");
    ok(!panel.shadowRoot.querySelector("#sign-in-prompt"), "No prompt");
  } finally {
    lookup.resolve(null);
    await waitForSignInStatus(panel);
    closeWidgetsSidebar();
  }
});

add_task(async function test_signed_out_resolves_without_notification() {
  const lookup = holdSignedInUser();
  const panel = await openWidgetsSidebar(window, { waitForStatus: false });
  try {
    await panel.updateComplete;
    ok(panel.authentication.isStatusPending, "The status is unknown");
    ok(!panel.shadowRoot.querySelector("#sign-in-prompt"), "No prompt yet");
    ok(!panel.composer && !panel.composeButton, "No composer yet");

    lookup.resolve(null);
    await waitForSignInStatus(panel);
    ok(
      panel.shadowRoot.querySelector("#sign-in-prompt"),
      "The prompt shows after the lookup without a notification"
    );
    ok(!panel.composer && !panel.composeButton, "No composer when signed out");
  } finally {
    lookup.resolve(null);
    await waitForSignInStatus(panel);
    closeWidgetsSidebar();
  }
});

add_task(async function test_signed_in_update_resolves_before_lookup() {
  const lookup = holdSignedInUser();
  const panel = await openWidgetsSidebar(window, { waitForStatus: false });
  try {
    await panel.updateComplete;
    ok(panel.authentication.isStatusPending, "The status is unknown");

    setSignedIn(true);
    await waitForSignInStatus(panel);
    ok(panel.composer, "The composer shows before the lookup settles");
    ok(!panel.shadowRoot.querySelector("#sign-in-prompt"), "No prompt");

    lookup.resolve(null);
    await TestUtils.waitForTick();
    ok(panel.composer, "A late signed-out lookup doesn't replace the status");
  } finally {
    lookup.resolve(null);
    await waitForSignInStatus(panel);
    closeWidgetsSidebar();
  }
});

add_task(async function test_lookup_failure_still_resolves() {
  setUIStatus(UIState.STATUS_NOT_CONFIGURED);
  gGetSignedInUser.rejects(new Error("FxA unavailable"));
  const logged = TestUtils.consoleMessageObserved(msg => {
    const arg = msg.wrappedJSObject.arguments?.[0];
    return typeof arg === "string" && arg.includes("sign-in status");
  });
  const panel = await openWidgetsSidebar(window, { waitForStatus: false });
  try {
    await logged;
    await waitForSignInStatus(panel);
    ok(
      panel.shadowRoot.querySelector("#sign-in-prompt"),
      "The prompt shows after the lookup fails"
    );
    ok(!panel.composer && !panel.composeButton, "No composer when signed out");
  } finally {
    await waitForSignInStatus(panel);
    closeWidgetsSidebar();
  }
});

add_task(async function test_known_status_skips_lookup() {
  for (const [status, signedIn] of [
    [UIState.STATUS_SIGNED_IN, true],
    [UIState.STATUS_LOGIN_FAILED, false],
  ]) {
    setUIStatus(status);
    gGetSignedInUser.resetHistory();
    const panel = await openWidgetsSidebar(window, { waitForStatus: false });
    try {
      await panel.updateComplete;
      ok(
        !panel.authentication.isStatusPending,
        `${status}: The status is known at once`
      );
      is(!!panel.composer, signedIn, `${status}: The composer matches sign-in`);
      is(
        !!panel.shadowRoot.querySelector("#sign-in-prompt"),
        !signedIn,
        `${status}: The prompt matches sign-in`
      );
      ok(
        gGetSignedInUser.notCalled,
        `${status}: A known status needs no lookup`
      );
    } finally {
      await waitForSignInStatus(panel);
      closeWidgetsSidebar();
    }
  }
});

add_task(async function test_close_before_status_known() {
  const lookup = holdSignedInUser();
  const panel = await openWidgetsSidebar(window, { waitForStatus: false });
  await panel.updateComplete;
  ok(
    panel.authentication.isStatusPending,
    "The status is unknown when the panel closes"
  );
  const controller = panel.authentication;
  const win = SidebarController.browser.contentWindow;
  is(win.document, panel.ownerDocument, "The listener is on the panel window");
  const unloaded = new Promise(resolve =>
    win.addEventListener("unload", resolve, { once: true })
  );
  SidebarController.hide();
  await unloaded;
  lookup.resolve(null);
  await TestUtils.waitForCondition(
    () => !controller.isStatusPending,
    "The lookup completion runs after the panel closes"
  );
  await TestUtils.waitForTick();
  gGetSignedInUser.reset();
  gGetSignedInUser.resolves(null);
});
