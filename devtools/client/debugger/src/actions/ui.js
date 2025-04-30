/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import {
  getActiveSearch,
  getPaneCollapse,
  getQuickOpenEnabled,
  getSource,
  getSourceTextContent,
  getIgnoreListSourceUrls,
  getSourceByURL,
  getBreakpointsForSource,
} from "../selectors/index";
import { selectSource } from "../actions/sources/select";
import { getEditor, updateEditorLineWrapping } from "../utils/editor/index";
import { blackboxSourceActorsForSource } from "./sources/blackbox";
import { toggleBreakpoints } from "./breakpoints/index";
import { copyToTheClipboard } from "../utils/clipboard";
import { isFulfilled } from "../utils/async-value";
import { primaryPaneTabs } from "../constants";

export function setPrimaryPaneTab(tabName) {
  return { type: "SET_PRIMARY_PANE_TAB", tabName };
}

export function closeActiveSearch() {
  return {
    type: "TOGGLE_ACTIVE_SEARCH",
    value: null,
  };
}

export function setActiveSearch(activeSearch) {
  return ({ dispatch, getState }) => {
    const activeSearchState = getActiveSearch(getState());
    if (activeSearchState === activeSearch) {
      return;
    }

    if (getQuickOpenEnabled(getState())) {
      dispatch({ type: "CLOSE_QUICK_OPEN" });
    }

    // Open start panel if it was collapsed so the project search UI is visible
    if (
      activeSearch === primaryPaneTabs.PROJECT_SEARCH &&
      getPaneCollapse(getState(), "start")
    ) {
      dispatch({
        type: "TOGGLE_PANE",
        position: "start",
        paneCollapsed: false,
      });
    }

    dispatch({
      type: "TOGGLE_ACTIVE_SEARCH",
      value: activeSearch,
    });
  };
}

export function toggleFrameworkGrouping(toggleValue) {
  return ({ dispatch }) => {
    dispatch({
      type: "TOGGLE_FRAMEWORK_GROUPING",
      value: toggleValue,
    });
  };
}

export function toggleInlinePreview(toggleValue) {
  return ({ dispatch }) => {
    dispatch({
      type: "TOGGLE_INLINE_PREVIEW",
      value: toggleValue,
    });
  };
}

export function toggleEditorWrapping(toggleValue) {
  return ({ dispatch }) => {
    updateEditorLineWrapping(toggleValue);

    dispatch({
      type: "TOGGLE_EDITOR_WRAPPING",
      value: toggleValue,
    });
  };
}

export function toggleSourceMapsEnabled(toggleValue) {
  return ({ dispatch }) => {
    dispatch({
      type: "TOGGLE_SOURCE_MAPS_ENABLED",
      value: toggleValue,
    });
  };
}

export function showSource(sourceId) {
  return ({ dispatch, getState }) => {
    const source = getSource(getState(), sourceId);
    if (!source) {
      return;
    }

    if (getPaneCollapse(getState(), "start")) {
      dispatch({
        type: "TOGGLE_PANE",
        position: "start",
        paneCollapsed: false,
      });
    }

    dispatch(setPrimaryPaneTab("sources"));

    dispatch(selectSource(source));
  };
}

export function togglePaneCollapse(position, paneCollapsed) {
  return ({ dispatch, getState }) => {
    const prevPaneCollapse = getPaneCollapse(getState(), position);
    if (prevPaneCollapse === paneCollapsed) {
      return;
    }

    // Set active search to null when closing start panel if project search was active
    if (
      position === "start" &&
      paneCollapsed &&
      getActiveSearch(getState()) === primaryPaneTabs.PROJECT_SEARCH
    ) {
      dispatch(closeActiveSearch());
    }

    dispatch({
      type: "TOGGLE_PANE",
      position,
      paneCollapsed,
    });
  };
}

/**
 * Highlight one or many lines in CodeMirror for a given source.
 *
 * @param {Object} location
 * @param {String} location.sourceId
 *        The precise source to highlight.
 * @param {Number} location.start
 *        The 1-based index of first line to highlight.
 * @param {Number} location.end
 *        The 1-based index of last line to highlight.
 */
export function highlightLineRange(location) {
  return {
    type: "HIGHLIGHT_LINES",
    location,
  };
}

export function flashLineRange(location) {
  return ({ dispatch }) => {
    dispatch(highlightLineRange(location));
    setTimeout(() => dispatch(clearHighlightLineRange()), 200);
  };
}

export function clearHighlightLineRange() {
  return {
    type: "CLEAR_HIGHLIGHT_LINES",
  };
}

export function openConditionalPanel(location, log = false) {
  if (!location) {
    return null;
  }

  return {
    type: "OPEN_CONDITIONAL_PANEL",
    location,
    log,
  };
}

export function closeConditionalPanel() {
  return {
    type: "CLOSE_CONDITIONAL_PANEL",
  };
}

export function updateViewport() {
  const editor = getEditor();
  return {
    type: "SET_VIEWPORT",
    // The viewport locations are set and used for rendering  column breakpoints
    // markers correctly within the viewport.
    // The offsets value represents an allowance of characters or lines offscreen to improve
    // perceived performance of column breakpoint rendering.
    viewport: editor.getLocationsInViewport(100, 20),
  };
}

export function updateCursorPosition(cursorPosition) {
  return { type: "SET_CURSOR_POSITION", cursorPosition };
}

export function setOrientation(orientation) {
  return { type: "SET_ORIENTATION", orientation };
}

export function setSearchOptions(searchKey, searchOptions) {
  return { type: "SET_SEARCH_OPTIONS", searchKey, searchOptions };
}

export function copyToClipboard(location) {
  return ({ getState }) => {
    const content = getSourceTextContent(getState(), location);
    if (content && isFulfilled(content) && content.value.type === "text") {
      copyToTheClipboard(content.value.value);
    }
  };
}

export function setHideOrShowIgnoredSources(shouldHide) {
  return { type: "HIDE_IGNORED_SOURCES", shouldHide };
}

export function toggleSourceMapIgnoreList(shouldEnable) {
  return async thunkArgs => {
    const { dispatch, getState } = thunkArgs;
    const ignoreListSourceUrls = getIgnoreListSourceUrls(getState());
    // Blackbox the source actors on the server
    for (const url of ignoreListSourceUrls) {
      const source = getSourceByURL(getState(), url);
      await blackboxSourceActorsForSource(thunkArgs, source, shouldEnable);
      // Disable breakpoints in sources on the ignore list
      const breakpoints = getBreakpointsForSource(getState(), source);
      await dispatch(toggleBreakpoints(shouldEnable, breakpoints));
    }
    await dispatch({
      type: "ENABLE_SOURCEMAP_IGNORELIST",
      shouldEnable,
    });
  };
}
