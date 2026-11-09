/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let btpGracePeriodSec = Services.prefs.getIntPref(
  "privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec"
);

/**
 * Registers a console listener and waits for the bounce tracker classified or
 * purged warning message to be logged.
 *
 * @returns {Promise} - Promise which resolves once the message has been logged.
 */
async function waitForBTPConsoleMessage(type, siteHost) {
  if (!["classified", "purged"].includes(type)) {
    throw new Error("Invalid message type argument passed.");
  }

  let lastMessage;
  let gracePeriodFormatted;
  if (type == "classified") {
    // Checking if the grace period is
    // privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec requires
    // extra steps because l10n applies further transformations to the number such
    // as adding a ",".
    gracePeriodFormatted = new Intl.NumberFormat("en-US", {
      useGrouping: true,
    }).format(
      Services.prefs.getIntPref(
        "privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec"
      )
    );
  }

  let msgIncludesString;
  if (type == "classified") {
    msgIncludesString = `“${siteHost}” has been classified as a bounce tracker. If it does not receive user activation within the next ${gracePeriodFormatted} seconds it will have its state purged.`;
  } else {
    msgIncludesString = `The state of “${siteHost}” was recently purged because it was detected as a bounce tracker.`;
  }

  info("Waiting for console message with content: " + msgIncludesString);

  let isBTPClassifiedMsg = msg => msg.includes(msgIncludesString);
  await new Promise(resolve => {
    SpecialPowers.registerConsoleListener(consoleMsg => {
      if (!consoleMsg?.message || consoleMsg.message == "SENTINEL") {
        return;
      }
      lastMessage = consoleMsg;
      if (isBTPClassifiedMsg(consoleMsg.message)) {
        resolve();
      }
    });
  });
  SpecialPowers.postConsoleSentinel();

  ok(lastMessage.isScriptError, "Message should be script error.");
  ok(lastMessage.isWarning, "Message should be a warning.");

  ok(
    isBTPClassifiedMsg(lastMessage.message),
    `Observed bounce tracker ${type} console message.`
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "privacy.bounceTrackingProtection.mode",
        Ci.nsIBounceTrackingProtection.MODE_ENABLED,
      ],
      ["privacy.bounceTrackingProtection.bounceTrackingGracePeriodSec", 0],
    ],
  });
});

add_task(async function test_bounce_tracker_web_console_messages() {
  info("Testing the classified warning message.");
  let consoleMsgPromise = waitForBTPConsoleMessage("classified", SITE_TRACKER);

  info("Run server bounce with cookie.");
  await runTestBounce({
    bounceType: "server",
    setState: "cookie-server",
    // Don't do automatic cleanup after bounce otherwise we don't get the purge
    // warning message later.
    skipBounceTrackingProtectionCleanup: true,
    skipSiteDataCleanup: true,
  });

  await consoleMsgPromise;

  info("Testing the purged warning message.");
  consoleMsgPromise = waitForBTPConsoleMessage("purged", SITE_TRACKER);

  // Visit the bounce tracker site normally.
  await BrowserTestUtils.withNewTab(ORIGIN_TRACKER, async () => {
    await consoleMsgPromise;
  });

  // Need to cleanup ourselves as we've instructed the test wrapper to skip it.
  let bounceTrackingProtection = Cc[
    "@mozilla.org/bounce-tracking-protection;1"
  ].getService(Ci.nsIBounceTrackingProtection);
  bounceTrackingProtection.clearAll();
  await SiteDataTestUtils.clear();
});
