/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  EXPAND_TAB,
  TAB_SIZE,
  DETECT_INDENT,
  getIndentationFromIteration,
} = require("resource://devtools/shared/indentation.js");

const { debounce } = require("resource://devtools/shared/debounce.js");
const nodeConstants = require("resource://devtools/shared/dom-node-constants.js");

const ENABLE_CODE_FOLDING = "devtools.editor.enableCodeFolding";
const KEYMAP_PREF = "devtools.editor.keymap";
const AUTO_CLOSE = "devtools.editor.autoclosebrackets";
const AUTOCOMPLETE = "devtools.editor.autocomplete";
const CARET_BLINK_TIME = "ui.caretBlinkTime";
const XHTML_NS = "http://www.w3.org/1999/xhtml";

const VALID_KEYMAPS = new Map([
  [
    "emacs",
    "chrome://devtools/content/shared/sourceeditor/codemirror/keymap/emacs.js",
  ],
  [
    "vim",
    "chrome://devtools/content/shared/sourceeditor/codemirror/keymap/vim.js",
  ],
  [
    "sublime",
    "chrome://devtools/content/shared/sourceeditor/codemirror/keymap/sublime.js",
  ],
]);

// Maximum allowed margin (in number of lines) from top or bottom of the editor
// while shifting to a line which was initially out of view.
const MAX_VERTICAL_OFFSET = 3;

const RE_JUMP_TO_LINE = /^(\d+):?(\d+)?/;
const AUTOCOMPLETE_MARK_CLASSNAME = "cm-auto-complete-shadow-text";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const { PrefObserver } = require("resource://devtools/client/shared/prefs.js");
const KeyShortcuts = require("resource://devtools/client/shared/key-shortcuts.js");

const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const L10N = new LocalizationHelper(
  "devtools/client/locales/sourceeditor.properties"
);

loader.lazyRequireGetter(
  this,
  "wasm",
  "resource://devtools/client/shared/sourceeditor/wasm.js"
);

loader.lazyRequireGetter(
  this,
  "scopeUtils",
  "resource://devtools/client/shared/sourceeditor/scope-utils.js"
);

loader.lazyRequireGetter(
  this,
  "lezerUtils",
  "resource://devtools/client/shared/sourceeditor/lezer-utils.js"
);

const { OS } = Services.appinfo;

// CM_BUNDLE and CM_IFRAME represent the HTML and JavaScript that is
// injected into an iframe in order to initialize a CodeMirror instance.

const CM_BUNDLE =
  "chrome://devtools/content/shared/sourceeditor/codemirror/codemirror.bundle.js";

const CM_IFRAME =
  "chrome://devtools/content/shared/sourceeditor/codemirror/cmiframe.html";

const CM_MAPPING = [
  "clearHistory",
  "defaultCharWidth",
  "extendSelection",
  "focus",
  "getCursor",
  "getLine",
  "getScrollInfo",
  "getSelection",
  "getViewport",
  "hasFocus",
  "lineCount",
  "openDialog",
  "redo",
  "refresh",
  "replaceSelection",
  "setSelection",
  "somethingSelected",
  "undo",
];

const ONLY_SPACES_REGEXP = /^\s*$/;

const editors = new WeakMap();

/**
 * A very thin wrapper around CodeMirror. Provides a number
 * of helper methods to make our use of CodeMirror easier and
 * another method, appendTo, to actually create and append
 * the CodeMirror instance.
 *
 * Note that Editor doesn't expose CodeMirror instance to the
 * outside world.
 *
 * Constructor accepts one argument, config. It is very
 * similar to the CodeMirror configuration object so for most
 * properties go to CodeMirror's documentation (see below).
 *
 * Other than that, it accepts one additional and optional
 * property contextMenu. This property should be an element, or
 * an ID of an element that we can use as a context menu.
 *
 * This object is also an event emitter.
 *
 * CodeMirror docs: http://codemirror.net/doc/manual.html
 */
class Editor extends EventEmitter {
  // Static methods on the Editor object itself.

  /**
   * Returns a string representation of a shortcut 'key' with
   * a OS specific modifier. Cmd- for Macs, Ctrl- for other
   * platforms. Useful with extraKeys configuration option.
   *
   * CodeMirror defines all keys with modifiers in the following
   * order: Shift - Ctrl/Cmd - Alt - Key
   */
  static accel(key, modifiers = {}) {
    return (
      (modifiers.shift ? "Shift-" : "") +
      (Services.appinfo.OS == "Darwin" ? "Cmd-" : "Ctrl-") +
      (modifiers.alt ? "Alt-" : "") +
      key
    );
  }

  /**
   * Returns a string representation of a shortcut for a
   * specified command 'cmd'. Append Cmd- for macs, Ctrl- for other
   * platforms unless noaccel is specified in the options. Useful when overwriting
   * or disabling default shortcuts.
   */
  static keyFor(cmd, opts = { noaccel: false }) {
    const key = L10N.getStr(cmd + ".commandkey");
    return opts.noaccel ? key : Editor.accel(key);
  }

  static modes = {
    cljs: { name: "text/x-clojure" },
    css: { name: "css" },
    fs: { name: "x-shader/x-fragment" },
    haxe: { name: "haxe" },
    http: { name: "http" },
    html: { name: "htmlmixed" },
    js: { name: "javascript" },
    text: { name: "text" },
    vs: { name: "x-shader/x-vertex" },
    wasm: { name: "wasm" },
  };

  container = null;
  version = null;
  config = null;
  Doc = null;
  searchState = {
    cursors: [],
    currentCursorIndex: -1,
    query: "",
  };

  #abortController;

  // The id for the current source in the editor (selected source). This is used to:
  // * cache the scroll snapshot for tracking scroll positions and the symbols,
  // * know when an actual source is displayed (and not only a loading/error message)
  #currentDocumentId = null;

  #currentDocument = null;
  #CodeMirror6;
  #compartments;
  #effects;
  #lastDirty;
  #loadedKeyMaps;
  #ownerDoc;
  #prefObserver;
  #win;
  #lineGutterMarkers = new Map();
  #lineContentMarkers = new Map();
  #posContentMarkers = new Map();
  #editorDOMEventHandlers = {};
  #gutterDOMEventHandlers = {};
  // A cache of all the scroll snapshots for the all the sources that
  // are currently open in the editor. The keys for the Map are the id's
  // for the source and the values are the scroll snapshots for the sources.
  #scrollSnapshots = new Map();
  #updateListener = null;

  #sources = new Map();

  constructor(config) {
    super();

    const tabSize = Services.prefs.getIntPref(TAB_SIZE);
    const useTabs = !Services.prefs.getBoolPref(EXPAND_TAB);
    const useAutoClose = Services.prefs.getBoolPref(AUTO_CLOSE);

    this.version = null;
    this.config = {
      cm6: false,
      value: "",
      mode: Editor.modes.text,
      indentUnit: tabSize,
      tabSize,
      contextMenu: null,
      matchBrackets: true,
      highlightSelectionMatches: {
        wordsOnly: true,
      },
      extraKeys: {},
      indentWithTabs: useTabs,
      inputStyle: "accessibleTextArea",
      // This is set to the biggest value for setTimeout (See https://developer.mozilla.org/en-US/docs/Web/API/WindowOrWorkerGlobalScope/setTimeout#Maximum_delay_value)
      // This is because codeMirror queries the underlying textArea for some things that
      // can't be retrieved with events in some browser (but we're fine in Firefox).
      pollInterval: Math.pow(2, 31) - 1,
      styleActiveLine: true,
      autoCloseBrackets: "()[]{}''\"\"``",
      autoCloseEnabled: useAutoClose,
      theme: "mozilla",
      themeSwitching: true,
      autocomplete: false,
      autocompleteOpts: {},
      // Expect a CssProperties object (see devtools/client/fronts/css-properties.js)
      cssProperties: null,
      // Set to true to prevent the search addon to be activated.
      disableSearchAddon: false,
      maxHighlightLength: 1000,
      // Disable codeMirror setTimeout-based cursor blinking (will be replaced by a CSS animation)
      cursorBlinkRate: 0,
      // List of non-printable chars that will be displayed in the editor, showing their
      // unicode version. We only add a few characters to the default list:
      // - \u202d LEFT-TO-RIGHT OVERRIDE
      // - \u202e RIGHT-TO-LEFT OVERRIDE
      // - \u2066 LEFT-TO-RIGHT ISOLATE
      // - \u2067 RIGHT-TO-LEFT ISOLATE
      // - \u2069 POP DIRECTIONAL ISOLATE
      specialChars:
        // eslint-disable-next-line no-control-regex
        /[\u0000-\u001f\u007f-\u009f\u00ad\u061c\u200b-\u200f\u2028\u2029\u202d\u202e\u2066\u2067\u2069\ufeff\ufff9-\ufffc]/,
      specialCharPlaceholder: char => {
        // Use the doc provided to the setup function if we don't have a reference to a codeMirror
        // editor yet (this can happen when an Editor is being created with existing content)
        const doc = this.#ownerDoc;
        const el = doc.createElement("span");
        el.classList.add("cm-non-printable-char");
        el.append(doc.createTextNode(`\\u${char.codePointAt(0).toString(16)}`));
        return el;
      },
      // In CodeMirror 5, adds a `CodeMirror-selectedtext` class on selected text that
      // can be used to set the selected text color, which isn't possible by default.
      // This is especially useful for High Contrast Mode where we do need to adjust the
      // selection text color
      styleSelectedText: true,
    };

    // Additional shortcuts.
    this.config.extraKeys[Editor.keyFor("jumpToLine")] = () =>
      this.jumpToLine();
    this.config.extraKeys[Editor.keyFor("moveLineUp", { noaccel: true })] =
      () => this.moveLineUp();
    this.config.extraKeys[Editor.keyFor("moveLineDown", { noaccel: true })] =
      () => this.moveLineDown();
    this.config.extraKeys[Editor.keyFor("toggleComment")] = "toggleComment";

    // Disable ctrl-[ and ctrl-] because toolbox uses those shortcuts.
    this.config.extraKeys[Editor.keyFor("indentLess")] = false;
    this.config.extraKeys[Editor.keyFor("indentMore")] = false;

    // Disable Alt-B and Alt-F to navigate groups (respectively previous and next) since:
    // - it's not standard in input fields
    // - it also inserts a character which feels weird
    this.config.extraKeys["Alt-B"] = false;
    this.config.extraKeys["Alt-F"] = false;

    // Disable Ctrl/Cmd + U as it's used for "View Source". It's okay to disable Ctrl+U as
    // the underlying command, `undoSelection`, isn't standard in input fields and isn't
    // widely known.
    this.config.extraKeys[Editor.accel("U")] = false;

    if (!config.disableSearchAddon) {
      // Override the default search shortcut so the built-in UI doesn't get hidden
      // when hitting Enter (so the user can cycle through results).
      this.config.extraKeys[Editor.accel("F")] = () =>
        editors.get(this).execCommand("findPersistent");
    }

    // Disable keys that trigger events with a null-string `which` property.
    // It looks like some of those (e.g. the Function key), can trigger a poll
    // which fails to see that there's a selection, which end up replacing the
    // selected text with an empty string.
    // TODO: We should investigate the root cause.
    this.config.extraKeys["'\u0000'"] = false;

    // Overwrite default config with user-provided, if needed.
    Object.keys(config).forEach(k => {
      if (k != "extraKeys") {
        this.config[k] = config[k];
        return;
      }

      if (!config.extraKeys) {
        return;
      }

      Object.keys(config.extraKeys).forEach(key => {
        this.config.extraKeys[key] = config.extraKeys[key];
      });
    });

    if (!this.config.gutters) {
      this.config.gutters = [];
    }
    if (
      this.config.lineNumbers &&
      !this.config.gutters.includes("CodeMirror-linenumbers")
    ) {
      this.config.gutters.push("CodeMirror-linenumbers");
    }

    // Remember the initial value of autoCloseBrackets.
    this.config.autoCloseBracketsSaved = this.config.autoCloseBrackets;

    // If the tab behaviour is not explicitly set to `false` from the config, set a tab behavior.
    // If something is selected, indent those lines. If nothing is selected and we're
    // indenting with tabs, insert one tab. Otherwise insert N
    // whitespaces where N == indentUnit option.
    if (this.config.extraKeys.Tab !== false) {
      this.config.extraKeys.Tab = cm => {
        if (config.extraKeys?.Tab) {
          // If a consumer registers its own extraKeys.Tab, we execute it before doing
          // anything else. If it returns false, that mean that all the key handling work is
          // done, so we can do an early return.
          const res = config.extraKeys.Tab(cm);
          if (res === false) {
            return;
          }
        }

        if (cm.somethingSelected()) {
          cm.indentSelection("add");
          return;
        }

        if (this.config.indentWithTabs) {
          cm.replaceSelection("\t", "end", "+input");
          return;
        }

        let num = cm.getOption("indentUnit");
        if (cm.getCursor().ch !== 0) {
          num -= cm.getCursor().ch % num;
        }
        cm.replaceSelection(" ".repeat(num), "end", "+input");
      };

      if (this.config.cssProperties) {
        // Ensure that autocompletion has cssProperties if it's passed in via the options.
        this.config.autocompleteOpts.cssProperties = this.config.cssProperties;
      }
    }
  }

  /**
   * Exposes the CodeMirror class. We want to be able to
   * invoke static commands such as runMode for syntax highlighting.
   */
  get CodeMirror() {
    const codeMirror = editors.get(this);
    return codeMirror?.constructor;
  }

  /**
   * Exposes the CodeMirror instance. We want to get away from trying to
   * abstract away the API entirely, and this makes it easier to integrate in
   * various environments and do complex things.
   */
  get codeMirror() {
    if (!editors.has(this)) {
      throw new Error(
        "CodeMirror instance does not exist. You must wait " +
          "for it to be appended to the DOM."
      );
    }
    return editors.get(this);
  }

  /**
   * Return whether there is a CodeMirror instance associated with this Editor.
   */
  get hasCodeMirror() {
    return editors.has(this);
  }

