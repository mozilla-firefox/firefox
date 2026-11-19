/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const {
  createSummaryGraphPathStringFunction,
  SummaryGraphHelper,
} = require("resource://devtools/client/inspector/animation/utils/graph-helper.js");
const TimingPath = require("resource://devtools/client/inspector/animation/components/graph/TimingPath.js");

class EffectTimingPath extends TimingPath {
  static get propTypes() {
    return {
      animation: PropTypes.object.isRequired,
      durationPerPixel: PropTypes.number.isRequired,
      offset: PropTypes.number.isRequired,
      simulateAnimation: PropTypes.func.isRequired,
      totalDuration: PropTypes.number.isRequired,
    };
  }

  render() {
    const {
      animation,
      durationPerPixel,
      offset,
      simulateAnimation,
      totalDuration,
    } = this.props;

    const { state } = animation;
    const effectTiming = Object.assign({}, state, {
      iterations: state.iterationCount ? state.iterationCount : Infinity,
    });

    const simulatedAnimation = simulateAnimation(null, effectTiming, false);

    if (!simulatedAnimation) {
      return null;
    }

    const endTime = simulatedAnimation.effect.getComputedTiming().endTime;

    const getValueFunc = time => {
      if (time < 0) {
        return 0;
      }

      simulatedAnimation.currentTime = time < endTime ? time : endTime;
      return Math.max(
        simulatedAnimation.effect.getComputedTiming().progress,
        0
      );
    };

    const toPathStringFunc = createSummaryGraphPathStringFunction(
      endTime,
      state.absoluteValues.playbackRate
    );
    const helper = new SummaryGraphHelper(
      state,
      null,
      totalDuration,
      durationPerPixel,
      getValueFunc,
      toPathStringFunc
    );

    return dom.g(
      {
        className: "animation-effect-timing-path",
        transform: `translate(${offset})`,
      },
      super.renderGraph(state, helper)
    );
  }
}

module.exports = EffectTimingPath;
