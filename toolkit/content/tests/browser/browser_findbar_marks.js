/* eslint-disable mozilla/no-arbitrary-setTimeout */

// This test verifies that the find scrollbar marks are triggered in the right locations.
// Reftests in layout/xul/reftest are used to verify their appearance.

const TEST_PAGE_URI =
  "data:text/html,<body style='font-size: 20px; margin: 0;'><p style='margin: 0; block-size: 30px;'>This is some fun text.</p><p style='margin-block-start: 2000px; block-size: 30px;'>This is some tex to find.</p><p style='margin-block-start: 500px; block-size: 30px;'>This is some text to find.</p></body>";

let gUpdateCount = 0;

requestLongerTimeout(5);

function initForBrowser(browser) {
  gUpdateCount = 0;

  browser.sendMessageToActor(
    "Finder:EnableMarkTesting",
    { enable: true },
    "Finder"
  );

  let checkFn = event => {
    event.target.lastMarks = event.detail;
    event.target.eventsCount = event.target.eventsCount
      ? event.target.eventsCount + 1
      : 1;
    return false;
  };

  let endFn = BrowserTestUtils.addContentEventListener(
    browser,
    "find-scrollmarks-changed",
    () => {},
    { capture: true },
    checkFn
  );

  return () => {
    browser.sendMessageToActor(
      "Finder:EnableMarkTesting",
      { enable: false },
      "Finder"
    );

    endFn();
  };
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

add_task(async function test_findmarks() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE_URI
  );

  // Open the findbar so that the content scroll size can be measured.
  await promiseFindFinished(gBrowser, "s");

  let browser = tab.linkedBrowser;
  let scrollMaxY = await SpecialPowers.spawn(browser, [], () => {
    return content.scrollMaxY;
  });

  let endFn = initForBrowser(browser);

  for (let step = 0; step < 3; step++) {
    // If the document root or body is absolutely positioned, this can affect the scroll height.
    await SpecialPowers.spawn(browser, [step], stepChild => {
      let document = content.document;
      let adjustments = [
        () => {},
        () => {
          document.documentElement.style.position = "absolute;";
        },
        () => {
          document.documentElement.style.position = "";
          document.body.style.position = "absolute";
        },
      ];

      adjustments[stepChild]();
    });

    // For the first value, get the numbers and ensure that they are approximately
    // in the right place. Later tests should give the same values.
    await promiseFindFinished(gBrowser, "tex", true);

    let values = await getMarks(browser, true);

    // The exact values vary on each platform, so use fuzzy matches.
    // 2610 is the approximate expected document height, and
    // 10, 2040, 2570 are the approximate positions of the marks.
    const expectedDocHeight = 2610;
    isfuzzy(
      values[0],
      Math.round(10 * (scrollMaxY / expectedDocHeight)),
      10,
      "first value"
    );
    isfuzzy(
      values[1],
      Math.round(2040 * (scrollMaxY / expectedDocHeight)),
      10,
      "second value"
    );
    isfuzzy(
      values[2],
      Math.round(2570 * (scrollMaxY / expectedDocHeight)),
      10,
      "third value"
    );

    await doAndVerifyFind(browser, "text", true, [values[0], values[2]]);
    await doAndVerifyFind(browser, "", true, []);
    await doAndVerifyFind(browser, "isz", false, [], true); // marks should not be updated here
    await doAndVerifyFind(browser, "tex", true, values);
    await doAndVerifyFind(browser, "isz", true, []);
    await doAndVerifyFind(browser, "tex", true, values);

    let findbar = await gBrowser.getFindBar();
    let closedPromise = BrowserTestUtils.waitForEvent(findbar, "findbarclose");
    await EventUtils.synthesizeKey("KEY_Escape");
    await closedPromise;

    await verifyFind(browser, "", true, []);
  }

  endFn();

  gBrowser.removeTab(tab);
});

