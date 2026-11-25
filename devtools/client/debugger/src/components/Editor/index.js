/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import PropTypes from "devtools/client/shared/vendor/react-prop-types";
import React, { PureComponent } from "devtools/client/shared/vendor/react";
import { div } from "devtools/client/shared/vendor/react-dom-factories";
import { bindActionCreators } from "devtools/client/shared/vendor/redux";
import ReactDOM from "devtools/client/shared/vendor/react-dom";
import { connect } from "devtools/client/shared/vendor/react-redux";

import { getLineText, isLineBlackboxed } from "./../../utils/source";
import { createLocation } from "./../../utils/location";
import { asSettled, isFulfilled, isRejected } from "../../utils/async-value";

import {
  getActiveSearch,
  getSelectedLocation,
  getSelectedSource,
  getSelectedSourceTextContent,
  getSelectedBreakableLines,
  getConditionalPanelLocation,
  getIsCurrentThreadPaused,
  getSkipPausing,
  getInlinePreview,
  getBlackBoxRanges,
  isSourceBlackBoxed,
  getHighlightedLineRangeForSelectedSource,
  isSourceMapIgnoreListEnabled,
  isSourceOnSourceMapIgnoreList,
  isMapScopesEnabled,
  getSelectedTraceIndex,
  getShouldScrollToSelectedLocation,
  getShouldHighlightSelectedLocation,
  getSelectedTraceLocation,
  getSearchOptions,
  getBreakpointsList,
} from "../../selectors/index";

// Redux actions
import actions from "../../actions/index";

import FileSearch from "./FileSearch";
import HighlightLines from "./HighlightLines";
import Preview from "./Preview/index";
import Breakpoints from "./Breakpoints";
import ColumnBreakpoints from "./ColumnBreakpoints";
import DebugLine from "./DebugLine";
import HighlightLine from "./HighlightLine";
import ConditionalPanel from "./ConditionalPanel";
import TracePanel from "./TracePanel";
import InlinePreviews from "./InlinePreviews";
import Exceptions from "./Exceptions";

import {
  fromEditorLine,
  getEditor,
  removeEditor,
  toSourceLine,
  toEditorPosition,
  onMouseOver,
  clearSearch as clearSearchEditor,
  find,
  findNext,
  findPrev,
} from "../../utils/editor/index";

import { searchKeys } from "../../constants";
import { scrollList } from "../../utils/result-list";

import { updateEditorSizeCssVariables } from "../../utils/ui";

const { debounce } = require("resource://devtools/shared/debounce.js");
const { throttle } = require("resource://devtools/shared/throttle.js");
const classnames = require("resource://devtools/client/shared/classnames.js");
const SourceEditor = require("resource://devtools/client/shared/sourceeditor/editor.js");

const { appinfo } = Services;
const isMacOS = appinfo.OS === "Darwin";

function isSecondary(ev) {
  return isMacOS && ev.ctrlKey && ev.button === 0;
}

function isCmd(ev) {
  return isMacOS ? ev.metaKey : ev.ctrlKey;
}

const cssVars = {
  searchbarHeight: "var(--editor-searchbar-height)",
};

class Editor extends PureComponent {
  static get propTypes() {
    return {
      selectedSource: PropTypes.object,
      selectedSourceTextContent: PropTypes.object,
      selectedSourceIsBlackBoxed: PropTypes.bool,
      closeTabForSource: PropTypes.func.isRequired,
      toggleBreakpointAtLine: PropTypes.func.isRequired,
      conditionalPanelLocation: PropTypes.object,
      closeConditionalPanel: PropTypes.func.isRequired,
      openConditionalPanel: PropTypes.func.isRequired,
      updateViewport: PropTypes.func.isRequired,
      isPaused: PropTypes.bool.isRequired,
      addBreakpointAtLine: PropTypes.func.isRequired,
      continueToHere: PropTypes.func.isRequired,
      jumpToMappedLocation: PropTypes.func.isRequired,
      selectedLocation: PropTypes.object,
      startPanelSize: PropTypes.number.isRequired,
      endPanelSize: PropTypes.number.isRequired,
      searchInFileEnabled: PropTypes.bool.isRequired,
      inlinePreviewEnabled: PropTypes.bool.isRequired,
      skipPausing: PropTypes.bool.isRequired,
      blackboxedRanges: PropTypes.object.isRequired,
      breakableLines: PropTypes.object.isRequired,
      highlightedLineRange: PropTypes.object,
      isSourceOnIgnoreList: PropTypes.bool,
      isOriginalSourceAndMapScopesEnabled: PropTypes.bool,
      shouldScrollToSelectedLocation: PropTypes.bool,
      setInScopeLines: PropTypes.func,
      modifiers: PropTypes.object.isRequired,
      setActiveSearch: PropTypes.func.isRequired,
      closeFileSearch: PropTypes.func.isRequired,
      querySearchWorker: PropTypes.func.isRequired,
      selectLocation: PropTypes.func.isRequired,
      showEditorContextMenu: PropTypes.func.isRequired,
      showEditorGutterContextMenu: PropTypes.func.isRequired,
      updateStyleSheetContent: PropTypes.func.isRequired,
    };
  }

