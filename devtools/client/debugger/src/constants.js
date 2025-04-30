/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

export const searchKeys = {
  PROJECT_SEARCH: "project-search",
  FILE_SEARCH: "file-search",
  QUICKOPEN_SEARCH: "quickopen-search",
};

export const primaryPaneTabs = {
  SOURCES: "sources",
  OUTLINE: "outline",
  PROJECT_SEARCH: "project",
  TRACER: "tracer",
};

export const markerTypes = {
  /* Line Markers */
  CONDITIONAL_BP_MARKER: "conditional-breakpoint-panel-marker",
  DEBUG_LINE_MARKER: "debug-line-marker",
  LINE_EXCEPTION_MARKER: "line-exception-marker",
  HIGHLIGHT_LINE_MARKER: "highlight-line-marker",
  MULTI_HIGHLIGHT_LINE_MARKER: "multi-highlight-line-marker",
  BLACKBOX_LINE_MARKER: "blackbox-line-marker",
  INLINE_PREVIEW_MARKER: "inline-preview-marker",
  /* Position Markers */
  COLUMN_BREAKPOINT_MARKER: "column-breakpoint-marker",
  DEBUG_POSITION_MARKER: "debug-position-marker",
  EXCEPTION_POSITION_MARKER: "exception-position-marker",
  ACTIVE_SELECTION_MARKER: "active-selection-marker",
  PAUSED_LOCATION_MARKER: "paused-location-marker",
  /* Gutter Markers */
  EMPTY_LINE_MARKER: "empty-line-marker",
  BLACKBOX_LINE_GUTTER_MARKER: "blackbox-line-gutter-marker",
  GUTTER_BREAKPOINT_MARKER: "gutter-breakpoint-marker",
};
