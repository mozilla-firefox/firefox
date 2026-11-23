/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  TranslationsFeature:
    "chrome://global/content/translations/TranslationsFeature.sys.mjs",
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
});

/**
 * Handles privileged requests and browser state for about:translations.
 */
export class AboutTranslationsParent extends JSWindowActorParent {
  #isDestroyed = false;

  /**
   * A dedicated handle to this.#observe.bind(this), which we need to register non-static
   * per-instance observers when the actor is created as well as remove when it is destroyed.
   *
   * @type {Function | null}
   *
   * @see {AboutTranslationsParent.actorCreated}
   * @see {AboutTranslationsParent.didDestroy}
   */
  #boundObserve = null;

  /**
   * Retrieves the display name for a language.
   *
   * @returns {Intl.DisplayNames}
   */
  #languageDisplayNames = null;

  /**
   * The tab container whose Split View events this actor observes.
   *
   * @type {Element | null}
   */
  #splitViewEventTarget = null;

  /**
   * The browser whose Split View membership this actor observes.
   *
   * @type {Browser | null}
   */
  #splitViewBrowser = null;

  /**
   * Reports the page's current Split View membership to the child actor.
   */
  #sendSplitViewState() {
    this.sendAsyncMessage("AboutTranslations:SplitViewStateChanged", {
      isInSplitView: Boolean(this.#getTab()?.splitview),
    });
  }

  /**
   * Gets the browser tab displaying this page.
   *
   * @returns {MozTabbrowserTab | null}
   */
  #getTab() {
    const browser = this.browsingContext.top.embedderElement;
    const gBrowser = this.browsingContext.topChromeWindow?.gBrowser;

