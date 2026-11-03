/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "verticalTabsEnabled",
  "sidebar.verticalTabs"
);

/**
 * What each `sidebar.visibility` value means for the launcher. The values are
 * orientation-exclusive, so a mode implies the tab orientation it belongs to.
 *
 * @typedef {object} VisibilityMode
 *
 * @property {"always" | "user" | "never"} launcherVisibility
 *   What governs whether the launcher shows: it always shows, it is the user's
 *   to show and hide with the toolbar button, or it never shows. This also
 *   says what the toolbar button does in this mode - toggle the launcher's
 *   expanded state, toggle the launcher, or toggle the panel.
 * @property {boolean} expandable
 *   Whether the launcher has an expanded state at all. Only vertical tabs do.
 * @property {boolean} [userVisibleOnEnter]
 *   Where the user's choice starts when this mode is entered. Only meaningful
 *   for modes whose launcher is the user's to control.
 * @property {boolean} [expandedOnEnter]
 *   Whether the launcher is expanded when this mode is entered.
 * @property {boolean} [userVisibleWhenUnknown]
 *   What restored state that says nothing about the launcher means. Left unset
 *   by the modes that would rather keep the launcher as the window has it.
 */

/** @type {{[visibility: string]: VisibilityMode}} */
const VISIBILITY_MODES = Object.freeze({
  "always-show": {
    launcherVisibility: "always",
    expandable: true,
    expandedOnEnter: true,
  },
  "expand-on-hover": { launcherVisibility: "always", expandable: true },
  "hide-sidebar": {
    launcherVisibility: "user",
    expandable: true,
    userVisibleOnEnter: false,
    // Restored state that says nothing about the launcher means hidden here:
    // in this mode a hidden launcher is the setting the user picked, so it has
    // to survive a restart even from a profile that never saved it
    // (bug 2065431). The other modes leave the launcher as this window has it.
    userVisibleWhenUnknown: false,
  },
  "hide-on-close": {
    launcherVisibility: "user",
    expandable: false,
    userVisibleOnEnter: true,
  },
  "hide-launcher": { launcherVisibility: "never", expandable: false },
});

const DEFAULT_VISIBILITY_MODE = VISIBILITY_MODES["always-show"];

/**
 * The properties that make up a sidebar's UI state.
 *
 * @typedef {object} SidebarStateProps
 *
 * @property {string} command
 *   The id of the current sidebar panel. The panel may be closed and still have a command value.
 *   Re-opening the sidebar panel will then load the current command id.
 * @property {boolean} panelOpen
 *   Whether there is an open panel.
 * @property {number} panelWidth
 *   Current width of the sidebar panel.
 * @property {boolean} userLauncherVisible
 *   Whether the user has the launcher showing. Only the visibility modes that
 *   put the launcher under the user's control read this; the others derive
 *   the launcher's visibility from the mode alone. Read `launcherVisible` for
 *   whether the launcher is actually showing.
 * @property {boolean} launcherExpanded
 *   Whether the launcher is expanded.
 *   When sidebar.visibility pref value is "always-show", the toolbar button serves to toggle this property
 * @property {boolean} launcherDragActive
 *   Whether the launcher is currently being dragged.
 * @property {boolean} pinnedTabsDragActive
 *   Whether the pinned tabs container is currently being dragged.
 * @property {boolean} toolsDragActive
 *   Whether the tools container is currently being dragged.
 * @property {boolean} launcherHoverActive
 *   Whether the launcher is currently being hovered.
 * @property {number} launcherWidth
 *   Current width of the sidebar launcher.
 * @property {number} expandedLauncherWidth
 *   Width of the expanded launcher
 * @property {number} pinnedTabsHeight
 *   Current height of the pinned tabs container
 * @property {number} expandedPinnedTabsHeight
 *   Height of the pinned tabs container when the sidebar is expanded
 * @property {number} collapsedPinnedTabsHeight
 *   Height of the pinned tabs container when the sidebar is collapsed
 * @property {number} toolsHeight
 *   Current height of the tools container
 * @property {number} expandedToolsHeight
 *   Height of the tools container when the sidebar is expanded
 * @property {number} collapsedToolsHeight
 *   Height of the tools container when the sidebar is collapsed
 */

const LAUNCHER_MINIMUM_WIDTH = 100;

const LEGACY_USED_PREF = "sidebar.old-sidebar.has-used";
const REVAMP_USED_PREF = "sidebar.new-sidebar.has-used";

