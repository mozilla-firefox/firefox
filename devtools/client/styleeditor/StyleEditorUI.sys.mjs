/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  loader,
  require,
} from "resource://devtools/shared/loader/Loader.sys.mjs";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");

import {
  getString,
  text,
  showFilePicker,
  optionsPopupMenu,
} from "resource://devtools/client/styleeditor/StyleEditorUtil.sys.mjs";
import { StyleSheetEditor } from "resource://devtools/client/styleeditor/StyleSheetEditor.sys.mjs";

const { PrefObserver } = require("resource://devtools/client/shared/prefs.js");

const KeyShortcuts = require("resource://devtools/client/shared/key-shortcuts.js");
const {
  shortSource,
} = require("resource://devtools/shared/inspector/css-logic.js");

const lazy = {};

loader.lazyRequireGetter(
  lazy,
  "KeyCodes",
  "resource://devtools/client/shared/keycodes.js",
  true
);

loader.lazyRequireGetter(
  lazy,
  "OriginalSource",
  "resource://devtools/client/styleeditor/original-source.js",
  true
);

ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  LocalModeMappings:
    "resource://devtools/client/framework/LocalModeMappings.sys.mjs",
});
loader.lazyRequireGetter(
  lazy,
  "ResponsiveUIManager",
  "resource://devtools/client/responsive/manager.js"
);
loader.lazyRequireGetter(
  lazy,
  "openContentLink",
  "resource://devtools/client/shared/link.js",
  true
);
loader.lazyRequireGetter(
  lazy,
  "copyString",
  "resource://devtools/shared/platform/clipboard.js",
  true
);

const LOAD_ERROR = "error-load";
const PREF_AT_RULES_SIDEBAR = "devtools.styleeditor.showAtRulesSidebar";
const PREF_SIDEBAR_WIDTH = "devtools.styleeditor.atRulesSidebarWidth";
const PREF_NAV_WIDTH = "devtools.styleeditor.navSidebarWidth";
const PREF_ORIG_SOURCES = "devtools.source-map.client-service.enabled";

const FILTERED_CLASSNAME = "splitview-filtered";
const ALL_FILTERED_CLASSNAME = "splitview-all-filtered";

const HTML_NS = "http://www.w3.org/1999/xhtml";

/**
 * StyleEditorUI is controls and builds the UI of the Style Editor, including
 * maintaining a list of editors for each stylesheet on a debuggee.
 *
 * Emits events:
 *   'editor-added': A new editor was added to the UI
 *   'editor-selected': An editor was selected
 *   'error': An error occured
 *
 */
export class StyleEditorUI extends EventEmitter {
  #activeSummary = null;
  #commands;
  #contextMenu;
  #contextMenuStyleSheet;
  #copyUrlItem;
  #cssProperties;
  #filter;
  #filterInput;
  #filterInputClearButton;
  #loadingStyleSheets;
  #nav;
  #openLinkNewTabItem;
  #optionsButton;
  #optionsMenu;
  #panelDoc;
  #prefObserver;
  #prettyPrintButton;
  #root;
  #seenSheets = new Map();
  #shortcuts;
  #side;
  #sourceMapPrefObserver;
  #styleSheetBoundToSelect;
  #styleSheetToSelect;
  /**
   * Maps keyed by summary element whose value is an object containing:
   * - {Element} details: The associated details element (i.e. container for CodeMirror)
   * - {StyleSheetEditor} editor: The associated editor, for easy retrieval
   */
  #summaryDataMap = new WeakMap();
  #toolbox;
  #tplDetails;
  #tplSummary;
  #uiAbortController = new AbortController();
  #window;

