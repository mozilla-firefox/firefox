/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @type {lazy} */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "CaptchaDetectionChild",
    maxLogLevelPref: "captchadetection.loglevel",
  });
});

/**
 * Abstract class for handling captchas.
 */
class CaptchaHandler {
  static type = "abstract";

  /**
   * @param {CaptchaDetectionChild} actor - The window actor.
   * @param {Event} _event - The initial event that created the actor.
   * @param {boolean} skipConstructedNotif - Whether to skip the constructed notification. Used for captchas with multiple frames.
   */
  constructor(actor, _event, skipConstructedNotif = false) {
    /** @type {CaptchaDetectionChild} */
    this.actor = actor;
    if (!skipConstructedNotif) {
      this.notifyConstructed();
    }
  }

  notifyConstructed() {
    lazy.console.debug(`CaptchaHandler constructed: ${this.constructor.type}`);
    this.actor.sendAsyncMessage("CaptchaHandler:Constructed", {
      type: this.constructor.type,
    });
  }

  static matches(_document) {
    throw new Error("abstract method");
  }

  updateState(state) {
    this.actor.sendAsyncMessage("CaptchaState:Update", state);
  }

  onActorDestroy() {
    lazy.console.debug("CaptchaHandler destroyed");
  }

  /**
   * @param {Event} event - The event to handle.
   */
  handleEvent(event) {
    lazy.console.debug("CaptchaHandler got event:", event);
  }

  /**
   * @param {ReceiveMessageArgument} message - The message to handle.
   */
  receiveMessage(message) {
    lazy.console.debug("CaptchaHandler got message:", message);
  }
}

/**
 * Handles Google Recaptcha v2 captchas.
 *
 * ReCaptcha v2 places two iframes in the page. One for the
 * challenge and one for the checkmark. This handler listens
 * for the checkmark being clicked or the challenge being
 * shown. When either of these events happen, the handler
 * sends a message to the parent actor. The handler then
 * disconnects the mutation observer to avoid further
 * processing.
 */
class GoogleRecaptchaV2Handler extends CaptchaHandler {
  #enabled;
  #mutationObserver;

  static type = "g-recaptcha-v2";

  constructor(actor, event) {
    super(
      actor,
      event,
      actor.document.location.pathname.endsWith("/bframe") ||
        (Cu.isInAutomation &&
          actor.document.location.pathname.endsWith(
            "g_recaptcha_v2_checkbox.html"
          ))
    );
    this.#enabled = true;
    this.#mutationObserver = new this.actor.contentWindow.MutationObserver(
      this.#mutationHandler.bind(this)
    );
    this.#mutationObserver.observe(this.actor.document, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["style"],
    });
  }

  static matches(document) {
    if (Cu.isInAutomation) {
      return (
        document
          .getElementById("captchaType")
          ?.getAttribute("data-captcha-type") === GoogleRecaptchaV2Handler.type
      );
    }

    return (
      [
        "https://www.google.com/recaptcha/api2/",
        "https://www.google.com/recaptcha/enterprise/",
      ].some(match => document.location.href.startsWith(match)) &&
      !document.location.search.includes("size=invisible")
    );
  }

  #mutationHandler(_mutations, observer) {
    if (!this.#enabled) {
      return;
    }

    const token = this.actor.document.getElementById("recaptcha-token");
    const initialized = token && token.value !== "";
    if (!initialized) {
      return;
    }

    const checkmark = this.actor.document.getElementById("recaptcha-anchor");
    if (checkmark && checkmark.ariaChecked === "true") {
      this.updateState({
        type: GoogleRecaptchaV2Handler.type,
        changes: "GotCheckmark",
      });
      this.#enabled = false;
      observer.disconnect();
      return;
    }

    const images = this.actor.document.getElementById("rc-imageselect");
    if (images) {
      this.updateState({
        type: GoogleRecaptchaV2Handler.type,
        changes: "ImagesShown",
      });
      this.#enabled = false;
      observer.disconnect();
    }
  }

  onActorDestroy() {
    super.onActorDestroy();
    this.#mutationObserver.disconnect();
  }
}

/**
 * Handles Cloudflare Turnstile captchas.
 *
 * Cloudflare Turnstile captchas have a success and fail div
 * that are displayed when the captcha is completed. This
 * handler listens for the success or fail div to be displayed
 * and sends a message to the parent actor when either of
 * these events happen. The handler then disconnects the
 * mutation observer to avoid further processing.
 * We use two mutation observers to detect shadowroot
 * creation and then observe the shadowroot for the success
 * or fail div.
 */
class CFTurnstileHandler extends CaptchaHandler {
  #observingShadowRoot;
  #mutationObserver;
  #reportedResult;

