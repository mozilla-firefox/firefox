/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from head.js */

"use strict";

var gFeatures = undefined;
var gTestTrackersCleanedUp = false;
var gTestTrackersCleanupRegistered = false;
let gPrivateWindow = null;

this.AntiTracking = {
  runTestInNormalAndPrivateMode(
    name,
    callbackTracking,
    callbackNonTracking,
    cleanupFunction,
    extraPrefs,
    windowOpenTest = true,
    userInteractionTest = true,
    expectedBlockingNotifications = Ci.nsIWebProgressListener
      .STATE_COOKIES_BLOCKED_TRACKER,
    iframeSandbox = null,
    accessRemoval = null,
    callbackAfterRemoval = null,
    iframeAllow = null
  ) {
    // Normal mode
    this.runTest(
      name,
      callbackTracking,
      callbackNonTracking,
      cleanupFunction,
      extraPrefs,
      windowOpenTest,
      userInteractionTest,
      expectedBlockingNotifications,
      false,
      iframeSandbox,
      accessRemoval,
      callbackAfterRemoval,
      iframeAllow
    );

    // Private mode
    this.runTest(
      name,
      callbackTracking,
      callbackNonTracking,
      cleanupFunction,
      extraPrefs,
      windowOpenTest,
      userInteractionTest,
      expectedBlockingNotifications,
      true,
      iframeSandbox,
      accessRemoval,
      callbackAfterRemoval,
      iframeAllow
    );
  },

  runTest(
    name,
    callbackTracking,
    callbackNonTracking,
    cleanupFunction,
    extraPrefs,
    windowOpenTest = true,
    userInteractionTest = true,
    expectedBlockingNotifications = Ci.nsIWebProgressListener
      .STATE_COOKIES_BLOCKED_TRACKER,
    runInPrivateWindow = false,
    iframeSandbox = null,
    accessRemoval = null,
    callbackAfterRemoval = null,
    iframeAllow = null
  ) {
    let runExtraTests = true;
    let options = {};
    if (typeof callbackNonTracking == "object" && !!callbackNonTracking) {
      options.callback = callbackNonTracking.callback;
      runExtraTests = callbackNonTracking.runExtraTests;
      if ("cookieBehavior" in callbackNonTracking) {
        options.cookieBehavior = callbackNonTracking.cookieBehavior;
      } else {
        options.cookieBehavior = BEHAVIOR_ACCEPT;
      }
      if ("expectedBlockingNotifications" in callbackNonTracking) {
        options.expectedBlockingNotifications =
          callbackNonTracking.expectedBlockingNotifications;
      } else {
        options.expectedBlockingNotifications = 0;
      }
      if ("blockingByAllowList" in callbackNonTracking) {
        options.blockingByAllowList = callbackNonTracking.blockingByAllowList;
        if (options.blockingByAllowList) {
          // If we're on the allow list, there won't be any blocking!
          options.expectedBlockingNotifications = 0;
        }
      } else {
        options.blockingByAllowList = false;
      }
      callbackNonTracking = options.callback;
      options.accessRemoval = null;
      options.callbackAfterRemoval = null;
    }

    if (runInPrivateWindow) {
      this._openPrivateWindowTask();
    }

    // Here we want to test that a 3rd party context is simply blocked.
    this._createTask({
      name,
      cookieBehavior: BEHAVIOR_REJECT_TRACKER,
      allowList: false,
      callback: callbackTracking,
      extraPrefs,
      expectedBlockingNotifications,
      runInPrivateWindow,
      iframeSandbox,
      accessRemoval,
      callbackAfterRemoval,
      iframeAllow,
    });
    this._createCleanupTask(cleanupFunction);

    if (callbackNonTracking) {
      // Phase 1: Here we want to test that a 3rd party context is not blocked if pref is off.
      if (runExtraTests) {
        // There are five ways in which the third-party context may not be blocked:
        //   * If the cookieBehavior pref causes it to not be blocked.
        //   * If the contentBlocking pref causes it to not be blocked.
        //   * If both of these prefs cause it to not be blocked.
        //   * If the top-level page is on the content blocking allow list.
        //   * If the contentBlocking third-party cookies UI pref is off, the allow list will be ignored.
        // All of these cases are tested here.
        this._createTask({
          name,
          cookieBehavior: BEHAVIOR_ACCEPT,
          allowList: false,
          callback: callbackNonTracking,
          extraPrefs,
          expectedBlockingNotifications: 0,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval: null, // only passed with non-blocking callback
          callbackAfterRemoval: null,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);

        this._createTask({
          name,
          cookieBehavior: BEHAVIOR_ACCEPT,
          allowList: true,
          callback: callbackNonTracking,
          extraPrefs,
          expectedBlockingNotifications: 0,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval: null, // only passed with non-blocking callback
          callbackAfterRemoval: null,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);

        this._createTask({
          name,
          cookieBehavior: BEHAVIOR_REJECT,
          allowList: false,
          callback: callbackTracking,
          extraPrefs,
          expectedBlockingNotifications: expectedBlockingNotifications
            ? Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_ALL
            : 0,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval: null, // only passed with non-blocking callback
          callbackAfterRemoval: null,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);

        if (!AppConstants.DEBUG) {
          this._createTask({
            name,
            cookieBehavior: BEHAVIOR_LIMIT_FOREIGN,
            allowList: true,
            callback: callbackNonTracking,
            extraPrefs,
            expectedBlockingNotifications: 0,
            runInPrivateWindow,
            iframeSandbox,
            accessRemoval: null, // only passed with non-blocking callback
            callbackAfterRemoval: null,
            iframeAllow,
          });
          this._createCleanupTask(cleanupFunction);
        }

        this._createTask({
          name: name + " reject foreign with exception",
          cookieBehavior: BEHAVIOR_REJECT_FOREIGN,
          allowList: true,
          callback: callbackNonTracking,
          extraPrefs: [...(extraPrefs || [])],
          expectedBlockingNotifications: 0,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval,
          callbackAfterRemoval,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);

        this._createTask({
          name,
          cookieBehavior: BEHAVIOR_REJECT_FOREIGN,
          allowList: false,
          callback: callbackTracking,
          extraPrefs: [...(extraPrefs || [])],
          expectedBlockingNotifications: expectedBlockingNotifications
            ? Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_FOREIGN
            : 0,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval,
          callbackAfterRemoval,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);

        if (!AppConstants.DEBUG) {
          this._createTask({
            name,
            cookieBehavior: BEHAVIOR_REJECT_TRACKER,
            allowList: true,
            callback: callbackNonTracking,
            extraPrefs,
            expectedBlockingNotifications: 0,
            runInPrivateWindow,
            iframeSandbox,
            accessRemoval,
            callbackAfterRemoval,
            iframeAllow,
          });
          this._createCleanupTask(cleanupFunction);

          this._createTask({
            name,
            cookieBehavior: BEHAVIOR_REJECT_TRACKER,
            allowList: false,
            callback: callbackNonTracking,
            extraPrefs,
            expectedBlockingNotifications: false,
            runInPrivateWindow,
            iframeSandbox,
            accessRemoval: null, // only passed with non-blocking callback
            callbackAfterRemoval: null,
            thirdPartyPage: TEST_ANOTHER_3RD_PARTY_PAGE,
            iframeAllow,
          });
          this._createCleanupTask(cleanupFunction);
        }
      } else {
        // This is only used for imageCacheWorker.js tests
        this._createTask({
          name,
          cookieBehavior: options.cookieBehavior,
          allowList: options.blockingByAllowList,
          callback: options.callback,
          extraPrefs,
          expectedBlockingNotifications: options.expectedBlockingNotifications,
          runInPrivateWindow,
          iframeSandbox,
          accessRemoval: options.accessRemoval,
          callbackAfterRemoval: options.callbackAfterRemoval,
          iframeAllow,
        });
        this._createCleanupTask(cleanupFunction);
      }

      // Phase 2: Here we want to test that a third-party context doesn't
      // get blocked with when the same origin is opened through window.open().
      if (windowOpenTest) {
        this._createWindowOpenTask(
          name,
          BEHAVIOR_REJECT_TRACKER,
          callbackTracking,
          callbackNonTracking,
          runInPrivateWindow,
          iframeSandbox,
          false,
          extraPrefs,
          iframeAllow
        );
        this._createCleanupTask(cleanupFunction);

        // Now, check if it works for nested iframes.
        this._createWindowOpenTask(
          name,
          BEHAVIOR_REJECT_TRACKER,
          callbackTracking,
          callbackNonTracking,
          runInPrivateWindow,
          iframeSandbox,
          true,
          extraPrefs,
          iframeAllow
        );
        this._createCleanupTask(cleanupFunction);
      }

      // Phase 3: Here we want to test that a third-party context doesn't
      // get blocked with user interaction present
      if (userInteractionTest) {
        this._createUserInteractionTask(
          name,
          BEHAVIOR_REJECT_TRACKER,
          callbackTracking,
          callbackNonTracking,
          runInPrivateWindow,
          iframeSandbox,
          false,
          extraPrefs,
          iframeAllow
        );
        this._createCleanupTask(cleanupFunction);

        // Now, check if it works for nested iframes.
        this._createUserInteractionTask(
          name,
          BEHAVIOR_REJECT_TRACKER,
          callbackTracking,
          callbackNonTracking,
          runInPrivateWindow,
          iframeSandbox,
          true,
          extraPrefs,
          iframeAllow
        );
        this._createCleanupTask(cleanupFunction);
      }
    }

    if (runInPrivateWindow) {
      this._closePrivateWindowTask();
    }
  },

  _waitObserver(targetTopic, expectedCount) {
    let cnt = 0;

    return new Promise(resolve => {
      Services.obs.addObserver(function observer(subject, topic) {
        if (topic != targetTopic) {
          return;
        }
        cnt++;

        if (cnt != expectedCount) {
          return;
        }

        Services.obs.removeObserver(observer, targetTopic);
        resolve();
      }, targetTopic);
    });
  },

  _waitUserInteractionPerm() {
    return this._waitObserver(
      "antitracking-test-user-interaction-perm-added",
      1
    );
  },

  _waitStorageAccessPerm(expectedCount) {
    return this._waitObserver(
      "antitracking-test-storage-access-perm-added",
      expectedCount
    );
  },

  async interactWithTracker() {
    let win = await BrowserTestUtils.openNewBrowserWindow();
    await BrowserTestUtils.withNewTab(
      { gBrowser: win.gBrowser, url: TEST_3RD_PARTY_PAGE },
      async function (browser) {
        info("Let's interact with the tracker");

        await SpecialPowers.spawn(browser, [], async function () {
          SpecialPowers.wrap(content.document).userInteractionForTesting();
        });
      }
    );
    await BrowserTestUtils.closeWindow(win);
  },

  async _setupTest(win, cookieBehavior, runInPrivateWindow, extraPrefs) {
    await SpecialPowers.flushPrefEnv();

    await setCookieBehaviorPref(cookieBehavior, runInPrivateWindow);
    await SpecialPowers.pushPrefEnv({
      set: [
        ["dom.storage_access.enabled", true],
        ["privacy.trackingprotection.enabled", false],
        ["privacy.trackingprotection.pbmode.enabled", false],
        ["dom.security.https_first_pbm", false],
        [
          "privacy.trackingprotection.annotate_channels",
          cookieBehavior != BEHAVIOR_ACCEPT,
        ],
        ["privacy.restrict3rdpartystorage.console.lazy", false],
        [
          "privacy.restrict3rdpartystorage.userInteractionRequiredForHosts",
          "tracking.example.com,tracking.example.org",
        ],
        ["privacy.antitracking.testing", true],
        ["network.lna.block_trackers", false],
      ],
    });

    if (extraPrefs && Array.isArray(extraPrefs) && extraPrefs.length) {
      await SpecialPowers.pushPrefEnv({ set: extraPrefs });

      let enableWebcompat = Services.prefs.getBoolPref(
        "privacy.antitracking.enableWebcompat"
      );

      // If the skip list is disabled by pref, it will always return an empty
      // list.
      if (enableWebcompat) {
        for (let item of extraPrefs) {
          // When setting up exception URLs, we need to wait to ensure our prefs
          // actually take effect.  In order to do this, we set up a exception
          // list observer and wait until it calls us back.
          if (item[0] == "urlclassifier.trackingAnnotationSkipURLs") {
            info("Waiting for the exception list service to initialize...");
            let classifier = Cc[
              "@mozilla.org/url-classifier/dbservice;1"
            ].getService(Ci.nsIURIClassifier);
            let feature = classifier.getFeatureByName("tracking-annotation");
            await TestUtils.waitForCondition(() => {
              for (let x of item[1].split(",")) {
                if (
                  feature.exceptionList
                    .testGetEntries()
                    .some(e => e.urlPattern == x)
                ) {
                  return true;
                }
              }
              return false;
            }, "Exception list service initialized");
            break;
          }
        }
      }
    }

    await UrlClassifierTestUtils.addTestTrackers();
    if (!gTestTrackersCleanupRegistered) {
      registerCleanupFunction(_ => {
        if (gTestTrackersCleanedUp) {
          return;
        }
        UrlClassifierTestUtils.cleanupTestTrackers();
        gTestTrackersCleanedUp = true;
      });
      gTestTrackersCleanupRegistered = true;
    }
  },

  _createTask(options) {
    add_task(async function () {
      info(
        "Starting " +
          (options.cookieBehavior != BEHAVIOR_ACCEPT
            ? "blocking"
            : "non-blocking") +
          " cookieBehavior (" +
          options.cookieBehavior +
          ") with" +
          (options.allowList ? "" : "out") +
          " allow list test " +
          options.name +
          " running in a " +
          (options.runInPrivateWindow ? "private" : "normal") +
          " window " +
          " with iframe sandbox set to " +
          options.iframeSandbox +
          " and iframe allow set to " +
          options.iframeAllow +
          " and access removal set to " +
          options.accessRemoval +
          (typeof options.thirdPartyPage == "string"
            ? " and third party page set to " + options.thirdPartyPage
            : "") +
          (typeof options.topPage == "string"
            ? " and top page set to " + options.topPage
            : "")
      );

      is(
        !!options.callbackAfterRemoval,
        !!options.accessRemoval,
        "callbackAfterRemoval must be passed when accessRemoval is non-null"
      );

      let ownedPrivateWindow = false;
      let win = window;
      if (options.runInPrivateWindow) {
        if (gPrivateWindow) {
          win = gPrivateWindow;
        } else {
          win = OpenBrowserWindow({ private: true });
          await TestUtils.topicObserved("browser-delayed-startup-finished");
          ownedPrivateWindow = true;
        }
      }

      await AntiTracking._setupTest(
        win,
        options.cookieBehavior,
        options.runInPrivateWindow,
        options.extraPrefs
      );

      let topPage;
      if (typeof options.topPage == "string") {
        topPage = options.topPage;
      } else {
        topPage = TEST_TOP_PAGE;
      }

      let thirdPartyPage, thirdPartyDomainURI;
      if (typeof options.thirdPartyPage == "string") {
        thirdPartyPage = options.thirdPartyPage;
        let url = new URL(thirdPartyPage);
        thirdPartyDomainURI = Services.io.newURI(url.origin);
      } else {
        thirdPartyPage = TEST_3RD_PARTY_PAGE;
        thirdPartyDomainURI = Services.io.newURI(TEST_3RD_PARTY_DOMAIN);
      }

      // It's possible that the third-party domain has been exceptionlisted
      // through extraPrefs, so let's try annotating it here and adjust our
      // blocking expectations as necessary.
      if (
        options.expectedBlockingNotifications ==
        Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER
      ) {
        if (
          !(await AntiTracking._isThirdPartyPageClassifiedAsTracker(
            topPage,
            thirdPartyDomainURI
          ))
        ) {
          options.expectedBlockingNotifications = 0;
        }
      }

      let cookieBlocked = 0;
      let { expectedBlockingNotifications } = options;
      if (!Array.isArray(expectedBlockingNotifications)) {
        expectedBlockingNotifications = [expectedBlockingNotifications];
      }
      let listener = {
        onContentBlockingEvent(webProgress, request, event) {
          for (const notification of expectedBlockingNotifications) {
            if (event & notification) {
              ++cookieBlocked;
            }
          }
        },
      };
      function prepareTestEnvironmentOnPage() {
        win.gBrowser.addProgressListener(listener);

        Services.console.reset();
      }

      if (!options.allowList) {
        prepareTestEnvironmentOnPage();
      }

      let consoleWarningPromise;

      if (options.expectedBlockingNotifications) {
        consoleWarningPromise = new Promise(resolve => {
          let consoleListener = {
            observe(msg) {
              if (
                msg
                  .QueryInterface(Ci.nsIScriptError)
                  .category.startsWith("cookieBlocked")
              ) {
                Services.console.unregisterListener(consoleListener);
                resolve();
              }
            },
          };

          Services.console.registerListener(consoleListener);
        });
      } else {
        consoleWarningPromise = Promise.resolve();
      }

      let { expectedPartitioningNotifications } = options;
      if (!Array.isArray(expectedPartitioningNotifications)) {
        expectedPartitioningNotifications = [expectedPartitioningNotifications];
      }

      let consolePartitioningWarningPromise;
      if (options.expectedPartitioningNotifications) {
        consolePartitioningWarningPromise = new Promise(resolve => {
          let consoleListener = {
            observe(msg) {
              if (
                msg
                  .QueryInterface(Ci.nsIScriptError)
                  .category.startsWith("cookiePartitioned")
              ) {
                Services.console.unregisterListener(consoleListener);
                resolve();
              }
            },
          };

          Services.console.registerListener(consoleListener);
        });
      } else {
        consolePartitioningWarningPromise = Promise.resolve();
      }

      info("Creating a new tab");
      let tab = BrowserTestUtils.addTab(win.gBrowser, topPage);
      win.gBrowser.selectedTab = tab;

      let browser = win.gBrowser.getBrowserForTab(tab);
      await BrowserTestUtils.browserLoaded(browser);

      info("Check the cookieJarSettings of the browser object");
      ok(
        browser.cookieJarSettings,
        "The browser object has the cookieJarSettings."
      );
      is(
        browser.cookieJarSettings.cookieBehavior,
        options.cookieBehavior,
        "The cookieJarSettings has the correct cookieBehavior"
      );

      if (options.allowList) {
        info("Disabling content blocking for this page");
        win.gProtectionsHandler.disableForCurrentPage();

        prepareTestEnvironmentOnPage();

        // The previous function reloads the browser, so wait for it to load again!
        await BrowserTestUtils.browserLoaded(browser);
      }

      info("Creating a 3rd party content");
      let doAccessRemovalChecks =
        typeof options.accessRemoval == "string" &&
        options.cookieBehavior == BEHAVIOR_REJECT_TRACKER &&
        !options.allowList;
      await SpecialPowers.spawn(
        browser,
        [
          {
            page: thirdPartyPage,
            nextPage: TEST_4TH_PARTY_PAGE,
            callback: options.callback.toString(),
            callbackAfterRemoval: options.callbackAfterRemoval
              ? options.callbackAfterRemoval.toString()
              : null,
            accessRemoval: options.accessRemoval,
            iframeSandbox: options.iframeSandbox,
            iframeAllow: options.iframeAllow,
            allowList: options.allowList,
            doAccessRemovalChecks,
          },
        ],
        async function (obj) {
          let id = "id" + Math.random();
          await new content.Promise(resolve => {
            let ifr = content.document.createElement("iframe");
            ifr.id = id;
            ifr.onload = function () {
              info("Sending code to the 3rd party content");
              let callback = obj.allowList + "!!!" + obj.callback;
              ifr.contentWindow.postMessage(callback, "*");
            };
            if (typeof obj.iframeSandbox == "string") {
              ifr.setAttribute("sandbox", obj.iframeSandbox);
            }
            if (typeof obj.iframeAllow == "string") {
              ifr.setAttribute("allow", obj.iframeAllow);
            }

            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                resolve();
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });

            ifr.src = obj.page;
            content.document.body.appendChild(ifr);
          });

          if (obj.doAccessRemovalChecks) {
            info(`Running after removal checks (${obj.accessRemoval})`);
            switch (obj.accessRemoval) {
              case "navigate-subframe":
                await new content.Promise(resolve => {
                  let ifr = content.document.getElementById(id);
                  let oldWindow = ifr.contentWindow;
                  ifr.onload = function () {
                    info("Sending code to the old 3rd party content");
                    oldWindow.postMessage(obj.callbackAfterRemoval, "*");
                  };
                  if (typeof obj.iframeSandbox == "string") {
                    ifr.setAttribute("sandbox", obj.iframeSandbox);
                  }
                  if (typeof obj.iframeAllow == "string") {
                    ifr.setAttribute("allow", obj.iframeAllow);
                  }

                  content.addEventListener("message", function msg(event) {
                    if (event.data.type == "finish") {
                      content.removeEventListener("message", msg);
                      resolve();
                      return;
                    }

                    if (event.data.type == "ok") {
                      ok(event.data.what, event.data.msg);
                      return;
                    }

                    if (event.data.type == "info") {
                      info(event.data.msg);
                      return;
                    }

                    ok(false, "Unknown message");
                  });

                  ifr.src = obj.nextPage;
                });
                break;
              case "navigate-topframe":
                // pass-through
                break;
              default:
                ok(
                  false,
                  "Unexpected accessRemoval code passed: " + obj.accessRemoval
                );
                break;
            }
          }
        }
      );

      if (
        doAccessRemovalChecks &&
        options.accessRemoval == "navigate-topframe"
      ) {
        BrowserTestUtils.startLoadingURIString(browser, TEST_4TH_PARTY_PAGE);
        await BrowserTestUtils.browserLoaded(browser);

        let pageshow = BrowserTestUtils.waitForContentEvent(
          tab.linkedBrowser,
          "pageshow"
        );
        gBrowser.goBack();
        await pageshow;

        await SpecialPowers.spawn(
          browser,
          [
            {
              page: thirdPartyPage,
              callbackAfterRemoval: options.callbackAfterRemoval
                ? options.callbackAfterRemoval.toString()
                : null,
              iframeSandbox: options.iframeSandbox,
              iframeAllow: options.iframeAllow,
            },
          ],
          async function (obj) {
            let ifr = content.document.createElement("iframe");
            ifr.onload = function () {
              info(
                "Sending code to the 3rd party content to verify accessRemoval"
              );
              ifr.contentWindow.postMessage(obj.callbackAfterRemoval, "*");
            };
            if (typeof obj.iframeSandbox == "string") {
              ifr.setAttribute("sandbox", obj.iframeSandbox);
            }
            if (typeof obj.iframeAllow == "string") {
              ifr.setAttribute("allow", obj.iframeAllow);
            }

            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });

            ifr.src = obj.page;
            content.document.body.appendChild(ifr);
          }
        );
      }

      // Wait until the message appears on the console.
      await consoleWarningPromise;
      await consolePartitioningWarningPromise;

      let allMessages = Services.console.getMessageArray().filter(msg => {
        try {
          // Select all messages that the anti-tracking backend could generate.
          let category = msg.QueryInterface(Ci.nsIScriptError).category;

          let isBlocking = category.startsWith("cookieBlocked");

          // Only look for partitioning notifications if we expect them.
          let isPartitioning =
            options.expectedPartitioningNotifications &&
            category.startsWith("cookiePartitioned");

          return isBlocking || isPartitioning;
        } catch (e) {
          return false;
        }
      });
      // When changing this list, please make sure to update the corresponding
      // code in ReportBlockingToConsole().
      let expectedCategories = [];
      let rawExpectedCategories =
        options.expectedBlockingNotifications ??
        options.expectedPartitioningNotifications;
      if (!Array.isArray(rawExpectedCategories)) {
        // if given a single value to match, expect each message to match it
        rawExpectedCategories = Array(allMessages.length).fill(
          rawExpectedCategories
        );
      }
      for (let category of rawExpectedCategories) {
        switch (category) {
          case Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_BY_PERMISSION:
            expectedCategories.push("cookieBlockedPermission");
            break;
          case Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER:
            expectedCategories.push("cookieBlockedTracker");
            break;
          case Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_ALL:
            expectedCategories.push("cookieBlockedAll");
            break;
          case Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_FOREIGN:
            expectedCategories.push("cookieBlockedForeign");
            break;
          case Ci.nsIWebProgressListener.STATE_COOKIES_PARTITIONED_TRACKER:
            expectedCategories.push("cookiePartitionedForeign");
            break;
        }
      }

      if (!expectedCategories.length) {
        is(allMessages.length, 0, "No console messages should be generated");
      } else {
        ok(!!allMessages.length, "Some console message should be generated");
        if (options.errorMessageDomains) {
          is(
            allMessages.length,
            options.errorMessageDomains.length,
            "Enough items provided in errorMessageDomains"
          );
        }
      }
      let index = 0;
      for (let msg of allMessages) {
        is(
          msg.category,
          expectedCategories[index],
          `Message ${index} should be of expected category`
        );

        if (options.errorMessageDomains) {
          ok(
            msg.errorMessage.includes(options.errorMessageDomains[index]),
            `Error message domain ${options.errorMessageDomains[index]} (${index}) found in "${msg.errorMessage}"`
          );
          index++;
        }
      }

      if (options.allowList) {
        info("Enabling content blocking for this page");
        win.gProtectionsHandler.enableForCurrentPage();

        // The previous function reloads the browser, so wait for it to load again!
        await BrowserTestUtils.browserLoaded(browser);
      }

      win.gBrowser.removeProgressListener(listener);

      if (!!cookieBlocked != !!options.expectedBlockingNotifications) {
        ok(false, JSON.stringify(cookieBlocked));
        ok(false, JSON.stringify(options.expectedBlockingNotifications));
      }
      is(
        !!cookieBlocked,
        !!options.expectedBlockingNotifications,
        "Checking cookie blocking notifications"
      );

      info("Removing the tab");
      BrowserTestUtils.removeTab(tab);

      if (ownedPrivateWindow) {
        win.close();
      }
    });
  },

  _createCleanupTask(cleanupFunction) {
    add_task(async function () {
      info("Cleaning up.");
      if (cleanupFunction) {
        await cleanupFunction();
      }

      if (gPrivateWindow) {
        await new Promise(resolve => {
          Services.clearData.deleteDataFromOriginAttributesPattern(
            { privateBrowsingId: 1 },
            () => resolve()
          );
        });
      }
    });
  },

  _openPrivateWindowTask() {
    add_task(async function () {
      gPrivateWindow = OpenBrowserWindow({ private: true });
      await TestUtils.topicObserved("browser-delayed-startup-finished");
    });
  },

  _closePrivateWindowTask() {
    add_task(async function () {
      await BrowserTestUtils.closeWindow(gPrivateWindow);
      gPrivateWindow = null;
    });
  },

  _createWindowOpenTask(
    name,
    cookieBehavior,
    blockingCallback,
    nonBlockingCallback,
    runInPrivateWindow,
    iframeSandbox,
    testInSubIFrame,
    extraPrefs,
    iframeAllow
  ) {
    add_task(async function () {
      info(
        `Starting window-open${
          testInSubIFrame ? " sub iframe" : ""
        } test ${name}`
      );

      let ownedPrivateWindow = false;
      let win = window;
      if (runInPrivateWindow) {
        if (gPrivateWindow) {
          win = gPrivateWindow;
        } else {
          win = OpenBrowserWindow({ private: true });
          await TestUtils.topicObserved("browser-delayed-startup-finished");
          ownedPrivateWindow = true;
        }
      }

      // Enable SA heuristics for trackers because the test depends on it.
      extraPrefs = [
        ...(extraPrefs || []),
        [
          "privacy.restrict3rdpartystorage.heuristic.exclude_third_party_trackers",
          false,
        ],
      ];

      await AntiTracking._setupTest(
        win,
        cookieBehavior,
        runInPrivateWindow,
        extraPrefs
      );

      info("Creating a new tab");
      let tab = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE);
      win.gBrowser.selectedTab = tab;

      let browser = win.gBrowser.getBrowserForTab(tab);
      await BrowserTestUtils.browserLoaded(browser);

      info("Create a first-level iframe to test sub iframes.");
      if (testInSubIFrame) {
        let iframeBrowsingContext = await SpecialPowers.spawn(
          browser,
          [{ page: TEST_IFRAME_PAGE }],
          async function (obj) {
            // Add an iframe.
            let ifr = content.document.createElement("iframe");
            const loading = ContentTaskUtils.waitForEvent(ifr, "load");
            ifr.src = obj.page;
            content.document.body.appendChild(ifr);
            await loading;

            return ifr.browsingContext;
          }
        );

        browser = iframeBrowsingContext;
      }

      let pageURL = TEST_3RD_PARTY_PAGE_WO;
      if (gFeatures == "noopener") {
        pageURL += "?noopener";
      }

      info("Creating a 3rd party content");
      await SpecialPowers.spawn(
        browser,
        [
          {
            page: pageURL,
            blockingCallback: blockingCallback.toString(),
            nonBlockingCallback: nonBlockingCallback.toString(),
            iframeSandbox,
            iframeAllow,
          },
        ],
        async function (obj) {
          await new content.Promise(resolve => {
            let ifr = content.document.createElement("iframe");
            ifr.onload = function () {
              info("Sending code to the 3rd party content");
              ifr.contentWindow.postMessage(obj, "*");
            };
            if (typeof obj.iframeSandbox == "string") {
              ifr.setAttribute("sandbox", obj.iframeSandbox);
            }
            if (typeof obj.iframeAllow == "string") {
              ifr.setAttribute("allow", obj.iframeAllow);
            }

            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                resolve();
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });

            ifr.src = obj.page;
            content.document.body.appendChild(ifr);
          });
        }
      );

      info("Removing the tab");
      BrowserTestUtils.removeTab(tab);

      if (ownedPrivateWindow) {
        win.close();
      }
    });
  },

  _createUserInteractionTask(
    name,
    cookieBehavior,
    blockingCallback,
    nonBlockingCallback,
    runInPrivateWindow,
    iframeSandbox,
    testInSubIFrame,
    extraPrefs,
    iframeAllow
  ) {
    add_task(async function () {
      info(
        `Starting user-interaction${
          testInSubIFrame ? " sub iframe" : ""
        } test ${name}`
      );

      let ownedPrivateWindow = false;
      let win = window;
      if (runInPrivateWindow) {
        if (gPrivateWindow) {
          win = gPrivateWindow;
        } else {
          win = OpenBrowserWindow({ private: true });
          await TestUtils.topicObserved("browser-delayed-startup-finished");
          ownedPrivateWindow = true;
        }
      }

      // Enable SA heuristics for trackers because the test depends on it.
      extraPrefs = [
        ...(extraPrefs || []),
        [
          "privacy.restrict3rdpartystorage.heuristic.exclude_third_party_trackers",
          false,
        ],
      ];

      await AntiTracking._setupTest(
        win,
        cookieBehavior,
        runInPrivateWindow,
        extraPrefs
      );

      info("Creating a new tab");
      let tab = BrowserTestUtils.addTab(win.gBrowser, TEST_TOP_PAGE);
      win.gBrowser.selectedTab = tab;

      let browser = win.gBrowser.getBrowserForTab(tab);
      await BrowserTestUtils.browserLoaded(browser);

      if (testInSubIFrame) {
        info("Create a first-level iframe to test sub iframes.");
        let iframeBrowsingContext = await SpecialPowers.spawn(
          browser,
          [{ page: TEST_IFRAME_PAGE }],
          async function (obj) {
            // Add an iframe.
            let ifr = content.document.createElement("iframe");
            const loading = ContentTaskUtils.waitForEvent(ifr, "load");
            ifr.src = obj.page;
            content.document.body.appendChild(ifr);
            await loading;

            return ifr.browsingContext;
          }
        );

        browser = iframeBrowsingContext;
      }

      // The following test will open an popup which interacts with the tracker
      // page. So there will be an user-interaction permission added. We wait
      // it explicitly.
      let promiseUIPerm = AntiTracking._waitUserInteractionPerm();

      info("Creating a 3rd party content");
      await SpecialPowers.spawn(
        browser,
        [
          {
            page: TEST_3RD_PARTY_PAGE_UI,
            popup: TEST_POPUP_PAGE,
            blockingCallback: blockingCallback.toString(),
            iframeSandbox,
            iframeAllow,
          },
        ],
        async function (obj) {
          let ifr = content.document.createElement("iframe");
          const loading = ContentTaskUtils.waitForEvent(ifr, "load");
          if (typeof obj.iframeSandbox == "string") {
            ifr.setAttribute("sandbox", obj.iframeSandbox);
          }
          if (typeof obj.iframeAllow == "string") {
            ifr.setAttribute("allow", obj.iframeAllow);
          }
          ifr.src = obj.page;
          content.document.body.appendChild(ifr);
          await loading;

          info(
            "The 3rd party content should not have access to first party storage."
          );
          await new content.Promise(resolve => {
            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                resolve();
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });
            ifr.contentWindow.postMessage(
              { callback: obj.blockingCallback },
              "*"
            );
          });

          let windowClosed = new content.Promise(resolve => {
            Services.ww.registerNotification(
              function notification(aSubject, aTopic) {
                if (aTopic == "domwindowclosed") {
                  Services.ww.unregisterNotification(notification);
                  resolve();
                }
              }
            );
          });

          info("Opening a window from the iframe.");
          SpecialPowers.spawn(
            ifr,
            [{ popup: obj.popup }],
            async function (obj) {
              content.open(obj.popup);
            }
          );

          info("Let's wait for the window to be closed");
          await windowClosed;

          info(
            "First time, the 3rd party content should not have access to first party storage " +
              "because the tracker did not have user interaction"
          );
          await new content.Promise(resolve => {
            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                resolve();
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });
            ifr.contentWindow.postMessage(
              { callback: obj.blockingCallback },
              "*"
            );
          });
        }
      );

      // We wait until the user-interaction permission is added.
      await promiseUIPerm;

      // We also need to wait the user-interaction permission here.
      promiseUIPerm = AntiTracking._waitUserInteractionPerm();
      await AntiTracking.interactWithTracker();
      await promiseUIPerm;

      // Following test will also open an popup to interact with the page. We
      // need to explicitly wait it. Without waiting it, it could be added after
      // we clear up the test and interfere the next test.
      promiseUIPerm = AntiTracking._waitUserInteractionPerm();

      // We have to wait until the storage access permission is added. This has
      // the same reason as above user-interaction permission. Note that there
      // will be two storage access permission added due to the way how we
      // trigger the heuristic. The first permission is added due to 'Opener'
      // heuristic and the second one is due to 'Opener after user interaction'.
      // The page we use to trigger the heuristic will trigger both heuristic,
      // so we have to wait 2 permissions.
      let promiseStorageAccessPerm = AntiTracking._waitStorageAccessPerm(2);

      await SpecialPowers.spawn(
        browser,
        [
          {
            page: TEST_3RD_PARTY_PAGE_UI,
            popup: TEST_POPUP_PAGE,
            nonBlockingCallback: nonBlockingCallback.toString(),
            iframeSandbox,
            iframeAllow,
          },
        ],
        async function (obj) {
          let ifr = content.document.createElement("iframe");
          const loading = ContentTaskUtils.waitForEvent(ifr, "load");
          if (typeof obj.iframeSandbox == "string") {
            ifr.setAttribute("sandbox", obj.iframeSandbox);
          }
          if (typeof obj.iframeAllow == "string") {
            ifr.setAttribute("allow", obj.iframeAllow);
          }
          ifr.src = obj.page;
          content.document.body.appendChild(ifr);
          await loading;

          let windowClosed = new content.Promise(resolve => {
            Services.ww.registerNotification(
              function notification(aSubject, aTopic) {
                if (aTopic == "domwindowclosed") {
                  Services.ww.unregisterNotification(notification);
                  resolve();
                }
              }
            );
          });

          info("Opening a window from the iframe.");
          SpecialPowers.spawn(
            ifr,
            [{ popup: obj.popup }],
            async function (obj) {
              content.open(obj.popup);
            }
          );

          info("Let's wait for the window to be closed");
          await windowClosed;

          info(
            "The 3rd party content should now have access to first party storage."
          );
          await new content.Promise(resolve => {
            content.addEventListener("message", function msg(event) {
              if (event.data.type == "finish") {
                content.removeEventListener("message", msg);
                resolve();
                return;
              }

              if (event.data.type == "ok") {
                ok(event.data.what, event.data.msg);
                return;
              }

              if (event.data.type == "info") {
                info(event.data.msg);
                return;
              }

              ok(false, "Unknown message");
            });
            ifr.contentWindow.postMessage(
              { callback: obj.nonBlockingCallback },
              "*"
            );
          });
        }
      );

      // Explicitly wait the user-interaction and storage access permission
      // before we do the cleanup.
      await promiseUIPerm;
      await promiseStorageAccessPerm;

      info("Removing the tab");
      BrowserTestUtils.removeTab(tab);

      if (ownedPrivateWindow) {
        win.close();
      }
    });
  },

  async _isThirdPartyPageClassifiedAsTracker(topPage, thirdPartyDomainURI) {
    let channel;
    await new Promise((resolve, reject) => {
      channel = NetUtil.newChannel({
        uri: thirdPartyDomainURI,
        loadingPrincipal: Services.scriptSecurityManager.createContentPrincipal(
          thirdPartyDomainURI,
          {}
        ),
        securityFlags:
          Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
        contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
      });

      channel
        .QueryInterface(Ci.nsIHttpChannelInternal)
        .setTopWindowURIIfUnknown(Services.io.newURI(topPage));

      function Listener() {}
      Listener.prototype = {
        onStartRequest() {},
        onDataAvailable(request, stream, off, cnt) {
          // Consume the data to prevent hitting the assertion.
          NetUtil.readInputStreamToString(stream, cnt);
        },
        onStopRequest(request) {
          let status = request.QueryInterface(Ci.nsIHttpChannel).responseStatus;
          if (status == 200) {
            resolve();
          } else {
            reject();
          }
        },
      };
      let listener = new Listener();
      channel.asyncOpen(listener);
    });

    return !!(
      channel.QueryInterface(Ci.nsIClassifiedChannel).classificationFlags &
      Ci.nsIClassifiedChannel.CLASSIFIED_ANY_BASIC_TRACKING
    );
  },
};
