/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  capture: "chrome://remote/content/shared/Capture.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  getSeenNodesForBrowsingContext:
    "chrome://remote/content/shared/webdriver/Session.sys.mjs",
  json: "chrome://remote/content/marionette/json.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

// Because Marionette supports a single session only we store its id
// globally so that the parent actor can access it.
let webDriverSessionId = null;

export class MarionetteCommandsParent extends JSWindowActorParent {
  #deferredDialogOpened;

  actorCreated() {
    this.#deferredDialogOpened = null;
  }

  assertInViewPort(target, _context) {
    return this.sendQuery("MarionetteCommandsParent:_assertInViewPort", {
      target,
    });
  }

  dispatchEvent(eventName, details) {
    return this.sendQuery("MarionetteCommandsParent:_dispatchEvent", {
      eventName,
      details,
    });
  }

  finalizeAction() {
    return this.sendQuery("MarionetteCommandsParent:_finalizeAction");
  }

  getClientRects(webEl, _context) {
    return this.sendQuery("MarionetteCommandsParent:_getClientRects", {
      elem: webEl,
    });
  }

  getInViewCentrePoint(rect, _context) {
    return this.sendQuery("MarionetteCommandsParent:_getInViewCentrePoint", {
      rect,
    });
  }

  async sendQuery(name, serializedValue) {
    const seenNodes = lazy.getSeenNodesForBrowsingContext(
      webDriverSessionId,
      this.manager.browsingContext
    );

    // return early if a dialog is opened
    this.#deferredDialogOpened = Promise.withResolvers();
    let {
      error,
      isWebDriverError,
      seenNodeIds,
      serializedValue: serializedResult,
      hasSerializedWindows,
    } = await Promise.race([
      super.sendQuery(name, serializedValue),
      this.#deferredDialogOpened.promise,
    ]).finally(() => {
      this.#deferredDialogOpened = null;
    });

    if (error) {
      if (isWebDriverError) {
        // If it's a WebDriver error we need to deserialize it.
        error = lazy.error.WebDriverError.fromJSON(error);
      }

      this.#handleError(error, seenNodes);
    }

    // Update seen nodes for serialized element and shadow root nodes.
    seenNodeIds?.forEach(nodeId => seenNodes.add(nodeId));

    if (hasSerializedWindows) {
      // The serialized data contains WebWindow references that need to be
      // converted to unique identifiers.
      serializedResult = lazy.json.mapToNavigableIds(serializedResult);
    }

    return serializedResult;
  }

  /**
   * Handle an error and replace error type if necessary.
   *
   * @param {Error} error
   *     The error to handle.
   * @param {Set<string>} seenNodes
   *     List of node ids already seen in this navigable.
   *
   * @throws {Error}
   *     The original or replaced error.
   */
  #handleError(error, seenNodes) {
    // If an element hasn't been found during deserialization check if it
    // may be a stale reference.
    if (
      error instanceof lazy.error.NoSuchElementError &&
      error.data.elementId !== undefined &&
      seenNodes.has(error.data.elementId)
    ) {
      throw new lazy.error.StaleElementReferenceError(error);
    }

    // If a shadow root hasn't been found during deserialization check if it
    // may be a detached reference.
    if (
      error instanceof lazy.error.NoSuchShadowRootError &&
      error.data.shadowId !== undefined &&
      seenNodes.has(error.data.shadowId)
    ) {
      throw new lazy.error.DetachedShadowRootError(error);
    }

