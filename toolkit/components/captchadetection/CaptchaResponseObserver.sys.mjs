/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/** @type {lazy} */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "CaptchaDetectionResponseObserver",
    maxLogLevelPref: "captchadetection.loglevel",
  });
});

ChromeUtils.defineESModuleGetters(lazy, {
  CommonUtils: "resource://services-common/utils.sys.mjs",
});

/**
 * This class is responsible for observing HTTP responses
 *
 * Do note that it won't work for cached responses.
 * We don't need to handle cached responses because
 * we are only interested in API responses from
 * captcha services.
 */
export class CaptchaResponseObserver {
  /**
   * @param {function(nsIHttpChannel): boolean} shouldIntercept - A function that returns true if the response should be intercepted.
   * @param {function(nsIHttpChannel, nsresult, string): void} onResponseBody - A function that is called when the response body is available.
   */
  constructor(shouldIntercept, onResponseBody) {
    /** @type {Map<nsIChannel, nsIPipe>} */
    this.requestToTeePipe = new WeakMap();
    this.shouldIntercept = shouldIntercept;
    this.onResponseBody = onResponseBody;
  }

  register() {
    Services.obs.addObserver(this, "http-on-examine-response");
  }

  unregister() {
    Services.obs.removeObserver(this, "http-on-examine-response");
  }

  observe(channel) {
    if (!(channel instanceof Ci.nsIHttpChannel)) {
      return;
    }

    channel.QueryInterface(Ci.nsITraceableChannel);

    if (!this.shouldIntercept(channel)) {
      return;
    }

    const pipe = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
    pipe.init(false, false, 0, 0xffffffff, null);

    const tee = Cc["@mozilla.org/network/stream-listener-tee;1"].createInstance(
      Ci.nsIStreamListenerTee
    );
    const originalListener = channel.setNewListener(tee);
    tee.init(originalListener, pipe.outputStream, this);

    this.requestToTeePipe.set(channel, pipe);
  }

  onStartRequest() {}

  /**
   * @param {nsIHttpChannel} channel - The channel.
   * @param {nsIXPCComponents_Values} statusCode - The status code, not to be confused with the HTTP status code.
   */
  async onStopRequest(channel, statusCode) {
    const pipe = this.requestToTeePipe.get(channel);

    pipe.outputStream.close();
    this.requestToTeePipe.delete(channel);

    let length = 0;
    try {
      length = pipe.inputStream.available();
    } catch (e) {
      lazy.console.error("Error reading response body", e);
      return;
    }

    let responseBody = "";
    if (length) {
      const sin = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
        Ci.nsIScriptableInputStream
      );
      sin.init(pipe.inputStream);
      responseBody = sin.readBytes(length);
      sin.close();
    }

    if (
      channel instanceof Ci.nsIEncodedChannel &&
      channel.contentEncodings &&
      !channel.applyConversion &&
      !channel.hasContentDecompressed
    ) {
      const encodingHeader = channel.getResponseHeader("Content-Encoding");
      const encodings = encodingHeader.split(/\s*\t*,\s*\t*/);
      for (const encoding of encodings) {
        responseBody = lazy.CommonUtils.convertString(
          responseBody,
          encoding,
          "uncompressed"
        );
      }
    }

    this.onResponseBody(channel, statusCode, responseBody);
  }

  QueryInterface = ChromeUtils.generateQI([
    Ci.nsIObserver,
    Ci.nsIRequestObserver,
  ]);
}

/**
 * @typedef lazy
 * @type {object}
 * @property {ConsoleInstance} console - console instance.
 * @property {typeof import("services/common/utils.sys.mjs").CommonUtils} CommonUtils - CommonUtils instance.
 */
