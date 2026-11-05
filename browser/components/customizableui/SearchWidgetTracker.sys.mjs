/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { CustomizableUI } from "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs";

const WIDGET_ID = "search-container";

/**
 * Updates persisted widths when the search bar is removed from any
 * customizable area and put into the palette.
 */
export const SearchWidgetTracker = {
  /**
   * Main entrypoint to initializing the SearchWidgetTracker.
   */
  init() {
    CustomizableUI.addListener(this);
  },

  /**
   * The callback for when a widget is moved via CustomizableUI. We use this
   * to detect movement of the search bar.
   *
   * @param {Element} node
   *   The DOM node that was acted upon.
   * @param {Element|null} _nextNode
   *   The DOM node (if any) that the widget was inserted before.
   * @param {Element} _container
   *   The *actual* DOM container for the widget (could be an overflow panel in
   *   case of an overflowable toolbar).
   * @param {boolean} wasRemoval
   *   True iff the action that happened was the removal of the DOM node.
   */
  onWidgetAfterDOMChange(node, _nextNode, _container, wasRemoval) {
    if (node.id == WIDGET_ID && wasRemoval) {
      this._removePersistedWidths();
    }
  },

  /**
   * Removes any widget customization on the search bar (which can be created
   * with the resizer that appears if the search bar is placed immediately after
   * the URL bar). Goes through each open browser window and removes the width
   * property / style on each existant search bar.
   */
  _removePersistedWidths() {
    Services.xulStore.removeValue(
      AppConstants.BROWSER_CHROME_URL,
      WIDGET_ID,
      "width"
    );
    for (let win of CustomizableUI.windows) {
      let searchbar =
        win.document.getElementById(WIDGET_ID) ||
        win.gNavToolbox.palette.querySelector("#" + WIDGET_ID);
      searchbar.removeAttribute("width");
      searchbar.style.removeProperty("width");
    }
  },
};
