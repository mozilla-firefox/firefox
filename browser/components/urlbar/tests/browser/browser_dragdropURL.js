/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for draging and dropping to the Urlbar.
 */

const TEST_URL = "data:text/html,a test page";

add_setup(async function () {
  // Stop search-engine loads from hitting the network.
  await SearchTestUtils.updateRemoteSettingsConfig([{ identifier: "engine" }]);

  CustomizableUI.addWidgetToArea("home-button", "nav-bar");
  registerCleanupFunction(() => {
    CustomizableUI.removeWidgetFromArea("home-button");
    Services.prefs.clearUserPref("browser.engagement.home-button.has-removed");
  });
});

/**
 * Simulates a drop on the URL bar input field.
 * The drag source must be something different from the URL bar,
 * so we pick the home button somewhat arbitrarily.
 *
 * @param {object} content
 *   A {type, data} object representing the DND content.
 */
function simulateURLBarDrop(content) {
  EventUtils.synthesizeDrop(
    document.getElementById("home-button"), // Dragstart element.
    gURLBar.inputField, // Drop element.
    [[content]], // Drag data.
    "copy",
    window
  );
}

add_task(async function checkDragURL() {
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    info("Check dragging a normal url to the urlbar");
    const DRAG_URL = "https://www.example.com/";
    simulateURLBarDrop({ type: "text/plain", data: DRAG_URL });

    // For safety reasons, in the drop case we don't want to
    // immediately show the dropped value.
    Assert.equal(
      gURLBar.value,
      TEST_URL,
      "URL bar value should not have changed"
    );
    Assert.equal(
      gBrowser.selectedBrowser.userTypedValue,
      null,
      "Stored URL bar value should not have changed"
    );

    await BrowserTestUtils.browserLoaded(browser, false, DRAG_URL);
    Assert.equal(
      gURLBar.untrimmedValue,
      DRAG_URL,
      "URL bar value changes after load"
    );
  });
});

add_task(async function checkDragForbiddenURL() {
  await BrowserTestUtils.withNewTab(TEST_URL, function () {
    // See also browser_removeUnsafeProtocolsFromURLBarPaste.js for other
    // examples. In general we trust that function, we pick some testcases to
    // ensure we disallow dropping trimmed text.
    for (let url of [
      "chrome://browser/content/aboutDialog.xhtml",
      "file:///",
      "javascript:",
      "javascript:void(0)",
      "java\r\ns\ncript:void(0)",
      " javascript:void(0)",
      "\u00A0java\nscript:void(0)",
      "javascript:document.domain",
      "javascript:javascript:alert('hi!')",
    ]) {
      info(`Check dragging "${url}" to the URL bar`);
      simulateURLBarDrop({ type: "text/plain", data: url });
      Assert.notEqual(
        gURLBar.value,
        url,
        `Shouldn't be allowed to drop ${url} on URL bar`
      );
    }
  });
});

add_task(async function checkDragText() {
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    info("Check dragging multi word text to the urlbar");
    const TEXT = "Firefox is awesome";
    const TEXT_URL = "https://www.example.com/search?q=Firefox+is+awesome";
    let promiseLoad = BrowserTestUtils.browserLoaded(browser, false, TEXT_URL);
    simulateURLBarDrop({ type: "text/plain", data: TEXT });
    await promiseLoad;

    info("Check dragging single word text to the urlbar");
    const WORD = "Firefox";
    const WORD_URL = "https://www.example.com/search?q=Firefox";
    promiseLoad = BrowserTestUtils.browserLoaded(browser, false, WORD_URL);
    simulateURLBarDrop({ type: "text/plain", data: WORD });
    await promiseLoad;
  });
});

add_task(async function checkDragFromURLBarHTMLEscaping() {
  info(
    "Check that title and href are HTML-escaped in the text/html drag flavor"
  );
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    const TITLE = `A & B < C > D "E" 'F'`;
    await SpecialPowers.spawn(browser, [TITLE], title => {
      content.document.title = title;
    });
    await TestUtils.waitForCondition(
      () => window.gBrowser.contentTitle == TITLE,
      "Waiting for title to change"
    );

    gURLBar.focus();
    gURLBar.select();

    let dataTransfer = new DataTransfer();
    gURLBar.inputField.dispatchEvent(
      new DragEvent("dragstart", { dataTransfer, bubbles: true })
    );

    const ESCAPED_TITLE = "A &amp; B &lt; C &gt; D &quot;E&quot; &#39;F&#39;";
    let html = dataTransfer.getData("text/html");
    Assert.ok(html.length, "text/html drag data was set");
    Assert.ok(
      html.includes(ESCAPED_TITLE),
      "title is HTML-escaped in drag data"
    );
  });
});