  static type = "cf-turnstile";

  constructor(actor, event) {
    super(actor, event);
    this.#observingShadowRoot = false;
    this.#reportedResult = false;
    if (this.actor.document.body?.openOrClosedShadowRoot) {
      this.#observeShadowRoot(this.actor.document.body.openOrClosedShadowRoot);
      return;
    }
    this.#mutationObserver = new this.actor.contentWindow.MutationObserver(
      this.#mutationHandler.bind(this)
    );
    this.#mutationObserver.observe(this.actor.document.documentElement, {
      attributes: true,
    });
  }

  static matchesRegex = new RegExp(
    "https://challenges.cloudflare.com/cdn-cgi/challenge-platform/.+?/turnstile/(?:if/ov2/av0/rcv|f/av0/rch)/"
  );

  static matches(document) {
    if (Cu.isInAutomation) {
      return (
        document
          .getElementById("captchaType")
          ?.getAttribute("data-captcha-type") === CFTurnstileHandler.type
      );
    }

    return CFTurnstileHandler.matchesRegex.test(document.location.href);
  }

  #mutationHandler(_mutations, observer) {
    lazy.console.debug(_mutations);
    if (this.#observingShadowRoot) {
      return;
    }

    const shadowRoot = this.actor.document.body?.openOrClosedShadowRoot;
    if (!shadowRoot) {
      return;
    }
    observer.disconnect();
    lazy.console.debug("Found shadowRoot", shadowRoot);

    this.#observeShadowRoot(shadowRoot);
  }

  #observeShadowRoot(shadowRoot) {
    if (this.#observingShadowRoot) {
      return;
    }
    this.#observingShadowRoot = true;

    this.#mutationObserver = new this.actor.contentWindow.MutationObserver(() =>
      this.#checkResult(shadowRoot)
    );
    this.#mutationObserver.observe(shadowRoot, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["style"],
    });

    // A MutationObserver only reports mutations made after observe(), so the
    // result may already be showing by the time we get here. Check once now.
    this.#checkResult(shadowRoot);
  }

  #checkResult(shadowRoot) {
    if (this.#reportedResult) {
      return;
    }

    const fail = shadowRoot.getElementById("fail");
    const success = shadowRoot.getElementById("success");
    if (!fail || !success) {
      return;
    }

    let result;
    if (fail.style.display !== "none") {
      result = "Failed";
    } else if (success.style.display !== "none") {
      result = "Succeeded";
    } else {
      return;
    }

    lazy.console.debug(`Captcha ${result}`);
    this.#reportedResult = true;
    this.#mutationObserver.disconnect();
    this.updateState({
      type: CFTurnstileHandler.type,
      result,
    });
  }

  onActorDestroy() {
    super.onActorDestroy();
    this.#mutationObserver?.disconnect();
  }
}

/**
 * Handles Datadome captchas.
 *
 * Datadome works by placing an iframe that postMessages to
 * the parent window. This actor attaches to the iframe
 * that posts messages to the parent window. Therefore, we
 * ask the parent actor to initialize a new actor in the
 * parent window. That actor will then listen for messages
 * from the iframe and send a message to the parent actor
 * when the captcha is completed.
 */
class DatadomeHandler extends CaptchaHandler {
  static type = "datadome";

  constructor(actor, event) {
    super(actor, event);

    event.stopImmediatePropagation();

    this.actor
      .sendQuery("CaptchaDetection:Init", { type: DatadomeHandler.type })
      .then(() => {
        // re-dispatch the event
        event.target.dispatchEvent(event);
      });
  }

  static matches(document) {
    if (Cu.isInAutomation) {
      return (
        document
          .getElementById("captchaType")
          ?.getAttribute("data-captcha-type") === DatadomeHandler.type
      );
    }

    return document.location.href.startsWith(
      "https://geo.captcha-delivery.com/captcha/"
    );
  }
}

/**
 * Handles hCaptcha captchas.
 *
 * hCaptcha works by placing two iframes in the page. One for
 * the challenge and one for the checkbox. This handler listens
 * for the challenge being shown or the checkbox being checked.
 * When either of these events happen, the handler sends a
 * message to the parent actor. The handler then disconnects
 * the mutation observer to avoid further processing.
 */
class HCaptchaHandler extends CaptchaHandler {
  #shown;
  #checked;
  #mutationObserver;

  static type = "hCaptcha";

  constructor(actor, event) {
    let params = null;
    try {
      params = new URLSearchParams(actor.document.location.hash.slice(1));
    } catch {
      // invalid URL
      super(actor, event, true);
      return;
    }

    const frameType = params.get("frame");

    super(actor, event, frameType === "challenge");

    if (frameType === "challenge") {
      this.#initChallengeHandler();
    } else if (frameType === "checkbox") {
      this.#initCheckboxHandler();
    }
  }

