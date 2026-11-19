/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  UrlUtils: "resource://gre/modules/UrlUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(
  lazy,
  "graphemeSegmenter",
  () => new Intl.Segmenter(undefined, { granularity: "grapheme" })
);

const MAX_CLUSTER_OVERSHOOT = 32;
// Some characters, such as many emoji, occupy two units in a JavaScript
// string. The second unit is called a low surrogate and falls in this range.
const LOW_SURROGATE_MIN = 0xdc00;
const LOW_SURROGATE_MAX = 0xdfff;

export var SelectionUtils = {
  /**
   * Collapse whitespace in a string and shorten it for display, never
   * splitting a grapheme cluster (surrogate pairs, combining marks, emoji
   * sequences) unless the cluster is exceedingly long.
   *
   * @param {string} aText
   *        The text to shorten.
   * @param {number} aMaxLen
   *        The maximum length in UTF-16 code units. The cut is moved forward
   *        past a grapheme cluster that straddles it, by at most
   *        MAX_CLUSTER_OVERSHOOT code units. Beyond that the cluster is split,
   *        but never in the middle of a surrogate pair.
   * @param {object} [aOptions]
   * @param {boolean} [aOptions.addEllipsis=true]
   *        Whether to append the locale's ellipsis when text was removed.
   * @returns {string}
   */
  truncateForDisplay(aText, aMaxLen, { addEllipsis = true } = {}) {
    let text = aText.trim().replace(/\s+/g, " ");
    if (text.length <= aMaxLen) {
      return text;
    }

    let cut = aMaxLen;
    let segment = lazy.graphemeSegmenter.segment(text).containing(aMaxLen);
    if (segment && segment.index < aMaxLen) {
      let clusterEnd = segment.index + segment.segment.length;
      let nextCode = text.charCodeAt(aMaxLen);
      if (clusterEnd - aMaxLen <= MAX_CLUSTER_OVERSHOOT) {
        cut = clusterEnd;
      } else if (
        nextCode >= LOW_SURROGATE_MIN &&
        nextCode <= LOW_SURROGATE_MAX
      ) {
        cut++;
      }
    }
    if (cut >= text.length) {
      return text;
    }

    text = text.substring(0, cut);
    return addEllipsis ? text + Services.locale.ellipsis : text;
  },

  /**
   * Trim the selection text to a reasonable size and sanitize it to make it
   * safe for search query input.
   *
   * @param aSelection
   *        The selection text to trim.
   * @param aMaxLen
   *        The maximum string length, defaults to a reasonable size if undefined.
   * @return The trimmed selection text.
   */
  trimSelection(aSelection, aMaxLen) {
    // Selections of more than 150 characters aren't useful.
    const maxLen = Math.min(aMaxLen || 150, aSelection.length);

    if (aSelection.length > maxLen) {
      // only use the first maxLen important chars. see bug 221361
      let pattern = new RegExp("^(?:\\s*.){0," + maxLen + "}");
      pattern.test(aSelection);
      aSelection = RegExp.lastMatch;
    }

    aSelection = aSelection.trim().replace(/\s+/g, " ");

    if (aSelection.length > maxLen) {
      aSelection = aSelection.substr(0, maxLen);
    }

    return aSelection;
  },

  /**
   * Retrieve the text selection details for the given window.
   *
   * @param  aTopWindow
   *         The top window of the element containing the selection.
   * @param  aCharLen
   *         The maximum string length for the selection text.
   * @return The selection details containing the full and trimmed selection text
   *         and link details for link selections.
   */
  getSelectionDetails(aTopWindow, aCharLen) {
    let focusedWindow = {};
    let focusedElement = Services.focus.getFocusedElementForWindow(
      aTopWindow,
      true,
      focusedWindow
    );
    focusedWindow = focusedWindow.value;

    let selection = focusedWindow.getSelection();
    let selectionStr = selection.toString();
    let fullText;

    let url;
    let linkText;

    let isDocumentLevelSelection = true;
    // try getting a selected text in text input.
    if (!selectionStr && focusedElement) {
      // Don't get the selection for password fields. See bug 565717.
      if (
        ChromeUtils.getClassName(focusedElement) === "HTMLTextAreaElement" ||
        (ChromeUtils.getClassName(focusedElement) === "HTMLInputElement" &&
          focusedElement.mozIsTextField(true))
      ) {
        selection = focusedElement.editor.selection;
        selectionStr = selection.toString();
        isDocumentLevelSelection = false;
      }
    }

    let collapsed = selection.areNormalAndCrossShadowBoundaryRangesCollapsed;

    if (selectionStr) {
      // Have some text, let's figure out if it looks like a URL that isn't
      // actually a link.
      linkText = selectionStr.trim();

      if (
        lazy.UrlUtils.looksLikeUrl(linkText, {
          requirePath: false,
          validateOrigin: true,
        })
      ) {
        // Now let's see if this is an intentional link selection. Our guess is
        // based on whether the selection begins/ends with whitespace or is
        // preceded/followed by a non-word character.

        // selection.toString() trims trailing whitespace, so we look for
        // that explicitly in the first and last ranges.
        let beginRange = selection.getRangeAt(0);
        let delimitedAtStart = /^\s/.test(beginRange);
        if (!delimitedAtStart) {
          let container = beginRange.startContainer;
          let offset = beginRange.startOffset;
          if (container.nodeType == container.TEXT_NODE && offset > 0) {
            delimitedAtStart = /\W/.test(container.textContent[offset - 1]);
          } else {
            delimitedAtStart = true;
          }
        }

        let delimitedAtEnd = false;
        if (delimitedAtStart) {
          let endRange = selection.getRangeAt(selection.rangeCount - 1);
          delimitedAtEnd = /\s$/.test(endRange);
          if (!delimitedAtEnd) {
            let container = endRange.endContainer;
            let offset = endRange.endOffset;
            if (
              container.nodeType == container.TEXT_NODE &&
              offset < container.textContent.length
            ) {
              delimitedAtEnd = /\W/.test(container.textContent[offset]);
            } else {
              delimitedAtEnd = true;
            }
          }
        }

        if (delimitedAtStart && delimitedAtEnd) {
          try {
            url = Services.uriFixup.getFixupURIInfo(linkText).preferredURI;
          } catch (ex) {}
        }
      }
    }

    if (selectionStr) {
      // Pass up to 16K through unmolested.  If an add-on needs more, they will
      // have to use a content script.
      fullText = selectionStr.substr(0, 16384);
      selectionStr = this.trimSelection(selectionStr, aCharLen);
    }

    // This try catch is necessary since in rare cases, we think some text is a
    // link, but the host is actually invalid so we fail in nsIURI.host
    try {
      if (url && !url.host) {
        url = null;
      }
    } catch (ex) {
      url = null;
    }

    return {
      text: selectionStr,
      docSelectionIsCollapsed: collapsed,
      isDocumentLevelSelection,
      fullText,
      linkURL: url ? url.spec : null,
      linkText: url ? linkText : "",
    };
  },
};
