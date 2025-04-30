/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
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

export const ContentAnalysisUtils = {
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
            // Specify an explicit type here to suppress type errors about the missing
            // properties, because there are a ton of them that make this hard to read.
            /** @type {nsIContentAnalysisRequest} */
            ({
              analysisType: Ci.nsIContentAnalysisRequest.eBulkDataEntry,
              reason: isPaste
                ? Ci.nsIContentAnalysisRequest.eClipboardPaste
                : Ci.nsIContentAnalysisRequest.eDragAndDrop,
              resources: [],
              operationTypeForDisplay: isPaste
                ? Ci.nsIContentAnalysisRequest.eClipboard
                : Ci.nsIContentAnalysisRequest.eDroppedText,
              url:
                url ??
                contentAnalysis.getURIForBrowsingContext(browsingContext),
              textContent: data,
              /* browsingContext can sometimes be undefined in tests where content
                 is being pasted into chrome (specifically the GenAI custom chat shortcut) */
              windowGlobalParent: browsingContext?.currentWindowContext,
            }),
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
  },
};
