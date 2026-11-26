/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { debounce } = require("resource://devtools/shared/debounce.js");
const isMacOS = Services.appinfo.OS === "Darwin";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  getFocusableElements: "resource://devtools/client/shared/focus.mjs",
});

loader.lazyRequireGetter(this, "Debugger", "Debugger");
loader.lazyRequireGetter(
  this,
  "EventEmitter",
  "resource://devtools/shared/event-emitter.js"
);
loader.lazyRequireGetter(
  this,
  "AutocompletePopup",
  "resource://devtools/client/shared/autocomplete-popup.js"
);

loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);
loader.lazyRequireGetter(
  this,
  "KeyCodes",
  "resource://devtools/client/shared/keycodes.js",
  true
);
loader.lazyRequireGetter(
  this,
  "Editor",
  "resource://devtools/client/shared/sourceeditor/editor.js"
);
loader.lazyRequireGetter(
  this,
  "l10n",
  "resource://devtools/client/webconsole/utils/messages.js",
  true
);
loader.lazyRequireGetter(
  this,
  "saveAs",
  "resource://devtools/shared/DevToolsUtils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "beautify",
  "resource://devtools/shared/jsbeautify/beautify.js"
);

// React & Redux
const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

// History Modules
const {
  getHistory,
  getHistoryValue,
} = require("resource://devtools/client/webconsole/selectors/history.js");
const {
  getAutocompleteState,
} = require("resource://devtools/client/webconsole/selectors/autocomplete.js");
const actions = require("resource://devtools/client/webconsole/actions/index.js");

const EvaluationContextSelector = createFactory(
  require("resource://devtools/client/webconsole/components/Input/EvaluationContextSelector.js")
);

// Constants used for defining the direction of JSTerm input history navigation.
const {
  HISTORY_BACK,
  HISTORY_FORWARD,
} = require("resource://devtools/client/webconsole/constants.js");

const JSTERM_CODEMIRROR_ORIGIN = "jsterm";
const PREF_CMNEXT_ENABLED = "devtools.webconsole.codemirrorNext";

/**
 * Create a JSTerminal (a JavaScript command line). This is attached to an
 * existing HeadsUpDisplay (a Web Console instance). This code is responsible
 * with handling command line input and code evaluation.
 */
class JSTerm extends Component {
  static get propTypes() {
    return {
      // Returns previous or next value from the history
      // (depending on direction argument).
      getValueFromHistory: PropTypes.func.isRequired,
      // History of executed expression (state).
      history: PropTypes.object.isRequired,
      // Console object.
      webConsoleUI: PropTypes.object.isRequired,
      // Needed for opening context menu
      serviceContainer: PropTypes.object.isRequired,
      // Handler for clipboard 'paste' event (also used for 'drop' event, callback).
      onPaste: PropTypes.func,
      // Evaluate provided expression.
      evaluateExpression: PropTypes.func.isRequired,
      // Update position in the history after executing an expression (action).
      updateHistoryPosition: PropTypes.func.isRequired,
      // Update autocomplete popup state.
      autocompleteUpdate: PropTypes.func.isRequired,
      autocompleteClear: PropTypes.func.isRequired,
      // Data to be displayed in the autocomplete popup.
      autocompleteData: PropTypes.object.isRequired,
      // Toggle the editor mode.
      editorToggle: PropTypes.func.isRequired,
      // Dismiss the editor onboarding UI.
      editorOnboardingDismiss: PropTypes.func.isRequired,
      // Set the last JS input value.
      terminalInputChanged: PropTypes.func.isRequired,
      // Is the input in editor mode.
      editorMode: PropTypes.bool,
      editorWidth: PropTypes.number,
      editorPrettifiedAt: PropTypes.number,
      showEditorOnboarding: PropTypes.bool,
      autocomplete: PropTypes.bool,
      autocompletePopupPosition: PropTypes.string,
      inputEnabled: PropTypes.bool,
    };
  }

  // AbortController to cancel all event listener on destroy.
  #abortController = null;
  #destroyed = false;

  constructor(props) {
    super(props);

    const { webConsoleUI } = props;

    this.webConsoleUI = webConsoleUI;
    this.hudId = this.webConsoleUI.hudId;

    this._onEditorChanges = this._onEditorChanges.bind(this);
    this._onEditorBlur = this._onEditorBlur.bind(this);
    this._onEditorBeforeChange = this._onEditorBeforeChange.bind(this);
    this._onEditorKeyHandled = this._onEditorKeyHandled.bind(this);
    this.onContextMenu = this.onContextMenu.bind(this);
    this.imperativeUpdate = this.imperativeUpdate.bind(this);

    // We debounce the autocompleteUpdate so we don't send too many requests to the server
    // as the user is typing.
    // The delay should be small enough to be unnoticed by the user.
    this.autocompleteUpdate = debounce(this.props.autocompleteUpdate, 75, this);

    // Updates to the terminal input which can trigger eager evaluations are
    // similarly debounced.
    this.terminalInputChanged = debounce(
      this.props.terminalInputChanged,
      75,
      this
    );

    // Because the autocomplete has a slight delay (75ms), there can be time where the
    // codeMirror completion text is out-of-date, which might lead to issue when the user
    // accept the autocompletion while the update of the completion text is still pending.
    // In order to account for that, we put any future value of the completion text in
    // this property.
    this.pendingCompletionText = null;

    /**
     * Last input value.
     *
     * @type string
     */
    this.lastInputValue = "";

    this.autocompletePopup = null;

    EventEmitter.decorate(this);
    webConsoleUI.jsterm = this;
  }

