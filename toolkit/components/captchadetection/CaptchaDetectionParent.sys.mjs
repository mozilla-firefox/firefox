/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @type {lazy} */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "CaptchaDetectionParent",
    maxLogLevelPref: "captchadetection.loglevel",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  CaptchaDetectionPingUtils:
    "moz-src:///toolkit/components/captchadetection/CaptchaDetectionPingUtils.sys.mjs",
  CaptchaResponseObserver:
    "moz-src:///toolkit/components/captchadetection/CaptchaResponseObserver.sys.mjs",
});

/**
 * Holds the state of captchas for each top document.
 * Currently, only used by google reCAPTCHA v2 and hCaptcha.
 * The state is an object with the following structure:
 * [key: topBrowsingContextId]: typeof ReturnType<TopDocState.#defaultValue()>
 */
class DocCaptchaState {
  #state;

  constructor() {
    this.#state = new Map();
  }

  /**
   * @param {number} topId - The top bc id.
   * @returns {Map<any, any>} - The state of the top bc.
   */
  get(topId) {
    return this.#state.get(topId);
  }

  static #defaultValue() {
    return new Map();
  }

  /**
   * @param {number} topId - The top bc id.
   * @param {(state: ReturnType<DocCaptchaState['get']>) => void} updateFunction - The function to update the state.
   */
  update(topId, updateFunction) {
    if (!this.#state.has(topId)) {
      this.#state.set(topId, DocCaptchaState.#defaultValue());
    }
    updateFunction(this.#state.get(topId));
  }

  /**
   * @param {number} topId - The top doc id.
   */
  clear(topId) {
    this.#state.delete(topId);
  }
}

const docState = new DocCaptchaState();

/**
 * This actor parent is responsible for recording the state of captchas
 * or communicating with parent browsing context.
 */
class CaptchaDetectionParent extends JSWindowActorParent {
  #responseObserver;

  actorCreated() {
    lazy.console.debug("actorCreated");
  }

  actorDestroy() {
    lazy.console.debug("actorDestroy()");

    this.#onPageHidden();
  }

  /** @type {CaptchaStateUpdateFunction} */
  #updateGRecaptchaV2State({ changes, type }) {
    lazy.console.debug("updateGRecaptchaV2State", changes);

    const topId = this.#topInnerWindowId;
    const isPBM = this.browsingContext.usePrivateBrowsing;