  static matches(document) {
    if (Cu.isInAutomation) {
      return (
        document
          .getElementById("captchaType")
          ?.getAttribute("data-captcha-type") === HCaptchaHandler.type
      );
    }

    return (
      document.location.href.startsWith(
        "https://newassets.hcaptcha.com/captcha/v1/"
      ) &&
      document.location.pathname.endsWith("/static/hcaptcha.html") &&
      !document.location.hash.includes("size=invisible") &&
      !document.location.hash.includes("frame=checkbox-invisible")
    );
  }

  #initChallengeHandler() {
    this.#shown = false;
    this.#mutationObserver = new this.actor.contentWindow.MutationObserver(
      this.#challengeMutationHandler.bind(this)
    );
    this.#mutationObserver.observe(this.actor.document.body, {
      attributes: true,
      attributeFilter: ["aria-hidden"],
    });
  }

  #challengeMutationHandler(_mutations, observer) {
    if (this.#shown) {
      return;
    }

    this.#shown = this.actor.document.body.ariaHidden !== "true";
    if (!this.#shown) {
      return;
    }

    this.updateState({
      type: HCaptchaHandler.type,
      changes: "shown",
    });
    observer.disconnect();
  }

  #initCheckboxHandler() {
    this.#checked = false;
    this.#mutationObserver = new this.actor.contentWindow.MutationObserver(
      this.#checkboxMutationHandler.bind(this)
    );
    this.#mutationObserver.observe(this.actor.document, {
      subtree: true,
      attributes: true,
      attributeFilter: ["aria-checked"],
    });
  }

  #checkboxMutationHandler(_mutations, observer) {
    if (this.#checked) {
      return;
    }

    const checkbox = this.actor.document.getElementById("checkbox");
    if (checkbox?.ariaChecked === "true") {
      this.#checked = true;
      this.updateState({
        type: HCaptchaHandler.type,
        changes: "passed",
      });
      observer.disconnect();
    }
  }

  onActorDestroy() {
    super.onActorDestroy();
    this.#mutationObserver.disconnect();
  }
}

/**
 * Handles Arkose Labs captchas.
 *
 * We listen for network requests to the
 * captcha API. When the response is received, we check for
 * the presence of the expected keys and send a message to
 * the parent actor with the state of the captcha.
 */
class ArkoseLabsHandler extends CaptchaHandler {
  static type = "arkoseLabs";

  constructor(actor) {
    super(actor);
    this.actor.sendQuery("CaptchaDetection:Init", {
      type: ArkoseLabsHandler.type,
    });
  }

  static matches(document) {
    if (Cu.isInAutomation) {
      return (
        document
          .getElementById("captchaType")
          ?.getAttribute("data-captcha-type") === ArkoseLabsHandler.type
      );
    }

    return document.location.href.startsWith(
      "https://client-api.arkoselabs.com/fc/assets/ec-game-core/game-core/"
    );
  }
}

/**
 * This actor runs in the captcha's frame. It provides information
 * about the captcha's state to the parent actor.
 */
export class CaptchaDetectionChild extends JSWindowActorChild {
  actorCreated() {
    lazy.console.debug("actorCreated");
  }

  /**
   * @constant
   * @type {Array<CaptchaHandler>}
   */
  static #handlers = [
    GoogleRecaptchaV2Handler,
    CFTurnstileHandler,
    DatadomeHandler,
    HCaptchaHandler,
    ArkoseLabsHandler,
  ];

  /**
   * @param {Event} event - The event that created the actor.
   */
  #initCaptchaHandler(event) {
    for (const handler of CaptchaDetectionChild.#handlers) {
      if (handler.matches(this.document)) {
        this.handler = new handler(this, event);
        return;
      }
    }
  }

  actorDestroy() {
    lazy.console.debug("actorDestroy()");
    this.handler?.onActorDestroy();
  }

  /**
   * @param {Event} event - The event to handle.
   */
  handleEvent(event) {
    if (
      !this.handler &&
      (event.type === "DOMContentLoaded" || event.type === "pageshow")
    ) {
      this.#initCaptchaHandler(event);
      return;
    }

    if (event.type === "pagehide") {
      this.sendAsyncMessage("Page:Hide");
    }

    this.handler?.handleEvent(event);
  }

  /**
   * @param {ReceiveMessageArgument} message - The message to handle.
   */
  receiveMessage(message) {
    if (this.handler) {
      this.handler.receiveMessage(message);
    }
  }
}

/**
 * @typedef lazy
 * @type {object}
 * @property {ConsoleInstance} console - console instance.
 */
