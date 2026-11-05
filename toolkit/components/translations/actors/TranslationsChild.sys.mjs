/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  TranslationsDocument:
    "chrome://global/content/translations/translations-document.sys.mjs",
  LRUCache:
    "chrome://global/content/translations/translations-document.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: "browser.translations.logLevel",
    logModule: "Translations",
  });
});

/**
 * This file is extremely sensitive to memory size and performance!
 */
export class TranslationsChild extends JSWindowActorChild {
  /**
   * @type {TranslationsDocument | null}
   */
  #translatedDoc = null;

  get translatedDoc() {
    return this.#translatedDoc;
  }

  /**
   * This cache is shared across TranslationsChild instances. This means
   * that it will be shared across multiple page loads in the same origin.
   *
   * @type {LRUCache | null}
   */
  static #translationsCache = null;

  #isDestroyed = false;

  actorCreated() {
    const isTopLevelActor = this.browsingContext === this.browsingContext?.top;
    const innerWindowId = this.contentWindow?.windowGlobalChild?.innerWindowId;

    lazy.console.debug(
      `Created ${isTopLevelActor ? "top-level" : "sub-frame"} TranslationsChild actor.`,
      { innerWindowId }
    );
  }

  didDestroy() {
    this.#isDestroyed = true;
    this.#translatedDoc?.destroy();
    this.#translatedDoc = null;
  }

  addProfilerMarker(message, startTime) {
    ChromeUtils.addProfilerMarker(
      "TranslationsChild",
      {
        innerWindowId: this.contentWindow?.windowGlobalChild.innerWindowId,
        startTime,
      },
      message
    );
  }

  async receiveMessage({ name, data }) {
    if (this.#isDestroyed) {
      return undefined;
    }

    switch (name) {
      case "Translations:FindBarOpen": {
        this.#translatedDoc?.enterContentEagerTranslationsMode();
        return undefined;
      }
      case "Translations:FindBarClose": {
        this.#translatedDoc?.enterLazyTranslationsMode();
        return undefined;
      }
      case "Translations:TranslatePage": {
        if (this.#translatedDoc?.engineStatus === "error") {
          this.#translatedDoc.destroy();
          this.#translatedDoc = null;
        }

        if (this.#translatedDoc) {
          console.error("This page was already translated.");
          return undefined;
        }

        const { isFindBarOpen, languagePair, port, subFrameSchedulerId } = data;

        if (
          !TranslationsChild.#translationsCache ||
          !TranslationsChild.#translationsCache.matches(languagePair)
        ) {
          TranslationsChild.#translationsCache = new lazy.LRUCache(
            languagePair
          );
        }

        this.#translatedDoc = new lazy.TranslationsDocument(
          this.document,
          languagePair.sourceLanguage,
          languagePair.targetLanguage,
          this.contentWindow.windowGlobalChild.innerWindowId,
          port,
          () => this.sendAsyncMessage("Translations:RequestPort"),
          () => this.sendAsyncMessage("Translations:ReportFirstVisibleChange"),
          TranslationsChild.#translationsCache,
          isFindBarOpen,
          subFrameSchedulerId
        );

        return undefined;
      }
      case "Translations:AcquirePort": {
        this.addProfilerMarker("Acquired a port, resuming translations");
        this.#translatedDoc.acquirePort(data.port);
        return undefined;
      }
      case "Translations:EngineTerminated": {
        this.#translatedDoc?.handleEngineTerminated();
        return undefined;
      }
      default:
        throw new Error("Unknown message.", name);
    }
  }
}
