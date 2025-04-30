/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");
const {
  getUrlToolTip,
  propertiesEqual,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");
const {
  getFormattedTime,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");
const { truncateString } = require("resource://devtools/shared/string.js");
const {
  MAX_UI_STRING_LENGTH,
} = require("resource://devtools/client/netmonitor/src/constants.js");
const {
  getOverriddenUrl,
} = require("resource://devtools/client/netmonitor/src/selectors/index.js");

const UPDATED_FILE_PROPS = ["urlDetails", "waitingTime"];

class RequestListColumnFile extends Component {
  static get propTypes() {
    return {
      item: PropTypes.object.isRequired,
      slowLimit: PropTypes.number,
      onWaterfallMouseDown: PropTypes.func,
      isOverridden: PropTypes.bool.isRequired,
      overriddenUrl: PropTypes.string,
    };
  }

  shouldComponentUpdate(nextProps) {
    return (
      !propertiesEqual(UPDATED_FILE_PROPS, this.props.item, nextProps.item) ||
      nextProps.overriddenUrl !== this.props.overriddenUrl
    );
  }

  render() {
    const {
      item: { urlDetails, waitingTime },
      isOverridden,
      slowLimit,
      onWaterfallMouseDown,
      overriddenUrl,
    } = this.props;

    const requestedFile = urlDetails.baseNameWithQuery;
    const fileToolTip = getUrlToolTip(urlDetails);

    const isSlow = slowLimit > 0 && !!waitingTime && waitingTime > slowLimit;

    // Build extra content for the title if the request is overridden.
    const overrideTitle = isOverridden ? ` → ${overriddenUrl}` : "";

    return dom.td(
      {
        className: "requests-list-column requests-list-file",
        title:
          truncateString(fileToolTip, MAX_UI_STRING_LENGTH) + overrideTitle,
      },
      dom.div({}, truncateString(requestedFile, MAX_UI_STRING_LENGTH)),
      isSlow &&
        dom.div({
          title: L10N.getFormatStr(
            "netmonitor.audits.slowIconTooltip",
            getFormattedTime(waitingTime),
            getFormattedTime(slowLimit)
          ),
          onMouseDown: onWaterfallMouseDown,
          className: "requests-list-slow-button",
        })
    );
  }
}

module.exports = connect(
  (state, props) => {
    const overriddenUrl = getOverriddenUrl(state, props.item.urlDetails?.url);
    return {
      isOverridden: !!overriddenUrl,
      overriddenUrl,
    };
  },
  {},
  undefined,
  { storeKey: "toolbox-store" }
)(RequestListColumnFile);
