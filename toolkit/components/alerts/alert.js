/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// Copied from nsILookAndFeel.h, see comments on eMetric_AlertNotificationOrigin
const NS_ALERT_HORIZONTAL = 1;
const NS_ALERT_LEFT = 2;
const NS_ALERT_TOP = 4;

const WINDOW_MARGIN = AppConstants.platform == "win" ? 0 : 10;
const WINDOW_SHADOW_SPREAD = AppConstants.platform == "win" ? 10 : 0;

var gOrigin = 0; // Default value: alert from bottom right.
var gReplacedWindow = null;
var gAlertCallbacks = null;
var gAlertTextClickable = false;
var gAlertCookie = "";
var gIsActive = false;
var gIsReplaced = false;
var gRequireInteraction = false;

function prefillAlertInfo() {
  // unwrap all the args....
  // arguments[0] --> the alert icon image
  // arguments[1] --> the alert title
  // arguments[2] --> the alert text
  // arguments[3] --> is the text clickable?
  // arguments[4] --> the alert cookie to be passed back to the listener
  // arguments[5] --> the alert origin reported by the look and feel
  // arguments[6] --> bidi
  // arguments[7] --> lang
  // arguments[8] --> requires interaction
  // arguments[9] --> replaced alert window (nsIDOMWindow)
  // arguments[10] --> an optional callback listener (nsIObserver)
  // arguments[11] -> the nsIURI.hostPort of the origin, optional

  switch (window.arguments.length) {
    default:
    case 12: {
      if (window.arguments[11]) {
        let alertBox = document.getElementById("alertBox");
        alertBox.setAttribute("hasOrigin", true);

        let hostPort = window.arguments[11];
        const ALERT_BUNDLE = Services.strings.createBundle(
          "chrome://alerts/locale/alert.properties"
        );
        const BRAND_BUNDLE = Services.strings.createBundle(
          "chrome://branding/locale/brand.properties"
        );
        const BRAND_NAME = BRAND_BUNDLE.GetStringFromName("brandShortName");
        let label = document.getElementById("alertSourceLabel");
        label.setAttribute(
          "value",
          ALERT_BUNDLE.formatStringFromName("source.label", [hostPort])
        );
        let doNotDisturbMenuItem = document.getElementById(
          "doNotDisturbMenuItem"
        );
        doNotDisturbMenuItem.setAttribute(
          "label",
          ALERT_BUNDLE.formatStringFromName("pauseNotifications.label", [
            BRAND_NAME,
          ])
        );
        let disableForOrigin = document.getElementById(
          "disableForOriginMenuItem"
        );
        disableForOrigin.setAttribute(
          "label",
          ALERT_BUNDLE.formatStringFromName(
            "webActions.disableForOrigin.label",
            [hostPort]
          )
        );
        let openSettings = document.getElementById("openSettingsMenuItem");
        openSettings.setAttribute(
          "label",
          ALERT_BUNDLE.GetStringFromName("webActions.settings.label")
        );
      }
    }
    // fall through
    case 11:
      gAlertCallbacks = window.arguments[10];
    // fall through
    case 10:
      gReplacedWindow = window.arguments[9];
    // fall through
    case 9:
      gRequireInteraction = window.arguments[8];
    // fall through
    case 8:
      if (window.arguments[7]) {
        document
          .getElementById("alertTitleLabel")
          .setAttribute("lang", window.arguments[7]);
        document
          .getElementById("alertTextLabel")
          .setAttribute("lang", window.arguments[7]);
      }
    // fall through
    case 7:
      if (window.arguments[6]) {
        document.getElementById("alertNotification").style.direction =
          window.arguments[6];
      }
    // fall through
    case 6:
      gOrigin = window.arguments[5];
    // fall through
    case 5:
      gAlertCookie = window.arguments[4];
    // fall through
    case 4:
      gAlertTextClickable = window.arguments[3];
      if (gAlertTextClickable) {
        document
          .getElementById("alertNotification")
          .setAttribute("clickable", true);
        document
          .getElementById("alertTextLabel")
          .setAttribute("clickable", true);
      }
    // fall through
    case 3:
      if (window.arguments[2]) {
        document.getElementById("alertBox").setAttribute("hasBodyText", true);
        document.getElementById("alertTextLabel").textContent =
          window.arguments[2];
      }
    // fall through
    case 2:
      document
        .getElementById("alertTitleLabel")
        .setAttribute("value", window.arguments[1]);
    // fall through
    case 1:
      if (window.arguments[0]) {
        const imgContainer = window.arguments[0];

        // Convert the imgIContainer to a data URL and display it.
        const imgTools = Cc["@mozilla.org/image/tools;1"].getService(
          Ci.imgITools
        );
        const imageStream = imgTools.encodeImage(imgContainer, "image/png");

        const binaryStream = Cc[
          "@mozilla.org/binaryinputstream;1"
        ].createInstance(Ci.nsIBinaryInputStream);
        binaryStream.setInputStream(imageStream);
        const available = binaryStream.available();

        const buffer = new ArrayBuffer(available);
        binaryStream.readArrayBuffer(available, buffer);
        let array = new Uint8Array(buffer);

        document.getElementById("alertBox").setAttribute("hasImage", true);

        document
          .getElementById("alertImage")
          .setAttribute("src", "data:image/png;base64," + array.toBase64());
      }
    // fall through
    case 0:
      break;
  }
}