/**
 * A reactive data store for the sidebar's UI state. Similar to Lit's
 * ReactiveController, any updates to properties can potentially trigger UI
 * updates, or updates to other properties.
 */
export class SidebarState {
  #controller = null;
  /** @type {SidebarStateProps} */
  #props = {
    ...SidebarState.defaultProperties,
  };
  bookmarksExpandedFolders = [];
  #navToolboxCollapsed = false;

  /** @type {SidebarStateProps} */
  static defaultProperties = Object.freeze({
    command: "",
    launcherDragActive: false,
    launcherExpanded: false,
    launcherHoverActive: false,
    panelOpen: false,
    pinnedTabsDragActive: false,
    toolsDragActive: false,
    userLauncherVisible: false,
  });

  /**
   * Construct a new SidebarState.
   *
   * @param {SidebarController} controller
   *   The controller this state belongs to. SidebarState is instantiated
   *   per-window, thereby allowing us to retrieve DOM elements attached to
   *   the controller.
   */
  constructor(controller) {
    this.#controller = controller;
    this.revampEnabled = controller.sidebarRevampEnabled;
    this.revampVisibility = controller.sidebarRevampVisibility;

    if (this.revampEnabled) {
      this.#props.userLauncherVisible = this.defaultLauncherVisible;
    }
  }

  /**
   * The rules the current `sidebar.visibility` value implies.
   *
   * @returns {VisibilityMode}
   */
  get #visibilityMode() {
    return VISIBILITY_MODES[this.revampVisibility] ?? DEFAULT_VISIBILITY_MODE;
  }

  /**
   * Get the sidebar launcher.
   *
   * @returns {HTMLElement}
   */
  get #launcherEl() {
    return this.#controller.sidebarMain;
  }

  /**
   * Get parent element of the sidebar launcher.
   *
   * @returns {XULElement}
   */
  get #launcherContainerEl() {
    return this.#controller.sidebarContainer;
  }

  /**
   * Get the splitter sibling for the sidebar launcher.
   *
   * @returns {XULElement}
   */
  get #launcherSplitterEl() {
    return this.#controller.launcherSplitter;
  }

  /**
   * Get the sidebar panel element.
   *
   * @returns {XULElement}
   */
  get #sidebarBoxEl() {
    return this.#controller._box;
  }

  /**
   * Get the sidebar panel.
   *
   * @returns {XULElement}
   */
  get #panelEl() {
    return this.#controller._box;
  }

  /**
   * Get the pinned tabs container element.
   *
   * @returns {XULElement}
   */
  get #pinnedTabsContainerEl() {
    return this.#controller._pinnedTabsContainer;
  }

  /**
   * Get the items-wrapper part of the pinned tabs container element.
   *
   * @returns {XULElement}
   */
  get #pinnedTabsItemsWrapper() {
    return this.#pinnedTabsContainerEl?.shadowRoot?.querySelector(
      "[part=items-wrapper]"
    );
  }

  /**
   * Get the tools container element.
   *
   * @returns {XULElement}
   */
  get #toolsContainer() {
    return this.#controller.sidebarMain?.buttonsWrapper;
  }

  /**
   * Get the tools button-group element.
   *
   * @returns {XULElement}
   */
  get #toolsButtonGroup() {
    return this.#controller.sidebarMain?.buttonGroup;
  }

  /**
   * Get window object from the controller.
   */
  get #controllerGlobal() {
    return this.#launcherContainerEl.documentGlobal;
  }

  /**
   * Update the starting properties according to external factors such as
   * window type and user preferences.
   */
  initializeState(showLauncher = this.defaultLauncherVisible) {
    if (!this.#controller.inSingleTabWindow) {
      if (lazy.verticalTabsEnabled) {
        this.#props.launcherExpanded = true;
      }
      this.#props.userLauncherVisible = showLauncher;
    }
    this.applyLauncherVisibility();

    // Explicitly trigger effects to ensure that the UI is kept up to date.
    this.launcherExpanded = this.#props.launcherExpanded;
  }

  /**
   * Load the state information given by session store, backup state, or
   * adopted window.
   *
   * @param {SidebarStateProps} props
   *   New properties to overwrite the default state with.
   */
  loadCurrentState(props) {
    if (props.hasOwnProperty("hidden")) {
      props.userLauncherVisible = !props.hidden;
      delete props.hidden;
    }
    // We need to keep this for backwards compatibility since a previously saved
    // session might have this property, see bug 2066086.
    if (props.hasOwnProperty("launcherVisible")) {
      props.userLauncherVisible = props.launcherVisible;
      delete props.launcherVisible;
    }

    props.userLauncherVisible ??= this.#visibilityMode.userVisibleWhenUnknown;

    for (const [key, value] of Object.entries(props)) {
      if (value === undefined) {
        // `undefined` means we should use the default value.
        continue;
      }
      switch (key) {
        case "command":
          this.command = value;
          break;
        case "panelWidth":
          this.#panelEl.style.width = `${value}px`;
          break;
        case "width":
          this.#panelEl.style.width = value;
          break;
        case "expanded":
          this.launcherExpanded = value;
          break;
        case "panelOpen":
          // we need to know if we have a command value before finalizing panelOpen
          break;
        case "pinnedTabsHeight":
        case "toolsHeight":
          this.#props[key] = value;
          break;
        default:
          this[key] = value;
      }
    }

    if (this.command && !props.hasOwnProperty("panelOpen")) {
      // legacy state saved before panelOpen was a thing
      props.panelOpen = true;
    }
    if (!this.command) {
      props.panelOpen = false;
    }

    this.panelOpen = !!props.panelOpen;
    if (this.command && this.panelOpen) {
      // show() is async, so make sure we return its promise here
      return this.#controller.showInitially(this.command, "restored");
    }
    return this.#controller.hide();
  }

  /**
   * Toggle the value of a boolean property.
   *
   * @param {string} key
   *   The property to toggle.
   */
  toggle(key) {
    if (Object.hasOwn(this.#props, key)) {
      this[key] = !this[key];
    }
  }

  /**
   * Serialize the state properties for persistence in session store or prefs.
   *
   * @returns {SidebarStateProps}
   */
  getProperties() {
    const props = {
      command: this.command,
      panelOpen: this.panelOpen,
      panelWidth: this.panelWidth,
      bookmarksExpandedFolders: this.bookmarksExpandedFolders,
      launcherWidth: convertToInt(this.launcherWidth),
      expandedLauncherWidth: convertToInt(this.expandedLauncherWidth),
      launcherExpanded: this.launcherExpanded,
      userLauncherVisible: this.userLauncherVisible,
      pinnedTabsHeight: this.pinnedTabsHeight,
      expandedPinnedTabsHeight: this.expandedPinnedTabsHeight,
      collapsedPinnedTabsHeight: this.collapsedPinnedTabsHeight,
      toolsHeight: this.toolsHeight,
      expandedToolsHeight: this.expandedToolsHeight,
      collapsedToolsHeight: this.collapsedToolsHeight,
    };
    // omit any properties with undefined values'
    for (let [key, value] of Object.entries(props)) {
      if (value === undefined) {
        delete props[key];
      }
    }
    return props;
  }

  get panelOpen() {
    return this.#props.panelOpen;
  }

  set panelOpen(open) {
    if (this.#props.panelOpen == open) {
      return;
    }
    this.#props.panelOpen = !!open;
    // Opening a panel never changes whether the launcher is showing: in the
    // modes where it can be hidden, that is the user's choice to make. The
    // box's padding does depend on both, though.
    this.#updatePanelPadding();
    if (open) {
      Services.prefs.setBoolPref(
        this.revampEnabled ? REVAMP_USED_PREF : LEGACY_USED_PREF,
        true
      );
    }

    const mainEl = this.#controller.sidebarContainer;
    const boxEl = this.#controller._box;
    const contentAreaEl =
      this.#controllerGlobal.document.getElementById("tabbrowser-tabbox");
    if (mainEl?.toggleAttribute) {
      mainEl.toggleAttribute("sidebar-panel-open", open);
    }
    boxEl.toggleAttribute("sidebar-panel-open", open);
    contentAreaEl.toggleAttribute("sidebar-panel-open", open);
    this.#controller.requestMaxWidthUpdate();
  }

  get panelWidth() {
    // Use the value from `style`. This is a more accurate user preference, as
    // opposed to what the resize observer gives us.
    return convertToInt(this.#panelEl?.style.width);
  }

  set panelWidth(width) {
    this.#controller.requestMaxWidthUpdate();
  }

  get expandedPinnedTabsHeight() {
    return this.#props.expandedPinnedTabsHeight;
  }

  set expandedPinnedTabsHeight(height) {
    this.#props.expandedPinnedTabsHeight = height;
    this.updatePinnedTabsHeight();
  }

  get collapsedPinnedTabsHeight() {
    return this.#props.collapsedPinnedTabsHeight;
  }

  set collapsedPinnedTabsHeight(height) {
    this.#props.collapsedPinnedTabsHeight = height;
    this.updatePinnedTabsHeight();
  }

  get expandedToolsHeight() {
    return this.#props.expandedToolsHeight;
  }

  set expandedToolsHeight(height) {
    this.#props.expandedToolsHeight = height;
    this.updateToolsHeight();
  }

  get collapsedToolsHeight() {
    return this.#props.collapsedToolsHeight;
  }

  set collapsedToolsHeight(height) {
    this.#props.collapsedToolsHeight = height;
    this.updateToolsHeight();
  }

  /**
   * Whether the launcher shows in this visibility mode when the user hasn't
   * chosen otherwise. The modes that put the launcher under the user's control
   * start hidden; the rest always show it.
   *
   * @returns {boolean}
   */
  get defaultLauncherVisible() {
    return (
      this.revampEnabled && this.#visibilityMode.launcherVisibility === "always"
    );
  }

  /**
   * Whether the launcher should stay hidden while a panel is open. This is the
   * case in horizontal-tabs "hide-launcher" mode, where the launcher is replaced
   * by the panel header dropdown: the toolbar button then toggles only the
   * panel and the launcher remains hidden until the user leaves that mode.
   *
   * @returns {boolean}
   */
  get launcherHiddenWithPanel() {
    return this.#visibilityMode.launcherVisibility === "never";
  }

  /**
   * Whether the launcher has an expanded state in this visibility mode. Only
   * vertical tabs do; with horizontal tabs the launcher is always collapsed.
   *
   * @returns {boolean}
   */
  get launcherExpandable() {
    return this.#visibilityMode.expandable;
  }

  /**
   * Whether the toolbar button toggles the launcher's expanded state rather
   * than the launcher itself (or, where the launcher never shows, the panel).
   *
   * @returns {boolean}
   */
  get toolbarButtonTogglesExpanded() {
    return this.#visibilityMode.launcherVisibility === "always";
  }

  /**
   * Whether the launcher is showing. Derived: the visibility mode decides,
   * consulting the user's choice only in the modes that are theirs to control.
   * Set `userLauncherVisible` to act on the user's behalf.
   *
   * @returns {boolean}
   */
  get launcherVisible() {
    if (!this.revampEnabled || this.#controller.inSingleTabWindow) {
      // Neither the legacy sidebar nor a popup window has a launcher.
      return false;
    }
    switch (this.#visibilityMode.launcherVisibility) {
      case "always":
        return true;
      case "never":
        return false;
      default:
        return !!this.#props.userLauncherVisible;
    }
  }

  get userLauncherVisible() {
    return this.#props.userLauncherVisible;
  }

  /**
   * Show or hide the launcher on the user's behalf. This is what the toolbar
   * button, the launcher's "Hide sidebar" context menu item and restored
   * session state write; it has no effect in the modes where the launcher
   * always or never shows.
   *
   * @param {boolean} visible
   */
  set userLauncherVisible(visible) {
    this.#props.userLauncherVisible = !!visible;
    this.applyLauncherVisibility();
  }

  /**
   * Adopt a new `sidebar.visibility` value. Entering a mode where the launcher
   * is the user's to control starts that choice from the mode's own default,
   * which is how picking "Hide tabs and sidebar" hides the launcher.
   *
   * @param {string} visibility
   */
  enterVisibilityMode(visibility) {
    this.revampVisibility = visibility;
    const mode = this.#visibilityMode;
    if (mode.launcherVisibility === "user") {
      this.#props.userLauncherVisible = mode.userVisibleOnEnter;
    }
    this.applyLauncherVisibility();
    this.launcherExpanded = !!mode.expandedOnEnter;
  }

  /**
   * Bring the DOM in line with `launcherVisible`. Call this after changing
   * anything the getter derives from: the user's choice, the visibility mode,
   * or whether the revamped sidebar is enabled at all.
   */
  applyLauncherVisibility() {
    const visible = this.launcherVisible;
    this.#launcherContainerEl.hidden = !visible;
    this.#launcherSplitterEl.hidden = !visible;
    if (visible) {
      this.#controller._enableLauncherDragging();
    } else {
      this.#controller._disableLauncherDragging();
    }
    if (this.revampEnabled) {
      this.#launcherEl?.requestUpdate();
    }
    this.#updateTabbrowser(visible);
    this.#updatePanelPadding();
  }

  /**
   * A panel shown without a launcher beside it needs padding of its own.
   */
  #updatePanelPadding() {
    this.#sidebarBoxEl.style.paddingInlineStart =
      this.panelOpen && !this.launcherVisible ? "var(--space-small)" : "unset";
  }

  get launcherExpanded() {
    return this.#props.launcherExpanded;
  }

  set launcherExpanded(expanded) {
    if (!this.revampEnabled) {
      // Launcher not supported in legacy sidebar.
      this.#props.launcherExpanded = false;
      return;
    }
    const previousExpanded = this.#props.launcherExpanded;
    this.#props.launcherExpanded = expanded;
    this.#launcherEl.expanded = expanded;
    if (expanded && !previousExpanded) {
      Glean.sidebar.expand.record();
    }
    // Marking the tab container element as expanded or not simplifies the CSS logic
    // and selectors considerably.
    const { tabContainer } = this.#controllerGlobal.gBrowser;
    const mainEl = this.#controller.sidebarContainer;
    const boxEl = this.#controller._box;
    const contentAreaEl =
      this.#controllerGlobal.document.getElementById("tabbrowser-tabbox");
    tabContainer.toggleAttribute("expanded", expanded);
    if (mainEl?.toggleAttribute) {
      mainEl.toggleAttribute("sidebar-launcher-expanded", expanded);
    }
    this.#launcherSplitterEl?.toggleAttribute(
      "sidebar-launcher-expanded",
      expanded
    );
    boxEl?.toggleAttribute("sidebar-launcher-expanded", expanded);
    contentAreaEl.toggleAttribute("sidebar-launcher-expanded", expanded);
    this.#controller.updateToolbarButton();
    if (!this.launcherDragActive) {
      this.#updateLauncherWidth();
    }
    if (
      !this.pinnedTabsDragActive &&
      this.#controller.sidebarRevampVisibility !== "expand-on-hover"
    ) {
      this.updatePinnedTabsHeight();
    }
    this.handleUpdateToolsHeightOnLauncherExpanded();
  }

  get launcherDragActive() {
    return this.#props.launcherDragActive;
  }

  set launcherDragActive(active) {
    this.#props.launcherDragActive = active;
    if (active) {
      // Temporarily disable expand on hover functionality while dragging
      if (this.#controller.sidebarRevampVisibility === "expand-on-hover") {
        this.#controller.toggleExpandOnHover(false);
      }

      this.#launcherEl.toggleAttribute("customWidth", true);
    } else {
      this.launcherWidth =
        this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
          this.#launcherContainerEl
        ).width;

      // Re-enable expand on hover if necessary
      if (this.#controller.sidebarRevampVisibility === "expand-on-hover") {
        this.#controller.toggleExpandOnHover(true, true);
      }

      if (this.launcherWidth < LAUNCHER_MINIMUM_WIDTH) {
        // Snap back to collapsed state when the new width is too narrow.
        this.launcherExpanded = false;
        if (this.revampVisibility === "hide-sidebar") {
          // Dragging the launcher shut is the user hiding it. Only this mode
          // does so: with horizontal tabs a narrow drag just collapses.
          this.userLauncherVisible = false;
        }
      } else {
        this.launcherExpanded = true;
        // Store the user-preferred launcher width.
        this.expandedLauncherWidth = this.launcherWidth;
      }
    }
    const rootEl = this.#controllerGlobal.document.documentElement;
    rootEl.toggleAttribute("sidebar-launcher-drag-active", active);
  }

  get pinnedTabsDragActive() {
    return this.#props.pinnedTabsDragActive;
  }

  set pinnedTabsDragActive(active) {
    this.#props.pinnedTabsDragActive = active;

    let itemsWrapperHeight =
      this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
        this.#pinnedTabsItemsWrapper
      ).height;
    let pinnedTabsContainerHeight =
      this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
        this.#pinnedTabsContainerEl
      ).height;
    if (!active) {
      // Dragging to the full content height resets to auto-sizing rather than
      // storing a height that only reflects the current layout.
      const atMaxHeight = itemsWrapperHeight - pinnedTabsContainerHeight < 1;
      this.pinnedTabsHeight = atMaxHeight ? 0 : pinnedTabsContainerHeight;
      // Store the user-preferred pinned tabs height.
      if (this.#props.launcherExpanded) {
        this.expandedPinnedTabsHeight = this.pinnedTabsHeight;
      } else {
        this.collapsedPinnedTabsHeight = this.pinnedTabsHeight;
      }
    }
  }

  get toolsDragActive() {
    return this.#props.toolsDragActive;
  }

  set toolsDragActive(active) {
    this.#props.toolsDragActive = active;
    let maxToolsHeight = this.maxToolsHeight;
    if (!active && this.#toolsContainer) {
      let buttonGroupHeight =
        this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
          this.#toolsContainer
        ).height;
      this.toolsHeight =
        buttonGroupHeight > maxToolsHeight ? maxToolsHeight : buttonGroupHeight;
      if (
        buttonGroupHeight > maxToolsHeight &&
        this.#controller.sidebarRevampVisibility !== "expand-on-hover"
      ) {
        this.#launcherEl.shouldShowOverflowButton = false;
      }
      // Store the user-preferred tools height.
      if (this.#props.launcherExpanded) {
        this.expandedToolsHeight = this.toolsHeight;
      } else {
        this.collapsedToolsHeight = this.toolsHeight;
      }
    }
  }

  get maxToolsHeight() {
    const FIRST_LAST_TAB_PADDING = 5.8833;
    if (!this.#toolsButtonGroup) {
      return null;
    }
    let referenceToolButton;
    if (this.#toolsButtonGroup.children.length > 1) {
      referenceToolButton = this.#toolsButtonGroup.children[1];
    } else {
      referenceToolButton = this.#toolsButtonGroup.children[0];
    }
    let toolRect =
      this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
        referenceToolButton
      );
    let extraPadding = 0;
    if (this.#toolsButtonGroup.children.length >= 3) {
      extraPadding = FIRST_LAST_TAB_PADDING * 2;
    } else if (this.#toolsButtonGroup.children.length === 2) {
      extraPadding = FIRST_LAST_TAB_PADDING;
    }
    return this.#props.launcherExpanded
      ? "unset"
      : toolRect.height * this.#toolsButtonGroup.children.length + extraPadding;
  }

  get launcherHoverActive() {
    return this.#props.launcherHoverActive;
  }

  set launcherHoverActive(active) {
    this.#props.launcherHoverActive = active;
  }

  get launcherWidth() {
    return this.#props.launcherWidth;
  }

  set launcherWidth(width) {
    this.#props.launcherWidth = width;
    const { document } = this.#controllerGlobal;
    if (!document.documentElement.hasAttribute("inDOMFullscreen")) {
      // Expand the launcher when it gets wide enough.
      if (this.launcherDragActive) {
        this.launcherExpanded = width >= LAUNCHER_MINIMUM_WIDTH;
      }
      this.#controller.requestMaxWidthUpdate();
    }
  }

  get expandedLauncherWidth() {
    return this.#props.expandedLauncherWidth;
  }

  set expandedLauncherWidth(width) {
    this.#props.expandedLauncherWidth = width;
    this.#updateLauncherWidth();
  }

  /**
   * If the sidebar is expanded, resize the launcher to the user-preferred
   * width (if available). If it is collapsed, reset the launcher width.
   */
  #updateLauncherWidth() {
    if (this.launcherExpanded && this.expandedLauncherWidth) {
      this.#launcherContainerEl.style.width = `${this.expandedLauncherWidth}px`;
    } else if (!this.launcherExpanded) {
      this.#launcherContainerEl.style.width = "";
    }
    this.#launcherEl.toggleAttribute(
      "customWidth",
      !!this.expandedLauncherWidth
    );
  }

  get pinnedTabsHeight() {
    return this.#props.pinnedTabsHeight;
  }

  set pinnedTabsHeight(height) {
    this.#props.pinnedTabsHeight = height;
    if (this.launcherExpanded && lazy.verticalTabsEnabled) {
      this.expandedPinnedTabsHeight = height;
    } else if (lazy.verticalTabsEnabled) {
      this.collapsedPinnedTabsHeight = height;
    }
  }

  get toolsHeight() {
    return this.#props.toolsHeight;
  }

  set toolsHeight(height) {
    this.#props.toolsHeight = height;
    if (this.launcherExpanded) {
      this.expandedToolsHeight = height;
    } else {
      this.collapsedToolsHeight = height;
    }
  }

  /**
   * When the sidebar is expanded/collapsed, resize the pinned tabs container to the user-preferred
   * height (if available).
   */
  updatePinnedTabsHeight() {
    if (!this.#pinnedTabsContainerEl || this.pinnedTabsDragActive) {
      return;
    }
    if (!lazy.verticalTabsEnabled) {
      this.#pinnedTabsContainerEl.style.height = "";
      return;
    }
    const preferredHeight = this.launcherExpanded
      ? this.expandedPinnedTabsHeight
      : this.collapsedPinnedTabsHeight;
    if (!preferredHeight) {
      // Nothing stored for this state, so clear any height left over from the
      // other state and let the container size itself to its contents.
      this.#pinnedTabsContainerEl.style.height = "";
      return;
    }
    const itemsWrapper = this.#pinnedTabsItemsWrapper;
    const itemsWrapperHeight = itemsWrapper
      ? this.#controllerGlobal.windowUtils.getBoundsWithoutFlushing(
          itemsWrapper
        ).height
      : 0;
    if (!itemsWrapperHeight) {
      // The pinned tabs have no layout to clamp the stored height to, so leave
      // the container's height alone until they do.
      return;
    }
    // Clamp for display only, never overwriting the user's saved preference.
    this.#pinnedTabsContainerEl.style.height = `${Math.min(
      preferredHeight,
      itemsWrapperHeight
    )}px`;
  }

  /**
   * When the sidebar is expanded or collapsed, resize the tools container to the expected height.
   */
  handleUpdateToolsHeightOnLauncherExpanded() {
    if (!this.toolsDragActive) {
      if (this.#controller.sidebarRevampVisibility !== "expand-on-hover") {
        this.updateToolsHeight();
      } else if (this.#toolsContainer) {
        this.#toolsContainer.style.height = this.#props.launcherExpanded
          ? ""
          : "0";
      }
    }
  }

  /**
   * Resize the tools container to the user-preferred height (if available).
   */
  updateToolsHeight() {
    if (this.#toolsContainer) {
      if (!lazy.verticalTabsEnabled) {
        this.#toolsContainer.style.height = "";
        return;
      }

      if (
        this.launcherExpanded &&
        this.#props.expandedToolsHeight !== undefined
      ) {
        this.#toolsContainer.style.height = `${this.#props.expandedToolsHeight}px`;
      } else if (
        !this.launcherExpanded &&
        this.#props.collapsedToolsHeight !== undefined
      ) {
        this.#toolsContainer.style.height = `${this.#props.collapsedToolsHeight}px`;
      } else if (
        (this.launcherExpanded &&
          this.#props.expandedToolsHeight === undefined) ||
        (!this.launcherExpanded &&
          this.#props.collapsedToolsHeight === undefined)
      ) {
        this.#toolsContainer.style.height = "";
      }
    }
  }

  #updateTabbrowser(isSidebarShown) {
    const doc = this.#controllerGlobal.document;
    const tabbox = doc.getElementById("tabbrowser-tabbox");
    if (!tabbox || !doc.documentElement) {
      return;
    }
    tabbox.toggleAttribute(
      "sidebar-shown",
      isSidebarShown && !this.#navToolboxCollapsed
    );
  }

  get navToolboxCollapsed() {
    return this.#navToolboxCollapsed;
  }

  set navToolboxCollapsed(val) {
    if (this.#navToolboxCollapsed === val) {
      return;
    }
    this.#navToolboxCollapsed = val;
    this.#updateTabbrowser(this.launcherVisible);
  }

  get command() {
    return this.#props.command || "";
  }

  set command(id) {
    if (id && !this.#controller.sidebars.has(id)) {
      throw new Error("Setting command to an invalid value");
    }
    if (id && id !== this.#props.command) {
      this.#props.command = id;
      // We need the attribute to mirror the command property as its used as a CSS hook
      this.#controller._box.setAttribute("sidebarcommand", id);
    } else if (!id) {
      delete this.#props.command;
      this.#controller._box.setAttribute("sidebarcommand", "");
    }
  }
}

/**
 * Convert a value to an integer.
 *
 * @param {string} value
 *   The value to convert.
 * @returns {number}
 *   The resulting integer, or `undefined` if it's not a number.
 */
function convertToInt(value) {
  const intValue = parseInt(value);
  if (isNaN(intValue)) {
    return undefined;
  }
  return intValue;
}
