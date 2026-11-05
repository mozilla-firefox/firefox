/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The tab strip's custom elements, as the modules that drive them see them.
//
// Projects outside browser/components/tabbrowser reach these by naming this
// file in their tsconfig `include`.

type MozTabbrowserTab = import("../content/tab.mjs").MozTabbrowserTab;

type MozTabbrowserTabs = import("../content/tabs.mjs").MozTabbrowserTabs;

type MozTabbrowserTabGroup =
  import("../content/tabgroup.mjs").MozTabbrowserTabGroup;

interface MozTabbrowserTabGroupLabel extends XULElement {
  // Constant, as on the tab group the label stands in for. The label is a plain
  // element with no class of its own, so tabgroup.mjs assigns all four.
  pinned: false;
  splitview: null;

  container: MozTabbrowserTabs;
  group: MozTabbrowserTabGroup;

  // Assigned by the tab strip.
  elementIndex: number;
}

type MozTabSplitViewWrapper =
  import("../content/tabsplitview.mjs").MozTabSplitViewWrapper;

// What a split view contributes to session state, as its `state` getter builds
// it and sessionstore stores it.
type TabSplitViewStateData =
  import("../content/tabsplitview.mjs").TabSplitViewStateData;

// toolkit/content/widgets/findbar.js, which tsc cannot see. Declares only the
// members tabbrowser uses.
interface MozFindbar extends XULElement {
  browser: MozBrowser;
  readonly _findField: HTMLInputElement;
  readonly FIND_NORMAL: number;
  findMode: number;
  close(noAnim?: boolean): void;
  onFindCommand(): Promise<void>;
}

// toolkit/content/widgets/toolbarbutton.js, which tsc cannot see. Declares
// only the members tabbrowser uses.
interface MozToolbarbutton extends XULElement {
  readonly menupopup: XULPopupElement | null;
  readonly icon: Element | null;
  disabled: boolean;
}

// toolkit/content/widgets/moz-checkbox, whose Lit reactive properties tsc
// cannot see. Declares only the members tabbrowser uses.
interface MozCheckbox extends HTMLElement {
  label: string;
  iconSrc: string;
  checked: boolean;
}

// toolkit/content/widgets/arrowscrollbox.js, which tsc cannot see. Declares
// only the members tabbrowser uses.
interface MozArrowScrollbox extends XULElement {
  readonly overflowing: boolean;
  smoothScroll: boolean;
  readonly scrollClientRect: DOMRect;
  readonly _scrollButtonDown: Element;
  _getScrollableElements(): Element[];
  _canScrollToElement(element: Element): boolean;
  ensureElementIsVisible(element: Element, aInstant?: boolean): void;
  scrollByPixels(aPixels: number, aInstant?: boolean): void;
}
