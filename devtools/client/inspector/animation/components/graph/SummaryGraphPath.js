/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const ReactDOM = require("resource://devtools/client/shared/vendor/react-dom.mjs");

const ComputedTimingPath = createFactory(
  require("resource://devtools/client/inspector/animation/components/graph/ComputedTimingPath.js")
);
const EffectTimingPath = createFactory(
  require("resource://devtools/client/inspector/animation/components/graph/EffectTimingPath.js")
);
const NegativeDelayPath = createFactory(
  require("resource://devtools/client/inspector/animation/components/graph/NegativeDelayPath.js")
);
const NegativeEndDelayPath = createFactory(
  require("resource://devtools/client/inspector/animation/components/graph/NegativeEndDelayPath.js")
);
const {
  DEFAULT_GRAPH_HEIGHT,
} = require("resource://devtools/client/inspector/animation/utils/graph-helper.js");

// Minimum opacity for semitransparent fill color for keyframes's easing graph.
const MIN_KEYFRAMES_EASING_OPACITY = 0.5;

class SummaryGraphPath extends Component {
  static get propTypes() {
    return {
      animation: PropTypes.object.isRequired,
      getAnimatedPropertyMap: PropTypes.object.isRequired,
      simulateAnimation: PropTypes.func.isRequired,
      timeScale: PropTypes.object.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      // Duration which can display in one pixel.
      durationPerPixel: 0,
      // To avoid rendering while the state is updating
      // since we call an async function in updateState.
      isStateUpdating: false,
      // List of keyframe which consists by only offset and easing.
      keyframesList: [],
    };
  }

  componentDidMount() {
    // No need to set isStateUpdating state since paint sequence is finish here.
    this.updateState(this.props);
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    this.setState({ isStateUpdating: true });
    this.updateState(nextProps);
  }

  shouldComponentUpdate(nextProps, nextState) {
    return !nextState.isStateUpdating;
  }

  /**
   * Return animatable keyframes list which has only offset and easing.
   * Also, this method remove duplicate keyframes.
   * For example, if the given animatedPropertyMap is,
   * [
   *   {
   *     key: "color",
   *     values: [
   *       {
   *         offset: 0,
   *         easing: "ease",
   *         value: "rgb(255, 0, 0)",
   *       },
   *       {
   *         offset: 1,
   *         value: "rgb(0, 255, 0)",
   *       },
   *     ],
   *   },
   *   {
   *     key: "opacity",
   *     values: [
   *       {
   *         offset: 0,
   *         easing: "ease",
   *         value: 0,
   *       },
   *       {
   *         offset: 1,
   *         value: 1,
   *       },
   *     ],
   *   },
   * ]
   *
   * then this method returns,
   * [
   *   [
   *     {
   *       offset: 0,
   *       easing: "ease",
   *     },
   *     {
   *       offset: 1,
   *     },
   *   ],
   * ]
   *
   * @param {Map} animated property map
   *        which can get form getAnimatedPropertyMap in animation.js
   * @return {Array} list of keyframes which has only easing and offset.
   */
  getOffsetAndEasingOnlyKeyframes(animatedPropertyMap) {
    return [...animatedPropertyMap.values()]
      .filter((keyframes1, i, self) => {
        return (
          i !==
          self.findIndex((keyframes2, j) => {
            return this.isOffsetAndEasingKeyframesEqual(keyframes1, keyframes2)
              ? j
              : -1;
          })
        );
      })
      .map(keyframes => {
        return keyframes.map(keyframe => {
          return { easing: keyframe.easing, offset: keyframe.offset };
        });
      });
  }

  /**
   * Return true if given keyframes have same length, offset and easing.
   *
   * @param {Array} keyframes1
   * @param {Array} keyframes2
   * @return {boolean} true: equals
   */
  isOffsetAndEasingKeyframesEqual(keyframes1, keyframes2) {
    if (keyframes1.length !== keyframes2.length) {
      return false;
    }

    for (let i = 0; i < keyframes1.length; i++) {
      const keyframe1 = keyframes1[i];
      const keyframe2 = keyframes2[i];

      if (
        keyframe1.offset !== keyframe2.offset ||
        keyframe1.easing !== keyframe2.easing
      ) {
        return false;
      }
    }

    return true;
  }

  updateState(props) {
    const { animation, getAnimatedPropertyMap, timeScale } = props;

    let animatedPropertyMap = null;
    let thisEl = null;

    try {
      animatedPropertyMap = getAnimatedPropertyMap(animation);
      thisEl = ReactDOM.findDOMNode(this);
    } catch (e) {
      // Expected if we've already been destroyed or other node have been selected
      // in the meantime.
      console.error(e);
      return;
    }

    const keyframesList =
      this.getOffsetAndEasingOnlyKeyframes(animatedPropertyMap);
    const totalDuration =
      timeScale.getDuration() *
      Math.abs(animation.state.absoluteValues.playbackRate);
    const durationPerPixel = totalDuration / thisEl.parentNode.clientWidth;

    this.setState({
      durationPerPixel,
      isStateUpdating: false,
      keyframesList,
    });
  }

  render() {
    const { durationPerPixel, keyframesList } = this.state;
    const { animation, simulateAnimation, timeScale } = this.props;

    if (!durationPerPixel || !animation.state.type) {
      // Undefined animation.state.type means that the animation had been removed already.
      // Even if the animation was removed, we still need the empty svg since the
      // component might be re-used.
      return dom.svg({
        width: "0",
        height: "0",
      });
    }

    const { playBackRateMultiplier } = animation.state;
    const { createdTime, playbackRate } = animation.state.absoluteValues;
    const multiplier = Math.abs(playbackRate) * playBackRateMultiplier;

    // Absorb the playbackRate in viewBox of SVG and offset of child path elements
    // in order to each graph path components can draw without considering to the
    // playbackRate.
    const offset = createdTime * multiplier;
    const startTime = timeScale.minStartTime * multiplier;
    const totalDuration = timeScale.getDuration() * multiplier;
    const opacity = Math.max(
      1 / keyframesList.length,
      MIN_KEYFRAMES_EASING_OPACITY
    );

    return dom.svg(
      {
        className: "animation-summary-graph-path",
        preserveAspectRatio: "none",
        viewBox:
          `${startTime} -${DEFAULT_GRAPH_HEIGHT} ` +
          `${totalDuration} ${DEFAULT_GRAPH_HEIGHT}`,
      },
      keyframesList.map(keyframes =>
        ComputedTimingPath({
          animation,
          durationPerPixel,
          keyframes,
          offset,
          opacity,
          simulateAnimation,
          totalDuration,
        })
      ),
      animation.state.easing !== "linear"
        ? EffectTimingPath({
            animation,
            durationPerPixel,
            offset,
            simulateAnimation,
            totalDuration,
          })
        : null,
      animation.state.delay < 0
        ? keyframesList.map(keyframes => {
            return NegativeDelayPath({
              animation,
              durationPerPixel,
              keyframes,
              offset,
              simulateAnimation,
              totalDuration,
            });
          })
        : null,
      animation.state.iterationCount && animation.state.endDelay < 0
        ? keyframesList.map(keyframes => {
            return NegativeEndDelayPath({
              animation,
              durationPerPixel,
              keyframes,
              offset,
              simulateAnimation,
              totalDuration,
            });
          })
        : null
    );
  }
}

module.exports = SummaryGraphPath;
