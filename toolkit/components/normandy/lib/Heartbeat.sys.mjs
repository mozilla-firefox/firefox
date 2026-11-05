/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { TelemetryController } from "resource://gre/modules/TelemetryController.sys.mjs";
import { clearTimeout, setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { CleanupManager } from "resource://normandy/lib/CleanupManager.sys.mjs";
import { EventEmitter } from "resource://normandy/lib/EventEmitter.sys.mjs";
import { LogManager } from "resource://normandy/lib/LogManager.sys.mjs";

const PREF_SURVEY_DURATION = "browser.uitour.surveyDuration";
const NOTIFICATION_TIME = 3000;
const HEARTBEAT_CSS_URI = Services.io.newURI(
  "resource://normandy/skin/shared/Heartbeat.css"
);
const log = LogManager.getLogger("heartbeat");
const windowsWithInjectedCss = new WeakSet();
let anyWindowsWithInjectedCss = false;

// Add cleanup handler for CSS injected into windows by Heartbeat
CleanupManager.addCleanupHandler(() => {
  if (anyWindowsWithInjectedCss) {
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      if (windowsWithInjectedCss.has(window)) {
        const utils = window.windowUtils;
        utils.removeSheet(HEARTBEAT_CSS_URI, window.AGENT_SHEET);
        windowsWithInjectedCss.delete(window);
      }
    }
  }
});

/**
 * Show the Heartbeat UI to request user feedback.
 *
 * @param chromeWindow
 *        The chrome window that the heartbeat notification is displayed in.
 * @param {object} options Options object.
 * @param {string} options.message
 *        The message, or question, to display on the notification.
 * @param {string} options.thanksMessage
 *        The thank you message to display after user votes.
 * @param {string} options.flowId
 *        An identifier for this rating flow. Please note that this is only used to
 *        identify the notification box.
 * @param {string} [options.engagementButtonLabel=null]
 *        The text of the engagement button to use instead of stars. If this is null
 *        or invalid, rating stars are used.
 * @param {string} [options.learnMoreMessage=null]
 *        The label of the learn more link. No link will be shown if this is null.
 * @param {string} [options.learnMoreUrl=null]
 *        The learn more URL to open when clicking on the learn more link. No learn more
 *        will be shown if this is an invalid URL.
 * @param {string} [options.surveyId]
 *        An ID for the survey, reflected in the Telemetry ping.
 * @param {number} [options.surveyVersion]
 *        Survey's version number, reflected in the Telemetry ping.
 * @param {boolean} [options.testing]
 *        Whether this is a test survey, reflected in the Telemetry ping.
 * @param {string} [options.postAnswerURL=null]
 *        The url to visit after the user answers the question.
 */