    throw error;
  }

  notifyDialogOpened() {
    if (this.#deferredDialogOpened) {
      this.#deferredDialogOpened.resolve({ data: null });
    }
  }

  // Proxying methods for WebDriver commands

  clearElement(webEl) {
    return this.sendQuery("MarionetteCommandsParent:clearElement", {
      elem: webEl,
    });
  }

  clickElement(webEl, capabilities) {
    return this.sendQuery("MarionetteCommandsParent:clickElement", {
      elem: webEl,
      capabilities: capabilities.toJSON(),
    });
  }

  async executeScript(script, args, opts) {
    return this.sendQuery("MarionetteCommandsParent:executeScript", {
      script,
      args: lazy.json.mapFromNavigableIds(args),
      opts,
    });
  }

  findElement(strategy, selector, opts) {
    return this.sendQuery("MarionetteCommandsParent:findElement", {
      strategy,
      selector,
      opts,
    });
  }

  findElements(strategy, selector, opts) {
    return this.sendQuery("MarionetteCommandsParent:findElements", {
      strategy,
      selector,
      opts,
    });
  }

  async getShadowRoot(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getShadowRoot", {
      elem: webEl,
    });
  }

  async getActiveElement() {
    return this.sendQuery("MarionetteCommandsParent:getActiveElement");
  }

  async getComputedLabel(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getComputedLabel", {
      elem: webEl,
    });
  }

  async getComputedRole(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getComputedRole", {
      elem: webEl,
    });
  }

  async getElementAttribute(webEl, name) {
    return this.sendQuery("MarionetteCommandsParent:getElementAttribute", {
      elem: webEl,
      name,
    });
  }

  async getElementProperty(webEl, name) {
    return this.sendQuery("MarionetteCommandsParent:getElementProperty", {
      elem: webEl,
      name,
    });
  }

  async getElementRect(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getElementRect", {
      elem: webEl,
    });
  }

  async getElementTagName(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getElementTagName", {
      elem: webEl,
    });
  }

  async getElementText(webEl) {
    return this.sendQuery("MarionetteCommandsParent:getElementText", {
      elem: webEl,
    });
  }

  async getElementValueOfCssProperty(webEl, name) {
    return this.sendQuery(
      "MarionetteCommandsParent:getElementValueOfCssProperty",
      {
        elem: webEl,
        name,
      }
    );
  }

  async getPageSource() {
    return this.sendQuery("MarionetteCommandsParent:getPageSource");
  }

  async isElementDisplayed(webEl, capabilities) {
    return this.sendQuery("MarionetteCommandsParent:isElementDisplayed", {
      capabilities: capabilities.toJSON(),
      elem: webEl,
    });
  }

  async isElementEnabled(webEl, capabilities) {
    return this.sendQuery("MarionetteCommandsParent:isElementEnabled", {
      capabilities: capabilities.toJSON(),
      elem: webEl,
    });
  }

  async isElementSelected(webEl, capabilities) {
    return this.sendQuery("MarionetteCommandsParent:isElementSelected", {
      capabilities: capabilities.toJSON(),
      elem: webEl,
    });
  }

  async sendKeysToElement(webEl, text, capabilities) {
    return this.sendQuery("MarionetteCommandsParent:sendKeysToElement", {
      capabilities: capabilities.toJSON(),
      elem: webEl,
      text,
    });
  }

  async switchToFrame(id) {
    const { browsingContextId } = await this.sendQuery(
      "MarionetteCommandsParent:switchToFrame",
      { id }
    );

    return {
      browsingContext: BrowsingContext.get(browsingContextId),
    };
  }

  async switchToParentFrame() {
    const { browsingContextId } = await this.sendQuery(
      "MarionetteCommandsParent:switchToParentFrame"
    );

    return {
      browsingContext: BrowsingContext.get(browsingContextId),
    };
  }

  async takeScreenshot(webEl, format, full, scroll) {
    const rect = await this.sendQuery(
      "MarionetteCommandsParent:getScreenshotRect",
      {
        elem: webEl,
        full,
        scroll,
      }
    );

    // If no element has been specified use the top-level browsing context.
    // Otherwise use the browsing context from the currently selected frame.
    const browsingContext = webEl
      ? this.browsingContext
      : this.browsingContext.top;

    let canvas = await lazy.capture.canvas(
      browsingContext.topChromeWindow,
      browsingContext,
      rect.x,
      rect.y,
      rect.width,
      rect.height
    );

    switch (format) {
      case lazy.capture.Format.Hash:
        return lazy.capture.toHash(canvas);

      case lazy.capture.Format.Base64:
        return lazy.capture.toBase64(canvas, "image/png");

      default:
        throw new TypeError(`Invalid capture format: ${format}`);
    }
  }
}

