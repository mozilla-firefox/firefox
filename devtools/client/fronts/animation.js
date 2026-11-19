/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  FrontClassWithSpec,
  registerFront,
} = require("resource://devtools/shared/protocol.js");
const {
  animationSpec,
  animationsSpec,
} = require("resource://devtools/shared/specs/animation.js");

class AnimationFront extends FrontClassWithSpec(animationSpec) {
  constructor(conn, targetFront, parentFront) {
    super(conn, targetFront, parentFront);

    this.state = {};
    this.before("changed", this.onChanged.bind(this));
  }

  form(form) {
    this._form = form;
    this.state = this.initialState;
  }

  /**
   * If the AnimationsActor was given a reference to the WalkerActor previously
   * then calling this getter will return the animation target NodeFront.
   */
  get animationTargetNodeFront() {
    if (!this._form.animationTargetNodeActorID) {
      return null;
    }

    return this.conn.getFrontByID(this._form.animationTargetNodeActorID);
  }

  /**
   * Getter for the initial state of the player. Up to date states can be
   * retrieved by calling the getCurrentState method.
   */
  get initialState() {
    return {
      type: this._form.type,
      startTime: this._form.startTime,
      currentTime: this._form.currentTime,
      playState: this._form.playState,
      playbackRate: this._form.playbackRate,
      playBackRateMultiplier: this._form.playBackRateMultiplier,
      name: this._form.name,
      duration: this._form.duration,
      delay: this._form.delay,
      endDelay: this._form.endDelay,
      iterationCount: this._form.iterationCount,
      iterationStart: this._form.iterationStart,
      easing: this._form.easing,
      fill: this._form.fill,
      direction: this._form.direction,
      animationTimingFunction: this._form.animationTimingFunction,
      isRunningOnCompositor: this._form.isRunningOnCompositor,
      propertyState: this._form.propertyState,
      documentCurrentTime: this._form.documentCurrentTime,
      createdTime: this._form.createdTime,
      currentTimeAtCreated: this._form.currentTimeAtCreated,
      absoluteValues: this.calculateAbsoluteValues(this._form),
      properties: this._form.properties,
    };
  }

  /**
   * Executed when the AnimationActor emits a "changed" event. Used to
   * update the local knowledge of the state.
   */
  onChanged(partialState) {
    const { state } = this.reconstructState(partialState);
    this.state = state;
  }

  /**
   * Refresh the current state of this animation on the client from information
   * found on the server. Doesn't return anything, just stores the new state.
   */
  async refreshState() {
    const data = await this.getCurrentState();
    if (this.currentStateHasChanged) {
      this.state = data;
    }
  }

  /**
   * getCurrentState interceptor re-constructs incomplete states since the actor
   * only sends the values that have changed.
   */
  getCurrentState() {
    this.currentStateHasChanged = false;
    return super.getCurrentState().then(partialData => {
      const { state, hasChanged } = this.reconstructState(partialData);
      this.currentStateHasChanged = hasChanged;
      return state;
    });
  }

  reconstructState(data) {
    let hasChanged = false;

    for (const key in this.state) {
      if (typeof data[key] === "undefined") {
        data[key] = this.state[key];
      } else if (data[key] !== this.state[key]) {
        hasChanged = true;
      }
    }

    data.absoluteValues = this.calculateAbsoluteValues(data);
    return { state: data, hasChanged };
  }

  calculateAbsoluteValues(data) {
    const {
      createdTime,
      currentTime,
      currentTimeAtCreated,
      delay,
      duration,
      endDelay = 0,
      fill,
      iterationCount,
      playbackRate,
      playBackRateMultiplier = 1,
    } = data;

    // `playbackRate` can be set to 0, which means toRate would return +Infinity
    // and break the panel. Fallback to 1. See bug 2094186.
    const layoutPlaybackRate = playbackRate || 1;
    const multiplier = layoutPlaybackRate * playBackRateMultiplier;

    const toRate = v => v / Math.abs(multiplier);
    const isPositivePlaybackRate = layoutPlaybackRate > 0;
    let absoluteDelay = 0;
    let absoluteEndDelay = 0;
    let isDelayFilled = false;
    let isEndDelayFilled = false;

    if (isPositivePlaybackRate) {
      absoluteDelay = toRate(delay);
      absoluteEndDelay = toRate(endDelay);
      isDelayFilled = fill === "both" || fill === "backwards";
      isEndDelayFilled = fill === "both" || fill === "forwards";
    } else {
      absoluteDelay = toRate(endDelay);
      absoluteEndDelay = toRate(delay);
      isDelayFilled = fill === "both" || fill === "forwards";
      isEndDelayFilled = fill === "both" || fill === "backwards";
    }

    let endTime = 0;

    if (duration === Infinity) {
      // Set endTime so as to enable the scrubber with keeping the consistency of UI
      // even the duration was Infinity. In case of delay is longer than zero, handle
      // the graph duration as double of the delay amount. In case of no delay, handle
      // the duration as 1ms which is short enough so as to make the scrubber movable
      // and the limited duration is prioritized.
      endTime = absoluteDelay > 0 ? absoluteDelay * 2 : 1;
    } else {
      endTime =
        absoluteDelay +
        toRate(duration * (iterationCount || 1)) +
        absoluteEndDelay;
    }

    const absoluteCreatedTime = isPositivePlaybackRate
      ? createdTime
      : createdTime - endTime;
    const absoluteCurrentTimeAtCreated = isPositivePlaybackRate
      ? currentTimeAtCreated
      : endTime - currentTimeAtCreated;
    const animationCurrentTime = isPositivePlaybackRate
      ? currentTime
      : endTime - currentTime;
    const absoluteCurrentTime =
      absoluteCreatedTime + toRate(animationCurrentTime);
    const absoluteStartTime = absoluteCreatedTime + Math.min(absoluteDelay, 0);
    const absoluteStartTimeAtCreated =
      absoluteCreatedTime + absoluteCurrentTimeAtCreated;
    // To show whole graph with endDelay, we add negative endDelay amount to endTime.
    const endTimeWithNegativeEndDelay = endTime - Math.min(absoluteEndDelay, 0);
    const absoluteEndTime = absoluteCreatedTime + endTimeWithNegativeEndDelay;

    return {
      createdTime: absoluteCreatedTime,
      currentTime: absoluteCurrentTime,
      currentTimeAtCreated: absoluteCurrentTimeAtCreated,
      delay: absoluteDelay,
      endDelay: absoluteEndDelay,
      endTime: absoluteEndTime,
      isDelayFilled,
      isEndDelayFilled,
      playbackRate: layoutPlaybackRate,
      startTime: absoluteStartTime,
      startTimeAtCreated: absoluteStartTimeAtCreated,
    };
  }
}

registerFront(AnimationFront);

class AnimationsFront extends FrontClassWithSpec(animationsSpec) {
  constructor(client, targetFront, parentFront) {
    super(client, targetFront, parentFront);

    // Attribute name from which to retrieve the actorID out of the target actor's form
    this.formAttributeName = "animationsActor";
  }

  setWalkerActor(walkerFront) {
    this.walker = walkerFront;
    return super.setWalkerActor(walkerFront);
  }

  destroy() {
    super.destroy();
    this.walker = null;
  }
}

registerFront(AnimationsFront);
