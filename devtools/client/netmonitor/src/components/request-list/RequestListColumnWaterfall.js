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
  L10N,
} = require("resource://devtools/client/netmonitor/src/utils/l10n.js");
const {
  fetchNetworkUpdatePacket,
  propertiesEqual,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");

// List of properties of the timing info we want to create boxes for
const {
  TIMING_KEYS,
} = require("resource://devtools/client/netmonitor/src/constants.js");

const { div } = dom;

const UPDATED_WATERFALL_ITEM_PROPS = ["eventTimings", "totalTime"];
const UPDATED_WATERFALL_PROPS = [
  "item",
  "firstRequestStartedMs",
  "waterfallScale",
  "isVisible",
];

module.exports = class RequestListColumnWaterfall extends Component {
  static get propTypes() {
    return {
      connector: PropTypes.object.isRequired,
      firstRequestStartedMs: PropTypes.number.isRequired,
      item: PropTypes.object.isRequired,
      onWaterfallMouseDown: PropTypes.func.isRequired,
      waterfallScale: PropTypes.number,
      isVisible: PropTypes.bool.isRequired,
    };
  }

  constructor() {
    super();
    this.handleMouseOver = this.handleMouseOver.bind(this);
  }

  componentDidMount() {
    const { connector, item } = this.props;
    fetchNetworkUpdatePacket(connector.requestData, item, ["eventTimings"]);
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    if (nextProps.isVisible && typeof nextProps.item.totalTime === "number") {
      const { connector, item } = nextProps;
      fetchNetworkUpdatePacket(connector.requestData, item, ["eventTimings"]);
    }
  }

  shouldComponentUpdate(nextProps) {
    return (
      nextProps.isVisible &&
      (!propertiesEqual(UPDATED_WATERFALL_PROPS, this.props, nextProps) ||
        !propertiesEqual(
          UPDATED_WATERFALL_ITEM_PROPS,
          this.props.item,
          nextProps.item
        ))
    );
  }

  handleMouseOver({ target }) {
    if (!target.title) {
      target.title = this.timingTooltip();
    }
  }

  timingTooltip() {
    const { eventTimings, totalTime } = this.props.item;
    const tooltip = [];

    if (eventTimings) {
      for (const key of TIMING_KEYS) {
        const width = eventTimings.timings[key];

        if (width > 0) {
          tooltip.push(
            L10N.getFormatStr("netmonitor.waterfall.tooltip." + key, width)
          );
        }
      }
    }

    if (typeof totalTime === "number") {
      tooltip.push(
        L10N.getFormatStr("netmonitor.waterfall.tooltip.total", totalTime)
      );
    }

    const lf = new Intl.ListFormat(undefined, { type: "unit" });
    return lf.format(tooltip);
  }

  timingBoxes() {
    const {
      waterfallScale,
      item: { eventTimings, totalTime },
    } = this.props;
    const boxes = [];

    // Physical pixel as minimum size
    const minPixel = 1 / window.devicePixelRatio;

    if (typeof totalTime === "number") {
      if (eventTimings) {
        // Add a set of boxes representing timing information.
        for (const key of TIMING_KEYS) {
          if (eventTimings.timings[key] > 0) {
            boxes.push(
              div({
                key,
                className: `requests-list-timings-box ${key}`,
                style: {
                  width: Math.max(
                    eventTimings.timings[key] * waterfallScale,
                    minPixel
                  ),
                },
              })
            );
          }
        }
      }
      // Minimal box to at least show start and total time
      if (!boxes.length) {
        boxes.push(
          div({
            className: "requests-list-timings-box filler",
            key: "filler",
            style: { width: Math.max(totalTime * waterfallScale, minPixel) },
          })
        );
      }

      const title = L10N.getFormatStr("networkMenu.totalMS2", totalTime);
      boxes.push(
        div(
          {
            key: "total",
            className: "requests-list-timings-total",
            title,
          },
          title
        )
      );
    } else {
      // Pending requests are marked for start time
      boxes.push(
        div({
          className: "requests-list-timings-box filler",
          key: "pending",
          style: { width: minPixel },
        })
      );
    }

    return boxes;
  }

  render() {
    const {
      firstRequestStartedMs,
      item: { startedMs },
      waterfallScale,
      onWaterfallMouseDown,
    } = this.props;

    return dom.td(
      {
        className: "requests-list-column requests-list-waterfall",
        onMouseOver: this.handleMouseOver,
      },
      div(
        {
          className: "requests-list-timings",
          style: {
            paddingInlineStart: `${
              (startedMs - firstRequestStartedMs) * waterfallScale
            }px`,
          },
          onMouseDown: onWaterfallMouseDown,
        },
        this.timingBoxes()
      )
    );
  }
};
