/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/genai/GenAI.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const DEFAULT_IGNORE_FIELDS = "input,moz-multiline-editor";

/**
 * Check that textareas are ignored only while the Highlight to Search feature
 * gate is on, so existing chatbot users keep the menu there until then.
 */
add_task(async function test_textarea_ignored_with_feature_gate() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.ignoreFields", DEFAULT_IGNORE_FIELDS]],
  });
  Assert.ok(
    !GenAI.ignoredInputs.has("textarea"),
    "Textareas are not ignored while the feature gate is off"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.highlightToSearch.featureGate", true]],
  });
  Assert.ok(
    GenAI.ignoredInputs.has("textarea"),
    "Textareas are ignored while the feature gate is on"
  );
  Assert.ok(GenAI.ignoredInputs.has("input"), "Inputs are still ignored");

  await SpecialPowers.popPrefEnv();
  Assert.ok(
    !GenAI.ignoredInputs.has("textarea"),
    "Textareas are shown again once the feature gate is off"
  );
  await SpecialPowers.popPrefEnv();
});

async function selectTextareaAndMouseUp(browser) {
  await SimpleTest.promiseFocus(browser);
  await BrowserTestUtils.synthesizeMouse(
    browser,
    10,
    10,
    { type: "mousedown" },
    browser
  );
  await SpecialPowers.spawn(browser, [], () => {
    const textarea = content.document.querySelector("textarea");
    textarea.focus();
    textarea.select();
  });
  await BrowserTestUtils.synthesizeMouse(
    browser,
    60,
    10,
    { type: "mouseup" },
    browser
  );
}

/**
 * Check that selecting text in a textarea shows no menu while the feature gate
 * is on, and still does while it is off.
 */
add_task(async function test_textarea_selection_menu() {
  const panel = document.getElementById("selection-shortcut-action-panel");
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");

  for (const featureGate of [true, false]) {
    spy.resetHistory();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", featureGate],
        ["browser.ml.chat.shortcuts.ignoreFields", DEFAULT_IGNORE_FIELDS],
        ["browser.ml.chat.shortcuts", true],
        ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
        ["browser.ml.chat.provider", "http://localhost:8080"],
      ],
    });

    await BrowserTestUtils.withNewTab(
      "data:text/html,<textarea>some text</textarea>",
      async browser => {
        await selectTextareaAndMouseUp(browser);
        await TestUtils.waitForCondition(
          () => spy.calledWith("GenAI:ShowShortcuts"),
          "Actor offered the textarea selection to the parent"
        );

        if (featureGate) {
          Assert.equal(
            panel.state,
            "closed",
            "No menu for a textarea while the feature gate is on"
          );
        } else {
          await TestUtils.waitForCondition(
            () => panel.getAttribute("panelopen") === "true",
            "Menu opens for a textarea while the feature gate is off"
          );
          const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
          panel.hide();
          await hidden;
        }
      }
    );

    await SpecialPowers.popPrefEnv();
  }

  spy.restore();
});

/**
 * Check which sites are ignored: the defaults, their subdomains, and nothing
 * that merely ends with the same letters or has no host.
 */
add_task(async function test_ignored_sites_matching() {
  for (const spec of [
    "https://docs.google.com/document/d/1/edit",
    "https://example.atlassian.net/wiki/spaces/A",
    "https://atlassian.net/",
    "https://www.notion.so/a-page",
    "https://someone.notion.site/a-page",
  ]) {
    Assert.ok(
      GenAI.isIgnoredSite(Services.io.newURI(spec)),
      `${spec} is ignored`
    );
  }
  for (const spec of [
    "https://www.google.com/",
    "https://notatlassian.net/",
    "https://example.org/",
    "about:blank",
  ]) {
    Assert.ok(
      !GenAI.isIgnoredSite(Services.io.newURI(spec)),
      `${spec} is not ignored`
    );
  }
  Assert.ok(!GenAI.isIgnoredSite(null), "A missing URI is not ignored");
});

/**
 * Check that the menu is not shown for a selection on an ignored site or its
 * subdomains, judged by the frame the selection is in, but only while the
 * Highlight to Search feature gate is on.
 */
add_task(async function test_ignored_sites_menu() {
  const panel = document.getElementById("selection-shortcut-action-panel");
  const spy = sinon.spy(GenAI, "handleShortcutsMessage");
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.highlightToSearch.ignoredSites", "example.com"]],
  });

  for (const [url, frameUrl, featureGate, shouldShow, message] of [
    ["https://example.com/", null, true, false, "No menu on an ignored site"],
    ["https://www.example.com/", null, true, false, "No menu on its subdomain"],
    ["https://example.org/", null, true, true, "Menu shows on another site"],
    [
      "https://example.com/",
      null,
      false,
      true,
      "Menu shows on an ignored site while the feature gate is off",
    ],
    [
      "https://example.org/",
      "https://example.com/",
      true,
      false,
      "No menu for a selection in an ignored frame on another site",
    ],
    [
      "https://example.com/",
      "https://example.org/",
      true,
      true,
      "Menu shows for a selection in another frame on an ignored site",
    ],
  ]) {
    spy.resetHistory();
    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.highlightToSearch.featureGate", featureGate],
        ["browser.ml.chat.shortcuts", true],
        ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
        ["browser.ml.chat.provider", "http://localhost:8080"],
      ],
    });

    await BrowserTestUtils.withNewTab(url, async browser => {
      if (frameUrl) {
        await SpecialPowers.spawn(browser, [frameUrl], async src => {
          const frame = content.document.createElement("iframe");
          frame.src = src;
          const loaded = ContentTaskUtils.waitForEvent(frame, "load");
          content.document.body.appendChild(frame);
          await loaded;
        });
        const frameContext = browser.browsingContext.children[0];
        await SimpleTest.promiseFocus(browser);
        await BrowserTestUtils.synthesizeMouse(
          "body",
          10,
          10,
          { type: "mousedown" },
          frameContext
        );
        await SpecialPowers.spawn(frameContext, [], () => {
          content.getSelection().selectAllChildren(content.document.body);
        });
        await BrowserTestUtils.synthesizeMouse(
          "body",
          60,
          10,
          { type: "mouseup" },
          frameContext
        );
      } else {
        await selectAllAndMouseUp(browser);
      }
      await TestUtils.waitForCondition(
        () => spy.calledWith("GenAI:ShowShortcuts"),
        "Actor offered the selection to the parent"
      );

      if (shouldShow) {
        await TestUtils.waitForCondition(
          () => panel.getAttribute("panelopen") === "true",
          message
        );
        const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
        panel.hide();
        await hidden;
      } else {
        Assert.equal(panel.state, "closed", message);
      }
    });

    await SpecialPowers.popPrefEnv();
  }

  Assert.deepEqual(
    Glean.selectionMenu.suppressed.testGetValue().map(e => e.extra.reason),
    ["site_list", "site_list", "site_list"],
    "Each suppressed selection is recorded with its reason"
  );

  spy.restore();
  await SpecialPowers.popPrefEnv();
});
