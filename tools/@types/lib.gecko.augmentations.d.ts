/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
export {};

interface MozElementBase {
  new (): Element;
}

declare global {
  const MozElements: Readonly<{
    MozElementMixin<T extends MozElementBase>(base: T): T;
    TabsBase: typeof TabsBase;
    MozTab: typeof MozTab;
  }>;

  class MozXULElement extends XULElement implements MozElementBase {
    static implementCustomInterface(cls: MozElementBase, ifaces: nsIID[]): void;
    static readonly fragment: DocumentFragment;
    initializeAttributeInheritance(): void;
    attributeChangedCallback(
      name: string,
      oldValue: string | null,
      newValue: string | null
    ): void;
  }
  class MozHTMLElement extends HTMLElement implements MozElementBase {
    static implementCustomInterface(cls: MozElementBase, ifaces: nsIID[]): void;
  }

  // toolkit/content/widgets/tabbox.js. Declares only the <tabbox> members that
  // a <tabs> subclass reads.
  interface MozTabbox extends MozXULElement {
    readonly tabpanels: XULElement;
  }

  // toolkit/content/widgets/tabbox.js, with MozElements.BaseControl's two
  // members folded in. Declares the members that a <tabs> subclass and its
  // consumers reach; add one when it becomes an error.
  class TabsBase extends MozXULElement {
    disabled: boolean;
    tabIndex: number;
    selectedIndex: number;
    readonly switchByScrolling: boolean;
    get selectedItem(): MozTab | null;
    set selectedItem(val: MozTab | null);
    get tabbox(): MozTabbox;
    // A subclass can mix non-tab items into its focus order, as the doc
    // comment on advanceSelectedItem describes.
    get ariaFocusableItems(): XULElement[];
    get ariaFocusedIndex(): number;
    get ariaFocusedItem(): XULElement | null;
    set ariaFocusedItem(val: XULElement | null);
    baseConnect(): void;
    updateWheelListeners(): void;
    advanceSelectedTab(aDir?: -1 | 1, aWrap?: boolean, aEvent?: Event): void;
    advanceSelectedItem(aDir?: -1 | 1, aWrap?: boolean): void;
    _selectNewTab(
      aNewTab: MozTab,
      aFallbackDir?: -1 | 1,
      aWrap?: boolean
    ): void;
    // Generic so that `filter` receives, and the method returns, the same tab
    // type as the call site's `startTab`.
    findNextTab<T extends MozTab>(
      startTab: T,
      opts?: {
        direction?: number;
        wrap?: boolean;
        startWithAdjacent?: boolean;
        filter?: (tab: T) => boolean;
      }
    ): T | null;
  }

  // toolkit/content/widgets/tabbox.js, with MozElements.BaseText's label and
  // image folded in. Declares only the MozTab members that code outside the
  // class uses on a <tab> subclass. When tsc reports a MozTab member as
  // missing, declare that member here.
  class MozTab extends MozXULElement {
    readonly selected: boolean;
    linkedPanel: string;
    label: string;
    get image(): string | null;
    set image(val: string);
    on_mousedown(event: MouseEvent): void;
  }

  type MozBrowser =
    import("../../toolkit/content/widgets/browser-custom-element.mjs").MozBrowser;
}