  $editorWrapper;
  constructor(props) {
    super(props);

    this.state = {
      editor: null,
    };
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    let { editor } = this.state;
    const prevEditor = editor;

    if (!editor) {
      // See Bug 1913061
      if (!nextProps.selectedSource) {
        return;
      }
      editor = this.setupEditor();
    }

    this.setTextContent(nextProps, editor, prevEditor);
  }

  async setTextContent(nextProps, editor, prevEditor) {
    const shouldUpdateText =
      nextProps.selectedSource !== this.props.selectedSource ||
      nextProps.selectedSourceTextContent?.value !==
        this.props.selectedSourceTextContent?.value ||
      // If the selectedSource gets set before the editor get selected, make sure we update the text
      prevEditor !== editor;

    const shouldScroll =
      nextProps.selectedLocation &&
      nextProps.shouldScrollToSelectedLocation &&
      this.shouldScrollToLocation(nextProps);

    if (shouldUpdateText) {
      await this.setText(nextProps, editor);
    }

    if (shouldScroll) {
      await this.scrollToLocation(nextProps, editor);
    }
    // Note: Its important to get the scope lines after
    // the scrolling is complete to make sure codemirror
    // has loaded the content for the current viewport.
    //
    // Also if scope mapping is on, the babel parser worker
    // will be used instead (for scope mapping) as the preview data relies
    // on it for original variable mapping.
    if (nextProps.isPaused && !nextProps.isOriginalSourceAndMapScopesEnabled) {
      this.props.setInScopeLines(editor);
    }
  }

  onEditorUpdated = viewUpdate => {
    const { editor } = this.state;
    if (viewUpdate.docChanged || viewUpdate.geometryChanged) {
      updateEditorSizeCssVariables(viewUpdate.view.dom);
      const { selectedLocation } = this.props;
      // The updates made to the  stylesheet contents are
      // only sent when these conditions are satisfied.
      if (
        // When a source is selected
        selectedLocation &&
        // When it is actually a stylesheet
        selectedLocation.source.isStyleSheet &&
        // When a user changes the doc content
        editor.isViewUpdateFromUserInput(viewUpdate)
      ) {
        this.updateStyleSheetText(
          selectedLocation.sourceActor,
          viewUpdate.state.doc.toString()
        );
      }
      this.props.updateViewport();
    } else if (viewUpdate.selectionSet) {
      this.onCursorChange();
    }
  };

  setupEditor() {
    const editor = getEditor();

    // disables the default search shortcuts
    editor._initShortcuts = () => {};

    const node = ReactDOM.findDOMNode(this);
    const mountEl = node.querySelector(".editor-mount");
    if (node instanceof HTMLElement) {
      editor.appendToLocalElement(mountEl);
    }

    editor.setUpdateListener(this.onEditorUpdated);
    editor.enableGutter({
      click: (event, cm, line) => {
        // Ignore clicks on the code folding button
        if (
          event.target.className.includes("cm6-dt-foldgutter__toggle-button")
        ) {
          return;
        }
        // Clicking any where on the fold gutter (except on a code folding button)
        // should toggle the breakpoint for this line, if possible.
        if (event.target.className.includes("cm-foldGutter")) {
          this.props.toggleBreakpointAtLine(line);
          return;
        }
        this.onGutterClick(cm, line, null, event);
      },
      contextmenu: (event, cm, line) => this.openMenu(event, line),
    });
    editor.addEditorDOMEventListeners({
      click: (event, cm, line, column) => this.onClick(event, line, column),
      contextmenu: (event, cm, line, column) =>
        this.openMenu(event, line, column),
      mouseover: onMouseOver(editor),
    });

    this.setState({ editor });
    // Used for tests
    Object.defineProperty(window, "codeMirrorSourceEditorTestInstance", {
      get() {
        return editor;
      },
    });
    return editor;
  }

