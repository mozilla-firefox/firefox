/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  createElement,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const {
  section,
} = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const SessionHistoryDiagram = require("resource://devtools/client/application/src/components/session-history/SessionHistory.js");
const SessionHistoryUnavailable = require("resource://devtools/client/application/src/components/session-history/SessionHistoryUnavailable.js");

class SessionHistoryPage extends PureComponent {
  static get propTypes() {
    return {
      current: PropTypes.number.isRequired,
      diagrams: PropTypes.arrayOf(PropTypes.object).isRequired,
      entriesByKey: PropTypes.object.isRequired,
      disabled: PropTypes.bool,
    };
  }

  render() {
    const { current, diagrams, entriesByKey, disabled } = this.props;
    return section(
      {
        className: `app-page js-session-history-page ${
          disabled ? "app-page--empty" : ""
        }`,
      },
      disabled
        ? createElement(SessionHistoryUnavailable, {})
        : createElement(SessionHistoryDiagram, {
            current,
            diagrams,
            entriesByKey,
          })
    );
  }
}

function mapStateToProps(state) {
  return { ...state.sessionHistory };
}

// Exports
module.exports = connect(mapStateToProps)(SessionHistoryPage);