  /**
   * Appends the current Editor instance to the element specified by
   * 'el'. You can also provide your own iframe to host the editor as
   * an optional second parameter. This method actually creates and
   * loads CodeMirror and all its dependencies.
   *
   * This method is asynchronous and returns a promise.
   */
  appendTo(el, env) {
    return new Promise(resolve => {
      const cm = editors.get(this);

      if (!env) {
        env = el.ownerDocument.createElementNS(XHTML_NS, "iframe");
        env.className = "source-editor-frame";
      }

      if (cm) {
        throw new Error("You can append an editor only once.");
      }

      const onLoad = () => {
        // Prevent flickering by showing the iframe once loaded.
        // See https://github.com/w3c/csswg-drafts/issues/9624
        env.style.visibility = "";
        const win = env.contentWindow.wrappedJSObject;
        this.container = env;

        const editorEl = win.document.body;
        const editorDoc = el.ownerDocument;
        if (this.config.cm6) {
          this.#setupCm6(editorEl, editorDoc);
        } else {
          this.#setup(editorEl, editorDoc);
        }
        resolve();
      };

      env.style.visibility = "hidden";
      env.addEventListener("load", onLoad, {
        capture: true,
        once: true,
        signal: this.#abortController?.signal,
      });
      env.src = CM_IFRAME;
      el.appendChild(env);

      this.once("destroy", () => el.removeChild(env));
    });
  }

  appendToLocalElement(el) {
    const win = el.ownerDocument.defaultView;
    this.#abortController = new win.AbortController();
    if (this.config.cm6) {
      this.#setupCm6(el);
    } else {
      this.#setup(el);
    }
  }

  // This update listener allows listening to the changes
  // to the codemiror editor.
  setUpdateListener(listener = null) {
    this.#updateListener = listener;
  }

  /**
   * Do the actual appending and configuring of the CodeMirror instance. This is
   * used by both append functions above, and does all the hard work to
   * configure CodeMirror with all the right options/modes/etc.
   */
  #setup(el, doc) {
    this.#ownerDoc = doc || el.ownerDocument;
    const win = el.ownerDocument.defaultView;

    Services.scriptloader.loadSubScript(CM_BUNDLE, win);
    this.#win = win;

    if (this.config.cssProperties) {
      // Replace the propertyKeywords, colorKeywords and valueKeywords
      // properties of the CSS MIME type with the values provided by the CSS properties
      // database.
      const { propertyKeywords, colorKeywords, valueKeywords } = getCSSKeywords(
        this.config.cssProperties
      );

      const cssSpec = win.CodeMirror.resolveMode("text/css");
      cssSpec.propertyKeywords = propertyKeywords;
      cssSpec.colorKeywords = colorKeywords;
      cssSpec.valueKeywords = valueKeywords;
      win.CodeMirror.defineMIME("text/css", cssSpec);

      const scssSpec = win.CodeMirror.resolveMode("text/x-scss");
      scssSpec.propertyKeywords = propertyKeywords;
      scssSpec.colorKeywords = colorKeywords;
      scssSpec.valueKeywords = valueKeywords;
      win.CodeMirror.defineMIME("text/x-scss", scssSpec);
    }

    win.CodeMirror.commands.save = () => this.emit("saveRequested");

    // Create a CodeMirror instance add support for context menus,
    // overwrite the default controller (otherwise items in the top and
    // context menus won't work).

    const cm = win.CodeMirror(el, this.config);
    this.Doc = win.CodeMirror.Doc;

    // Disable APZ for source editors. It currently causes the line numbers to
    // "tear off" and swim around on top of the content. Bug 1160601 tracks
    // finding a solution that allows APZ to work with CodeMirror.
    cm.getScrollerElement().addEventListener(
      "wheel",
      ev => {
        // By handling the wheel events ourselves, we force the platform to
        // scroll synchronously, like it did before APZ. However, we lose smooth
        // scrolling for users with mouse wheels. This seems acceptible vs.
        // doing nothing and letting the gutter slide around.
        ev.preventDefault();

        let { deltaX, deltaY } = ev;

        if (ev.deltaMode == ev.DOM_DELTA_LINE) {
          deltaX *= cm.defaultCharWidth();
          deltaY *= cm.defaultTextHeight();
        } else if (ev.deltaMode == ev.DOM_DELTA_PAGE) {
          deltaX *= cm.getWrapperElement().clientWidth;
          deltaY *= cm.getWrapperElement().clientHeight;
        }

        cm.getScrollerElement().scrollBy(deltaX, deltaY);
      },
      { signal: this.#abortController?.signal }
    );

    cm.getWrapperElement().addEventListener(
      "contextmenu",
      ev => {
        if (!this.config.contextMenu) {
          return;
        }

        ev.stopPropagation();
        ev.preventDefault();

        let popup = this.config.contextMenu;
        if (typeof popup == "string") {
          popup = this.#ownerDoc.getElementById(this.config.contextMenu);
        }

        this.emit("popupOpen", ev, popup);
        popup.openPopupAtScreen(ev.screenX, ev.screenY, true);
      },
      { signal: this.#abortController?.signal }
    );

    const pipedEvents = [
      "beforeChange",
      "blur",
      "changes",
      "cursorActivity",
      "focus",
      "keyHandled",
      "scroll",
    ];
    for (const eventName of pipedEvents) {
      cm.on(eventName, (...args) => this.emit(eventName, ...args));
    }

    cm.on("change", () => {
      this.emit("change");
      if (!this.#lastDirty) {
        this.#lastDirty = true;
        this.emit("dirty-change");
      }
    });

    cm.on("gutterClick", (cmArg, line, gutter, ev) => {
      const lineOrOffset = !this.isWasm ? line : this.lineToWasmOffset(line);
      this.emit("gutterClick", lineOrOffset, ev.button);
    });

    win.CodeMirror.defineExtension("l10n", name => {
      return L10N.getStr(name);
    });

    if (!this.config.disableSearchAddon) {
      this.#initSearchShortcuts(win);
    } else {
      // Hotfix for Bug 1527898. We should remove those overrides as part of Bug 1527903.
      Object.assign(win.CodeMirror.commands, {
        find: null,
        findPersistent: null,
        findPersistentNext: null,
        findPersistentPrev: null,
        findNext: null,
        findPrev: null,
        clearSearch: null,
        replace: null,
        replaceAll: null,
      });
    }

    // Retrieve the cursor blink rate from user preference, or fall back to CodeMirror's
    // default value.
    let cursorBlinkingRate = win.CodeMirror.defaults.cursorBlinkRate;
    if (Services.prefs.prefHasUserValue(CARET_BLINK_TIME)) {
      cursorBlinkingRate = Services.prefs.getIntPref(
        CARET_BLINK_TIME,
        cursorBlinkingRate
      );
    }
    // This will be used in the animation-duration property we set on the cursor to
    // implement the blinking animation. If cursorBlinkingRate is 0 or less, the cursor
    // won't blink.
    cm.getWrapperElement().style.setProperty(
      "--caret-blink-time",
      `${Math.max(0, cursorBlinkingRate)}ms`
    );

    editors.set(this, cm);

    this.reloadPreferences = this.reloadPreferences.bind(this);
    this.setKeyMap = this.setKeyMap.bind(this, win);

    this.#prefObserver = new PrefObserver("devtools.editor.");
    this.#prefObserver.on(TAB_SIZE, this.reloadPreferences);
    this.#prefObserver.on(EXPAND_TAB, this.reloadPreferences);
    this.#prefObserver.on(AUTO_CLOSE, this.reloadPreferences);
    this.#prefObserver.on(AUTOCOMPLETE, this.reloadPreferences);
    this.#prefObserver.on(DETECT_INDENT, this.reloadPreferences);
    this.#prefObserver.on(ENABLE_CODE_FOLDING, this.reloadPreferences);

    this.reloadPreferences();

    // Init a map of the loaded keymap files. Should be of the form Map<String->Boolean>.
    this.#loadedKeyMaps = new Set();
    this.#prefObserver.on(KEYMAP_PREF, this.setKeyMap);
    this.setKeyMap();

    win.editor = this;
    const editorReadyEvent = new win.CustomEvent("editorReady");
    win.dispatchEvent(editorReadyEvent);
  }

  /**
   * Do the actual appending and configuring of the CodeMirror 6 instance.
   * This is used by appendTo and appendToLocalElement, and does all the hard work to
   * configure CodeMirror 6 with all the right options/modes/etc.
   * This should be kept in sync with #setup.
   *
   * @param {Element} el: Element into which the codeMirror editor should be appended.
   * @param {Document} document: Optional document, if not set, will default to el.ownerDocument
   */
  #setupCm6(el, doc) {
    this.#ownerDoc = doc || el.ownerDocument;
    const win = el.ownerDocument.defaultView;
    this.#win = win;

    this.#CodeMirror6 = this.#win.ChromeUtils.importESModule(
      "resource://devtools/client/shared/sourceeditor/codemirror6/codemirror6.bundle.mjs",
      { global: "current" }
    );

    const {
      codemirror,
      codemirrorView: {
        drawSelection,
        EditorView,
        keymap,
        lineNumbers,
        placeholder,
      },
      codemirrorState: { EditorState, Compartment, Prec },
      codemirrorSearch: { highlightSelectionMatches },
      codemirrorLanguage: {
        syntaxTreeAvailable,
        indentUnit,
        codeFolding,
        syntaxHighlighting,
        bracketMatching,
      },
      codemirrorLangJavascript,
      lezerHighlight,
    } = this.#CodeMirror6;

    const tabSizeCompartment = new Compartment();
    const indentCompartment = new Compartment();
    const lineWrapCompartment = new Compartment();
    const lineNumberCompartment = new Compartment();
    const lineNumberMarkersCompartment = new Compartment();
    const searchHighlightCompartment = new Compartment();
    const domEventHandlersCompartment = new Compartment();
    const foldGutterCompartment = new Compartment();