    return browser ? (gBrowser?.getTabForBrowser(browser) ?? null) : null;
  }

  /**
   * Observes Split View membership for the supplied browser, or stops observing.
   *
   * @param {Browser | null} browser
   */
  #setSplitViewBrowser(browser) {
    const browserEvents = ["SwapDocShells", "EndSwapDocShells"];
    const tabContainerEvents = ["SplitViewTabChange", "SplitViewRemoved"];

    for (const eventName of browserEvents) {
      this.#splitViewBrowser?.removeEventListener(eventName, this);
    }
    for (const eventName of tabContainerEvents) {
      this.#splitViewEventTarget?.removeEventListener(eventName, this);
    }

    this.#splitViewBrowser = browser;
    this.#splitViewEventTarget =
      browser?.documentGlobal.gBrowser?.tabContainer ?? null;

    for (const eventName of browserEvents) {
      this.#splitViewBrowser?.addEventListener(eventName, this);
    }
    for (const eventName of tabContainerEvents) {
      this.#splitViewEventTarget?.addEventListener(eventName, this);
    }
  }

  /**
   * Reports membership changes and follows the page when its browser changes.
   *
   * @param {CustomEvent} event
   */
  handleEvent(event) {
    switch (event.type) {
      case "SwapDocShells":
        if (event.target === this.browsingContext.top.embedderElement) {
          // The destination browser is available before the docshell moves.
          this.#setSplitViewBrowser(event.detail);
        }
        break;
      case "EndSwapDocShells":
      case "SplitViewTabChange":
      case "SplitViewRemoved":
        this.#sendSplitViewState();
        break;
    }
  }

  actorCreated() {
    this.#setSplitViewBrowser(this.browsingContext.top.embedderElement);
    lazy.TranslationsParent.ensurePrefObservers();
    this.#boundObserve = this.#observe.bind(this);
    Services.obs.addObserver(
      this.#boundObserve,
      "translations:model-records-changed"
    );
    Services.obs.addObserver(
      this.#boundObserve,
      "translations:enabled-state-changed"
    );
  }

  didDestroy() {
    this.#setSplitViewBrowser(null);
    if (this.#boundObserve) {
      Services.obs.removeObserver(
        this.#boundObserve,
        "translations:model-records-changed"
      );
      Services.obs.removeObserver(
        this.#boundObserve,
        "translations:enabled-state-changed"
      );
      this.#boundObserve = null;
    }
    this.#isDestroyed = true;
  }

  /**
   * Observes notifications for about:translations updates.
   *
   * @param {nsISupports} _subject
   * @param {string} topic
   * @param {string} data
   *
   * @see {nsIObserver}
   */
  #observe(_subject, topic, data) {
    if (this.#isDestroyed) {
      return;
    }

    switch (topic) {
      case "translations:model-records-changed": {
        this.sendAsyncMessage("AboutTranslations:RebuildTranslator");
        break;
      }
      case "translations:enabled-state-changed": {
        this.sendAsyncMessage("AboutTranslations:EnabledStateChanged", {
          enabled: data === "enabled",
        });
        break;
      }
    }
  }

  async receiveMessage({ name, data }) {
    switch (name) {
      case "AboutTranslations:GetTranslationsPort": {
        if (this.#isDestroyed) {
          return undefined;
        }

        const { languagePair } = data;
        try {
          const port =
            await lazy.TranslationsParent.requestTranslationsPort(languagePair);

          // At the time of writing, you can't return a port via the `sendQuery` API,
          // so results can't just be returned. The `sendAsyncMessage` method must be
          // invoked. Additionally, in the AboutTranslationsChild, the port must
          // be transferred to the content page with `postMessage`.
          this.sendAsyncMessage(
            "AboutTranslations:SendTranslationsPort",
            {
              languagePair,
              port,
            },
            [port] // Mark the port as transferable.
          );
        } catch (error) {
          console.error(error);
        }

        return undefined;
      }
      case "AboutTranslations:GetDisplayName": {
        const { language } = data;

        if (!this.#languageDisplayNames) {
          this.#languageDisplayNames =
            lazy.TranslationsParent.createLanguageDisplayNames();
        }

        try {
          return this.#languageDisplayNames.of(language);
        } catch {
          // No display name could be retrieved.
          return "";
        }
      }
      case "AboutTranslations:GetSupportedLanguages": {
        return lazy.TranslationsParent.getSupportedLanguages();
      }
      case "AboutTranslations:IsTranslationsEngineSupported": {
        return lazy.TranslationsParent.getIsTranslationsEngineSupported();
      }
      case "AboutTranslations:GetEnabledState": {
        return lazy.TranslationsFeature.isEnabled;
      }
      case "AboutTranslations:IsEnabledStateManagedByPolicy": {
        return lazy.TranslationsFeature.isManagedByPolicy;
      }
      case "AboutTranslations:EnableTranslationsFeature": {
        await lazy.TranslationsFeature.enable();
        return undefined;
      }
      case "AboutTranslations:GetIsTranslationsPageInSplitView": {
        return Boolean(this.#getTab()?.splitview);
      }
      case "AboutTranslations:CloseTranslationsSplitViewTab": {
        const tab = this.#getTab();
        if (tab?.splitview) {
          this.browsingContext.topChromeWindow.gBrowser.removeTab(tab);
        }

        return undefined;
      }
      case "AboutTranslations:Telemetry": {
        const { telemetryFunctionName, telemetryData } = data;
        const aboutTranslationsTelemetry =
          lazy.TranslationsParent.telemetry().aboutTranslationsPage();
        const telemetryFunction =
          aboutTranslationsTelemetry[telemetryFunctionName];

        if (typeof telemetryFunction !== "function") {
          throw new Error(
            `Unknown AboutTranslationsTelemetry function name '${telemetryFunctionName}'`
          );
        }

        if (telemetryFunctionName === "onOpen") {
          telemetryFunction({
            ...telemetryData,
            openLocation: this.#getTab()?.splitview ? "split" : "full",
          });
        } else {
          telemetryFunction(telemetryData);
        }

        return undefined;
      }
      default: {
        throw new Error("Unknown AboutTranslations message: " + name);
      }
    }
  }
}
