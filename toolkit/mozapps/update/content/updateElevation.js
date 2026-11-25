/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* This is temporary until bug 1521632 is fixed */

"use strict";

/* import-globals-from /toolkit/content/contentAreaUtils.js */

const gUpdateElevationDialog = {
  openUpdateURL(event) {
    if (event.button == 0) {
      openURL(event.target.getAttribute("url"));
    }
  },
  getAUSString(key, strings) {
    if (strings) {
      return this.strings.getFormattedString(key, strings);
    }
    return this.strings.getString(key);
  },
  _setButton(button, string) {
    var label = this.getAUSString(string);
    if (label.includes("%S")) {
      label = label.replace(/%S/, this.brandName);
    }
    button.label = label;
    button.setAttribute("accesskey", this.getAUSString(string + ".accesskey"));
  },
  async onLoad() {
    this.strings = document.getElementById("updateStrings");
    this.brandName = document
      .getElementById("brandStrings")
      .getString("brandShortName");

    let um = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    let update = await um.getReadyUpdate();
    let updateFinishedName = document.getElementById("updateFinishedName");
    updateFinishedName.value = update.name;

    let link = document.getElementById("detailsLinkLabel");
    if (update.detailsURL) {
      link.setAttribute("url", update.detailsURL);
      // The details link is stealing focus so it is disabled by default and
      // should only be enabled after onPageShow has been called.
      link.disabled = false;
    } else {
      link.hidden = true;
    }
    link.addEventListener("click", event =>
      gUpdateElevationDialog.openUpdateURL(event)
    );

    let manualLinkLabel = document.getElementById("manualLinkLabel");
    let manualURL = Services.urlFormatter.formatURLPref(
      "app.update.url.manual"
    );
    manualLinkLabel.value = manualURL;
    manualLinkLabel.setAttribute("url", manualURL);
    manualLinkLabel.addEventListener("click", event =>
      gUpdateElevationDialog.openUpdateURL(event)
    );

    let buttonElevateExtra2 = document.getElementById("elevateExtra2");
    buttonElevateExtra2.addEventListener("command", () =>
      gUpdateElevationDialog.onRestartLater()
    );
    this._setButton(buttonElevateExtra2, "restartLaterButton");

    let buttonElevateExtra1 = document.getElementById("elevateExtra1");
    buttonElevateExtra1.addEventListener("command", () =>
      gUpdateElevationDialog.onNoThanks()
    );
    this._setButton(buttonElevateExtra1, "noThanksButton");

    let buttonElevateAccept = document.getElementById("elevateAccept");
    // note that onRestartNow runs asynchronously
    buttonElevateAccept.addEventListener("command", () =>
      gUpdateElevationDialog.onRestartNow()
    );
    this._setButton(buttonElevateAccept, "restartNowButton");
    buttonElevateAccept.focus();
  },
  onRestartLater() {
    window.close();
  },
  async onNoThanks() {
    Services.obs.notifyObservers(null, "update-canceled");
    let um = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    let update = await um.getReadyUpdate();
    um.cleanupReadyUpdate();
    // Since the user has clicked "No Thanks", we should not prompt them to update to
    // this version again unless they manually select "Check for Updates..."
    // which will clear app.update.elevate.never preference.
    let aus = Cc["@mozilla.org/updates/update-service;1"].getService(
      Ci.nsIApplicationUpdateService
    );
    if (aus.elevationRequired && update) {
      Services.prefs.setCharPref("app.update.elevate.never", update.appVersion);
    }
    window.close();
  },
  async onRestartNow() {
    // disable the "finish" (Restart) and "extra1" (Later) buttons
    // because the Software Update wizard is still up at the point,
    // and will remain up until we return and we close the
    // window with a |window.close()| in wizard.xml
    // (it was the firing the "wizardfinish" event that got us here.)
    // This prevents the user from switching back
    // to the Software Update dialog and clicking "Restart" or "Later"
    // when dealing with the "confirm close" prompts.
    // See bug #350299 for more details.
    document.getElementById("elevateExtra2").disabled = true;
    document.getElementById("elevateExtra1").disabled = true;
    document.getElementById("elevateAccept").disabled = true;

    // This dialog was shown because elevation was required so there is no need
    // to check if elevation is required again.
    let um = Cc["@mozilla.org/updates/update-manager;1"].getService(
      Ci.nsIUpdateManager
    );
    await um.elevationOptedIn();

    // Notify all windows that an application quit has been requested.
    if (!Services.startup.canQuit("restart")) {
      return;
    }

    // If already in safe mode restart in safe mode (bug 327119)
    if (Services.appinfo.inSafeMode) {
      Services.env.set("MOZ_SAFE_MODE_RESTART", "1");
    }

    // Restart the application
    Services.startup.quit(
      Ci.nsIAppStartup.eAttemptQuit | Ci.nsIAppStartup.eRestart
    );
  },
};

window.addEventListener("load", () => gUpdateElevationDialog.onLoad());
