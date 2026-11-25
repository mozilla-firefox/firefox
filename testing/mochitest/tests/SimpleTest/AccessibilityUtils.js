/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Loaded into this scope by browser-test.js before this file.
const ClickChecks = this.ClickChecks;

/**
 * Accessible states used to check node's state from the accessiblity API
 * perspective.
 *
 * Note: if gecko is built with --disable-accessibility, the interfaces
 * are not defined. This is why we use getters instead to be able to use
 * these statically.
 */

this.AccessibilityUtils = (function () {
  const FORCE_DISABLE_ACCESSIBILITY_PREF = "accessibility.force_disabled";

  // Accessible states.
  const { STATE_FOCUSABLE, STATE_INVISIBLE, STATE_LINKED, STATE_UNAVAILABLE } =
    Ci.nsIAccessibleStates;

  // Accessible action for showing long description.
  const CLICK_ACTION = "click";

  // Roles that are considered focusable with the keyboard.
  const KEYBOARD_FOCUSABLE_ROLES = new Set([
    Ci.nsIAccessibleRole.ROLE_BUTTONMENU,
    Ci.nsIAccessibleRole.ROLE_CHECKBUTTON,
    Ci.nsIAccessibleRole.ROLE_COMBOBOX,
    Ci.nsIAccessibleRole.ROLE_EDITCOMBOBOX,
    Ci.nsIAccessibleRole.ROLE_ENTRY,
    Ci.nsIAccessibleRole.ROLE_LINK,
    Ci.nsIAccessibleRole.ROLE_LISTBOX,
    Ci.nsIAccessibleRole.ROLE_PASSWORD_TEXT,
    Ci.nsIAccessibleRole.ROLE_PUSHBUTTON,
    Ci.nsIAccessibleRole.ROLE_RADIOBUTTON,
    Ci.nsIAccessibleRole.ROLE_SEARCHBOX,
    Ci.nsIAccessibleRole.ROLE_SLIDER,
    Ci.nsIAccessibleRole.ROLE_SPINBUTTON,
    Ci.nsIAccessibleRole.ROLE_SUMMARY,
    Ci.nsIAccessibleRole.ROLE_SWITCH,
    Ci.nsIAccessibleRole.ROLE_TOGGLE_BUTTON,
  ]);

  // Roles that are user interactive.
  const INTERACTIVE_ROLES = new Set([
    ...KEYBOARD_FOCUSABLE_ROLES,
    Ci.nsIAccessibleRole.ROLE_CHECK_MENU_ITEM,
    Ci.nsIAccessibleRole.ROLE_CHECK_RICH_OPTION,
    Ci.nsIAccessibleRole.ROLE_COMBOBOX_OPTION,
    Ci.nsIAccessibleRole.ROLE_MENUITEM,
    Ci.nsIAccessibleRole.ROLE_OPTION,
    Ci.nsIAccessibleRole.ROLE_OUTLINE,
    Ci.nsIAccessibleRole.ROLE_OUTLINEITEM,
    Ci.nsIAccessibleRole.ROLE_PAGETAB,
    Ci.nsIAccessibleRole.ROLE_PARENT_MENUITEM,
    Ci.nsIAccessibleRole.ROLE_RADIO_MENU_ITEM,
    Ci.nsIAccessibleRole.ROLE_RICH_OPTION,
  ]);

  // Roles which, when focusable, are operated by changing their value rather
  // than by being activated, so they do not expose an accessible action. The
  // same roles also cover decorations that are never focusable, like a toolbar
  // spring or an <hr>, so test them with isFocusableValueRole rather than with
  // this set directly.
  const FOCUSABLE_VALUE_ROLES = new Set([
    Ci.nsIAccessibleRole.ROLE_SCROLLBAR,
    Ci.nsIAccessibleRole.ROLE_SEPARATOR,
  ]);

  // Roles that are considered interactive when they are focusable.
  const INTERACTIVE_IF_FOCUSABLE_ROLES = new Set([
    ...FOCUSABLE_VALUE_ROLES,
    // If article is focusable, we can assume it is inside a feed.
    Ci.nsIAccessibleRole.ROLE_ARTICLE,
    // Column header can be focusable.
    Ci.nsIAccessibleRole.ROLE_COLUMNHEADER,
    Ci.nsIAccessibleRole.ROLE_GRID_CELL,
    Ci.nsIAccessibleRole.ROLE_MENUBAR,
    Ci.nsIAccessibleRole.ROLE_MENUPOPUP,
    Ci.nsIAccessibleRole.ROLE_PAGETABLIST,
    // Row header can be focusable.
    Ci.nsIAccessibleRole.ROLE_ROWHEADER,
    Ci.nsIAccessibleRole.ROLE_TOOLBAR,
  ]);

  // Roles that are considered form controls.
  const FORM_ROLES = new Set([
    Ci.nsIAccessibleRole.ROLE_CHECKBUTTON,
    Ci.nsIAccessibleRole.ROLE_CHECK_RICH_OPTION,
    Ci.nsIAccessibleRole.ROLE_COMBOBOX,
    Ci.nsIAccessibleRole.ROLE_EDITCOMBOBOX,
    Ci.nsIAccessibleRole.ROLE_ENTRY,
    Ci.nsIAccessibleRole.ROLE_LISTBOX,
    Ci.nsIAccessibleRole.ROLE_PASSWORD_TEXT,
    Ci.nsIAccessibleRole.ROLE_PROGRESSBAR,
    Ci.nsIAccessibleRole.ROLE_RADIOBUTTON,
    Ci.nsIAccessibleRole.ROLE_SLIDER,
    Ci.nsIAccessibleRole.ROLE_SPINBUTTON,
    Ci.nsIAccessibleRole.ROLE_SWITCH,
  ]);

  const DEFAULT_ENV = Object.freeze({
    // Checks that accessible object has at least one accessible action.
    actionCountRule: true,
    // Checks that accessible object (and its corresponding node) is focusable
    // (has focusable state and its node's tabindex is not set to -1).
    focusableRule: true,
    // Checks that clickable accessible object (and its corresponding node) has
    // appropriate interactive semantics.
    ifClickableThenInteractiveRule: true,
    // Checks that accessible object has a role that is considered to be
    // interactive.
    interactiveRule: true,
    // Checks that accessible object has a non-empty label.
    labelRule: true,
    // Checks that a node is enabled and is expected to be enabled via
    // the accessibility API.
    mustBeEnabled: true,
    // Checks that a node has a corresponding accessible object.
    mustHaveAccessibleRule: true,
    // Checks that accessible object (and its corresponding node) have a non-
    // negative tabindex. Platform accessibility API still sets focusable state
    // on tabindex=-1 nodes.
    nonNegativeTabIndexRule: true,
  });

  let gA11YChecks = false;

  let gEnv = {
    ...DEFAULT_ENV,
  };

  // The node the test clicked, while the checks are running on a different
  // node. See assertCanBeClicked.
  let gClickedNode = null;

  // This is set by AccessibilityUtils.init so that we always have a reference
  // to SimpleTest regardless of changes to the global scope.
  let SimpleTest = null;

  /**
   * Get role attribute for an accessible object if specified for its
   * corresponding ``DOMNode``.
   *
   * @param   {nsIAccessible} accessible
   *          Accessible for which to determine its role attribute value.
   *
   * @returns {string}
   *          Role attribute value if specified.
   */
  function getAriaRoles(accessible) {
    try {
      return accessible.attributes.getStringProperty("xml-roles");
    } catch (e) {
      // No xml-roles. nsPersistentProperties throws if the attribute for a key
      // is not found.
    }

    return "";
  }

  /**
   * Get related accessible objects that are targets of labelled by relation e.g.
   * labels.
   *
   * @param   {nsIAccessible} accessible
   *          Accessible objects to get labels for.
   *
   * @returns {Array}
   *          A list of accessible objects that are labels for a given accessible.
   */
  function getLabels(accessible) {
    const relation = accessible.getRelationByType(
      Ci.nsIAccessibleRelation.RELATION_LABELLED_BY
    );
    return [...relation.getTargets().enumerate(Ci.nsIAccessible)];
  }

  /**
   * Test if an accessible has a ``hidden`` attribute.
   *
   * @param  {nsIAccessible} accessible
   *         Accessible object.
   *
   * @return {boolean}
   *         True if the accessible object has a ``hidden`` attribute, false
   *         otherwise.
   */
  function hasHiddenAttribute(accessible) {
    let hidden = false;
    try {
      hidden = accessible.attributes.getStringProperty("hidden");
    } catch (e) {}
    // If the property is missing, error will be thrown
    return hidden && hidden === "true";
  }

  /**
   * Test if an accessible is hidden from the user.
   *
   * @param  {nsIAccessible} accessible
   *         Accessible object.
   *
   * @return {boolean}
   *         True if accessible is hidden from user, false otherwise.
   */
  function isHidden(accessible) {
    if (!accessible) {
      return true;
    }

    while (accessible) {
      if (hasHiddenAttribute(accessible)) {
        return true;
      }

      accessible = accessible.parent;
    }

    return false;
  }

  /**
   * Check if an accessible has a given state.
   *
   * @param  {nsIAccessible} accessible
   *         Accessible object to test.
   * @param  {number} stateToMatch
   *         State to match.
   *
   * @return {boolean}
   *         True if |accessible| has |stateToMatch|, false otherwise.
   */
  function matchState(accessible, stateToMatch) {
    const state = {};
    accessible.getState(state, {});

    return !!(state.value & stateToMatch);
  }

  /**
   * Determine if an accessible has a role that is interactive only while
   * focusable, and is currently focusable.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object for a node.
   */
  function isFocusableValueRole(accessible) {
    return (
      FOCUSABLE_VALUE_ROLES.has(accessible.role) &&
      matchState(accessible, STATE_FOCUSABLE)
    );
  }

  /**
   * Determine if an accessible is a button that is purposefully non-focusable.
   *
   * The Go button in the Url Bar is an example of a purposefully
   * non-focusable image toolbar button that provides an mouse/touch-only
   * control for the search query submission, while a keyboard user could
   * press `Enter` to do it. Similarly, two scroll buttons that appear when
   * toolbar is overflowing, and keyboard-only users would actually scroll
   * tabs in the toolbar while trying to navigate to these controls. When
   * toolbarbuttons are redundant for keyboard users, we do not want to
   * create an extra tab stop for such controls, thus we are expecting the
   * button markup to include `keyNav="false"` attribute to flag it.
   */
  function isNoKeyNavButton(accessible) {
    const node = accessible.DOMNode;
    if (
      !node ||
      !node.documentGlobal ||
      node.getAttribute("keyNav") != "false"
    ) {
      return false;
    }

    const ariaRoles = getAriaRoles(accessible);
    return (
      ariaRoles.includes("button") ||
      accessible.role == Ci.nsIAccessibleRole.ROLE_PUSHBUTTON
    );
  }

  /**
   * Determine if an accessible is a keyboard focusable browser toolbar button.
   * Browser toolbar buttons aren't keyboard focusable in the usual way.
   * Instead, focus is managed by JS code which sets tabindex on a single
   * button at a time. Thus, we need to special case the focusable check for
   * these buttons.
   */
  function isKeyboardFocusableBrowserToolbarButton(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    const toolbar =
      node.closest("toolbar") ||
      node.flattenedTreeParentNode.closest("toolbar");
    if (!toolbar || toolbar.getAttribute("keyNav") != "true") {
      return false;
    }
    return node.documentGlobal.ToolbarKeyboardNavigator._isButton(node);
  }

  /**
   * Determine if an accessible is a keyboard focusable control within a Firefox
   * View list. The main landmark of the Firefox View has role="application" for
   * users to expect a custom keyboard navigation pattern. Controls within this
   * area aren't keyboard focusable in the usual way. Instead, focus is managed
   * by JS code which sets tabindex on a single control within each list at a
   * time. Thus, we need to special case the focusable check for these controls.
   */
  function isKeyboardFocusableFxviewControlInApplication(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    // Firefox View application rows currently include only buttons and links:
    if (
      !node.className.includes("fxview-tab-row-") ||
      (accessible.role != Ci.nsIAccessibleRole.ROLE_PUSHBUTTON &&
        accessible.role != Ci.nsIAccessibleRole.ROLE_LINK)
    ) {
      return false; // Not a button or a link in a Firefox View app.
    }
    // ToDo: We may eventually need to support intervening generics between
    // a list and its listitem here and/or aria-owns lists.
    const listitemAcc = accessible.parent;
    const listAcc = listitemAcc.parent;
    if (
      (!listAcc || listAcc.role != Ci.nsIAccessibleRole.ROLE_LIST) &&
      (!listitemAcc || listitemAcc.role != Ci.nsIAccessibleRole.ROLE_LISTITEM)
    ) {
      return false; // This button/link isn't inside a listitem within a list.
    }
    // All listitems should be not focusable while both a button and a link
    // within each list item might have tabindex="-1".
    if (
      node.tabIndex &&
      matchState(accessible, STATE_FOCUSABLE) &&
      !matchState(listitemAcc, STATE_FOCUSABLE)
    ) {
      // ToDo: We may eventually need to support lists which use aria-owns here.
      // Check that there is only one keyboard reachable control within the list.
      const childCount = listAcc.childCount;
      let foundFocusable = false;
      for (let c = 0; c < childCount; c++) {
        const listitem = listAcc.getChildAt(c);
        const listitemChildCount = listitem.childCount;
        for (let i = 0; i < listitemChildCount; i++) {
          const listitemControl = listitem.getChildAt(i);
          // Use tabIndex rather than a11y focusable state because all controls
          // within the listitem might have tabindex="-1".
          if (listitemControl.DOMNode.tabIndex == 0) {
            if (foundFocusable) {
              // Only one control within a list should be focusable.
              // ToDo: Fine-tune the a11y-check error message generated in this case.
              // Strictly speaking, it's not ideal that we're performing an action
              // from an is function, which normally only queries something without
              // any externally observable behaviour. That said, fixing that would
              // involve different return values for different cases (not a list,
              // too many focusable listitem controls, etc) so we could move the
              // a11yFail call to the caller.
              a11yFail(
                "Only one control should be focusable in a list",
                accessible
              );
              return false;
            }
            foundFocusable = true;
          }
        }
      }
      return foundFocusable;
    }
    return false;
  }

  /**
   * Determine if an accessible is a keyboard focusable option within a listbox.
   * We use it in the Url bar results - these controls are't keyboard focusable
   * in the usual way. Instead, focus is managed by JS code which sets tabindex
   * on a single option at a time. Thus, we need to special case the focusable
   * check for these option items.
   */
  function isKeyboardFocusableOption(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    const urlbarListbox = node.closest(".urlbarView-results");
    if (!urlbarListbox || urlbarListbox.getAttribute("role") != "listbox") {
      return false;
    }
    return node.getAttribute("role") == "option";
  }

  /**
   * Determine if an accessible is a keyboard focusable PanelMultiView control.
   * These controls aren't keyboard focusable in the usual way. Instead, focus
   * is managed by JS code which sets tabindex dynamically. Thus, we need to
   * special case the focusable check for these controls.
   */
  function isKeyboardFocusablePanelMultiViewControl(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    const panelview = node.closest("panelview");
    if (!panelview || panelview.hasAttribute("disablekeynav")) {
      return false;
    }
    return (
      node.documentGlobal.PanelView.forNode(
        panelview
      )._tabNavigableWalker.filter(node) == NodeFilter.FILTER_ACCEPT
    );
  }

  /**
   * Determine if an accessible is a button that is excluded from a focus
   * order, because its adjacent sibling is a focusable spinner. Controls with
   * role="spinbutton" are often placed between two buttons that could
   * increase ("^") or decrease ("v") the value of this spinner. Those buttons
   * are not expected to be focusable, because their functionality for keyboard
   * users is redundant to the spinner. But they are exposed to assistive
   * technology for touch, mouse, switch, and speech-to-text users. Thus, we
   * need to special case the focusable check for these buttons adjacent to
   * a spinner.
   */
  function isKeyboardFocusableSpinbuttonSibling(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }

    // The control itself is a button:
    if (accessible.role != Ci.nsIAccessibleRole.ROLE_PUSHBUTTON) {
      return false;
    }

    // At least one sibling is a keyboard-focusable spinbutton:
    for (const sibling of [
      node.previousElementSibling,
      node.nextElementSibling,
    ]) {
      if (sibling && sibling.tabIndex >= 0 && sibling.role == "spinbutton") {
        return true;
      }
    }
    return false;
  }

  /**
   * Determine if an accessible is a keyboard focusable tab within a tablist.
   * Per the ARIA design pattern, these controls aren't keyboard focusable in
   * the usual way. Instead, focus is managed by JS code which sets tabindex on
   * a single tab at a time. Thus, we need to special case the focusable check
   * for these tab controls.
   */
  function isKeyboardFocusableTabInTablist(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    if (accessible.role != Ci.nsIAccessibleRole.ROLE_PAGETAB) {
      return false; // Not a tab.
    }
    const tablist = findNonGenericParentAccessible(accessible);
    if (!tablist || tablist.role != Ci.nsIAccessibleRole.ROLE_PAGETABLIST) {
      return false; // The tab isn't inside a tablist.
    }
    // ToDo: We may eventually need to support tablists which use
    // aria-activedescendant here.
    // Check that there is only one keyboard reachable tab.
    let foundFocusable = false;
    for (const tab of findNonGenericChildrenAccessible(tablist)) {
      // Allow whitespaces to be included in the tablist for styling purposes
      const isWhitespace =
        tab.role == Ci.nsIAccessibleRole.ROLE_TEXT_LEAF &&
        tab.DOMNode.textContent.trim().length === 0;
      if (tab.role != Ci.nsIAccessibleRole.ROLE_PAGETAB && !isWhitespace) {
        // The tablist includes children other than tabs or whitespaces
        a11yFail("Only tabs should be included in a tablist", accessible);
      }
      // Use tabIndex rather than a11y focusable state because all tabs might
      // have tabindex="-1".
      if (
        tab.DOMNode.tabIndex == 0 &&
        hasFocusableShadowAncestors(tab.DOMNode)
      ) {
        if (foundFocusable) {
          // Only one tab within a tablist should be focusable.
          // ToDo: Fine-tune the a11y-check error message generated in this case.
          // Strictly speaking, it's not ideal that we're performing an action
          // from an is function, which normally only queries something without
          // any externally observable behaviour. That said, fixing that would
          // involve different return values for different cases (not a tab,
          // too many focusable tabs, etc) so we could move the a11yFail call
          // to the caller.
          a11yFail("Only one tab should be focusable in a tablist", accessible);
          return false;
        }
        foundFocusable = true;
      }
    }
    return foundFocusable;
  }

  /**
   * Determine if a node is keyboard focusable by ensuring none of its shadow
   * host ancestors have a negative tabindex.
   *
   * @param {Node} node
   *   The node to check within the shadow tree.
   * @returns {boolean}
   *   `true` if the node is not trapped behind an unfocusable shadow host.
   */
  function hasFocusableShadowAncestors(node) {
    let root = node.getRootNode();
    while (ShadowRoot.isInstance(root)) {
      const host = root.host;
      if (host.hasAttribute("tabindex") && host.tabIndex < 0) {
        return false;
      }
      root = host.getRootNode();
    }
    return true;
  }

  /**
   * Determine if an accessible is a keyboard focusable button in the url bar.
   * Url bar buttons aren't keyboard focusable in the usual way. Instead,
   * focus is managed by JS code which sets tabindex on a single button at a
   * time. Thus, we need to special case the focusable check for these buttons.
   * This also applies to the search bar buttons that reuse the same pattern.
   */
  function isKeyboardFocusableUrlbarButton(accessible) {
    const node = accessible.DOMNode;
    if (!node || !node.documentGlobal) {
      return false;
    }
    const isUrlBar =
      node
        .closest(".urlbarView > .search-one-offs")
        ?.getAttribute("disabletab") == "true";
    const isSearchBar =
      node
        .closest("#PopupSearchAutoComplete > .search-one-offs")
        ?.getAttribute("is_searchbar") == "true";
    return (
      (isUrlBar || isSearchBar) &&
      node.getAttribute("tabindex") == "-1" &&
      node.tagName == "button" &&
      node.classList.contains("searchbar-engine-one-off-item")
    );
  }

  /**
   * Determine if an accessible is a keyboard focusable XUL tab.
   * Only one tab is focusable at a time, but after focusing it, you can use
   * the keyboard to focus other tabs.
   */
  function isKeyboardFocusableXULTab(accessible) {
    const node = accessible.DOMNode;
    return node && XULElement.isInstance(node) && node.tagName == "tab";
  }

  /**
   * The gridcells are not expected to be interactive and focusable
   * individually, but it is allowed to manually manage focus within the grid
   * per ARIA Grid pattern (https://www.w3.org/WAI/ARIA/apg/patterns/grid/).
   * Example of such grid would be a datepicker where one gridcell can be
   * selected and the focus is moved with arrow keys once the user tabbed into
   * the grid. In grids like a calendar, only one element would be included in
   * the focus order and the rest of grid cells may not have an interactive
   * accessible created. We need to special case the check for these gridcells.
   */
  function isAccessibleGridcell(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const accessible = getAccessible(node);

    if (!accessible || accessible.role != Ci.nsIAccessibleRole.ROLE_GRID_CELL) {
      return false; // Not a grid cell.
    }
    // ToDo: We may eventually need to support intervening generics between
    // a grid cell and its grid container here.
    const gridRow = accessible.parent;
    if (!gridRow || gridRow.role != Ci.nsIAccessibleRole.ROLE_ROW) {
      return false; // The grid cell isn't inside a row.
    }
    let grid = gridRow.parent;
    if (!grid) {
      return false; // The grid cell isn't inside a grid.
    }
    if (grid.role == Ci.nsIAccessibleRole.ROLE_GROUPING) {
      // Grid built on the HTML table may include <tbody> wrapper:
      grid = grid.parent;
      if (!grid || grid.role != Ci.nsIAccessibleRole.ROLE_GRID) {
        return false; // The grid cell isn't inside a grid.
      }
    }
    // Check that there is only one keyboard reachable grid cell.
    let foundFocusable = false;
    for (const gridCell of grid.DOMNode.querySelectorAll(
      "td, [role=gridcell]"
    )) {
      // Grid cells are not expected to have a "tabindex" attribute and to be
      // included in the focus order, with the exception of the only one cell
      // that is included in the page tab sequence to provide access to the grid.
      if (gridCell.tabIndex == 0) {
        if (foundFocusable) {
          // Only one grid cell within a grid should be focusable.
          // ToDo: Fine-tune the a11y-check error message generated in this case.
          // Strictly speaking, it's not ideal that we're performing an action
          // from an is function, which normally only queries something without
          // any externally observable behaviour. That said, fixing that would
          // involve different return values for different cases (not a grid
          // cell, too many focusable grid cells, etc) so we could move the
          // a11yFail call to the caller.
          a11yFail(
            "Only one grid cell should be focusable in a grid",
            accessible
          );
          return false;
        }
        foundFocusable = true;
      }
    }
    return foundFocusable;
  }

  /**
   * XUL treecol elements currently aren't focusable, making them inaccessible.
   * For now, we don't flag these as a failure to avoid breaking multiple tests.
   * ToDo: We should remove this exception after this is fixed in bug 1848397.
   */
  function isInaccessibleXulTreecol(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const listheader = node.flattenedTreeParentNode;
    if (listheader.tagName !== "listheader" || node.tagName !== "treecol") {
      return false;
    }
    return true;
  }

  /**
   * Determine if a DOM node is a combobox container of the url bar. We
   * intentionally leave this element unlabeled, because its child is a search
   * input that is the target and main control of this component. In general, we
   * want to avoid duplication in the label announcement when a user focuses the
   * input. Both NVDA and VO ignore the label on at least one of these controls
   * if both have a label. But the bigger concern here is that it's very
   * difficult to keep the accessible name synchronized between the combobox and
   * the input. Thus, we need to special case the label check for this control.
   */
  function isUnlabeledUrlBarCombobox(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    let ariaRole = node.getAttribute("role");
    // <searchbar> is the only case of this pattern.
    const isSearchbar = node.tagName == "searchbar" && node.id == "searchbar";
    return isSearchbar && ariaRole == "combobox";
  }

  /**
   * Determine if a DOM node is an option within the url bar. We know each
   * url bar option is accessible, but it disappears as soon as it is clicked
   * during tests and the a11y-checks do not have time to test the label,
   * because the Fluent localization is not yet completed by then. Thus, we
   * need to special case the label check for these controls.
   */
  function isUnlabeledUrlBarOption(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const role = getAccessible(node)?.role;
    const isOption =
      node.tagName == "span" &&
      node.getAttribute("role") == "option" &&
      node.classList.contains("urlbarView-row-inner");
    const isMenuItem =
      node.tagName == "menuitem" &&
      role == Ci.nsIAccessibleRole.ROLE_MENUITEM &&
      node.classList.contains("urlbarView-result-menuitem");
    // Not all options have "data-l10n-id" attributes in the URL Bar, because
    // some of options are autocomplete options based on the user input and
    // they are not expected to be localized.
    return isOption || isMenuItem;
  }

  /**
   * Determine if a DOM node is a menuitem within the XUL menu. We know each
   * menuitem is accessible, but it disappears as soon as it is clicked during
   * tests and the a11y-checks do not have time to test the label, because the
   * Fluent localization is not yet completed by then. Thus, we need to special
   * case the label check for these controls.
   */
  function isUnlabeledMenuitem(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const hasLabel = node.querySelector("label, description");
    const isMenuItem =
      node.getAttribute("role") == "menuitem" ||
      (node.tagName == "richlistitem" &&
        node.classList.contains("autocomplete-richlistitem")) ||
      (node.tagName == "menuitem" &&
        node.classList.contains("urlbarView-result-menuitem"));

    let parentNode = node.getRootNode().host ?? node.parentNode;
    const isParentMenu =
      parentNode.getAttribute("role") == "menu" ||
      (parentNode.tagName == "richlistbox" &&
        parentNode.classList.contains("autocomplete-richlistbox")) ||
      (parentNode.tagName == "menupopup" &&
        parentNode.classList.contains("urlbarView-result-menu"));
    return (
      isMenuItem &&
      isParentMenu &&
      hasLabel &&
      (node.hasAttribute("data-l10n-id") || node.tagName == "richlistitem")
    );
  }

  /**
   * Determine if the node is a "Show All" or one of image buttons on the
   * about:config page, or a "X" close button on moz-message-bar. We know these
   * buttons are accessible, but they disappear/are replaced as soon as they
   * are clicked during tests and the a11y-checks do not have time to test the
   * label, because the Fluent localization is not yet completed by then.
   * Thus, we need to special case the label check for these controls.
   */
  function isUnlabeledImageButton(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const isShowAllButton = node.id == "show-all";
    const isReplacedImageButton =
      node.classList.contains("button-add") ||
      node.classList.contains("button-delete") ||
      node.classList.contains("button-reset");
    const isCloseMozMessageBarButton =
      node.classList.contains("close") &&
      node.getAttribute("data-l10n-id") == "moz-message-bar-close-button";
    return (
      node.tagName.toLowerCase() == "button" &&
      node.hasAttribute("data-l10n-id") &&
      (isShowAllButton || isReplacedImageButton || isCloseMozMessageBarButton)
    );
  }

  /**
   * Determine if a node is a XUL:button on a prompt popup. We know this button
   * is accessible, but it disappears as soon as it is clicked during tests and
   * the a11y-checks do not have time to test the label, because the Fluent
   * localization is not yet completed by then. Thus, we need to special case
   * the label check for these controls.
   */
  function isUnlabeledXulButton(node) {
    if (!node || !node.documentGlobal) {
      return false;
    }
    const hasLabel = node.querySelector("label, xul\\:label");
    const isButton =
      node.getAttribute("role") == "button" ||
      node.tagName == "button" ||
      node.tagName == "xul:button";
    return isButton && hasLabel && node.hasAttribute("data-l10n-id");
  }

  /**
   * Determine if a node is a XUL element for which tabIndex should be ignored.
   * Some XUL elements report -1 for the .tabIndex property, even though they
   * are in fact keyboard focusable.
   */
  function shouldIgnoreTabIndex(node) {
    if (!XULElement.isInstance(node)) {
      return false;
    }
    return node.tagName == "label" && node.getAttribute("is") == "text-link";
  }

  /**
   * Determine if accessible is focusable with the keyboard.
   *
   * @param   {nsIAccessible} accessible
   *          Accessible for which to determine if it is keyboard focusable.
   *
   * @returns {boolean}
   *          True if focusable with the keyboard.
   */
  function isKeyboardFocusable(accessible) {
    if (
      isKeyboardFocusableBrowserToolbarButton(accessible) ||
      isKeyboardFocusableOption(accessible) ||
      isKeyboardFocusablePanelMultiViewControl(accessible) ||
      isKeyboardFocusableUrlbarButton(accessible) ||
      isKeyboardFocusableXULTab(accessible) ||
      isKeyboardFocusableTabInTablist(accessible) ||
      isKeyboardFocusableFxviewControlInApplication(accessible) ||
      isKeyboardFocusableSpinbuttonSibling(accessible)
    ) {
      return true;
    }
    // State will be focusable even if the tabindex is negative.
    const node = accessible.DOMNode;
    const role = accessible.role;
    return (
      matchState(accessible, STATE_FOCUSABLE) &&
      // Platform accessibility will still report STATE_FOCUSABLE even with the
      // tabindex="-1" so we need to check that it is >= 0 to be considered
      // keyboard focusable.
      (!gEnv.nonNegativeTabIndexRule ||
        node.tabIndex > -1 ||
        node.closest('[aria-activedescendant][tabindex="0"]') ||
        // If an ARIA toolbar uses a roving tabindex, some controls on the
        // toolbar might not currently be focusable even though they can be
        // reached with arrow keys and become focusable at that point.
        ((role == Ci.nsIAccessibleRole.ROLE_PUSHBUTTON ||
          role == Ci.nsIAccessibleRole.ROLE_TOGGLE_BUTTON) &&
          node.closest('[role="toolbar"]')) ||
        // <moz-radio-group> and <moz-visual-picker> also use a roving tabindex.
        (role === Ci.nsIAccessibleRole.ROLE_RADIOBUTTON &&
          node.getRootNode().host?.localName === "moz-radio") ||
        (role === Ci.nsIAccessibleRole.ROLE_RADIOBUTTON &&
          node.getRootNode().host?.localName === "moz-visual-picker-item") ||
        // Sidebar lists also use a roving tabindex.
        ((role === Ci.nsIAccessibleRole.ROLE_SUMMARY ||
          role === Ci.nsIAccessibleRole.ROLE_OUTLINEITEM) &&
          node.getRootNode().host?.localName === "sidebar-bookmark-list") ||
        (role === Ci.nsIAccessibleRole.ROLE_OUTLINEITEM &&
          node.getRootNode().host?.localName === "moz-card" &&
          node.getRootNode().host.getRootNode().host?.localName ===
            "sidebar-history") ||
        shouldIgnoreTabIndex(node))
    );
  }

  function describeNode({ id, tagName, className }) {
    return `id: ${id}, tagName: ${tagName}, className: ${className}`;
  }

  function buildMessage(message, DOMNode) {
    if (DOMNode) {
      message += `: ${describeNode(DOMNode)}`;
      if (gClickedNode) {
        message +=
          `. The checks fell back to this node from the one the test ` +
          `clicked: ${describeNode(gClickedNode)}`;
      }
    }

    return message;
  }

  /**
   * Fail a test with a given message because of an issue with a given
   * accessible object. This is used for cases where there's an actual
   * accessibility failure that prevents UI from being accessible to keyboard/AT
   * users.
   *
   * @param {string} message
   * @param {nsIAccessible} accessible
   *        Accessible to log along with the failure message.
   */
  function a11yFail(message, { DOMNode }) {
    SimpleTest.ok(false, buildMessage(message, DOMNode));
  }

  /**
   * Find the closest popup ancestor of an accessible, if the accessible is
   * invisible because that popup is not open.
   *
   * A popup that is still opening keeps its accessibles: the accessibility
   * service only skips the subtree once it is closed or hiding. They are
   * invisible and not focusable, which is why this is reached from the
   * focusable check and not from the "not accessible" one.
   *
   * @param {nsIAccessible} accessible
   * @returns {?Element} the popup, or null if there is none, it is open, or the
   *   accessible is visible.
   */
  function notOpenPopupAncestor(accessible) {
    if (!matchState(accessible, STATE_INVISIBLE)) {
      return null;
    }

    const popup = ClickChecks.popupAncestor(accessible.DOMNode);
    return popup && popup.state != "open" ? popup : null;
  }

  /**
   * Log a todo statement with a given message because of an issue with a given
   * accessible object. This is used for cases where accessibility best
   * practices are not followed or for something that is not as severe to be
   * considered a failure.
   *
   * @param {string} message
   * @param {nsIAccessible} accessible
   *        Accessible to log along with the todo message.
   */
  function a11yWarn(message, { DOMNode }) {
    SimpleTest.todo(false, buildMessage(message, DOMNode));
  }

  /**
   * Test if the node's unavailable via the accessibility API.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object.
   */
  function assertEnabled(accessible) {
    if (gEnv.mustBeEnabled && matchState(accessible, STATE_UNAVAILABLE)) {
      a11yFail(
        "Node expected to be enabled but is disabled via the accessibility API",
        accessible
      );
    }
  }

  /**
   * Test if it is possible to focus on a node with the keyboard. This method
   * also checks for additional keyboard focus issues that might arise.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object for a node.
   */
  function assertFocusable(accessible) {
    if (
      gEnv.mustBeEnabled &&
      gEnv.focusableRule &&
      !isNoKeyNavButton(accessible) &&
      !isKeyboardFocusable(accessible)
    ) {
      const ariaRoles = getAriaRoles(accessible);
      // Do not force ARIA combobox or listbox to be focusable.
      if (!ariaRoles.includes("combobox") && !ariaRoles.includes("listbox")) {
        const notOpenPopup = notOpenPopupAncestor(accessible);
        a11yFail(
          notOpenPopup
            ? `Node is inside ${ClickChecks.describePopup(notOpenPopup)}, ` +
                `so it is not focusable`
            : "Node is not focusable via the accessibility API",
          accessible
        );
      }

      return;
    }

    if (!INTERACTIVE_IF_FOCUSABLE_ROLES.has(accessible.role)) {
      // ROLE_TABLE is used for grids too which are considered interactive.
      if (
        accessible.role === Ci.nsIAccessibleRole.ROLE_TABLE &&
        !getAriaRoles(accessible).includes("grid")
      ) {
        a11yWarn(
          "Focusable nodes should have interactive semantics",
          accessible
        );

        return;
      }
    }

    if (accessible.DOMNode.tabIndex > 0) {
      a11yWarn("Avoid using tabindex attribute greater than zero", accessible);
    }
  }

  /**
   * Test if it is possible to interact with a node via the accessibility API.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object for a node.
   */
  function assertInteractive(accessible) {
    const focusableValueRole = isFocusableValueRole(accessible);

    if (
      gEnv.mustBeEnabled &&
      gEnv.actionCountRule &&
      accessible.actionCount === 0 &&
      !focusableValueRole
    ) {
      a11yFail("Node does not support any accessible actions", accessible);

      return;
    }

    if (
      gEnv.mustBeEnabled &&
      gEnv.interactiveRule &&
      !INTERACTIVE_ROLES.has(accessible.role) &&
      !focusableValueRole
    ) {
      if (
        // Labels that have a label for relation with their target are clickable.
        (accessible.role !== Ci.nsIAccessibleRole.ROLE_LABEL ||
          accessible.getRelationByType(
            Ci.nsIAccessibleRelation.RELATION_LABEL_FOR
          ).targetsCount === 0) &&
        // Images that are inside an anchor (have linked state).
        (accessible.role !== Ci.nsIAccessibleRole.ROLE_GRAPHIC ||
          !matchState(accessible, STATE_LINKED))
      ) {
        // Look for click action in the list of actions.
        for (let i = 0; i < accessible.actionCount; i++) {
          if (
            gEnv.ifClickableThenInteractiveRule &&
            accessible.getActionName(i) === CLICK_ACTION
          ) {
            a11yFail(
              "Clickable nodes must have interactive semantics",
              accessible
            );
          }
        }
      }

      a11yFail(
        "Node does not have a correct interactive role and may not be " +
          "manipulated via the accessibility API",
        accessible
      );
    }
  }

  /**
   * Test if the node is labelled appropriately for accessibility API.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object for a node.
   */
  function assertLabelled(accessible, allowRecurse = true) {
    const { DOMNode } = accessible;
    let name = accessible.name;
    if (!name) {
      // If text has just been inserted into the tree, the a11y engine might not
      // have picked it up yet.
      forceRefreshDriverTick(DOMNode);
      try {
        name = accessible.name;
      } catch (e) {
        // The Accessible died because the DOM node was removed or hidden.
        if (gEnv.labelRule) {
          // Some elements disappear as soon as they are clicked during tests,
          // their accessible dies before the Fluent localization is completed.
          // We want to exclude these groups of nodes from the label check.
          // Note: In other cases, this first block isn't necessarily hit
          // because Fluent isn't finished yet. This might happen if a text
          // node was inserted (whether by Fluent or something else) but a11y
          // hasn't picked it up yet, but the node gets hidden before a11y
          // can pick it up.
          if (
            isUnlabeledUrlBarOption(DOMNode) ||
            isUnlabeledMenuitem(DOMNode) ||
            isUnlabeledImageButton(DOMNode)
          ) {
            return;
          }
          a11yWarn("Unlabeled element removed before l10n finished", {
            DOMNode,
          });
        }
        return;
      }
      const doc = DOMNode.ownerDocument;
      if (
        !name &&
        allowRecurse &&
        gEnv.labelRule &&
        doc.hasPendingL10nMutations
      ) {
        // There are pending async l10n mutations which might result in a valid
        // accessible name. Try this check again once l10n is finished.
        doc.addEventListener(
          "L10nMutationsFinished",
          () => {
            try {
              accessible.name;
            } catch (e) {
              // The Accessible died because the DOM node was removed or hidden.
              if (
                isUnlabeledUrlBarOption(DOMNode) ||
                isUnlabeledImageButton(DOMNode) ||
                isUnlabeledXulButton(DOMNode)
              ) {
                return;
              }
              a11yWarn("Unlabeled element removed before l10n finished", {
                DOMNode,
              });
              return;
            }
            assertLabelled(accessible, false);
          },
          { once: true }
        );
        return;
      }
    }
    if (name) {
      name = name.trim();
    }
    if (gEnv.labelRule && !name) {
      // The URL and Search Bar comboboxes are purposefully unlabeled,
      // since they include labeled inputs that are receiving focus.
      // Or the Accessible died because the DOM node was removed or hidden.
      if (
        isUnlabeledUrlBarCombobox(DOMNode) ||
        isUnlabeledUrlBarOption(DOMNode)
      ) {
        return;
      }
      a11yFail("Interactive elements must be labeled", accessible);

      return;
    }

    if (FORM_ROLES.has(accessible.role)) {
      const labels = getLabels(accessible);
      const hasNameFromVisibleLabel = labels.some(
        label => !matchState(label, STATE_INVISIBLE)
      );

      if (!hasNameFromVisibleLabel) {
        a11yWarn("Form elements should have a visible text label", accessible);
      }
    } else if (
      accessible.role === Ci.nsIAccessibleRole.ROLE_LINK &&
      DOMNode.nodeName === "AREA" &&
      DOMNode.hasAttribute("href")
    ) {
      const alt = DOMNode.getAttribute("alt");
      if (alt && alt.trim() !== name) {
        a11yFail(
          "Use alt attribute to label area elements that have the href attribute",
          accessible
        );
      }
    }
  }

  /**
   * Test if the node's visible via accessibility API.
   *
   * @param {nsIAccessible} accessible
   *        Accessible object for a node.
   */
  function assertVisible(accessible) {
    if (isHidden(accessible)) {
      a11yFail(
        "Node is not currently visible via the accessibility API and may not " +
          "be manipulated by it",
        accessible
      );
    }
  }

  /**
   * Walk node ancestry and force refresh driver tick in every document.
   *
   * @param {DOMNode} node
   *        Node for traversing the ancestry.
   */
  function forceRefreshDriverTick(node) {
    const wins = [];
    let bc = BrowsingContext.getFromWindow(node.ownerDocument.defaultView); // eslint-disable-line
    while (bc) {
      wins.push(bc.associatedWindow);
      bc = bc.embedderWindowGlobal?.browsingContext;
    }

    let win = wins.pop();
    while (win) {
      // Stop the refresh driver from doing its regular ticks and force two
      // refresh driver ticks: first to let layout update and notify a11y,  and
      // the second to let a11y process updates.
      win.windowUtils.advanceTimeAndRefresh(100);
      win.windowUtils.advanceTimeAndRefresh(100);
      // Go back to normal refresh driver ticks.
      win.windowUtils.restoreNormalRefresh();
      win = wins.pop();
    }
  }

  /**
   * Get an accessible object for a node.
   * Note: this method will not resolve if accessible object does not become
   * available for a given node.
   *
   * @param  {DOMNode} node
   *         Node to get the accessible object for.
   *
   * @return {nsIAccessible}
   *         Accessibility object for a given node.
   */
  function getAccessible(node) {
    const accessibilityService = Cc[
      "@mozilla.org/accessibilityService;1"
    ].getService(Ci.nsIAccessibilityService);
    if (!accessibilityService) {
      // This is likely a build with --disable-accessibility
      return null;
    }

    let acc = accessibilityService.getAccessibleFor(node);
    if (acc) {
      return acc;
    }

    // Force refresh tick throughout document hierarchy
    forceRefreshDriverTick(node);
    return accessibilityService.getAccessibleFor(node);
  }

  /**
   * Find the nearest interactive accessible ancestor for a node.
   */
  function findInteractiveAccessible(node) {
    let acc;
    // Walk DOM ancestors until we find one with an accessible.
    for (; node && !acc; node = node.flattenedTreeParentNode) {
      acc = getAccessible(node);
    }
    if (!acc) {
      // No accessible ancestor.
      return acc;
    }
    // Walk a11y ancestors until we find one which is interactive.
    for (; acc; acc = acc.parent) {
      const relation = acc.getRelationByType(
        Ci.nsIAccessibleRelation.RELATION_LABEL_FOR
      );
      if (
        acc.role === Ci.nsIAccessibleRole.ROLE_LABEL &&
        relation.targetsCount > 0
      ) {
        // If a <label> was clicked to activate a radiobutton or a checkbox,
        // return the accessible of the related input.
        // Note: aria-labelledby doesn't give the node a role of label, so this
        // won't work for aria-labelledby cases. That said, aria-labelledby also
        // doesn't have implicit click behaviour either and there's not really
        // any way we can check for that.
        const targetAcc = relation.getTarget(0);
        return targetAcc;
      }
      if (INTERACTIVE_ROLES.has(acc.role) || isFocusableValueRole(acc)) {
        return acc;
      }
    }
    // No interactive ancestor.
    return null;
  }

  /**
   * Find the nearest non-generic ancestor for a node to account for generic
   * containers to intervene between the ancestor and it child.
   */
  function findNonGenericParentAccessible(childAcc) {
    for (let acc = childAcc.parent; acc; acc = acc.parent) {
      if (acc.computedARIARole != "generic") {
        return acc;
      }
    }
    return null;
  }

  /**
   * Find the nearest non-generic children for a node to account for generic
   * containers to intervene between the ancestor and its children.
   */
  function* findNonGenericChildrenAccessible(parentAcc) {
    const count = parentAcc.childCount;
    for (let c = 0; c < count; ++c) {
      const child = parentAcc.getChildAt(c);
      // When Gecko will consider only one role as generic, we'd use child.role
      if (child.computedARIARole == "generic") {
        yield* findNonGenericChildrenAccessible(child);
      } else {
        yield child;
      }
    }
  }

  /**
   * Determine if a node is in a row of a toolkit autocomplete popup. These
   * popups act on mouseup and close before the click event, so the row has to
   * be checked on mousedown, while the popup is still open.
   */
  function isInAutocompleteRow(node) {
    // Not closest(): the target is in autocomplete-row-item's shadow DOM.
    for (; node; node = node.flattenedTreeParentNode) {
      if (node.tagName == "richlistitem") {
        return node.parentNode.classList.contains("autocomplete-richlistbox");
      }
    }
    return false;
  }

  function runIfA11YChecks(task) {
    return (...args) => (gA11YChecks ? task(...args) : null);
  }

  /**
   * AccessibilityUtils provides utility methods for retrieving accessible objects
   * and performing accessibility related checks.
   * Current methods:
   *   assertCanBeClicked
   *   setEnv
   *   resetEnv
   *
   */
  const AccessibilityUtils = {
    assertCanBeClicked(node) {
      if (node.frameLoader?.isRemoteFrame) {
        // The click landed in a document of another process, which is what
        // the user clicks, not the frame element itself. Remote documents
        // aren't supported by these checks.
        a11yWarn("Unable to perform a11y checks in a remote document", {
          DOMNode: node,
        });
        return;
      }
      // Click events might fire on an inaccessible or non-interactive
      // descendant, even if the test author targeted them at an interactive
      // element. For example, if there's a button with an image inside it,
      // node might be the image.
      let acc = findInteractiveAccessible(node);
      if (!acc) {
        // Force refresh tick to find interactive accessible
        forceRefreshDriverTick(node);
        acc = findInteractiveAccessible(node);
      }

      if (!acc) {
        if (isAccessibleGridcell(node) || isInaccessibleXulTreecol(node)) {
          return;
        }
        if (gEnv.mustHaveAccessibleRule) {
          a11yFail("Node is not accessible via accessibility API", {
            DOMNode: node,
          });
        }

        return;
      }

      // acc is not necessarily the node the test clicked. Remember that node
      // so that buildMessage can name it: a failure reported on a node the
      // test never touched is otherwise hard to make sense of.
      if (acc.DOMNode != node) {
        gClickedNode = node;
      }

      try {
        assertInteractive(acc);
        assertFocusable(acc);
        assertVisible(acc);
        assertEnabled(acc);
        assertLabelled(acc);
      } finally {
        gClickedNode = null;
      }
    },

    setEnv(env = DEFAULT_ENV) {
      gEnv = {
        ...DEFAULT_ENV,
        ...env,
      };
    },

    resetEnv() {
      gEnv = { ...DEFAULT_ENV };
    },

    reset(a11yChecks = false, testPath = "") {
      gA11YChecks = a11yChecks;

      const { Services } = SpecialPowers;
      // Disable accessibility service if it is running and if a11y checks are
      // disabled. However, don't do this for accessibility engine tests.
      if (
        !gA11YChecks &&
        Services.appinfo.accessibilityEnabled &&
        !testPath.startsWith("chrome://mochitests/content/browser/accessible/")
      ) {
        Services.prefs.setIntPref(FORCE_DISABLE_ACCESSIBILITY_PREF, 1);
        Services.prefs.clearUserPref(FORCE_DISABLE_ACCESSIBILITY_PREF);
      }

      // Reset accessibility environment flags that might've been set within the
      // test.
      this.resetEnv();
    },

    init(simpleTest) {
      this._shouldHandleClicks = true;
      // A top level xul window's DocShell doesn't have a chromeEventHandler
      // attribute. In that case, the chrome event handler is just the global
      // window object.
      this._handler ??=
        window.docShell.chromeEventHandler ?? window.docShell.domWindow;
      this._handler.addEventListener("mousedown", this, true, true);
      this._handler.addEventListener("click", this, true, true);
      SimpleTest = simpleTest;
    },

    uninit() {
      this._handler?.removeEventListener("mousedown", this, true);
      this._handler?.removeEventListener("click", this, true);
      this._handler = null;
      this._checkedOnMousedown = null;
      SimpleTest = null;
    },

    /**
     * Suppress (or disable suppression of) handling of captured click events.
     * This should only be called by EventUtils, etc. when a click event will
     * be generated but we know it is not actually a click intended to activate
     * a control; e.g. drag/drop. Tests that wish to disable specific checks
     * should use setEnv instead.
     */
    suppressClickHandling(shouldSuppress) {
      this._shouldHandleClicks = !shouldSuppress;
    },

    handleEvent({ type, button, composedTarget }) {
      if (!this._shouldHandleClicks) {
        return;
      }
      if (type == "mousedown") {
        this._checkedOnMousedown = null;
        if (button != 0 || !isInAutocompleteRow(composedTarget)) {
          return;
        }
        this._checkedOnMousedown = composedTarget;
      } else if (composedTarget == this._checkedOnMousedown) {
        this._checkedOnMousedown = null;
        return;
      }
      if (composedTarget.tagName.toLowerCase() == "slot") {
        // The click occurred on a text node inside a slot. Since events don't
        // target text nodes, the event was retargeted to the slot. However, a
        // slot isn't itself rendered. To deal with this, use the slot's parent
        // instead.
        composedTarget = composedTarget.flattenedTreeParentNode;
      }
      const bounds =
        composedTarget.documentGlobal?.windowUtils?.getBoundsWithoutFlushing(
          composedTarget
        );
      if (bounds && (bounds.width == 0 || bounds.height == 0)) {
        // Some tests click hidden nodes. These clearly aren't testing the UI
        // for the node itself (and presumably there is a test somewhere else
        // that does). Therefore, we can't (and shouldn't) do a11y checks.
        a11yWarn(
          "handleEvent() was unable to perform a11y checks on hidden node",
          { DOMNode: composedTarget }
        );
        return;
      }
      this.assertCanBeClicked(composedTarget);
    },
  };

  AccessibilityUtils.assertCanBeClicked = runIfA11YChecks(
    AccessibilityUtils.assertCanBeClicked.bind(AccessibilityUtils)
  );

  AccessibilityUtils.setEnv = runIfA11YChecks(
    AccessibilityUtils.setEnv.bind(AccessibilityUtils)
  );

  AccessibilityUtils.resetEnv = runIfA11YChecks(
    AccessibilityUtils.resetEnv.bind(AccessibilityUtils)
  );

  return AccessibilityUtils;
})();