    this.#compartments = {
      tabSizeCompartment,
      indentCompartment,
      lineWrapCompartment,
      lineNumberCompartment,
      lineNumberMarkersCompartment,
      searchHighlightCompartment,
      domEventHandlersCompartment,
      foldGutterCompartment,
    };

    const { lineContentMarkerEffect, lineContentMarkerExtension } =
      this.#createlineContentMarkersExtension();

    const { positionContentMarkerEffect, positionContentMarkerExtension } =
      this.#createPositionContentMarkersExtension();

    this.#effects = { lineContentMarkerEffect, positionContentMarkerEffect };

    const indentStr = (this.config.indentWithTabs ? "\t" : " ").repeat(
      this.config.indentUnit || 2
    );

    // Track the scroll snapshot for the current document at the end of the scroll
    this.#editorDOMEventHandlers.scroll = [
      debounce(this.#cacheScrollSnapshot, 250),
    ];

    const extensions = [
      bracketMatching(),
      indentCompartment.of(indentUnit.of(indentStr)),
      tabSizeCompartment.of(EditorState.tabSize.of(this.config.tabSize)),
      lineWrapCompartment.of(
        this.config.lineWrapping ? EditorView.lineWrapping : []
      ),
      EditorState.readOnly.of(this.config.readOnly),
      lineNumberCompartment.of(this.config.lineNumbers ? lineNumbers() : []),
      codeFolding({
        placeholderText: "↔",
      }),
      foldGutterCompartment.of([]),
      syntaxHighlighting(lezerHighlight.classHighlighter),
      EditorView.updateListener.of(v => {
        if (!cm.isDocumentLoadComplete) {
          // Check that the full syntax tree is available the current viewport
          if (syntaxTreeAvailable(v.state, v.view.viewState.viewport.to)) {
            cm.isDocumentLoadComplete = true;
          }
        }
        if (v.viewportChanged || v.docChanged) {
          if (v.docChanged) {
            cm.isDocumentLoadComplete = false;
          }
          // reset line gutter markers for the new visible ranges
          // when the viewport changes(e.g when the page is scrolled).
          if (this.#lineGutterMarkers.size > 0) {
            this.setLineGutterMarkers();
          }
        }
        // Any custom defined update listener should be called
        if (typeof this.#updateListener == "function") {
          this.#updateListener(v);
        }
      }),
      domEventHandlersCompartment.of(
        EditorView.domEventHandlers(this.#createEventHandlers())
      ),
      lineNumberMarkersCompartment.of([]),
      lineContentMarkerExtension,
      positionContentMarkerExtension,
      searchHighlightCompartment.of(this.#searchHighlighterExtension([])),
      highlightSelectionMatches(),
      // keep last so other extension take precedence
      codemirror.minimalSetup,
    ];

    if (this.config.mode === Editor.modes.js) {
      extensions.push(codemirrorLangJavascript.javascript());
    }

    if (this.config.placeholder) {
      extensions.push(placeholder(this.config.placeholder));
    }

    if (this.config.keyMap) {
      extensions.push(Prec.highest(keymap.of(this.config.keyMap)));
    }

    if (Services.prefs.prefHasUserValue(CARET_BLINK_TIME)) {
      // We need to multiply the preference value by 2 to match Firefox cursor rate
      const cursorBlinkRate = Services.prefs.getIntPref(CARET_BLINK_TIME) * 2;
      extensions.push(
        drawSelection({
          cursorBlinkRate,
        })
      );
    }

    const cm = new EditorView({
      parent: el,
      extensions,
    });

    cm.isDocumentLoadComplete = false;
    editors.set(this, cm);

    // For now, we only need to pipe the blur event
    cm.contentDOM.addEventListener("blur", e => this.emit("blur", e), {
      signal: this.#abortController?.signal,
    });
  }

  /**
   * This creates the extension which handles marking of lines within the editor.
   *
   * @returns {Object} The object contains an extension and effects which used to trigger updates to the extension
   *          {Object} - lineContentMarkerExtension - The line content marker extension
   *          {Object} - lineContentMarkerEffect - The effects to add and remove markers
   *
   */
  #createlineContentMarkersExtension() {
    const {
      codemirrorView: { Decoration, WidgetType, EditorView },
      codemirrorState: { StateField, StateEffect },
    } = this.#CodeMirror6;

    const lineContentMarkers = this.#lineContentMarkers;

    class LineContentWidget extends WidgetType {
      constructor(line, value, markerId, createElementNode) {
        super();
        this.line = line;
        this.value = value;
        this.markerId = markerId;
        this.createElementNode = createElementNode;
      }

      toDOM() {
        return this.createElementNode(this.line, this.value);
      }

      eq(widget) {
        return (
          widget.line == this.line &&
          widget.markerId == this.markerId &&
          widget.value == this.value
        );
      }
    }

    /**
     * Uses the marker and current decoration list to create a new decoration list
     *
     * @param {Object} marker - The marker to be used to create the new decoration
     * @param {Transaction} transaction - The transaction object
     * @param {Array} newMarkerDecorations - List of the new marker decorations being built
     */
    function _buildDecorationsForMarker(
      marker,
      transaction,
      newMarkerDecorations
    ) {
      const vStartLine = transaction.state.doc.lineAt(
        marker._view.viewport.from
      );
      const vEndLine = transaction.state.doc.lineAt(marker._view.viewport.to);

      let decorationLines;
      if (marker.shouldMarkAllLines) {
        decorationLines = [];
        for (let i = vStartLine.number; i <= vEndLine.number; i++) {
          decorationLines.push({ line: i });
        }
      } else {
        decorationLines = marker.lines;
      }

      for (const { line, value } of decorationLines) {
        // Make sure the position is within the viewport
        if (line < vStartLine.number || line > vEndLine.number) {
          continue;
        }

        const lo = transaction.state.doc.line(line);
        if (marker.lineClassName) {
          // Markers used:
          // 1) blackboxed-line-marker
          // 2) multi-highlight-line-marker
          // 3) highlight-line-marker
          // 4) line-exception-marker
          // 5) debug-line-marker
          const classDecoration = Decoration.line({
            class: marker.lineClassName,
          });
          classDecoration.markerType = marker.id;
          newMarkerDecorations.push(classDecoration.range(lo.from));
        } else if (marker.createLineElementNode) {
          // Markers used:
          // 1) conditional-breakpoint-panel-marker
          // 2) inline-preview-marker
          const nodeDecoration = Decoration.widget({
            widget: new LineContentWidget(
              line,
              value,
              marker.id,
              marker.createLineElementNode
            ),
            // Render the widget after the cursor
            side: 1,
            block: !!marker.renderAsBlock,
          });
          nodeDecoration.markerType = marker.id;
          newMarkerDecorations.push(nodeDecoration.range(lo.to));
        }
      }
    }

    /**
     * This updates the decorations for the marker specified
     *
     * @param {Array} markerDecorations - The current decorations displayed in the document
     * @param {Array} marker - The current marker whose decoration should be update
     * @param {Transaction} transaction
     * @returns
     */
    function updateDecorations(markerDecorations, marker, transaction) {
      const newDecorations = [];
      _buildDecorationsForMarker(marker, transaction, newDecorations);

      return markerDecorations.update({
        // Filter out old decorations for the specified marker
        filter: (from, to, decoration) => {
          return decoration.markerType !== marker.id;
        },
        add: newDecorations,
        sort: true,
      });
    }

    /**
     * This updates all the decorations for all the markers. This
     * used in scenarios when an update to view (e.g vertically scrolling into a new viewport)
     * requires all the marker decoraions.
     *
     * @param {Array} markerDecorations - The current decorations displayed in the document
     * @param {Array} allMarkers - All the cached markers
     * @param {Object} transaction
     * @returns
     */
    function updateDecorationsForAllMarkers(
      markerDecorations,
      allMarkers,
      transaction
    ) {
      const allNewDecorations = [];

      for (const marker of allMarkers) {
        _buildDecorationsForMarker(marker, transaction, allNewDecorations);
      }

      return markerDecorations.update({
        // This filters out all the old decorations
        filter: () => false,
        add: allNewDecorations,
        sort: true,
      });
    }

    function removeDecorations(markerDecorations, markerId) {
      return markerDecorations.update({
        filter: (from, to, decoration) => {
          return decoration.markerType !== markerId;
        },
      });
    }

    // The effects used to create the transaction when markers are
    // either added and removed.
    const addEffect = StateEffect.define();
    const removeEffect = StateEffect.define();

    const lineContentMarkerExtension = StateField.define({
      create() {
        return Decoration.none;
      },
      update(markerDecorations, transaction) {
        // Map the decorations through the transaction changes, this is important
        // as it remaps the decorations from positions in the old document to
        // positions in the new document.
        markerDecorations = markerDecorations.map(transaction.changes);
        for (const effect of transaction.effects) {
          // When a new marker is added
          if (effect.is(addEffect)) {
            markerDecorations = updateDecorations(
              markerDecorations,
              effect.value,
              transaction
            );
          } else if (effect.is(removeEffect)) {
            // when a marker is removed
            markerDecorations = removeDecorations(
              markerDecorations,
              effect.value
            );
          } else {
            const cachedMarkers = lineContentMarkers.values();
            // For updates that are not related to this marker decoration,
            // we want to update the decorations when the editor is scrolled
            // and a new viewport is loaded.
            markerDecorations = updateDecorationsForAllMarkers(
              markerDecorations,
              cachedMarkers,
              transaction
            );
          }
        }
        return markerDecorations;
      },
      provide: field => EditorView.decorations.from(field),
    });

    return {
      lineContentMarkerExtension,
      lineContentMarkerEffect: { addEffect, removeEffect },
    };
  }

  #createEventHandlers() {
    const eventHandlers = {};
    for (const eventName in this.#editorDOMEventHandlers) {
      const handlers = this.#editorDOMEventHandlers[eventName];
      eventHandlers[eventName] = (event, editor) => {
        if (!event.target) {
          return;
        }
        for (const handler of handlers) {
          // Wait a cycle so the codemirror updates to the current cursor position,
          // information, TODO: Currently noticed this issue with CM6, not ideal but should
          // investigate further Bug 1890895.
          event.target.ownerGlobal.setTimeout(() => {
            const view = editor.viewState;
            const cursorPos = lezerUtils.positionToLocation(
              view.state.doc,
              view.state.selection.main.head
            );
            handler(event, view, cursorPos.line, cursorPos.column);
          }, 0);
        }
      };
    }
    return eventHandlers;
  }

  /**
   * Adds the DOM event handlers for the editor.
   * @param {Object} domEventHandlers - A dictionary of handlers for the DOM events
   *                                    the handlers are getting called with the following arguments
   *                                     - {Object} `event`: The DOM event
   *                                     - {Object} `view`: The codemirror view
   *                                     - {Number} cursorLine`: The line where the cursor is currently position
   *                                     - {Number} `cursorColumn`: The column where the cursor is currently position
   *                                     - {Number} `eventLine`: The line where the event was fired.
   *                                                             This might be different from the cursor line for mouse events.
   *                                     - {Number} `eventColumn`: The column where the event was fired.
   *                                                                This might be different from the cursor column for mouse events.
   */
  addEditorDOMEventListeners(domEventHandlers) {
    const cm = editors.get(this);
    const {
      codemirrorView: { EditorView },
    } = this.#CodeMirror6;

    // Update the cache of dom event handlers
    for (const eventName in domEventHandlers) {
      if (!this.#editorDOMEventHandlers[eventName]) {
        this.#editorDOMEventHandlers[eventName] = [];
      }
      this.#editorDOMEventHandlers[eventName].push(domEventHandlers[eventName]);
    }

    cm.dispatch({
      effects: this.#compartments.domEventHandlersCompartment.reconfigure(
        EditorView.domEventHandlers(this.#createEventHandlers())
      ),
    });
  }

  #cacheScrollSnapshot = () => {
    const cm = editors.get(this);
    if (!this.#currentDocumentId) {
      return;
    }
    this.#scrollSnapshots.set(this.#currentDocumentId, cm.scrollSnapshot());
    this.emitForTests("cm-editor-scrolled");
  };

  /**
   * Remove specified DOM event handlers for the editor.
   * @param {Object} domEventHandlers - A dictionary of handlers for the DOM events
   */
  removeEditorDOMEventListeners(domEventHandlers) {
    const cm = editors.get(this);
    const {
      codemirrorView: { EditorView },
    } = this.#CodeMirror6;

    for (const eventName in domEventHandlers) {
      const domEventHandler = domEventHandlers[eventName];
      const cachedEventHandlers = this.#editorDOMEventHandlers[eventName];
      if (!domEventHandler || !cachedEventHandlers) {
        continue;
      }
      const index = cachedEventHandlers.findIndex(
        handler => handler == domEventHandler
      );
      this.#editorDOMEventHandlers[eventName].splice(index, 1);
    }

    cm.dispatch({
      effects: this.#compartments.domEventHandlersCompartment.reconfigure(
        EditorView.domEventHandlers(this.#createEventHandlers())
      ),
    });
  }

  /**
   * Clear the DOM event handlers for the editor.
   */
  #clearEditorDOMEventListeners() {
    const cm = editors.get(this);
    const {
      codemirrorView: { EditorView },
    } = this.#CodeMirror6;

    this.#editorDOMEventHandlers = {};
    this.#gutterDOMEventHandlers = {};
    cm.dispatch({
      effects: this.#compartments.domEventHandlersCompartment.reconfigure(
        EditorView.domEventHandlers({})
      ),
    });
  }

  /**
   * This adds a marker used to add classes to editor line based on a condition.
   *   @property {object}             marker
   *                                  The rule rendering a marker or class.
   *   @property {object}             marker.id
   *                                  The unique identifier for this marker
   *   @property {string}             marker.lineClassName
   *                                  The css class to apply to the line
   *   @property {Array<Object>}      marker.lines
   *                                  The lines to add markers to. Each line object has a `line` and `value` property.
   *   @property {Boolean}           marker.renderAsBlock
   *                                  The specifies that the widget should be rendered as a block element. defaults to `false`. This is optional.
   *   @property {Boolean}           marker.shouldMarkAllLines
   *                                  Set to true to apply the marker to all the lines. In such case, `positions` is ignored. This is optional.
   *   @property {Function}           marker.createLineElementNode
   *                                  This should return the DOM element which is used for the marker. The line number is passed as a parameter.
   *                                  This is optional.
   */
  setLineContentMarker(marker) {
    const cm = editors.get(this);
    // We store the marker an the view state, this is gives access to view data
    // when defining updates to the StateField.
    marker._view = cm;
    this.#lineContentMarkers.set(marker.id, marker);
    cm.dispatch({
      effects: this.#effects.lineContentMarkerEffect.addEffect.of(marker),
    });
  }

  /**
   * This removes the marker which has the specified className
   * @param {string} markerId - The unique identifier for this marker
   */
  removeLineContentMarker(markerId) {
    const cm = editors.get(this);
    this.#lineContentMarkers.delete(markerId);
    cm.dispatch({
      effects: this.#effects.lineContentMarkerEffect.removeEffect.of(markerId),
    });
  }

  /**
   * This creates the extension used to manage the rendering of markers
   * at specific positions with the editor. e.g used for column breakpoints
   *
   * @returns {Object} The object contains an extension and effects which used to trigger updates to the extension
   *          {Object} - positionContentMarkerExtension - The position content marker extension
   *          {Object} - positionContentMarkerEffect - The effects to add and remove markers
   */
  #createPositionContentMarkersExtension() {
    const {
      codemirrorView: { Decoration, EditorView, WidgetType },
      codemirrorState: { StateField, StateEffect },
      codemirrorLanguage: { syntaxTree },
    } = this.#CodeMirror6;

    const cachedPositionContentMarkers = this.#posContentMarkers;

    class NodeWidget extends WidgetType {
      constructor({
        line,
        column,
        isFirstNonSpaceColumn,
        positionData,
        markerId,
        createElementNode,
        customEq,
      }) {
        super();
        this.line = line;
        this.column = column;
        this.isFirstNonSpaceColumn = isFirstNonSpaceColumn;
        this.positionData = positionData;
        this.markerId = markerId;
        this.customEq = customEq;
        this.toDOM = () =>
          createElementNode(line, column, isFirstNonSpaceColumn, positionData);
      }

      eq(widget) {
        let eq =
          this.line == widget.line &&
          this.column == widget.column &&
          this.markerId == widget.markerId;
        if (this.positionData && this.customEq) {
          eq = eq && this.customEq(this.positionData, widget.positionData);
        }
        return eq;
      }
    }

    function getIndentation(lineText) {
      if (!lineText) {
        return 0;
      }

      const lineMatch = lineText.match(/^\s*/);
      if (!lineMatch) {
        return 0;
      }
      return lineMatch[0].length;
    }

    function _buildDecorationsForPositionMarkers(
      marker,
      transaction,
      newMarkerDecorations
    ) {
      const viewport = marker._view.viewport;
      const vStartLine = transaction.state.doc.lineAt(viewport.from);
      const vEndLine = transaction.state.doc.lineAt(viewport.to);

      for (const position of marker.positions) {
        // If codemirror positions are provided (e.g from search cursor)
        // compare that directly.
        if (position.from && position.to) {
          if (position.from >= viewport.from && position.to <= viewport.to) {
            if (marker.positionClassName) {
              // Markers used:
              // 1. active-selection-marker
              const classDecoration = Decoration.mark({
                class: marker.positionClassName,
              });
              classDecoration.markerType = marker.id;
              newMarkerDecorations.push(
                classDecoration.range(position.from, position.to)
              );
            }
          }
          continue;
        }
        // If line and column are provided
        if (
          position.line >= vStartLine.number &&
          position.line <= vEndLine.number
        ) {
          const line = transaction.state.doc.line(position.line);
          // Make sure to track any indentation at the beginning of the line
          const column = Math.max(position.column, getIndentation(line.text));
          const pos = line.from + column;

          if (marker.createPositionElementNode) {
            // Markers used:
            // 1. column-breakpoint-marker
            const isFirstNonSpaceColumn = ONLY_SPACES_REGEXP.test(
              line.text.substr(0, column)
            );
            const nodeDecoration = Decoration.widget({
              widget: new NodeWidget({
                line: position.line,
                column: position.column,
                isFirstNonSpaceColumn,
                positionData: position.positionData,
                markerId: marker.id,
                createElementNode: marker.createPositionElementNode,
                customEq: marker.customEq,
              }),
              // Make sure the widget is rendered after the cursor
              // see https://codemirror.net/docs/ref/#view.Decoration^widget^spec.side for details.
              side: 1,
            });
            nodeDecoration.markerType = marker.id;
            newMarkerDecorations.push(nodeDecoration.range(pos, pos));
          }
          if (marker.positionClassName) {
            // Markers used:
            // 1. exception-position-marker
            // 2. debug-position-marker
            const tokenAtPos = syntaxTree(transaction.state).resolve(pos, 1);
            // While trying to update the markers, during content changes, the syntax tree is not
            // guaranteed to be complete, so there is the possibility of getting wrong `from` and `to` values for the token.
            // To make sure we are handling a valid token, let's check that the `from` value (which is the start position of the retrieved token)
            // matches the position we want.
            if (tokenAtPos.from !== pos) {
              continue;
            }
            const tokenString = line.text.slice(
              position.column,
              tokenAtPos.to - line.from
            );
            // Ignore any empty strings and opening braces
            if (
              tokenString === "" ||
              tokenString === "{" ||
              tokenString === "["
            ) {
              continue;
            }
            const classDecoration = Decoration.mark({
              class: marker.positionClassName,
            });
            classDecoration.markerType = marker.id;
            newMarkerDecorations.push(
              classDecoration.range(pos, tokenAtPos.to)
            );
          }
        }
      }
    }

    /**
     * This updates the decorations for the marker specified
     *
     * @param {Array} markerDecorations - The current decorations displayed in the document
     * @param {Array} marker - The current marker whose decoration should be update
     * @param {Transaction} transaction
     * @returns
     */
    function updateDecorations(markerDecorations, marker, transaction) {
      const newDecorations = [];

      _buildDecorationsForPositionMarkers(marker, transaction, newDecorations);
      return markerDecorations.update({
        filter: (from, to, decoration) => {
          return decoration.markerType !== marker.id;
        },
        add: newDecorations,
        sort: true,
      });
    }

    /**
     * This updates all the decorations for all the markers. This
     * used in scenarios when an update to view (e.g vertically scrolling into a new viewport)
     * requires all the marker decoraions.
     *
     * @param {Array} markerDecorations - The current decorations displayed in the document
     * @param {Array} markers - All the cached markers
     * @param {Object} transaction
     * @returns
     */
    function updateDecorationsForAllMarkers(
      markerDecorations,
      markers,
      transaction
    ) {
      const allNewDecorations = [];

      // Sort the markers iterator thanks to `displayLast` boolean.
      // This is typically used by the paused location marker to be shown after the column breakpoints.
      markers = Array.from(markers).sort((a, b) => {
        if (a.displayLast) {
          return 1;
        }
        if (b.displayLast) {
          return -1;
        }
        return 0;
      });

      for (const marker of markers) {
        _buildDecorationsForPositionMarkers(
          marker,
          transaction,
          allNewDecorations
        );
      }
      return markerDecorations.update({
        filter: () => false,
        add: allNewDecorations,
        sort: true,
      });
    }

    function removeDecorations(markerDecorations, markerId) {
      return markerDecorations.update({
        filter: (from, to, decoration) => {
          return decoration.markerType !== markerId;
        },
      });
    }

    const addEffect = StateEffect.define();
    const removeEffect = StateEffect.define();

    const positionContentMarkerExtension = StateField.define({
      create() {
        return Decoration.none;
      },
      update(markerDecorations, transaction) {
        // Map the decorations through the transaction changes, this is important
        // as it remaps the decorations from positions in the old document to
        // positions in the new document.
        markerDecorations = markerDecorations.map(transaction.changes);
        for (const effect of transaction.effects) {
          if (effect.is(addEffect)) {
            // When a new marker is added
            markerDecorations = updateDecorations(
              markerDecorations,
              effect.value,
              transaction
            );
          } else if (effect.is(removeEffect)) {
            // When a marker is removed
            markerDecorations = removeDecorations(
              markerDecorations,
              effect.value
            );
          } else {
            // For updates that are not related to this marker decoration,
            // we want to update the decorations when the editor is scrolled
            // and a new viewport is loaded.
            markerDecorations = updateDecorationsForAllMarkers(
              markerDecorations,
              cachedPositionContentMarkers.values(),
              transaction
            );
          }
        }
        return markerDecorations;
      },
      provide: field => EditorView.decorations.from(field),
    });

    return {
      positionContentMarkerExtension,
      positionContentMarkerEffect: { addEffect, removeEffect },
    };
  }

  /**
   * This adds a marker used to decorate token / content at a specific position .
   * @param {Object} marker
   * @param {String} marker.id
   * @param {Array<Object>} marker.positions - This includes the line / column and any optional positionData which defines each position.
   * @param {Function} marker.createPositionElementNode - This describes how to render the marker.
   *                                                      The position data (i.e line, column and positionData) are passed as arguments.
   * @param {Function} marker.customEq - A custom function to determine the equality of the marker. This allows the user define special conditions
   *                                     for when position details have changed and an update of the marker should happen.
   *                                     The positionData defined for the current and the previous instance of the marker are passed as arguments.
   */
  setPositionContentMarker(marker) {
    const cm = editors.get(this);

    // We store the marker an the view state, this is gives access to viewport data
    // when defining updates to the StateField.
    marker._view = cm;
    this.#posContentMarkers.set(marker.id, marker);
    cm.dispatch({
      effects: this.#effects.positionContentMarkerEffect.addEffect.of(marker),
    });
  }

  /**
   * This removes the marker which has the specified id
   * @param {string} markerId - The unique identifier for this marker
   */
  removePositionContentMarker(markerId) {
    const cm = editors.get(this);
    this.#posContentMarkers.delete(markerId);
    cm.dispatch({
      effects:
        this.#effects.positionContentMarkerEffect.removeEffect.of(markerId),
    });
  }

  /**
   * Set event listeners for the line gutter
   * @param {Object} domEventHandlers
   *
   * example usage:
   *  const domEventHandlers = { click(event) { console.log(event);} }
   */
  setGutterEventListeners(domEventHandlers) {
    const cm = editors.get(this);
    const {
      codemirrorView: { lineNumbers },
      codemirrorLanguage: { foldGutter },
    } = this.#CodeMirror6;

    for (const eventName in domEventHandlers) {
      const handler = domEventHandlers[eventName];
      this.#gutterDOMEventHandlers[eventName] = (view, line, event) => {
        line = view.state.doc.lineAt(line.from);
        handler(event, view, line.number);
      };
    }

    cm.dispatch({
      effects: [
        this.#compartments.lineNumberCompartment.reconfigure(
          lineNumbers({ domEventHandlers: this.#gutterDOMEventHandlers })
        ),
        this.#compartments.foldGutterCompartment.reconfigure(
          foldGutter({
            class: "cm6-dt-foldgutter",
            markerDOM: open => {
              if (!this.#ownerDoc) {
                return null;
              }
              const button = this.#ownerDoc.createElement("button");
              button.classList.add("cm6-dt-foldgutter__toggle-button");
              button.setAttribute("aria-expanded", open);
              return button;
            },
            domEventHandlers: this.#gutterDOMEventHandlers,
          })
        ),
      ],
    });
  }

  /**
   * This supports adding/removing of line classes or markers on the
   * line number gutter based on the defined conditions. This only supports codemirror 6.
   *
   *   @param {Array<Marker>} markers         - The list of marker objects which defines the rules
   *                                            for rendering each marker.
   *   @property {object}     marker - The rule rendering a marker or class. This is required.
   *   @property {string}     marker.id - The unique identifier for this marker.
   *   @property {string}     marker.lineClassName - The css class to add to the line. This is required.
   *   @property {function}   marker.condition - The condition that decides if the marker/class gets added or removed.
   *                                              This should return `false` for lines where the marker should not be added and the
   *                                              result of the condition for any other line.
   *   @property {function=}  marker.createLineElementNode - This gets the line and the result of the condition as arguments and should return the DOM element which
   *                                            is used for the marker. This is optional.
   */
  setLineGutterMarkers(markers) {
    const cm = editors.get(this);

    if (markers) {
      // Cache the markers for use later. See next comment
      for (const marker of markers) {
        if (!marker.id) {
          throw new Error("Marker has no unique identifier");
        }
        this.#lineGutterMarkers.set(marker.id, marker);
      }
    }
    // When no markers are passed, the cached markers are used to update the line gutters.
    // This is useful for re-rendering the line gutters when the viewport changes
    // (note: the visible ranges will be different) in this case, mainly when the editor is scrolled.
    else if (!this.#lineGutterMarkers.size) {
      return;
    }
    markers = Array.from(this.#lineGutterMarkers.values());

    const {
      codemirrorView: { lineNumberMarkers, GutterMarker },
      codemirrorState: { RangeSetBuilder },
    } = this.#CodeMirror6;

    // This creates a new GutterMarker https://codemirror.net/docs/ref/#view.GutterMarker
    // to represents how each line gutter is rendered in the view.
    // This is set as the value for the Range https://codemirror.net/docs/ref/#state.Range
    // which represents the line.
    class LineGutterMarker extends GutterMarker {
      constructor(className, lineNumber, createElementNode, conditionResult) {
        super();
        this.elementClass = className || null;
        this.lineNumber = lineNumber;
        this.createElementNode = createElementNode;
        this.conditionResult = conditionResult;

        this.toDOM = createElementNode
          ? () => createElementNode(lineNumber, conditionResult)
          : null;
      }

      eq(marker) {
        return (
          marker.lineNumber == this.lineNumber &&
          marker.conditionResult == this.conditionResult
        );
      }
    }

    // Loop through the visible ranges https://codemirror.net/docs/ref/#view.EditorView.visibleRanges
    // (representing the lines in the current viewport) and generate a new rangeset for updating the line gutter
    // based on the conditions defined in the markers(for each line) provided.
    const builder = new RangeSetBuilder();
    const { from, to } = cm.viewport;
    let pos = from;
    while (pos <= to) {
      const line = cm.state.doc.lineAt(pos);
      for (const {
        lineClassName,
        condition,
        createLineElementNode,
      } of markers) {
        if (typeof condition !== "function") {
          throw new Error("The `condition` is not a valid function");
        }
        const conditionResult = condition(line.number);
        if (conditionResult !== false) {
          builder.add(
            line.from,
            line.to,
            new LineGutterMarker(
              lineClassName,
              line.number,
              createLineElementNode,
              conditionResult
            )
          );
        }
      }
      pos = line.to + 1;
    }

    // To update the state with the newly generated marker range set, a dispatch is called on the view
    // with an transaction effect created by the lineNumberMarkersCompartment, which is used to update the
    // lineNumberMarkers extension configuration.
    cm.dispatch({
      effects: this.#compartments.lineNumberMarkersCompartment.reconfigure(
        lineNumberMarkers.of(builder.finish())
      ),
    });
  }

  /**
   * This creates the extension used to manage the rendering of markers for
   * results for any search pattern
   * @param {RegExp}      pattern - The search pattern
   * @param {String}      className - The class used to decorate each result
   * @returns {Array<ViewPlugin>} An extension which is an array containing the view
   *                              which manages the rendering of the line content markers.
   */
  #searchHighlighterExtension({
    /* This defaults to matching nothing */ pattern = /.^/g,
    className = "",
  }) {
    const cm = editors.get(this);
    if (!cm) {
      return [];
    }
    const {
      codemirrorView: { Decoration, ViewPlugin, EditorView, MatchDecorator },
      codemirrorSearch: { RegExpCursor },
    } = this.#CodeMirror6;

    this.searchState.query = pattern;
    const searchCursor = new RegExpCursor(cm.state.doc, pattern, {
      ignoreCase: pattern.ignoreCase,
    });
    this.searchState.cursors = Array.from(searchCursor);
    this.searchState.currentCursorIndex = -1;

    const patternMatcher = new MatchDecorator({
      regexp: pattern,
      decorate: (add, from, to) => {
        add(from, to, Decoration.mark({ class: className }));
      },
    });

    const searchHighlightView = ViewPlugin.fromClass(
      class {
        decorations;
        constructor(view) {
          this.decorations = patternMatcher.createDeco(view);
        }
        update(viewUpdate) {
          this.decorations = patternMatcher.updateDeco(
            viewUpdate,
            this.decorations
          );
        }
      },
      {
        decorations: instance => instance.decorations,
        provide: plugin =>
          EditorView.atomicRanges.of(view => {
            return view.plugin(plugin)?.decorations || Decoration.none;
          }),
      }
    );

    return [searchHighlightView];
  }

  /**
   * This should add the class to the results of a search pattern specified
   *
   * @param {RegExp} pattern - The search pattern
   * @param {String} className - The class used to decorate each result
   */
  highlightSearchMatches(pattern, className) {
    const cm = editors.get(this);
    cm.dispatch({
      effects: this.#compartments.searchHighlightCompartment.reconfigure(
        this.#searchHighlighterExtension({ pattern, className })
      ),
    });
  }

  /**
   * This clear any decoration on all the search results
   */
  clearSearchMatches() {
    this.highlightSearchMatches(undefined, "");
  }

  /**
   * Retrieves the cursor for the next selection to be highlighted
   *
   * @param {Boolean} reverse - Determines the direction of the cursor movement
   * @returns {RegExpSearchCursor}
   */
  getNextSearchCursor(reverse) {
    if (reverse) {
      if (this.searchState.currentCursorIndex == 0) {
        this.searchState.currentCursorIndex =
          this.searchState.cursors.length - 1;
      } else {
        this.searchState.currentCursorIndex--;
      }
    } else if (
      this.searchState.currentCursorIndex ==
      this.searchState.cursors.length - 1
    ) {
      this.searchState.currentCursorIndex = 0;
    } else {
      this.searchState.currentCursorIndex++;
    }
    return this.searchState.cursors[this.searchState.currentCursorIndex];
  }

  /**
   * Get the start and end locations of the current viewport
   *
   * @param {Number} offsetHorizontalCharacters - Offset of characters offscreen
   * @param {Number} offsetVerticalLines - Offset of lines offscreen
   * @returns {Object}  - The location information for the current viewport
   */
  getLocationsInViewport(
    offsetHorizontalCharacters = 0,
    offsetVerticalLines = 0
  ) {
    if (this.isDestroyed()) {
      return null;
    }
    const cm = editors.get(this);
    let startLine, endLine, scrollLeft, charWidth, rightPosition;
    if (this.config.cm6) {
      // Report no viewport until we show an actual source (and not a loading/error message)
      if (!this.#currentDocumentId) {
        return null;
      }
      const { from, to } = cm.viewport;
      startLine = cm.state.doc.lineAt(from).number - offsetVerticalLines;
      endLine = cm.state.doc.lineAt(to).number + offsetVerticalLines;
      scrollLeft = cm.scrollDOM.scrollLeft;
      charWidth = cm.defaultCharacterWidth;
      rightPosition = scrollLeft + cm.dom.getBoundingClientRect().width;
    } else {
      if (!cm) {
        return null;
      }

      const scrollArea = cm.getScrollInfo();
      const rect = cm.getWrapperElement().getBoundingClientRect();
      startLine = cm.lineAtHeight(rect.top, "window") - offsetVerticalLines;
      endLine = cm.lineAtHeight(rect.bottom, "window") + offsetVerticalLines;
      scrollLeft = cm.doc.scrollLeft;
      charWidth = cm.defaultCharWidth();
      rightPosition = scrollLeft + (scrollArea.clientWidth - 30);
    }

    return {
      start: {
        line: startLine,
        column:
          scrollLeft > 0
            ? Math.floor(scrollLeft / charWidth) - offsetHorizontalCharacters
            : 0,
      },
      end: {
        line: endLine,
        column:
          Math.floor(rightPosition / charWidth) + offsetHorizontalCharacters,
      },
    };
  }

  /**
   * Gets the position information for the current selection
   * @returns {Object} cursor      - The location information for the  current selection
   *                   cursor.from - An object with the starting line / column of the selection
   *                   cursor.to   - An object with the end line / column of the selection
   */
  getSelectionCursor() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const selection = cm.state.selection.ranges[0];
      const lineFrom = cm.state.doc.lineAt(selection.from);
      const lineTo = cm.state.doc.lineAt(selection.to);
      return {
        from: {
          line: lineFrom.number,
          ch: selection.from - lineFrom.from,
        },
        to: {
          line: lineTo.number,
          ch: selection.to - lineTo.from,
        },
      };
    }
    return {
      from: cm.getCursor("from"),
      to: cm.getCursor("to"),
    };
  }

  /**
   * Gets the text content for the current selection
   * @returns {String}
   */
  getSelectedText() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const selection = cm.state.selection.ranges[0];
      return cm.state.doc.sliceString(selection.from, selection.to);
    }
    return cm.getSelection().trim();
  }

  /**
   * Returns the token at a specific position in the source
   *
   * @param   {Object} cm
   * @param   {Object} position
   * @param   {Number} position.line - line in the source
   * @param   {Number} position.column  - column on a line in the source
   * @returns {Object|null} token - start position of the token, end position of the token and
   *                                      the  type of the token. Returns null if no token are
   *                                      found at the passed coords
   */
  #tokenAtCoords(cm, { line, column }) {
    if (this.config.cm6) {
      const {
        codemirrorLanguage: { syntaxTree },
      } = this.#CodeMirror6;

      const token = lezerUtils.getTreeNodeAtLocation(
        cm.state.doc,
        syntaxTree(cm.state),
        { line, column }
      );
      if (!token) {
        return null;
      }

      return {
        startColumn: column,
        endColumn: token.to - token.from,
        type: token.type?.name,
      };
    }
    if (line < 0 || line >= cm.lineCount()) {
      return null;
    }

    const token = cm.getTokenAt({ line: line - 1, ch: column });
    if (!token) {
      return null;
    }

    return { startColumn: token.start, endColumn: token.end, type: token.type };
  }

  /**
   * Returns the expression at the specified position.
   *
   * The strategy of querying codeMirror tokens was borrowed
   * from Chrome's inital implementation in JavaScriptSourceFrame.js#L414
   *
   * @param {Object} coord
   * @param {Number} coord.line - line in the source
   * @param {Number} coord.column  - column on a line in the source
   * @return {Object|null} An object with the following properties:
   *                       - {String} `expression`: The expression at specified coordinate
   *                       - {Object} `location`: start and end lines/columns of the expression
   *                       Returns null if no suitable expression could be found at passed coordinates
   */
  getExpressionFromCoords(coord) {
    const cm = editors.get(this);
    const token = this.#tokenAtCoords(cm, coord);
    if (!token) {
      return null;
    }

    let expressionStart = token.startColumn;
    const expressionEnd = token.endColumn;
    const lineNumber = coord.line;

    const lineText = this.config.cm6
      ? cm.state.doc.line(coord.line).text
      : cm.doc.getLine(coord.line - 1);

    while (
      expressionStart > 1 &&
      lineText.charAt(expressionStart - 1) === "."
    ) {
      const tokenBefore = this.#tokenAtCoords(cm, {
        line: coord.line,
        column: expressionStart - 2,
      });

      if (!tokenBefore?.type) {
        return null;
      }

      expressionStart = tokenBefore.startColumn;
    }

    const expression = lineText.substring(expressionStart, expressionEnd) || "";

    if (!expression) {
      return null;
    }

    const location = {
      start: { line: lineNumber, column: expressionStart },
      end: { line: lineNumber, column: expressionEnd },
    };
    return { expression, location };
  }

  /**
   * Given screen coordinates this should return the line and column
   * related. This used currently to determine the line and columns
   * for the tokens that are hovered over.
   * @param {Number} left - Horizontal position from the left
   * @param {Number} top - Vertical position from the top
   * @returns {Object} position - The line and column related to the screen coordinates.
   */
  getPositionAtScreenCoords(left, top) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const position = cm.posAtCoords(
        { x: left, y: top },
        // "precise", i.e. if a specific position cannot be determined, an estimated one will be used
        false
      );
      const line = cm.state.doc.lineAt(position);
      return {
        line: line.number,
        column: position - line.from,
      };
    }
    const { line, ch } = cm.coordsChar(
      { left, top },
      // Use the "window" context where the coordinates are relative to the top-left corner
      // of the currently visible (scrolled) window.
      // This enables codemirror also correctly handle wrappped lines in the editor.
      "window"
    );
    return {
      line: line + 1,
      column: ch,
    };
  }

  /**
   * Check that text is selected
   * @returns {Boolean}
   */
  isTextSelected() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const selection = cm.state.selection.ranges[0];
      return selection.from !== selection.to;
    }
    return cm.somethingSelected();
  }

  /**
   * Returns a boolean indicating whether the editor is ready to
   * use. Use appendTo(el).then(() => {}) for most cases
   */
  isAppended() {
    return editors.has(this);
  }

  /**
   * Returns the currently active highlighting mode.
   * See Editor.modes for the list of all suppoert modes.
   */
  getMode() {
    return this.getOption("mode");
  }

  /**
   * Loads a script into editor's containing window.
   */
  loadScript(url) {
    if (!this.container) {
      throw new Error("Can't load a script until the editor is loaded.");
    }
    const win = this.container.contentWindow.wrappedJSObject;
    Services.scriptloader.loadSubScript(url, win);
  }

  /**
   * Creates a CodeMirror Document
   *
   * @param {String} text: Initial text of the document
   * @param {Object|String} mode: Mode of the document. See https://codemirror.net/5/doc/manual.html#option_mode
   * @returns CodeMirror.Doc
   */
  createDocument(text = "", mode) {
    return new this.Doc(text, mode);
  }

  /**
   * Replaces the current document with a new source document
   */
  replaceDocument(doc) {
    const cm = editors.get(this);
    cm.swapDoc(doc);
  }

  /**
   * Changes the value of a currently used highlighting mode.
   * See Editor.modes for the list of all supported modes.
   */
  setMode(value) {
    this.setOption("mode", value);

    // If autocomplete was set up and the mode is changing, then
    // turn it off and back on again so the proper mode can be used.
    if (this.config.autocomplete) {
      this.setOption("autocomplete", false);
      this.setOption("autocomplete", true);
    }
  }

  /**
   * The source editor can expose several commands linked from system and context menus.
   * Kept for backward compatibility with styleeditor.
   */
  insertCommandsController() {
    const {
      insertCommandsController,
    } = require("resource://devtools/client/shared/sourceeditor/editor-commands-controller.js");
    insertCommandsController(this);
  }

  /**
   * Returns text from the text area. If line argument is provided
   * the method returns only that line.
   */
  getText(line) {
    const cm = editors.get(this);

    if (line == null) {
      return this.config.cm6 ? cm.state.doc.toString() : cm.getValue();
    }

    const info = this.lineInfo(line);
    return info ? info.text : "";
  }

  getDoc() {
    if (!this.config) {
      return null;
    }
    const cm = editors.get(this);
    if (this.config.cm6) {
      if (!this.#currentDocument) {
        // A key for caching the WASM content in the WeakMap
        this.#currentDocument = { id: this.#currentDocumentId };
      }
      return this.#currentDocument;
    }
    return cm.getDoc();
  }

  get isWasm() {
    return wasm.isWasm(this.getDoc());
  }

  getWasmLineNumberFormatter() {
    return wasm.getWasmLineNumberFormatter(this.getDoc());
  }

  wasmOffsetToLine(offset) {
    return wasm.wasmOffsetToLine(this.getDoc(), offset);
  }

  lineToWasmOffset(number) {
    return wasm.lineToWasmOffset(this.getDoc(), number);
  }

  toLineIfWasmOffset(maybeOffset) {
    if (typeof maybeOffset !== "number" || !this.isWasm) {
      return maybeOffset;
    }
    return this.wasmOffsetToLine(maybeOffset);
  }

  renderWasmText(content) {
    return wasm.renderWasmText(this.getDoc(), content);
  }

  /**
   * Gets details about the line
   *
   * @param {Number} line
   * @returns {Object} line info object
   */
  lineInfo(line) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const el = this.getElementAtLine(line);
      return {
        text: el.innerText,
        // TODO: Expose those, or see usage for those and do things differently
        line: null,
        gutterMarkers: null,
        textClass: null,
        bgClass: null,
        wrapClass: el.className,
        widgets: null,
      };
    }

    return cm.lineInfo(line);
  }

  /**
   * Get the functions symbols for the current source loaded in the
   * the editor.
   *
   * @param {Number} maxResults - The maximum no of results to display
   */
  async getFunctionSymbols(maxResults) {
    const cm = editors.get(this);
    const { codemirrorLanguage } = this.#CodeMirror6;

    const functionSymbols = [];
    let resultsCount = 0;
    await lezerUtils.walkTree(cm, codemirrorLanguage, {
      filterSet: lezerUtils.nodeTypeSets.functionsDeclAndExpr,
      enterVisitor: node => {
        if (resultsCount == maxResults) {
          return;
        }
        const syntaxNode = node.node;
        const name = lezerUtils.getFunctionName(cm.state.doc, syntaxNode);
        // Ignore anonymous functions
        if (name == null) {
          return;
        }

        functionSymbols.push({
          name,
          klass: lezerUtils.getFunctionClass(cm.state.doc, syntaxNode),
          location: {
            start: lezerUtils.positionToLocation(cm.state.doc, node.from),
            end: lezerUtils.positionToLocation(cm.state.doc, node.to),
          },
          parameterNames: lezerUtils.getFunctionParameterNames(
            cm.state.doc,
            syntaxNode
          ),
          identifier: null,
          index: node.index,
        });
        resultsCount++;
      },
      forceParseTo: cm.state.doc.length,
    });

    return functionSymbols;
  }

  /**
   * Get the class symbols for the current source loaded in the the editor.
   *
   * @returns
   */
  async getClassSymbols() {
    const cm = editors.get(this);
    const { codemirrorLanguage } = this.#CodeMirror6;

    const classSymbols = [];
    await lezerUtils.walkTree(cm, codemirrorLanguage, {
      filterSet: lezerUtils.nodeTypeSets.classes,
      enterVisitor: node => {
        const classVarDefNode = node.node.firstChild.nextSibling;
        classSymbols.push({
          name: cm.state.doc.sliceString(
            classVarDefNode.from,
            classVarDefNode.to
          ),
          location: {
            start: lezerUtils.positionToLocation(cm.state.doc, node.from),
            end: lezerUtils.positionToLocation(cm.state.doc, node.to),
          },
        });
      },
      forceParseTo: cm.state.doc.length,
    });

    return classSymbols;
  }

  /**
   * Finds the best function name for the location specified.
   * This is used to map original function names to their corresponding
   * generated functions.
   *
   * @param {Object} location
   * @returns
   */
  async getClosestFunctionName(location) {
    const cm = editors.get(this);
    const {
      codemirrorLangJavascript: { javascriptLanguage },
      codemirrorLanguage: { forceParsing, syntaxTree },
    } = this.#CodeMirror6;

    let doc, tree;
    // If the specified source is already loaded in the editor,
    // codemirror has likely parsed most or all the source needed,
    // just leverage that
    const sourceId = location.source.id;
    if (this.#currentDocumentId === sourceId) {
      doc = cm.state.doc;
      // Parse the rest of the if needed.
      await forceParsing(cm, doc.length, 10000);

      tree = syntaxTree(cm.state);
    } else {
      // If the source is not currently loaded in the editor we will need
      // to explicitly parse its source text.
      // Note: The `loadSourceText` actions is called before this util `getClosestFunctionName`
      // to make sure source content is available to use.
      const sourceContent = this.#sources.get(location.source.id);
      if (!sourceContent) {
        console.error(
          `Can't find source content for ${location.source.id}, no function name can be determined`
        );
        return "";
      }

      // Create a codemirror document for the current source text.
      doc = cm.state.toText(sourceContent);
      tree = lezerUtils.getTree(javascriptLanguage, sourceId, sourceContent);
    }

    const token = lezerUtils.getTreeNodeAtLocation(doc, tree, location);
    if (!token) {
      return null;
    }

    const enclosingScope = lezerUtils.getEnclosingFunction(doc, token);
    return enclosingScope ? enclosingScope.funcName : "";
  }

  /**
   * Traverse the syntaxTree and return expressions
   * which best match the specified token location is on our
   * list of accepted symbol types.
   *
   * @param {Object} tokenLocation
   * @returns {Array} Member expression matches
   */
  async findBestMatchExpressions(tokenLocation) {
    const cm = editors.get(this);
    const { codemirrorLanguage } = this.#CodeMirror6;

    const expressions = [];

    const line = cm.state.doc.line(tokenLocation.line);
    const tokPos = line.from + tokenLocation.column;

    await lezerUtils.walkTree(cm, codemirrorLanguage, {
      filterSet: lezerUtils.nodeTypeSets.expressions,
      enterVisitor: node => {
        if (node.from <= tokPos && node.to >= tokPos) {
          expressions.push({
            type: node.name,
            // Computed member expressions not currently supported
            computed: false,
            expression: cm.state.doc.sliceString(node.from, node.to),
            location: {
              start: lezerUtils.positionToLocation(cm.state.doc, node.from),
              end: lezerUtils.positionToLocation(cm.state.doc, node.to),
            },
            from: node.from,
            to: node.to,
          });
        }
      },
      walkFrom: line.from,
      walkTo: line.to,
    });

    // There might be multiple expressions which are within the locations.
    // We want to match expressions based on dots before the desired token.
    //
    // ========================== EXAMPLE 1 ================================
    // Full Expression: `this.myProperty.x`
    // Hovered Token: `myProperty`
    // Found Expressions:
    // { name: "MemberExpression", expression: "this.myProperty.x", from: 1715, to: 1732 }
    // { name: "MemberExpression", expression: "this.myProperty" from: 1715, to: 1730 } *
    // { name: "PropertyName", expression: "myProperty" from: 1720, to: 1730 }
    //
    // ========================== EXAMPLE 2 ==================================
    // Full Expression: `a(b).catch`
    // Hovered Token: `b`
    // Found Expressions:
    // { name: "MemberExpression", expression: "a(b).catch", from: 1921  to: 1931 }
    // { name: "VariableName", expression: "b", from: 1923  to: 1924 } *
    //
    // We sort based on the `to` make sure we return the correct property
    return expressions.sort((a, b) => {
      if (a.to < b.to) {
        return -1;
      } else if (a.to > b.to) {
        return 1;
      }
      return 0;
    });
  }

  /**
   * Get all the lines which are inscope when paused a the specified location.
   *
   * @param {Object} location
   * @param {Array} in scope lines
   */
  async getInScopeLines(location) {
    const cm = editors.get(this);
    const { codemirrorLanguage } = this.#CodeMirror6;

    // Converts the CM6 position to a source line
    function posToLine(view, pos) {
      const line = view.state.doc.lineAt(pos);
      return line.number;
    }

    const functionLocations = [];

    await lezerUtils.walkTree(cm, codemirrorLanguage, {
      filterSet: lezerUtils.nodeTypeSets.functions,
      enterVisitor: node => {
        functionLocations.push({
          name: node.name,
          startLine: posToLine(cm, node.from),
          endLine: posToLine(cm, node.to),
        });
      },
      forceParseTo: cm.viewport.to,
    });

    // Sort based on the start locations so the scopes
    // are in the same order as in the source.
    const sortedLocations = scopeUtils.sortByStart(functionLocations);

    // Any function locations which are within the immediate function scope
    // of the paused location.
    const innerLocations = scopeUtils.getInnerLocations(
      sortedLocations,
      location
    );

    // Any outer locations which do not contain the immediate function
    // of the paused location
    const outerLocations = sortedLocations.filter(loc => {
      if (innerLocations.includes(loc)) {
        return false;
      }
      return !scopeUtils.containsPosition(loc, location);
    });

    const outOfScopeLines = scopeUtils.getOutOfScopeLines(
      scopeUtils.removeOverlapLocations(outerLocations)
    );

    // This operation can be very costly for large files so we sacrifice a bit of readability
    // for performance sake.
    // We initialize an array with a fixed size and we'll directly assign value for lines
    // that are not out of scope. This is much faster than having an empty array and pushing
    // into it.
    const sourceNumLines = cm.state.doc.lines;
    const sourceLines = new Array(sourceNumLines);
    for (let i = 0; i < sourceNumLines; i++) {
      const line = i + 1;
      if (outOfScopeLines.size == 0 || !outOfScopeLines.has(line)) {
        sourceLines[i] = line;
      }
    }

    // Finally we need to remove any undefined values, i.e. the ones that were matching
    // out of scope lines.
    return sourceLines.filter(i => i != undefined);
  }

  /**
   * Gets the location and meta details of all the references
   * (within the scope of the immediate enclosing function of the specified location)
   * which are related to the bindings specified.
   *
   * @param {Object} location
   * @param {Array<String>} bindings -  list of binding names
   * @returns {Function}
   **/
  async getBindingReferences(location, bindings) {
    const cm = editors.get(this);
    const {
      codemirrorLanguage: { syntaxTree },
    } = this.#CodeMirror6;

    const token = lezerUtils.getTreeNodeAtLocation(
      cm.state.doc,
      syntaxTree(cm.state),
      location
    );
    if (!token) {
      return null;
    }

    const enclosingScope = lezerUtils.getEnclosingFunction(
      cm.state.doc,
      token,
      { includeAnonymousFunctions: true }
    );

    if (!enclosingScope) {
      return null;
    }

    const bindingReferences = {};
    // This should find location and meta information for the binding name specified.
    await lezerUtils.walkCursor(enclosingScope.node.cursor(), {
      filterSet: lezerUtils.nodeTypeSets.bindingReferences,
      enterVisitor: node => {
        const bindingName = cm.state.doc.sliceString(node.from, node.to);
        if (!bindings.includes(bindingName)) {
          return;
        }
        const ref = {
          start: lezerUtils.positionToLocation(cm.state.doc, node.from),
          end: lezerUtils.positionToLocation(cm.state.doc, node.to),
        };
        const syntaxNode = node.node;
        // Previews for member expressions are built of the meta property which is
        // reference of the child property and so on. e.g a.b.c
        if (syntaxNode.parent.name == lezerUtils.nodeTypes.MemberExpression) {
          ref.meta = lezerUtils.getMetaBindings(
            cm.state.doc,
            syntaxNode.parent
          );
        }

        if (!bindingReferences[bindingName]) {
          bindingReferences[bindingName] = { refs: [] };
        }
        bindingReferences[bindingName].refs.push(ref);
      },
    });

    return bindingReferences;
  }

  /**
   * Replaces whatever is in the text area with the contents of
   * the 'value' argument.
   *
   * @param {String} value: The text to replace the editor content
   * @param {String} documentId: Optional unique id represeting the specific document which is source of the text.
   *                 Will be null for loading and error messages.
   */
  async setText(value, documentId) {
    const cm = editors.get(this);
    const isWasm = typeof value !== "string" && "binary" in value;

    if (documentId) {
      this.#currentDocumentId = documentId;
    } else {
      // Reset this ID when showing loading and error messages,
      // so that we keep track when an actual source is displayed
      this.#currentDocumentId = null;
    }

    if (isWasm) {
      // wasm?
      // binary does not survive as Uint8Array, converting from string
      const binary = value.binary;
      const data = new Uint8Array(binary.length);
      for (let i = 0; i < data.length; i++) {
        data[i] = binary.charCodeAt(i);
      }

      const { lines, done } = wasm.getWasmText(this.getDoc(), data);
      const MAX_LINES = 10000000;
      if (lines.length > MAX_LINES) {
        lines.splice(MAX_LINES, lines.length - MAX_LINES);
        lines.push(";; .... text is truncated due to the size");
      }
      if (!done) {
        lines.push(";; .... possible error during wast conversion");
      }

      if (this.config.cm6) {
        value = lines.join("\n");
      } else {
        // cm will try to split into lines anyway, saving memory
        value = { split: () => lines };
      }
    }

    if (this.config.cm6) {
      if (cm.state.doc.toString() == value) {
        return;
      }

      const {
        codemirrorView: { EditorView, lineNumbers },
      } = this.#CodeMirror6;

      await cm.dispatch({
        changes: { from: 0, to: cm.state.doc.length, insert: value },
        selection: { anchor: 0 },
      });

      const effects = [];
      if (this.config?.lineNumbers) {
        const lineNumbersConfig = {
          domEventHandlers: this.#gutterDOMEventHandlers,
        };
        if (isWasm) {
          lineNumbersConfig.formatNumber = this.getWasmLineNumberFormatter();
        }
        effects.push(
          this.#compartments.lineNumberCompartment.reconfigure(
            lineNumbers(lineNumbersConfig)
          )
        );
      }
      // Get the cached scroll snapshot for this source and restore
      // the scroll position. Note: The scroll has to be done in a seperate dispatch
      // (after the previous dispatch has set the document), this is because
      // it is required that the document the scroll snapshot is applied to
      // is the exact document it was saved on.
      const scrollSnapshot = this.#scrollSnapshots.get(documentId);

      effects.push(
        scrollSnapshot ? scrollSnapshot : EditorView.scrollIntoView(0)
      );

      await cm.dispatch({ effects });

      if (this.currentDocumentId) {
        // If there is no scroll snapshot explicitly cache the snapshot set as no scroll
        // is triggered.
        if (!scrollSnapshot) {
          this.#cacheScrollSnapshot();
        }
      }
    } else {
      cm.setValue(value);
    }

    this.resetIndentUnit();
  }

  addSource(id, sourceText) {
    this.#sources.set(id, sourceText);
  }

  clearSources(ids) {
    if (ids) {
      for (const id of ids) {
        this.#sources.delete(id);
      }
    } else {
      this.#sources.clear();
      lezerUtils.clear();
    }
  }

  /* Currently used only in tests */
  sourcesCount() {
    return this.#sources.size;
  }

  /**
   * Reloads the state of the editor based on all current preferences.
   * This is called automatically when any of the relevant preferences
   * change.
   */
  reloadPreferences() {
    // Restore the saved autoCloseBrackets value if it is preffed on.
    const useAutoClose = Services.prefs.getBoolPref(AUTO_CLOSE);
    this.setOption(
      "autoCloseBrackets",
      useAutoClose ? this.config.autoCloseBracketsSaved : false
    );

    this.updateCodeFoldingGutter();

    this.resetIndentUnit();
    this.setupAutoCompletion();
  }

  /**
   * Set the current keyMap for CodeMirror, and load the support file if needed.
   *
   * @param {Window} win: The window on which the keymap files should be loaded.
   */
  setKeyMap(win) {
    if (this.config.isReadOnly) {
      return;
    }

    const keyMap = Services.prefs.getCharPref(KEYMAP_PREF);

    // If alternative keymap is provided, use it.
    if (VALID_KEYMAPS.has(keyMap)) {
      if (!this.#loadedKeyMaps.has(keyMap)) {
        Services.scriptloader.loadSubScript(VALID_KEYMAPS.get(keyMap), win);
        this.#loadedKeyMaps.add(keyMap);
      }
      this.setOption("keyMap", keyMap);
    } else {
      this.setOption("keyMap", "default");
    }
  }

  /**
   * Sets the editor's indentation based on the current prefs and
   * re-detect indentation if we should.
   */
  resetIndentUnit() {
    if (this.isDestroyed()) {
      return;
    }
    const cm = editors.get(this);
    const iterFn = (start, maxEnd, callback) => {
      if (!this.config.cm6) {
        if (this.isDestroyed()) {
          return;
        }
        cm.eachLine(start, maxEnd, line => {
          return callback(line.text);
        });
      } else {
        const iterator = cm.state.doc.iterLines(
          start + 1,
          Math.min(cm.state.doc.lines, maxEnd) + 1
        );
        let callbackRes;
        do {
          iterator.next();
          callbackRes = callback(iterator.value);
        } while (iterator.done !== true && !callbackRes);
      }
    };

    const { indentUnit, indentWithTabs } = getIndentationFromIteration(iterFn);

    if (!this.config.cm6) {
      cm.setOption("tabSize", indentUnit);
      cm.setOption("indentUnit", indentUnit);
      cm.setOption("indentWithTabs", indentWithTabs);
    } else {
      const {
        codemirrorState: { EditorState },
        codemirrorLanguage,
      } = this.#CodeMirror6;

      cm.dispatch({
        effects: this.#compartments.tabSizeCompartment.reconfigure(
          EditorState.tabSize.of(indentUnit)
        ),
      });
      cm.dispatch({
        effects: this.#compartments.indentCompartment.reconfigure(
          codemirrorLanguage.indentUnit.of(
            (indentWithTabs ? "\t" : " ").repeat(indentUnit)
          )
        ),
      });
    }
  }

  /**
   * Replaces contents of a text area within the from/to {line, ch}
   * range. If neither `from` nor `to` arguments are provided works
   * exactly like setText. If only `from` object is provided, inserts
   * text at that point, *overwriting* as many characters as needed.
   */
  replaceText(value, from, to) {
    const cm = editors.get(this);

    if (!from) {
      this.setText(value);
      return;
    }

    if (!to) {
      const text = cm.getRange({ line: 0, ch: 0 }, from);
      this.setText(text + value);
      return;
    }

    cm.replaceRange(value, from, to);
  }

  /**
   * Inserts text at the specified {line, ch} position, shifting existing
   * contents as necessary.
   */
  insertText(value, at) {
    const cm = editors.get(this);
    cm.replaceRange(value, at, at);
  }

  /**
   * Deselects contents of the text area.
   */
  dropSelection() {
    if (!this.somethingSelected()) {
      return;
    }

    this.setCursor(this.getCursor());
  }

  /**
   * Returns true if there is more than one selection in the editor.
   */
  hasMultipleSelections() {
    const cm = editors.get(this);
    return cm.listSelections().length > 1;
  }

  /**
   * Gets the first visible line number in the editor.
   */
  getFirstVisibleLine() {
    const cm = editors.get(this);
    return cm.lineAtHeight(0, "local");
  }

  /**
   * Scrolls the view such that the given line number is the first visible line.
   */
  setFirstVisibleLine(line) {
    const cm = editors.get(this);
    const { top } = cm.charCoords({ line, ch: 0 }, "local");
    cm.scrollTo(0, top);
  }

  /**
   * Sets the cursor to the specified {line, ch} position with an additional
   * option to align the line at the "top", "center" or "bottom" of the editor
   * with "top" being default value.
   */
  setCursor({ line, ch }, align) {
    const cm = editors.get(this);
    this.alignLine(line, align);
    cm.setCursor({ line, ch });
    this.emit("cursorActivity");
  }

  /**
   * Aligns the provided line to either "top", "center" or "bottom" of the
   * editor view with a maximum margin of MAX_VERTICAL_OFFSET lines from top or
   * bottom.
   */
  alignLine(line, align) {
    const cm = editors.get(this);
    const from = cm.lineAtHeight(0, "page");
    const to = cm.lineAtHeight(cm.getWrapperElement().clientHeight, "page");
    const linesVisible = to - from;
    const halfVisible = Math.round(linesVisible / 2);

    // If the target line is in view, skip the vertical alignment part.
    if (line <= to && line >= from) {
      return;
    }

    // Setting the offset so that the line always falls in the upper half
    // of visible lines (lower half for bottom aligned).
    // MAX_VERTICAL_OFFSET is the maximum allowed value.
    const offset = Math.min(halfVisible, MAX_VERTICAL_OFFSET);

    let topLine =
      {
        center: Math.max(line - halfVisible, 0),
        bottom: Math.max(line - linesVisible + offset, 0),
        top: Math.max(line - offset, 0),
      }[align || "top"] || offset;

    // Bringing down the topLine to total lines in the editor if exceeding.
    topLine = Math.min(topLine, this.lineCount());
    this.setFirstVisibleLine(topLine);
  }

  /**
   * Returns whether a marker of a specified class exists in a line's gutter.
   */
  hasMarker(line, gutterName, markerClass) {
    const marker = this.getMarker(line, gutterName);
    if (!marker) {
      return false;
    }

    return marker.classList.contains(markerClass);
  }

  /**
   * Adds a marker with a specified class to a line's gutter. If another marker
   * exists on that line, the new marker class is added to its class list.
   */
  addMarker(line, gutterName, markerClass) {
    const cm = editors.get(this);
    const info = this.lineInfo(line);
    if (!info) {
      return;
    }

    const gutterMarkers = info.gutterMarkers;
    let marker;
    if (gutterMarkers) {
      marker = gutterMarkers[gutterName];
      if (marker) {
        marker.classList.add(markerClass);
        return;
      }
    }

    marker = cm.getWrapperElement().ownerDocument.createElement("div");
    marker.className = markerClass;
    cm.setGutterMarker(info.line, gutterName, marker);
  }

  /**
   * The reverse of addMarker. Removes a marker of a specified class from a
   * line's gutter.
   */
  removeMarker(line, gutterName, markerClass) {
    if (!this.hasMarker(line, gutterName, markerClass)) {
      return;
    }

    this.lineInfo(line).gutterMarkers[gutterName].classList.remove(markerClass);
  }

  /**
   * Adds a marker with a specified class and an HTML content to a line's
   * gutter. If another marker exists on that line, it is overwritten by a new
   * marker.
   */
  addContentMarker(line, gutterName, markerClass, content) {
    const cm = editors.get(this);
    const info = this.lineInfo(line);
    if (!info) {
      return;
    }

    const marker = cm.getWrapperElement().ownerDocument.createElement("div");
    marker.className = markerClass;
    // eslint-disable-next-line no-unsanitized/property
    marker.innerHTML = content;
    cm.setGutterMarker(info.line, gutterName, marker);
  }

  /**
   * The reverse of addContentMarker. Removes any line's markers in the
   * specified gutter.
   */
  removeContentMarker(line, gutterName) {
    const cm = editors.get(this);
    const info = this.lineInfo(line);
    if (!info) {
      return;
    }

    cm.setGutterMarker(info.line, gutterName, null);
  }

  getMarker(line, gutterName) {
    const info = this.lineInfo(line);
    if (!info) {
      return null;
    }

    const gutterMarkers = info.gutterMarkers;
    if (!gutterMarkers) {
      return null;
    }

    return gutterMarkers[gutterName];
  }

  /**
   * Removes all gutter markers in the gutter with the given name.
   */
  removeAllMarkers(gutterName) {
    const cm = editors.get(this);
    cm.clearGutter(gutterName);
  }

  /**
   * Handles attaching a set of events listeners on a marker. They should
   * be passed as an object literal with keys as event names and values as
   * function listeners. The line number, marker node and optional data
   * will be passed as arguments to the function listener.
   *
   * You don't need to worry about removing these event listeners.
   * They're automatically orphaned when clearing markers.
   */
  setMarkerListeners(line, gutterName, markerClass, eventsArg, data) {
    if (!this.hasMarker(line, gutterName, markerClass)) {
      return;
    }

    const cm = editors.get(this);
    const marker = cm.lineInfo(line).gutterMarkers[gutterName];

    for (const name in eventsArg) {
      const listener = eventsArg[name].bind(this, line, marker, data);
      marker.addEventListener(name, listener, {
        signal: this.#abortController?.signal,
      });
    }
  }

  /**
   * Returns whether a line is decorated using the specified class name.
   */
  hasLineClass(line, className) {
    const info = this.lineInfo(line);

    if (!info || !info.wrapClass) {
      return false;
    }

    return info.wrapClass.split(" ").includes(className);
  }

  /**
   * Sets a CSS class name for the given line, including the text and gutter.
   */
  addLineClass(lineOrOffset, className) {
    const cm = editors.get(this);
    const line = this.toLineIfWasmOffset(lineOrOffset);
    cm.addLineClass(line, "wrap", className);
  }

  /**
   * The reverse of addLineClass.
   */
  removeLineClass(lineOrOffset, className) {
    const cm = editors.get(this);
    const line = this.toLineIfWasmOffset(lineOrOffset);
    cm.removeLineClass(line, "wrap", className);
  }

  /**
   * Mark a range of text inside the two {line, ch} bounds. Since the range may
   * be modified, for example, when typing text, this method returns a function
   * that can be used to remove the mark.
   */
  markText(from, to, className = "marked-text") {
    const cm = editors.get(this);
    const text = cm.getRange(from, to);
    const span = cm.getWrapperElement().ownerDocument.createElement("span");
    span.className = className;
    span.textContent = text;

    const mark = cm.markText(from, to, { replacedWith: span });
    return {
      anchor: span,
      clear: () => mark.clear(),
    };
  }

  /**
   * Calculates and returns one or more {line, ch} objects for
   * a zero-based index who's value is relative to the start of
   * the editor's text.
   *
   * If only one argument is given, this method returns a single
   * {line,ch} object. Otherwise it returns an array.
   */
  getPosition(...args) {
    const cm = editors.get(this);
    const res = args.map(ind => cm.posFromIndex(ind));
    return args.length === 1 ? res[0] : res;
  }

  /**
   * The reverse of getPosition. Similarly to getPosition this
   * method returns a single value if only one argument was given
   * and an array otherwise.
   */
  getOffset(...args) {
    const cm = editors.get(this);
    const res = args.map(pos => cm.indexFromPos(pos));
    return args.length > 1 ? res : res[0];
  }

  /**
   * Returns a {line, ch} object that corresponds to the
   * left, top coordinates.
   */
  getPositionFromCoords({ left, top }) {
    const cm = editors.get(this);
    return cm.coordsChar({ left, top });
  }

  /**
   * Returns true if there's something to undo and false otherwise.
   */
  canUndo() {
    const cm = editors.get(this);
    return cm.historySize().undo > 0;
  }

  /**
   * Returns true if there's something to redo and false otherwise.
   */
  canRedo() {
    const cm = editors.get(this);
    return cm.historySize().redo > 0;
  }

  /**
   * Marks the contents as clean and returns the current
   * version number.
   */
  setClean() {
    const cm = editors.get(this);
    this.version = cm.changeGeneration();
    this.#lastDirty = false;
    this.emit("dirty-change");
    return this.version;
  }

  /**
   * Returns true if contents of the text area are
   * clean i.e. no changes were made since the last version.
   */
  isClean() {
    const cm = editors.get(this);
    return cm.isClean(this.version);
  }

  /**
   * This method opens an in-editor dialog asking for a line to
   * jump to. Once given, it changes cursor to that line.
   */
  jumpToLine() {
    const doc = editors.get(this).getWrapperElement().ownerDocument;
    const div = doc.createElement("div");
    const inp = doc.createElement("input");
    const txt = doc.createTextNode(L10N.getStr("gotoLineCmd.promptTitle"));

    inp.type = "text";
    inp.style.width = "10em";
    inp.style.marginInlineStart = "1em";

    div.appendChild(txt);
    div.appendChild(inp);

    this.openDialog(div, line => {
      // Handle LINE:COLUMN as well as LINE
      const match = line.toString().match(RE_JUMP_TO_LINE);
      if (match) {
        const [, matchLine, column] = match;
        this.setCursor({ line: matchLine - 1, ch: column ? column - 1 : 0 });
      }
    });
  }

  /**
   * Moves the content of the current line or the lines selected up a line.
   */
  moveLineUp() {
    const cm = editors.get(this);
    const start = cm.getCursor("start");
    const end = cm.getCursor("end");

    if (start.line === 0) {
      return;
    }

    // Get the text in the lines selected or the current line of the cursor
    // and append the text of the previous line.
    let value;
    if (start.line !== end.line) {
      value =
        cm.getRange(
          { line: start.line, ch: 0 },
          { line: end.line, ch: cm.getLine(end.line).length }
        ) + "\n";
    } else {
      value = cm.getLine(start.line) + "\n";
    }
    value += cm.getLine(start.line - 1);

    // Replace the previous line and the currently selected lines with the new
    // value and maintain the selection of the text.
    cm.replaceRange(
      value,
      { line: start.line - 1, ch: 0 },
      { line: end.line, ch: cm.getLine(end.line).length }
    );
    cm.setSelection(
      { line: start.line - 1, ch: start.ch },
      { line: end.line - 1, ch: end.ch }
    );
  }

  /**
   * Moves the content of the current line or the lines selected down a line.
   */
  moveLineDown() {
    const cm = editors.get(this);
    const start = cm.getCursor("start");
    const end = cm.getCursor("end");

    if (end.line + 1 === cm.lineCount()) {
      return;
    }

    // Get the text of next line and append the text in the lines selected
    // or the current line of the cursor.
    let value = cm.getLine(end.line + 1) + "\n";
    if (start.line !== end.line) {
      value += cm.getRange(
        { line: start.line, ch: 0 },
        { line: end.line, ch: cm.getLine(end.line).length }
      );
    } else {
      value += cm.getLine(start.line);
    }

    // Replace the currently selected lines and the next line with the new
    // value and maintain the selection of the text.
    cm.replaceRange(
      value,
      { line: start.line, ch: 0 },
      { line: end.line + 1, ch: cm.getLine(end.line + 1).length }
    );
    cm.setSelection(
      { line: start.line + 1, ch: start.ch },
      { line: end.line + 1, ch: end.ch }
    );
  }

  /**
   * Intercept CodeMirror's Find and replace key shortcut to select the search input
   */
  findOrReplace(node, isReplaceAll) {
    const cm = editors.get(this);
    const isInput = node.tagName === "INPUT";
    const isSearchInput = isInput && node.type === "search";
    // replace box is a different input instance than search, and it is
    // located in a code mirror dialog
    const isDialogInput =
      isInput &&
      node.parentNode &&
      node.parentNode.classList.contains("CodeMirror-dialog");
    if (!(isSearchInput || isDialogInput)) {
      return;
    }

    if (isSearchInput || isReplaceAll) {
      // select the search input
      // it's the precise reason why we reimplement these key shortcuts
      node.select();
    }

    // need to call it since we prevent the propagation of the event and
    // cancel codemirror's key handling
    cm.execCommand("findPersistent");
  }

  /**
   * Intercept CodeMirror's findNext and findPrev key shortcut to allow
   * immediately search for next occurance after typing a word to search.
   */
  findNextOrPrev(node, isFindPrev) {
    const cm = editors.get(this);
    const isInput = node.tagName === "INPUT";
    const isSearchInput = isInput && node.type === "search";
    if (!isSearchInput) {
      return;
    }
    const query = node.value;
    // cm.state.search allows to automatically start searching for the next occurance
    // it's the precise reason why we reimplement these key shortcuts
    if (!cm.state.search || cm.state.search.query !== query) {
      cm.state.search = {
        posFrom: null,
        posTo: null,
        overlay: null,
        query,
      };
    }

    // need to call it since we prevent the propagation of the event and
    // cancel codemirror's key handling
    if (isFindPrev) {
      cm.execCommand("findPrev");
    } else {
      cm.execCommand("findNext");
    }
  }

  /**
   * Returns current font size for the editor area, in pixels.
   */
  getFontSize() {
    const cm = editors.get(this);
    const el = cm.getWrapperElement();
    const win = el.ownerDocument.defaultView;

    return parseInt(win.getComputedStyle(el).getPropertyValue("font-size"), 10);
  }

  /**
   * Sets font size for the editor area.
   */
  setFontSize(size) {
    const cm = editors.get(this);
    cm.getWrapperElement().style.fontSize = parseInt(size, 10) + "px";
    cm.refresh();
  }

  setLineWrapping(value) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const {
        codemirrorView: { EditorView },
      } = this.#CodeMirror6;
      cm.dispatch({
        effects: this.#compartments.lineWrapCompartment.reconfigure(
          value ? EditorView.lineWrapping : []
        ),
      });
    } else {
      cm.setOption("lineWrapping", value);
    }
    this.config.lineWrapping = value;
  }

  /**
   * Sets an option for the editor.  For most options it just defers to
   * CodeMirror.setOption, but certain ones are maintained within the editor
   * instance.
   */
  setOption(o, v) {
    const cm = editors.get(this);

    // Save the state of a valid autoCloseBrackets string, so we can reset
    // it if it gets preffed off and back on.
    if (o === "autoCloseBrackets" && v) {
      this.config.autoCloseBracketsSaved = v;
    }

    if (o === "autocomplete") {
      this.config.autocomplete = v;
      this.setupAutoCompletion();
    } else {
      cm.setOption(o, v);
      this.config[o] = v;
    }

    if (o === "enableCodeFolding") {
      // The new value maybe explicitly force foldGUtter on or off, ignoring
      // the prefs service.
      this.updateCodeFoldingGutter();
    }
  }

  /**
   * Gets an option for the editor.  For most options it just defers to
   * CodeMirror.getOption, but certain ones are maintained within the editor
   * instance.
   */
  getOption(o) {
    const cm = editors.get(this);
    if (o === "autocomplete") {
      return this.config.autocomplete;
    }

    return cm.getOption(o);
  }

  /**
   * Sets up autocompletion for the editor. Lazily imports the required
   * dependencies because they vary by editor mode.
   *
   * Autocompletion is special, because we don't want to automatically use
   * it just because it is preffed on (it still needs to be requested by the
   * editor), but we do want to always disable it if it is preffed off.
   */
  setupAutoCompletion() {
    if (!this.config.autocomplete && !this.initializeAutoCompletion) {
      // Do nothing since there is no autocomplete config and no autocompletion have
      // been initialized.
      return;
    }
    // The autocomplete module will overwrite this.initializeAutoCompletion
    // with a mode specific autocompletion handler.
    if (!this.initializeAutoCompletion) {
      this.extend(
        require("resource://devtools/client/shared/sourceeditor/autocomplete.js")
      );
    }

    if (this.config.autocomplete && Services.prefs.getBoolPref(AUTOCOMPLETE)) {
      this.initializeAutoCompletion(this.config.autocompleteOpts);
    } else {
      this.destroyAutoCompletion();
    }
  }

  getAutoCompletionText() {
    const cm = editors.get(this);
    const mark = cm
      .getAllMarks()
      .find(m => m.className === AUTOCOMPLETE_MARK_CLASSNAME);
    if (!mark) {
      return "";
    }

    return mark.attributes["data-completion"] || "";
  }

  setAutoCompletionText(text) {
    const cursor = this.getCursor();
    const cm = editors.get(this);
    const className = AUTOCOMPLETE_MARK_CLASSNAME;

    cm.operation(() => {
      cm.getAllMarks().forEach(mark => {
        if (mark.className === className) {
          mark.clear();
        }
      });

      if (text) {
        cm.markText({ ...cursor, ch: cursor.ch - 1 }, cursor, {
          className,
          attributes: {
            "data-completion": text,
          },
        });
      }
    });
  }

  /**
   * Gets the element at the specified codemirror offset
   * @param {Number} offset
   * @return {Element|null}
   */
  #getElementAtOffset(offset) {
    const cm = editors.get(this);
    const el = cm.domAtPos(offset).node;
    if (!el) {
      return null;
    }
    // Text nodes do not have offset* properties, so lets use its
    // parent element;
    if (el.nodeType == nodeConstants.TEXT_NODE) {
      return el.parentElement;
    }
    return el;
  }

  /**
   * This checks if the specified position (line/column) is within the current viewport
   * bounds. it helps determine if scrolling should happen.
   * @param {Number} line - The line in the source
   * @param {Number} column - The column in the source
   * @returns {Boolean}
   */
  isPositionVisible(line, column) {
    const cm = editors.get(this);
    let inXView, inYView;

    function withinBounds(x, min, max) {
      return x >= min && x <= max;
    }

    if (this.config.cm6) {
      const pos = this.#positionToOffset(line, column);
      if (pos == null) {
        return false;
      }
      // `coordsAtPos` returns the absolute position of the line/column location
      // so that we have to ensure comparing with same absolute position for
      // CodeMirror DOM Element.
      //
      // Note that it may return the coordinates for a column breakpoint marker
      // so it may still report as visible, if the marker is on the edge of the viewport
      // and the displayed character at line/column is actually hidden after the scrollable area.
      const coords = cm.coordsAtPos(pos);
      if (!coords) {
        return false;
      }
      const { x, y, width, height } = cm.dom.getBoundingClientRect();
      const gutterWidth = cm.dom.querySelector(".cm-gutters").clientWidth;

      inXView = coords.left > x + gutterWidth && coords.right < x + width;
      inYView = coords.top > y && coords.bottom < y + height;
    } else {
      const { top, left } = cm.charCoords({ line, ch: column }, "local");
      const scrollArea = cm.getScrollInfo();
      const charWidth = cm.defaultCharWidth();
      const fontHeight = cm.defaultTextHeight();
      const { scrollTop, scrollLeft } = cm.doc;

      inXView = withinBounds(
        left,
        scrollLeft,
        // Note: 30 might relate to the margin on one of the scroll bar elements.
        // See comment https://github.com/firefox-devtools/debugger/pull/5182#discussion_r163439209
        scrollLeft + (scrollArea.clientWidth - 30) - charWidth
      );
      inYView = withinBounds(
        top,
        scrollTop,
        scrollTop + scrollArea.clientHeight - fontHeight
      );
    }
    return inXView && inYView;
  }

  /**
   * Converts  line/col to CM6 offset position
   * @param {Number} line - The line in the source
   * @param {Number} col - The column in the source
   * @returns {Number}
   */
  #positionToOffset(line, col = 0) {
    const cm = editors.get(this);
    try {
      const offset = cm.state.doc.line(line);
      return offset.from + col;
    } catch (e) {
      // Line likey does not exist in viewport yet
      console.warn(e.message);
    }
    return null;
  }

  /**
   * This returns the line and column for the specified search cursor's position
   *
   * @param {RegExpSearchCursor} searchCursor
   * @returns {Object}
   */
  getPositionFromSearchCursor(searchCursor) {
    const cm = editors.get(this);
    const lineFrom = cm.state.doc.lineAt(searchCursor.from);
    return {
      line: lineFrom.number - 1,
      ch: searchCursor.to - searchCursor.match[0].length - lineFrom.from,
    };
  }

  /**
   * Scrolls the editor to the specified codemirror position
   *
   * @param {Number} position
   */
  scrollToPosition(position) {
    const cm = editors.get(this);
    if (!this.config.cm6) {
      throw new Error("This function is only compatible with CM6");
    }
    const {
      codemirrorView: { EditorView },
    } = this.#CodeMirror6;
    return cm.dispatch({
      effects: EditorView.scrollIntoView(position, {
        x: "nearest",
        y: "center",
      }),
    });
  }

  /**
   * Scrolls the editor to the specified line and column
   * @param {Number} line - The line in the source
   * @param {Number} column - The column in the source
   * @param {String|null} yAlign - Optional value for position of the line after the line is scrolled.
   *                               (Used by `scrollEditorIntoView` test helper)
   */
  async scrollTo(line, column, yAlign) {
    if (this.isDestroyed()) {
      return null;
    }
    const cm = editors.get(this);
    if (this.config.cm6) {
      const {
        codemirrorView: { EditorView },
      } = this.#CodeMirror6;

      if (!this.isPositionVisible(line, column)) {
        const offset = this.#positionToOffset(line, column);
        if (offset == null) {
          return null;
        }
        return cm.dispatch({
          effects: EditorView.scrollIntoView(offset, {
            x: "center",
            y: yAlign || "center",
          }),
        });
      }
    } else {
      // For all cases where these are on the first line and column,
      // avoid the possibly slow computation of cursor location on large bundles.
      if (!line && !column) {
        cm.scrollTo(0, 0);
        return null;
      }

      const { top, left } = cm.charCoords({ line, ch: column }, "local");

      if (!this.isPositionVisible(line, column)) {
        const scroller = cm.getScrollerElement();
        const centeredX = Math.max(left - scroller.offsetWidth / 2, 0);
        const centeredY = Math.max(top - scroller.offsetHeight / 2, 0);

        return cm.scrollTo(centeredX, centeredY);
      }
    }
    return null;
  }

  // Used only in tests
  setSelectionAt(start, end) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const from = this.#positionToOffset(start.line, start.column);
      const to = this.#positionToOffset(end.line, end.column);
      if (from == null || to == null) {
        return;
      }
      cm.dispatch({ selection: { anchor: from, head: to } });
    } else {
      cm.setSelection(
        { line: start.line - 1, ch: start.column },
        { line: end.line - 1, ch: end.column }
      );
    }
  }

  // Used only in tests
  setCursorAt(line, column) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const position = cm.state.doc.line(line + 1).from + column;
      return cm.dispatch({ selection: { anchor: position, head: position } });
    }
    return cm.setCursor({ line, ch: column });
  }

  // Used only in tests
  getEditorFileMode() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      return cm.contentDOM.dataset.language;
    }
    return cm.getOption("mode").name;
  }

  // Used only in tests
  getEditorContent() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      return cm.state.doc.toString();
    }
    return cm.getValue();
  }

  isSearchStateReady() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      return !!this.searchState.cursors;
    }
    return !!cm.state.search;
  }

  // Used only in tests
  getCoords(line, column = 0) {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const offset = this.#positionToOffset(line, column);
      if (offset == null) {
        return null;
      }
      return cm.coordsAtPos(offset);
    }
    // CodeMirror is 0-based while line and column arguments are 1-based.
    // Pass "column=-1" when there is no column argument passed.
    return cm.charCoords({ line: ~~line, ch: ~~column });
  }

  // Used only in tests
  // Only used for CM6
  getElementAtLine(line) {
    const offset = this.#positionToOffset(line);
    const el = this.#getElementAtOffset(offset);
    return el.closest(".cm-line");
  }

  // Used only in tests
  getSearchQuery() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      return this.searchState.query.toString();
    }
    return cm.state.search.query;
  }

  // Used only in tests
  // Gets currently selected search term
  getSearchSelection() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      const cursor =
        this.searchState.cursors[this.searchState.currentCursorIndex];
      if (!cursor) {
        return { text: "", line: -1, column: -1 };
      }

      const cursorPosition = lezerUtils.positionToLocation(
        cm.state.doc,
        cursor.to
      );
      // The lines in CM6 are 1 based while CM5 is  0 based
      return {
        text: cursor.match[0],
        line: cursorPosition.line - 1,
        column: cursorPosition.column,
      };
    }
    const cursor = cm.getCursor();
    return {
      text: cm.getSelection(),
      line: cursor.line,
      column: cursor.ch,
    };
  }

  // Only used for CM6
  getElementAtPos(line, column) {
    const offset = this.#positionToOffset(line, column);
    const el = this.#getElementAtOffset(offset);
    return el;
  }

  // Used only in tests
  getLineCount() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      return cm.state.doc.lines;
    }
    return cm.lineCount();
  }

  /**
   * Extends an instance of the Editor object with additional
   * functions. Each function will be called with context as
   * the first argument. Context is a {ed, cm} object where
   * 'ed' is an instance of the Editor object and 'cm' is an
   * instance of the CodeMirror object. Example:
   *
   * function hello(ctx, name) {
   *   let { cm, ed } = ctx;
   *   cm;   // CodeMirror instance
   *   ed;   // Editor instance
   *   name; // 'Mozilla'
   * }
   *
   * editor.extend({ hello: hello });
   * editor.hello('Mozilla');
   */
  extend(funcs) {
    Object.keys(funcs).forEach(name => {
      const cm = editors.get(this);
      const ctx = { ed: this, cm, Editor };

      if (name === "initialize") {
        funcs[name](ctx);
        return;
      }

      this[name] = funcs[name].bind(null, ctx);
    });
  }

  isDestroyed() {
    return !this.config || !editors.get(this);
  }

  destroy() {
    if (this.config.cm6 && this.#CodeMirror6) {
      this.#clearEditorDOMEventListeners();
    }
    if (this.#abortController) {
      this.#abortController.abort();
      this.#abortController = null;
    }
    this.container = null;
    this.config = null;
    this.version = null;
    this.#ownerDoc = null;
    this.#updateListener = null;
    this.#lineGutterMarkers.clear();
    this.#lineContentMarkers.clear();
    this.#scrollSnapshots.clear();
    this.clearSources();

    if (this.#prefObserver) {
      this.#prefObserver.off(KEYMAP_PREF, this.setKeyMap);
      this.#prefObserver.off(TAB_SIZE, this.reloadPreferences);
      this.#prefObserver.off(EXPAND_TAB, this.reloadPreferences);
      this.#prefObserver.off(AUTO_CLOSE, this.reloadPreferences);
      this.#prefObserver.off(AUTOCOMPLETE, this.reloadPreferences);
      this.#prefObserver.off(DETECT_INDENT, this.reloadPreferences);
      this.#prefObserver.off(ENABLE_CODE_FOLDING, this.reloadPreferences);
      this.#prefObserver.destroy();
    }

    // Remove the link between the document and code-mirror.
    const cm = editors.get(this);
    if (cm?.doc) {
      cm.doc.cm = null;
    }

    // Destroy the CM6 view
    if (cm?.destroy) {
      cm.destroy();
    }
    this.emit("destroy");
  }

  updateCodeFoldingGutter() {
    let shouldFoldGutter = this.config.enableCodeFolding;
    const foldGutterIndex = this.config.gutters.indexOf(
      "CodeMirror-foldgutter"
    );
    const cm = editors.get(this);

    if (shouldFoldGutter === undefined) {
      shouldFoldGutter = Services.prefs.getBoolPref(ENABLE_CODE_FOLDING);
    }

    if (shouldFoldGutter) {
      // Add the gutter before enabling foldGutter
      if (foldGutterIndex === -1) {
        const gutters = this.config.gutters.slice();
        gutters.push("CodeMirror-foldgutter");
        this.setOption("gutters", gutters);
      }

      this.setOption("foldGutter", true);
    } else {
      // No code should remain folded when folding is off.
      if (cm) {
        cm.execCommand("unfoldAll");
      }

      // Remove the gutter so it doesn't take up space
      if (foldGutterIndex !== -1) {
        const gutters = this.config.gutters.slice();
        gutters.splice(foldGutterIndex, 1);
        this.setOption("gutters", gutters);
      }

      this.setOption("foldGutter", false);
    }
  }

  /**
   * Register all key shortcuts.
   */
  #initSearchShortcuts(win) {
    const shortcuts = new KeyShortcuts({
      window: win,
    });
    const keys = ["find.key", "findNext.key", "findPrev.key"];

    if (OS === "Darwin") {
      keys.push("replaceAllMac.key");
    } else {
      keys.push("replaceAll.key");
    }
    // Process generic keys:
    keys.forEach(name => {
      const key = L10N.getStr(name);
      shortcuts.on(key, event => this.#onSearchShortcut(name, event));
    });
  }
  /**
   * Key shortcut listener.
   */
  #onSearchShortcut = (name, event) => {
    if (!this.#isInputOrTextarea(event.target)) {
      return;
    }
    const node = event.originalTarget;

    switch (name) {
      // replaceAll.key is Alt + find.key
      case "replaceAllMac.key":
        this.findOrReplace(node, true);
        break;
      // replaceAll.key is Shift + find.key
      case "replaceAll.key":
        this.findOrReplace(node, true);
        break;
      case "find.key":
        this.findOrReplace(node, false);
        break;
      // findPrev.key is Shift + findNext.key
      case "findPrev.key":
        this.findNextOrPrev(node, true);
        break;
      case "findNext.key":
        this.findNextOrPrev(node, false);
        break;
      default:
        console.error("Unexpected editor key shortcut", name);
        return;
    }
    // Prevent default for this action
    event.stopPropagation();
    event.preventDefault();
  };

  /**
   * Check if a node is an input or textarea
   */
  #isInputOrTextarea(element) {
    const name = element.tagName.toLowerCase();
    return name === "input" || name === "textarea";
  }

  /**
   * Parse passed code string and returns an HTML string with the same classes CodeMirror
   * adds to handle syntax highlighting.
   *
   * @param {Document} doc: A document that will be used to create elements
   * @param {String} code: The code to highlight
   * @returns {String} The HTML string for the parsed code
   */
  highlightText(doc, code) {
    if (!doc) {
      return code;
    }

    const outputNode = doc.createElement("div");
    if (!this.config.cm6) {
      this.CodeMirror.runMode(code, "application/javascript", outputNode);
    } else {
      const { codemirrorLangJavascript, lezerHighlight } = this.#CodeMirror6;
      const { highlightCode, classHighlighter } = lezerHighlight;

      function emit(text, classes) {
        const textNode = doc.createTextNode(text);
        if (classes) {
          const span = doc.createElement("span");
          span.appendChild(textNode);
          span.className = classes;
          outputNode.appendChild(span);
        } else {
          outputNode.appendChild(textNode);
        }
      }
      function emitBreak() {
        outputNode.appendChild(doc.createTextNode("\n"));
      }

      highlightCode(
        code,
        codemirrorLangJavascript.javascriptLanguage.parser.parse(code),
        classHighlighter,
        emit,
        emitBreak
      );
    }
    return outputNode.innerHTML;
  }

  /**
   * Focus the CodeMirror editor
   */
  focus() {
    const cm = editors.get(this);
    cm.focus();
  }

  /**
   * Select the whole document
   */
  selectAll() {
    const cm = editors.get(this);
    if (this.config.cm6) {
      cm.dispatch({
        selection: { anchor: 0, head: cm.state.doc.length },
        userEvent: "select",
      });
    } else {
      cm.execCommand("selectAll");
    }
  }
}

