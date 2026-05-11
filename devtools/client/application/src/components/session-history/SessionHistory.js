/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createElement,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  PropTypes,
} = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  button,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const FluentReact = require("resource://devtools/client/shared/vendor/fluent-react.js");
const Localized = createFactory(FluentReact.Localized);

const spacerCell = { "aria-hidden": true };

function EntryInfo({ fields, id }) {
  return createElement(
    "div",
    { id, popover: "auto" },
    createElement(
      "dl",
      {},
      ...Object.entries(fields).flatMap(([key, value]) => [
        createElement("dt", {}, key),
        createElement(
          "dd",
          {},
          value instanceof Array
            ? createElement("ul", {
                children: value.map(child =>
                  createElement("li", {}, `${child}`)
                ),
              })
            : `${value}`
        ),
      ])
    )
  );
}

EntryInfo.propTypes = {
  fields: PropTypes.object.isRequired,
  id: PropTypes.string.isRequired,
};

function SessionHistoryDiagram({ current, diagrams, entriesByKey }) {
  const cols = [];
  // We're going to use <col> and <colgroup> to have a flat table. We do this
  // instead of nested tables to make sure that this integrates with a11y.
  for (let i = 0; i < diagrams.length; i++) {
    const { start, end } = diagrams[i];
    cols.push(createElement("col", { span: end - start }));
    if (i < diagrams.length - 1) {
      cols.push(createElement("col", { className: "diagram-spacer" }));
    }
  }

  const headerCells = [];
  for (let i = 0; i < diagrams.length; i++) {
    const { start, end } = diagrams[i];
    for (let index = start; index < end; index++) {
      const props = index == current ? { id: "current" } : {};
      headerCells.push(createElement("th", props, index));
    }
    if (i < diagrams.length - 1) {
      headerCells.push(createElement("th", spacerCell));
    }
  }

  const maxRows = Math.max(...diagrams.map(({ rows }) => rows.length));
  let ctr = 0;
  const bodyRows = [];
  const diagramDone = new Array(diagrams.length).fill(false);
  for (let rowIndex = 0; rowIndex < maxRows; rowIndex++) {
    const cells = [];
    for (let diagramIndex = 0; diagramIndex < diagrams.length; diagramIndex++) {
      if (diagramIndex > 0 && rowIndex === 0) {
        cells.push(createElement("td", { ...spacerCell, rowSpan: maxRows }));
      }
      if (diagramDone[diagramIndex]) {
        continue;
      }
      const { rows, start, end } = diagrams[diagramIndex];
      if (rowIndex < rows.length) {
        for (const { age, key, sameDocNav } of rows[rowIndex]) {
          if (key) {
            const id = `entry-info-container-${start}-${ctr++}`;
            const className = sameDocNav ? "same-document-nav" : "";
            const url = URL.parse(entriesByKey[key].url);
            cells.push(
              createElement(
                "td",
                { colSpan: age, className },
                Localized(
                  {
                    id: "session-history-entry-info-button-title",
                    attrs: { title: true },
                  },
                  button({ popovertarget: id }, `${url.pathname}${url.search}`)
                ),
                createElement(EntryInfo, { fields: entriesByKey[key], id })
              )
            );
          } else {
            cells.push(createElement("td", { colSpan: age }));
          }
        }
      } else {
        // In the case where we have column (or columns) that lack entries we
        // want the column to not have borders between rows, but instead have
        // the entire column be empty. This might e.g. happen when we add an
        // iframe dynamically, creating a diagram that'd look like:
        //
        // +-----+-----+-----+-----+
        // | top | top | top | top |
        // +-----+-----+-----+-----+
        // |     |     | a   | a#1 |
        // +     +     +-----+-----+
        // |     |     | b         |
        // +-----+-----+-----+-----+
        cells.push(
          createElement("td", {
            colSpan: end - start,
            rowSpan: maxRows - rowIndex,
            className: "diagram-padding",
          })
        );
        diagramDone[diagramIndex] = true;
      }
    }
    bodyRows.push(createElement("tr", {}, ...cells));
  }

  return createElement(
    "table",
    { id: "diagram-container-table" },
    createElement("colgroup", {}, ...cols),
    createElement("thead", {}, createElement("tr", {}, ...headerCells)),
    createElement("tbody", {}, ...bodyRows)
  );
}

SessionHistoryDiagram.propTypes = {
  current: PropTypes.number.isRequired,
  diagrams: PropTypes.arrayOf(PropTypes.object).isRequired,
  entriesByKey: PropTypes.object.isRequired,
};

// Exports
module.exports = SessionHistoryDiagram;