  componentDidMount() {
    const { shortcuts } = this.context;

    shortcuts.on(L10N.getStr("toggleBreakpoint.key"), this.onToggleBreakpoint);
    shortcuts.on(
      L10N.getStr("toggleAllBreakpoints.key"),
      this.onToggleAllBreakpoints
    );
    shortcuts.on(
      L10N.getStr("toggleCondPanel.breakpoint.key"),
      this.onToggleConditionalPanel
    );
    shortcuts.on(
      L10N.getStr("toggleCondPanel.logPoint.key"),
      this.onToggleLogPanel
    );
    shortcuts.on(
      L10N.getStr("sourceTabs.closeTab.key"),
      this.onCloseShortcutPress
    );
    shortcuts.on("Esc", this.onEscape);
  }

  onCloseShortcutPress = e => {
    const { selectedSource } = this.props;
    if (selectedSource) {
      e.preventDefault();
      e.stopPropagation();
      this.props.closeTabForSource(selectedSource);
    }
  };

  componentDidUpdate(prevProps, prevState) {
    const {
      selectedSource,
      selectedLocation,
      blackboxedRanges,
      isSourceOnIgnoreList,
      breakableLines,
    } = this.props;
    const { editor } = this.state;

    if (!selectedSource || !editor) {
      return;
    }

    const shouldUpdateBreakableLines =
      prevProps.breakableLines.size !== this.props.breakableLines.size ||
      prevProps.selectedSource?.id !== selectedSource.id ||
      prevProps.selectedLocation?.sourceActor?.id !==
        selectedLocation?.sourceActor?.id ||
      // Make sure we update after the editor has loaded
      (!prevState.editor && !!editor);

    if (shouldUpdateBreakableLines) {
      editor.setLineGutterMarkers([
        {
          id: editor.markerTypes.EMPTY_LINE_MARKER,
          lineClassName: "empty-line",
          condition: line => {
            const lineNumber = fromEditorLine(selectedSource, line);
            return !breakableLines.has(lineNumber);
          },
        },
      ]);
    }

    editor.setLineGutterMarkers([
      {
        id: editor.markerTypes.BLACKBOX_LINE_GUTTER_MARKER,
        lineClassName: "blackboxed-line",
        condition: line => {
          const lineNumber = fromEditorLine(selectedSource, line);
          return isLineBlackboxed(
            blackboxedRanges[selectedSource.url],
            lineNumber,
            isSourceOnIgnoreList
          );
        },
      },
    ]);

    if (
      prevProps.selectedSource?.id !== selectedSource.id ||
      prevProps.blackboxedRanges[selectedSource.url]?.length !==
        blackboxedRanges[selectedSource.url]?.length ||
      (!prevState.editor && !!editor)
    ) {
      if (blackboxedRanges[selectedSource.url] == undefined) {
        editor.removeLineContentMarker(editor.markerTypes.BLACKBOX_LINE_MARKER);
        return;
      }

      const lines = [];
      for (const range of blackboxedRanges[selectedSource.url]) {
        for (let line = range.start.line; line <= range.end.line; line++) {
          lines.push({ line });
        }
      }

      editor.setLineContentMarker({
        id: editor.markerTypes.BLACKBOX_LINE_MARKER,
        lineClassName: "blackboxed-line",
        // If the the whole source is blackboxed, lets just mark all positions.
        shouldMarkAllLines: !blackboxedRanges[selectedSource.url].length,
        lines,
      });
    }
  }