export var Heartbeat = class {
  constructor(chromeWindow, options) {
    if (typeof options.flowId !== "string") {
      throw new Error(
        `flowId must be a string, but got ${JSON.stringify(
          options.flowId
        )}, a ${typeof options.flowId}`
      );
    }

    if (!options.flowId) {
      throw new Error("flowId must not be an empty string");
    }

    if (typeof options.message !== "string") {
      throw new Error(
        `message must be a string, but got ${JSON.stringify(
          options.message
        )}, a ${typeof options.message}`
      );
    }

    if (!options.message) {
      throw new Error("message must not be an empty string");
    }

    if (options.postAnswerUrl) {
      options.postAnswerUrl = new URL(options.postAnswerUrl);
    } else {
      options.postAnswerUrl = null;
    }

    if (options.learnMoreUrl) {
      options.learnMoreUrl = URL.parse(options.learnMoreUrl);
    }

    this.chromeWindow = chromeWindow;
    this.eventEmitter = new EventEmitter();
    this.options = options;
    this.surveyResults = {};
    this.buttons = [];

    if (!windowsWithInjectedCss.has(chromeWindow)) {
      windowsWithInjectedCss.add(chromeWindow);
      const utils = chromeWindow.windowUtils;
      utils.loadSheet(HEARTBEAT_CSS_URI, chromeWindow.AGENT_SHEET);
      anyWindowsWithInjectedCss = true;
    }

    // so event handlers are consistent
    this.handleWindowClosed = this.handleWindowClosed.bind(this);
    this.close = this.close.bind(this);

    // Add Learn More Link
    if (this.options.learnMoreMessage && this.options.learnMoreUrl) {
      this.buttons.push({
        link: this.options.learnMoreUrl.toString(),
        label: this.options.learnMoreMessage,
        callback: () => {
          this.maybeNotifyHeartbeat("LearnMore");
          return true;
        },
      });
    }

    if (this.options.engagementButtonLabel) {
      this.buttons.push({
        label: this.options.engagementButtonLabel,
        callback: () => {
          // Let the consumer know user engaged.
          this.maybeNotifyHeartbeat("Engaged");

          this.userEngaged({
            type: "button",
            flowId: this.options.flowId,
          });

          // Return true so that the notification bar doesn't close itself since
          // we have a thank you message to show.
          return true;
        },
      });
    }

    // Build the heartbeat stars
    if (!this.options.engagementButtonLabel) {
      this.ratingContainer = this.chromeWindow.document.createElement("span");

      const fiveStarElement =
        this.chromeWindow.document.createElement("moz-five-star");

      fiveStarElement.selectable = true;

      // Add the click handler
      fiveStarElement.addEventListener("select", ev => {
        const rating = ev.detail.rating;
        this.maybeNotifyHeartbeat("Voted", { score: rating });
        this.userEngaged({
          type: "stars",
          score: rating,
          flowId: this.options.flowId,
        });
      });

      this.ratingContainer.appendChild(fiveStarElement);
    }

    this.notificationBox = this.chromeWindow.gNotificationBox;
    this.noticePromise = new Promise(resolve => {
      this.notificationBox
        .appendNotification(
          "heartbeat-" + this.options.flowId,
          {
            label: this.options.message,
            image: "resource://normandy/skin/shared/heartbeat-icon.svg",
            priority: this.notificationBox.PRIORITY_SYSTEM,
            eventCallback: eventType => {
              if (eventType !== "removed") {
                return;
              }
              this.maybeNotifyHeartbeat("NotificationClosed");
            },
          },
          this.buttons
        )
        .then(noticeEl => {
          noticeEl.classList.add("heartbeat");
          this.chromeWindow.requestAnimationFrame(() => {
            noticeEl.messageText.classList.add("heartbeat");
          });
          if (this.ratingContainer) {
            noticeEl.buttonContainer.append(this.ratingContainer);
          }
          resolve(noticeEl);
        }, resolve);
    });

    // Let the consumer know the notification was shown.
    this.maybeNotifyHeartbeat("NotificationOffered");
    this.chromeWindow.addEventListener(
      "SSWindowClosing",
      this.handleWindowClosed
    );

    const surveyDuration =
      Services.prefs.getIntPref(PREF_SURVEY_DURATION, 300) * 1000;
    this.surveyEndTimer = setTimeout(() => {
      this.maybeNotifyHeartbeat("SurveyExpired");
      this.close();
    }, surveyDuration);

    CleanupManager.addCleanupHandler(this.close);
  }

  maybeNotifyHeartbeat(name, data = {}) {
    if (this.pingSent) {
      log.warn(
        "Heartbeat event received after Telemetry ping sent. name:",
        name,
        "data:",
        data
      );
      return;
    }

    const timestamp = Date.now();
    let sendPing = false;
    let cleanup = false;

    const phases = {
      NotificationOffered: () => {
        this.surveyResults.flowId = this.options.flowId;
        this.surveyResults.offeredTS = timestamp;
      },
      LearnMore: () => {
        if (!this.surveyResults.learnMoreTS) {
          this.surveyResults.learnMoreTS = timestamp;
        }
      },
      Engaged: () => {
        this.surveyResults.engagedTS = timestamp;
      },
      Voted: () => {
        this.surveyResults.votedTS = timestamp;
        this.surveyResults.score = data.score;
      },
      SurveyExpired: () => {
        this.surveyResults.expiredTS = timestamp;
      },
      NotificationClosed: () => {
        this.surveyResults.closedTS = timestamp;
        cleanup = true;
        sendPing = true;
      },
      WindowClosed: () => {
        this.surveyResults.windowClosedTS = timestamp;
        cleanup = true;
        sendPing = true;
      },
      default: () => {
        log.error("Unrecognized Heartbeat event:", name);
      },
    };

    (phases[name] || phases.default)();

    data.timestamp = timestamp;
    data.flowId = this.options.flowId;
    this.eventEmitter.emit(name, data);

    if (sendPing) {
      // Send the ping to Telemetry
      const payload = Object.assign({ version: 1 }, this.surveyResults);
      for (const meta of ["surveyId", "surveyVersion", "testing"]) {
        if (this.options.hasOwnProperty(meta)) {
          payload[meta] = this.options[meta];
        }
      }

      log.debug("Sending telemetry");
      TelemetryController.submitExternalPing("heartbeat", payload, {
        addClientId: true,
        addEnvironment: true,
      });

      const gleanOmittedFields = ["version", "surveyVersion", "testing"];
      for (const [k, v] of Object.entries(payload)) {
        if (gleanOmittedFields.includes(k)) {
          continue;
        }
        const metricName = k.endsWith("TS") ? k.slice(0, -2) : k;
        Glean.heartbeat[metricName].set(v);
      }
      GleanPings.heartbeat.submit();

      // only for testing
      this.eventEmitter.emit("TelemetrySent", payload);

      // Survey is complete, clear out the expiry timer & survey configuration
      this.endTimerIfPresent("surveyEndTimer");

      this.pingSent = true;
      this.surveyResults = null;
    }

    if (cleanup) {
      this.cleanup();
    }
  }

  userEngaged(engagementParams) {
    this.noticePromise.then(noticeEl => {
      // Make the heartbeat icon pulse twice
      noticeEl.label = this.options.thanksMessage;
      noticeEl.messageImage?.classList.remove("pulse-onshow");
      noticeEl.messageImage?.classList.add("pulse-twice");
      // Remove the custom contents of the buttons
      for (let button of noticeEl.buttonContainer.querySelectorAll("button")) {
        button.remove();
      }
    });

    // Remove the custom contents of the notice
    if (this.ratingContainer) {
      this.ratingContainer.remove();
    }

    // Open the engagement tab if we have a valid engagement URL.
    if (this.options.postAnswerUrl) {
      for (const key in engagementParams) {
        this.options.postAnswerUrl.searchParams.append(
          key,
          engagementParams[key]
        );
      }
      // Open the engagement URL in a new tab.
      let { gBrowser } = this.chromeWindow;
      gBrowser.selectedTab = gBrowser.addWebTab(
        this.options.postAnswerUrl.toString(),
        {
          triggeringPrincipal:
            Services.scriptSecurityManager.createNullPrincipal({}),
        }
      );
    }

    this.endTimerIfPresent("surveyEndTimer");

    this.engagementCloseTimer = setTimeout(
      () => this.close(),
      NOTIFICATION_TIME
    );
  }

  endTimerIfPresent(timerName) {
    if (this[timerName]) {
      clearTimeout(this[timerName]);
      this[timerName] = null;
    }
  }

  handleWindowClosed() {
    this.maybeNotifyHeartbeat("WindowClosed");
  }

  close() {
    this.noticePromise.then(noticeEl =>
      this.notificationBox.removeNotification(noticeEl)
    );
  }

  cleanup() {
    // Kill the timers which might call things after we've cleaned up:
    this.endTimerIfPresent("surveyEndTimer");
    this.endTimerIfPresent("engagementCloseTimer");
    // remove listeners
    this.chromeWindow.removeEventListener(
      "SSWindowClosing",
      this.handleWindowClosed
    );
    // remove references for garbage collection
    this.chromeWindow = null;
    this.notificationBox = null;
    this.noticePromise = null;
    this.ratingContainer = null;
    this.eventEmitter = null;
    // Ensure we don't re-enter and release the CleanupManager's reference to us:
    CleanupManager.removeCleanupHandler(this.close);
  }
};
