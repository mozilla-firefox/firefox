/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Contains helper methods for JS code that need to call into
 * Content Analysis. Note that most JS code will not need to explicitly
 * use this - this is only for edge cases that the existing C++ code
 * does not handle, such as pasting into a prompt() or pasting into
 * the GenAI chatbot shortcut menu.
 */
// @ts-check

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "clipboardCopyEnabled",
  "browser.contentanalysis.interception_point.clipboard_copy.enabled",
  false
);

export const ContentAnalysisUtils = {
  /**
   * Builds an nsIContentAnalysisRequest from the given parameters.
   *
   * @param {object} data The core properties of the request.
   * @param {nsIContentAnalysisRequest.AnalysisType} data.analysisType The type of analysis being requested.
   * @param {nsIContentAnalysisRequest.OperationType} data.operationTypeForDisplay The operation to display to the user.
   * @param {nsIContentAnalysisRequest.Reason} data.reason The reason the request was created.
   * @param {nsIURI} data.url An nsIURI that indicates where the content would be sent to.
   * @param {WindowGlobalParent} data.windowGlobalParent The WindowGlobalParent associated with the request.
   * @param {object} [extraProps] Additional properties to set on the returned request.
   * @returns {nsIContentAnalysisRequest}
   */
  createContentAnalysisRequest(
    { analysisType, operationTypeForDisplay, reason, url, windowGlobalParent },
    extraProps
  ) {
    return /** @type {nsIContentAnalysisRequest} */ ({
      QueryInterface: ChromeUtils.generateQI(["nsIContentAnalysisRequest"]),
      analysisType,
      dataTransfer: undefined,
      email: undefined,
      fileNameForDisplay: undefined,
      filePath: undefined,
      operationTypeForDisplay,
      printerName: undefined,
      reason,
      requestToken: undefined,
      sha256Digest: undefined,
      sourceWindowGlobal: undefined,
      testOnlyIgnoreCanceledAndAlwaysSubmitToAgent: false,
      textContent: undefined,
      timeoutMultiplier: 1,
      transferable: undefined,
      url,
      userActionId: "",
      userActionRequestsCount: 0,
      windowGlobalParent,
      getPrintData: () => {
        return [];
      },
      resources: [],
      ...extraProps,
    });
  },

  /**
   * Sets up Content Analysis to monitor clipboard pastes and drag-and-drop
   * and send the text on to Content Analysis for approval. This method
   * will check if Content Analysis is active and if not it will return early.
   *
   * @param {HTMLInputElement} textElement The DOM element to monitor
   * @param {CanonicalBrowsingContext} browsingContext The browsing context that the textElement
   *                                                   is part of. Used to show the "DLP busy" dialog.
   * @param {nsIURI} url An nsIURI that indicates where the content would be sent to.
   *                       If this is undefined, this method will get the URI from the browsingContext.
   */
  setupContentAnalysisEventsForTextElement(textElement, browsingContext, url) {
    // Do not use a lazy service getter for this, because tests set up different mocks,
    // so if multiple tests run that call into this we can end up calling into an old mock.
    const contentAnalysis = Cc["@mozilla.org/contentanalysis;1"].getService(
      Ci.nsIContentAnalysis
    );
    if (!textElement || !contentAnalysis.isActive) {
      return;
    }
    let caEventChecker = async event => {
      let isPaste = event.type == "paste";
      let dataTransfer = isPaste ? event.clipboardData : event.dataTransfer;
      let data = dataTransfer.getData("text/plain");
      if (!data || !data.length) {
        return;
      }

      // Prevent the paste/drop from happening until content analysis returns a response
      event.preventDefault();
      // Selections can be forward or backward, so use min/max
      const startIndex = Math.min(
        textElement.selectionStart,
        textElement.selectionEnd
      );
      const endIndex = Math.max(
        textElement.selectionStart,
        textElement.selectionEnd
      );
      const selectionDirection = endIndex < startIndex ? "backward" : "forward";
      try {
        const response = await contentAnalysis.analyzeContentRequests(
          [
            this.createContentAnalysisRequest(
              {
                analysisType: Ci.nsIContentAnalysisRequest.eBulkDataEntry,
                operationTypeForDisplay: isPaste
                  ? Ci.nsIContentAnalysisRequest.ePasteClipboard
                  : Ci.nsIContentAnalysisRequest.eDroppedText,
                reason: isPaste
                  ? Ci.nsIContentAnalysisRequest.eClipboardPaste
                  : Ci.nsIContentAnalysisRequest.eDragAndDrop,
                url:
                  url ??
                  contentAnalysis.getURIForBrowsingContext(browsingContext),
                /* browsingContext can sometimes be undefined in tests where content
                   is being pasted into chrome (specifically the GenAI custom chat shortcut) */
                windowGlobalParent: browsingContext?.currentWindowContext,
              },
              { textContent: data }
            ),
          ],
          true
        );
        if (response.shouldAllowContent) {
          textElement.value =
            textElement.value.slice(0, startIndex) +
            data +
            textElement.value.slice(endIndex);
          textElement.focus();
          if (startIndex !== endIndex) {
            // Select the pasted text
            textElement.setSelectionRange(
              startIndex,
              startIndex + data.length,
              selectionDirection
            );
          }
        }
      } catch (error) {
        console.error("Content analysis request returned error: ", error);
      }
    };
    textElement.addEventListener("paste", caEventChecker);
    textElement.addEventListener("drop", caEventChecker);

    // Copying back out of this element needs analyzing too.
    this.setupContentAnalysisEventsForCopyFromElement(
      textElement,
      browsingContext
    );
  },

  /**
   * Sets up Content Analysis to monitor copying (and cutting) text out of a
   * chrome element and send it on to Content Analysis for approval.
   *
   * Chrome copies are exempt from the check in nsBaseClipboard::SetData -- that
   * exemption is what keeps copying from the URL bar, about: pages and the like
   * from being analyzed. But a few chrome surfaces display content-supplied
   * text (a prompt()'s default value, an alert()'s message), so those have to
   * opt in here, the same way they do for paste.
   *
   * This works by taking over the copy and writing the text to the clipboard
   * on behalf of the page's window. nsBaseClipboard then treats it like any
   * copy from that page: it runs the content analysis check, and on block
   * writes the placeholder notice.
   *
   * @param {Element} element The DOM element to monitor.
   * @param {CanonicalBrowsingContext} browsingContext The browsing context the
   *        element's content came from. Used for the URL reported to the agent
   *        and to show the "DLP busy" dialog.
   */
  setupContentAnalysisEventsForCopyFromElement(element, browsingContext) {
    if (!element) {
      return;
    }
    let caCopyChecker = event => {
      // Do not use a lazy service getter for this, because tests set up different
      // mocks, so if multiple tests run that call into this we can end up calling
      // into an old mock.
      const contentAnalysis = Cc["@mozilla.org/contentanalysis;1"].getService(
        Ci.nsIContentAnalysis
      );
      if (!contentAnalysis.isActive || !lazy.clipboardCopyEnabled) {
        return;
      }

      // A more specific handler (one registered on a text input inside this
      // element) has already claimed this copy and will analyze it, so don't
      // analyze or write it a second time.
      if (event.contentAnalysisHandled) {
        return;
      }

      const isCut = event.type == "cut";
      const isTextInput = typeof element.selectionStart == "number";
      let data;
      let startIndex = 0;
      let endIndex = 0;
      if (isTextInput) {
        // Selections can be forward or backward, so use min/max
        startIndex = Math.min(element.selectionStart, element.selectionEnd);
        endIndex = Math.max(element.selectionStart, element.selectionEnd);
        data = element.value.substring(startIndex, endIndex);
      } else {
        data = element.ownerDocument.getSelection().toString();
      }
      if (!data || !data.length) {
        return;
      }

      // Stop the chrome copy; the write below replaces it.
      event.preventDefault();
      event.contentAnalysisHandled = true;

      const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
        Ci.nsITransferable
      );
      trans.init(null);
      trans.addDataFlavor("text/plain");
      const str = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      str.data = data;
      trans.setTransferData("text/plain", str);

      // Writing with the page's window context is what makes nsBaseClipboard
      // analyze this as a copy from that page. The callback only fires once
      // the verdict is in (NS_ERROR_CONTENT_BLOCKED on block), so a cut can
      // wait for it before deleting anything: a blocked cut leaves the text in
      // place, since it never reached the clipboard.
      const request = Services.clipboard.asyncSetData(
        Ci.nsIClipboard.kGlobalClipboard,
        browsingContext?.currentWindowContext,
        {
          QueryInterface: ChromeUtils.generateQI([
            "nsIAsyncClipboardRequestCallback",
          ]),
          onComplete(result) {
            if (result === Cr.NS_OK) {
              if (isCut && isTextInput) {
                element.value =
                  element.value.slice(0, startIndex) +
                  element.value.slice(endIndex);
                element.focus();
                element.setSelectionRange(startIndex, startIndex);
              }
            } else if (result !== Cr.NS_ERROR_CONTENT_BLOCKED) {
              console.error(
                "Writing to the clipboard failed: ",
                Components.Exception("", result)
              );
            }
          },
        }
      );
      request.setData(trans, null);
    };
    element.addEventListener("copy", caCopyChecker);
    element.addEventListener("cut", caCopyChecker);
  },
};