  componentWillUnmount() {
    const { editor } = this.state;
    const { shortcuts } = this.context;
    shortcuts.off(L10N.getStr("sourceTabs.closeTab.key"));
    shortcuts.off(L10N.getStr("toggleBreakpoint.key"));
    shortcuts.off(L10N.getStr("toggleAllBreakpoints.key"));
    shortcuts.off(L10N.getStr("toggleCondPanel.breakpoint.key"));
    shortcuts.off(L10N.getStr("toggleCondPanel.logPoint.key"));

    if (this.abortController) {
      this.abortController.abort();
      this.abortController = null;
    }

    if (editor) {
      editor.destroy();
      this.setState({ editor: null });
      removeEditor();
    }
  }

  getCurrentPosition() {
    const { editor } = this.state;
    const { selectedLocation } = this.props;
    if (!selectedLocation) {
      return null;
    }
    let { line, column } = selectedLocation;
    // When no specific line has been selected, fallback to the current cursor posiiton
    if (line == 0) {
      const selectionCursor = editor.getSelectionCursor();
      line = toSourceLine(selectedLocation.source, selectionCursor.from.line);
      column = selectionCursor.from.ch;
    }
    return { line, column };
  }

  onToggleBreakpoint = e => {
    e.preventDefault();
    e.stopPropagation();

    const currentPosition = this.getCurrentPosition();
    if (!currentPosition || typeof currentPosition.line !== "number") {
      return;
    }

    this.props.toggleBreakpointAtLine(currentPosition.line);
  };

  onToggleAllBreakpoints = e => {
    e.preventDefault();
    e.stopPropagation();

    const shouldDisableBreakpoints = this.props.breakpoints.every(
      b => !b.disabled
    );

    this.props.toggleAllBreakpoints(shouldDisableBreakpoints);
  };

  onToggleLogPanel = e => {
    e.stopPropagation();
    e.preventDefault();
    this.toggleBreakpointPanel(true);
  };

  onToggleConditionalPanel = e => {
    e.stopPropagation();
    e.preventDefault();
    this.toggleBreakpointPanel(false);
  };

  toggleBreakpointPanel(logPanel) {
    const {
      conditionalPanelLocation,
      closeConditionalPanel,
      openConditionalPanel,
      selectedLocation,
    } = this.props;

    const currentPosition = this.getCurrentPosition();

    if (conditionalPanelLocation) {
      return closeConditionalPanel();
    }

    if (!selectedLocation || typeof currentPosition?.line !== "number") {
      return null;
    }

    return openConditionalPanel(
      createLocation({
        line: currentPosition.line,
        column: currentPosition.column,
        source: selectedLocation.source,
        sourceActor: selectedLocation.sourceActor,
      }),
      logPanel
    );
  }

  onEditorScroll = debounce(this.props.updateViewport, 75);

  updateStyleSheetText = throttle(this.props.updateStyleSheetContent, 500);

  /*
   * The default Esc command is overridden in the CodeMirror keymap to allow
   * the Esc keypress event to be catched by the toolbox and trigger the
   * split console. Restore it here, but preventDefault if and only if there
   * is a multiselection.
   */
  onEscape() {}

  // Note: The line is optional, if not passed it fallsback to lineAtHeight.
  openMenu(event, line, ch) {
    event.stopPropagation();
    event.preventDefault();

    const {
      selectedLocation,
      selectedSourceTextContent,
      conditionalPanelLocation,
      closeConditionalPanel,
    } = this.props;

    const { editor } = this.state;

    if (!selectedLocation || !editor) {
      return;
    }

    // only allow one conditionalPanel location.
    if (conditionalPanelLocation) {
      closeConditionalPanel();
    }

    const target = event.target;

    if (typeof line != "number") {
      return;
    }

    if (
      target.classList.contains("cm-gutter") ||
      target.classList.contains("cm-gutterElement")
    ) {
      const location = createLocation({
        source: selectedLocation.source,
        sourceActor: selectedLocation.sourceActor,
        line,
        column: undefined,
      });

      const lineText = getLineText(
        selectedLocation.source.id,
        selectedSourceTextContent,
        line
      ).trim();

      const lineObject = { from: { line, ch }, to: { line, ch } };

      this.props.showEditorGutterContextMenu(
        event,
        lineObject,
        location,
        lineText
      );
      return;
    }

    if (target.getAttribute("id") === "columnmarker") {
      return;
    }

    const location = createLocation({
      source: selectedLocation.source,
      sourceActor: selectedLocation.sourceActor,
      line: fromEditorLine(selectedLocation.source, line),
      column: editor.isWasm ? 0 : ch,
    });

    const lineObject = editor.getSelectionCursor();
    this.props.showEditorContextMenu(event, editor, lineObject, location);
  }