function onAlertLoad() {
  const ALERT_DURATION_IMMEDIATE = 20000;
  let alertTextBox = document.getElementById("alertTextBox");
  let alertImageBox = document.getElementById("alertImageBox");
  alertImageBox.style.minHeight = alertTextBox.scrollHeight + "px";

  window.sizeToContent();

  let alertTextLabel = document.getElementById("alertTextLabel");
  if (alertTextLabel.scrollHeight > alertTextLabel.clientHeight) {
    alertTextLabel.setAttribute("tooltiptext", alertTextLabel.textContent);
  }

  if (gReplacedWindow && !gReplacedWindow.closed) {
    moveWindowToReplace(gReplacedWindow);
    gReplacedWindow.gIsReplaced = true;
    gReplacedWindow.close();
  } else {
    moveWindowToEnd();
  }

  window.addEventListener("click", onAlertClick);
  window.addEventListener("beforeunload", onAlertBeforeUnload);
  window.addEventListener("XULAlertClose", function () {
    window.close();
  });

  // If the require interaction flag is set, prevent auto-closing the notification.
  if (!gRequireInteraction) {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      setTimeout(function () {
        window.close();
      }, ALERT_DURATION_IMMEDIATE);
    } else {
      let alertBox = document.getElementById("alertBox");
      alertBox.addEventListener("animationend", function hideAlert(event) {
        if (
          event.animationName == "alert-animation" ||
          event.animationName == "alert-clicked-animation" ||
          event.animationName == "alert-closing-animation"
        ) {
          alertBox.removeEventListener("animationend", hideAlert);
          window.close();
        }
      });
      alertBox.setAttribute("animate", true);
    }
  }

  let alertSettings = document.getElementById("alertSettings");
  alertSettings.addEventListener("focus", onAlertSettingsFocus);
  alertSettings.addEventListener("click", onAlertSettingsClick);

  document
    .getElementById("alert-close")
    .addEventListener("click", event => event.stopPropagation());
  document
    .getElementById("alert-close")
    .addEventListener("command", onAlertClose);
  document
    .getElementById("doNotDisturbMenuItem")
    .addEventListener("command", doNotDisturb);
  document
    .getElementById("disableForOriginMenuItem")
    .addEventListener("command", disableForOrigin);
  document
    .getElementById("openSettingsMenuItem")
    .addEventListener("command", openSettings);

  gIsActive = true;

  let ev = new CustomEvent("AlertActive", { bubbles: true, cancelable: true });
  document.documentElement.dispatchEvent(ev);

  if (gAlertCallbacks) {
    gAlertCallbacks.onAlertShow();
  }
}

function moveWindowToReplace(aReplacedAlert) {
  let heightDelta = window.outerHeight - aReplacedAlert.outerHeight;

  // Move windows that come after the replaced alert if the height is different.
  if (heightDelta != 0) {
    for (let alertWindow of Services.wm.getEnumerator("alert:alert")) {
      if (!alertWindow.gIsActive) {
        continue;
      }
      // boolean to determine if the alert window is after the replaced alert.
      let alertIsAfter =
        gOrigin & NS_ALERT_TOP
          ? alertWindow.screenY > aReplacedAlert.screenY
          : aReplacedAlert.screenY > alertWindow.screenY;
      if (alertIsAfter) {
        // The new Y position of the window.
        let adjustedY =
          gOrigin & NS_ALERT_TOP
            ? alertWindow.screenY + heightDelta
            : alertWindow.screenY - heightDelta;
        alertWindow.moveTo(alertWindow.screenX, adjustedY);
      }
    }
  }

  let adjustedY =
    gOrigin & NS_ALERT_TOP
      ? aReplacedAlert.screenY
      : aReplacedAlert.screenY - heightDelta;
  window.moveTo(aReplacedAlert.screenX, adjustedY);
}