// Since Editor is a thin layer over CodeMirror some methods
// are mapped directly—without any changes.

CM_MAPPING.forEach(name => {
  Editor.prototype[name] = function (...args) {
    const cm = editors.get(this);
    return cm[name].apply(cm, args);
  };
});

/**
 * We compute the CSS property names, values, and color names to be used with
 * CodeMirror to more closely reflect what is supported by the target platform.
 * The database is used to replace the values used in CodeMirror while initiating
 * an editor object. This is done here instead of the file codemirror/css.js so
 * as to leave that file untouched and easily upgradable.
 */
function getCSSKeywords(cssProperties) {
  function keySet(array) {
    const keys = {};
    for (let i = 0; i < array.length; ++i) {
      keys[array[i]] = true;
    }
    return keys;
  }

  const propertyKeywords = cssProperties.getNames();
  const colorKeywords = {};
  const valueKeywords = {};

  propertyKeywords.forEach(property => {
    if (property.includes("color")) {
      cssProperties.getValues(property).forEach(value => {
        colorKeywords[value] = true;
      });
    } else {
      cssProperties.getValues(property).forEach(value => {
        valueKeywords[value] = true;
      });
    }
  });

  return {
    propertyKeywords: keySet(propertyKeywords),
    colorKeywords,
    valueKeywords,
  };
}

module.exports = Editor;
