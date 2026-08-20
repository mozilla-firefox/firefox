"use strict";

const PAGE =
  "data:text/html,<html><body>A%20regular,%20everyday,%20normal%20page.";
const AUTOSUBMIT_PREF = "browser.crashReports.unsubmittedCheck.autoSubmit2";

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

// On debug builds, crashing tabs results in much thinking, which
// slows down the test and results in intermittent test timeouts,
// so we'll pump up the expected timeout for this test.
requestLongerTimeout(2);

/**
 * Tests that if the user is not configured to autosubmit
 * backlogged crash reports, that we offer to do that, and
 * that the user can accept that offer.
 */
add_task(async function test_show_form() {
  await SpecialPowers.pushPrefEnv({
    set: [[AUTOSUBMIT_PREF, false]],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: PAGE,
    },
    async function (browser) {
      // Make sure we've flushed the browser messages so that
      // we can restore it.
      await TabStateFlusher.flush(browser);

      // Now crash the browser.
      await BrowserTestUtils.crashFrame(browser);

      let doc = browser.contentDocument;

      // Ensure the request is visible. We can safely reach into
      // the content since about:tabcrashed is an in-process URL.
      let requestAutoSubmit = doc.getElementById("requestAutoSubmit");
      Assert.ok(
        !requestAutoSubmit.hidden,
        "Request for autosubmission is visible."
      );

      // Since the pref is set to false, the checkbox should be
      // unchecked.
      let autoSubmit = doc.getElementById("autoSubmit");
      Assert.ok(
        !autoSubmit.checked,
        "Checkbox for autosubmission is not checked."
      );

      // Check the checkbox, and then restore the tab.
      autoSubmit.checked = true;
      let restoreButton = doc.getElementById("restoreTab");
      restoreButton.click();

      await BrowserTestUtils.browserLoaded(browser, false, PAGE);

      // The autosubmission pref should now be set.
      Assert.ok(
        Services.prefs.getBoolPref(AUTOSUBMIT_PREF),
        "Autosubmission pref should have been set."
      );
    }
  );
});

/**
 * Tests that if the user is autosubmitting backlogged crash reports
 * that we don't make the offer again.
 */
add_task(async function test_show_form() {
  await SpecialPowers.pushPrefEnv({
    set: [[AUTOSUBMIT_PREF, true]],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: PAGE,
    },
    async function (browser) {
      await TabStateFlusher.flush(browser);
      // Now crash the browser.
      await BrowserTestUtils.crashFrame(browser);

      let doc = browser.contentDocument;

      // Ensure the request is NOT visible. We can safely reach into
      // the content since about:tabcrashed is an in-process URL.
      let requestAutoSubmit = doc.getElementById("requestAutoSubmit");
      Assert.ok(
        requestAutoSubmit.hidden,
        "Request for autosubmission is not visible."
      );

      // Restore the tab.
      let restoreButton = doc.getElementById("restoreTab");
      restoreButton.click();

      await BrowserTestUtils.browserLoaded(browser, false, PAGE);

      // The autosubmission pref should still be set to true.
      Assert.ok(
        Services.prefs.getBoolPref(AUTOSUBMIT_PREF),
        "Autosubmission pref should have been set."
      );
    }
  );
});

/**
 * Tests that we properly set the autoSubmit preference if the user is
 * presented with a tabcrashed page without a crash report.
 */
add_task(async function test_no_offer() {
  // We should default to sending the report.
  Assert.ok(TabCrashHandler.prefs.getBoolPref("sendReport"));

  await SpecialPowers.pushPrefEnv({
    set: [[AUTOSUBMIT_PREF, false]],
  });

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: PAGE,
    },
    async function (browser) {
      await TabStateFlusher.flush(browser);

      // Make it so that it seems like no dump is available for the next crash.
      prepareNoDump();

      // Now crash the browser.
      await BrowserTestUtils.crashFrame(browser);

      let doc = browser.contentDocument;

      // Ensure the request to autosubmit is invisible, since there's no report.
      let requestRect = doc
        .getElementById("requestAutoSubmit")
        .getBoundingClientRect();
      Assert.equal(
        0,
        requestRect.height,
        "Request for autosubmission has no height"
      );
      Assert.equal(
        0,
        requestRect.width,
        "Request for autosubmission has no width"
      );

      // Since the pref is set to false, the checkbox should be
      // unchecked.
      let autoSubmit = doc.getElementById("autoSubmit");
      Assert.ok(
        !autoSubmit.checked,
        "Checkbox for autosubmission is not checked."
      );

      let restoreButton = doc.getElementById("restoreTab");
      restoreButton.click();

      await BrowserTestUtils.browserLoaded(browser, false, PAGE);

      // The autosubmission pref should now be set.
      Assert.ok(
        !Services.prefs.getBoolPref(AUTOSUBMIT_PREF),
        "Autosubmission pref should not have changed."
      );
    }
  );

  // We should not have changed the default value for sending the report.
  Assert.ok(TabCrashHandler.prefs.getBoolPref("sendReport"));
});
