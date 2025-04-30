/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import { PureComponent } from "devtools/client/shared/vendor/react";
import PropTypes from "devtools/client/shared/vendor/react-prop-types";
import {
  toEditorPosition,
  getDocument,
  hasDocument,
  startOperation,
  endOperation,
  getTokenEnd,
} from "../../utils/editor/index";
import { isException } from "../../utils/pause/index";
import { getIndentation } from "../../utils/indentation";
import { connect } from "devtools/client/shared/vendor/react-redux";
import { markerTypes } from "../../constants";
import {
  getVisibleSelectedFrame,
  getPauseReason,
  getSourceTextContent,
  getCurrentThread,
  getViewport,
} from "../../selectors/index";
import { features } from "../../utils/prefs";

export class DebugLine extends PureComponent {
  debugExpression;

  static get propTypes() {
    return {
      editor: PropTypes.object,
      selectedSource: PropTypes.object,
      location: PropTypes.object,
      why: PropTypes.object,
      sourceTextContent: PropTypes.object,
    };
  }

  componentDidMount() {
    this.setDebugLine();
  }

  componentWillUnmount() {
    this.clearDebugLine(this.props);
  }

  componentDidUpdate(prevProps) {
    if (!features.codemirrorNext) {
      startOperation();
    }
    this.clearDebugLine(prevProps);
    this.setDebugLine();
    if (!features.codemirrorNext) {
      endOperation();
    }
  }

  setDebugLine() {
    const { why, location, editor, selectedSource } = this.props;
    if (!location) {
      return;
    }

    if (features.codemirrorNext) {
      if (!selectedSource || location.source.id !== selectedSource.id) {
        return;
      }

      const { lineClass, markTextClass } = this.getTextClasses(why);
      const editorLocation = toEditorPosition(location);

      // Show the paused "caret", to highlight on which particular line **and column** we are paused.
      //
      // Using only a `positionClassName` wouldn't only be applied to the immediate
      // token after the position and force to use ::before to show the paused location.
      // Using ::before prevents using :hover to be able to hide the icon on mouse hovering.
      //
      // So we have to use `createPositionElementNode`, similarly to column breakpoints
      // to have a new dedicated DOM element for the paused location.
      editor.setPositionContentMarker({
        id: markerTypes.PAUSED_LOCATION_MARKER,

        // Ensure displaying the marker after all the other markers and especially the column breakpoint markers
        displayLast: true,

        positions: [editorLocation],
        createPositionElementNode(_line, _column, isFirstNonSpaceColumn) {
          const pausedLocation = document.createElement("span");
          pausedLocation.className = `paused-location${isFirstNonSpaceColumn ? " first-column" : ""}`;

          const bar = document.createElement("span");
          bar.className = `vertical-bar`;
          pausedLocation.appendChild(bar);

          return pausedLocation;
        },
      });

      editor.setLineContentMarker({
        id: markerTypes.DEBUG_LINE_MARKER,
        lineClassName: lineClass,
        lines: [{ line: editorLocation.line }],
      });
      editor.setPositionContentMarker({
        id: markerTypes.DEBUG_POSITION_MARKER,
        positionClassName: markTextClass,
        positions: [editorLocation],
      });
    } else {
      const doc = getDocument(location.source.id);

      let { line, column } = toEditorPosition(location);
      let { markTextClass, lineClass } = this.getTextClasses(why);
      doc.addLineClass(line, "wrap", lineClass);

      const lineText = doc.getLine(line);
      column = Math.max(column, getIndentation(lineText));

      // If component updates because user clicks on
      // another source tab, codeMirror will be null.
      const columnEnd = doc.cm ? getTokenEnd(doc.cm, line, column) : null;

      if (columnEnd === null) {
        markTextClass += " to-line-end";
      }

      this.debugExpression = doc.markText(
        { ch: column, line },
        { ch: columnEnd, line },
        { className: markTextClass }
      );
    }
  }

  clearDebugLine(otherProps = {}) {
    if (features.codemirrorNext) {
      const { location, editor, selectedSource } = this.props;
      // Remove the debug line marker when no longer paused, or the selected source
      // is no longer the source where the pause occured.
      if (
        !location ||
        location.source.id !== selectedSource.id ||
        otherProps?.location !== location ||
        otherProps?.selectedSource?.id !== selectedSource.id
      ) {
        editor.removeLineContentMarker(markerTypes.DEBUG_LINE_MARKER);
        editor.removePositionContentMarker(markerTypes.DEBUG_POSITION_MARKER);
        editor.removePositionContentMarker(markerTypes.PAUSED_LOCATION_MARKER);
      }
    } else {
      const { why, location } = otherProps;
      // Avoid clearing the line if we didn't set a debug line before,
      // or, if the document is no longer available
      if (!location || !hasDocument(location.source.id)) {
        return;
      }

      if (this.debugExpression) {
        this.debugExpression.clear();
      }

      const { line } = toEditorPosition(location);
      const doc = getDocument(location.source.id);
      const { lineClass } = this.getTextClasses(why);
      doc.removeLineClass(line, "wrap", lineClass);
    }
  }

  getTextClasses(why) {
    if (why && isException(why)) {
      return {
        markTextClass: "debug-expression-error",
        lineClass: "new-debug-line-error",
      };
    }

    // In CM6, we no longer highlight the next token via debug-expression
    // and only highlight the line via paused-line.
    return {
      markTextClass: features.codemirrorNext ? null : "debug-expression",
      lineClass: "paused-line",
    };
  }

  render() {
    return null;
  }
}

function isDocumentReady(location, sourceTextContent) {
  const contentAvailable = location && sourceTextContent;
  // With CM6, the codemirror document is no longer cached
  // so no need to check if its available
  if (features.codemirrorNext) {
    return contentAvailable;
  }
  return contentAvailable && hasDocument(location.source.id);
}

const mapStateToProps = state => {
  // Avoid unecessary intermediate updates when there is no location
  // or the source text content isn't yet fully loaded
  const frame = getVisibleSelectedFrame(state);
  const location = frame?.location;
  if (!location) {
    return {};
  }
  // For CM6, also check if we have a valid viewport.
  // This is a way to know if the actual source is displayed
  // and we are no longer on the "loading..." message
  if (features.codemirrorNext) {
    const viewport = getViewport(state);
    if (!viewport) {
      return {};
    }
  }
  const sourceTextContent = getSourceTextContent(state, location);
  if (!isDocumentReady(location, sourceTextContent)) {
    return {};
  }
  return {
    location,
    why: getPauseReason(state, getCurrentThread(state)),
    sourceTextContent,
  };
};

export default connect(mapStateToProps)(DebugLine);
