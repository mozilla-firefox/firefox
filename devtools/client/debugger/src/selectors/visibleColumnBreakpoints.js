/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import { createSelector } from "devtools/client/shared/vendor/reselect";

import {
  getViewport,
  getSelectedSource,
  getSelectedSourceTextContent,
  getBreakpointPositionsForSource,
} from "./index";
import { getVisibleBreakpoints } from "./visibleBreakpoints";
import { getSelectedLocation } from "../utils/selected-location";
import { sortSelectedLocations } from "../utils/location";
import { getLineText } from "../utils/source";
import { features } from "../utils/prefs";

function contains(location, range) {
  if (features.codemirrorNext) {
    // If the location is within the viewport lines or if the location is on the first or last line
    // and the columns are within the start or end line content.
    return (
      (location.line > range.start.line && location.line < range.end.line) ||
      (location.line == range.start.line &&
        location.column >= range.start.column) ||
      (location.line == range.end.line && location.column <= range.end.column)
    );
  }
  return (
    location.line >= range.start.line &&
    location.line <= range.end.line &&
    (!location.column ||
      (location.column >= range.start.column &&
        location.column <= range.end.column))
  );
}

function convertToList(breakpointPositions) {
  return [].concat(...Object.values(breakpointPositions));
}

/**
 * Retrieve the list of column breakpoints to be displayed.
 * This ignores lines without any breakpoint, but also lines with a single possible breakpoint.
 * So that we only return breakpoints where there is at least two possible breakpoint on a given line.
 * Also, this only consider lines currently visible in CodeMirror editor.
 *
 * This method returns an array whose elements are objects having two attributes:
 *  - breakpoint: A breakpoint object refering to a precise column location
 *  - location: The location object in an active source where the breakpoint location matched.
 *              This location may be the generated or original source based on the currently selected source type.
 *
 * See `visibleColumnBreakpoints()` for the definition of arguments.
 */
export function getColumnBreakpoints(
  positions,
  breakpoints,
  viewport,
  selectedSource,
  selectedSourceTextContent
) {
  if (!positions || !selectedSource || !breakpoints.length || !viewport) {
    return [];
  }

  const breakpointsPerLine = new Map();
  for (const breakpoint of breakpoints) {
    if (breakpoint.options.hidden) {
      continue;
    }
    const location = getSelectedLocation(breakpoint, selectedSource);
    const { line } = location;
    let breakpointsPerColumn = breakpointsPerLine.get(line);
    if (!breakpointsPerColumn) {
      breakpointsPerColumn = new Map();
      breakpointsPerLine.set(line, breakpointsPerColumn);
    }
    breakpointsPerColumn.set(location.column, breakpoint);
  }

  const columnBreakpoints = [];
  for (const keyLine in positions) {
    const positionsPerLine = positions[keyLine];
    // Only consider positions where there is more than one breakable position per line.
    // When there is only one breakpoint, this isn't a column breakpoint.
    if (positionsPerLine.length <= 1) {
      continue;
    }
    for (const breakpointPosition of positionsPerLine) {
      // For minified sources we want to limit the amount of displayed column breakpoints
      // This is nice to have for perf reasons
      if (columnBreakpoints.length >= 100) {
        continue;
      }

      const location = getSelectedLocation(breakpointPosition, selectedSource);
      // Only consider positions visible in the current CodeMirror viewport
      if (!contains(location, viewport)) {
        continue;
      }

      const { line } = location;
      // Ignore any further computation if there is no breakpoint on that line.
      const breakpointsPerColumn = breakpointsPerLine.get(line);
      if (!breakpointsPerColumn) {
        continue;
      }

      // Filters out breakpoints to the right of the line. (bug 1552039)
      // XXX Not really clear why we get such positions??
      const { column } = location;
      if (column) {
        const lineText = getLineText(
          selectedSource.id,
          selectedSourceTextContent,
          line
        );
        if (column > lineText.length) {
          continue;
        }
      }

      // Finally, return the expected format output for this selector.
      // Location of each column breakpoint + a reference to the breakpoint object (if one is set on that column, it can be null).
      const breakpoint = breakpointsPerColumn.get(column);

      columnBreakpoints.push({
        location,
        breakpoint,
      });
    }
  }

  return columnBreakpoints;
}

function getVisibleBreakpointPositions(state) {
  const source = getSelectedSource(state);
  if (!source) {
    return null;
  }
  return getBreakpointPositionsForSource(state, source.id);
}

export const visibleColumnBreakpoints = createSelector(
  getVisibleBreakpointPositions,
  getVisibleBreakpoints,
  getViewport,
  getSelectedSource,
  getSelectedSourceTextContent,
  getColumnBreakpoints
);

export function getFirstBreakpointPosition(state, location) {
  const positions = getBreakpointPositionsForSource(state, location.source.id);
  if (!positions) {
    return null;
  }

  return sortSelectedLocations(convertToList(positions), location.source).find(
    position =>
      getSelectedLocation(position, location.source).line == location.line
  );
}
