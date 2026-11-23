/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from head.js */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/antitracking/test/browser/dynamicfpi_head.js",
  this
);

this.PartitionedStorageHelper = {
  _gPrivateWindow: null,

  async _getPrivateWindow() {
    if (!this._gPrivateWindow) {
      this._gPrivateWindow = OpenBrowserWindow({ private: true });
      await TestUtils.topicObserved("browser-delayed-startup-finished");
      registerCleanupFunction(async () => {
        await BrowserTestUtils.closeWindow(this._gPrivateWindow);
        this._gPrivateWindow = null;
      });
    }
    return this._gPrivateWindow;
  },

  runTestInNormalAndPrivateMode(
    name,
    callback,
    cleanupFunction,
    extraPrefs,
    runInSecure = true
  ) {
    // Normal mode
    this.runTest(name, callback, cleanupFunction, extraPrefs, {
      runInSecureContext: runInSecure,
      runInPrivateWindow: false,
    });

    // Private mode
    this.runTest(name, callback, cleanupFunction, extraPrefs, {
      runInSecureContext: runInSecure,
      runInPrivateWindow: true,
    });
  },

  runTest(
    name,
    callback,
    cleanupFunction,
    extraPrefs,
    { runInPrivateWindow = false, runInSecureContext = true } = {}
  ) {
    DynamicFPIHelper.runTest(
      name,
      callback,
      cleanupFunction,
      extraPrefs,
      runInPrivateWindow,
      { runInSecureContext }
    );
  },

  runPartitioningTestInNormalAndPrivateMode(
    name,
    testCategory,
    getDataCallback,
    addDataCallback,
    cleanupFunction,
    expectUnpartition = false
  ) {
    // Normal mode
    this.runPartitioningTest(
      name,
      testCategory,
      getDataCallback,
      addDataCallback,
      cleanupFunction,
      expectUnpartition,
      false
    );

    // Private mode
    this.runPartitioningTest(
      name,
      testCategory,
      getDataCallback,
      addDataCallback,
      cleanupFunction,
      expectUnpartition,
      true
    );
  },

  runPartitioningTest(
    name,
    testCategory,
    getDataCallback,
    addDataCallback,
    cleanupFunction,
    expectUnpartition,
    runInPrivateWindow = false
  ) {
    for (let variant of ["normal", "initial-aboutblank"]) {
      for (let limitForeignContexts of [false, true]) {
        this.runPartitioningTestInner(
          name,
          testCategory,
          getDataCallback,
          addDataCallback,
          cleanupFunction,
          variant,
          runInPrivateWindow,
          limitForeignContexts,
          expectUnpartition
        );
      }
    }
  },

  runPartitioningTestInner(
    name,
    testCategory,
    getDataCallback,
    addDataCallback,
    cleanupFunction,
    variant,
    runInPrivateWindow,
    limitForeignContexts,
    expectUnpartition
  ) {
    add_task(async _ => {
      info(
        "Starting test `" +
          name +
          "' testCategory `" +
          testCategory +
          "' variant `" +
          variant +
          "' in a " +
          (runInPrivateWindow ? "private" : "normal") +
          " window " +
          (limitForeignContexts ? "with" : "without") +
          " limitForeignContexts to check that 2 tabs are correctly partititioned"
      );

      await SpecialPowers.flushPrefEnv();
      await setCookieBehaviorPref(
        BEHAVIOR_PARTITION_FOREIGN,
        runInPrivateWindow
      );
      await SpecialPowers.pushPrefEnv({
        set: [
          ["dom.storage_access.enabled", true],
          ["network.cookie.cookieBehavior.optInPartitioning", true],
          ["network.cookie.CHIPS.enabled", true],
          ["privacy.dynamic_firstparty.limitForeign", limitForeignContexts],
          ["privacy.trackingprotection.enabled", false],
          ["privacy.trackingprotection.pbmode.enabled", false],
          ["privacy.trackingprotection.annotate_channels", true],
          ["dom.security.https_first_pbm", false],
          [
            "privacy.restrict3rdpartystorage.userInteractionRequiredForHosts",
            "not-tracking.example.com",
          ],
          ["security.allow_eval_with_system_principal", true],
        ],
      });

      let win = window;
      if (runInPrivateWindow) {
        win = await PartitionedStorageHelper._getPrivateWindow();
      }

      info("Creating the first tab");
      let tab1 = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE_HTTPS);
      win.gBrowser.selectedTab = tab1;

      let browser1 = win.gBrowser.getBrowserForTab(tab1);
      await BrowserTestUtils.browserLoaded(browser1);

      info("Creating the second tab");
      let tab2 = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE_9);
      win.gBrowser.selectedTab = tab2;

      let browser2 = win.gBrowser.getBrowserForTab(tab2);
      await BrowserTestUtils.browserLoaded(browser2);

      info("Creating the third tab");
      let tab3 = BrowserTestUtils.addTab(
        win.gBrowser,
        TEST_4TH_PARTY_PARTITIONED_PAGE_HTTPS
      );
      win.gBrowser.selectedTab = tab3;

      let browser3 = win.gBrowser.getBrowserForTab(tab3);
      await BrowserTestUtils.browserLoaded(browser3);

      // Use the same URL as first tab to check partitioned data
      info("Creating the forth tab");
      let tab4 = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE_HTTPS);
      win.gBrowser.selectedTab = tab4;

      let browser4 = win.gBrowser.getBrowserForTab(tab4);
      await BrowserTestUtils.browserLoaded(browser4);

      async function getDataFromThirdParty(browser, result) {
        // Overwrite the special case here since third party cookies are not
        // avilable when `limitForeignContexts` is enabled.
        if (testCategory === "cookies" && limitForeignContexts) {
          info("overwrite result to empty");
          result = "";
        }

        await SpecialPowers.spawn(
          browser,
          [
            {
              page:
                TEST_4TH_PARTY_PARTITIONED_PAGE_HTTPS + "?variant=" + variant,
              getDataCallback: getDataCallback.toString(),
              result,
            },
          ],
          async obj => {
            await new content.Promise(resolve => {
              let ifr = content.document.createElement("iframe");
              ifr.onload = __ => {
                info("Sending code to the 3rd party content");
                ifr.contentWindow.postMessage({ cb: obj.getDataCallback }, "*");
              };

              content.addEventListener(
                "message",
                function msg(event) {
                  is(
                    event.data,
                    obj.result,
                    "Partitioned cookie jar has value: " + obj.result
                  );
                  resolve();
                },
                { once: true }
              );

              ifr.src = obj.page;
              content.document.body.appendChild(ifr);
            });
          }
        );
      }

      async function getDataFromFirstParty(browser, result) {
        await SpecialPowers.spawn(
          browser,
          [
            {
              getDataCallback: getDataCallback.toString(),
              result,
              variant,
            },
          ],
          async obj => {
            let runnableStr = `(() => {return (${obj.getDataCallback});})();`;
            let runnable = eval(runnableStr); // eslint-disable-line no-eval
            let win = content;
            if (obj.variant == "initial-aboutblank") {
              let i = win.document.createElement("iframe");
              i.src = "about:blank";
              win.document.body.appendChild(i);
              // override win to make it point to the initial about:blank window
              win = i.contentWindow;
            }

            let result = await runnable.call(content, win);
            is(
              result,
              obj.result,
              "Partitioned cookie jar is empty: " + obj.result
            );
          }
        );
      }

      info("Checking 3rd party has an empty cookie jar in first tab");
      await getDataFromThirdParty(browser1, "");

      info("Checking 3rd party has an empty cookie jar in second tab");
      await getDataFromThirdParty(browser2, "");

      info("Checking first party has an empty cookie jar in third tab");
      await getDataFromFirstParty(browser3, "");

      info("Checking 3rd party has an empty cookie jar in forth tab");
      await getDataFromThirdParty(browser4, "");

      async function createDataInThirdParty(browser, value) {
        await SpecialPowers.spawn(
          browser,
          [
            {
              page:
                TEST_4TH_PARTY_PARTITIONED_PAGE_HTTPS + "?variant=" + variant,
              addDataCallback: addDataCallback.toString(),
              value,
            },
          ],
          async obj => {
            await new content.Promise(resolve => {
              let ifr = content.document.getElementsByTagName("iframe")[0];
              content.addEventListener(
                "message",
                function msg(event) {
                  ok(event.data, "Data created");
                  resolve();
                },
                { once: true }
              );

              ifr.contentWindow.postMessage(
                {
                  cb: obj.addDataCallback,
                  value: obj.value,
                },
                "*"
              );
            });
          }
        );
      }

      async function createDataInFirstParty(browser, value) {
        await SpecialPowers.spawn(
          browser,
          [
            {
              addDataCallback: addDataCallback.toString(),
              value,
              variant,
            },
          ],
          async obj => {
            let runnableStr = `(() => {return (${obj.addDataCallback});})();`;
            let runnable = eval(runnableStr); // eslint-disable-line no-eval
            let win = content;
            if (obj.variant == "initial-aboutblank") {
              let i = win.document.createElement("iframe");
              i.src = "about:blank";
              win.document.body.appendChild(i);
              // override win to make it point to the initial about:blank window
              win = i.contentWindow;
            }

            let result = await runnable.call(content, win, obj.value);
            ok(result, "Data created");
          }
        );
      }

      info("Creating data in the first tab");
      await createDataInThirdParty(browser1, "part=A");

      info("Creating data in the second tab");
      await createDataInThirdParty(browser2, "part=B");

      // Before writing browser4, check data written by browser1
      info("First tab should still have just 'A'");
      await getDataFromThirdParty(browser1, "part=A");
      info("Forth tab should still have just 'A'");
      await getDataFromThirdParty(browser4, "part=A");

      // Ensure to create data in the forth tab before the third tab,
      // otherwise cookie will be written successfully due to prior cookie
      // of the base domain exists.
      info("Creating data in the forth tab");
      await createDataInThirdParty(browser4, "part=D");

      info("Creating data in the third tab");
      await createDataInFirstParty(browser3, "part=C");

      // read all tabs
      info("First tab should be changed to 'D'");
      await getDataFromThirdParty(browser1, "part=D");

      info("Second tab should still have just 'B'");
      await getDataFromThirdParty(browser2, "part=B");

      info("Third tab should still have just 'C'");
      await getDataFromFirstParty(browser3, "part=C");

      info("Forth tab should still have just 'D'");
      await getDataFromThirdParty(browser4, "part=D");

      async function setStorageAccessForThirdParty(browser) {
        info(`Setting permission for ${browser.currentURI.spec}`);
        let type = "3rdPartyStorage^https://not-tracking.example.com";
        let permission = Services.perms.ALLOW_ACTION;
        let expireType = Services.perms.EXPIRE_SESSION;
        Services.perms.addFromPrincipal(
          browser.contentPrincipal,
          type,
          permission,
          expireType,
          0
        );
        // Wait for permission to be set successfully
        let originAttributes = runInPrivateWindow
          ? { privateBrowsingId: 1 }
          : {};
        await new Promise(resolve => {
          let id = setInterval(async _ => {
            if (
              await SpecialPowers.testPermission(type, permission, {
                url: browser.currentURI.spec,
                originAttributes,
              })
            ) {
              clearInterval(id);
              resolve();
            }
          }, 0);
        });
      }

      if (!expectUnpartition) {
        info("Setting Storage access for third parties");

        await setStorageAccessForThirdParty(browser1);
        await setStorageAccessForThirdParty(browser2);
        await setStorageAccessForThirdParty(browser3);
        await setStorageAccessForThirdParty(browser4);

        info("Done setting Storage access for third parties");

        // read all tabs
        info("First tab should still have just 'D'");
        await getDataFromThirdParty(browser1, "part=D");

        info("Second tab should still have just 'B'");
        await getDataFromThirdParty(browser2, "part=B");

        info("Third tab should still have just 'C'");
        await getDataFromFirstParty(browser3, "part=C");

        info("Forth tab should still have just 'D'");
        await getDataFromThirdParty(browser4, "part=D");
      }

      info("Done checking departitioned state");

      info("Removing the tabs");
      BrowserTestUtils.removeTab(tab1);
      BrowserTestUtils.removeTab(tab2);
      BrowserTestUtils.removeTab(tab3);
      BrowserTestUtils.removeTab(tab4);
    });

    add_task(async _ => {
      info("Cleaning up.");
      if (cleanupFunction) {
        await cleanupFunction();
      }

      if (PartitionedStorageHelper._gPrivateWindow) {
        await new Promise(resolve => {
          Services.clearData.deleteDataFromOriginAttributesPattern(
            { privateBrowsingId: 1 },
            () => resolve()
          );
        });
      }
    });
  },
};
