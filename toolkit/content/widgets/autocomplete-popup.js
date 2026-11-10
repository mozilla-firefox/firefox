/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  const lazy = {};

  ChromeUtils.defineESModuleGetters(lazy, {
    AutoCompleteParent: "moz-src:///toolkit/actors/AutoCompleteParent.sys.mjs",
  });

  if (!customElements.get("autocomplete-row-item")) {
    customElements.setElementCreationCallback("autocomplete-row-item", () => {
      ChromeUtils.importESModule(
        "chrome://global/content/autocomplete-row-item/autocomplete-row-item.mjs",
        { global: "current" }
      );
    });
  }

  const MozPopupElement = MozElements.MozElementMixin(XULPopupElement);
  MozElements.MozAutocompleteRichlistboxPopup = class MozAutocompleteRichlistboxPopup extends (
    MozPopupElement
  ) {
    constructor() {
      super();

      this.attachShadow({ mode: "open" });

      {
        let slot = document.createElement("slot");
        slot.part = "content";
        this.shadowRoot.appendChild(slot);
      }

      this.mInput = null;
      this.mPopupOpen = false;
      this._currentIndex = 0;
      this._disabledItemClicked = false;
      this._secondaryActionFocused = false;

      this.setListeners();
    }

    initialize() {
      this.setAttribute("ignorekeys", "true");
      this.setAttribute("level", "top");
      this.setAttribute("consumeoutsideclicks", "never");

      this.textContent = "";
      this.appendChild(this.constructor.fragment);

      /**
       * This is the default number of rows that we give the autocomplete
       * popup when the textbox doesn't have a "maxrows" attribute
       * for us to use.
       */
      this.defaultMaxRows = 6;

      /**
       * In some cases (e.g. when the input's dropmarker button is clicked),
       * the input wants to display a popup with more rows. In that case, it
       * should increase its maxRows property and store the "normal" maxRows
       * in this field. When the popup is hidden, we restore the input's
       * maxRows to the value stored in this field.
       *
       * This field is set to -1 between uses so that we can tell when it's
       * been set by the input and when we need to set it in the popupshowing
       * handler.
       */
      this._normalMaxRows = -1;
      this._previousSelectedIndex = -1;
      this.mLastMoveTime = Date.now();
      this.mousedOverIndex = -1;
      this._richlistbox = this.querySelector(".autocomplete-richlistbox");

      if (!this.listEvents) {
        this.listEvents = {
          handleEvent: event => {
            if (!this.parentNode) {
              return;
            }

            switch (event.type) {
              case "mousedown":
                this._disabledItemClicked =
                  !!event.target.closest("richlistitem")?.disabled;
                break;
              case "mouseup":
                // Don't call onPopupClick for the scrollbar buttons, thumb,
                // slider, etc. If we hit the richlistbox and not a
                // richlistitem, we ignore the event.
                if (
                  event.target.closest("richlistbox,richlistitem").localName ==
                    "richlistitem" &&
                  !this._disabledItemClicked
                ) {
                  this.onPopupClick(event);
                }
                this._disabledItemClicked = false;
                break;
              case "mousemove": {
                if (Date.now() - this.mLastMoveTime <= 30) {
                  return;
                }

                let item = event.target.closest("richlistbox,richlistitem");

                // The pointer is over the richlistbox but not a row (the gap
                // between rows), so clear pointer selection. Otherwise
                // it stays selected while nothing is visually highlighted.
                if (item.localName == "richlistbox") {
                  if (this.richlistbox.hasAttribute("pointerselected")) {
                    lazy.AutoCompleteParent.getCurrentActor()?.clearAutoCompletePreview();
                    this.mousedOverIndex = -1;
                    this._setSelectedIndex(-1, false, true);
                  }
                  return;
                }

                let index = this.richlistbox.getIndexOfItem(item);

                this.mousedOverIndex = index;

                if (item.selectedByMouseOver) {
                  this._setSelectedIndex(index, true);
                }

                this.mLastMoveTime = Date.now();
                break;
              }
              case "mouseout": {
                if (
                  this.richlistbox.hasAttribute("pointerselected") &&
                  !this.richlistbox.contains(event.relatedTarget)
                ) {
                  lazy.AutoCompleteParent.getCurrentActor()?.clearAutoCompletePreview();
                  this.mousedOverIndex = -1;
                  this._setSelectedIndex(-1, false, true);
                }
                break;
              }
            }
          },
        };
      }
      this.richlistbox.addEventListener("mousedown", this.listEvents);
      this.richlistbox.addEventListener("mouseup", this.listEvents);
      this.richlistbox.addEventListener("mousemove", this.listEvents);
      this.richlistbox.addEventListener("mouseout", this.listEvents);
    }

    get richlistbox() {
      if (!this._richlistbox) {
        this.initialize();
      }
      return this._richlistbox;
    }

    static get markup() {
      return `
      <richlistbox class="autocomplete-richlistbox"/>
    `;
    }

    /**
     * nsIAutoCompletePopup
     */
    get input() {
      return this.mInput;
    }

    get overrideValue() {
      return null;
    }

    get popupOpen() {
      return this.mPopupOpen;
    }

    get maxRows() {
      return (this.mInput && this.mInput.maxRows) || this.defaultMaxRows;
    }

    set selectedIndex(val) {
      this._setSelectedIndex(val, false);
    }

    _setSelectedIndex(val, pointer, clearedByPointerLeave = false) {
      const changed = val != this.richlistbox.selectedIndex;
      if (changed) {
        this._previousSelectedIndex = this.richlistbox.selectedIndex;
      }

      this.richlistbox.toggleAttribute("pointerselected", pointer);

      this.richlistbox.selectedIndex = val;

      const prevSelectedItem = this.richlistbox.children[
        this._previousSelectedIndex
      ]?.querySelector("autocomplete-row-item");
      const selectedItem = this.richlistbox.children[val]?.querySelector(
        "autocomplete-row-item"
      );

      if (prevSelectedItem) {
        prevSelectedItem.selected = false;
        prevSelectedItem.pointerselected = false;
      }

      if (selectedItem) {
        selectedItem.selected = true;
        selectedItem.pointerselected = pointer;
      }

      if (changed) {
        this._secondaryActionFocused = false;
        if (prevSelectedItem) {
          prevSelectedItem.subfocused = false;
        }
        if (selectedItem) {
          selectedItem.subfocused = false;
        }
      }

      if (changed && (selectedItem || prevSelectedItem)) {
        lazy.AutoCompleteParent.getCurrentActor()?.previewAutoCompleteEntry();
      }

      // Since ensureElementIsVisible may cause an expensive Layout flush,
      // invoke it only if there may be a scrollbar, so if we could fetch
      // more results than we can show at once.
      // maxResults is the maximum number of fetched results, maxRows is the
      // maximum number of rows we show at once, without a scrollbar.
      if (this.mPopupOpen && this.maxResults > this.maxRows) {
        // when clearing the selection (val == -1, so selectedItem will be
        // null), we want to scroll back to the top (bug 406194). Except when
        // the pointer just leaves the panel, keep the scroll position (bug 2057175).
        this.richlistbox.ensureElementIsVisible(
          this.richlistbox.selectedItem ||
            (clearedByPointerLeave ? null : this.richlistbox.firstElementChild)
        );
      }
    }

    get selectedIndex() {
      return this.richlistbox.selectedIndex;
    }

    get maxResults() {
      // This is how many richlistitems will be kept around.
      // Note, this getter may be overridden, or instances
      // can have the nomaxresults attribute set to have no
      // limit.
      if (this.getAttribute("nomaxresults") == "true") {
        return Infinity;
      }
      return 20;
    }

    get matchCount() {
      return Math.min(this.mInput.controller.matchCount, this.maxResults);
    }

    get overflowPadding() {
      return Number(this.getAttribute("overflowpadding"));
    }

    set view(val) {}

    get view() {
      return this.mInput.controller;
    }

    closePopup() {
      if (this.mPopupOpen) {
        this.hidePopup();
        this.style.removeProperty("--panel-width");
      }
    }

    getNextIndex(aReverse, aAmount, aIndex, aMaxRow) {
      if (aMaxRow < 0) {
        return -1;
      }

      do {
        var newIdx = aIndex + (aReverse ? -1 : 1) * aAmount;
        if (
          (aReverse && aIndex == -1) ||
          (newIdx > aMaxRow && aIndex != aMaxRow)
        ) {
          newIdx = aMaxRow;
        } else if ((!aReverse && aIndex == -1) || (newIdx < 0 && aIndex != 0)) {
          newIdx = 0;
        }

        if (
          (newIdx < 0 && aIndex == 0) ||
          (newIdx > aMaxRow && aIndex == aMaxRow)
        ) {
          aIndex = -1;
        } else {
          aIndex = newIdx;
        }

        if (aIndex == -1) {
          return -1;
        }
      } while (
        !this.richlistbox.canUserSelect(this.richlistbox.getItemAtIndex(aIndex))
      );

      return aIndex;
    }

    onPopupClick(aEvent) {
      this.input.controller.handleEnter(true, aEvent);
    }

    onSearchBegin() {
      this.mousedOverIndex = -1;

      if (typeof this._onSearchBegin == "function") {
        this._onSearchBegin();
      }
    }

    openAutocompletePopup(aInput, aElement) {
      // until we have "baseBinding", (see bug #373652) this allows
      // us to override openAutocompletePopup(), but still call
      // the method on the base class
      this._openAutocompletePopup(aInput, aElement);
    }

    _openAutocompletePopup(aInput, aElement) {
      if (!this._richlistbox) {
        this.initialize();
      }

      if (!this.mPopupOpen) {
        // It's possible that the panel is hidden initially
        // to avoid impacting startup / new window performance
        aInput.popup.hidden = false;

        this.mInput = aInput;
        // The content path sets these from the input the popup drops out of;
        // a chrome input takes them from the chrome document.
        this.style.direction = "";
        this.style.colorScheme = "";
        // clear any previous selection, see bugs 400671 and 488357
        this.selectedIndex = -1;

        // invalidate() depends on the width attribute
        this._invalidate();

        this.openPopup(aElement, "after_start", 0, 0, false, false);
      }
    }

    invalidate(reason) {
      // Don't bother doing work if we're not even showing
      if (!this.mPopupOpen) {
        return;
      }

      this._invalidate(reason);
    }

    _invalidate() {
      // collapsed if no matches
      this.richlistbox.collapsed = this.matchCount == 0;

      this._setSecondaryActionFocused(false);

      // Update the richlistbox height.
      if (this._adjustHeightRAFToken) {
        cancelAnimationFrame(this._adjustHeightRAFToken);
        this._adjustHeightRAFToken = null;
      }

      if (this.mPopupOpen) {
        this._adjustHeightOnPopupShown = false;
        this._adjustHeightRAFToken = requestAnimationFrame(() =>
          this.adjustHeight()
        );
      } else {
        this._adjustHeightOnPopupShown = true;
      }

      this._currentIndex = 0;
      if (this._appendResultTimeout) {
        clearTimeout(this._appendResultTimeout);
      }

      if (
        !this.richlistbox.classList.contains("autocomplete-row-item-container")
      ) {
        this.richlistbox.replaceChildren();
      }

      this.richlistbox.classList.add("autocomplete-row-item-container");
      this._appendAutocompleteResults();
    }

    _collapseUnusedItems() {
      let existingItemsCount = this.richlistbox.children.length;
      for (let i = this.matchCount; i < existingItemsCount; ++i) {
        this.richlistbox.children[i].collapsed = true;
      }
    }

    adjustHeight() {
      // Figure out how many rows to show
      let rows = this.richlistbox.children;
      let numRows = Math.min(this.matchCount, this.maxRows, rows.length);

      // Default the height to 0 if we have no rows to show
      let height = 0;
      if (numRows) {
        let firstRowRect = rows[0].getBoundingClientRect();
        if (this._rlbPadding == undefined) {
          let style = window.getComputedStyle(this.richlistbox);
          let paddingTop = parseFloat(style.paddingTop) || 0;
          let paddingBottom = parseFloat(style.paddingBottom) || 0;
          this._rlbPadding = paddingTop + paddingBottom;
        }

        let lastRowMarginBottom = 0;
        let firstRowMarginTop = 0;
        // The class `forceHandleUnderflow` is for the item might need to
        // handle OverUnderflow or Overflow when the height of an item will
        // be changed dynamically.
        for (let i = 0; i < numRows; i++) {
          let child = rows[i];
          if (child.classList.contains("forceHandleUnderflow")) {
            child.handleOverUnderflow();
          }

          const styles = getComputedStyle(child);

          // Get margin-top value of first row
          if (i == 0) {
            firstRowMarginTop = parseFloat(styles.marginTop);
          }

          // Get margin-bottom value of last row
          if (i == numRows - 1) {
            lastRowMarginBottom = parseFloat(styles.marginBottom);
          }
        }

        let lastRowRect = rows[numRows - 1].getBoundingClientRect();
        // Calculate the height to have the first row to last row shown
        height =
          lastRowRect.bottom -
          firstRowRect.top +
          this._rlbPadding +
          firstRowMarginTop +
          lastRowMarginBottom;
      }

      this._collapseUnusedItems();

      // We need to get the ceiling of the calculated value to ensure that the
      // box fully contains all of its contents and doesn't cause a scrollbar.
      this.richlistbox.style.maxHeight = Math.ceil(height) + "px";
    }

    _createAutocompleteRowItem() {
      const item = document.createXULElement("richlistitem");
      item.className = "autocomplete-row-item";
      item.selectedByMouseOver = true;
      item.appendChild(document.createElement("autocomplete-row-item"));
      return item;
    }

    // Importable-login results reuse legacy Fluent strings whose value embeds
    // <div data-l10n-name="line1/line2"> markup; split it into the row item's
    // label and description.
    async _localizeRowLabel(row, { id, args }) {
      MozXULElement.insertFTLIfNeeded("toolkit/main-window/autocomplete.ftl");
      const value = await document.l10n.formatValue(id, args);
      const parsed = new DOMParser().parseFromString(value, "text/html");
      const line1 = parsed.querySelector('[data-l10n-name="line1"]');
      const line2 = parsed.querySelector('[data-l10n-name="line2"]');
      row.label = (line1 ?? parsed.body).textContent;
      row.description = line2?.textContent ?? null;
    }

    _closeSecondaryActionMenus() {
      for (const rowItem of this.richlistbox.querySelectorAll(
        "autocomplete-row-item"
      )) {
        rowItem.closeActionsMenu();
      }
    }

    _appendAutocompleteResults() {
      const controller = this.mInput.controller;
      const matchCount = this.matchCount;

      // Process maxRows per chunk to improve performance and user experience
      for (let i = 0; i < this.maxRows; i++) {
        if (this._currentIndex >= matchCount) {
          break;
        }

        let style = controller.getStyleAt(this._currentIndex);
        let value =
          style && style.includes("autofill")
            ? controller.getFinalCompleteValueAt(this._currentIndex)
            : controller.getValueAt(this._currentIndex);
        let label = controller.getLabelAt(this._currentIndex);
        let comment = controller.getCommentAt(this._currentIndex);
        let image = controller.getImageAt(this._currentIndex);

        let parsedComment = null;
        try {
          parsedComment = comment?.length ? JSON.parse(comment) : null;
        } catch {}

        // Reuse the existing row item for an existing result if possible.
        if (!this.richlistbox.children[this._currentIndex]) {
          this.richlistbox.appendChild(this._createAutocompleteRowItem());
        }

        const item = this.richlistbox.children[this._currentIndex];
        const row = item.querySelector("autocomplete-row-item");

        if (row) {
          if (parsedComment?.l10n) {
            this._localizeRowLabel(row, parsedComment.l10n);
          } else {
            row.label = label;
            row.description = parsedComment?.secondary ?? null;
          }
          row.icon = parsedComment?.icon ?? image;
          row.value = value;
          const secondaryAction = parsedComment?.secondaryAction;
          let secondary = null;
          if (secondaryAction) {
            secondary = {
              type: secondaryAction.type,
              label: secondaryAction.label,
              tooltip: secondaryAction.tooltip,
            };
            // Route each secondary action back to its provider through the
            // parent actor, identifying a menu action by its index (a single
            // action omits it). Pass the row index, since the row under the
            // pointer may not be selected.
            const rowIndex = this._currentIndex;
            const activateAction = actionIndex => () =>
              lazy.AutoCompleteParent.getCurrentActor()?.selectAutoCompleteEntry(
                true,
                actionIndex,
                rowIndex
              );
            if (secondaryAction.actions) {
              secondary.actions = secondaryAction.actions.map(
                ({ label }, index) => ({
                  label,
                  action: activateAction(index),
                })
              );
            } else {
              secondary.action = activateAction();
            }
          }
          row.actions = { primary: () => {}, secondary };
          // Rows are reused between searches, so close any actions menu left
          // open by the entry that previously occupied this row.
          row.closeActionsMenu();

          row.type = parsedComment?.type ?? null;
          row.sources = parsedComment?.sources ?? [];
          row.sourcesLabel = parsedComment?.sourcesLabel ?? null;
          row.sourcesPillsLabel = parsedComment?.sourcesPillsLabel ?? null;
          row.sourcesPillsLabelHover =
            parsedComment?.sourcesPillsLabelHover ?? null;
          row.loading = parsedComment?.loading ?? false;
          row.loadingLabel = parsedComment?.loadingLabel ?? null;
          row.emptySourcesLabel = parsedComment?.emptySourcesLabel ?? null;
        }

        item.setAttribute("dir", this.style.direction);
        item.setAttribute("originaltype", style);
        item.toggleAttribute(
          "footer",
          ["action", "loginsFooter"].includes(style)
        );

        if (parsedComment?.ariaLabel) {
          item.setAttribute("aria-label", parsedComment.ariaLabel);
        } else {
          item.removeAttribute("aria-label");
        }

        if (parsedComment?.type) {
          item.setAttribute("type", parsedComment.type);
        } else {
          item.removeAttribute("type");
        }
        item.collapsed = false;

        this._currentIndex++;
      }

      if (this._currentIndex < matchCount) {
        this._appendResultTimeout = setTimeout(
          () => this._appendAutocompleteResults(),
          0
        );
      }
    }

    selectBy(aReverse, aPage) {
      try {
        var amount = aPage ? 5 : 1;

        // because we collapsed unused items, we can't use this.richlistbox.getRowCount(), we need to use the matchCount
        this.selectedIndex = this.getNextIndex(
          aReverse,
          amount,
          this.selectedIndex,
          this.matchCount - 1
        );
        if (this.selectedIndex == -1) {
          this.input._focus();
        }
      } catch (ex) {
        // do nothing - occasionally timer-related js errors happen here
        // e.g. "this.selectedIndex has no properties", when you type fast and hit a
        // navigation key before this popup has opened
      }
    }

    get _selectedRowItem() {
      return this.richlistbox.selectedItem?.querySelector(
        "autocomplete-row-item"
      );
    }

    _setSecondaryActionFocused(focused) {
      this._secondaryActionFocused = focused;
      const rowItem = this._selectedRowItem;
      if (focused) {
        if (rowItem) {
          rowItem.subfocused = true;
        }
      } else {
        for (const item of this.richlistbox.querySelectorAll(
          "autocomplete-row-item"
        )) {
          item.subfocused = false;
        }
      }
      if (focused && this.mPopupOpen && this.richlistbox.selectedItem) {
        this.richlistbox.ensureElementIsVisible(this.richlistbox.selectedItem);
        const label = rowItem?.actions?.secondary?.label;
        if (label) {
          window.A11yUtils?.announce({ raw: label });
        }
      }
    }

    navigateSecondaryAction(reverse) {
      if (!this._selectedRowItem?.actions?.secondary) {
        return false;
      }

      if (reverse) {
        if (this._secondaryActionFocused) {
          this._setSecondaryActionFocused(false);
          return true;
        }
        return false;
      }

      if (this._secondaryActionFocused) {
        this._setSecondaryActionFocused(false);
        return false;
      }
      this._setSecondaryActionFocused(true);
      return true;
    }

    maybeActivateSecondaryAction() {
      if (!this._secondaryActionFocused) {
        return false;
      }
      const rowItem = this._selectedRowItem;
      if (!rowItem?.activateSecondaryAction()) {
        this._setSecondaryActionFocused(false);
      }
      return true;
    }

    maybeLeaveSecondaryAction() {
      if (!this._secondaryActionFocused) {
        return false;
      }
      this._setSecondaryActionFocused(false);
      return true;
    }

    disconnectedCallback() {
      if (this.listEvents) {
        this.richlistbox.removeEventListener("mousedown", this.listEvents);
        this.richlistbox.removeEventListener("mouseup", this.listEvents);
        this.richlistbox.removeEventListener("mousemove", this.listEvents);
        this.richlistbox.removeEventListener("mouseout", this.listEvents);
        delete this.listEvents;
      }
    }

    setListeners() {
      // Popups nested inside this panel (such as a row's secondary action menu)
      // bubble their own popup events up to here, so only react to our own.
      const isOwnEvent = event => event.target == this;

      this.addEventListener("popupshowing", event => {
        if (!isOwnEvent(event)) {
          return;
        }
        // If normalMaxRows wasn't already set by the input, then set it here
        // so that we restore the correct number when the popup is hidden.

        // Null-check this.mInput; see bug 1017914
        if (this._normalMaxRows < 0 && this.mInput) {
          this._normalMaxRows = this.mInput.maxRows;
        }

        this.mPopupOpen = true;
      });

      this.addEventListener("popupshown", event => {
        if (!isOwnEvent(event)) {
          return;
        }
        if (this._adjustHeightOnPopupShown) {
          this._adjustHeightOnPopupShown = false;
          this.adjustHeight();
        }
      });

      this.addEventListener("popuphiding", event => {
        if (!isOwnEvent(event)) {
          this._setSecondaryActionFocused(false);
          return;
        }

        this._closeSecondaryActionMenus();
        var isListActive = true;
        if (this.selectedIndex == -1) {
          isListActive = false;
        }
        this.input.controller.stopSearch();

        this.mPopupOpen = false;
        this._setSecondaryActionFocused(false);

        // Reset the maxRows property to the cached "normal" value (if there's
        // any), and reset normalMaxRows so that we can detect whether it was set
        // by the input when the popupshowing handler runs.

        // Null-check this.mInput; see bug 1017914
        if (this.mInput && this._normalMaxRows > 0) {
          this.mInput.maxRows = this._normalMaxRows;
        }
        this._normalMaxRows = -1;
        // If the list was being navigated and then closed, make sure
        // we fire accessible focus event back to textbox

        // Null-check this.mInput; see bug 1017914
        if (isListActive && this.mInput) {
          this.mInput.mIgnoreFocus = true;
          this.mInput._focus();
          this.mInput.mIgnoreFocus = false;
        }
      });
    }
  };

  MozPopupElement.implementCustomInterface(
    MozElements.MozAutocompleteRichlistboxPopup,
    [Ci.nsIAutoCompletePopup]
  );

  customElements.define(
    "autocomplete-richlistbox-popup",
    MozElements.MozAutocompleteRichlistboxPopup,
    {
      extends: "panel",
    }
  );
}
