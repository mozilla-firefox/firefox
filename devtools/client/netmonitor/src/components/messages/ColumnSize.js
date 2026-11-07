/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  getFormattedSize,
} = require("resource://devtools/client/netmonitor/src/utils/format-utils.js");

/**
 * Renders the "Size" column of a message.
 */
class ColumnSize extends Component {
  static get propTypes() {
    return {
      item: PropTypes.object.isRequired,
    };
  }

  shouldComponentUpdate(nextProps) {
    return (
      this.props.item.payload !== nextProps.item.payload ||
      this.props.item.payloadSize !== nextProps.item.payloadSize
    );
  }

  render() {
    const { payload, payloadSize } = this.props.item;
    const size = payloadSize ?? payload.length;

    return dom.td(
      {
        className: "message-list-column message-list-size",
        title: getFormattedSize(size),
      },
      getFormattedSize(size)
    );
  }
}

module.exports = ColumnSize;