  /**
   * CodeMirror event handler, called whenever the cursor moves
   * for user-driven or programatic reasons.
   */
  onCursorChange = () => {
    const { editor } = this.state;
    if (!editor || !this.props.selectedLocation) {
      return;
    }
    const { selectedLocation } = this.props;
    const selectionCursor = editor.getSelectionCursor();
    const { line, ch } = selectionCursor.to;

    // Avoid triggering an infinite loop of select location action
    // if we moved the cursor to the location already stored in redux
    //
    // About columns: consider the reducer's column set to undefined as equivalent to selecting column 0 in CodeMirror.
    // This also avoids dispatching duplicated `selectLocation` actions when selecting a line without a specific column.
    if (
      selectedLocation.line == line &&
      ((typeof selectedLocation.column != "number" && ch == 0) ||
        selectedLocation.column == ch)
    ) {
      return;
    }

    this.props.selectLocation(
      createLocation({
        source: selectedLocation.source,
        sourceActor: selectedLocation.sourceActor,
        line: toSourceLine(selectedLocation.source, line),
        column: ch,
      }),
      {
        // Reset the context, so that we don't switch to original
        // while moving the cursor within a bundle
        keepContext: false,

        // Avoid highlighting the selected line
        highlight: false,

        // Avoid scrolling to the selected line, it's already visible
        scroll: false,
      }
    );
  };

  onGutterClick = (cm, line, gutter, ev) => {
    const {
      selectedLocation,
      conditionalPanelLocation,
      closeConditionalPanel,
      addBreakpointAtLine,
      continueToHere,
      breakableLines,
      blackboxedRanges,
      isSourceOnIgnoreList,
    } = this.props;

    // ignore right clicks in the gutter
    if (isSecondary(ev) || ev.button === 2 || !selectedLocation) {
      return;
    }

    if (conditionalPanelLocation) {
      closeConditionalPanel();
      return;
    }

    if (gutter === "CodeMirror-foldgutter") {
      return;
    }

    const sourceLine = toSourceLine(selectedLocation.source, line);
    if (typeof sourceLine !== "number") {
      return;
    }

    // ignore clicks on a non-breakable line
    if (!breakableLines.has(sourceLine)) {
      return;
    }

    if (isCmd(ev)) {
      continueToHere(
        createLocation({
          source: selectedLocation.source,
          sourceActor: selectedLocation.sourceActor,
          line: sourceLine,
          column: undefined,
        })
      );
      return;
    }

    addBreakpointAtLine(
      sourceLine,
      ev.altKey,
      ev.shiftKey ||
        isLineBlackboxed(
          blackboxedRanges[selectedLocation.source.url],
          sourceLine,
          isSourceOnIgnoreList
        )
    );
  };

  onClick(e, line, ch) {
    const { selectedLocation, jumpToMappedLocation } = this.props;

    if (!selectedLocation) {
      return;
    }

    const sourceLocation = createLocation({
      source: selectedLocation.source,
      sourceActor: selectedLocation.sourceActor,
      line: fromEditorLine(selectedLocation.source, line),
      column: this.state.editor.isWasm ? 0 : ch,
    });

    if (e.metaKey && e.altKey) {
      jumpToMappedLocation(sourceLocation);
    }
  }

  shouldScrollToLocation(nextProps) {
    if (
      !nextProps.selectedLocation?.line ||
      !nextProps.selectedSourceTextContent
    ) {
      return false;
    }

    const { selectedLocation, selectedSourceTextContent } = this.props;
    const contentChanged =
      !selectedSourceTextContent?.value &&
      nextProps.selectedSourceTextContent?.value;
    const locationChanged = selectedLocation !== nextProps.selectedLocation;

    return contentChanged || locationChanged;
  }

  async scrollToLocation(nextProps, editor) {
    const { selectedLocation } = nextProps;
    const { line, column } = toEditorPosition(selectedLocation);
    await editor.scrollTo(line, column);

    // Prevent focusing codemirror if we don't want to highlight the line
    // mostly to avoid changing the focus from where it currently is.
    // For example in the Search Bar.
    if (this.props.shouldHighlightSelectedLocation) {
      editor.focus();
    }
    // This should also scroll the editor to the specified position
    await editor.setCursorAt(line, column);
  }