/**
 * Proxy that will dynamically create MarionetteCommands actors for a dynamically
 * provided browsing context until the method can be fully executed by the
 * JSWindowActor pair.
 *
 * @param {function(): BrowsingContext} browsingContextFn
 *     A function that returns the reference to the browsing context for which
 *     the query should run.
 */
export function getMarionetteCommandsActorProxy(browsingContextFn) {
  const MAX_ATTEMPTS = 10;

  /**
   * Methods which modify the content page cannot be retried safely.
   * See Bug 1673345.
   */
  const NO_RETRY_METHODS = [
    "clickElement",
    "executeScript",
    "sendKeysToElement",
  ];

  return new Proxy(
    {},
    {
      get(target, methodName) {
        return async (...args) => {
          let attempts = 0;
          // eslint-disable-next-line no-constant-condition
          while (true) {
            let browsingContext = browsingContextFn();

            // If a top-level browsing context was replaced and retrying is allowed,
            // retrieve the new one for the current browser.
            if (
              browsingContext?.isReplaced &&
              browsingContext.top === browsingContext &&
              !NO_RETRY_METHODS.includes(methodName)
            ) {
              browsingContext = BrowsingContext.getCurrentTopByBrowserId(
                browsingContext.browserId
              );
            }

            if (!browsingContext || browsingContext.isDiscarded) {
              throw new lazy.error.NoSuchWindowError(
                `BrowsingContext does no longer exist`
              );
            }

            try {
              const actor =
                browsingContext.currentWindowGlobal.getActor(
                  "MarionetteCommands"
                );

              const result = await actor[methodName](...args);
              return result;
            } catch (e) {
              if (!["AbortError", "InactiveActor"].includes(e.name)) {
                // Only retry when the JSWindowActor pair gets destroyed, or
                // gets inactive eg. when the page is moved into bfcache.
                throw e;
              }

              if (NO_RETRY_METHODS.includes(methodName)) {
                lazy.logger.trace(
                  `[${browsingContext.id}] Querying "${methodName}"` +
                    ` failed with ${e.name}, returning "null" as fallback`
                );
                return null;
              }

              if (++attempts > MAX_ATTEMPTS) {
                lazy.logger.trace(
                  `[${browsingContext.id}] Querying "${methodName}"` +
                    ` reached the limit of retry attempts (${MAX_ATTEMPTS})`
                );
                throw e;
              }

              lazy.logger.trace(
                `[${browsingContext.id}] Retrying "${methodName}"` +
                  `, attempt: ${attempts}`
              );
              await new Promise(resolve =>
                Services.tm.dispatchToMainThread(resolve)
              );
            }
          }
        };
      },
    }
  );
}

/**
 * Register the MarionetteCommands actor that holds all the commands.
 *
 * @param {string} sessionId
 *     The id of the current WebDriver session.
 */
export function registerCommandsActor(sessionId) {
  try {
    ChromeUtils.registerWindowActor("MarionetteCommands", {
      kind: "JSWindowActor",
      parent: {
        esModuleURI:
          "chrome://remote/content/marionette/actors/MarionetteCommandsParent.sys.mjs",
      },
      child: {
        esModuleURI:
          "chrome://remote/content/marionette/actors/MarionetteCommandsChild.sys.mjs",
      },

      allFrames: true,
      includeChrome: true,
    });
  } catch (e) {
    if (e.name === "NotSupportedError") {
      lazy.logger.warn(`MarionetteCommands actor is already registered!`);
    } else {
      throw e;
    }
  }

  webDriverSessionId = sessionId;
}

export function unregisterCommandsActor() {
  webDriverSessionId = null;

  ChromeUtils.unregisterWindowActor("MarionetteCommands");
}