  componentDidMount() {
    if (this.props.editorMode) {
      this.setEditorWidth(this.props.editorWidth);
    }

    const autocompleteOptions = {
      onSelect: this.onAutocompleteSelect.bind(this),
      onClick: this.acceptProposedCompletion.bind(this),
      listId: "webConsole_autocompletePopupListBox",
      position: this.props.autocompletePopupPosition,
      autoSelect: true,
      useXulWrapper: true,
    };

    const doc = this.webConsoleUI.document;
    const { toolbox } = this.webConsoleUI.wrapper;
    const tooltipDoc = toolbox ? toolbox.doc : doc;
    // The popup will be attached to the toolbox document or HUD document in the case
    // such as the browser console which doesn't have a toolbox.
    this.autocompletePopup = new AutocompletePopup(
      tooltipDoc,
      autocompleteOptions
    );

    if (this.node) {
      const onArrowUp = () => {
        let inputUpdated;
        if (this.autocompletePopup.isOpen) {
          this.autocompletePopup.selectPreviousItem();
          return true;
        }

        if (this.props.editorMode === false && this.canCaretGoPrevious()) {
          inputUpdated = this.historyPeruse(HISTORY_BACK);
        }

        return inputUpdated ? true : passToNextHandler();
      };

      const onArrowDown = () => {
        let inputUpdated;
        if (this.autocompletePopup.isOpen) {
          this.autocompletePopup.selectNextItem();
          return true;
        }

        if (this.props.editorMode === false && this.canCaretGoNext()) {
          inputUpdated = this.historyPeruse(HISTORY_FORWARD);
        }

        return inputUpdated ? true : passToNextHandler();
      };

      const onArrowLeft = () => {
        if (this.autocompletePopup.isOpen || this.getAutoCompletionText()) {
          this.clearCompletion();
        }
        return passToNextHandler();
      };

      const onArrowRight = () => {
        // We only want to complete on Right arrow if the completion text is
        // displayed.
        if (this.getAutoCompletionText()) {
          this.acceptProposedCompletion();
          return true;
        }

        this.clearCompletion();
        return passToNextHandler();
      };

      const onCtrlCmdEnter = () => {
        if (this.hasAutocompletionSuggestion()) {
          return this.acceptProposedCompletion();
        }

        this._execute();
        return true;
      };

      /**
       * Returns a value indicating that the current handler would not
       * handle the key and should be passed to other handlers (or the default behaviour)
       */
      function passToNextHandler() {
        return Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)
          ? false
          : "CodeMirror.Pass";
      }

      const KEY_BINDINGS = {
        Enter: () => {
          // No need to handle shift + Enter as it's natively handled by CodeMirror.

          const hasSuggestion = this.hasAutocompletionSuggestion();
          if (!hasSuggestion && !Debugger.isCompilableUnit(this._getValue())) {
            // incomplete statement
            return passToNextHandler();
          }

          if (hasSuggestion) {
            this.acceptProposedCompletion();
            return true;
          }

          if (!this.props.editorMode) {
            this._execute();
            return true;
          }
          return passToNextHandler();
        },

        "Cmd-Enter": onCtrlCmdEnter,
        "Ctrl-Enter": onCtrlCmdEnter,

        [Editor.accel("S")]: () => {
          const value = this._getValue();
          if (!value) {
            return true;
          }

          const date = new Date();
          const suggestedName =
            `console-input-${date.getFullYear()}-` +
            `${date.getMonth() + 1}-${date.getDate()}_${date.getHours()}-` +
            `${date.getMinutes()}-${date.getSeconds()}.js`;
          const data = new TextEncoder().encode(value);
          return saveAs(window, data, suggestedName, [
            {
              pattern: "*.js",
              label: l10n.getStr(
                "webconsole-input-open-javascript-file-filter"
              ),
            },
          ]);
        },

        [Editor.accel("O")]: async () => this._openFile(),

        Tab: () => {
          if (this.hasEmptyInput()) {
            this.editor.codeMirror.getInputField().blur();
            return false;
          }

          if (
            this.props.autocompleteData &&
            this.props.autocompleteData.getterPath
          ) {
            this.props.autocompleteUpdate(
              true,
              this.props.autocompleteData.getterPath
            );
            return false;
          }

          const isSomethingSelected = this.editor.isTextSelected();
          const hasSuggestion = this.hasAutocompletionSuggestion();

          if (hasSuggestion && !isSomethingSelected) {
            this.acceptProposedCompletion();
            return false;
          }

          if (!isSomethingSelected) {
            this.editor.insertStringAtCursor("\t", 0, JSTERM_CODEMIRROR_ORIGIN);
            return false;
          }

          // Something is selected, let the editor handle the indent.
          return true;
        },

        "Shift-Tab": () => {
          if (this.hasEmptyInput()) {
            this.focusPreviousElement();
            return false;
          }

          const hasSuggestion = this.hasAutocompletionSuggestion();
          if (hasSuggestion) {
            return false;
          }

          return passToNextHandler();
        },

        Up: onArrowUp,
        "Cmd-Up": onArrowUp,

        Down: onArrowDown,
        "Cmd-Down": onArrowDown,

        Left: onArrowLeft,
        "Ctrl-Left": onArrowLeft,
        "Cmd-Left": onArrowLeft,
        "Alt-Left": onArrowLeft,
        // On OSX, Ctrl-A navigates to the beginning of the line.
        "Ctrl-A": isMacOS ? onArrowLeft : undefined,

        Right: onArrowRight,
        "Ctrl-Right": onArrowRight,
        "Cmd-Right": onArrowRight,
        "Alt-Right": onArrowRight,

        "Ctrl-N": () => {
          // Control-N differs from down arrow: it ignores autocomplete state.
          // Note that we preserve the default 'down' navigation within
          // multiline text.
          if (
            Services.appinfo.OS === "Darwin" &&
            this.props.editorMode === false &&
            this.canCaretGoNext() &&
            this.historyPeruse(HISTORY_FORWARD)
          ) {
            return true;
          }
          this.clearCompletion();
          return passToNextHandler();
        },

        "Ctrl-P": () => {
          // Control-P differs from up arrow: it ignores autocomplete state.
          // Note that we preserve the default 'up' navigation within
          // multiline text.
          if (
            Services.appinfo.OS === "Darwin" &&
            this.props.editorMode === false &&
            this.canCaretGoPrevious() &&
            this.historyPeruse(HISTORY_BACK)
          ) {
            return true;
          }
          this.clearCompletion();
          return passToNextHandler();
        },

        PageUp: () => {
          if (this.autocompletePopup.isOpen) {
            this.autocompletePopup.selectPreviousPageItem();
          } else {
            const { outputScroller } = this.webConsoleUI;
            const { scrollTop, clientHeight } = outputScroller;
            outputScroller.scrollTop = Math.max(0, scrollTop - clientHeight);
          }
          return true;
        },

        PageDown: () => {
          if (this.autocompletePopup.isOpen) {
            this.autocompletePopup.selectNextPageItem();
          } else {
            const { outputScroller } = this.webConsoleUI;
            const { scrollTop, scrollHeight, clientHeight } = outputScroller;
            outputScroller.scrollTop = Math.min(
              scrollHeight,
              scrollTop + clientHeight
            );
          }
          return true;
        },

        Home: () => {
          if (this.autocompletePopup.isOpen) {
            this.autocompletePopup.selectItemAtIndex(0);
            return true;
          }

          if (!this._getValue()) {
            this.webConsoleUI.outputScroller.scrollTop = 0;
            return true;
          }

          if (this.getAutoCompletionText()) {
            this.clearCompletion();
          }
          return passToNextHandler();
        },

        End: () => {
          if (this.autocompletePopup.isOpen) {
            this.autocompletePopup.selectItemAtIndex(
              this.autocompletePopup.itemCount - 1
            );
            return true;
          }

          if (!this._getValue()) {
            const { outputScroller } = this.webConsoleUI;
            outputScroller.scrollTop = outputScroller.scrollHeight;
            return true;
          }

          if (this.getAutoCompletionText()) {
            this.clearCompletion();
          }

          return Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)
            ? true
            : passToNextHandler();
        },

        "Ctrl-Space": async () => {
          if (!this.autocompletePopup.isOpen) {
            const variables = await this.editor.getExpressionVariables();
            this.props.autocompleteUpdate(true, null, variables);
            return true;
          }
          return passToNextHandler();
        },

        Esc: false,
        // Don't handle Ctrl/Cmd + F so it can be listened by a parent node
        [Editor.accel("F")]: false,
      };

