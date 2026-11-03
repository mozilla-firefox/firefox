"use strict";

add_task(async function testWebNavigationGetNonExistentTab() {
  let extension = ExtensionTestUtils.loadExtension({
    background: async function () {
      // There is no "tabId = 0" because the id assigned by tabTracker (defined in ext-browser.js)
      // starts from 1.
      await browser.test.assertRejects(
        browser.webNavigation.getAllFrames({ tabId: 0 }),
        "Invalid tab ID: 0",
        "getAllFrames rejected Promise should pass the expected error"
      );

      // There is no "tabId = 0" because the id assigned by tabTracker (defined in ext-browser.js)
      // starts from 1, processId is currently marked as optional and it is ignored.
      await browser.test.assertRejects(
        browser.webNavigation.getFrame({
          tabId: 0,
          frameId: 15,
          processId: 20,
        }),
        "Invalid tab ID: 0",
        "getFrame rejected Promise should pass the expected error"
      );

      browser.test.sendMessage("getNonExistentTab.done");
    },
    manifest: {
      permissions: ["webNavigation"],
    },
  });

  await extension.startup();

  await extension.awaitMessage("getNonExistentTab.done");

  await extension.unload();
});

add_task(async function testWebNavigationFrames() {
  let extension = ExtensionTestUtils.loadExtension({
    background: async function () {
      let tabId;
      let collectedDetails = [];

      browser.webNavigation.onCompleted.addListener(async details => {
        collectedDetails.push(details);

        if (details.frameId !== 0) {
          // wait for the top level iframe to be complete
          return;
        }

        let getAllFramesDetails = await browser.webNavigation.getAllFrames({
          tabId,
        });

        let getFramePromises = getAllFramesDetails.map(({ frameId }) => {
          // processId is currently marked as optional and it is ignored.
          return browser.webNavigation.getFrame({
            tabId,
            frameId,
            processId: 0,
          });
        });

        let getFrameResults = await Promise.all(getFramePromises);
        browser.test.sendMessage("webNavigationFrames.done", {
          collectedDetails,
          getAllFramesDetails,
          getFrameResults,
        });

        // Pick a random frameId.
        let nonExistentFrameId = Math.floor(Math.random() * 10000);

        // Increment the picked random nonExistentFrameId until it doesn't exists.
        while (
          getAllFramesDetails.filter(
            details => details.frameId == nonExistentFrameId
          ).length
        ) {
          nonExistentFrameId += 1;
        }

        // Check that getFrame Promise is rejected with the expected error message on nonexistent frameId.
        await browser.test.assertRejects(
          browser.webNavigation.getFrame({
            tabId,
            frameId: nonExistentFrameId,
            processId: 20,
          }),
          `No frame found with frameId: ${nonExistentFrameId}`,
          "getFrame promise should be rejected with the expected error message on unexistent frameId"
        );

        await browser.tabs.remove(tabId);
        browser.test.sendMessage("webNavigationFrames.done");
      });

      let tab = await browser.tabs.create({ url: "tab.html" });
      tabId = tab.id;
    },
    manifest: {
      permissions: ["webNavigation", "tabs"],
    },
    files: {
      "tab.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
          </head>
          <body>
            <iframe src="subframe.html"></iframe>
            <iframe src="subframe.html"></iframe>
          </body>
        </html>
      `,
      "subframe.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
          </head>
        </html>
      `,
    },
  });

  await extension.startup();

  let { collectedDetails, getAllFramesDetails, getFrameResults } =
    await extension.awaitMessage("webNavigationFrames.done");

  is(getAllFramesDetails.length, 3, "expected number of frames found");
  is(
    getAllFramesDetails.length,
    collectedDetails.length,
    "number of frames found should equal the number onCompleted events collected"
  );

  is(
    getAllFramesDetails[0].frameId,
    0,
    "the root frame has the expected frameId"
  );
  is(
    getAllFramesDetails[0].parentFrameId,
    -1,
    "the root frame has the expected parentFrameId"
  );

  // ordered by frameId
  let sortByFrameId = (el1, el2) => {
    let val1 = el1 ? el1.frameId : -1;
    let val2 = el2 ? el2.frameId : -1;
    return val1 - val2;
  };

  collectedDetails = collectedDetails.sort(sortByFrameId);
  getAllFramesDetails = getAllFramesDetails.sort(sortByFrameId);
  getFrameResults = getFrameResults.sort(sortByFrameId);

  info("check frame details content");

  is(
    getFrameResults.length,
    getAllFramesDetails.length,
    "getFrame and getAllFrames should return the same number of results"
  );

  Assert.deepEqual(
    getFrameResults,
    getAllFramesDetails,
    "getFrame and getAllFrames should return the same results"
  );

  info(`check frame details collected and retrieved with getAllFrames`);

  for (let [i, collected] of collectedDetails.entries()) {
    let getAllFramesDetail = getAllFramesDetails[i];

    is(getAllFramesDetail.frameId, collected.frameId, "frameId");
    is(
      getAllFramesDetail.parentFrameId,
      collected.parentFrameId,
      "parentFrameId"
    );
    is(getAllFramesDetail.tabId, collected.tabId, "tabId");
    is(getAllFramesDetail.url, collected.url, "url");
  }

  info("frame details content checked");

  await extension.awaitMessage("webNavigationFrames.done");

  await extension.unload();
});

