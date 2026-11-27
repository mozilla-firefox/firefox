"use strict";

/**
 * Returns the id of the crash minidump.
 *
 * @param subject (nsISupports)
 *        The subject passed through the ipc:content-shutdown
 *        observer notification when a content process crash has
 *        occurred.
 * @returns {string} The crash dump id.
 */
function getCrashDumpId(subject) {
  Assert.ok(
    subject instanceof Ci.nsIPropertyBag2,
    "Subject needs to be a nsIPropertyBag2 to clean up properly"
  );

  return subject.get("dumpID");
}

/**
 * Cleans up the .dmp and .extra file from a crash.
 *
 * @param id {String} The crash dump id.
 */
function cleanUpMinidump(id) {
  let dir = Services.dirsvc.get("ProfD", Ci.nsIFile);
  dir.append("minidumps");

  let file = dir.clone();
  file.append(id + ".dmp");
  file.remove(true);

  file = dir.clone();
  file.append(id + ".extra");
  file.remove(true);
}

/**
 * This test ensures that if a remote frameloader crashes after
 * the frameloader owner swaps it out for a new frameloader,
 * that a oop-browser-crashed event is not sent to the new
 * frameloader's browser element.
 */
add_task(async function test_crash_in_previous_frameloader() {
  // On debug builds, crashing tabs results in much thinking, which
  // slows down the test and results in intermittent test timeouts,
  // so we'll pump up the expected timeout for this test.
  requestLongerTimeout(2);

  if (!gMultiProcessBrowser) {
    Assert.ok(false, "This test should only be run in multi-process mode.");
    return;
  }

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "http://example.com",
    },
    async function (browser) {
      // First, sanity check...
      Assert.ok(
        browser.isRemoteBrowser,
        "This browser needs to be remote if this test is going to " +
          "work properly."
      );

      // We will wait for the oop-browser-crashed event to have
      // a chance to appear. That event is fired when RemoteTabs
      // are destroyed, and that occurs _before_ ContentParents
      // are destroyed, so we'll wait on the ipc:content-shutdown
      // observer notification, which is fired when a ContentParent
      // goes away. After we see this notification, oop-browser-crashed
      // events should have fired.
      let contentProcessGone = TestUtils.topicObserved("ipc:content-shutdown");
      let sawTabCrashed = false;
      let onTabCrashed = () => {
        sawTabCrashed = true;
      };

      browser.addEventListener("oop-browser-crashed", onTabCrashed);

      // The name of the game is to cause a crash in a remote browser,
      // and then immediately swap out the browser for a non-remote one.
      await SpecialPowers.spawn(browser, [], function () {
        const { ctypes } = ChromeUtils.importESModule(
          "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
        );

        let dies = function () {
          ChromeUtils.privateNoteIntentionalCrash();
          let zero = new ctypes.intptr_t(8);
          let badptr = ctypes.cast(zero, ctypes.PointerType(ctypes.int32_t));
          badptr.contents;
        };

        // When the parent flips the remoteness of the browser, the
        // page should receive the pagehide event, which we'll then
        // use to crash the frameloader.
        docShell.chromeEventHandler.addEventListener("pagehide", function () {
          dump("\nEt tu, Brute?\n");
          dies();
        });
      });

      gBrowser.updateBrowserRemoteness(browser, {
        remoteType: E10SUtils.NOT_REMOTE,
      });
      info("Waiting for content process to go away.");
      let [subject /* , data */] = await contentProcessGone;

      // If we don't clean up the minidump, the harness will
      // complain.
      let dumpID = getCrashDumpId(subject);

      Assert.ok(dumpID, "There should be a dumpID");
      if (dumpID) {
        await Services.crashmanager.ensureCrashIsPresent(dumpID);
        cleanUpMinidump(dumpID);
      }

      info("Content process is gone!");
      Assert.ok(
        !sawTabCrashed,
        "Should not have seen the oop-browser-crashed event."
      );
      browser.removeEventListener("oop-browser-crashed", onTabCrashed);
    }
  );
});
