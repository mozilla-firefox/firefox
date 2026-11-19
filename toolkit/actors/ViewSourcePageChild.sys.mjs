/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These are markers used to delimit the selection during processing. They
// are removed from the final rendering.
// We use noncharacter Unicode codepoints to minimize the risk of clashing
// with anything that might legitimately be present in the document.
// U+FDD0..FDEF <noncharacters>
const MARK_SELECTION_START = "\uFDD0";
const MARK_SELECTION_END = "\uFDEF";

/**
 * When showing selection source, chrome will construct a page fragment to
 * show, and then instruct content to draw a selection after load.  This is
 * set true when there is a pending request to draw selection.
 */
let gNeedsDrawSelection = false;

/**
 * Start at a specific line number.
 */
let gInitialLineNumber = -1;

export class ViewSourcePageChild extends JSWindowActorChild {
  constructor() {
    super();

    ChromeUtils.defineLazyGetter(this, "l10n", function () {
      return new Localization(["toolkit/global/viewSource.ftl"]);
    });
  }

  static setNeedsDrawSelection(value) {
    gNeedsDrawSelection = value;
  }

  static setInitialLineNumber(value) {
    gInitialLineNumber = value;
  }

  receiveMessage(msg) {
    switch (msg.name) {
      case "ViewSource:GoToLine":
        this.goToLine(msg.data.lineNumber);
        break;
    }
    return undefined;
  }

  /**
   * Any events should get handled here, and should get dispatched to
   * a specific function for the event type.
   */
  handleEvent(event) {
    switch (event.type) {
      case "pageshow":
        this.onPageShow(event);
        break;
      case "click":
        this.onClick(event);
        break;
    }
  }