add_task(async function testWebNavigationGetFrameOnDiscardedTab() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs", "webNavigation"],
    },
    async background() {
      let tabs = await browser.tabs.query({ currentWindow: true });
      browser.test.assertEq(2, tabs.length, "Expect 2 tabs open");

      const tabId = tabs[1].id;

      await browser.tabs.discard(tabId);
      let tab = await browser.tabs.get(tabId);
      browser.test.assertEq(true, tab.discarded, "Expect a discarded tab");

      const allFrames = await browser.webNavigation.getAllFrames({ tabId });
      browser.test.assertEq(
        null,
        allFrames,
        "Expect null from calling getAllFrames on discarded tab"
      );

      tab = await browser.tabs.get(tabId);
      browser.test.assertEq(
        true,
        tab.discarded,
        "Expect tab to stay discarded"
      );

      const topFrame = await browser.webNavigation.getFrame({
        tabId,
        frameId: 0,
      });
      browser.test.assertEq(
        null,
        topFrame,
        "Expect null from calling getFrame on discarded tab"
      );

      tab = await browser.tabs.get(tabId);
      browser.test.assertEq(
        true,
        tab.discarded,
        "Expect tab to stay discarded"
      );

      browser.test.sendMessage("get-frames-done");
    },
  });

  const initialTab = gBrowser.selectedTab;
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://mochi.test:8888/?toBeDiscarded=true"
  );
  // Switch back to the initial tab to allow the new tab
  // to be discarded.
  await BrowserTestUtils.switchTab(gBrowser, initialTab);

  ok(!!tab.linkedPanel, "Tab not initially discarded");

  await extension.startup();
  await extension.awaitMessage("get-frames-done");

  ok(!tab.linkedPanel, "Tab should be discarded");

  BrowserTestUtils.removeTab(tab);

  await extension.unload();
});

add_task(async function testWebNavigationCrossOriginFrames() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["webNavigation"],
    },
    async background() {
      let url =
        "http://mochi.test:8888/tests/toolkit/components/extensions/test/mochitest/file_contains_iframe.html";
      let tab = await browser.tabs.create({ url });

      await new Promise(resolve => {
        browser.webNavigation.onCompleted.addListener(details => {
          if (details.tabId === tab.id && details.frameId === 0) {
            resolve();
          }
        });
      });

      let frames = await browser.webNavigation.getAllFrames({ tabId: tab.id });
      browser.test.assertEq(frames[0].url, url, "Top is from mochi.test");

      await browser.tabs.remove(tab.id);
      browser.test.sendMessage("webNavigation.CrossOriginFrames", frames);
    },
  });

  await extension.startup();

  let frames = await extension.awaitMessage("webNavigation.CrossOriginFrames");
  is(frames.length, 2, "getAllFrames() returns both frames.");

  is(frames[0].frameId, 0, "Top frame has correct frameId.");
  is(frames[0].parentFrameId, -1, "Top parentFrameId is correct.");

  Assert.greater(
    frames[1].frameId,
    0,
    "Cross-origin iframe has non-zero frameId."
  );
  is(frames[1].parentFrameId, 0, "Iframe parentFrameId is correct.");
  is(
    frames[1].url,
    "http://example.org/tests/toolkit/components/extensions/test/mochitest/file_contains_img.html",
    "Irame is from example.org"
  );

  await extension.unload();
});