      this.editor = new Editor({
        cm6: Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED),
        autofocus: true,
        enableCodeFolding: this.props.editorMode,
        lineNumbers: this.props.editorMode,
        lineWrapping: true,
        mode: Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)
          ? Editor.modes.javascript
          : {
              name: "javascript",
              globalVars: true,
            },
        styleActiveLine: false,
        tabIndex: "0",
        viewportMargin: Infinity,
        disableSearchAddon: true,
        // CM5
        extraKeys: KEY_BINDINGS,
        // CM6
        keyMap: Editor.mapKeyBindings(KEY_BINDINGS),
      });

      if (!Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
        this.editor.on("changes", this._onEditorChanges);
        this.editor.on("beforeChange", this._onEditorBeforeChange);
        this.editor.on("blur", this._onEditorBlur);
        this.editor.on("keyHandled", this._onEditorKeyHandled);
      }

      this.editor.appendToLocalElement(this.node);
      if (Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
        this.editor.setUpdateListener(viewUpdate => {
          if (viewUpdate.docChanged) {
            this._onEditorChanges(viewUpdate, viewUpdate.changes);
          }
        });
        this.editor.setBeforeUpdateListener(changes => {
          this._onEditorBeforeChange(this.editor, changes[0]);
        });
        this.editor.addEditorDOMEventListeners({
          blur: (event, cm) => this._onEditorBlur(cm),
          keyDown: () => this._onEditorKeyHandled(),
          paste: event => this.props.onPaste(event),
          drop: event => this.props.onPaste(event),
        });
      } else {
        const cm = this.editor.codeMirror;
        cm.on("paste", (_, event) => this.props.onPaste(event));
        cm.on("drop", (_, event) => this.props.onPaste(event));
      }

      this.#abortController = new AbortController();
      const signal = this.#abortController.signal;
      doc.addEventListener(
        "visibilitychange",
        () => {
          if (
            doc.visibilityState == "hidden" &&
            this.autocompletePopup.isOpen
          ) {
            this.autocompletePopup.hidePopup();
          }
        },
        { signal }
      );
      this.node.addEventListener(
        "keydown",
        event => {
          if (event.keyCode === KeyCodes.DOM_VK_ESCAPE) {
            if (this.autocompletePopup.isOpen) {
              this.clearCompletion();
              event.preventDefault();
              event.stopPropagation();
            }

            if (
              this.props.autocompleteData &&
              this.props.autocompleteData.getterPath
            ) {
              this.props.autocompleteClear();
              event.preventDefault();
              event.stopPropagation();
            }
          }
        },
        { signal }
      );

      this.resizeObserver = new ResizeObserver(() => {
        // If we don't have the node reference, or if the node isn't connected
        // anymore, we disconnect the resize observer (componentWillUnmount is never
        // called on this component, so we have to do it here).
        if (!this.node || !this.node.isConnected) {
          this.resizeObserver.disconnect();
          return;
        }
        // CM5
        if (!Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
          // Calling `refresh` will update the cursor position, and all the selection blocks.
          this.editor.codeMirror.refresh();
        }
      });
      this.resizeObserver.observe(this.node);

      // Update the character width needed for the popup offset calculations.
      this._inputCharWidth = this.editor?.getInputCharWidth() || null;
      this.lastInputValue && this._setValue(this.lastInputValue);
    }
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    this.imperativeUpdate(nextProps);
  }

  shouldComponentUpdate(nextProps) {
    return (
      this.props.showEditorOnboarding !== nextProps.showEditorOnboarding ||
      this.props.editorMode !== nextProps.editorMode
    );
  }

  /**
   * Do all the imperative work needed after a Redux store update.
   *
   * @param {object} nextProps: props passed from shouldComponentUpdate.
   */
  imperativeUpdate(nextProps) {
    if (!nextProps) {
      return;
    }

    if (
      nextProps.autocompleteData !== this.props.autocompleteData &&
      nextProps.autocompleteData.pendingRequestId === null
    ) {
      this.updateAutocompletionPopup(nextProps.autocompleteData);
    }

    if (nextProps.editorMode !== this.props.editorMode) {
      if (this.editor) {
        if (Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
          nextProps.editorMode
            ? this.editor.enableGutter()
            : this.editor.disableGutter();
        } else {
          this.editor.setOption("lineNumbers", nextProps.editorMode);
          this.editor.setOption("enableCodeFolding", nextProps.editorMode);
        }
      }

      if (nextProps.editorMode && nextProps.editorWidth) {
        this.setEditorWidth(nextProps.editorWidth);
      } else {
        this.setEditorWidth(null);
      }

      if (this.autocompletePopup.isOpen) {
        this.autocompletePopup.hidePopup();
      }
    }

    if (
      nextProps.autocompletePopupPosition !==
        this.props.autocompletePopupPosition &&
      this.autocompletePopup
    ) {
      this.autocompletePopup.position = nextProps.autocompletePopupPosition;
    }

    if (
      nextProps.editorPrettifiedAt &&
      nextProps.editorPrettifiedAt !== this.props.editorPrettifiedAt
    ) {
      this._setValue(
        beautify.js(this._getValue(), {
          // Read directly from prefs because this.editor.config.indentUnit and
          // this.editor.getOption('indentUnit') are not really synced with
          // prefs.
          indent_size: Services.prefs.getIntPref("devtools.editor.tabsize"),
          indent_with_tabs: !Services.prefs.getBoolPref(
            "devtools.editor.expandtab"
          ),
        })
      );
    }
  }

  /**
   *
   * @param {number | null} editorWidth: The width to set the node to. If null, removes any
   *                                   `width` property on node style.
   */
  setEditorWidth(editorWidth) {
    if (!this.node) {
      return;
    }

    if (editorWidth) {
      this.node.style.width = `${editorWidth}px`;
    } else {
      this.node.style.removeProperty("width");
    }
  }

  focus() {
    if (this.editor) {
      this.editor.focus();
    }
  }

  focusPreviousElement() {
    const inputField = this.editor.codeMirror.getInputField();

    const findPreviousFocusableElement = el => {
      if (!el || !el.querySelectorAll) {
        return null;
      }

      // We only want to get visible focusable element, and for that we can assert that
      // the offsetParent isn't null. We can do that because we don't have fixed position
      // element in the console.
      const items = lazy
        .getFocusableElements(el)
        .filter(({ offsetParent }) => offsetParent !== null);
      const inputIndex = items.indexOf(inputField);

      if (items.length === 0 || (inputIndex > -1 && items.length === 1)) {
        return findPreviousFocusableElement(el.parentNode);
      }

      const index = inputIndex > 0 ? inputIndex - 1 : items.length - 1;
      return items[index];
    };

    const focusableEl = findPreviousFocusableElement(this.node.parentNode);
    if (focusableEl) {
      focusableEl.focus();
    }
  }

  /**
   * Execute a string. Execution happens asynchronously in the content process.
   */
  _execute() {
    const value = this._getValue();
    // In editor mode, we only evaluate the text selection if there's one. The feature isn't
    // enabled in inline mode as it can be confusing since input is cleared when evaluating.
    const executeString = this.props.editorMode
      ? this.editor.getSelectedText() || value
      : value;

    if (!executeString) {
      return;
    }

    if (!this.props.editorMode) {
      // Calling this.props.terminalInputChanged instead of this.terminalInputChanged
      // because we want to instantly hide the instant evaluation result, and don't want
      // the delay we have in this.terminalInputChanged.
      this.props.terminalInputChanged("");
      this._setValue("");
    }
    if (!Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
      this.clearCompletion();
    }
    this.props.evaluateExpression(executeString);
  }

  /**
   * Sets the value of the input field.
   *
   * @param string newValue
   *        The new value to set.
   * @returns void
   */
  _setValue(newValue = "") {
    this.lastInputValue = newValue;
    this.terminalInputChanged(newValue);

    if (this.editor) {
      const lines = newValue.split("\n");
      const ch = lines.at(-1).length;

      if (Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
        this.editor.setText(newValue);
        this.editor.setCursorAt(lines.length, ch);
        this.editor.setAutoCompletionText();
      } else {
        // In order to get the autocomplete popup to work properly, we need to set the
        // editor text and the cursor in the same operation. If we don't, the text change
        // is done before the cursor is moved, and the autocompletion call to the server
        // sends an erroneous query.
        this.editor.codeMirror.operation(() => {
          this.editor.setText(newValue);

          // Set the cursor at the end of the input.
          this.editor.setCursor({ line: lines.length - 1, ch });
          this.editor.setAutoCompletionText();
        });
      }
    }

    this.emitForTests("set-input-value");
  }

  /**
   * Gets the value from the input field
   *
   * @returns string
   */
  _getValue() {
    return this.editor ? this.editor.getText() : "";
  }

  /**
   * Open the file picker for the user to select a javascript file and open it.
   *
   */
  async _openFile() {
    const fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
    fp.init(
      this.webConsoleUI.document.defaultView.browsingContext,
      l10n.getStr("webconsole-input-open-javascript-file"),
      Ci.nsIFilePicker.modeOpen
    );

    // Append file filters
    fp.appendFilter(
      l10n.getStr("webconsole-input-open-javascript-file-filter"),
      "*.js"
    );

    function readFile(file) {
      return new Promise(resolve => {
        IOUtils.read(file.path).then(data => {
          const decoder = new TextDecoder();
          resolve(decoder.decode(data));
        });
      });
    }

    const content = await new Promise(resolve => {
      fp.open(rv => {
        if (rv == Ci.nsIFilePicker.returnOK) {
          const file = Cc["@mozilla.org/file/local;1"].createInstance(
            Ci.nsIFile
          );
          file.initWithPath(fp.file.path);
          readFile(file).then(resolve);
        }
      });
    });

    this._setValue(content);
  }

  getSelectionStart() {
    return this.editor.getTextBeforeCursor().length;
  }

  /**
   * Even handler for the "beforeChange" event fired by codeMirror. This event is fired
   * when codeMirror is about to make a change to its DOM representation.
   */
  _onEditorBeforeChange(cm, change) {
    // If the user did not type a character that matches the completion text, then we
    // clear it before the change is done to prevent a visual glitch.
    // See Bugs 1491776 & 1558248.
    const { from, to, origin, text } = change;
    const col = Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)
      ? "column"
      : "ch";
    const isAddedText =
      from.line === to.line && from[col] <= to[col] && origin === "+input";

    // if there was no changes (hitting delete on an empty input, or suppr when at the end
    // of the input), we bail out.
    if (
      !isAddedText &&
      origin === "+delete" &&
      from.line === to.line &&
      from[col] === to[col]
    ) {
      return;
    }

    // text is a string for CM6 and and array of characters for CM5
    const addedText = Array.isArray(text) ? text.join("") : text;

    const completionText = this.getAutoCompletionText();
    const addedCharacterMatchCompletion =
      isAddedText && completionText.startsWith(addedText);

    const addedCharacterMatchPopupItem =
      isAddedText &&
      this.autocompletePopup.items.some(({ preLabel, label }) =>
        label.startsWith(preLabel + addedText)
      );
    const nextSelectedAutocompleteItemIndex =
      addedCharacterMatchPopupItem &&
      this.autocompletePopup.items.findIndex(({ preLabel, label }) =>
        label.startsWith(preLabel + addedText)
      );

    if (addedCharacterMatchPopupItem) {
      this.autocompletePopup.selectItemAtIndex(
        nextSelectedAutocompleteItemIndex,
        { preventSelectCallback: true }
      );
    }

    if (!Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
      if (
        !completionText ||
        change.canceled ||
        !addedCharacterMatchCompletion
      ) {
        this.setAutoCompletionText("");
      }
    }

    if (!addedCharacterMatchCompletion && !addedCharacterMatchPopupItem) {
      this.autocompletePopup.hidePopup();
    } else if (
      !change.canceled &&
      (completionText ||
        addedCharacterMatchCompletion ||
        addedCharacterMatchPopupItem)
    ) {
      // The completion text will be updated when the debounced autocomplete update action
      // is done, so in the meantime we set the pending value to pendingCompletionText.
      // See Bug 1595068 for more information.
      this.pendingCompletionText = completionText.substring(text.length);
      // And we update the preLabel of the matching autocomplete items that may be used
      // in the acceptProposedAutocompletion function.
      this.autocompletePopup.items.forEach(item => {
        if (item.label.startsWith(item.preLabel + addedText)) {
          item.preLabel += addedText;
        }
      });
    }
  }

  /**
   * Even handler for the "blur" event fired by codeMirror.
   */
  _onEditorBlur(cm) {
    if (this.editor.isTextSelected()) {
      // If there's a selection when the input is blurred, then we remove it by setting
      // the cursor at the position that matches the start of the first selection.
      if (Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)) {
        const range = cm.state.selection.ranges[0];
        this.editor.setCursorAtPosition(range.head, false);
      } else {
        const [{ head }] = cm.listSelections();
        cm.setCursor(head, { scroll: false });
      }
    }
  }

  /**
   * Fired after a key is handled through a key map.
   *
   * @param {CodeMirror} cm: codeMirror instance
   * @param {string} key: The key that was handled
   */
  _onEditorKeyHandled(cm, key) {
    // The autocloseBracket addon handle closing brackets keys when they're typed, but
    // there's already an existing closing bracket.
    // ex:
    //  1. input is `foo(x|)` (where | represents the cursor)
    //  2. user types `)`
    //  3. input is now `foo(x)|` (i.e. the typed character wasn't inserted)
    // In such case, _onEditorBeforeChange isn't triggered, so we need to hide the popup
    // here. We can do that because this function won't be called when codeMirror _do_
    // insert the closing char.
    const closingKeys = [`']'`, `')'`, "'}'"];
    if (this.autocompletePopup.isOpen && closingKeys.includes(key)) {
      this.clearCompletion();
    }
  }

  /**
   * The editor "changes" event handler.
   */
  async _onEditorChanges(cm, changes) {
    const value = this._getValue();

    if (this.lastInputValue !== value) {
      // We don't autocomplete if the changes were made by JsTerm (e.g. autocomplete was
      // accepted). isJsTermChangeOnly is not useful for CM6 so should be false
      const isJsTermChangeOnly = Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED)
        ? false
        : changes.every(({ origin }) => origin === JSTERM_CODEMIRROR_ORIGIN);

      if (
        !isJsTermChangeOnly &&
        (this.props.autocomplete || this.hasAutocompletionSuggestion())
      ) {
        const variables = await this.editor.getExpressionVariables();

        // If JSTerm was destroyed during the async operation, bail out
        // immediately before triggering additional actions.
        if (this.#destroyed) {
          return;
        }

        this.autocompleteUpdate(false, null, variables);
      }

      this.lastInputValue = value;
      this.terminalInputChanged(value);
    }
  }

  /**
   * Go up/down the history stack of input values.
   *
   * @param number direction
   *        History navigation direction: HISTORY_BACK or HISTORY_FORWARD.
   *
   * @returns boolean
   *          True if the input value changed, false otherwise.
   */
  historyPeruse(direction) {
    const { history, updateHistoryPosition, getValueFromHistory } = this.props;

    if (!history.entries.length) {
      return false;
    }

    const newInputValue = getValueFromHistory(direction);
    const expression = this._getValue();
    updateHistoryPosition(direction, expression);

    if (newInputValue != null) {
      this._setValue(newInputValue);
      return true;
    }

    return false;
  }

  /**
   * Test for empty input.
   *
   * @return boolean
   */
  hasEmptyInput() {
    return this._getValue() === "";
  }

  /**
   * Check if the caret is at a location that allows selecting the previous item
   * in history when the user presses the Up arrow key.
   *
   * @return boolean
   *         True if the caret is at a location that allows selecting the
   *         previous item in history when the user presses the Up arrow key,
   *         otherwise false.
   */
  canCaretGoPrevious() {
    if (!this.editor) {
      return false;
    }

    const inputValue = this._getValue();
    const { line, ch } = this.editor.getCursorPos();
    const firstLine = Services.prefs.getBoolPref(PREF_CMNEXT_ENABLED) ? 1 : 0;
    return (
      (line === firstLine && ch === 0) ||
      (line === firstLine && ch === inputValue.length)
    );
  }

  /**
   * Check if the caret is at a location that allows selecting the next item in
   * history when the user presses the Down arrow key.
   *
   * @return boolean
   *         True if the caret is at a location that allows selecting the next
   *         item in history when the user presses the Down arrow key, otherwise
   *         false.
   */
  canCaretGoNext() {
    if (!this.editor) {
      return false;
    }

    const inputValue = this._getValue();
    const multiline = /[\r\n]/.test(inputValue);

    const { ch } = this.editor.getCursorPos();
    return (
      (!multiline && ch === 0) ||
      this.editor.getTextBeforeCursor().length === inputValue.length
    );
  }

  /**
   * Takes the data returned by the server and update the autocomplete popup state (i.e.
   * its visibility and items).
   *
   * @param {object} data
   *        The autocompletion data as returned by the webconsole actor's autocomplete
   *        service. Should be of the following shape:
   *        {
   *          matches: {Array} array of the properties matching the input,
   *          matchProp: {String} The string used to filter the properties,
   *          isElementAccess: {Boolean} True when the input is an element access,
   *                           i.e. `document["addEve`.
   *        }
   * @fires autocomplete-updated
   */
  async updateAutocompletionPopup(data) {
    if (!this.editor) {
      return;
    }

    const { matches, matchProp, isElementAccess } = data;
    if (!matches.length) {
      this.clearCompletion();
      return;
    }

    const inputUntilCursor = this.editor.getTextBeforeCursor();

    const items = matches.map(label => {
      let preLabel = label.substring(0, matchProp.length);
      // If the user is performing an element access, and if they did not typed a quote,
      // then we need to adjust the preLabel to match the quote from the label + what
      // the user entered.
      if (isElementAccess && /^['"`]/.test(matchProp) === false) {
        preLabel = label.substring(0, matchProp.length + 1);
      }
      return { preLabel, label, isElementAccess };
    });

    if (items.length) {
      const { preLabel, label } = items[0];
      let suffix = label.substring(preLabel.length);
      if (isElementAccess) {
        if (!matchProp) {
          suffix = label;
        }
        const inputAfterCursor = this._getValue().substring(
          inputUntilCursor.length
        );
        // If there's not a bracket after the cursor, add it to the completionText.
        if (!inputAfterCursor.trimLeft().startsWith("]")) {
          suffix = suffix + "]";
        }
      }
      this.setAutoCompletionText(suffix);
    }

    const popup = this.autocompletePopup;
    // We don't want to trigger the onSelect callback since we already set the completion
    // text a few lines above.
    popup.setItems(items, 0, {
      preventSelectCallback: true,
    });

    const minimumAutoCompleteLength = 2;

    // We want to show the autocomplete popup if:
    // - there are at least 2 matching results
    // - OR, if there's 1 result, but whose label does not start like the input (this can
    //   happen with insensitive search: `num` will match `Number`).
    // - OR, if there's 1 result, but we can't show the completionText (because there's
    // some text after the cursor), unless the text in the popup is the same as the input.
    if (
      items.length >= minimumAutoCompleteLength ||
      (items.length === 1 && items[0].preLabel !== matchProp) ||
      (items.length === 1 &&
        !this.canDisplayAutoCompletionText() &&
        items[0].label !== matchProp)
    ) {
      // We need to show the popup at the "." or "[".
      const xOffset = -1 * matchProp.length * this._inputCharWidth;
      const yOffset = 5;
      const popupAlignElement =
        this.props.serviceContainer.getJsTermTooltipAnchor();
      this._openPopupPendingPromise = popup.openPopup(
        popupAlignElement,
        xOffset,
        yOffset,
        0,
        {
          preventSelectCallback: true,
        }
      );
      await this._openPopupPendingPromise;
      this._openPopupPendingPromise = null;
    } else if (
      items.length < minimumAutoCompleteLength &&
      (popup.isOpen || this._openPopupPendingPromise)
    ) {
      if (this._openPopupPendingPromise) {
        await this._openPopupPendingPromise;
      }
      popup.hidePopup();
    }

    // Eager evaluation results incorporate the current autocomplete item. We need to
    // trigger it here as well as in onAutocompleteSelect as we set the items with
    // preventSelectCallback (which means we won't trigger onAutocompleteSelect when the
    // popup is open).
    this.terminalInputChanged(
      this.getInputValueWithCompletionText().expression
    );

    this.emit("autocomplete-updated");
  }

  onAutocompleteSelect() {
    const { selectedItem } = this.autocompletePopup;
    if (selectedItem) {
      const { preLabel, label, isElementAccess } = selectedItem;
      let suffix = label.substring(preLabel.length);

      // If the user is performing an element access, we need to check if we should add
      // starting and ending quotes, as well as a closing bracket.
      if (isElementAccess) {
        const inputBeforeCursor = this.editor.getTextBeforeCursor();
        if (inputBeforeCursor.trim().endsWith("[")) {
          suffix = label;
        }

        const inputAfterCursor = this._getValue().substring(
          inputBeforeCursor.length
        );
        // If there's no closing bracket after the cursor, add it to the completionText.
        if (!inputAfterCursor.trimLeft().startsWith("]")) {
          suffix = suffix + "]";
        }
      }
      this.setAutoCompletionText(suffix);
    } else {
      this.setAutoCompletionText("");
    }
    // Eager evaluation results incorporate the current autocomplete item.
    this.terminalInputChanged(
      this.getInputValueWithCompletionText().expression
    );
  }

  /**
   * Clear the current completion information, cancel any pending autocompletion update
   * and close the autocomplete popup, if needed.
   *
   * @fires autocomplete-updated
   */
  clearCompletion() {
    this.autocompleteUpdate.cancel();
    // Update Eager evaluation result as the completion text was removed.
    this.terminalInputChanged(this._getValue());

    this.setAutoCompletionText("");
    let onPopupClosed = Promise.resolve();
    if (this.autocompletePopup) {
      this.autocompletePopup.clearItems();

      if (this.autocompletePopup.isOpen || this._openPopupPendingPromise) {
        onPopupClosed = this.autocompletePopup.once("popup-closed");

        if (this._openPopupPendingPromise) {
          this._openPopupPendingPromise.then(() =>
            this.autocompletePopup.hidePopup()
          );
        } else {
          this.autocompletePopup.hidePopup();
        }
        onPopupClosed.then(() => this.focus());
      }
    }
    onPopupClosed.then(() => this.emit("autocomplete-updated"));
  }

  /**
   * Accept the proposed input completion.
   */
  acceptProposedCompletion() {
    const {
      completionText,
      numberOfCharsToMoveTheCursorForward,
      numberOfCharsToReplaceCharsBeforeCursor,
    } = this.getInputValueWithCompletionText();

    this.autocompleteUpdate.cancel();
    this.props.autocompleteClear();

    // If the code triggering the opening of the popup was already triggered but not yet
    // settled, then we need to wait until it's resolved in order to close the popup (See
    // Bug 1655406).
    if (this._openPopupPendingPromise) {
      this._openPopupPendingPromise.then(() =>
        this.autocompletePopup.hidePopup()
      );
    }

    if (completionText) {
      this.editor.insertStringAtCursor(
        completionText,
        numberOfCharsToReplaceCharsBeforeCursor,
        JSTERM_CODEMIRROR_ORIGIN
      );

      if (numberOfCharsToMoveTheCursorForward) {
        const { line, ch } = this.editor.getCursorPos();
        this.editor.setCursorAt(line, ch + numberOfCharsToMoveTheCursorForward);
      }
    }
  }

  /**
   * Returns an object containing the expression we would get if the user accepted the
   * current completion text. This is more than the current input + the completion text,
   * as there are special cases for element access and case-insensitive matches.
   *
   * @return {object}: An object of the following shape:
   *         - {String} expression: The complete expression
   *         - {String} completionText: the completion text only, which should be used
   *                    with the next property
   *         - {Integer} numberOfCharsToReplaceCharsBeforeCursor: The number of chars that
   *                     should be removed from the current input before the cursor to
   *                     cleanly apply the completionText. This is handy when we only want
   *                     to insert the completionText.
   *         - {Integer} numberOfCharsToMoveTheCursorForward: The number of chars that the
   *                     cursor should be moved after the completion is done. This can
   *                     be useful for element access where there's already a closing
   *                     quote and/or bracket.
   */
  getInputValueWithCompletionText() {
    const inputBeforeCursor = this.editor.getTextBeforeCursor();
    const inputAfterCursor = this._getValue().substring(
      inputBeforeCursor.length
    );
    let completionText = this.getAutoCompletionText();
    let numberOfCharsToReplaceCharsBeforeCursor;
    let numberOfCharsToMoveTheCursorForward = 0;

    // If the autocompletion popup is open, we always get the selected element from there,
    // since the autocompletion text might not be enough (e.g. `dOcUmEn` should
    // autocomplete to `document`, but the autocompletion text only shows `t`).
    if (this.autocompletePopup.isOpen && this.autocompletePopup.selectedItem) {
      const { selectedItem } = this.autocompletePopup;
      const { label, preLabel, isElementAccess } = selectedItem;

      completionText = label;
      numberOfCharsToReplaceCharsBeforeCursor = preLabel.length;

      // If the user is performing an element access, we need to check if we should add
      // starting and ending quotes, as well as a closing bracket.
      if (isElementAccess) {
        const lastOpeningBracketIndex = inputBeforeCursor.lastIndexOf("[");
        if (lastOpeningBracketIndex > -1) {
          numberOfCharsToReplaceCharsBeforeCursor = inputBeforeCursor.substring(
            lastOpeningBracketIndex + 1
          ).length;
        }

        // If the autoclose bracket option is enabled, the input might be in a state where
        // there's already the closing quote and the closing bracket, e.g.
        // `document["activeEl|"]`, so we don't need to add
        // Let's retrieve the completionText last character, to see if it's a quote.
        const completionTextLastChar =
          completionText[completionText.length - 1];
        const endingQuote = [`"`, `'`, "`"].includes(completionTextLastChar)
          ? completionTextLastChar
          : "";
        if (
          endingQuote &&
          inputAfterCursor.trimLeft().startsWith(endingQuote)
        ) {
          completionText = completionText.substring(
            0,
            completionText.length - 1
          );
          numberOfCharsToMoveTheCursorForward++;
        }

        // If there's not a closing bracket already, we add one.
        if (
          !inputAfterCursor.trimLeft().match(new RegExp(`^${endingQuote}?]`))
        ) {
          completionText = completionText + "]";
        } else {
          // if there's already one, we want to move the cursor after the closing bracket.
          numberOfCharsToMoveTheCursorForward++;
        }
      }
    }

    const expression =
      inputBeforeCursor.substring(
        0,
        inputBeforeCursor.length -
          (numberOfCharsToReplaceCharsBeforeCursor || 0)
      ) +
      completionText +
      inputAfterCursor;

    return {
      completionText,
      expression,
      numberOfCharsToMoveTheCursorForward,
      numberOfCharsToReplaceCharsBeforeCursor,
    };
  }

  /**
   * Set the autocompletion text of the input.
   *
   * @param string suffix
   *        The proposed suffix for the input value.
   */
  setAutoCompletionText(suffix) {
    if (!this.editor) {
      return;
    }

    this.pendingCompletionText = null;

    if (suffix && !this.canDisplayAutoCompletionText()) {
      suffix = "";
    }

    this.editor.setAutoCompletionText(suffix);
  }

  getAutoCompletionText() {
    const renderedCompletionText =
      this.editor && this.editor.getAutoCompletionText();
    return typeof this.pendingCompletionText === "string"
      ? this.pendingCompletionText
      : renderedCompletionText;
  }

  /**
   * Indicate if the input has an autocompletion suggestion, i.e. that there is either
   * something in the autocompletion text or that there's a selected item in the
   * autocomplete popup.
   */
  hasAutocompletionSuggestion() {
    // We can have cases where the popup is opened but we can't display the autocompletion
    // text.
    return (
      this.getAutoCompletionText() ||
      (this.autocompletePopup.isOpen &&
        Number.isInteger(this.autocompletePopup.selectedIndex) &&
        this.autocompletePopup.selectedIndex > -1)
    );
  }

  /**
   * Returns a boolean indicating if we can display an autocompletion text in the input,
   * i.e. if there is no characters displayed on the same line of the cursor and after it.
   */
  canDisplayAutoCompletionText() {
    if (!this.editor) {
      return false;
    }

    const textAfterCursor = this.editor.getTextAfterCursor();
    return textAfterCursor === "";
  }

  onContextMenu(e) {
    this.props.serviceContainer.openEditContextMenu(e);
  }

  destroy() {
    this.autocompleteUpdate.cancel();
    this.terminalInputChanged.cancel();
    this._openPopupPendingPromise = null;

    if (this.autocompletePopup) {
      this.autocompletePopup.destroy();
      this.autocompletePopup = null;
    }

    if (this.#abortController) {
      this.#abortController.abort();
      this.#abortController = null;
    }

    if (this.editor) {
      this.resizeObserver.disconnect();
      this.editor.destroy();
      this.editor = null;
    }

    this.webConsoleUI = null;
    this.#destroyed = true;
  }

  renderOpenEditorButton() {
    if (this.props.editorMode) {
      return null;
    }

    return dom.button({
      className:
        "devtools-button webconsole-input-openEditorButton" +
        (this.props.showEditorOnboarding ? " devtools-feature-callout" : ""),
      title: l10n.getStr("webconsole-input-open-editor-button-tooltip", {
        shortcut: isMacOS ? "Cmd + B" : "Ctrl + B",
      }),
      onClick: this.props.editorToggle,
    });
  }

  renderEvaluationContextSelector() {
    if (this.props.editorMode) {
      return null;
    }

    return EvaluationContextSelector(this.props);
  }

  renderEditorOnboarding() {
    if (!this.props.showEditorOnboarding) {
      return null;
    }

    const enterString = l10n.getStr("webconsole-enter-key");

    return dom.header(
      { className: "editor-onboarding" },
      dom.img({
        className: "editor-onboarding-fox",
        src: "chrome://devtools/skin/images/fox-smiling.svg",
      }),
      dom.p(
        {
          "data-l10n-id": "webconsole-input-editor-onboarding",
          "data-l10n-args": JSON.stringify({
            runShortcut: isMacOS ? `Cmd+${enterString}` : `Ctrl+${enterString}`,
          }),
        },
        dom.span({
          "data-l10n-name": "enter-key",
          className: "editor-onboarding-shortcut",
        }),
        dom.span({
          "data-l10n-name": "run-shortcut",
          className: "editor-onboarding-shortcut",
        })
      ),
      dom.button(
        {
          className: "editor-onboarding-dismiss-button",
          onClick: () => this.props.editorOnboardingDismiss(),
        },
        l10n.getStr("webconsole-input-editor-onboarding-dismiss")
      )
    );
  }

  render() {
    if (!this.props.inputEnabled) {
      return null;
    }

    return dom.div(
      {
        className: "jsterm-input-container devtools-input",
        key: "jsterm-container",
        "aria-live": "off",
        tabIndex: -1,
        onContextMenu: this.onContextMenu,
        ref: node => {
          this.node = node;
        },
      },
      dom.div(
        { className: "webconsole-input-buttons" },
        this.renderEvaluationContextSelector(),
        this.renderOpenEditorButton()
      ),
      this.renderEditorOnboarding()
    );
  }
}

