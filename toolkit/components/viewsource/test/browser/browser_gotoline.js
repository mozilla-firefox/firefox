/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const LINES = 800; // should be enough for any reasonable viewport on automation.
const CONTENT = (() => {
  let c = "";
  for (let i = 1; i <= LINES; ++i) {
    c += `line ${i}\n`;
  }
  return c;
})();

add_task(async function () {
  // First test with text with the text/html mimetype.
  info("Testing text/html");
  let tab = await openDocument("data:text/html," + encodeURIComponent(CONTENT));
  await checkViewSource(tab);
  gBrowser.removeTab(tab);

  info("Testing text/plain");
  tab = await openDocument("data:text/plain," + encodeURIComponent(CONTENT));
  await checkViewSource(tab);
  gBrowser.removeTab(tab);
});

var checkViewSource = async function (aTab) {
  let browser = aTab.linkedBrowser;
  await SpecialPowers.spawn(browser, [CONTENT], async function (text) {
    is(content.document.body.textContent, text, "Correct content loaded");
  });

  // Let's cover beginning and end, forwards and backwards.
  for (let lineNumber of [1, 100, 800, 100, 1]) {
    info(`Going to line ${lineNumber}`);
    browser.sendMessageToActor(
      "ViewSource:GoToLine",
      {
        lineNumber,
      },
      "ViewSourcePage"
    );
    await SpecialPowers.spawn(browser, [lineNumber], async function (i) {
      let selection = content.getSelection();
      Assert.equal(
        selection.toString().trim(),
        "line " + i,
        "Correct text selected"
      );

      // Scrolling to the selection is async.
      await new Promise(r => {
        content.requestAnimationFrame(() => content.requestAnimationFrame(r));
      });

      let foundNonEmptyRect = false;
      for (let rect of selection.getRangeAt(0).getClientRects()) {
        if (rect.width == 0 || rect.height == 0) {
          continue;
        }
        info(`Testing selection rect: ${JSON.stringify(rect)}`);
        info(`Current scroll position: ${content.scrollY}`);
        foundNonEmptyRect = true;
        Assert.greaterOrEqual(
          rect.top,
          0,
          "Top of the selection should be in viewport (top)"
        );
        Assert.lessOrEqual(
          rect.bottom,
          content.innerHeight,
          "Top of the selection should be in viewport (bottom)"
        );
      }
      Assert.ok(foundNonEmptyRect, "Should have some non-empty rects");
    });
  }

  // A line past the end isn't found, so the parent shows "Line not found".
  await SpecialPowers.spawn(browser, [LINES * 10], async function (i) {
    let actor = content.windowGlobalChild.getActor("ViewSourcePage");
    Assert.equal(actor.findLocation(i), null, "Line past the end not found");
  });
};
