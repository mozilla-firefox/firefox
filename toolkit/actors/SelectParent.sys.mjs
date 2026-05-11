/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

let prefsChanged = false;

const onPrefsChanged = () => (prefsChanged = true);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "DOM_FORMS_SELECTSEARCH",
  "dom.forms.selectSearch",
  false,
  onPrefsChanged
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "CUSTOM_STYLING_ENABLED",
  "dom.forms.select.customstyling",
  false,
  onPrefsChanged
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "MAC_NATIVE_SELECT_ENABLED",
  "widget.macos.allow-native-select",
  false,
  onPrefsChanged
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "MAC_NATIVE_ANCHORED_MENUS_ENABLED",
  "widget.macos.native-anchored-menus",
  false
);

// Minimum elements required to show select search
const SEARCH_MINIMUM_ELEMENTS = 40;

// The properties that we should respect only when the item is not active.
const PROPERTIES_RESET_WHEN_ACTIVE = [
  "color",
  "background-color",
  "text-shadow",
];

// Duplicated in SelectChild.sys.mjs
// Please keep these lists in sync.
const SUPPORTED_OPTION_OPTGROUP_PROPERTIES = [
  "direction",
  "color",
  "background-color",
  "text-shadow",
  "text-transform",
  "font-family",
  "font-weight",
  "font-size",
  "font-style",
];

const SUPPORTED_SELECT_PROPERTIES = [
  ...SUPPORTED_OPTION_OPTGROUP_PROPERTIES,
  "scrollbar-width",
  "scrollbar-color",
];