    if (changes === "ImagesShown") {
      docState.update(topId, state => {
        state.set(type + changes, true);
      });

      // We don't call maybeSubmitPing here because we might end up
      // submitting the ping without the "GotCheckmark" event.
      // maybeSubmitPing will be called when "GotCheckmark" event is
      // received, or when the daily maybeSubmitPing is called.
      const shownMetric = "googleRecaptchaV2Ps" + (isPBM ? "Pbm" : "");
      Glean.captchaDetection[shownMetric].add(1);
    } else if (changes === "GotCheckmark") {
      const autoCompleted = !docState.get(topId)?.has(type + "ImagesShown");
      const resultMetric =
        "googleRecaptchaV2" +
        (autoCompleted ? "Ac" : "Pc") +
        (isPBM ? "Pbm" : "");
      Glean.captchaDetection[resultMetric].add(1);
      lazy.console.debug("Incremented metric", resultMetric);
      docState.clear(topId);
      this.#onMetricSet();
    }
  }

  /** @type {CaptchaStateUpdateFunction} */
  #recordCFTurnstileResult({ result }) {
    lazy.console.debug("recordCFTurnstileResult", result);

    const isPBM = this.browsingContext.usePrivateBrowsing;
    const resultMetric =
      "cloudflareTurnstile" +
      (result === "Succeeded" ? "Cc" : "Cf") +
      (isPBM ? "Pbm" : "");
    Glean.captchaDetection[resultMetric].add(1);
    lazy.console.debug("Incremented metric", resultMetric);
    this.#onMetricSet();
  }

  async #datadomeInit() {
    const parent = this.browsingContext.parentWindowContext;
    if (!parent) {
      lazy.console.error("Datadome captcha loaded in a top-level window?");
      return;
    }

    let actor = null;
    try {
      actor = parent.getActor("CaptchaDetectionCommunication");
      if (!actor) {
        lazy.console.error("CaptchaDetection actor not found in parent window");
        return;
      }
    } catch (e) {
      lazy.console.error("Error getting actor", e);
      return;
    }

    await actor.sendQuery("Datadome:AddMessageListener");
  }

  /** @type {CaptchaStateUpdateFunction} */
  #recordDatadomeEvent({ event, ...payload }) {
    lazy.console.debug("recordDatadomeEvent", { event, payload });

    const suffix = this.browsingContext.usePrivateBrowsing ? "Pbm" : "";
    let metricName = "datadome";
    if (event === "load") {
      if (payload.captchaShown) {
        metricName += "Ps";
      } else if (payload.blocked) {
        metricName += "Bl";
      }
    } else if (event === "passed") {
      metricName += "Pc";
    } else {
      lazy.console.error("Unknown Datadome event", event);
      return;
    }

    metricName += suffix;
    Glean.captchaDetection[metricName].add(1);
    lazy.console.debug("Incremented metric", metricName);

    this.#onMetricSet(0);
  }

  /** @type {CaptchaStateUpdateFunction} */
  #recordHCaptchaState({ changes, type }) {
    lazy.console.debug("recordHCaptchaEvent", changes);

    const topId = this.#topInnerWindowId;
    const isPBM = this.browsingContext.usePrivateBrowsing;

    if (changes === "shown") {
      // I don't think HCaptcha supports auto-completion, but we act
      // as if it does just in case.
      docState.update(topId, state => {
        state.set(type + changes, true);
      });

      // We don't call maybeSubmitPing here because we might end up
      // submitting the ping without the "passed" event.
      // maybeSubmitPing will be called when "passed" event is
      // received, or when the daily maybeSubmitPing is called.
      const shownMetric = "hcaptchaPs" + (isPBM ? "Pbm" : "");
      Glean.captchaDetection[shownMetric].add(1);
      lazy.console.debug("Incremented metric", shownMetric);
    } else if (changes === "passed") {
      const autoCompleted = !docState.get(topId)?.has(type + "shown");
      const resultMetric =
        "hcaptcha" + (autoCompleted ? "Ac" : "Pc") + (isPBM ? "Pbm" : "");
      Glean.captchaDetection[resultMetric].add(1);
      lazy.console.debug("Incremented metric", resultMetric);
      docState.clear(topId);
      this.#onMetricSet();
    }
  }

  /** @type {CaptchaStateUpdateFunction} */
  #recordArkoseLabsEvent({ event, solved, solutionsSubmitted }) {
    lazy.console.debug("recordArkoseLabsEvent", {
      event,
      solved,
      solutionsSubmitted,
    });

    const isPBM = this.browsingContext.usePrivateBrowsing;

    const suffix = isPBM ? "Pbm" : "";
    const resultMetric = "arkoselabs" + (solved ? "Pc" : "Pf") + suffix;
    Glean.captchaDetection[resultMetric].add(1);
    lazy.console.debug("Incremented metric", resultMetric);

    const metricName = "arkoselabsSolutionsRequired" + suffix;
    Glean.captchaDetection[metricName].accumulateSingleSample(
      solutionsSubmitted
    );
    lazy.console.debug("Sampled", metricName, "with", solutionsSubmitted);

    this.#onMetricSet();
  }

  async #arkoseLabsInit() {
    let solutionsSubmitted = 0;
    this.#responseObserver = new lazy.CaptchaResponseObserver(
      channel =>
        channel.loadInfo?.browsingContextID === this.browsingContext.id &&
        channel.URI &&
        (Cu.isInAutomation
          ? channel.URI.filePath.endsWith("arkose_labs_api.sjs")
          : channel.URI.spec === "https://client-api.arkoselabs.com/fc/ca/"),
      (_channel, statusCode, responseBody) => {
        if (statusCode !== Cr.NS_OK) {
          return;
        }

        let body;
        try {
          body = JSON.parse(responseBody);
          if (!body) {
            lazy.console.debug(
              "ResponseObserver:ResponseBody",
              "Failed to parse JSON"
            );
            return;
          }
        } catch (e) {
          lazy.console.debug(
            "ResponseObserver:ResponseBody",
            "Failed to parse JSON",
            e,
            responseBody
          );
          return;
        }

        // Check for the presence of the expected keys
        if (["response", "solved"].some(key => !body.hasOwnProperty(key))) {
          lazy.console.debug(
            "ResponseObserver:ResponseBody",
            "Missing keys",
            body
          );
          return;
        }

        solutionsSubmitted++;
        if (typeof body.solved !== "boolean") {
          return;
        }

        this.#recordArkoseLabsEvent({
          event: "completed",
          solved: body.solved,
          solutionsSubmitted,
        });

        solutionsSubmitted = 0;
      }
    );
    this.#responseObserver.register();
  }

  get #topInnerWindowId() {
    return this.browsingContext.topWindowContext.innerWindowId;
  }

  #onPageHidden() {
    docState.clear(this.#topInnerWindowId);

    if (this.#responseObserver) {
      this.#responseObserver.unregister();
    }
  }

  async #onMetricSet(parentDepth = 1) {
    lazy.CaptchaDetectionPingUtils.maybeSubmitPing();
    if (Cu.isInAutomation) {
      await this.#notifyTestMetricIsSet(parentDepth);
    }
  }

  /**
   * Notify the `parentDepth`'nth parent browsing context that the test metric is set.
   *
   * @param {number} parentDepth - The depth of the parent window context.
   * The reason we need this param is because Datadome calls this method
   * not from the captcha iframe, but its parent browsing context. So
   * it overrides the depth to 0.
   */
  async #notifyTestMetricIsSet(parentDepth = 1) {
    if (!Cu.isInAutomation) {
      throw new Error("This method should only be called in automation");
    }

    let parent = this.browsingContext.currentWindowContext;
    for (let i = 0; i < parentDepth; i++) {
      parent = parent.parentWindowContext;
      if (!parent) {
        lazy.console.error("No parent window context");
        return;
      }
    }

    let actor = null;
    try {
      actor = parent.getActor("CaptchaDetectionCommunication");
      if (!actor) {
        lazy.console.error("CaptchaDetection actor not found in parent window");
        return;
      }
    } catch (e) {
      lazy.console.error("Error getting actor", e);
      return;
    }

    await actor.sendQuery("Testing:MetricIsSet");
  }

  recordCaptchaHandlerConstructed({ type }) {
    lazy.console.debug("recordCaptchaHandlerConstructed", type);

    let metric = "";
    switch (type) {
      case "g-recaptcha-v2":
        metric = "googleRecaptchaV2Oc";
        break;
      case "cf-turnstile":
        metric = "cloudflareTurnstileOc";
        break;
      case "datadome":
        metric = "datadomeOc";
        break;
      case "hCaptcha":
        metric = "hcaptchaOc";
        break;
      case "arkoseLabs":
        metric = "arkoselabsOc";
        break;
    }
    metric += this.browsingContext.usePrivateBrowsing ? "Pbm" : "";
    Glean.captchaDetection[metric].add(1);
    lazy.console.debug("Incremented metric", metric);
  }

  async receiveMessage(message) {
    lazy.console.debug("receiveMessage", message);

    switch (message.name) {
      case "CaptchaState:Update":
        switch (message.data.type) {
          case "g-recaptcha-v2":
            this.#updateGRecaptchaV2State(message.data);
            break;
          case "cf-turnstile":
            this.#recordCFTurnstileResult(message.data);
            break;
          case "datadome":
            this.#recordDatadomeEvent(message.data);
            break;
          case "hCaptcha":
            this.#recordHCaptchaState(message.data);
            break;
        }
        break;
      case "CaptchaHandler:Constructed":
        // message.name === "CaptchaHandler:Constructed"
        // => message.data = {
        //   type: string,
        // }
        this.recordCaptchaHandlerConstructed(message.data);
        break;
      case "Page:Hide":
        // message.name === "TabState:Closed"
        // => message.data = undefined
        this.#onPageHidden();
        break;
      case "CaptchaDetection:Init":
        // message.name === "CaptchaDetection:Init"
        // => message.data = {
        //   type: string,
        // }
        switch (message.data.type) {
          case "datadome":
            return this.#datadomeInit();
          case "arkoseLabs":
            return this.#arkoseLabsInit();
        }
        break;
      default:
        lazy.console.error("Unknown message", message);
    }
    return null;
  }
}

export {
  CaptchaDetectionParent,
  CaptchaDetectionParent as CaptchaDetectionCommunicationParent,
};

/**
 * @typedef lazy
 * @type {object}
 * @property {ConsoleInstance} console - console instance.
 * @property {typeof import("./CaptchaDetectionPingUtils.sys.mjs").CaptchaDetectionPingUtils} CaptchaDetectionPingUtils - CaptchaDetectionPingUtils module.
 * @property {typeof import("./CaptchaResponseObserver.sys.mjs").CaptchaResponseObserver} CaptchaResponseObserver - CaptchaResponseObserver module.
 */

/**
 * @typedef CaptchaStateUpdateMessageData
 * @type {object}
 * @property {string} type - The type of the captcha.
 *
 * @typedef {(message: CaptchaStateUpdateMessageData) => void} CaptchaStateUpdateFunction
 */