  async setText(props, editor) {
    const { selectedSource, selectedSourceTextContent } = props;

    if (!editor) {
      return;
    }

    // check if we previously had a selected source
    if (!selectedSource) {
      return;
    }

    if (!selectedSourceTextContent?.value) {
      this.showLoadingMessage(editor);
      return;
    }

    if (isRejected(selectedSourceTextContent)) {
      let { value } = selectedSourceTextContent;
      if (typeof value !== "string") {
        value = "Unexpected source error";
      }

      this.showErrorMessage(value);
      return;
    }

    if (selectedSource.isStyleSheet) {
      await editor.setMode(SourceEditor.modes.css);
    }
    await editor.setText(selectedSourceTextContent.value.value, {
      documentId: selectedSource.id,
    });
    const isReadOnly =
      !selectedSource.isStyleSheet ||
      (selectedSource.isOriginal && !selectedSource.isPrettyPrinted);
    await editor.setReadOnly(isReadOnly);
  }

  showErrorMessage(msg) {
    const { editor } = this.state;
    if (!editor) {
      return;
    }

    let error;
    if (msg.includes("WebAssembly binary source is not available")) {
      error = L10N.getStr("wasmIsNotAvailable");
    } else {
      error = L10N.getFormatStr("errorLoadingText3", msg);
    }
    editor.setText(error);
  }

  showLoadingMessage(editor) {
    editor.setText(L10N.getStr("loadingText"));
  }

  getInlineEditorStyles() {
    const { searchInFileEnabled } = this.props;

    if (searchInFileEnabled) {
      return {
        height: `calc(100% - ${cssVars.searchbarHeight})`,
      };
    }

    return {
      height: "100%",
    };
  }

  // eslint-disable-next-line complexity
  renderItems() {
    const {
      selectedSource,
      conditionalPanelLocation,
      isPaused,
      isTraceSelected,
      inlinePreviewEnabled,
      highlightedLineRange,
      isOriginalSourceAndMapScopesEnabled,
      selectedSourceTextContent,
      selectedTraceLocation,
    } = this.props;
    const { editor } = this.state;

    if (!selectedSource || !editor) {
      return null;
    }

    // Only load the sub components if the content has loaded without issues.
    if (selectedSourceTextContent && !isFulfilled(selectedSourceTextContent)) {
      return null;
    }

    return React.createElement(
      React.Fragment,
      null,
      React.createElement(Breakpoints, { editor }),
      selectedTraceLocation
        ? React.createElement(TracePanel, {
            editor,
          })
        : null,
      (isPaused || isTraceSelected) &&
        selectedSource.isOriginal &&
        !selectedSource.isPrettyPrinted &&
        !isOriginalSourceAndMapScopesEnabled
        ? null
        : React.createElement(Preview, {
            editor,
            editorRef: this.$editorWrapper,
          }),
      React.createElement(DebugLine, { editor, selectedSource }),
      React.createElement(HighlightLine, { editor }),
      React.createElement(Exceptions, { editor }),
      conditionalPanelLocation
        ? React.createElement(ConditionalPanel, {
            editor,
            selectedSource,
          })
        : null,
      (isPaused || isTraceSelected) &&
        inlinePreviewEnabled &&
        (!selectedSource.isOriginal ||
          selectedSource.isPrettyPrinted ||
          isOriginalSourceAndMapScopesEnabled)
        ? React.createElement(InlinePreviews, {
            editor,
          })
        : null,
      highlightedLineRange
        ? React.createElement(HighlightLines, {
            editor,
            range: highlightedLineRange,
          })
        : null,
      React.createElement(ColumnBreakpoints, {
        editor,
      })
    );
  }