// Redux connect

function mapStateToProps(state) {
  return {
    history: getHistory(state),
    getValueFromHistory: direction => getHistoryValue(state, direction),
    autocompleteData: getAutocompleteState(state),
    showEditorOnboarding: state.ui.showEditorOnboarding,
    autocompletePopupPosition: state.prefs.eagerEvaluation ? "top" : "bottom",
    editorPrettifiedAt: state.ui.editorPrettifiedAt,
  };
}

function mapDispatchToProps(dispatch) {
  return {
    updateHistoryPosition: (direction, expression) =>
      dispatch(actions.updateHistoryPosition(direction, expression)),
    autocompleteUpdate: (force, getterPath, expressionVars) =>
      dispatch(actions.autocompleteUpdate(force, getterPath, expressionVars)),
    autocompleteClear: () => dispatch(actions.autocompleteClear()),
    evaluateExpression: expression =>
      dispatch(actions.evaluateExpression(expression)),
    editorToggle: () => dispatch(actions.editorToggle()),
    editorOnboardingDismiss: () => dispatch(actions.editorOnboardingDismiss()),
    terminalInputChanged: value =>
      dispatch(actions.terminalInputChanged(value)),
  };
}

module.exports = connect(mapStateToProps, mapDispatchToProps)(JSTerm);