export var SelectParentHelper = {
  /**
   * `populate` takes the `menulist` element and a list of `items` and generates
   * a popup list of options.
   *
   * If `CUSTOM_STYLING_ENABLED` is set to `true`, the function will also
   * style the select and its popup trying to prevent the text
   * and background to end up in the same color.
   *
   * All `ua*` variables represent the color values for the default colors
   * for their respective form elements used by the user agent.
   * The `select*` variables represent the color values defined for the
   * particular <select> element.
   *
   * The `customoptionstyling` attribute controls the application of
   * `-moz-appearance` on the elements and is disabled if the element is
   * defining its own background-color.
   *
   * @param {Element}        menulist
   * @param {Array<Element>} items
   * @param {Array<object>}  uniqueItemStyles
   * @param {number}         selectedIndex
   * @param {number}         zoom
   * @param {boolean}        custom
   * @param {boolean}        isDarkBackground
   * @param {object}         uaStyle
   * @param {object}         selectStyle
   */
  populate(
    menulist,
    items,
    uniqueItemStyles,
    selectedIndex,
    zoom,
    custom,
    isDarkBackground,
    uaStyle,
    selectStyle
  ) {
    let doc = menulist.ownerDocument;

    // Clear the current contents of the popup
    let menupopup = menulist.menupopup;
    menupopup.textContent = "";

    let stylesheet = menulist.querySelector("#ContentSelectDropdownStylesheet");
    if (stylesheet) {
      stylesheet.remove();
    }

    menupopup.setAttribute("style", "");
    menupopup.style.colorScheme = isDarkBackground ? "dark" : "light";
    menupopup.style.direction = selectStyle.direction;

    // Apply the font size of the <select> to the <menupopup> for use with
    // native menus, which do not have access to the <select>.
    if (
      AppConstants.platform == "macosx" &&
      lazy.MAC_NATIVE_ANCHORED_MENUS_ENABLED &&
      !this.disableMacNativeMenu()
    ) {
      menupopup.style.fontSize =
        zoom * parseFloat(selectStyle["font-size"], 10) + "px";
    }

    stylesheet = doc.createElementNS("http://www.w3.org/1999/xhtml", "style");
    stylesheet.setAttribute("id", "ContentSelectDropdownStylesheet");
    stylesheet.hidden = true;
    stylesheet = menulist.appendChild(stylesheet);

    let sheet = stylesheet.sheet;

    if (!custom) {
      selectStyle = uaStyle;
    }

    if (selectStyle.color == selectStyle["background-color"]) {
      selectStyle.color = uaStyle.color;
    }

    // We ensure that we set the content background if the color changes as
    // well, to prevent contrast issues.
    let selectBackgroundSet =
      selectStyle["background-color"] != uaStyle["background-color"] ||
      selectStyle.color != uaStyle.color;

    if (custom) {
      if (selectStyle["text-shadow"] != "none") {
        sheet.insertRule(
          `#ContentSelectDropdown > menupopup > :is(menuitem, menucaption)[_moz-menuactive="true"] {
          text-shadow: none;
        }`,
          0
        );
      }

      for (let property of SUPPORTED_SELECT_PROPERTIES) {
        let shouldSkip = (function () {
          if (property == "direction") {
            // Handled elsewhere.
            return true;
          }
          if (!selectStyle[property]) {
            return true;
          }
          if (property == "background-color") {
            // This also depends on whether "color" is set.
            return !selectBackgroundSet;
          }
          return selectStyle[property] == uaStyle[property];
        })();

        if (shouldSkip) {
          continue;
        }
        let value = selectStyle[property];
        if (property == "scrollbar-width") {
          // This needs to actually apply to the relevant scrollbox, because
          // scrollbar-width doesn't inherit.
          property = "--content-select-scrollbar-width";
        }
        if (property == "color") {
          property = "--panel-text-color";
        }
        menupopup.style.setProperty(property, value);
      }
      // Some webpages set the <select> backgroundColor to transparent,
      // but they don't intend to change the popup to transparent.
      // So we remove the backgroundColor and turn it into an image instead.
      if (selectBackgroundSet) {
        // We intentionally use the parsed color to prevent color
        // values like `url(..)` being injected into the
        // `background-image` property.
        let parsedColor = menupopup.style.backgroundColor;
        menupopup.style.setProperty(
          "--content-select-background-image",
          `linear-gradient(${parsedColor}, ${parsedColor})`
        );
        // Always drop the background color to avoid messing with the custom
        // shadow on Windows 10 styling.
        menupopup.style.backgroundColor = "";
        // If the background is set, we also make sure we set the color, to
        // prevent contrast issues.
        menupopup.style.setProperty("--panel-text-color", selectStyle.color);

        sheet.insertRule(
          `#ContentSelectDropdown > menupopup > :is(menuitem, menucaption):not([_moz-menuactive="true"]) {
            color: inherit;
        }`,
          0
        );
      }
    }

    for (let i = 0, len = uniqueItemStyles.length; i < len; ++i) {
      sheet.insertRule(
        `#ContentSelectDropdown .ContentSelectDropdown-item-${i} {}`,
        0
      );
      let style = uniqueItemStyles[i];
      let rule = sheet.cssRules[0].style;
      rule.direction = style.direction;
      rule.fontSize = zoom * parseFloat(style["font-size"], 10) + "px";

      if (!custom) {
        continue;
      }
      let optionBackgroundIsTransparent =
        style["background-color"] == "rgba(0, 0, 0, 0)";
      let optionBackgroundSet =
        !optionBackgroundIsTransparent || style.color != selectStyle.color;

      if (optionBackgroundIsTransparent && style.color != selectStyle.color) {
        style["background-color"] = selectStyle["background-color"];
      }

      if (style.color == style["background-color"]) {
        style.color = selectStyle.color;
      }

      let inactiveRule = null;
      for (const property of SUPPORTED_OPTION_OPTGROUP_PROPERTIES) {
        let shouldSkip = (function () {
          if (property == "direction" || property == "font-size") {
            // Handled elsewhere.
            return true;
          }
          if (!style[property]) {
            return true;
          }
          if (property == "background-color" || property == "color") {
            // This also depends on whether "color" is set.
            return !optionBackgroundSet;
          }
          return style[property] == selectStyle[property];
        })();
        if (shouldSkip) {
          continue;
        }
        if (PROPERTIES_RESET_WHEN_ACTIVE.includes(property)) {
          if (!inactiveRule) {
            sheet.insertRule(
              `#ContentSelectDropdown .ContentSelectDropdown-item-${i}:not([_moz-menuactive="true"]) {}`,
              0
            );
            inactiveRule = sheet.cssRules[0].style;
          }
          inactiveRule[property] = style[property];
        } else {
          rule[property] = style[property];
        }
      }
      style.customStyling = selectBackgroundSet || optionBackgroundSet;
    }

    // We only set the `customoptionstyling` if the background has been
    // manually set. This prevents the overlap between moz-appearance and
    // background-color. `color` and `text-shadow` do not interfere with it.
    if (custom && selectBackgroundSet) {
      menulist.menupopup.setAttribute("customoptionstyling", "true");
    } else {
      menulist.menupopup.removeAttribute("customoptionstyling");
    }

    this._currentZoom = zoom;
    this._currentMenulist = menulist;
    this.populateChildren(
      menulist,
      custom,
      items,
      uniqueItemStyles,
      selectedIndex
    );
  },

  open(browser, menulist, rect, isOpenedViaTouch, selectParentActor) {
    const canOpen = (() => {
      if (!menulist.ownerDocument.hasFocus()) {
        // Don't open in inactive browser windows.
        return false;
      }
      if (browser) {
        if (!browser.browsingContext.isActive) {
          // Don't open in inactive tabs.
          return false;
        }
        let tabbrowser = browser.getTabBrowser();
        if (tabbrowser && tabbrowser.selectedBrowser != browser) {
          // AsyncTabSwitcher might delay activating our browser, check
          // explicitly for tabbrowser.
          return false;
        }
      }
      return true;
    })();

    if (!canOpen) {
      selectParentActor.sendAsyncMessage("Forms:DismissedDropDown", {});
      return;
    }

    this._actor = selectParentActor;
    menulist.hidden = false;
    this._currentBrowser = browser;
    this._closedWithEnter = false;
    this._selectRect = rect;
    this._registerListeners(menulist.menupopup);

    let menupopup = menulist.menupopup;
    menupopup.classList.toggle("isOpenedViaTouch", isOpenedViaTouch);

    let win = menulist.documentGlobal;
    if (browser) {
      browser.constrainPopup(menupopup);
      browser.style.pointerEvents = "none";
    } else {
      menupopup.setConstraintRect(new win.DOMRect(0, 0, 0, 0));
    }
    menupopup.openPopupAtScreenRect(
      AppConstants.platform == "macosx" ? "selection" : "after_start",
      rect.left,
      rect.top,
      rect.width,
      rect.height,
      false,
      false
    );
  },

  hide(menulist, browser) {
    if (this._currentBrowser == browser) {
      menulist.menupopup.hidePopup();
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "mouseup": {
        function inRect(rect, x, y) {
          return (
            x >= rect.left &&
            x <= rect.left + rect.width &&
            y >= rect.top &&
            y <= rect.top + rect.height
          );
        }

        let x = event.screenX,
          y = event.screenY;
        let onAnchor =
          !inRect(this._currentMenulist.menupopup.getOuterScreenRect(), x, y) &&
          inRect(this._selectRect, x, y) &&
          this._currentMenulist.menupopup.state == "open";
        this._actor.sendAsyncMessage("Forms:MouseUp", { onAnchor });
        break;
      }

      case "mouseover":
        if (
          !event.relatedTarget ||
          !this._currentMenulist.contains(event.relatedTarget)
        ) {
          this._actor.sendAsyncMessage("Forms:MouseOver", {});
        }
        break;

      case "mouseout":
        if (
          !event.relatedTarget ||
          !this._currentMenulist.contains(event.relatedTarget)
        ) {
          this._actor.sendAsyncMessage("Forms:MouseOut", {});
        }
        break;

      case "keydown":
        if (event.keyCode == event.DOM_VK_RETURN) {
          this._closedWithEnter = true;
        }
        break;

      case "command":
        if (event.target.hasAttribute("value")) {
          this._actor.sendAsyncMessage("Forms:SelectDropDownItem", {
            value: event.target.value,
            closedWithEnter: this._closedWithEnter,
          });
        }
        break;

      case "fullscreen":
      case "FullscreenWarningOnScreen":
        if (this._currentMenulist) {
          this._currentMenulist.menupopup.hidePopup();
        }
        break;

      case "popuphidden": {
        let popup = event.target;
        this._unregisterListeners(popup);
        popup.parentNode.hidden = true;
        if (this._currentBrowser) {
          this._currentBrowser.style.pointerEvents = "";
          this._currentBrowser = null;
        }
        this._currentMenulist = null;
        this._selectRect = null;
        this._currentZoom = 1;
        try {
          this._actor.sendAsyncMessage("Forms:DismissedDropDown", {});
        } finally {
          this._actor = null;
        }
        break;
      }
    }
  },

  receiveMessage(browser, msg) {
    // Sanity check - we'd better know what the currently opened menulist is,
    // and what browser it belongs to...
    if (!this._currentMenulist || this._currentBrowser != browser) {
      return;
    }

    if (msg.name == "Forms:UpdateDropDown") {
      let scrollBox = this._currentMenulist.menupopup.scrollBox.scrollbox;
      let scrollTop = scrollBox.scrollTop;

      let options = msg.data.options;
      let selectedIndex = msg.data.selectedIndex;
      this.populate(
        this._currentMenulist,
        options.options,
        options.uniqueStyles,
        selectedIndex,
        this._currentZoom,
        msg.data.custom && lazy.CUSTOM_STYLING_ENABLED,
        msg.data.isDarkBackground,
        msg.data.defaultStyle,
        msg.data.style
      );

      // Restore scroll position to what it was prior to the update.
      scrollBox.scrollTop = scrollTop;
    } else if (msg.name == "Forms:BlurDropDown-Ping") {
      this._actor.sendAsyncMessage("Forms:BlurDropDown-Pong", {});
    }
  },

  _registerListeners(popup) {
    popup.addEventListener("command", this);
    popup.addEventListener("popuphidden", this);
    popup.addEventListener("mouseover", this);
    popup.addEventListener("mouseout", this);
    popup.documentGlobal.addEventListener("mouseup", this, true);
    popup.documentGlobal.addEventListener("keydown", this, true);
    popup.documentGlobal.addEventListener("fullscreen", this, true);
    popup.documentGlobal.addEventListener(
      "FullscreenWarningOnScreen",
      this,
      true
    );
  },

  _unregisterListeners(popup) {
    popup.removeEventListener("command", this);
    popup.removeEventListener("popuphidden", this);
    popup.removeEventListener("mouseover", this);
    popup.removeEventListener("mouseout", this);
    popup.documentGlobal.removeEventListener("mouseup", this, true);
    popup.documentGlobal.removeEventListener("keydown", this, true);
    popup.documentGlobal.removeEventListener("fullscreen", this, true);
    popup.documentGlobal.removeEventListener(
      "FullscreenWarningOnScreen",
      this,
      true
    );
  },

  /**
   * `populateChildren` creates all <menuitem> elements for the popup menu
   * based on the list of <option> elements from the <select> element.
   *
   * It attempts to intelligently add per-item CSS rules if the single
   * item values differ from the parent menu values and attempting to avoid
   * ending up with the same color of text and background.
   *
   * @param {Element}        menulist
   * @param {Array<Element>} options
   * @param {Array<object>}  uniqueOptionStyles
   * @param {number}         selectedIndex
   * @param {Element}        parentElement
   * @param {boolean}        isGroupDisabled
   * @param {boolean}        addSearch
   * @param {number}         nthChildIndex
   * @returns {number}
   */
  populateChildren(
    menulist,
    custom,
    options,
    uniqueOptionStyles,
    selectedIndex,
    parentElement = null,
    isGroupDisabled = false,
    addSearch = true,
    nthChildIndex = 1
  ) {
    let element = menulist.menupopup;

    let ariaOwns = "";
    for (let option of options) {
      let isOptGroup = option.isOptGroup;
      let isHR = option.isHR;

      let xulElement = "menuitem";
      if (isOptGroup) {
        xulElement = "menucaption";
      }
      if (isHR) {
        xulElement = "menuseparator";
      }

      let item = element.ownerDocument.createXULElement(xulElement);
      item.hidden =
        option.display == "none" || (parentElement && parentElement.hidden);

      if (parentElement) {
        // In the menupopup, the optgroup is a sibling of its contained options.
        // For accessibility, we want to preserve the hierarchy such that the
        // options are inside the optgroup. We do this using aria-owns on the
        // parent.
        item.id = "ContentSelectDropdownOption" + nthChildIndex;
        item.setAttribute("aria-level", "2");
        ariaOwns += item.id + " ";
      }

      element.appendChild(item);
      nthChildIndex++;

      if (isHR) {
        item.style.color = (custom && option.color) || "";

        // Continue early as HRs do not have other attributes.
        continue;
      }

      item.className = `ContentSelectDropdown-item-${option.styleIndex}`;

      if (isOptGroup) {
        item.setAttribute("role", "group");
      }
      item.setAttribute("label", option.textContent);
      // Keep track of which options are hidden by page content, so we can avoid
      // showing them on search input.
      item.hiddenByContent = item.hidden;
      item.setAttribute("tooltiptext", option.tooltip);

      if (uniqueOptionStyles[option.styleIndex].customStyling) {
        item.setAttribute("customoptionstyling", "true");
      } else {
        item.removeAttribute("customoptionstyling");
      }

      // A disabled optgroup disables all of its child options.
      let isDisabled = isGroupDisabled || option.disabled;
      if (isDisabled) {
        item.setAttribute("disabled", "true");
      }

      if (isOptGroup) {
        nthChildIndex = this.populateChildren(
          menulist,
          custom,
          option.children,
          uniqueOptionStyles,
          selectedIndex,
          item,
          isDisabled,
          false,
          nthChildIndex
        );
      } else {
        if (option.index == selectedIndex) {
          // We expect the parent element of the popup to be a <xul:menulist> that
          // has the popuponly attribute set to "true". This is necessary in order
          // for a <xul:menupopup> to act like a proper <html:select> dropdown, as
          // the <xul:menulist> does things like remember state and set the
          // _moz-menuactive attribute on the selected <xul:menuitem>.
          menulist.selectedItem = item;

          // It's hack time. In the event that we've re-populated the menulist due
          // to a mutation in the <select> in content, that means that the -moz_activemenu
          // may have been removed from the selected item. Since that's normally only
          // set for the initially selected on popupshowing for the menulist, and we
          // don't want to close and re-open the popup, we manually set it here.
          menulist.activeChild = item;
        }

        item.setAttribute("value", option.index);

        if (parentElement) {
          item.setAttribute("indented", true);
        }
      }
    }

    if (parentElement && ariaOwns) {
      parentElement.setAttribute("aria-owns", ariaOwns);
    }

    // Check if search pref is enabled, if this is the first time iterating through
    // the dropdown, and if the list is long enough for a search element to be added.
    if (
      lazy.DOM_FORMS_SELECTSEARCH &&
      addSearch &&
      element.childElementCount > SEARCH_MINIMUM_ELEMENTS
    ) {
      // Add a search text field as the first element of the dropdown
      let searchbox = element.ownerDocument.createElement("moz-input-search");
      searchbox.className = "contentSelectDropdown-searchbox";
      searchbox.addEventListener("input", this.onSearchInput);
      searchbox.addEventListener("focus", this.onSearchFocus.bind(this));
      searchbox.addEventListener("blur", this.onSearchBlur);
      searchbox.addEventListener("MozInputSearch:search", this.onSearchInput);

      // Handle special keys for exiting search
      searchbox.addEventListener(
        "keydown",
        event => {
          this.onSearchKeydown(event, menulist);
        },
        true
      );

      element.insertBefore(searchbox, element.children[0]);
    }

    return nthChildIndex;
  },

  disableMacNativeMenu() {
    return (
      AppConstants.platform == "macosx" &&
      (lazy.CUSTOM_STYLING_ENABLED ||
        lazy.DOM_FORMS_SELECTSEARCH ||
        !lazy.MAC_NATIVE_SELECT_ENABLED)
    );
  },

  onSearchKeydown(event, menulist) {
    if (event.defaultPrevented) {
      return;
    }

    let searchbox = event.currentTarget;
    switch (event.key) {
      case "Escape":
        searchbox.parentElement.hidePopup();
        break;
      case "ArrowDown":
      case "Enter":
      case "Tab":
        searchbox.blur();
        if (
          searchbox.nextElementSibling.localName == "menuitem" &&
          !searchbox.nextElementSibling.hidden
        ) {
          menulist.activeChild = searchbox.nextElementSibling;
        } else {
          let currentOption = searchbox.nextElementSibling;
          while (
            currentOption &&
            (currentOption.localName != "menuitem" || currentOption.hidden)
          ) {
            currentOption = currentOption.nextElementSibling;
          }
          if (currentOption) {
            menulist.activeChild = currentOption;
          } else {
            searchbox.focus();
          }
        }
        break;
      default:
        return;
    }
    event.preventDefault();
  },

  onSearchInput(event) {
    let searchObj = event.currentTarget;

    // Get input from search field, set to all lower case for comparison
    let input = searchObj.value.toLowerCase();
    // Get all items in dropdown (could be options or optgroups)
    let menupopup = searchObj.parentElement;
    let menuItems = menupopup.querySelectorAll("menuitem, menucaption");

    // Flag used to detect any group headers with no visible options.
    // These group headers should be hidden.
    let allHidden = true;
    // Keep a reference to the previous group header (menucaption) to go back
    // and set to hidden if all options within are hidden.
    let prevCaption = null;

    for (let currentItem of menuItems) {
      // Make sure we don't show any options that were hidden by page content
      if (!currentItem.hiddenByContent) {
        // Get label and tooltip (title) from option and change to
        // lower case for comparison
        let itemLabel = currentItem.getAttribute("label")?.toLowerCase() || "";
        let itemTooltip =
          currentItem.getAttribute("title")?.toLowerCase() || "";

        // If search input is empty, all options should be shown
        if (!input) {
          currentItem.hidden = false;
        } else if (currentItem.localName == "menucaption") {
          if (prevCaption != null) {
            prevCaption.hidden = allHidden;
          }
          prevCaption = currentItem;
          allHidden = true;
        } else {
          if (
            !currentItem.hasAttribute("indented") &&
            currentItem.previousElementSibling.hasAttribute("indented")
          ) {
            if (prevCaption != null) {
              prevCaption.hidden = allHidden;
            }
            prevCaption = null;
            allHidden = true;
          }
          if (itemLabel.includes(input) || itemTooltip.includes(input)) {
            currentItem.hidden = false;
            allHidden = false;
          } else {
            currentItem.hidden = true;
          }
        }
        if (prevCaption != null) {
          prevCaption.hidden = allHidden;
        }
      }
    }
  },

  onSearchFocus(event) {
    let menupopup = event.target.closest("menupopup");
    menupopup.parentElement.activeChild = null;
    menupopup.setAttribute("ignorekeys", "true");
    this._actor.sendAsyncMessage("Forms:SearchFocused", {});
  },

  onSearchBlur(event) {
    let menupopup = event.target.closest("menupopup");
    menupopup.setAttribute(
      "ignorekeys",
      AppConstants.platform == "win" ? "shortcuts" : "false"
    );
  },
};

