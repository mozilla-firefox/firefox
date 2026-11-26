/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const {
  getAllUi,
} = require("resource://devtools/client/webconsole/selectors/ui.js");

const {
  ORIGINAL_VARIABLE_MAPPING,
} = require("resource://devtools/client/webconsole/constants.js");

loader.lazyRequireGetter(
  this,
  "PropTypes",
  "resource://devtools/client/shared/vendor/react-prop-types.js"
);

const l10n = require("resource://devtools/shared/webconsole/l10n.js");

/**
 * Show the results of evaluating the current terminal text, if possible.
 */
class EvaluationNotification extends Component {
  static get propTypes() {
    return {
      notification: PropTypes.string,
    };
  }

  render() {
    const { notification } = this.props;
    if (notification == ORIGINAL_VARIABLE_MAPPING) {
      return dom.span(
        { className: "evaluation-notification warning" },
        dom.span({ className: "evaluation-notification__icon" }),
        dom.span(
          { className: "evaluation-notification__text" },
          l10n.getStr(
            "webconsole-evaluation-notification-no-original-variable-mapping"
          )
        )
      );
    }
    return null;
  }
}

function mapStateToProps(state) {
  return {
    notification: getAllUi(state).notification,
  };
}

module.exports = connect(mapStateToProps, null)(EvaluationNotification);
