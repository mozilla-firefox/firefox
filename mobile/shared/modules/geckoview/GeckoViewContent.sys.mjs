/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewModule } from "resource://gre/modules/GeckoViewModule.sys.mjs";

export class GeckoViewContent extends GeckoViewModule {
  onInit() {
    this.registerListener([
      "GeckoViewContent:ExitFullScreen",
      "GeckoView:ClearMatches",
      "GeckoView:DisplayMatches",
      "GeckoView:FindInPage",
      "GeckoView:RestoreState",
      "GeckoView:ContainsFormData",
      "GeckoView:ProcessBackPressed",
      "GeckoView:ScrollBy",
      "GeckoView:ScrollTo",
      "GeckoView:SetActive",
      "GeckoView:SetFocused",
      "GeckoView:SetPriorityHint",
      "GeckoView:UpdateInitData",
      "GeckoView:ZoomToInput",
      "GeckoView:IsPdfJs",
      "GeckoView:GetBrokenSiteReport",
      "GeckoView:GetWebCompatInfo",
      "GeckoView:SendGleanBrokenSiteReport",
      "GeckoView:SendMoreWebCompatInfo",
    ]);
  }

  onEnable() {
    this.window.addEventListener(
      "MozDOMFullscreen:Entered",
      this,
      /* capture */ true,
      /* untrusted */ false
    );
    this.window.addEventListener(
      "MozDOMFullscreen:Exited",
      this,
      /* capture */ true,
      /* untrusted */ false
    );
    this.window.addEventListener(
      "framefocusrequested",
      this,
      /* capture */ true,
      /* untrusted */ false
    );

    this.window.addEventListener("DOMWindowClose", this);
    this.window.addEventListener("pagetitlechanged", this);
    this.window.addEventListener("pageinfo", this);

    Services.obs.addObserver(this, "oop-frameloader-crashed");
    Services.obs.addObserver(this, "ipc:content-shutdown");
  }

  onDisable() {
    this.window.removeEventListener(
      "MozDOMFullscreen:Entered",
      this,
      /* capture */ true
    );
    this.window.removeEventListener(
      "MozDOMFullscreen:Exited",
      this,
      /* capture */ true
    );
    this.window.removeEventListener(
      "framefocusrequested",
      this,
      /* capture */ true
    );

    this.window.removeEventListener("DOMWindowClose", this);
    this.window.removeEventListener("pagetitlechanged", this);
    this.window.removeEventListener("pageinfo", this);

    Services.obs.removeObserver(this, "oop-frameloader-crashed");
    Services.obs.removeObserver(this, "ipc:content-shutdown");
  }

  get actor() {
    return this.getActor("GeckoViewContent");
  }

  get isPdfJs() {
    return (
      this.browser.contentPrincipal.spec === "resource://pdf.js/web/viewer.html"
    );
  }

  // Goes up the browsingContext chain and sends the message every time
  // we cross the process boundary so that every process in the chain is
  // notified.
  sendToAllChildren(aEvent, aData) {
    let { browsingContext } = this.actor;

    while (browsingContext) {
      if (!browsingContext.currentWindowGlobal) {
        break;
      }

      const currentPid = browsingContext.currentWindowGlobal.osPid;
      const parentPid = browsingContext.parent?.currentWindowGlobal.osPid;

      if (currentPid != parentPid) {
        const actor =
          browsingContext.currentWindowGlobal.getActor("GeckoViewContent");
        actor.sendAsyncMessage(aEvent, aData);
      }

      browsingContext = browsingContext.parent;
    }
  }

  #sendEnterDOMFullscreenEvent(aRequestOrigin) {
    // Track the actors that are involved in the fullscreen request. And we will
    // use them to send the exit message when the fullscreen is exited.
    this._fullscreenRequest = { actors: [] };

    let currentBC = aRequestOrigin.browsingContext;
    let currentPid = currentBC.currentWindowGlobal.osPid;
    let parentBC = currentBC.parent;