export class SelectParent extends JSWindowActorParent {
  get relevantBrowser() {
    return this.browsingContext.top.embedderElement;
  }

  get _document() {
    return this.browsingContext.topChromeWindow.document;
  }

  get _menulist() {
    return this._document.getElementById("ContentSelectDropdown");
  }

  _createMenulist() {
    let document = this._document;
    let menulist = document.createXULElement("menulist");
    menulist.setAttribute("id", "ContentSelectDropdown");
    menulist.setAttribute("popuponly", "true");
    menulist.setAttribute("hidden", "true");

    let popup = menulist.appendChild(document.createXULElement("menupopup"));
    popup.setAttribute("id", "ContentSelectDropdownPopup");
    popup.setAttribute("activateontab", "true");
    popup.setAttribute("position", "after_start");
    popup.setAttribute("tabspecific", "true");
    popup.setAttribute("level", "parent");
    if (AppConstants.platform == "win") {
      popup.setAttribute("consumeoutsideclicks", "false");
      popup.setAttribute("ignorekeys", "shortcuts");
    } else if (SelectParentHelper.disableMacNativeMenu()) {
      popup.toggleAttribute("nonnative", true);
    }

    let container =
      document.getElementById("mainPopupSet") ||
      document.querySelector("popupset") ||
      document.documentElement.appendChild(
        document.createXULElement("popupset")
      );

    container.appendChild(menulist);
    return menulist;
  }

  receiveMessage(message) {
    switch (message.name) {
      case "Forms:ShowDropDown": {
        let menulist = this._menulist || this._createMenulist();

        if (prefsChanged) {
          if (AppConstants.platform == "macosx") {
            menulist.menupopup.toggleAttribute(
              "nonnative",
              SelectParentHelper.disableMacNativeMenu()
            );
          }
          prefsChanged = false;
        }

        let data = message.data;

        SelectParentHelper.populate(
          menulist,
          data.options.options,
          data.options.uniqueStyles,
          data.selectedIndex,
          // We only want to apply the full zoom. The text zoom is already
          // applied in the font-size.
          this.browsingContext.fullZoom,
          data.custom && lazy.CUSTOM_STYLING_ENABLED,
          data.isDarkBackground,
          data.defaultStyle,
          data.style
        );
        SelectParentHelper.open(
          this.relevantBrowser,
          menulist,
          data.rect,
          data.isOpenedViaTouch,
          this
        );
        break;
      }

      case "Forms:HideDropDown": {
        SelectParentHelper.hide(this._menulist, this.relevantBrowser);
        break;
      }

      default:
        SelectParentHelper.receiveMessage(this.relevantBrowser, message);
    }
  }
}
