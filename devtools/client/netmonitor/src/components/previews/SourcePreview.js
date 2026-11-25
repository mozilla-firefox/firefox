/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
const {
  Component,
  createElement,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const Editor = require("resource://devtools/client/shared/sourceeditor/editor.js");
const {
  setTargetSearchResult,
} = require("resource://devtools/client/netmonitor/src/actions/search.js");
const searchWorker = require("resource://devtools/client/netmonitor/src/workers/search/index.js");
const buildQuery = require("resource://devtools/client/netmonitor/src/utils/build-query.js");
const { div } = dom;

const FileSearchBar = require("resource://devtools/client/shared/components/FileSearchBar.js");

loader.lazyRequireGetter(
  this,
  "KeyShortcuts",
  "resource://devtools/client/shared/key-shortcuts.js"
);

function scrollList(resultList, index) {
  if (!resultList.hasOwnProperty(index)) {
    return;
  }

  const resultEl = resultList[index];

  const scroll = () => {
    // Avoid expensive DOM computations involved in scrollIntoView
    // https://nolanlawson.com/2018/09/25/accurately-measuring-layout-on-the-web/
    requestAnimationFrame(() => {
      setTimeout(() => {
        if (!resultEl.scrollIntoView) {
          return;
        }
        resultEl.scrollIntoView({ block: "nearest", behavior: "auto" });
      });
    });
  };

  scroll();
}

function find(ctx, query, keepSelection, modifiers, options) {
  clearSearch(ctx);
  return doSearch(ctx, false, query, keepSelection, modifiers, options);
}

function findNext(ctx, query, keepSelection, modifiers) {
  return doSearch(ctx, false, query, keepSelection, modifiers, {});
}

function findPrev(ctx, query, keepSelection, modifiers) {
  return doSearch(ctx, true, query, keepSelection, modifiers, {});
}

function doSearch(
  ctx,
  rev,
  query,
  keepSelection,
  modifiers,
  { shouldScroll = true }
) {
  const { editor } = ctx;
  if (!query || isWhitespace(query)) {
    editor.clearSearchMatches();
    return null;
  }
  const regexQuery = buildQuery(query, modifiers, {
    ignoreSpaces: true,
    // regex must be global for the overlay
    isGlobal: true,
  });

  if (editor.searchState.query?.toString() !== regexQuery.toString()) {
    editor.highlightSearchMatches(regexQuery, "cm-highlight");
  }
  const cursor = editor.getNextSearchCursor(rev);
  if (!cursor) {
    return null;
  }
  editor.setPositionContentMarker({
    id: editor.markerTypes.ACTIVE_SELECTION_MARKER,
    positionClassName: "cm-matchhighlight",
    positions: [{ from: cursor.from, to: cursor.to }],
  });
  if (shouldScroll) {
    editor.scrollToPosition(cursor.from);
  }
  return editor.getPositionFromSearchCursor(cursor);
}

function isWhitespace(query) {
  return !query.match(/\S/);
}

function clearSearch(ctx) {
  const { editor } = ctx;
  editor.clearSearchMatches();
  editor.removePositionContentMarker("active-selection-marker");
}

/**
 * CodeMirror editor as a React component
 */
class SourcePreview extends Component {
  static get propTypes() {
    return {
      // Source editor syntax highlight mimeType, which is a mime type defined in CodeMirror
      mimeType: PropTypes.string,
      // Source editor content
      text: PropTypes.string,
      // Search result text to select
      targetSearchResult: PropTypes.object,
      // Reset target search result that has been used for navigation in this panel.
      // This is done to avoid second navigation the next time.
      resetTargetSearchResult: PropTypes.func,
      url: PropTypes.string,
    };
  }

  state = {
    shortcutsReady: false,
    searchInFileEnabled: false,
    searchOptions: {
      caseSensitive: false,
      wholeWord: false,
      regexMatch: false,
    },
  };

  componentDidMount() {
    this.shortcuts = new KeyShortcuts(this.editorRowContainer);

    this.setState({ shortcutsReady: true });
    this.loadEditor();
    this.updateEditor();
  }

  componentDidUpdate(prevProps) {
    const { targetSearchResult, text } = this.props;
    if (prevProps.text !== text) {
      // When updating from editor to editor
      this.updateEditor();
    } else if (prevProps.targetSearchResult !== targetSearchResult) {
      this.findSearchResult();
    }
  }

  componentWillUnmount() {
    if (this.shortcuts) {
      this.shortcuts.destroy();
    }
    this.unloadEditor();
  }

  setSearchOptions = (_searchKey, searchOptions) => {
    this.setState({ searchOptions });
  };

  setActiveSearch = () => {
    this.setState({ searchInFileEnabled: true });
  };

  closeFileSearch = () => {
    this.setState({ searchInFileEnabled: false });
  };

  setCursorLocation = async (line, ch) => {
    if (!this.editor) {
      return;
    }
    await this.editor.setCursorAt(line + 1, ch);
  };

  getSourceEditorModeForMimetype(mimeType) {
    const lang = mimeType.split("/")[1];
    return Editor.modes[lang];
  }

  loadEditor() {
    this.editor = new Editor({
      cm6: true,
      lineNumbers: true,
      lineWrapping: false,
      disableSearchAddon: true,
      useSearchAddonPanel: false,
      mode: null, // Disable auto syntax detection, but then we set the mode later
      readOnly: true,
      theme: "mozilla",
      value: "",
    });

    this.editor.appendToLocalElement(this.refs.editorElement);
    // Used for netmonitor tests
    window.codeMirrorSourceEditorTestInstance = this.editor;
  }

  async updateEditor() {
    const { mimeType, text, url } = this.props;
    if (this?.editor?.hasCodeMirror) {
      const mode = this.getSourceEditorModeForMimetype(mimeType);
      await this.editor.setMode(mode);
      await this.editor.setText(text, { documentId: url });
      // When navigating from the netmonitor search, find and highlight the
      // the current search result.
      await this.findSearchResult();
    }
  }

  unloadEditor() {
    if (this.editor) {
      this.editor.destroy();
      this.editor = null;
    }
  }

  async findSearchResult() {
    const { targetSearchResult, resetTargetSearchResult } = this.props;
    if (targetSearchResult?.line) {
      const { line } = targetSearchResult;
      // scroll the editor to center the line
      // with the target search result
      if (this.editor) {
        await this.editor.setCursorAt(line, 0);

        // Highlight line
        this.editor.setLineContentMarker({
          id: this.editor.markerTypes.HIGHLIGHT_LINE_MARKER,
          lineClassName: "highlight-line",
          lines: [{ line }],
        });
        this.clearHighlightLineAfterDuration();
      }
    }

    resetTargetSearchResult();
  }

  clearHighlightLineAfterDuration() {
    const editorContainer = document.querySelector(".editor-row-container");

    if (editorContainer === null) {
      return;
    }

    const duration = parseInt(
      getComputedStyle(editorContainer).getPropertyValue(
        "--highlight-line-duration"
      ),
      10
    );

    const highlightTimeout = setTimeout(() => {
      if (!this.editor) {
        return;
      }
      clearTimeout(highlightTimeout);
      this.editor.removeLineContentMarker("highlight-line-marker");
    }, duration);
  }

  querySearchWorker = (query, text, modifiers) => {
    return searchWorker.getMatches(query, text, modifiers);
  };

  render() {
    return div(
      {
        className: "editor-row-container",
        ref: el => (this.editorRowContainer = el),
      },
      div({
        ref: "editorElement",
        className: "source-editor-mount devtools-monospace",
      }),
      this.state.shortcutsReady
        ? createElement(FileSearchBar, {
            editor: this.editor,
            textContent: { type: "text", value: this.props.text },
            searchInFileEnabled: this.state.searchInFileEnabled,
            modifiers: this.state.searchOptions,
            searchOptions: this.state.searchOptions,
            shouldScroll: true,
            searchKey: "NETMONITOR_SOURCE_PREVIEW_SEARCH",
            shortcuts: this.shortcuts,
            setSearchOptions: this.setSearchOptions,
            setActiveSearch: this.setActiveSearch,
            closeFileSearch: this.closeFileSearch,
            setCursorLocation: this.setCursorLocation,
            querySearchWorker: this.querySearchWorker,
            scrollList,
            clearSearchEditor: clearSearch,
            find,
            findNext,
            findPrev,
          })
        : null
    );
  }
}

module.exports = connect(
  state => {
    if (!state.search) {
      return null;
    }
    return {
      targetSearchResult: state.search.targetSearchResult,
    };
  },
  dispatch => ({
    resetTargetSearchResult: () => dispatch(setTargetSearchResult(null)),
  })
)(SourcePreview);