function moveWindowToEnd() {
  // Determine position
  let x =
    gOrigin & NS_ALERT_LEFT
      ? screen.availLeft
      : screen.availLeft + screen.availWidth - window.outerWidth;
  let y =
    gOrigin & NS_ALERT_TOP
      ? screen.availTop
      : screen.availTop + screen.availHeight - window.outerHeight;

  // Position the window at the end of all alerts.
  for (let alertWindow of Services.wm.getEnumerator("alert:alert")) {
    if (alertWindow != window && alertWindow.gIsActive) {
      if (gOrigin & NS_ALERT_TOP) {
        y = Math.max(
          y,
          alertWindow.screenY + alertWindow.outerHeight - WINDOW_SHADOW_SPREAD
        );
      } else {
        y = Math.min(
          y,
          alertWindow.screenY - window.outerHeight + WINDOW_SHADOW_SPREAD
        );
      }
    }
  }

  // Offset the alert by WINDOW_MARGIN pixels from the edge of the screen
  y += gOrigin & NS_ALERT_TOP ? WINDOW_MARGIN : -WINDOW_MARGIN;
  x += gOrigin & NS_ALERT_LEFT ? WINDOW_MARGIN : -WINDOW_MARGIN;

  window.moveTo(x, y);
}

function onAlertBeforeUnload() {
  if (!gIsReplaced) {
    // Move other alert windows to fill the gap left by closing alert.
    let heightDelta = window.outerHeight + WINDOW_MARGIN - WINDOW_SHADOW_SPREAD;
    for (let alertWindow of Services.wm.getEnumerator("alert:alert")) {
      if (alertWindow != window && alertWindow.gIsActive) {
        if (gOrigin & NS_ALERT_TOP) {
          if (alertWindow.screenY > window.screenY) {
            alertWindow.moveTo(
              alertWindow.screenX,
              alertWindow.screenY - heightDelta
            );
          }
        } else if (window.screenY > alertWindow.screenY) {
          alertWindow.moveTo(
            alertWindow.screenX,
            alertWindow.screenY + heightDelta
          );
        }
      }
    }
  }

  if (gAlertCallbacks) {
    gAlertCallbacks.onAlertFinished();
  }
}

function onAlertClick() {
  if (gAlertCallbacks && gAlertTextClickable) {
    gAlertCallbacks.onAlertClick();
  }

  let alertBox = document.getElementById("alertBox");
  if (alertBox.getAttribute("animate") == "true") {
    // Closed when the animation ends.
    alertBox.setAttribute("clicked", "true");
  } else {
    window.close();
  }
}

function doNotDisturb() {
  const alertService = Cc["@mozilla.org/alerts-service;1"]
    .getService(Ci.nsIAlertsService)
    .QueryInterface(Ci.nsIAlertsDoNotDisturb);
  alertService.manualDoNotDisturb = true;
  onAlertClose();
}

function disableForOrigin() {
  gAlertCallbacks.onAlertDisable();
  onAlertClose();
}

function onAlertSettingsFocus(event) {
  event.target.removeAttribute("focusedViaMouse");
}

function onAlertSettingsClick(event) {
  // XXXjaws Hack used to remove the focus-ring only
  // from mouse interaction, but focus-ring drawing
  // should only be enabled when interacting via keyboard.
  event.target.setAttribute("focusedViaMouse", true);
  event.stopPropagation();
}

function openSettings() {
  gAlertCallbacks.onAlertSettings();
  onAlertClose();
}

function onAlertClose() {
  let alertBox = document.getElementById("alertBox");
  if (alertBox.getAttribute("animate") == "true") {
    // Closed when the animation ends.
    alertBox.setAttribute("closing", "true");
  } else {
    window.close();
  }
}

window.addEventListener("DOMContentLoaded", prefillAlertInfo);
window.addEventListener("load", onAlertLoad);