add_task(async function test_findmarks_vertical() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE_URI
  );
  let browser = tab.linkedBrowser;
  let endFn = initForBrowser(browser);

  for (let mode of [
    "sideways-lr",
    "sideways-rl",
    "vertical-lr",
    "vertical-rl",
  ]) {
    const maxMarkPos = await SpecialPowers.spawn(
      browser,
      [mode],
      writingMode => {
        let document = content.document;
        document.documentElement.style.writingMode = writingMode;

        return content.scrollMaxX - content.scrollMinX;
      }
    );

    await promiseFindFinished(gBrowser, "tex", true);
    const marks = await getMarks(browser, true, true);
    Assert.equal(marks.length, 3, `marks count with text "tex"`);
    for (const markPos of marks) {
      // This test is bogus; `0 <= -1 <= 9` as a JS expression returns true,
      // but that doesn't match the intent of the test. Caught by the linter,
      // tracked in bug 1973910.
      // eslint-disable-next-line mozilla/no-comparison-or-assignment-inside-ok
      Assert.ok(
        0 <= markPos <= maxMarkPos,
        `mark position ${markPos} should be in the range 0 ~ ${maxMarkPos}`
      );
    }
  }

  endFn();
  gBrowser.removeTab(tab);
});

// This test verifies what happens when scroll marks are visible and the window is resized.
add_task(async function test_found_resize() {
  let window2 = await BrowserTestUtils.openNewBrowserWindow({});
  let tab = await BrowserTestUtils.openNewForegroundTab(
    window2.gBrowser,
    TEST_PAGE_URI
  );

  let browser = tab.linkedBrowser;
  let endFn = initForBrowser(browser);

  await promiseFindFinished(window2.gBrowser, "tex", true);
  let values = await getMarks(browser, true);

  let resizePromise = BrowserTestUtils.waitForContentEvent(
    browser,
    "resize",
    true
  );
  window2.resizeTo(window2.outerWidth - 100, window2.outerHeight - 80);
  await resizePromise;

  // Some number of extra scrollbar adjustment and painting events can occur
  // when resizing the window, so don't use an exact match for the count.
  let resizedValues = await getMarks(browser, true);
  info(`values: ${JSON.stringify(values)}`);
  info(`resizedValues: ${JSON.stringify(resizedValues)}`);
  isfuzzy(resizedValues[0], values[0], 2, "first value");
  const kSlop = 50;
  Assert.greaterOrEqual(resizedValues[1] - kSlop, values[1], "second value");
  Assert.greaterOrEqual(resizedValues[2] - kSlop, values[2], "third value");

  endFn();

  await BrowserTestUtils.closeWindow(window2);
});

// Returns the scroll marks that should have been assigned
// to the scrollbar after a find. As a side effect, also
// verifies that the marks have been updated since the last
// call to getMarks. If increase is true, then the marks should
// have been updated, and if increase is false, the marks should
// not have been updated.
async function getMarks(browser, increase, shouldBeOnHScrollbar = false) {
  // The marks are updated on a findbar.iteratorTimeout timer, which can fire
  // long after the find itself has been reported as finished.
  let results = await SpecialPowers.spawn(
    browser,
    [increase, gUpdateCount],
    async (shouldWait, lastCount) => {
      if (shouldWait && (content.eventsCount ?? 0) <= lastCount) {
        await ContentTaskUtils.waitForEvent(
          content,
          "find-scrollmarks-changed",
          true
        );
      }

      let { marks, onHorizontalScrollbar } = content.lastMarks;
      content.lastMarks = {};
      return {
        onHorizontalScrollbar,
        marks: marks || [],
        count: content.eventsCount,
      };
    }
  );

  // The marks are updated whenever the scrollbar is updated and
  // this could happen several times as either a find for multiple
  // characters occurs. This check allows for mutliple updates to occur.
  if (increase) {
    Assert.greater(results.count, gUpdateCount, "expected events count");

    Assert.strictEqual(
      results.onHorizontalScrollbar,
      shouldBeOnHScrollbar,
      "marks should be on the horizontal scrollbar"
    );
  } else {
    Assert.equal(results.count, gUpdateCount, "expected events count");
  }

  gUpdateCount = results.count;
  return results.marks;
}

async function doAndVerifyFind(browser, text, increase, expectedMarks) {
  await promiseFindFinished(browser.getTabBrowser(), text, true);
  return verifyFind(browser, text, increase, expectedMarks);
}

async function verifyFind(browser, text, increase, expectedMarks) {
  let foundMarks = await getMarks(browser, increase);

  is(foundMarks.length, expectedMarks.length, "marks count with text " + text);
  for (let t = 0; t < foundMarks.length; t++) {
    isfuzzy(
      foundMarks[t],
      expectedMarks[t],
      5,
      "mark " + t + " with text " + text
    );
  }

  Assert.deepEqual(foundMarks, expectedMarks, "basic find with text " + text);
}