  /**
   * @param {Toolbox} toolbox
   * @param {object} commands Object defined from devtools/shared/commands to interact with the devtools backend
   * @param {Document} panelDoc
   *        Document of the toolbox panel to populate UI in.
   * @param {CssProperties} A css properties database.
   */
  constructor(toolbox, commands, panelDoc, cssProperties) {
    super();

    this.#toolbox = toolbox;
    this.#commands = commands;
    this.#panelDoc = panelDoc;
    this.#cssProperties = cssProperties;
    this.#window = this.#panelDoc.defaultView;
    this.#root = this.#panelDoc.getElementById("style-editor-chrome");

    this.editors = [];
    this.selectedEditor = null;
    this.savedLocations = {};

    this.#prefObserver = new PrefObserver("devtools.styleeditor.");
    this.#prefObserver.on(
      PREF_AT_RULES_SIDEBAR,
      this.#onAtRulesSidebarPrefChanged
    );
    this.#sourceMapPrefObserver = new PrefObserver(
      "devtools.source-map.client-service."
    );
    this.#sourceMapPrefObserver.on(
      PREF_ORIG_SOURCES,
      this.#onOrigSourcesPrefChanged
    );
  }

  get cssProperties() {
    return this.#cssProperties;
  }

  get currentTarget() {
    return this.#commands.targetCommand.targetFront;
  }

  /*
   * Index of selected stylesheet in document.styleSheets
   */
  get selectedStyleSheetIndex() {
    return this.selectedEditor
      ? this.selectedEditor.styleSheet.styleSheetIndex
      : -1;
  }

  /**
   * Initiates the style editor ui creation, and start to track TargetCommand updates.
   *
   * @param {object} options
   * @param {object} options.stylesheetToSelect
   * @param {StyleSheetResource} options.stylesheetToSelect.stylesheet
   * @param {Integer} options.stylesheetToSelect.line
   * @param {Integer} options.stylesheetToSelect.column
   */
  async initialize(options = {}) {
    this.createUI();

    if (options.stylesheetToSelect) {
      const { stylesheet, line, column } = options.stylesheetToSelect;
      // If a stylesheet resource and its location was passed (e.g. user clicked on a stylesheet
      // location in the rule view), we can directly add it to the list and select it
      // before watching for resources, for improved performance.
      if (stylesheet.resourceId) {
        try {
          await this.#handleStyleSheetResource(stylesheet);
          await this.selectStyleSheet(
            stylesheet,
            line - 1,
            column ? column - 1 : 0
          );
        } catch (e) {
          console.error(e);
        }
      }
    }

    await this.#commands.resourceCommand.watchResources(
      [this.#commands.resourceCommand.TYPES.DOCUMENT_EVENT],
      { onAvailable: this.#onResourceAvailable }
    );
    await this.#commands.targetCommand.watchTargets({
      types: [this.#commands.targetCommand.TYPES.FRAME],
      onAvailable: this.#onTargetAvailable,
      onDestroyed: this.#onTargetDestroyed,
    });

    this.#startLoadingStyleSheets();
    await this.#commands.resourceCommand.watchResources(
      [this.#commands.resourceCommand.TYPES.STYLESHEET],
      {
        onAvailable: this.#onResourceAvailable,
        onUpdated: this.#onResourceUpdated,
        onDestroyed: this.#onResourceDestroyed,
      }
    );
    await this.#waitForLoadingStyleSheets();
  }

  /**
   * Build the initial UI and wire buttons with event handlers.
   */
  createUI() {
    this.#filterInput = this.#root.querySelector(".devtools-filterinput");
    this.#filterInputClearButton = this.#root.querySelector(
      ".devtools-searchinput-clear"
    );
    this.#nav = this.#root.querySelector(".splitview-nav");
    this.#side = this.#root.querySelector(".splitview-side-details");
    this.#tplSummary = this.#root.querySelector(
      "#splitview-tpl-summary-stylesheet"
    );
    this.#tplDetails = this.#root.querySelector(
      "#splitview-tpl-details-stylesheet"
    );

    const eventListenersConfig = { signal: this.#uiAbortController.signal };

    // Add click event on the "new stylesheet" button in the toolbar and on the
    // "append a new stylesheet" link (visible when there are no stylesheets).
    for (const el of this.#root.querySelectorAll(".style-editor-newButton")) {
      el.addEventListener(
        "click",
        async () => {
          const stylesheetsFront =
            await this.currentTarget.getFront("stylesheets");
          stylesheetsFront.addStyleSheet(null);
          this.#clearFilterInput();
        },
        eventListenersConfig
      );
    }

    this.#root.querySelector(".style-editor-importButton").addEventListener(
      "click",
      () => {
        this.#importFromFile(this._mockImportFile || null, this.#window);
        this.#clearFilterInput();
      },
      eventListenersConfig
    );

    this.#prettyPrintButton = this.#root.querySelector(
      ".style-editor-prettyPrintButton"
    );
    this.#prettyPrintButton.addEventListener(
      "click",
      () => {
        if (!this.selectedEditor) {
          return;
        }
        this.#prettyPrintButton.classList.add("pretty");
        this.selectedEditor.prettifySourceText();
      },
      eventListenersConfig
    );

    this.#root
      .querySelector("#style-editor-options")
      .addEventListener(
        "click",
        this.#onOptionsButtonClick,
        eventListenersConfig
      );

    this.#filterInput.addEventListener(
      "input",
      this.#onFilterInputChange,
      eventListenersConfig
    );

    this.#filterInputClearButton.addEventListener(
      "click",
      () => this.#clearFilterInput(),
      eventListenersConfig
    );

    this.#panelDoc.addEventListener(
      "contextmenu",
      () => {
        this.#contextMenuStyleSheet = null;
      },
      { ...eventListenersConfig, capture: true }
    );

    this.#optionsButton = this.#panelDoc.getElementById("style-editor-options");

    this.#contextMenu = this.#panelDoc.getElementById("sidebar-context");
    this.#contextMenu.addEventListener(
      "popupshowing",
      this.#updateContextMenuItems,
      eventListenersConfig
    );

    this.#openLinkNewTabItem = this.#panelDoc.getElementById(
      "context-openlinknewtab"
    );
    this.#openLinkNewTabItem.addEventListener(
      "command",
      this.#openLinkNewTab,
      eventListenersConfig
    );

    this.#copyUrlItem = this.#panelDoc.getElementById("context-copyurl");
    this.#copyUrlItem.addEventListener(
      "command",
      this.#copyUrl,
      eventListenersConfig
    );

    // items list focus and search-on-type handling
    this.#nav.addEventListener(
      "keydown",
      this.#onNavKeyDown,
      eventListenersConfig
    );

    this.#shortcuts = new KeyShortcuts(this.#window);
    this.#shortcuts.on(
      `CmdOrCtrl+${getString("focusFilterInput.commandkey")}`,
      this.#onFocusFilterInputKeyboardShortcut
    );

    const nav = this.#panelDoc.querySelector(".splitview-controller");
    nav.style.width = Services.prefs.getIntPref(PREF_NAV_WIDTH) + "px";
  }

  #clearFilterInput() {
    this.#filterInput.value = "";
    this.#onFilterInputChange();
  }

  #onFilterInputChange = () => {
    this.#filter = this.#filterInput.value;
    this.#filterInputClearButton.toggleAttribute("hidden", !this.#filter);

    for (const summary of this.#nav.childNodes) {
      // Don't update nav class for every element, we do it after the loop.
      this.handleSummaryVisibility(summary, {
        triggerOnFilterStateChange: false,
      });
    }

    this.#onFilterStateChange();

    if (this.#activeSummary == null) {
      const firstVisibleSummary = Array.from(this.#nav.childNodes).find(
        node => !node.classList.contains(FILTERED_CLASSNAME)
      );

      if (firstVisibleSummary) {
        this.setActiveSummary(firstVisibleSummary, { reason: "filter-auto" });
      }
    }
  };

  #onFilterStateChange() {
    const summaries = Array.from(this.#nav.childNodes);
    const hasVisibleSummary = summaries.some(
      node => !node.classList.contains(FILTERED_CLASSNAME)
    );
    const allFiltered = !!summaries.length && !hasVisibleSummary;

    this.#nav.classList.toggle(ALL_FILTERED_CLASSNAME, allFiltered);

    this.#filterInput
      .closest(".devtools-searchbox")
      .classList.toggle("devtools-searchbox-no-match", !!allFiltered);
  }

  #onFocusFilterInputKeyboardShortcut = e => {
    // Prevent the print modal to be displayed.
    if (e) {
      e.stopPropagation();
      e.preventDefault();
    }
    this.#filterInput.select();
  };

  #onNavKeyDown = event => {
    function getFocusedItemWithin(nav) {
      let node = nav.ownerDocument.activeElement;
      while (node && node.parentNode != nav) {
        node = node.parentNode;
      }
      return node;
    }

    // do not steal focus from inside iframes or textboxes
    if (
      event.target.ownerDocument != this.#nav.ownerDocument ||
      event.target.tagName == "input" ||
      event.target.tagName == "textarea" ||
      event.target.classList.contains("textbox")
    ) {
      return false;
    }

    // handle keyboard navigation within the items list
    const visibleElements = Array.from(
      this.#nav.querySelectorAll(`li:not(.${FILTERED_CLASSNAME})`)
    );
    // Elements have a different visual order (due to the use of order), so
    // we need to sort them by their data-ordinal attribute
    visibleElements.sort(
      (a, b) => a.getAttribute("data-ordinal") - b.getAttribute("data-ordinal")
    );

    let elementToFocus;
    if (
      event.keyCode == lazy.KeyCodes.DOM_VK_PAGE_UP ||
      event.keyCode == lazy.KeyCodes.DOM_VK_HOME
    ) {
      elementToFocus = visibleElements[0];
    } else if (
      event.keyCode == lazy.KeyCodes.DOM_VK_PAGE_DOWN ||
      event.keyCode == lazy.KeyCodes.DOM_VK_END
    ) {
      elementToFocus = visibleElements.at(-1);
    } else if (event.keyCode == lazy.KeyCodes.DOM_VK_UP) {
      const focusedIndex = visibleElements.indexOf(
        getFocusedItemWithin(this.#nav)
      );
      elementToFocus = visibleElements[focusedIndex - 1];
    } else if (event.keyCode == lazy.KeyCodes.DOM_VK_DOWN) {
      const focusedIndex = visibleElements.indexOf(
        getFocusedItemWithin(this.#nav)
      );
      elementToFocus = visibleElements[focusedIndex + 1];
    }

    if (elementToFocus !== undefined) {
      event.stopPropagation();
      event.preventDefault();
      elementToFocus.focus();
      return false;
    }

    return true;
  };

  /**
   * Opens the Options Popup Menu
   *
   * @param {number} screenX
   * @param {number} screenY
   *   Both obtained from the event object, used to position the popup
   */
  #onOptionsButtonClick = ({ screenX, screenY }) => {
    this.#optionsMenu = optionsPopupMenu(
      this.#toggleOrigSources,
      this.#toggleAtRulesSidebar
    );

    this.#optionsMenu.once("open", () => {
      this.#optionsButton.setAttribute("open", true);
    });
    this.#optionsMenu.once("close", () => {
      this.#optionsButton.removeAttribute("open");
    });

    this.#optionsMenu.popup(screenX, screenY, this.#toolbox.doc);
  };

  /**
   * Be called when changing the original sources pref.
   */
  #onOrigSourcesPrefChanged = async () => {
    this.#clear();
    // When we toggle the source-map preference, we clear the panel and re-fetch the exact
    // same stylesheet resources from ResourceCommand, but `_addStyleSheet` will trigger
    // or ignore the additional source-map mapping.
    this.#root.classList.add("loading");
    for (const resource of this.#commands.resourceCommand.getAllResources(
      this.#commands.resourceCommand.TYPES.STYLESHEET
    )) {
      await this.#handleStyleSheetResource(resource);
    }

    this.#root.classList.remove("loading");

    this.emit("stylesheets-refreshed");
  };

  /**
   * Remove all editors and add loading indicator.
   */
  #clear = () => {
    // remember selected sheet and line number for next load
    if (this.selectedEditor && this.selectedEditor.sourceEditor) {
      const href = this.selectedEditor.styleSheet.href;
      const { line, ch } = this.selectedEditor.sourceEditor.getCursor();

      this.#styleSheetToSelect = {
        stylesheet: href,
        line,
        col: ch,
      };
    }

    // remember saved file locations
    for (const editor of this.editors) {
      if (editor.savedFile) {
        const identifier = this.getStyleSheetIdentifier(editor.styleSheet);
        this.savedLocations[identifier] = editor.savedFile;
      }
    }

    this.#clearStyleSheetEditors();
    // Clear the left sidebar items and their associated elements.
    while (this.#nav.hasChildNodes()) {
      this.removeSplitViewItem(this.#nav.firstChild);
    }

    this.selectedEditor = null;
    // Here the keys are style sheet actors, and the values are
    // promises that resolve to the sheet's editor.  See |_addStyleSheet|.
    this.#seenSheets = new Map();

    this.emit("stylesheets-clear");
  };

  /**
   * Add an editor for this stylesheet. Add editors for its original sources
   * instead (e.g. Sass sources), if applicable.
   *
   * @param  {Resource} resource
   *         The STYLESHEET resource which is received from resource command.
   * @return {Promise}
   *         A promise that resolves to the style sheet's editor when the style sheet has
   *         been fully loaded.  If the style sheet has a source map, and source mapping
   *         is enabled, then the promise resolves to null.
   */
  #addStyleSheet(resource) {
    if (!this.#seenSheets.has(resource)) {
      const promise = (async () => {
        // When the StyleSheet is mapped to one or many original sources,
        // do not create an editor for the minified StyleSheet.
        const hasValidOriginalSource =
          await this.#tryAddingOriginalStyleSheets(resource);
        if (hasValidOriginalSource) {
          return null;
        }
        // Otherwise, if source-map failed or this is a non-source-map CSS
        // create an editor for it.
        return this.#addStyleSheetEditor(resource);
      })();
      this.#seenSheets.set(resource, promise);
    }
    return this.#seenSheets.get(resource);
  }

  /**
   * Check if the given StyleSheet relates to an original StyleSheet (via source maps).
   * If one is found, create an editor for the original one.
   *
   * @param  {Resource} resource
   *         The STYLESHEET resource which is received from resource command.
   * @return Boolean
   *         Return true, when we found a viable related original StyleSheet.
   */
  async #tryAddingOriginalStyleSheets(resource) {
    // Avoid querying the SourceMap if this feature is disabled.
    if (!Services.prefs.getBoolPref(PREF_ORIG_SOURCES)) {
      return false;
    }

    const sourceMapLoader = this.#toolbox.sourceMapLoader;
    const {
      href,
      nodeHref,
      resourceId: id,
      sourceMapURL,
      sourceMapBaseURL,
    } = resource;
    let sources;
    try {
      sources = await sourceMapLoader.getOriginalURLs({
        id,
        url: href || nodeHref,
        sourceMapBaseURL,
        sourceMapURL,
      });
    } catch (e) {
      // Ignore any source map error, they will be logged
      // via the SourceMapLoader and Toolbox into the Web Console.
      return false;
    }

    // Return the generated CSS if the source-map failed to be parsed
    // or did not generate any original source.
    if (!sources || !sources.length) {
      return false;
    }

    // A single generated sheet might map to multiple original
    // sheets, so make editors for each of them.
    for (const { id: originalId, url: originalURL } of sources) {
      const original = new lazy.OriginalSource(
        originalURL,
        originalId,
        sourceMapLoader
      );

      // set so the first sheet will be selected, even if it's a source
      original.styleSheetIndex = resource.styleSheetIndex;
      original.relatedStyleSheet = resource;
      original.resourceId = resource.resourceId;
      original.targetFront = resource.targetFront;
      original.atRules = resource.atRules;
      await this.#addStyleSheetEditor(original);
    }

    return true;
  }

  #removeStyleSheet(resource, editor) {
    this.#seenSheets.delete(resource);
    this.#removeStyleSheetEditor(editor);
  }

  #getInlineStyleSheetsCount() {
    let count = 0;
    for (const editor of this.editors) {
      if (!editor.styleSheet.href && !editor.styleSheet.constructed) {
        count++;
      }
    }
    return count;
  }

  #getNewStyleSheetsCount() {
    let count = 0;
    for (const editor of this.editors) {
      if (editor.isNew) {
        count++;
      }
    }
    return count;
  }

  #getConstructedSheetsCount() {
    let count = 0;
    for (const editor of this.editors) {
      if (editor.styleSheet.constructed) {
        count++;
      }
    }
    return count;
  }

  /**
   * Finds the index to be shown in the Style Editor for inline, constructed or
   * user-created style sheets, returns undefined if not any of those.
   *
   * @param {StyleSheet} styleSheet
   *        Object representing stylesheet
   * @return {number}
   *         1-based Integer representing the index of the current stylesheet
   *         among all stylesheets of its type (inline, constructed or user-created).
   *         Defaults to 0 when non-applicable (e.g. for stylesheet with href)
   */
  #getNextFriendlyIndex(styleSheet) {
    if (styleSheet.href) {
      return 0;
    }

    if (styleSheet.isNew) {
      return this.#getNewStyleSheetsCount() + 1;
    }

    if (styleSheet.constructed) {
      return this.#getConstructedSheetsCount() + 1;
    }

    return this.#getInlineStyleSheetsCount() + 1;
  }

  /**
   * Add a new editor to the UI for a source.
   *
   * @param  {Resource} resource
   *         The resource which is received from resource command.
   * @return {Promise} that is resolved with the created StyleSheetEditor when
   *                   the editor is fully initialized or rejected on error.
   */
  async #addStyleSheetEditor(resource) {
    const editor = new StyleSheetEditor(
      resource,
      this.#window,
      this.#getNextFriendlyIndex(resource)
    );

    editor.on("property-change", this.#summaryChange.bind(this, editor));
    editor.on("at-rules-changed", this.#updateAtRulesList.bind(this, editor));
    editor.on("linked-css-file", this.#summaryChange.bind(this, editor));
    editor.on("linked-css-file-error", this.#summaryChange.bind(this, editor));
    editor.on("error", this.#onError);
    editor.on(
      "filter-input-keyboard-shortcut",
      this.#onFocusFilterInputKeyboardShortcut
    );

    // onAtRulesChanged fires at-rules-changed, so call the function after
    // registering the listener in order to ensure to get at-rules-changed event.
    editor.onAtRulesChanged(resource.atRules);

    this.editors.push(editor);

    this.#addEditorToUI(editor);

    if (resource.fileName) {
      this.emit("test:editor-updated", editor);
    }

    return editor;
  }

  /**
   * Import a style sheet from file and asynchronously create a
   * new stylesheet on the debuggee for it.
   *
   * @param {mixed} file
   *        Optional nsIFile or filename string.
   *        If not set a file picker will be shown.
   * @param {nsIWindow} parentWindow
   *        Optional parent window for the file picker.
   */
  #importFromFile(file, parentWindow) {
    const onFileSelected = selectedFile => {
      if (!selectedFile) {
        // nothing selected
        return;
      }
      lazy.NetUtil.asyncFetch(
        {
          uri: lazy.NetUtil.newURI(selectedFile),
          loadingNode: this.#window.document,
          securityFlags:
            Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
          contentPolicyType: Ci.nsIContentPolicy.TYPE_OTHER,
        },
        async (stream, status) => {
          if (!Components.isSuccessCode(status)) {
            this.emit("error", { key: LOAD_ERROR, level: "warning" });
            return;
          }
          const source = lazy.NetUtil.readInputStreamToString(
            stream,
            stream.available()
          );
          stream.close();

          const stylesheetsFront =
            await this.currentTarget.getFront("stylesheets");
          stylesheetsFront.addStyleSheet(source, selectedFile.path);
        }
      );
    };

    showFilePicker(file, false, parentWindow, onFileSelected);
  }

  /**
   * Forward any error from a stylesheet.
   *
   * @param  {data} data
   *         The event data
   */
  #onError = data => {
    this.emit("error", data);
  };

  /**
   * Toggle the original sources pref.
   */
  #toggleOrigSources() {
    const isEnabled = Services.prefs.getBoolPref(PREF_ORIG_SOURCES);
    Services.prefs.setBoolPref(PREF_ORIG_SOURCES, !isEnabled);
  }

  /**
   * Toggle the pref for showing the at-rules sidebar (for @media, @layer, @container, …)
   * in each editor.
   */
  #toggleAtRulesSidebar() {
    const isEnabled = Services.prefs.getBoolPref(PREF_AT_RULES_SIDEBAR);
    Services.prefs.setBoolPref(PREF_AT_RULES_SIDEBAR, !isEnabled);
  }

  /**
   * Toggle the at-rules sidebar in each editor depending on the setting.
   */
  #onAtRulesSidebarPrefChanged = () => {
    this.editors.forEach(this.#updateAtRulesList);
  };

  /**
   * This method handles the following cases related to the context
   * menu items "_openLinkNewTabItem" and "_copyUrlItem":
   *
   * 1) There was a stylesheet clicked on and it is external: show and
   * enable the context menu item
   * 2) There was a stylesheet clicked on and it is inline: show and
   * disable the context menu item
   * 3) There was no stylesheet clicked on (the right click happened
   * below the list): hide the context menu
   */
  #updateContextMenuItems = async () => {
    this.#openLinkNewTabItem.hidden = !this.#contextMenuStyleSheet;
    this.#copyUrlItem.hidden = !this.#contextMenuStyleSheet;

    if (this.#contextMenuStyleSheet) {
      this.#openLinkNewTabItem.toggleAttribute(
        "disabled",
        !this.#contextMenuStyleSheet.href
      );
      this.#copyUrlItem.toggleAttribute(
        "disabled",
        !this.#contextMenuStyleSheet.href
      );
    }
  };

  /**
   * Open a particular stylesheet in a new tab.
   */
  #openLinkNewTab = () => {
    if (this.#contextMenuStyleSheet) {
      lazy.openContentLink(this.#contextMenuStyleSheet.href);
    }
  };

  /**
   * Copies a stylesheet's URL.
   */
  #copyUrl = () => {
    if (this.#contextMenuStyleSheet) {
      lazy.copyString(this.#contextMenuStyleSheet.href);
    }
  };

  /**
   * Remove a particular stylesheet editor from the UI
   *
   * @param {StyleSheetEditor}  editor
   *        The editor to remove.
   */
  #removeStyleSheetEditor(editor) {
    if (editor.summary) {
      this.removeSplitViewItem(editor.summary);
    } else {
      const self = this;
      this.on("editor-added", function onAdd(added) {
        if (editor == added) {
          self.off("editor-added", onAdd);
          self.removeSplitViewItem(editor.summary);
        }
      });
    }

    editor.destroy();
    this.editors.splice(this.editors.indexOf(editor), 1);
  }

  /**
   * Clear all the editors from the UI.
   */
  #clearStyleSheetEditors() {
    for (const editor of this.editors) {
      editor.destroy();
    }
    this.editors = [];
  }

  /**
   * Add new sidebar item and editor to the UI.
   *  - summary is the entry in the left sidebar
   *  - details contains the editor + at rules
   *
   * @param  {StyleSheetEditor} editor
   *         Editor to create UI for.
   */
  #addEditorToUI(editor) {
    // Create the detail and summary nodes from the templates node (declared in index.xhtml)
    const details = this.#tplDetails.cloneNode(true);
    details.id = "";
    const summary = this.#tplSummary.cloneNode(true);
    summary.id = "";

    let ordinal = editor.styleSheet.styleSheetIndex;
    ordinal = ordinal == -1 ? Number.MAX_SAFE_INTEGER : ordinal;
    summary.style.order = ordinal;
    summary.setAttribute("data-ordinal", ordinal);

    const isSystem = !!editor.styleSheet.system;
    if (isSystem) {
      summary.classList.add("stylesheet-readonly");
    }

    this.#nav.appendChild(summary);
    this.#side.appendChild(details);

    this.#summaryDataMap.set(summary, {
      details,
      editor,
    });

    const createdEditor = editor;
    createdEditor.summary = summary;
    createdEditor.details = details;

    const eventListenersConfig = { signal: this.#uiAbortController.signal };

    summary.addEventListener(
      "click",
      event => {
        event.stopPropagation();
        this.setActiveSummary(summary);
      },
      eventListenersConfig
    );

    const stylesheetToggle = summary.querySelector(".stylesheet-toggle");
    if (isSystem) {
      stylesheetToggle.disabled = true;
      this.#window.document.l10n.setAttributes(
        stylesheetToggle,
        "styleeditor-visibility-toggle-system"
      );
    } else {
      stylesheetToggle.addEventListener(
        "click",
        event => {
          event.stopPropagation();
          event.target.blur();

          createdEditor.toggleDisabled();
        },
        eventListenersConfig
      );
    }

    summary.querySelector(".stylesheet-name").addEventListener(
      "keypress",
      event => {
        if (event.keyCode == lazy.KeyCodes.DOM_VK_RETURN) {
          this.setActiveSummary(summary);
        }
      },
      eventListenersConfig
    );

    summary.querySelector(".stylesheet-saveButton").addEventListener(
      "click",
      event => {
        event.stopPropagation();
        event.target.blur();

        createdEditor.saveToFile(createdEditor.savedFile);
      },
      eventListenersConfig
    );

    this.#updateSummaryForEditor(createdEditor, summary);

    summary.addEventListener(
      "contextmenu",
      () => {
        this.#contextMenuStyleSheet = createdEditor.styleSheet;
      },
      eventListenersConfig
    );

    summary.addEventListener(
      "focus",
      function onSummaryFocus(event) {
        if (event.target == summary) {
          // autofocus the stylesheet name
          summary.querySelector(".stylesheet-name").focus();
        }
      },
      eventListenersConfig
    );

    const sidebar = details.querySelector(".stylesheet-sidebar");
    sidebar.style.width = Services.prefs.getIntPref(PREF_SIDEBAR_WIDTH) + "px";

    const splitter = details.querySelector(".devtools-side-splitter");
    splitter.addEventListener(
      "mousemove",
      () => {
        const sidebarWidth = parseInt(sidebar.style.width, 10);
        if (!isNaN(sidebarWidth)) {
          Services.prefs.setIntPref(PREF_SIDEBAR_WIDTH, sidebarWidth);

          // update all at-rules sidebars for consistency
          const sidebars = [
            ...this.#panelDoc.querySelectorAll(".stylesheet-sidebar"),
          ];
          for (const atRuleSidebar of sidebars) {
            atRuleSidebar.style.width = sidebarWidth + "px";
          }
        }
      },
      eventListenersConfig
    );

    // autofocus if it's a new user-created stylesheet
    if (createdEditor.isNew) {
      this.#selectEditor(createdEditor);
    }

    if (this.#isEditorToSelect(createdEditor)) {
      this.switchToSelectedSheet();
    }

    // If this is the first stylesheet and there is no pending request to
    // select a particular style sheet, select this sheet.
    if (
      !this.selectedEditor &&
      !this.#styleSheetBoundToSelect &&
      createdEditor.styleSheet.styleSheetIndex == 0 &&
      !summary.classList.contains(FILTERED_CLASSNAME)
    ) {
      this.#selectEditor(createdEditor);
    }
    this.emit("editor-added", createdEditor);
  }

  /**
   * Switch to the editor that has been marked to be selected.
   *
   * @return {Promise}
   *         Promise that will resolve when the editor is selected.
   */
  switchToSelectedSheet() {
    const toSelect = this.#styleSheetToSelect;

    for (const editor of this.editors) {
      if (this.#isEditorToSelect(editor)) {
        // The _styleSheetBoundToSelect will always hold the latest pending
        // requested style sheet (with line and column) which is not yet
        // selected by the source editor. Only after we select that particular
        // editor and go the required line and column, it will become null.
        this.#styleSheetBoundToSelect = this.#styleSheetToSelect;
        this.#styleSheetToSelect = null;
        return this.#selectEditor(editor, toSelect.line, toSelect.col);
      }
    }

    return Promise.resolve();
  }

  /**
   * Returns whether a given editor is the current editor to be selected. Tests
   * based on href or underlying stylesheet.
   *
   * @param {StyleSheetEditor} editor
   *        The editor to test.
   */
  #isEditorToSelect(editor) {
    const toSelect = this.#styleSheetToSelect;
    if (!toSelect) {
      return false;
    }
    const isHref =
      toSelect.stylesheet === null || typeof toSelect.stylesheet == "string";

    return (
      (isHref && editor.styleSheet.href == toSelect.stylesheet) ||
      toSelect.stylesheet == editor.styleSheet
    );
  }

  /**
   * Select an editor in the UI.
   *
   * @param  {StyleSheetEditor} editor
   *         Editor to switch to.
   * @param  {number} line
   *         Line number to jump to
   * @param  {number} col
   *         Column number to jump to
   * @return {Promise}
   *         Promise that will resolve when the editor is selected and ready
   *         to be used.
   */
  #selectEditor(editor, line = null, col = null) {
    // Don't go further if the editor was destroyed in the meantime
    if (!this.editors.includes(editor)) {
      return null;
    }

    const editorPromise = editor.getSourceEditor().then(() => {
      // Don't go further if the editor was destroyed in the meantime
      if (!this.editors.includes(editor)) {
        return;
      }
      // line/col are null when the style editor is initialized and the first stylesheet
      // editor is selected. Unfortunately, this function might be called also when the
      // panel is opened from clicking on a CSS warning in the WebConsole panel, in which
      // case we have specific line+col.
      // There's no guarantee which one could be called first, and it happened that we
      // were setting the cursor once for the correct line coming from the webconsole,
      // and then re-setting it to the default value (which was <0,0>).
      // To avoid the race, we simply don't explicitly set the cursor to any default value,
      // which is not a big deal as CodeMirror does init it to <0,0> anyway.
      // See Bug 1738124 for more information.
      if (line !== null || col !== null) {
        editor.setCursor(line, col);
      }
      this.#styleSheetBoundToSelect = null;
    });

    const summaryPromise = this.getEditorSummary(editor).then(summary => {
      // Don't go further if the editor was destroyed in the meantime
      if (!this.editors.includes(editor)) {
        throw new Error("Editor was destroyed");
      }
      this.setActiveSummary(summary);
    });

    return Promise.all([editorPromise, summaryPromise]);
  }

  getEditorSummary(editor) {
    const self = this;

    if (editor.summary) {
      return Promise.resolve(editor.summary);
    }

    return new Promise(resolve => {
      this.on("editor-added", function onAdd(selected) {
        if (selected == editor) {
          self.off("editor-added", onAdd);
          resolve(editor.summary);
        }
      });
    });
  }

  getEditorDetails(editor) {
    const self = this;

    if (editor.details) {
      return Promise.resolve(editor.details);
    }

    return new Promise(resolve => {
      this.on("editor-added", function onAdd(selected) {
        if (selected == editor) {
          self.off("editor-added", onAdd);
          resolve(editor.details);
        }
      });
    });
  }

  /**
   * Returns an identifier for the given style sheet.
   *
   * @param {StyleSheet} styleSheet
   *        The style sheet to be identified.
   */
  getStyleSheetIdentifier(styleSheet) {
    // Identify inline style sheets by their host page URI and index
    // at the page.
    return styleSheet.href
      ? styleSheet.href
      : "inline-" + styleSheet.styleSheetIndex + "-at-" + styleSheet.nodeHref;
  }

  /**
   * Get the OriginalSource object for a given original sourceId returned from
   * the sourcemap worker service.
   *
   * @param {string} sourceId
   *        The ID to search for from the sourcemap worker.
   *
   * @return {OriginalSource | null}
   */
  getOriginalSourceSheet(sourceId) {
    for (const editor of this.editors) {
      const { styleSheet } = editor;
      if (styleSheet.isOriginalSource && styleSheet.sourceId === sourceId) {
        return styleSheet;
      }
    }
    return null;
  }

  /**
   * Given an URL, find a stylesheet resource with that URL, if one has been
   * loaded into the editor.js
   *
   * Do not use this unless you have no other way to get a StyleSheet resource
   * multiple sheets could share the same URL, so this will give you _one_
   * of possibly many sheets with that URL.
   *
   * @param {string} url
   *        An arbitrary URL to search for.
   *
   * @return {StyleSheetResource|null}
   */
  getStylesheetResourceForGeneratedURL(url) {
    for (const styleSheet of this.#seenSheets.keys()) {
      const sheetURL = styleSheet.href || styleSheet.nodeHref;
      if (!styleSheet.isOriginalSource && sheetURL === url) {
        return styleSheet;
      }
    }
    return null;
  }

  /**
   * selects a stylesheet and optionally moves the cursor to a selected line
   *
   * @param {StyleSheetResource} stylesheet
   *        Stylesheet to select or href of stylesheet to select
   * @param {number} line
   *        Line to which the caret should be moved (zero-indexed).
   * @param {number} col
   *        Column to which the caret should be moved (zero-indexed).
   * @return {Promise}
   *         Promise that will resolve when the editor is selected and ready
   *         to be used.
   */
  selectStyleSheet(stylesheet, line, col) {
    this.#styleSheetToSelect = {
      stylesheet,
      line,
      col,
    };

    /* Switch to the editor for this sheet, if it exists yet.
       Otherwise each editor will be checked when it's created. */
    return this.switchToSelectedSheet();
  }

  /**
   * Handler for an editor's 'property-changed' event.
   * Update the summary in the UI.
   *
   * @param  {StyleSheetEditor} editor
   *         Editor for which a property has changed
   */
  #summaryChange(editor) {
    this.#updateSummaryForEditor(editor);
  }

  /**
   * Update split view summary of given StyleEditor instance.
   *
   * @param {StyleSheetEditor} editor
   * @param {DOMElement} summary
   *        Optional item's summary element to update. If none, item
   *        corresponding to passed editor is used.
   */
  #updateSummaryForEditor(editor, summary) {
    summary = summary || editor.summary;
    if (!summary) {
      return;
    }

    let ruleCount = editor.styleSheet.ruleCount;
    if (editor.styleSheet.relatedStyleSheet) {
      ruleCount = editor.styleSheet.relatedStyleSheet.ruleCount;
    }
    if (ruleCount === undefined) {
      ruleCount = "-";
    }

    this.#panelDoc.l10n.setArgs(
      summary.querySelector(".stylesheet-rule-count"),
      {
        ruleCount,
      }
    );

    summary.classList.toggle("disabled", !!editor.styleSheet.disabled);
    summary.classList.toggle("unsaved", !!editor.unsaved);
    summary.classList.toggle("linked-file-error", !!editor.linkedCSSFileError);

    const label = summary.querySelector(".stylesheet-name > label");
    label.setAttribute("value", editor.friendlyName);
    if (editor.styleSheet.href) {
      label.setAttribute("tooltiptext", editor.styleSheet.href);
    }

    let linkedCSSSource = "";
    if (editor.linkedCSSFile) {
      linkedCSSSource = PathUtils.filename(editor.linkedCSSFile);
    } else if (editor.styleSheet.relatedStyleSheet) {
      // Compute a friendly name for the related generated source
      // (relatedStyleSheet is set on original CSS to refer to the generated one)
      linkedCSSSource = shortSource(editor.styleSheet.relatedStyleSheet);
      try {
        linkedCSSSource = decodeURI(linkedCSSSource);
      } catch (e) {}
    }
    text(summary, ".stylesheet-linked-file", linkedCSSSource);
    text(summary, ".stylesheet-title", editor.styleSheet.title || "");

    // We may need to change the summary visibility as a result of the changes.
    this.handleSummaryVisibility(summary);
  }

  /**
   * Update the pretty print button.
   * The button will be disabled if:
   * - the selected file is read-only
   * - OR the selected file is an original file
   */
  #updatePrettyPrintButton() {
    const isReadOnly = !!this.selectedEditor?.sourceEditor?.config?.readOnly;
    const isOriginalSource =
      !!this.selectedEditor?.styleSheet?.isOriginalSource;

    const disable = !this.selectedEditor || isOriginalSource || isReadOnly;

    // Only update the button if its state needs it
    if (disable !== this.#prettyPrintButton.hasAttribute("disabled")) {
      this.#prettyPrintButton.toggleAttribute("disabled");
    }
    this.#prettyPrintButton.classList.toggle(
      "pretty",
      this.selectedEditor?.isPrettyPrinted || false
    );
    let l10nString;
    if (disable) {
      if (isReadOnly) {
        l10nString = "styleeditor-pretty-print-button-disabled-read-only";
      } else if (isOriginalSource) {
        l10nString = "styleeditor-pretty-print-button-disabled";
      }
    } else {
      l10nString = "styleeditor-pretty-print-button";
    }

    this.#window.document.l10n.setAttributes(
      this.#prettyPrintButton,
      l10nString
    );
  }

  /**
   * Update the at-rules sidebar for an editor. Hide if there are no rules
   * Display a list of the at-rules (@media, @layer, @container, …) in the editor's associated style sheet.
   * Emits a 'at-rules-list-changed' event after updating the UI.
   *
   * @param  {StyleSheetEditor} editor
   *         Editor to update sidebar of
   */
  #updateAtRulesList = editor => {
    (async function () {
      const details = await this.getEditorDetails(editor);
      const rules = editor.atRules;
      const showSidebar = Services.prefs.getBoolPref(PREF_AT_RULES_SIDEBAR);
      const sidebar = details.querySelector(".stylesheet-sidebar");

      let inSource = false;

      const listItems = [];
      for (const rule of rules) {
        const { line, column } = rule;

        let location = {
          line,
          column,
          source: editor.styleSheet.href,
          styleSheet: editor.styleSheet,
        };
        if (editor.styleSheet.isOriginalSource) {
          const styleSheet = editor.cssSheet;
          location = await editor.styleSheet.getOriginalLocation(
            styleSheet,
            line,
            column
          );
        }

        // this at-rule is from a different original source
        if (location.source != editor.styleSheet.href) {
          continue;
        }
        inSource = true;

        const div = this.#panelDoc.createElementNS(HTML_NS, "div");
        div.classList.add("at-rule-label", rule.type);
        div.addEventListener(
          "click",
          this.#jumpToLocation.bind(this, location)
        );

        const ruleTextContainer = this.#panelDoc.createElementNS(
          HTML_NS,
          "div"
        );
        const type = this.#panelDoc.createElementNS(HTML_NS, "span");
        type.className = "at-rule-type";
        type.append(this.#panelDoc.createTextNode(`@${rule.type}\u00A0`));
        if (rule.type == "layer" && rule.layerName) {
          type.append(this.#panelDoc.createTextNode(`${rule.layerName}\u00A0`));
        } else if (rule.type === "property") {
          type.append(
            this.#panelDoc.createTextNode(`${rule.propertyName}\u00A0`)
          );
        } else if (rule.type === "position-try") {
          type.append(
            this.#panelDoc.createTextNode(`${rule.positionTryName}\u00A0`)
          );
        } else if (rule.type === "custom-media") {
          const parts = [];
          const { customMediaName, customMediaQuery } = rule;
          for (let i = 0, len = customMediaQuery.length; i < len; i++) {
            const media = customMediaQuery[i];
            const queryEl = this.#panelDoc.createElementNS(HTML_NS, "span");
            queryEl.textContent = media.text;
            if (!media.matches) {
              queryEl.classList.add("media-condition-unmatched");
            }
            parts.push(queryEl);
            if (len > 1 && i !== len - 1) {
              parts.push(", ");
            }
          }

          type.append(`${customMediaName} `, ...parts);
        }

        const cond = this.#panelDoc.createElementNS(HTML_NS, "span");
        cond.className = "at-rule-condition";
        if (rule.type == "media" && !rule.matches) {
          cond.classList.add("media-condition-unmatched");
        }
        if (this.#commands.descriptorFront.isLocalTab) {
          this.#setConditionContents(cond, rule.conditionText, rule.type);
        } else {
          cond.textContent = rule.conditionText;
        }

        const link = this.#panelDoc.createElementNS(HTML_NS, "div");
        link.className = "at-rule-line theme-link";
        if (location.line != -1) {
          link.textContent = ":" + location.line;
        }

        ruleTextContainer.append(type, cond);
        div.append(ruleTextContainer, link);
        listItems.push(div);
      }

      const list = details.querySelector(".stylesheet-at-rules-list");
      list.replaceChildren(...listItems);

      sidebar.hidden = !showSidebar || !inSource;

      this.emit("at-rules-list-changed", editor);
    })
      .bind(this)()
      .catch(console.error);
  };

  /**
   * Set the condition text for the at-rule element.
   * For media queries, it also injects links to open RDM at a specific size.
   *
   * @param {HTMLElement} element
   *        The element corresponding to the media sidebar condition
   * @param {string} ruleConditionText
   *        The rule conditionText
   * @param {string} type
   *        The type of the at-rule (e.g. "media", "layer", "supports", …)
   */
  #setConditionContents(element, ruleConditionText, type) {
    if (!ruleConditionText) {
      return;
    }

    // For non-media rules, we don't do anything more than displaying the conditionText
    // as there are no other condition text that would justify opening RDM at a specific
    // size (e.g. `@container` condition is relative to a container size, which varies
    // depending the node the rule applies to).
    if (type !== "media") {
      const node = this.#panelDoc.createTextNode(ruleConditionText);
      element.appendChild(node);
      return;
    }

    const minMaxPattern = /(min\-|max\-)(width|height):\s\d+(px)/gi;

    let match = minMaxPattern.exec(ruleConditionText);
    let lastParsed = 0;
    while (match && match.index != minMaxPattern.lastIndex) {
      const matchEnd = match.index + match[0].length;
      const node = this.#panelDoc.createTextNode(
        ruleConditionText.substring(lastParsed, match.index)
      );
      element.appendChild(node);

      const link = this.#panelDoc.createElementNS(HTML_NS, "a");
      link.href = "#";
      link.className = "media-responsive-mode-toggle";
      link.textContent = ruleConditionText.substring(match.index, matchEnd);
      link.addEventListener("click", this.#onMediaConditionClick.bind(this));
      element.appendChild(link);

      match = minMaxPattern.exec(ruleConditionText);
      lastParsed = matchEnd;
    }

    const node = this.#panelDoc.createTextNode(
      ruleConditionText.substring(lastParsed, ruleConditionText.length)
    );
    element.appendChild(node);
  }

  /**
   * Called when a media condition is clicked
   * If a responsive mode link is clicked, it will launch it.
   *
   * @param {object} e
   *        Event object
   */
  #onMediaConditionClick(e) {
    const conditionText = e.target.textContent;
    const isWidthCond = conditionText.toLowerCase().indexOf("width") > -1;
    const mediaVal = parseInt(/\d+/.exec(conditionText), 10);

    const options = isWidthCond ? { width: mediaVal } : { height: mediaVal };
    this.#launchResponsiveMode(options);
    e.preventDefault();
    e.stopPropagation();
  }

  /**
   * Launches the responsive mode with a specific width or height.
   *
   * @param  {object} options
   *         Object with width or/and height properties.
   */
  async #launchResponsiveMode(options = {}) {
    const tab = this.#commands.descriptorFront.localTab;
    const win = tab.ownerDocument.defaultView;

    await lazy.ResponsiveUIManager.openIfNeeded(win, tab, {
      trigger: "style_editor",
    });
    this.emit("responsive-mode-opened");

    lazy.ResponsiveUIManager.getResponsiveUIForTab(tab).setViewportSize(
      options
    );
  }

  /**
   * Jump cursor to the editor for a stylesheet and line number for a rule.
   *
   * @param  {object} location
   *         Location object with 'line', 'column', and 'source' properties.
   */
  #jumpToLocation(location) {
    const source = location.styleSheet || location.source;
    this.selectStyleSheet(source, location.line - 1, location.column - 1);
  }

  #startLoadingStyleSheets() {
    this.#root.classList.add("loading");
    this.#loadingStyleSheets = [];
  }

  async #waitForLoadingStyleSheets() {
    while (this.#loadingStyleSheets?.length > 0) {
      const pending = this.#loadingStyleSheets;
      this.#loadingStyleSheets = [];
      await Promise.all(pending);
    }

    this.#loadingStyleSheets = null;
    this.#root.classList.remove("loading");
    this.emit("reloaded");
  }

  /**
   * Handle new non-original stylesheet ressource.
   * (Original stylesheets are going to be created from #tryAddingOriginalStyleSheets)
   *
   * @param  {Resource} resource
   *         The STYLESHEET resource which is received from resource command.
   */
  async #handleStyleSheetResource(resource) {
    try {
      // The fileName is in resource means this stylesheet was imported from file by user.
      const { fileName } = resource;
      let file = fileName ? new lazy.FileUtils.File(fileName) : null;

      // recall location of saved file for this sheet after page reload
      if (!file) {
        const identifier = this.getStyleSheetIdentifier(resource);
        const savedFile = this.savedLocations[identifier];
        if (savedFile) {
          file = savedFile;
        }
      }

      // As this method only processes actual stylesheet running on the page
      // (and not the original stylesheet, which may have a forged URL which is different from the displayed content),
      // trust the file URL and automatically allow saving to matching local file.
      // (This may change if we start supporting `//# sourceURL` for stylesheets.)
      if (!file && resource.href?.startsWith("file://")) {
        const uri = Services.io.newURI(resource.href);
        uri.QueryInterface(Ci.nsIFileURL);
        file = uri.file;
      }

      // Check if this file relates to a Local Mode mapping
      // so that we can save directly to the local file
      if (!file && resource.href) {
        const path = lazy.LocalModeMappings.getLocalFileForURL(resource.href);
        if (path) {
          file = new lazy.FileUtils.File(path);
        }
      }
      resource.file = file;

      await this.#addStyleSheet(resource);
    } catch (e) {
      console.error(e);
      this.emit("error", { key: LOAD_ERROR, level: "warning" });
    }
  }

  // onAvailable is a mandatory argument for watchTargets,
  // but we don't do anything when a new target gets created.
  #onTargetAvailable = () => {};

  #onTargetDestroyed = ({ targetFront }) => {
    // Iterate over a copy of the list in order to prevent skipping
    // over some items when removing items of this list
    const editorsCopy = [...this.editors];
    for (const editor of editorsCopy) {
      const { styleSheet } = editor;
      if (styleSheet.targetFront == targetFront) {
        this.#removeStyleSheet(styleSheet, editor);
      }
    }
  };

  #onResourceAvailable = async resources => {
    const promises = [];
    for (const resource of resources) {
      if (
        resource.resourceType ===
        this.#commands.resourceCommand.TYPES.STYLESHEET
      ) {
        const onStyleSheetHandled = this.#handleStyleSheetResource(resource);

        if (this.#loadingStyleSheets) {
          // In case of reloading/navigating and panel's opening
          this.#loadingStyleSheets.push(onStyleSheetHandled);
        }
        promises.push(onStyleSheetHandled);
        continue;
      }

      if (!resource.targetFront.isTopLevel) {
        continue;
      }

      if (
        resource.name === "will-navigate" &&
        // When selecting a document in the Browser Toolbox iframe picker, we're getting
        // a will-navigate event. In such case, we don't want to clear the list (see Bug 1981937)
        (!this.#commands.targetCommand.descriptorFront
          .isBrowserProcessDescriptor ||
          !resource.isFrameSwitching)
      ) {
        this.#startLoadingStyleSheets();
        this.#clear();
      } else if (resource.name === "dom-complete") {
        promises.push(this.#waitForLoadingStyleSheets());
      }
    }
    await Promise.all(promises);
  };

  #onResourceUpdated = async updates => {
    // The editors are instantiated asynchronously from onResourceAvailable,
    // but we may receive updates right after due to throttling.
    // Ensure waiting for this async work before trying to update the related editors.
    await this.#waitForLoadingStyleSheets();

    for (const { resource, update } of updates) {
      if (
        update.resourceType === this.#commands.resourceCommand.TYPES.STYLESHEET
      ) {
        const editor = this.editors.find(
          e => e.resourceId === update.resourceId
        );

        if (!editor) {
          console.warn(
            "Could not find StyleEditor to apply STYLESHEET resource update"
          );
          continue;
        }

        switch (update.updateType) {
          case "style-applied": {
            editor.onStyleApplied(update);
            break;
          }
          case "property-change": {
            for (const [property, value] of Object.entries(
              update.resourceUpdates
            )) {
              editor.onPropertyChange(property, value);
            }
            break;
          }
          case "at-rules-changed":
          case "matches-change": {
            editor.onAtRulesChanged(resource.atRules);
            break;
          }
        }
      }
    }
  };

  #onResourceDestroyed = resources => {
    for (const resource of resources) {
      if (
        resource.resourceType !==
        this.#commands.resourceCommand.TYPES.STYLESHEET
      ) {
        continue;
      }

      const editorToRemove = this.editors.find(
        editor => editor.styleSheet.resourceId == resource.resourceId
      );

      if (editorToRemove) {
        const { styleSheet } = editorToRemove;
        this.#removeStyleSheet(styleSheet, editorToRemove);
      }
    }
  };

  /**
   * Set the active item's summary element.
   *
   * @param DOMElement summary
   * @param {object} options
   * @param {string=} options.reason: Indicates why the summary was selected. It's set to
   *                  "filter-auto" when the summary was automatically selected as the result
   *                  of the previous active summary being filtered out.
   */
  setActiveSummary(summary, options = {}) {
    if (summary == this.#activeSummary) {
      return;
    }

    if (this.#activeSummary) {
      const binding = this.#summaryDataMap.get(this.#activeSummary);

      this.#activeSummary.classList.remove("splitview-active");
      binding.details.classList.remove("splitview-active");
    }

    this.#activeSummary = summary;
    if (!summary) {
      this.selectedEditor = null;
      return;
    }

    const { details } = this.#summaryDataMap.get(summary);
    summary.classList.add("splitview-active");
    details.classList.add("splitview-active");

    this.showSummaryEditor(summary, options);
  }

  /**
   * Show summary's associated editor
   *
   * @param DOMElement summary
   * @param {object} options
   * @param {string=} options.reason: Indicates why the summary was selected. It's set to
   *                  "filter-auto" when the summary was automatically selected as the result
   *                  of the previous active summary being filtered out.
   */
  async showSummaryEditor(summary, options) {
    const { details, editor } = this.#summaryDataMap.get(summary);
    this.selectedEditor = editor;

    try {
      if (!editor.sourceEditor) {
        // only initialize source editor when we switch to this view
        const inputElement = details.querySelector(".stylesheet-editor-input");
        await editor.load(inputElement, this.#cssProperties);
      }

      editor.onShow(options);

      this.#updatePrettyPrintButton();
      Glean.devtoolsStyleeditorStylesheets.stylesheetsOpenedCount.add(1);
      this.emit("editor-selected", editor);
    } catch (e) {
      console.error(e);
    }
  }

  /**
   * Remove an item from the split view.
   *
   * @param DOMElement summary
   *        Summary element of the item to remove.
   */
  removeSplitViewItem(summary) {
    if (summary == this.#activeSummary) {
      this.setActiveSummary(null);
    }

    const data = this.#summaryDataMap.get(summary);
    if (!data) {
      return;
    }

    summary.remove();
    data.details.remove();
  }

  /**
   * Make the passed element visible or not, depending if it matches the current filter
   *
   * @param {Element} summary
   * @param {object} options
   * @param {boolean} options.triggerOnFilterStateChange: Set to false to avoid calling
   *                  #onFilterStateChange directly here. This can be useful when this
   *                  function is called for every item of the list, like in `setFilter`.
   */
  handleSummaryVisibility(summary, { triggerOnFilterStateChange = true } = {}) {
    if (!this.#filter) {
      summary.classList.remove(FILTERED_CLASSNAME);
      return;
    }

    const label = summary.querySelector(".stylesheet-name label");
    const itemText = label.value.toLowerCase();
    const matchesSearch = itemText.includes(this.#filter.toLowerCase());
    summary.classList.toggle(FILTERED_CLASSNAME, !matchesSearch);

    if (this.#activeSummary == summary && !matchesSearch) {
      this.setActiveSummary(null);
    }

    if (triggerOnFilterStateChange) {
      this.#onFilterStateChange();
    }
  }

  destroy() {
    this.#commands.resourceCommand.unwatchResources(
      [
        this.#commands.resourceCommand.TYPES.DOCUMENT_EVENT,
        this.#commands.resourceCommand.TYPES.STYLESHEET,
      ],
      {
        onAvailable: this.#onResourceAvailable,
        onUpdated: this.#onResourceUpdated,
        onDestroyed: this.#onResourceDestroyed,
      }
    );
    this.#commands.targetCommand.unwatchTargets({
      types: [this.#commands.targetCommand.TYPES.FRAME],
      onAvailable: this.#onTargetAvailable,
      onDestroyed: this.#onTargetDestroyed,
    });

    if (this.#uiAbortController) {
      this.#uiAbortController.abort();
      this.#uiAbortController = null;
    }
    this.#clearStyleSheetEditors();

    this.#seenSheets = null;
    this.#filterInput = null;
    this.#filterInputClearButton = null;
    this.#nav = null;
    this.#prettyPrintButton = null;
    this.#side = null;
    this.#tplDetails = null;
    this.#tplSummary = null;

    const sidebar = this.#panelDoc.querySelector(".splitview-controller");
    const sidebarWidth = parseInt(sidebar.style.width, 10);
    if (!isNaN(sidebarWidth)) {
      Services.prefs.setIntPref(PREF_NAV_WIDTH, sidebarWidth);
    }

    if (this.#sourceMapPrefObserver) {
      this.#sourceMapPrefObserver.off(
        PREF_ORIG_SOURCES,
        this.#onOrigSourcesPrefChanged
      );
      this.#sourceMapPrefObserver.destroy();
      this.#sourceMapPrefObserver = null;
    }

    if (this.#prefObserver) {
      this.#prefObserver.destroy();
      this.#prefObserver = null;
    }

    if (this.#shortcuts) {
      this.#shortcuts.destroy();
      this.#shortcuts = null;
    }
  }
}