    while (parentBC) {
      if (!parentBC.currentWindowGlobal) {
        break;
      }

      const parentPid = parentBC.currentWindowGlobal.osPid;
      if (currentPid != parentPid) {
        const actor = parentBC.currentWindowGlobal.getActor("GeckoViewContent");
        actor.sendAsyncMessage("GeckoView:DOMFullscreenEntered", {
          remoteFrameBC: currentBC,
        });
        this._fullscreenRequest.actors.push(actor);
        currentPid = parentPid;
      }

      currentBC = parentBC;
      parentBC = parentBC.parent;
    }

    const actor =
      aRequestOrigin.browsingContext.currentWindowGlobal.getActor(
        "GeckoViewContent"
      );
    actor.sendAsyncMessage("GeckoView:DOMFullscreenEntered", {});
    this._fullscreenRequest.actors.push(actor);
  }

  #sendExitDOMFullScreenEvent() {
    if (!this._fullscreenRequest) {
      return;
    }

    for (const actor of this._fullscreenRequest.actors) {
      if (
        !actor.hasBeenDestroyed() &&
        actor.windowContext &&
        !actor.windowContext.isInBFCache
      ) {
        actor.sendAsyncMessage("GeckoView:DOMFullscreenExited", {});
      }
    }
    delete this._fullscreenRequest;
  }

  // Bundle event handler.
  onEvent(aEvent, aData, aCallback) {
    debug`onEvent: event=${aEvent}, data=${aData}`;

    switch (aEvent) {
      case "GeckoViewContent:ExitFullScreen":
        this.browser.ownerDocument.exitFullscreen();
        break;
      case "GeckoView:ClearMatches": {
        if (!this.isPdfJs) {
          this._clearMatches();
        }
        break;
      }
      case "GeckoView:DisplayMatches": {
        if (!this.isPdfJs) {
          this._displayMatches(aData);
        }
        break;
      }
      case "GeckoView:FindInPage": {
        if (!this.isPdfJs) {
          this._findInPage(aData, aCallback);
        }
        break;
      }
      case "GeckoView:ZoomToInput": {
        const sendZoomToFocusedInputMessage = function () {
          // For ZoomToInput we just need to send the message to the current focused one.
          const actor =
            Services.focus.focusedContentBrowsingContext.currentWindowGlobal.getActor(
              "GeckoViewContent"
            );

          actor.sendAsyncMessage(aEvent, aData);
        };

        const { force } = aData;
        let gotResize = false;
        const onResize = () => {
          gotResize = true;
          if (this.window.windowUtils.isMozAfterPaintPending) {
            this.window.addEventListener(
              "MozAfterPaint",
              () => sendZoomToFocusedInputMessage(),
              { capture: true, once: true }
            );
          } else {
            sendZoomToFocusedInputMessage();
          }
        };

        this.window.addEventListener("resize", onResize, { capture: true });

        // When the keyboard is displayed, we can get one resize event,
        // multiple resize events, or none at all. Try to handle all these
        // cases by allowing resizing within a set interval, and still zoom to
        // input if there is no resize event at the end of the interval.
        this.window.setTimeout(() => {
          this.window.removeEventListener("resize", onResize, {
            capture: true,
          });
          if (!gotResize && force) {
            onResize();
          }
        }, 500);
        break;
      }
      case "GeckoView:ScrollBy":
        // Unclear if that actually works with oop iframes?
        this.sendToAllChildren(aEvent, aData);
        break;
      case "GeckoView:ScrollTo":
        // Unclear if that actually works with oop iframes?
        this.sendToAllChildren(aEvent, aData);
        break;
      case "GeckoView:UpdateInitData":
        this.sendToAllChildren(aEvent, aData);
        break;
      case "GeckoView:SetActive":
        this.browser.docShellIsActive = !!aData.active;
        break;
      case "GeckoView:SetFocused":
        if (aData.focused) {
          this.browser.focus();
          this.browser.setAttribute("primary", "true");
        } else {
          this.browser.removeAttribute("primary");
          this.browser.blur();
        }
        break;
      case "GeckoView:SetPriorityHint":
        if (this.browser.isRemoteBrowser) {
          const remoteTab = this.browser.frameLoader?.remoteTab;
          if (remoteTab) {
            remoteTab.priorityHint = aData.priorityHint;
          }
        }
        break;
      case "GeckoView:RestoreState":
        this.actor.restoreState(aData);
        break;
      case "GeckoView:ContainsFormData":
        this._containsFormData(aCallback);
        break;
      case "GeckoView:GetBrokenSiteReport":
        this._getBrokenSiteReport(aCallback);
        break;
      case "GeckoView:GetWebCompatInfo":
        this._getWebCompatInfo(aCallback);
        break;
      case "GeckoView:SendGleanBrokenSiteReport":
        this._sendGleanBrokenSiteReport(aData, aCallback);
        break;
      case "GeckoView:SendMoreWebCompatInfo":
        this._sendMoreWebCompatInfo(aData, aCallback);
        break;
      case "GeckoView:IsPdfJs":
        aCallback.onSuccess(this.isPdfJs);
        break;
      case "GeckoView:ProcessBackPressed":
        this._processBackPressed(aCallback);
        break;
    }
  }

  // DOM event handler
  handleEvent(aEvent) {
    debug`handleEvent: ${aEvent.type}`;

    switch (aEvent.type) {
      case "framefocusrequested":
        if (this.browser != aEvent.target) {
          return;
        }
        if (this.browser.hasAttribute("primary")) {
          return;
        }
        this.eventDispatcher.sendRequest("GeckoView:FocusRequest");
        aEvent.preventDefault();
        break;
      case "MozDOMFullscreen:Entered":
        if (this.browser == aEvent.target) {
          const chromeWindow = this.browser.documentGlobal;
          const requestOrigin =
            chromeWindow.browsingContext?.fullscreenRequestOrigin?.get();
          if (!requestOrigin) {
            chromeWindow.document.exitFullscreen();
            return;
          }

          // Remote browser; dispatch to content process.
          this.#sendEnterDOMFullscreenEvent(requestOrigin);
        }
        break;
      case "MozDOMFullscreen:Exited":
        this.#sendExitDOMFullScreenEvent();
        break;
      case "pagetitlechanged":
        this.eventDispatcher.sendRequest("GeckoView:PageTitleChanged", {
          title: this.browser.contentTitle,
        });
        break;
      case "DOMWindowClose":
        // We need this because we want to allow the app
        // to close the window itself. If we don't preventDefault()
        // here Gecko will close it immediately.
        aEvent.preventDefault();

        this.eventDispatcher.sendRequest("GeckoView:DOMWindowClose");
        break;
      case "pageinfo":
        if (aEvent.detail.previewImageURL) {
          this.eventDispatcher.sendRequest("GeckoView:PreviewImage", {
            previewImageUrl: aEvent.detail.previewImageURL,
          });
        }
        break;
    }
  }

  // nsIObserver event handler
  observe(aSubject, aTopic) {
    debug`observe: ${aTopic}`;
    this._contentCrashed = false;
    const browser = aSubject.ownerElement;

    switch (aTopic) {
      case "oop-frameloader-crashed": {
        if (!browser || browser != this.browser) {
          return;
        }
        this.window.setTimeout(() => {
          if (this._contentCrashed) {
            this.eventDispatcher.sendRequest("GeckoView:ContentCrash");
          } else {
            this.eventDispatcher.sendRequest("GeckoView:ContentKill");
          }
        }, 250);
        break;
      }
      case "ipc:content-shutdown": {
        aSubject.QueryInterface(Ci.nsIPropertyBag2);
        if (aSubject.get("dumpID")) {
          if (
            browser &&
            aSubject.get("childID") != browser.frameLoader.childID
          ) {
            return;
          }
          this._contentCrashed = true;
        }
        break;
      }
    }
  }

  async _getBrokenSiteReport(aCallback) {
    if (
      Cu.isInAutomation &&
      Services.prefs.getBoolPref(
        "browser.webcompat.geckoview.enableAllTestMocks",
        false
      )
    ) {
      const mockResult = {
        devicePixelRatio: 2.5,
        antitracking: { hasTrackingContentBlocked: false },
      };
      aCallback.onSuccess(JSON.stringify(mockResult));
      return;
    }
    try {
      const actor =
        this.browser.browsingContext.currentWindowGlobal.getActor(
          "ReportBrokenSite"
        );
      const info = await actor.getBrokenSiteReport();

      // Stringify to convert potential non-ASCII
      // characters in the returned web compat info map.
      aCallback.onSuccess(JSON.stringify(info));
    } catch (error) {
      aCallback.onError(`Cannot get broken site report, error: ${error}`);
    }
  }

  async _getWebCompatInfo(aCallback) {
    if (
      Cu.isInAutomation &&
      Services.prefs.getBoolPref(
        "browser.webcompat.geckoview.enableAllTestMocks",
        false
      )
    ) {
      const mockResult = {
        devicePixelRatio: 2.5,
        antitracking: { hasTrackingContentBlocked: false },
      };
      aCallback.onSuccess(JSON.stringify(mockResult));
      return;
    }
    try {
      const actor =
        this.browser.browsingContext.currentWindowGlobal.getActor(
          "ReportBrokenSite"
        );
      const info = await actor.sendQuery("GetWebCompatInfo");

      // Stringify to convert potential non-ASCII
      // characters in the returned web compat info map.
      aCallback.onSuccess(JSON.stringify(info));
    } catch (error) {
      aCallback.onError(`Cannot get web compat info, error: ${error}`);
    }
  }

  async _sendGleanBrokenSiteReport(aData, aCallback) {
    try {
      const actor =
        this.browser.browsingContext.currentWindowGlobal.getActor(
          "ReportBrokenSite"
        );

      actor.sendBrokenSiteReport(aData);
      aCallback.onSuccess();
    } catch (error) {
      aCallback.onError(
        `Cannot send broken site report via Glean, error: ${error}`
      );
    }
  }

  async _sendMoreWebCompatInfo(aData, aCallback) {
    if (
      Cu.isInAutomation &&
      Services.prefs.getBoolPref(
        "browser.webcompat.geckoview.enableAllTestMocks",
        false
      )
    ) {
      aCallback.onSuccess();
      return;
    }
    let infoObj = JSON.parse(aData.info);
    try {
      const actor =
        this.browser.browsingContext.currentWindowGlobal.getActor(
          "ReportBrokenSite"
        );

      await actor.sendQuery("SendDataToWebcompatCom", infoObj);
      aCallback.onSuccess();
    } catch (error) {
      aCallback.onError(`Cannot send more web compat info, error: ${error}`);
    }
  }

  async _containsFormData(aCallback) {
    aCallback.onSuccess(await this.actor.containsFormData());
  }

  _findInPage(aData, aCallback) {
    debug`findInPage: data=${aData} callback=${aCallback && "non-null"}`;

    let finder;
    try {
      finder = this.browser.finder;
    } catch (e) {
      if (aCallback) {
        aCallback.onError(`No finder: ${e}`);
      }
      return;
    }

    if (this._finderListener) {
      finder.removeResultListener(this._finderListener);
    }

    this._finderListener = {
      response: {
        found: false,
        wrapped: false,
        current: 0,
        total: -1,
        searchString: aData.searchString || finder.searchString,
        linkURL: null,
        clientRect: null,
        flags: {
          backwards: !!aData.backwards,
          linksOnly: !!aData.linksOnly,
          matchCase: !!aData.matchCase,
          wholeWord: !!aData.wholeWord,
        },
      },

      onFindResult(aOptions) {
        if (!aCallback || aOptions.searchString !== aData.searchString) {
          // Result from a previous search.
          return;
        }

        Object.assign(this.response, {
          found: aOptions.result !== Ci.nsITypeAheadFind.FIND_NOTFOUND,
          wrapped: aOptions.result !== Ci.nsITypeAheadFind.FIND_FOUND,
          linkURL: aOptions.linkURL,
          clientRect: aOptions.rect && {
            left: aOptions.rect.left,
            top: aOptions.rect.top,
            right: aOptions.rect.right,
            bottom: aOptions.rect.bottom,
          },
          flags: {
            backwards: aOptions.findBackwards,
            linksOnly: aOptions.linksOnly,
            matchCase: this.response.flags.matchCase,
            wholeWord: this.response.flags.wholeWord,
          },
        });

        if (!this.response.found) {
          this.response.current = 0;
          this.response.total = 0;
        }

        // Only send response if we have a count.
        if (!this.response.found || this.response.current !== 0) {
          debug`onFindResult: ${this.response}`;
          aCallback.onSuccess(this.response);
          aCallback = undefined;
        }
      },

      onMatchesCountResult(aResult) {
        if (!aCallback || finder.searchString !== aData.searchString) {
          // Result from a previous search.
          return;
        }

        Object.assign(this.response, {
          current: aResult.current,
          total: aResult.total,
        });

        // Only send response if we have a result. `found` and `wrapped` are
        // both false only when we haven't received a result yet.
        if (this.response.found || this.response.wrapped) {
          debug`onMatchesCountResult: ${this.response}`;
          aCallback.onSuccess(this.response);
          aCallback = undefined;
        }
      },

      onCurrentSelection() {},

      onHighlightFinished() {},
    };

    finder.caseSensitive = !!aData.matchCase;
    finder.entireWord = !!aData.wholeWord;
    finder.matchDiacritics = !!aData.matchDiacritics;
    finder.addResultListener(this._finderListener);

    const drawOutline =
      this._matchDisplayOptions && !!this._matchDisplayOptions.drawOutline;

    if (!aData.searchString || aData.searchString === finder.searchString) {
      // Search again.
      aData.searchString = finder.searchString;
      finder.findAgain(
        aData.searchString,
        !!aData.backwards,
        !!aData.linksOnly,
        drawOutline
      );
    } else {
      finder.fastFind(aData.searchString, !!aData.linksOnly, drawOutline);
    }
  }

  _clearMatches() {
    debug`clearMatches`;

    let finder;
    try {
      finder = this.browser.finder;
    } catch (e) {
      return;
    }

    finder.removeSelection();
    finder.highlight(false);

    if (this._finderListener) {
      finder.removeResultListener(this._finderListener);
      this._finderListener = null;
    }
  }

  _displayMatches(aData) {
    debug`displayMatches: data=${aData}`;

    let finder;
    try {
      finder = this.browser.finder;
    } catch (e) {
      return;
    }

    this._matchDisplayOptions = aData;
    finder.onModalHighlightChange(!!aData.dimPage);
    finder.onHighlightAllChange(!!aData.highlightAll);

    if (!aData.highlightAll && !aData.dimPage) {
      finder.highlight(false);
      return;
    }

    if (!this._finderListener || !finder.searchString) {
      return;
    }
    const linksOnly = this._finderListener.response.linksOnly;
    finder.highlight(true, finder.searchString, linksOnly, !!aData.drawOutline);
  }

  _processBackPressed(aCallback) {
    if (!this.browser?.hasActiveCloseWatcher) {
      aCallback.onSuccess(false);
      return;
    }
    this.browser.processCloseRequest();
    aCallback.onSuccess(true);
  }
}

const { debug, warn } = GeckoViewContent.initLogging("GeckoViewContent");
