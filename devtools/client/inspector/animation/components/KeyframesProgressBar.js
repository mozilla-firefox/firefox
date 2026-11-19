/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createRef,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

class KeyframesProgressBar extends PureComponent {
  static get propTypes() {
    return {
      addAnimationsCurrentTimeListener: PropTypes.func.isRequired,
      animation: PropTypes.object.isRequired,
      getAnimationsCurrentTime: PropTypes.func.isRequired,
      removeAnimationsCurrentTimeListener: PropTypes.func.isRequired,
      simulateAnimationForKeyframesProgressBar: PropTypes.func.isRequired,
      timeScale: PropTypes.object.isRequired,
    };
  }

  constructor(props) {
    super(props);

    this._progressBarRef = createRef();

    this.onCurrentTimeUpdated = this.onCurrentTimeUpdated.bind(this);
  }

  componentDidMount() {
    const { addAnimationsCurrentTimeListener } = this.props;

    this.setupAnimation(this.props);
    addAnimationsCurrentTimeListener(this.onCurrentTimeUpdated);
  }

  // FIXME: https://bugzilla.mozilla.org/show_bug.cgi?id=1774507
  UNSAFE_componentWillReceiveProps(nextProps) {
    const { animation, getAnimationsCurrentTime, timeScale } = nextProps;

    this.setupAnimation(nextProps);
    this.updateOffset(getAnimationsCurrentTime(), animation, timeScale);
  }

  componentWillUnmount() {
    const { removeAnimationsCurrentTimeListener } = this.props;

    removeAnimationsCurrentTimeListener(this.onCurrentTimeUpdated);
    this.simulatedAnimation = null;
  }

  onCurrentTimeUpdated(currentTime) {
    const { animation, timeScale } = this.props;
    this.updateOffset(currentTime, animation, timeScale);
  }

  updateOffset(currentTime, animation, timeScale) {
    const { createdTime } = animation.state;
    const { playbackRate } = animation.state.absoluteValues;

    const time =
      (timeScale.minStartTime + currentTime - createdTime) * playbackRate;

    if (isNaN(time)) {
      // Setting an invalid currentTime will throw so bail out if time is not a number for
      // any reason.
      return;
    }

    this.simulatedAnimation.currentTime = time;
    const position =
      this.simulatedAnimation.effect.getComputedTiming().progress;

    // onCurrentTimeUpdated is bound to requestAnimationFrame.
    // As to update the component too frequently has performance issue if React controlled,
    // update raw component directly. See Bug 1699039.
    this._progressBarRef.current.style.marginInlineStart = `${position * 100}%`;
  }

  setupAnimation(props) {
    const { animation, simulateAnimationForKeyframesProgressBar } = props;

    if (this.simulatedAnimation) {
      this.simulatedAnimation.cancel();
    }

    const timing = Object.assign({}, animation.state, {
      iterations: animation.state.iterationCount || Infinity,
      duration:
        animation.state.duration / animation.state.playBackRateMultiplier,
    });

    this.simulatedAnimation = simulateAnimationForKeyframesProgressBar(timing);
  }

  render() {
    return dom.div(
      { className: "keyframes-progress-bar-area" },
      dom.div({
        className: "indication-bar keyframes-progress-bar",
        ref: this._progressBarRef,
      })
    );
  }
}

module.exports = KeyframesProgressBar;
