/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  const { LoginHelper } = ChromeUtils.importESModule(
    "resource://gre/modules/LoginHelper.sys.mjs"
  );

  MozElements.MozAutocompleteRichlistitem = class MozAutocompleteRichlistitem extends (
    MozElements.MozRichlistitem
  ) {
    constructor() {
      super();

      /**
       * This overrides listitem's mousedown handler because we want to set the
       * selected item even when the shift or accel keys are pressed.
       */
      this.addEventListener("mousedown", () => {
        // Call this.control only once since it's not a simple getter.
        let control = this.control;
        if (!control || control.disabled) {
          return;
        }
        if (!this.selected) {
          control.selectItem(this);
        }
        control.currentItem = this;
      });

      this.addEventListener("mouseover", event => {
        // The point of implementing this handler is to allow drags to change
        // the selected item.  If the user mouses down on an item, it becomes
        // selected.  If they then drag the mouse to another item, select it.
        // Handle all three primary mouse buttons: right, left, and wheel, since
        // all three change the selection on mousedown.
        let mouseDown = event.buttons & 0b111;
        if (!mouseDown) {
          return;
        }
        // Call this.control only once since it's not a simple getter.
        let control = this.control;
        if (!control || control.disabled) {
          return;
        }
        if (!this.selected) {
          control.selectItem(this);
        }
        control.currentItem = this;
      });

      this.addEventListener("overflow", () => this._onOverflow());
      this.addEventListener("underflow", () => this._onUnderflow());
    }

    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();

      this._boundaryCutoff = null;
      this._inOverflow = false;

      this._adjustAcItem();
    }

    static get inheritedAttributes() {
      return {
        ".ac-type-icon": "selected,current,type",
        ".ac-site-icon": "src=image,selected,type",
        ".ac-title": "selected",
        ".ac-title-text": "selected",
        ".ac-separator": "selected,type",
        ".ac-url": "selected",
        ".ac-url-text": "selected",
      };
    }

    static get markup() {
      return `
      <image class="ac-type-icon"/>
      <image class="ac-site-icon"/>
      <hbox class="ac-title" align="center">
        <description class="ac-text-overflow-container">
          <description class="ac-title-text"/>
        </description>
      </hbox>
      <hbox class="ac-separator" align="center">
        <description class="ac-separator-text" value="—"/>
      </hbox>
      <hbox class="ac-url" align="center" aria-hidden="true">
        <description class="ac-text-overflow-container">
          <description class="ac-url-text"/>
        </description>
      </hbox>
    `;
    }

    get _typeIcon() {
      return this.querySelector(".ac-type-icon");
    }

    get _titleText() {
      return this.querySelector(".ac-title-text");
    }

    get _separator() {
      return this.querySelector(".ac-separator");
    }

    get _urlText() {
      return this.querySelector(".ac-url-text");
    }

    get _stringBundle() {
      if (!this.__stringBundle) {
        this.__stringBundle = Services.strings.createBundle(
          "chrome://global/locale/autocomplete.properties"
        );
      }
      return this.__stringBundle;
    }

    get boundaryCutoff() {
      if (!this._boundaryCutoff) {
        this._boundaryCutoff = Services.prefs.getIntPref(
          "toolkit.autocomplete.richBoundaryCutoff"
        );
      }
      return this._boundaryCutoff;
    }

    _cleanup() {
      this.removeAttribute("url");
      this.removeAttribute("image");
      this.removeAttribute("title");
      this.removeAttribute("text");
    }

    _onOverflow() {
      this._inOverflow = true;
      this._handleOverflow();
    }

    _onUnderflow() {
      this._inOverflow = false;
      this._handleOverflow();
    }

    _getBoundaryIndices(aText, aSearchTokens) {
      // Short circuit for empty search ([""] == "")
      if (aSearchTokens == "") {
        return [0, aText.length];
      }

      // Find which regions of text match the search terms
      let regions = [];
      for (let search of Array.prototype.slice.call(aSearchTokens)) {
        let matchIndex = -1;
        let searchLen = search.length;

        // Find all matches of the search terms, but stop early for perf
        let lowerText = aText.substr(0, this.boundaryCutoff).toLowerCase();
        while ((matchIndex = lowerText.indexOf(search, matchIndex + 1)) >= 0) {
          regions.push([matchIndex, matchIndex + searchLen]);
        }
      }

      // Sort the regions by start position then end position
      regions = regions.sort((a, b) => {
        let start = a[0] - b[0];
        return start == 0 ? a[1] - b[1] : start;
      });

      // Generate the boundary indices from each region
      let start = 0;
      let end = 0;
      let boundaries = [];
      let len = regions.length;
      for (let i = 0; i < len; i++) {
        // We have a new boundary if the start of the next is past the end
        let region = regions[i];
        if (region[0] > end) {
          // First index is the beginning of match
          boundaries.push(start);
          // Second index is the beginning of non-match
          boundaries.push(end);

          // Track the new region now that we've stored the previous one
          start = region[0];
        }

        // Push back the end index for the current or new region
        end = Math.max(end, region[1]);
      }

      // Add the last region
      boundaries.push(start);
      boundaries.push(end);

      // Put on the end boundary if necessary
      if (end < aText.length) {
        boundaries.push(aText.length);
      }

      // Skip the first item because it's always 0
      return boundaries.slice(1);
    }

    _getSearchTokens(aSearch) {
      let search = aSearch.toLowerCase();
      return search.split(/\s+/);
    }

    _setUpDescription(aDescriptionElement, aText) {
      // Get rid of all previous text
      if (!aDescriptionElement) {
        return;
      }
      while (aDescriptionElement.hasChildNodes()) {
        aDescriptionElement.firstChild.remove();
      }

      // Get the indices that separate match and non-match text
      let search = this.getAttribute("text");
      let tokens = this._getSearchTokens(search);
      let indices = this._getBoundaryIndices(aText, tokens);

      this._appendDescriptionSpans(
        indices,
        aText,
        aDescriptionElement,
        aDescriptionElement
      );
    }

    _appendDescriptionSpans(
      indices,
      text,
      spansParentElement,
      descriptionElement
    ) {
      let next;
      let start = 0;
      let len = indices.length;
      // Even indexed boundaries are matches, so skip the 0th if it's empty
      for (let i = indices[0] == 0 ? 1 : 0; i < len; i++) {
        next = indices[i];
        let spanText = text.substr(start, next - start);
        start = next;

        if (i % 2 == 0) {
          // Emphasize the text for even indices
          let span = spansParentElement.appendChild(
            document.createElementNS("http://www.w3.org/1999/xhtml", "span")
          );
          this._setUpEmphasisSpan(span, descriptionElement);
          span.textContent = spanText;
        } else {
          // Otherwise, it's plain text
          spansParentElement.appendChild(document.createTextNode(spanText));
        }
      }
    }

    _setUpEmphasisSpan(aSpan, aDescriptionElement) {
      aSpan.classList.add("ac-emphasize-text");
      switch (aDescriptionElement) {
        case this._titleText:
          aSpan.classList.add("ac-emphasize-text-title");
          break;
        case this._urlText:
          aSpan.classList.add("ac-emphasize-text-url");
          break;
      }
    }

    /**
     * This will generate an array of emphasis pairs for use with
     * _setUpEmphasisedSections(). Each pair is a tuple (array) that
     * represents a block of text - containing the text of that block, and a
     * boolean for whether that block should have an emphasis styling applied
     * to it.
     *
     * These pairs are generated by parsing a localised string (aSourceString)
     * with parameters, in the format that is used by
     * nsIStringBundle.formatStringFromName():
     *
     * "textA %1$S textB textC %2$S"
     *
     * Or:
     *
     * "textA %S"
     *
     * Where "%1$S", "%2$S", and "%S" are intended to be replaced by provided
     * replacement strings. These are specified an array of tuples
     * (aReplacements), each containing the replacement text and a boolean for
     * whether that text should have an emphasis styling applied. This is used
     * as a 1-based array - ie, "%1$S" is replaced by the item in the first
     * index of aReplacements, "%2$S" by the second, etc. "%S" will always
     * match the first index.
     */
    _generateEmphasisPairs(aSourceString, aReplacements) {
      let pairs = [];

      // Split on %S, %1$S, %2$S, etc. ie:
      //   "textA %S"
      //     becomes ["textA ", "%S"]
      //   "textA %1$S textB textC %2$S"
      //     becomes ["textA ", "%1$S", " textB textC ", "%2$S"]
      let parts = aSourceString.split(/(%(?:[0-9]+\$)?S)/);

      for (let part of parts) {
        // The above regex will actually give us an empty string at the
        // end - we don't want that, as we don't want to later generate an
        // empty text node for it.
        if (part.length === 0) {
          continue;
        }

        // Determine if this token is a replacement token or a normal text
        // token. If it is a replacement token, we want to extract the
        // numerical number. However, we still want to match on "$S".
        let match = part.match(/^%(?:([0-9]+)\$)?S$/);

        if (match) {
          // "%S" doesn't have a numerical number in it, but will always
          // be assumed to be 1. Furthermore, the input string specifies
          // these with a 1-based index, but we want a 0-based index.
          let index = (match[1] || 1) - 1;

          if (index >= 0 && index < aReplacements.length) {
            pairs.push([...aReplacements[index]]);
          }
        } else {
          pairs.push([part]);
        }
      }

      return pairs;
    }

    /**
     * _setUpEmphasisedSections() has the same use as _setUpDescription,
     * except instead of taking a string and highlighting given tokens, it takes
     * an array of pairs generated by _generateEmphasisPairs(). This allows
     * control over emphasising based on specific blocks of text, rather than
     * search for substrings.
     */
    _setUpEmphasisedSections(aDescriptionElement, aTextPairs) {
      // Get rid of all previous text
      while (aDescriptionElement.hasChildNodes()) {
        aDescriptionElement.firstChild.remove();
      }

      for (let [text, emphasise] of aTextPairs) {
        if (emphasise) {
          let span = aDescriptionElement.appendChild(
            document.createElementNS("http://www.w3.org/1999/xhtml", "span")
          );
          span.textContent = text;
          switch (emphasise) {
            case "match":
              this._setUpEmphasisSpan(span, aDescriptionElement);
              break;
          }
        } else {
          aDescriptionElement.appendChild(document.createTextNode(text));
        }
      }
    }

    _unescapeUrl(url) {
      return Services.textToSubURI.unEscapeURIForUI(url);
    }

    _reuseAcItem() {
      this.collapsed = false;

      // The popup may have changed size between now and the last
      // time the item was shown, so always handle over/underflow.
      let dwu = window.windowUtils;
      let popupWidth = dwu.getBoundsWithoutFlushing(this.parentNode).width;
      if (!this._previousPopupWidth || this._previousPopupWidth != popupWidth) {
        this._previousPopupWidth = popupWidth;
        this.handleOverUnderflow();
      }
    }

    _adjustAcItem() {
      let originalUrl = this.getAttribute("ac-value");
      let title = this.getAttribute("ac-label");
      this.setAttribute("url", originalUrl);
      this.setAttribute("image", this.getAttribute("ac-image"));
      this.setAttribute("title", title);
      this.setAttribute("text", this.getAttribute("ac-text"));

      let type = this.getAttribute("originaltype");
      let types = new Set(type.split(/\s+/));
      // Remove types that should ultimately not be in the `type` string.
      types.delete("autofill");
      type = [...types][0] || "";
      this.setAttribute("type", type);

      let displayUrl = this._unescapeUrl(originalUrl);

      // Show the domain as the title if we don't have a title.
      if (!title) {
        try {
          let uri = Services.io.newURI(originalUrl);
          // Not all valid URLs have a domain.
          if (uri.host) {
            title = uri.host;
          }
        } catch (e) {}
        if (!title) {
          title = displayUrl;
        }
      }

      if (Array.isArray(title)) {
        this._setUpEmphasisedSections(this._titleText, title);
      } else {
        this._setUpDescription(this._titleText, title);
      }
      this._setUpDescription(this._urlText, displayUrl);

      // Removing the max-width may be jarring when the item is visible, but
      // we have no other choice to properly crop the text.
      // Removing max-widths may cause overflow or underflow events, that
      // will set the _inOverflow property. In case both the old and the new
      // text are overflowing, the overflow event won't happen, and we must
      // enforce an _handleOverflow() call to update the max-widths.
      let wasInOverflow = this._inOverflow;
      this._removeMaxWidths();
      if (wasInOverflow && this._inOverflow) {
        this._handleOverflow();
      }
    }

    _removeMaxWidths() {
      if (this._hasMaxWidths) {
        this._titleText.style.removeProperty("max-width");
        this._urlText.style.removeProperty("max-width");
        this._hasMaxWidths = false;
      }
    }

    /**
     * This method truncates the displayed strings as necessary.
     */
    _handleOverflow() {
      let itemRect = this.parentNode.getBoundingClientRect();
      let titleRect = this._titleText.getBoundingClientRect();
      let separatorRect = this._separator.getBoundingClientRect();
      let urlRect = this._urlText.getBoundingClientRect();
      let separatorURLWidth = separatorRect.width + urlRect.width;

      // Total width for the title and URL is the width of the item
      // minus the start of the title text minus a little optional extra padding.
      // This extra padding amount is basically arbitrary but keeps the text
      // from getting too close to the popup's edge.
      let dir = this.getAttribute("dir");
      let titleStart =
        dir == "rtl"
          ? itemRect.right - titleRect.right
          : titleRect.left - itemRect.left;

      let popup = this.parentNode.parentNode;
      let itemWidth =
        itemRect.width -
        titleStart -
        popup.overflowPadding -
        (popup.margins ? popup.margins.end : 0);

      let titleWidth = titleRect.width;
      if (titleWidth + separatorURLWidth > itemWidth) {
        // The percentage of the item width allocated to the title.
        let titlePct = 0.66;

        let titleAvailable = itemWidth - separatorURLWidth;
        let titleMaxWidth = Math.max(titleAvailable, itemWidth * titlePct);
        if (titleWidth > titleMaxWidth) {
          this._titleText.style.maxWidth = titleMaxWidth + "px";
        }
        let urlMaxWidth = Math.max(
          itemWidth - titleWidth,
          itemWidth * (1 - titlePct)
        );
        urlMaxWidth -= separatorRect.width;
        this._urlText.style.maxWidth = urlMaxWidth + "px";
        this._hasMaxWidths = true;
      }
    }

    handleOverUnderflow() {
      this._removeMaxWidths();
      this._handleOverflow();
    }
  };

  MozXULElement.implementCustomInterface(
    MozElements.MozAutocompleteRichlistitem,
    [Ci.nsIDOMXULSelectControlItemElement]
  );

  class MozAutocompleteRichlistitemInsecureWarning
    extends MozElements.MozAutocompleteRichlistitem
  {
    constructor() {
      super();

      this.addEventListener("click", event => {
        if (event.button != 0) {
          return;
        }

        let comment = this.getAttribute("ac-comment");
        if (comment && JSON.parse(comment)?.noLearnMore) {
          return;
        }

        let baseURL = Services.urlFormatter.formatURLPref(
          "app.support.baseURL"
        );
        window.openTrustedLinkIn(baseURL + "insecure-password", "tab", {
          relatedToCurrent: true,
        });
      });
    }

    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }

      super.connectedCallback();

      // Unlike other autocomplete items, the height of the insecure warning
      // increases by wrapping. So "forceHandleUnderflow" is for container to
      // recalculate an item's height and width.
      this.classList.add("forceHandleUnderflow");
    }

    static get inheritedAttributes() {
      return {
        ".ac-type-icon": "selected,current,type",
        ".ac-site-icon": "src=image,selected,type",
        ".ac-title-text": "selected",
        ".ac-separator": "selected,type",
        ".ac-url": "selected",
        ".ac-url-text": "selected",
      };
    }

    static get markup() {
      return `
      <image class="ac-type-icon"/>
      <image class="ac-site-icon"/>
      <vbox class="ac-title" align="left">
        <description class="ac-text-overflow-container">
          <description class="ac-title-text"/>
        </description>
      </vbox>
      <hbox class="ac-separator" align="center">
        <description class="ac-separator-text" value="—"/>
      </hbox>
      <hbox class="ac-url" align="center">
        <description class="ac-text-overflow-container">
          <description class="ac-url-text"/>
        </description>
      </hbox>
    `;
    }
  }

  class MozAutocompleteRichlistitemLoginsFooter
    extends MozElements.MozAutocompleteRichlistitem {}

  class MozAutocompleteImportableLearnMoreRichlistitem
    extends MozElements.MozAutocompleteRichlistitem
  {
    constructor() {
      super();
      MozXULElement.insertFTLIfNeeded("toolkit/main-window/autocomplete.ftl");
    }

    static get markup() {
      return `
      <image class="ac-type-icon"/>
      <image class="ac-site-icon"/>
      <vbox class="ac-title" align="left">
        <description class="ac-text-overflow-container">
          <description class="ac-title-text"
                       data-l10n-id="autocomplete-import-learn-more"/>
        </description>
      </vbox>
      <hbox class="ac-separator" align="center">
        <description class="ac-separator-text" value="—"/>
      </hbox>
      <hbox class="ac-url" align="center">
        <description class="ac-text-overflow-container">
          <description class="ac-url-text"/>
        </description>
      </hbox>
    `;
    }

    // Override to avoid clearing out fluent description.
    _setUpDescription() {}
  }

  class MozAutocompleteTwoLineRichlistitem extends MozElements.MozRichlistitem {
    connectedCallback() {
      if (this.delayConnectedCallback()) {
        return;
      }

      this.textContent = "";
      this.appendChild(this.constructor.fragment);
      this.initializeAttributeInheritance();
      this.initializeSecondaryAction();
      this._adjustAcItem();
    }

    initializeSecondaryAction() {
      const button = this.querySelector(".ac-secondary-action");

      if (this.onSecondaryAction) {
        button.addEventListener("mousedown", event => {
          event.stopPropagation();
          this.onSecondaryAction();
        });
      } else {
        button?.remove();
      }
    }

    static get inheritedAttributes() {
      return {
        // getLabelAt:
        ".line1-label": "text=ac-label",
        ".ac-site-icon": "src=ac-image",
      };
    }

    static get markup() {
      return `
      <div xmlns="http://www.w3.org/1999/xhtml"
           xmlns:xul="http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"
           class="two-line-wrapper">
        <xul:image class="ac-site-icon"></xul:image>
        <div class="labels-wrapper">
          <div class="label-row line1-label"></div>
          <div class="label-row line2-label"></div>
        </div>
        <button class="ac-secondary-action"></button>
      </div>
    `;
    }

    _adjustAcItem() {
      let comment = JSON.parse(this.getAttribute("ac-comment"));
      this.querySelector(".line2-label").textContent = comment?.secondary || "";

      this.querySelector(".ac-site-icon").collapsed =
        this.getAttribute("ac-image") == "";
    }

    _onOverflow() {}

    _onUnderflow() {}

    handleOverUnderflow() {}
  }

  class MozAutocompleteLoginRichlistitem extends MozAutocompleteTwoLineRichlistitem {
    connectedCallback() {
      super.connectedCallback();
      this.firstChild.classList.add("ac-login-item");
    }

    onSecondaryAction() {
      const comment = JSON.parse(this.getAttribute("ac-comment"));
      LoginHelper.openPasswordManager(window, {
        loginGuid: comment?.guid,
      });
    }

    static get inheritedAttributes() {
      return {
        // getLabelAt:
        ".line1-label": "text=ac-label",
        ".ac-site-icon": "src=ac-image",
      };
    }
  }

  // This type has an action that is triggered when activated. The comment
  // for that result should contain a fillMessageName which is the message to send.
  class MozAutocompleteActionRichlistitem extends MozAutocompleteTwoLineRichlistitem {
    constructor() {
      super();
      this.selectedByMouseOver = true;
    }
  }

  class MozAutocompleteAutoFillRichlistitem extends MozAutocompleteTwoLineRichlistitem {
    constructor() {
      super();
      this.selectedByMouseOver = true;
    }

    _adjustAcItem() {
      let label = this.getAttribute("ac-label");
      this.querySelector(".line1-label").textContent = label;

      let { secondary, ariaLabel } = JSON.parse(
        this.getAttribute("ac-comment")
      );

      let line2Label = this.querySelector(".line2-label");
      line2Label.textContent = secondary ?? "";

      if (ariaLabel) {
        this.setAttribute("aria-label", ariaLabel);
      }

      this.querySelector(".ac-site-icon").collapsed =
        this.getAttribute("ac-image") == "";
    }

    set selected(val) {
      this.toggleAttribute("selected", val);

      setTimeout(() => {
        const { AutoCompleteParent } = ChromeUtils.importESModule(
          "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs"
        );
        const actor = AutoCompleteParent.getCurrentActor();
        actor?.previewAutoCompleteEntry();
      }, 0);
    }

    get selected() {
      return this.hasAttribute("selected");
    }
  }

  class MozAutocompleteGeneratedPasswordRichlistitem extends MozAutocompleteTwoLineRichlistitem {
    constructor() {
      super();

      // Line 2 and line 3 both display text with a different line-height than
      // line 1 but we want the line-height to be the same so we wrap the text
      // in <span> and only adjust the line-height via font CSS properties on them.
      this.generatedPasswordText = document.createElement("span");

      this.line3Text = document.createElement("span");
      this.line3 = document.createElement("div");
      this.line3.className = "label-row generated-password-autosave";
      this.line3.append(this.line3Text);
    }

    get _autoSaveString() {
      if (!this.__autoSaveString) {
        let brandShorterName = Services.strings
          .createBundle("chrome://branding/locale/brand.properties")
          .GetStringFromName("brandShorterName");
        this.__autoSaveString = Services.strings
          .createBundle("chrome://passwordmgr/locale/passwordmgr.properties")
          .formatStringFromName("generatedPasswordWillBeSaved", [
            brandShorterName,
          ]);
      }
      return this.__autoSaveString;
    }

    _adjustAcItem() {
      let { generatedPassword, willAutoSaveGeneratedPassword } = JSON.parse(
        this.getAttribute("ac-comment")
      );
      let line2Label = this.querySelector(".line2-label");
      line2Label.textContent = "";
      this.generatedPasswordText.textContent = generatedPassword;
      line2Label.append(this.generatedPasswordText);

      if (willAutoSaveGeneratedPassword) {
        this.line3Text.textContent = this._autoSaveString;
        this.querySelector(".labels-wrapper").append(this.line3);
      } else {
        this.line3.remove();
      }

      super._adjustAcItem();
    }
  }

  class MozAutocompleteImportableLoginsRichlistitem extends MozAutocompleteTwoLineRichlistitem {
    constructor() {
      super();
      MozXULElement.insertFTLIfNeeded("toolkit/main-window/autocomplete.ftl");
    }

    static get inheritedAttributes() {
      return {
        // getLabelAt:
        ".line1-label": "text=ac-label",
      };
    }

    static get markup() {
      return `
      <div xmlns="http://www.w3.org/1999/xhtml"
           xmlns:xul="http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"
           class="two-line-wrapper">
        <xul:image class="ac-site-icon" />
        <div class="labels-wrapper">
          <div class="label-row line1-label" data-l10n-name="line1" />
          <div class="label-row line2-label" data-l10n-name="line2" />
        </div>
      </div>
    `;
    }

    _adjustAcItem() {
      super._adjustAcItem();
      document.l10n.setAttributes(
        this.querySelector(".labels-wrapper"),
        `autocomplete-import-logins-${this.getAttribute("ac-value")}`,
        {
          host: JSON.parse(this.getAttribute("ac-comment")).hostname.replace(
            /^www\./,
            ""
          ),
        }
      );
    }
  }

  customElements.define(
    "autocomplete-richlistitem",
    MozElements.MozAutocompleteRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-richlistitem-insecure-warning",
    MozAutocompleteRichlistitemInsecureWarning,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-richlistitem-logins-footer",
    MozAutocompleteRichlistitemLoginsFooter,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-two-line-richlistitem",
    MozAutocompleteTwoLineRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-autofill-richlistitem",
    MozAutocompleteAutoFillRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-login-richlistitem",
    MozAutocompleteLoginRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-action-richlistitem",
    MozAutocompleteActionRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-generated-password-richlistitem",
    MozAutocompleteGeneratedPasswordRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-importable-learn-more-richlistitem",
    MozAutocompleteImportableLearnMoreRichlistitem,
    {
      extends: "richlistitem",
    }
  );

  customElements.define(
    "autocomplete-importable-logins-richlistitem",
    MozAutocompleteImportableLoginsRichlistitem,
    {
      extends: "richlistitem",
    }
  );
}
