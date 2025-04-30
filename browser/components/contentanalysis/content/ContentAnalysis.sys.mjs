/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-check

/**
 * Contains elements of the Content Analysis UI, which are integrated into
 * various browser behaviors (uploading, downloading, printing, etc) that
 * require content analysis to be done.
 * The content analysis itself is done by the clients of this script, who
 * use nsIContentAnalysis to talk to the external CA system.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
let internalContentAnalysisService = undefined;

ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  PanelMultiView: "resource:///modules/PanelMultiView.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "silentNotifications",
  "browser.contentanalysis.silent_notifications",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "agentName",
  "browser.contentanalysis.agent_name",
  "A DLP agent"
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "showBlockedResult",
  "browser.contentanalysis.show_blocked_result",
  true
);

export const ContentAnalysis = {
  _SHOW_NOTIFICATIONS: true,

  _SHOW_DIALOGS: false,

  _SLOW_DLP_NOTIFICATION_BLOCKING_TIMEOUT_MS: 250,

  _SLOW_DLP_NOTIFICATION_NONBLOCKING_TIMEOUT_MS: 3 * 1000,

  _RESULT_NOTIFICATION_TIMEOUT_MS: 5 * 60 * 1000, // 5 min

  _RESULT_NOTIFICATION_FAST_TIMEOUT_MS: 60 * 1000, // 1 min

  PROMPTID_PREFIX: "ContentAnalysisSlowDialog-",

  isInitialized: false,

  /**
   * @typedef {object} NotificationInfo - information about the busy dialog itself that is showing
   * @property {*} [close] - Method to close the native notification
   * @property {BrowsingContext} [dialogBrowsingContext] - browsing context where the
   *                                                       confirm() dialog is shown
   */

  /**
   * @typedef {object} BusyDialogInfo - information about a busy dialog that is either showing or will
   *                                    will be shown after a delay.
   * @property {string} userActionId - The userActionId of the request
   * @property {Set<string>} requestTokenSet - The set of requestTokens associated with the userActionId
   * @property {*} [timer] - Result of a setTimeout() call that can be used to cancel the showing of the busy
   *                         dialog if it has not been displayed yet.
   * @property {NotificationInfo} [notification] - Information about the busy dialog that is being shown.
   */

  /**
   * @type {Map<string, BusyDialogInfo>}
   *
   * Maps string UserActionId to info about the busy dialog.
   */
  userActionToBusyDialogMap: new Map(),

  /**
   * @typedef {object} ResourceNameOrOperationType
   * @property {string} [name] - the name of the resource
   * @property {number} [operationType] - the type of operation
   */

  /**
   * @typedef {object} RequestInfo
   * @property {CanonicalBrowsingContext} browsingContext - browsing context where the request was sent from
   * @property {ResourceNameOrOperationType} resourceNameOrOperationType - name of the operation
   */

  /**
   * @type {Map<string, RequestInfo>}
   */
  requestTokenToRequestInfo: new Map(),

  /**
   * @type {Set<string>}
   */
  warnDialogRequestTokens: new Set(),

  /**
   * The nsIContentAnalysis to use instead of lazy.gContentAnalysis. Should
   * only be used for tests.
   *
   * @type {nsIContentAnalysis?}
   */
  mockContentAnalysisForTest: undefined,

  /**
   * The nsIContentAnalysis to use. Nothing else in this file should
   * use lazy.gContentAnalysis.
   *
   * @returns {nsIContentAnalysis}
   */
  get contentAnalysis() {
    if (this.mockContentAnalysisForTest) {
      return this.mockContentAnalysisForTest;
    }
    if (!internalContentAnalysisService) {
      internalContentAnalysisService = Cc[
        "@mozilla.org/contentanalysis;1"
      ].getService(Ci.nsIContentAnalysis);
    }
    return internalContentAnalysisService;
  },

  /**
   * Sets the nsIContentAnalysis to use. Should only be used for tests.
   *
   * @param {nsIContentAnalysis?} contentAnalysis
   */
  setMockContentAnalysisForTest(contentAnalysis) {
    this.mockContentAnalysisForTest = contentAnalysis;
  },

  /**
   * Registers for various messages/events that will indicate the
   * need for communicating something to the user.
   *
   * @param {Window} window - The window to monitor
   */
  initialize(window) {
    if (!this.contentAnalysis.isActive) {
      this.uninitialize();
      return;
    }
    let doc = window.document;
    if (!this.isInitialized) {
      this.isInitialized = true;
      this.initializeObservers();

      ChromeUtils.defineLazyGetter(this, "l10n", function () {
        return new Localization(
          ["branding/brand.ftl", "toolkit/contentanalysis/contentanalysis.ftl"],
          true
        );
      });
    }

    // Do this even if initialized so the icon shows up on new windows, not just the
    // first one.
    for (let indicator of doc.getElementsByClassName(
      "content-analysis-indicator"
    )) {
      doc.l10n.setAttributes(indicator, "content-analysis-indicator-tooltip", {
        agentName: lazy.agentName,
      });
    }
    doc.documentElement.setAttribute("contentanalysisactive", "true");
  },

  async uninitialize() {
    if (this.isInitialized) {
      this.isInitialized = false;
      this.requestTokenToRequestInfo.clear();
      this.userActionToBusyDialogMap.clear();
      this.uninitializeObservers();
    }
  },

  /**
   * Register UI for CA events.
   */
  initializeObservers() {
    Services.obs.addObserver(this, "dlp-request-made");
    Services.obs.addObserver(this, "dlp-response");
    Services.obs.addObserver(this, "quit-application");
    Services.obs.addObserver(this, "quit-application-granted");
    Services.obs.addObserver(this, "quit-application-requested");
  },

  /**
   * Unregister UI for CA events.
   */
  uninitializeObservers() {
    Services.obs.removeObserver(this, "dlp-request-made");
    Services.obs.removeObserver(this, "dlp-response");
    Services.obs.removeObserver(this, "quit-application");
    Services.obs.removeObserver(this, "quit-application-granted");
    Services.obs.removeObserver(this, "quit-application-requested");
  },

  // nsIObserver
  async observe(aSubj, aTopic, _aData) {
    switch (aTopic) {
      case "quit-application-requested": {
        let pendingRequestInfos = this._getAllSlowCARequestInfos();
        let requestDescriptions = Array.from(
          pendingRequestInfos.flatMap(info =>
            info
              ? [
                  this._getResourceNameFromNameOrOperationType(
                    info.resourceNameOrOperationType
                  ),
                ]
              : []
          )
        );
        if (!requestDescriptions.length) {
          return;
        }
        let messageBody = this.l10n.formatValueSync(
          "contentanalysis-inprogress-quit-message"
        );
        messageBody = messageBody + "\n\n";
        messageBody += requestDescriptions.join("\n");
        let buttonSelected = Services.prompt.confirmEx(
          null,
          this.l10n.formatValueSync("contentanalysis-inprogress-quit-title"),
          messageBody,
          Ci.nsIPromptService.BUTTON_POS_0 *
            Ci.nsIPromptService.BUTTON_TITLE_IS_STRING +
            Ci.nsIPromptService.BUTTON_POS_1 *
              Ci.nsIPromptService.BUTTON_TITLE_CANCEL +
            Ci.nsIPromptService.BUTTON_POS_0_DEFAULT,
          this.l10n.formatValueSync(
            "contentanalysis-inprogress-quit-yesbutton"
          ),
          null,
          null,
          null,
          { value: false }
        );
        if (buttonSelected === 1) {
          // Cancel the quit operation
          aSubj.data = true;
        } else {
          // Ideally we would wait until "quit-application" to cancel outstanding
          // DLP requests, but the "DLP busy" or "DLP blocked" dialog can block the
          // main thread, thus preventing the "quit-application" from being sent,
          // which causes a shutdownhang. (bug 1899703)
          this.contentAnalysis.cancelAllRequests(true);
        }
        break;
      }
      // Note that we do this in quit-application-granted instead of quit-application
      // because otherwise we can get a shutdownhang if WARN dialogs are showing and
      // the user quits via keyboard or the hamburger menu (bug 1959966)
      case "quit-application-granted": {
        // We're quitting, so respond false to all WARN dialogs.
        let requestTokensToCancel = this.warnDialogRequestTokens;
        // Clear this first so the handler showing the dialog will know not
        // to call respondToWarnDialog() again.
        this.warnDialogRequestTokens = new Set();
        for (let warnDialogRequestToken of requestTokensToCancel) {
          this.contentAnalysis.respondToWarnDialog(
            warnDialogRequestToken,
            false
          );
        }
        break;
      }
      case "quit-application": {
        this.uninitialize();
        break;
      }
      case "dlp-request-made":
        {
          const request = aSubj.QueryInterface(Ci.nsIContentAnalysisRequest);
          if (!request) {
            console.error(
              "Showing in-browser Content Analysis notification but no request was passed"
            );
            return;
          }
          let browsingContext = request.windowGlobalParent?.browsingContext;
          if (!browsingContext) {
            throw new Error(
              "Got dlp-request-made message but couldn't find a browsingContext!"
            );
          }

          // Start timer that, when it expires,
          // presents a "slow CA check" message.
          let resourceNameOrOperationType =
            this._getResourceNameOrOperationTypeFromRequest(request, false);
          this.requestTokenToRequestInfo.set(request.requestToken, {
            browsingContext,
            resourceNameOrOperationType,
          });
          this._queueSlowCAMessage(
            request,
            resourceNameOrOperationType,
            browsingContext
          );
        }
        break;
      case "dlp-response": {
        const response = aSubj.QueryInterface(Ci.nsIContentAnalysisResponse);
        // Cancels timer or slow message UI,
        // if present, and possibly presents the CA verdict.
        if (!response) {
          throw new Error(
            "Got dlp-response message but no response object was passed"
          );
        }

        let windowAndResourceNameOrOperationType =
          this.requestTokenToRequestInfo.get(response.requestToken);
        if (!windowAndResourceNameOrOperationType) {
          // We may get multiple responses, for example, if we are blocked or
          // canceled after receiving our verdict because we were part of a
          // multipart transaction.  Just ignore that.
          console.warn(
            `Got dlp-response message with unknown token ${response.requestToken} | action: ${response.action}`
          );
          return;
        }
        this.requestTokenToRequestInfo.delete(response.requestToken);
        this._removeSlowCAMessage(response.userActionId, response.requestToken);
        const responseResult =
          response?.action ?? Ci.nsIContentAnalysisResponse.eUnspecified;
        // Don't show dialog if this is a cached response
        if (!response?.isCachedResponse) {
          await this._showCAResult(
            windowAndResourceNameOrOperationType.resourceNameOrOperationType,
            windowAndResourceNameOrOperationType.browsingContext,
            response.requestToken,
            response.userActionId,
            responseResult,
            response.isAgentResponse,
            response.cancelError
          );
        }
        break;
      }
    }
  },

  /**
   * Shows the panel that indicates that DLP is active.
   *
   * @param {Element} element The toolbarbutton the user has clicked on
   * @param {*} panelUI Maintains state for the main menu panel
   */
  async showPanel(element, panelUI) {
    element.ownerDocument.l10n.setAttributes(
      lazy.PanelMultiView.getViewNode(
        element.ownerDocument,
        "content-analysis-panel-description"
      ),
      "content-analysis-panel-text-styled",
      { agentName: lazy.agentName }
    );
    panelUI.showSubView("content-analysis-panel", element);
  },

  /**
   * Closes a busy dialog
   *
   * @param {BusyDialogInfo?} caView - the busy dialog to close
   */
  _disconnectFromView(caView) {
    if (!caView) {
      return;
    }
    if (caView.timer) {
      lazy.clearTimeout(caView.timer);
    } else if (caView.notification) {
      if (caView.notification.close) {
        // native notification
        caView.notification.close();
      } else if (caView.notification.dialogBrowsingContext) {
        // in-browser notification
        let browser =
          caView.notification.dialogBrowsingContext.top.embedderElement;
        // browser will be null if the tab was closed
        let win = browser?.ownerGlobal;
        if (win) {
          let dialogBox = win.gBrowser.getTabDialogBox(browser);
          // Just close the dialog associated with this CA request.
          dialogBox.getTabDialogManager().abortDialogs(dialog => {
            return (
              dialog.promptID == this.PROMPTID_PREFIX + caView.userActionId
            );
          });
        }
      } else {
        console.error(
          "Unexpected content analysis notification - can't close it!"
        );
      }
    }
  },

  /**
   * Shows either a dialog or native notification or both, depending on the values of
   * _SHOW_DIALOGS and _SHOW_NOTIFICATIONS.
   *
   * @param {string} aMessage - Message to show
   * @param {CanonicalBrowsingContext} aBrowsingContext - BrowsingContext to show the dialog in.
   * @param {number} aTimeout - timeout for closing the native notification. 0 indicates it is
   *                            not automatically closed.
   * @returns {NotificationInfo?} - information about the native notification, if it has been shown.
   */
  _showMessage(aMessage, aBrowsingContext, aTimeout = 0) {
    if (this._SHOW_DIALOGS) {
      Services.prompt.asyncAlert(
        aBrowsingContext,
        Ci.nsIPrompt.MODAL_TYPE_WINDOW,
        this.l10n.formatValueSync("contentanalysis-alert-title"),
        aMessage
      );
    }

    if (this._SHOW_NOTIFICATIONS) {
      let topWindow =
        aBrowsingContext.topChromeWindow ??
        aBrowsingContext.embedderWindowGlobal.browsingContext.topChromeWindow;
      const notification = new topWindow.Notification(
        this.l10n.formatValueSync("contentanalysis-notification-title"),
        { body: aMessage, silent: lazy.silentNotifications }
      );

      if (aTimeout != 0) {
        lazy.setTimeout(() => {
          notification.close();
        }, aTimeout);
      }
      return notification;
    }

    return null;
  },

  /**
   * Whether the notification should block browser interaction.
   *
   * @param {nsIContentAnalysisRequest.AnalysisType} aAnalysisType The type of DLP analysis being done.
   * @returns {boolean}
   */
  _shouldShowBlockingNotification(aAnalysisType) {
    return !(
      aAnalysisType == Ci.nsIContentAnalysisRequest.eFileDownloaded ||
      aAnalysisType == Ci.nsIContentAnalysisRequest.ePrint
    );
  },

  /**
   * This function also transforms the nameOrOperationType so we won't have to
   * look it up again.
   *
   * @param {ResourceNameOrOperationType} nameOrOperationType
   * @returns {string}
   */
  _getResourceNameFromNameOrOperationType(nameOrOperationType) {
    if (!nameOrOperationType.name) {
      let l10nId = undefined;
      switch (nameOrOperationType.operationType) {
        case Ci.nsIContentAnalysisRequest.eClipboard:
          l10nId = "contentanalysis-operationtype-clipboard";
          break;
        case Ci.nsIContentAnalysisRequest.eDroppedText:
          l10nId = "contentanalysis-operationtype-dropped-text";
          break;
        case Ci.nsIContentAnalysisRequest.eOperationPrint:
          l10nId = "contentanalysis-operationtype-print";
          break;
      }
      if (!l10nId) {
        console.error(
          "Unknown operationTypeForDisplay: " +
            nameOrOperationType.operationType
        );
        return "";
      }
      nameOrOperationType.name = this.l10n.formatValueSync(l10nId);
    }
    return nameOrOperationType.name;
  },

  /**
   * Gets a name or operation type from a request
   *
   * @param {nsIContentAnalysisRequest} aRequest The nsIContentAnalysisRequest
   * @param {boolean} aStandalone Whether the message is going to be used on its own
   *                              line. This is used to add more context to the message
   *                              if a file is being uploaded rather than just the name
   *                              of the file.
   * @returns {ResourceNameOrOperationType}
   */
  _getResourceNameOrOperationTypeFromRequest(aRequest, aStandalone) {
    if (
      aRequest.operationTypeForDisplay ==
      Ci.nsIContentAnalysisRequest.eCustomDisplayString
    ) {
      if (aStandalone) {
        return {
          name: this.l10n.formatValueSync(
            "contentanalysis-customdisplaystring-description",
            { filename: aRequest.operationDisplayString }
          ),
        };
      }
      return { name: aRequest.operationDisplayString };
    }
    return { operationType: aRequest.operationTypeForDisplay };
  },

  /**
   * Sets up an "operation is in progress" dialog to be shown after a delay,
   * unless one is already showing for this userActionId.
   *
   * @param {nsIContentAnalysisRequest} aRequest
   * @param {ResourceNameOrOperationType} aResourceNameOrOperationType
   * @param {CanonicalBrowsingContext} aBrowsingContext
   */
  _queueSlowCAMessage(
    aRequest,
    aResourceNameOrOperationType,
    aBrowsingContext
  ) {
    let entry = this.userActionToBusyDialogMap.get(aRequest.userActionId);
    if (entry) {
      // Don't show busy dialog if another request is already doing so.
      entry.requestTokenSet.add(aRequest.requestToken);
      return;
    }

    const analysisType = aRequest.analysisType;
    // For operations that block browser interaction, show the "slow content analysis"
    // dialog faster
    let slowTimeoutMs = this._shouldShowBlockingNotification(analysisType)
      ? this._SLOW_DLP_NOTIFICATION_BLOCKING_TIMEOUT_MS
      : this._SLOW_DLP_NOTIFICATION_NONBLOCKING_TIMEOUT_MS;

    entry = {
      requestTokenSet: new Set([aRequest.requestToken]),
      userActionId: aRequest.userActionId,
    };
    this.userActionToBusyDialogMap.set(aRequest.userActionId, entry);
    entry.timer = lazy.setTimeout(() => {
      entry.timer = null;
      entry.notification = this._showSlowCAMessage(
        analysisType,
        aRequest,
        this._getSlowDialogMessage(
          aResourceNameOrOperationType,
          aRequest.userActionRequestsCount
        ),
        aBrowsingContext
      );
    }, slowTimeoutMs);
  },

  /**
   * Removes the Slow CA message, if it is showing
   *
   * @param {string} aUserActionId The user action ID to remove
   * @param {string} aRequestToken The request token to remove
   */
  _removeSlowCAMessage(aUserActionId, aRequestToken) {
    let entry = this.userActionToBusyDialogMap.get(aUserActionId);
    if (!entry) {
      console.error(
        `Couldn't find slow dialog for user action ${aUserActionId}`
      );
      return;
    }
    if (!entry.requestTokenSet.delete(aRequestToken)) {
      console.warn(
        `Couldn't find request ${aRequestToken} in slow dialog object for user action ${aUserActionId}.  Shutting down?`
      );
      return;
    }
    if (entry.requestTokenSet.size) {
      // Continue showing the busy dialog since other requests are still pending.
      return;
    }
    this.userActionToBusyDialogMap.delete(aUserActionId);
    this._disconnectFromView(entry);
  },

  /**
   * Gets all the requests that are still in progress.
   *
   * @returns {IteratorObject<RequestInfo>} Information about the requests that are still in progress
   */
  _getAllSlowCARequestInfos() {
    return this.userActionToBusyDialogMap
      .values()
      .flatMap(val => val.requestTokenSet)
      .map(requestToken => this.requestTokenToRequestInfo.get(requestToken));
  },

  /**
   * Show a message to the user to indicate that a CA request is taking
   * a long time.
   *
   * @param {nsIContentAnalysisRequest.AnalysisType} aOperation The operation
   * @param {nsIContentAnalysisRequest} aRequest The request that is taking a long time
   * @param {string} aBodyMessage Message to show in the body of the alert
   * @param {CanonicalBrowsingContext} aBrowsingContext BrowsingContext to show the alert in
   */
  _showSlowCAMessage(aOperation, aRequest, aBodyMessage, aBrowsingContext) {
    if (!this._shouldShowBlockingNotification(aOperation)) {
      return this._showMessage(aBodyMessage, aBrowsingContext);
    }

    if (!aRequest) {
      throw new Error(
        "Showing in-browser Content Analysis notification but no request was passed"
      );
    }

    return this._showSlowCABlockingMessage(
      aBrowsingContext,
      aRequest.userActionId,
      aRequest.requestToken,
      aBodyMessage
    );
  },

  /**
   * Gets the dialog message to show for the Slow CA dialog.
   *
   * @param {ResourceNameOrOperationType} aResourceNameOrOperationType
   * @param {number} aNumRequests
   * @returns {string}
   */
  _getSlowDialogMessage(aResourceNameOrOperationType, aNumRequests) {
    if (aResourceNameOrOperationType.name) {
      let label =
        aNumRequests > 1
          ? "contentanalysis-slow-agent-dialog-body-file-and-more"
          : "contentanalysis-slow-agent-dialog-body-file";

      return this.l10n.formatValueSync(label, {
        agent: lazy.agentName,
        filename: aResourceNameOrOperationType.name,
        count: aNumRequests - 1,
      });
    }
    let l10nId = undefined;
    switch (aResourceNameOrOperationType.operationType) {
      case Ci.nsIContentAnalysisRequest.eClipboard:
        l10nId = "contentanalysis-slow-agent-dialog-body-clipboard";
        break;
      case Ci.nsIContentAnalysisRequest.eDroppedText:
        l10nId = "contentanalysis-slow-agent-dialog-body-dropped-text";
        break;
      case Ci.nsIContentAnalysisRequest.eOperationPrint:
        l10nId = "contentanalysis-slow-agent-dialog-body-print";
        break;
    }
    if (!l10nId) {
      console.error(
        "Unknown operationTypeForDisplay: ",
        aResourceNameOrOperationType
      );
      return "";
    }
    return this.l10n.formatValueSync(l10nId, { agent: lazy.agentName });
  },

  /**
   * Gets the dialog message to show when the request has an error.
   *
   * @param {ResourceNameOrOperationType} aResourceNameOrOperationType
   * @returns {string}
   */
  _getErrorDialogMessage(aResourceNameOrOperationType) {
    if (aResourceNameOrOperationType.name) {
      return this.l10n.formatValueSync(
        "contentanalysis-error-message-upload-file",
        { filename: aResourceNameOrOperationType.name }
      );
    }
    let l10nId = undefined;
    switch (aResourceNameOrOperationType.operationType) {
      case Ci.nsIContentAnalysisRequest.eClipboard:
        l10nId = "contentanalysis-error-message-clipboard";
        break;
      case Ci.nsIContentAnalysisRequest.eDroppedText:
        l10nId = "contentanalysis-error-message-dropped-text";
        break;
      case Ci.nsIContentAnalysisRequest.eOperationPrint:
        l10nId = "contentanalysis-error-message-print";
        break;
    }
    if (!l10nId) {
      console.error(
        "Unknown operationTypeForDisplay: ",
        aResourceNameOrOperationType
      );
      return "";
    }
    return this.l10n.formatValueSync(l10nId);
  },

  /**
   * Show the Slow CA blocking dialog.
   *
   * @param {BrowsingContext} aBrowsingContext
   * @param {string} aUserActionId
   * @param {string} aRequestToken
   * @param {string} aBodyMessage
   * @returns {NotificationInfo}
   */
  _showSlowCABlockingMessage(
    aBrowsingContext,
    aUserActionId,
    aRequestToken,
    aBodyMessage
  ) {
    // Note that TabDialogManager maintains a list of displaying dialogs, and so
    // we can pop up multiple of these and the first one will keep displaying until
    // it is closed, at which point the next one will display, etc.
    let promise = Services.prompt.asyncConfirmEx(
      aBrowsingContext,
      Ci.nsIPromptService.MODAL_TYPE_TAB,
      this.l10n.formatValueSync("contentanalysis-slow-agent-dialog-header"),
      aBodyMessage,
      Ci.nsIPromptService.BUTTON_POS_0 *
        Ci.nsIPromptService.BUTTON_TITLE_CANCEL +
        Ci.nsIPromptService.BUTTON_POS_1_DEFAULT +
        Ci.nsIPromptService.SHOW_SPINNER,
      null,
      null,
      null,
      null,
      false,
      { promptID: this.PROMPTID_PREFIX + aUserActionId }
    );
    promise
      .catch(() => {
        // need a catch clause to avoid an unhandled JS exception
        // when we programmatically close the dialog.
        // Since this only happens when we are programmatically closing
        // the dialog, no need to log the exception.
      })
      .finally(() => {
        // This is also called if the tab/window is closed while a request is
        // in progress, in which case we need to cancel all related requests.
        if (this.requestTokenToRequestInfo.delete(aRequestToken)) {
          // TODO: Is this useful?  I think no.
          this._removeSlowCAMessage(aUserActionId, aRequestToken);
        }
        this.contentAnalysis.cancelAllRequestsAssociatedWithUserAction(
          aUserActionId
        );
      });
    return {
      dialogBrowsingContext: aBrowsingContext,
    };
  },

  /**
   * Show a message to the user to indicate the result of a CA request.
   *
   * @param {ResourceNameOrOperationType} aResourceNameOrOperationType
   * @param {CanonicalBrowsingContext} aBrowsingContext
   * @param {string} aRequestToken
   * @param {string} aUserActionId
   * @param {number} aCAResult
   * @param {boolean} aIsAgentResponse
   * @param {number} aRequestCancelError
   * @returns {Promise<NotificationInfo?>} a notification object (if shown)
   */
  async _showCAResult(
    aResourceNameOrOperationType,
    aBrowsingContext,
    aRequestToken,
    aUserActionId,
    aCAResult,
    aIsAgentResponse,
    aRequestCancelError
  ) {
    let message = null;
    let timeoutMs = 0;

    switch (aCAResult) {
      case Ci.nsIContentAnalysisResponse.eAllow:
        // We don't need to show anything
        return null;
      case Ci.nsIContentAnalysisResponse.eReportOnly:
        message = await this.l10n.formatValue(
          "contentanalysis-genericresponse-message",
          {
            content: this._getResourceNameFromNameOrOperationType(
              aResourceNameOrOperationType
            ),
            response: "REPORT_ONLY",
          }
        );
        timeoutMs = this._RESULT_NOTIFICATION_FAST_TIMEOUT_MS;
        break;
      case Ci.nsIContentAnalysisResponse.eWarn: {
        let allow = false;
        try {
          this.warnDialogRequestTokens.add(aRequestToken);
          const result = await Services.prompt.asyncConfirmEx(
            aBrowsingContext,
            Ci.nsIPromptService.MODAL_TYPE_TAB,
            await this.l10n.formatValue("contentanalysis-warndialogtitle"),
            await this._warnDialogText(aResourceNameOrOperationType),
            Ci.nsIPromptService.BUTTON_POS_0 *
              Ci.nsIPromptService.BUTTON_TITLE_IS_STRING +
              Ci.nsIPromptService.BUTTON_POS_1 *
                Ci.nsIPromptService.BUTTON_TITLE_IS_STRING +
              Ci.nsIPromptService.BUTTON_POS_2_DEFAULT,
            await this.l10n.formatValue(
              "contentanalysis-warndialog-response-allow"
            ),
            await this.l10n.formatValue(
              "contentanalysis-warndialog-response-deny"
            ),
            null,
            null,
            false
          );
          allow = result.get("buttonNumClicked") === 0;
        } catch {
          // This can happen if the dialog is closed programmatically, for
          // example if the tab is moved to a new window.
          // In this case just pretend the user clicked deny, as this
          // emulates the behavior of cancelling when
          // the request is still active.
          allow = false;
        }
        // Note that the shutdown code in the "quit-application" handler
        // may have cleared out warnDialogRequestTokens and responded
        // to the request already, so don't call respondToWarnDialog()
        // if aRequestToken is not in warnDialogRequestTokens.
        if (this.warnDialogRequestTokens.delete(aRequestToken)) {
          this.contentAnalysis.respondToWarnDialog(aRequestToken, allow);
        }
        return null;
      }
      case Ci.nsIContentAnalysisResponse.eBlock: {
        if (aIsAgentResponse && !lazy.showBlockedResult) {
          // Don't show anything
          return null;
        }
        let titleId = undefined;
        let body = undefined;
        if (aResourceNameOrOperationType.name) {
          titleId = "contentanalysis-block-dialog-title-upload-file";
          body = this.l10n.formatValueSync(
            "contentanalysis-block-dialog-body-upload-file",
            { filename: aResourceNameOrOperationType.name }
          );
        } else {
          let bodyId = undefined;
          let bodyHasContent = false;
          switch (aResourceNameOrOperationType.operationType) {
            case Ci.nsIContentAnalysisRequest.eClipboard: {
              // Unlike the cases below, this can be shown when the DLP
              // agent is not available.  We use a different message for that.
              const caInfo = await this.contentAnalysis.getDiagnosticInfo();
              titleId = "contentanalysis-block-dialog-title-clipboard";
              bodyId = caInfo.connectedToAgent
                ? "contentanalysis-block-dialog-body-clipboard"
                : "contentanalysis-no-agent-connected-message-content";
              bodyHasContent = true;
              break;
            }
            case Ci.nsIContentAnalysisRequest.eDroppedText:
              titleId = "contentanalysis-block-dialog-title-dropped-text";
              bodyId = "contentanalysis-block-dialog-body-dropped-text";
              break;
            case Ci.nsIContentAnalysisRequest.eOperationPrint:
              titleId = "contentanalysis-block-dialog-title-print";
              bodyId = "contentanalysis-block-dialog-body-print";
              break;
          }
          if (!titleId || !bodyId) {
            console.error(
              "Unknown operationTypeForDisplay: ",
              aResourceNameOrOperationType
            );
            return null;
          }
          if (bodyHasContent) {
            body = this.l10n.formatValueSync(bodyId, {
              agent: lazy.agentName,
              content: "",
            });
          } else {
            body = this.l10n.formatValueSync(bodyId);
          }
        }
        let alertBrowsingContext = aBrowsingContext;
        if (aBrowsingContext.embedderElement?.getAttribute("printpreview")) {
          // If we're in a print preview dialog, things are tricky.
          // The window itself is about to close (because of the thrown NS_ERROR_CONTENT_BLOCKED),
          // so using an async call would just immediately make the dialog disappear. (bug 1899714)
          // Using a blocking version can cause a hang if the window is resizing while
          // we show the dialog. (bug 1900798)
          // So instead, try to find the browser that this print preview dialog is on top of
          // and show the dialog there.
          let printPreviewBrowser = aBrowsingContext.embedderElement;
          let win = printPreviewBrowser.ownerGlobal;
          for (let browser of win.gBrowser.browsers) {
            if (
              win.PrintUtils.getPreviewBrowser(browser)?.browserId ===
              printPreviewBrowser.browserId
            ) {
              alertBrowsingContext = browser.browsingContext;
              break;
            }
          }
        }
        await Services.prompt.asyncAlert(
          alertBrowsingContext,
          Ci.nsIPromptService.MODAL_TYPE_TAB,
          this.l10n.formatValueSync(titleId),
          body
        );
        return null;
      }
      case Ci.nsIContentAnalysisResponse.eUnspecified:
        message = await this.l10n.formatValue(
          "contentanalysis-unspecified-error-message-content",
          {
            agent: lazy.agentName,
            content: this._getErrorDialogMessage(aResourceNameOrOperationType),
          }
        );
        timeoutMs = this._RESULT_NOTIFICATION_TIMEOUT_MS;
        break;
      case Ci.nsIContentAnalysisResponse.eCanceled:
        {
          let messageId;
          switch (aRequestCancelError) {
            case Ci.nsIContentAnalysisResponse.eUserInitiated:
              console.error(
                "Got unexpected cancel response with eUserInitiated"
              );
              return null;
            case Ci.nsIContentAnalysisResponse.eOtherRequestInGroupCancelled:
              return null;
            case Ci.nsIContentAnalysisResponse.eNoAgent:
              messageId = "contentanalysis-no-agent-connected-message-content";
              break;
            case Ci.nsIContentAnalysisResponse.eInvalidAgentSignature:
              messageId =
                "contentanalysis-invalid-agent-signature-message-content";
              break;
            case Ci.nsIContentAnalysisResponse.eErrorOther:
              messageId = "contentanalysis-unspecified-error-message-content";
              break;
            case Ci.nsIContentAnalysisResponse.eShutdown:
              // we're shutting down, no need to show a dialog
              return null;
            case Ci.nsIContentAnalysisResponse.eTimeout:
              // We only show this if the default action was to block.
              messageId = "contentanalysis-timeout-block-error-message-content";
              break;
            default:
              console.error(
                "Unexpected CA cancelError value: " + aRequestCancelError
              );
              messageId = "contentanalysis-unspecified-error-message-content";
              break;
          }
          // We got an error with this request, so close any dialogs for any other request
          // with the same user action id and also remove their data so we don't show
          // any dialogs they might later try to show.
          const busyDialogInfo =
            this.userActionToBusyDialogMap.get(aUserActionId);
          if (busyDialogInfo) {
            busyDialogInfo.requestTokenSet.forEach(requestToken => {
              this.requestTokenToRequestInfo.delete(requestToken);
              this._removeSlowCAMessage(aUserActionId, requestToken);
            });
          }
          message = await this.l10n.formatValue(messageId, {
            agent: lazy.agentName,
            content: this._getErrorDialogMessage(aResourceNameOrOperationType),
            contentName: this._getResourceNameFromNameOrOperationType(
              aResourceNameOrOperationType
            ),
          });
          timeoutMs = this._RESULT_NOTIFICATION_TIMEOUT_MS;
        }
        break;
      default:
        throw new Error("Unexpected CA result value: " + aCAResult);
    }

    if (!message) {
      console.error(
        "_showCAResult did not get a message populated for result value " +
          aCAResult
      );
      return null;
    }

    return this._showMessage(message, aBrowsingContext, timeoutMs);
  },

  /**
   * Returns the correct text for warn dialog contents.
   *
   * @param {ResourceNameOrOperationType} aResourceNameOrOperationType
   */
  async _warnDialogText(aResourceNameOrOperationType) {
    const caInfo = await this.contentAnalysis.getDiagnosticInfo();
    if (caInfo.connectedToAgent) {
      return await this.l10n.formatValue("contentanalysis-warndialogtext", {
        content: this._getResourceNameFromNameOrOperationType(
          aResourceNameOrOperationType
        ),
      });
    }
    return await this.l10n.formatValue(
      "contentanalysis-no-agent-connected-message-content",
      { agent: lazy.agentName, content: "" }
    );
  },
};