  /**
   * A shortcut to the nsISelectionController for the content.
   */
  get selectionController() {
    return this.docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsISelectionDisplay)
      .QueryInterface(Ci.nsISelectionController);
  }

  /**
   * A shortcut to the nsIWebBrowserFind for the content.
   */
  get webBrowserFind() {
    return this.docShell
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebBrowserFind);
  }

  /**
   * This handler is for click events from:
   *   * error page content, which can show up if the user attempts to view the
   *     source of an attack page.
   */
  onClick(event) {
    let target = event.originalTarget;

    // Don't trust synthetic events
    if (!event.isTrusted || event.target.localName != "button") {
      return;
    }

    let errorDoc = target.ownerDocument;

    if (/^about:blocked/.test(errorDoc.documentURI)) {
      // The event came from a button on a malware/phishing block page

      if (target == errorDoc.getElementById("goBackButton")) {
        // Instead of loading some safe page, just close the window
        this.sendAsyncMessage("ViewSource:Close");
      }
    }
  }

  /**
   * Handler for the pageshow event.
   *
   * @param event
   *        The pageshow event being handled.
   */
  onPageShow() {
    // If we need to draw the selection, wait until an actual view source page
    // has loaded, instead of about:blank.
    if (
      gNeedsDrawSelection &&
      this.document.documentURI.startsWith("view-source:")
    ) {
      gNeedsDrawSelection = false;
      this.drawSelection();
    }

    if (gInitialLineNumber >= 0) {
      this.goToLine(gInitialLineNumber);
      gInitialLineNumber = -1;
    }
  }

  /**
   * Attempts to go to a particular line in the source code being
   * shown. If it succeeds in finding the line, it will fire a
   * "ViewSource:GoToLine:Success" message, passing up an object
   * with the lineNumber we just went to. If it cannot find the line,
   * it will fire a "ViewSource:GoToLine:Failed" message.
   *
   * @param lineNumber
   *        The line number to attempt to go to.
   */
  goToLine(lineNumber) {
    let range = this.findLocation(lineNumber);
    if (!range) {
      this.sendAsyncMessage("ViewSource:GoToLine:Failed");
      return;
    }

    let selection = this.document.defaultView.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);

    let selCon = this.selectionController;
    selCon.setDisplaySelection(Ci.nsISelectionController.SELECTION_ON);
    selCon.setCaretVisibilityDuringSelection(true);

    // Scroll the beginning of the line into view.
    selCon.scrollSelectionIntoView(
      Ci.nsISelectionController.SELECTION_NORMAL,
      Ci.nsISelectionController.SELECTION_WHOLE_SELECTION,
      0
    );

    this.sendAsyncMessage("ViewSource:GoToLine:Success", { lineNumber });
  }

  findLocation(lineNumber) {
    let line = this.document.getElementById(`line${lineNumber}`);
    let range = null;
    if (line) {
      range = this.document.createRange();
      range.setStart(line, 0);
      range.setEndAfter(line, line.childNodes.length);
      return range;
    }

    let pre = this.document.querySelector("pre");
    if (!pre || pre.id) {
      return null;
    }
    // Walk through each of the text nodes and count newlines.
    let curLine = 1;
    let treewalker = this.document.createTreeWalker(
      pre,
      NodeFilter.SHOW_TEXT,
      null
    );

    let found = false;
    for (
      let textNode = treewalker.firstChild();
      textNode && !found;
      textNode = treewalker.nextNode()
    ) {
      // \r is not a valid character in the DOM, so we only check for \n.
      let lineArray = textNode.data.split(/\n/);
      let lastLineInNode = curLine + lineArray.length - 1;

      // Check if we can skip the text node without further inspection.
      if (lastLineInNode < lineNumber) {
        curLine = lastLineInNode;
        continue;
      }

      // curPos is the offset within the current text node of the first
      // character in the current line.
      for (
        var i = 0, curPos = 0;
        i < lineArray.length;
        curPos += lineArray[i++].length + 1
      ) {
        if (i > 0) {
          curLine++;
        }

        if (curLine == lineNumber && !range) {
          range = this.document.createRange();
          range.setStart(textNode, curPos);

          // This will always be overridden later, except when we look for
          // the very last line in the file (this is the only line that does
          // not end with \n).
          range.setEndAfter(pre.lastChild);
        } else if (curLine == lineNumber + 1) {
          range.setEnd(textNode, curPos - 1);
          break;
        }
      }
    }

    return range;
  }

  /**
   * Using special markers left in the serialized source, this helper makes the
   * underlying markup of the selected fragment to automatically appear as
   * selected on the inflated view-source DOM.
   */
  drawSelection() {
    let doc = this.document;
    this.l10n.formatValue("view-source-selection-title").then(title => {
      doc.title = title;
    });

    // find the special selection markers that we added earlier, and
    // draw the selection between the two...
    var findService = null;
    try {
      // get the find service which stores the global find state
      findService = Cc["@mozilla.org/find/find_service;1"].getService(
        Ci.nsIFindService
      );
    } catch (e) {}
    if (!findService) {
      return;
    }

    // cache the current global find state
    var matchCase = findService.matchCase;
    var entireWord = findService.entireWord;
    var wrapFind = findService.wrapFind;
    var findBackwards = findService.findBackwards;
    var searchString = findService.searchString;
    var replaceString = findService.replaceString;

    // setup our find instance
    var findInst = this.webBrowserFind;
    findInst.matchCase = true;
    findInst.entireWord = false;
    findInst.wrapFind = true;
    findInst.findBackwards = false;

    // ...lookup the start mark
    findInst.searchString = MARK_SELECTION_START;
    var startLength = MARK_SELECTION_START.length;
    findInst.findNext();

    var selection = this.document.defaultView.getSelection();
    if (!selection.rangeCount) {
      return;
    }

    var range = selection.getRangeAt(0);

    var startContainer = range.startContainer;
    var startOffset = range.startOffset;

    // ...lookup the end mark
    findInst.searchString = MARK_SELECTION_END;
    var endLength = MARK_SELECTION_END.length;
    findInst.findNext();

    var endContainer = selection.anchorNode;
    var endOffset = selection.anchorOffset;

    // reset the selection that find has left
    selection.removeAllRanges();

    // delete the special markers now...
    endContainer.deleteData(endOffset, endLength);
    startContainer.deleteData(startOffset, startLength);
    if (startContainer == endContainer) {
      endOffset -= startLength;
    } // has shrunk if on same text node...
    range.setEnd(endContainer, endOffset);

    // show the selection and scroll it into view
    selection.addRange(range);
    // the default behavior of the selection is to scroll at the end of
    // the selection, whereas in this situation, it is more user-friendly
    // to scroll at the beginning. So we override the default behavior here
    try {
      this.selectionController.scrollSelectionIntoView(
        Ci.nsISelectionController.SELECTION_NORMAL,
        Ci.nsISelectionController.SELECTION_ANCHOR_REGION,
        true
      );
    } catch (e) {}

    // restore the current find state
    findService.matchCase = matchCase;
    findService.entireWord = entireWord;
    findService.wrapFind = wrapFind;
    findService.findBackwards = findBackwards;
    findService.searchString = searchString;
    findService.replaceString = replaceString;

    findInst.matchCase = matchCase;
    findInst.entireWord = entireWord;
    findInst.wrapFind = wrapFind;
    findInst.findBackwards = findBackwards;
    findInst.searchString = searchString;
  }
}