  renderFileSearch() {
    const {
      selectedLocation,
      selectedSourceTextContent,
      isPaused,
      searchInFileEnabled,
      modifiers,
      setActiveSearch,
      closeFileSearch,
      querySearchWorker,
      selectLocation,
      searchOptions,
      setSearchOptions,
    } = this.props;

    if (!selectedLocation) {
      return null;
    }

    const textContent =
      selectedSourceTextContent && isFulfilled(selectedSourceTextContent)
        ? selectedSourceTextContent.value
        : null;

    return React.createElement(FileSearch, {
      editor: this.state.editor,
      setActiveSearch,
      closeFileSearch,
      querySearchWorker,
      selectLocation,
      searchOptions,
      setSearchOptions,
      scrollList,
      createLocation,
      clearSearchEditor,
      find,
      findNext,
      findPrev,
      textContent,
      modifiers,
      searchInFileEnabled,
      selectedLocation,
      shouldScroll: !isPaused,
    });
  }

  render() {
    const { selectedSourceIsBlackBoxed, skipPausing } = this.props;
    return div(
      {
        className: classnames("editor-wrapper", {
          blackboxed: selectedSourceIsBlackBoxed,
          "skip-pausing": skipPausing,
        }),
        ref: c => (this.$editorWrapper = c),
      },
      div({
        className: "editor-mount devtools-monospace",
        style: this.getInlineEditorStyles(),
      }),
      this.renderFileSearch(),
      this.renderItems()
    );
  }
}

Editor.contextTypes = {
  shortcuts: PropTypes.object,
};

const mapStateToProps = state => {
  const selectedSource = getSelectedSource(state);
  const selectedLocation = getSelectedLocation(state);

  const selectedSourceTextContent = getSelectedSourceTextContent(state);

  return {
    selectedLocation,
    selectedSource,
    // Settled means the content loaded succesfully (fulfilled) or the there was
    // error (rejected)
    selectedSourceTextContent: asSettled(selectedSourceTextContent),
    selectedSourceIsBlackBoxed: selectedSource
      ? isSourceBlackBoxed(state, selectedSource)
      : null,
    isSourceOnIgnoreList:
      isSourceMapIgnoreListEnabled(state) &&
      isSourceOnSourceMapIgnoreList(state, selectedSource),
    searchInFileEnabled: getActiveSearch(state) === "file",
    conditionalPanelLocation: getConditionalPanelLocation(state),
    isPaused: getIsCurrentThreadPaused(state),
    isTraceSelected: getSelectedTraceIndex(state) != null,
    skipPausing: getSkipPausing(state),
    inlinePreviewEnabled: getInlinePreview(state),
    blackboxedRanges: getBlackBoxRanges(state),
    breakableLines: getSelectedBreakableLines(state),
    highlightedLineRange: getHighlightedLineRangeForSelectedSource(state),
    isOriginalSourceAndMapScopesEnabled:
      selectedSource?.isOriginal && isMapScopesEnabled(state),
    shouldScrollToSelectedLocation: getShouldScrollToSelectedLocation(state),
    shouldHighlightSelectedLocation: getShouldHighlightSelectedLocation(state),
    selectedTraceLocation: getSelectedTraceLocation(state),
    modifiers: getSearchOptions(state, "file-search"),
    searchOptions: getSearchOptions(state, searchKeys.FILE_SEARCH),
    breakpoints: getBreakpointsList(state),
  };
};

const mapDispatchToProps = dispatch => ({
  ...bindActionCreators(
    {
      openConditionalPanel: actions.openConditionalPanel,
      closeConditionalPanel: actions.closeConditionalPanel,
      continueToHere: actions.continueToHere,
      toggleBreakpointAtLine: actions.toggleBreakpointAtLine,
      toggleAllBreakpoints: actions.toggleAllBreakpoints,
      addBreakpointAtLine: actions.addBreakpointAtLine,
      jumpToMappedLocation: actions.jumpToMappedLocation,
      updateViewport: actions.updateViewport,
      closeTabForSource: actions.closeTabForSource,
      showEditorContextMenu: actions.showEditorContextMenu,
      showEditorGutterContextMenu: actions.showEditorGutterContextMenu,
      selectLocation: actions.selectLocation,
      setInScopeLines: actions.setInScopeLines,
      setActiveSearch: actions.setActiveSearch,
      closeFileSearch: actions.closeFileSearch,
      querySearchWorker: actions.querySearchWorker,
      setSearchOptions: actions.setSearchOptions,
      updateStyleSheetContent: actions.updateStyleSheetContent,
    },
    dispatch
  ),
});

export default connect(mapStateToProps, mapDispatchToProps)(Editor);
