/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "CaptchaDetectionCommunicationChild",
    maxLogLevelPref: "captchadetection.loglevel",
  });
});

/**
 * This actor may run in anywhere the parent actor wants to communicate with.
 * Only created with getActor() method.
 */
export class CaptchaDetectionCommunicationChild extends JSWindowActorChild {
  actorCreated() {
    lazy.console.debug("actorCreated()");
    this.addedMessageListener = false;
  }

  actorDestroy() {
    lazy.console.debug("actorDestroy()");
  }

  #datadomeAddMessageListener() {
    if (this.addedMessageListener) {
      return;
    }
    this.addedMessageListener = true;

    this.contentWindow.addEventListener("message", event => {
      if (
        event.origin !== "https://geo.captcha-delivery.com" &&
        !Cu.isInAutomation
      ) {
        return;
      }

      let data = null;
      try {
        data = JSON.parse(event.data);
        if (!data) {
          return;
        }
      } catch (e) {
        return;
      }

      if (data.eventType === "load" && data.hasOwnProperty("responseType")) {
        this.sendAsyncMessage("CaptchaState:Update", {
          type: "datadome",
          event: "load",
          captchaShown: data.responseType === "captcha",
          blocked: data.responseType === "hardblock",
        });
      } else if (data.eventType === "passed") {
        this.sendAsyncMessage("CaptchaState:Update", {
          type: "datadome",
          event: "passed",
        });
      }
    });
  }

  #testingMetricIsSet() {
    if (!Cu.isInAutomation) {
      throw new Error("This method is only for testing.");
    }

    this.contentWindow.postMessage(
      "Testing:MetricIsSet",
      this.contentWindow.location.origin
    );
  }

  receiveMessage(message) {
    lazy.console.debug("Received message", message);
    switch (message.name) {
      case "Datadome:AddMessageListener":
        this.#datadomeAddMessageListener();
        break;
      case "Testing:MetricIsSet":
        this.#testingMetricIsSet();
        break;
    }
  }
}
