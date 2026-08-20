/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from aboutDialog-appUpdater.js */
/* import-globals-from utilityOverlay.js */

// Services = object with smart getters for common XPCOM services
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  Referrals: "resource:///modules/referrals/Referrals.sys.mjs",
});

if (AppConstants.MOZ_UPDATER) {
  Services.scriptloader.loadSubScript(
    "chrome://browser/content/aboutDialog-appUpdater.js",
    this
  );
}

function init() {
  let defaults = Services.prefs.getDefaultBranch(null);
  let distroId = defaults.getCharPref("distribution.id", "");
  let distroAbout = defaults.getStringPref("distribution.about", "");
  // Only show distribution info when there is about text. An id-only
  // distribution is used for attribution and is shown in about:support.
  if (distroId && distroAbout) {
    let distroField = document.getElementById("distribution");
    distroField.value = distroAbout;
    distroField.style.display = "block";

    let distroVersion = defaults.getCharPref("distribution.version", "");
    if (distroVersion) {
      distroId += " - " + distroVersion;
    }

    let distroIdField = document.getElementById("distributionId");
    distroIdField.value = distroId;
    distroIdField.style.display = "block";
  }

  // Include the build ID and display warning if this is an "a#" (nightly or aurora) build
  let versionIdMap = new Map([
    ["base", "aboutDialog-version"],
    ["base-nightly", "aboutDialog-version-nightly"],
    ["base-arch", "aboutdialog-version-arch"],
    ["base-arch-nightly", "aboutdialog-version-arch-nightly"],
  ]);
  let versionIdKey = "base";
  let versionAttributes = {
    version: AppConstants.MOZ_APP_VERSION_DISPLAY,
  };

  let arch = Services.sysinfo.get("arch");
  if (["x86", "x86-64"].includes(arch)) {
    versionAttributes.bits = Services.appinfo.is64Bit ? 64 : 32;
  } else {
    versionIdKey += "-arch";
    versionAttributes.arch = arch;
  }

  let version = Services.appinfo.version;
  if (/a\d+$/.test(version)) {
    versionIdKey += "-nightly";
    let buildID = Services.appinfo.appBuildID;
    let year = buildID.slice(0, 4);
    let month = buildID.slice(4, 6);
    let day = buildID.slice(6, 8);
    versionAttributes.isodate = `${year}-${month}-${day}`;

    document.getElementById("experimental").hidden = false;
    document.getElementById("communityDesc").hidden = true;
  }

  // Use Fluent arguments for append version and the architecture of the build
  let versionField = document.getElementById("version");

  document.l10n.setAttributes(
    versionField,
    versionIdMap.get(versionIdKey),
    versionAttributes
  );

  // Show a release notes link if we have a URL.
  let relNotesLink = document.getElementById("releasenotes");
  let relNotesPrefType = Services.prefs.getPrefType(
    "app.releaseNotesURL.aboutDialog"
  );
  if (relNotesPrefType != Services.prefs.PREF_INVALID) {
    let relNotesURL = Services.urlFormatter.formatURLPref(
      "app.releaseNotesURL.aboutDialog"
    );
    if (relNotesURL != "about:blank") {
      relNotesLink.href = relNotesURL;
      relNotesLink.hidden = false;
    }
  }

  if (AppConstants.MOZ_UPDATER) {
    gAppUpdater = new appUpdater({ buttonAutoFocus: true });

    let channelLabel = document.getElementById("currentChannelText");
    let channelAttrs = document.l10n.getAttributes(channelLabel);
    let channel = UpdateUtils.UpdateChannel;
    document.l10n.setAttributes(channelLabel, channelAttrs.id, { channel });
    if (
      /^release($|\-)/.test(channel) ||
      Services.sysinfo.getProperty("isPackagedApp")
    ) {
      channelLabel.hidden = true;
    }
  }

  // contributeDescReferrals contains the Share Firefox link, so we
  // toggle which description based on the referrals config flag
  let referralsEnabled = Services.prefs.getBoolPref(
    "browser.referrals.enabled",
    false
  );
  document.getElementById("contributeDesc").hidden = referralsEnabled;
  let contributeDescReferrals = document.getElementById(
    "contributeDescReferrals"
  );
  contributeDescReferrals.hidden = !referralsEnabled;

  if (referralsEnabled) {
    contributeDescReferrals.addEventListener(
      "click",
      event => {
        if (
          event.target.closest('[data-l10n-name="helpus-shareFirefoxLink"]')
        ) {
          event.preventDefault();
          lazy.Referrals.openReferralsTab(window, "about_dialog");
        }
      },
      true
    );
  }

  if (AppConstants.IS_ESR) {
    document.getElementById("release").hidden = false;
  }

  document
    .getElementById("aboutDialogEscapeKey")
    .addEventListener("command", () => {
      window.close();
    });
  if (AppConstants.MOZ_UPDATER) {
    document
      .getElementById("aboutDialogHelpLink")
      .addEventListener("click", () => {
        openHelpLink("firefox-help");
      });
    document
      .getElementById("submit-feedback")
      .addEventListener("click", openFeedbackPage);
    document
      .getElementById("checkForUpdatesButton")
      .addEventListener("command", () => {
        gAppUpdater.checkForUpdates();
      });
    document
      .getElementById("downloadAndInstallButton")
      .addEventListener("command", () => {
        gAppUpdater.startDownload();
      });
    document.getElementById("updateButton").addEventListener("command", () => {
      gAppUpdater.buttonRestartAfterDownload();
    });
    window.addEventListener("unload", e => {
      onUnload(e);
    });
  }
}

init();
