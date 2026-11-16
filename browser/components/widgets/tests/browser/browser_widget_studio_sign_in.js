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
