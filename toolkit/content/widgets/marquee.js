/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.MarqueeWidget = class {
  constructor(shadowRoot) {
    this.shadowRoot = shadowRoot;
    this.element = shadowRoot.host;
    this.document = this.element.ownerDocument;
    this.window = this.document.defaultView;
    // This needed for behavior=alternate, in order to know in which of the two
    // directions we're going.
    this.dirsign = 1;
    this._currentLoop = this.element.loop;
    this.animation = null;
    this._restartScheduled = null;
  }

  onsetup() {
    let link = this.shadowRoot.createElementAndAppendChildAt(
      this.shadowRoot,
      "link"
    );
    link.rel = "stylesheet";
    link.href = "chrome://global/content/elements/marquee.css";
    this.shadowRoot.createElementAndAppendChildAt(this.shadowRoot, "slot");

    this._mutationObserver = new this.window.MutationObserver(aMutations =>
      this._mutationActor(aMutations)
    );
    this._mutationObserver.observe(this.element, {
      attributes: true,
      attributeOldValue: true,
      attributeFilter: ["loop", "direction", "behavior"],
    });

    // init needs to be run after the page has loaded in order to calculate
    // the correct height/width
    if (this.document.readyState == "complete") {
      this.init();
    } else {
      this.window.addEventListener("load", this, { once: true });
    }

    this.shadowRoot.addEventListener("marquee-start", this);
    this.shadowRoot.addEventListener("marquee-stop", this);
  }

  teardown() {
    this.doStop();
    this._mutationObserver.disconnect();

    this.window.removeEventListener("load", this);
    this.shadowRoot.removeEventListener("marquee-start", this);
    this.shadowRoot.removeEventListener("marquee-stop", this);
    this.shadowRoot.replaceChildren();
  }

  handleEvent(aEvent) {
    if (!aEvent.isTrusted) {
      return;
    }

    switch (aEvent.type) {
      case "load":
        this.init();
        break;
      case "marquee-start":
        this.doStart();
        break;
      case "marquee-stop":
        this.doStop();
        break;
      case "finish":
        this._animationFinished();
        break;
    }
  }

  _animationFinished() {
    let behavior = this.element.behavior;
    let shouldLoop =
      this._currentLoop > 1 || (this._currentLoop == -1 && behavior != "slide");
    if (shouldLoop) {
      if (this._currentLoop > 0) {
        this._currentLoop--;
      }
      if (behavior == "alternate") {
        this.dirsign = -this.dirsign;
      }
      this.doStop();
      this.doStart();
    }
  }

  get scrollDelayWithTruespeed() {
    if (this.element.scrollDelay < 60 && !this.element.trueSpeed) {
      return 60;
    }
    return this.element.scrollDelay;
  }

  get slot() {
    return this.shadowRoot.lastChild;
  }

  /**
   * Computes CSS-derived values needed to compute the transform of the
   * contents.
   *
   * In particular, it measures the auto width and height of the contents,
   * and the effective width and height of the marquee itself, along with its
   * css directionality (which affects the effective direction).
   */
  getMetrics() {
    let slot = this.slot;
    slot.style.width = "max-content";
    let slotCS = this.window.getComputedStyle(slot);
    let marqueeCS = this.window.getComputedStyle(this.element);
    let contentWidth = parseFloat(slotCS.width) || 0;
    let contentHeight = parseFloat(slotCS.height) || 0;
    let marqueeWidth = parseFloat(marqueeCS.width) || 0;
    let marqueeHeight = parseFloat(marqueeCS.height) || 0;
    slot.style.width = "";
    return {
      contentWidth,
      contentHeight,
      marqueeWidth,
      marqueeHeight,
      cssDirection: marqueeCS.direction,
    };
  }

  /**
   * Gets the layout metrics from getMetrics(), and returns an object
   * describing the start, end, and axis of the animation for the given marquee
   * behavior and direction.
   */
  getTransformParameters({
    contentWidth,
    contentHeight,
    marqueeWidth,
    marqueeHeight,
    cssDirection,
  }) {
    const innerWidth = marqueeWidth - contentWidth;
    const innerHeight = marqueeHeight - contentHeight;
    const dir = this.element.direction;

    let start = 0;
    let end = 0;
    const axis = dir == "up" || dir == "down" ? "y" : "x";
    switch (this.element.behavior) {
      case "alternate":
        switch (dir) {
          case "up":
          case "down": {
            if (innerHeight >= 0) {
              start = innerHeight;
              end = 0;
            } else {
              start = 0;
              end = innerHeight;
            }
            if (dir == "down") {
              [start, end] = [end, start];
            }
            if (this.dirsign == -1) {
              [start, end] = [end, start];
            }
            break;
          }
          case "right":
          case "left":
          default: {
            if (innerWidth >= 0) {
              start = innerWidth;
              end = 0;
            } else {
              start = 0;
              end = innerWidth;
            }
            if (dir == "right") {
              [start, end] = [end, start];
            }
            if (cssDirection == "rtl") {
              [start, end] = [end, start];
            }
            if (this.dirsign == -1) {
              [start, end] = [end, start];
            }
            break;
          }
        }
        break;
      case "slide":
        switch (dir) {
          case "up": {
            start = marqueeHeight;
            end = 0;
            break;
          }
          case "down": {
            start = -contentHeight;
            end = innerHeight;
            break;
          }
          case "right":
          default: {
            let isRight = dir == "right";
            if (cssDirection == "rtl") {
              isRight = !isRight;
            }
            if (isRight) {
              start = -contentWidth;
              end = innerWidth;
            } else {
              start = marqueeWidth;
              end = 0;
            }
            break;
          }
        }
        break;
      case "scroll":
      default:
        switch (dir) {
          case "up":
          case "down": {
            start = marqueeHeight;
            end = -contentHeight;
            if (dir == "down") {
              [start, end] = [end, start];
            }
            break;
          }
          case "right":
          case "left":
          default: {
            start = marqueeWidth;
            end = -contentWidth;
            if (dir == "right") {
              [start, end] = [end, start];
            }
            if (cssDirection == "rtl") {
              [start, end] = [end, start];
            }
            break;
          }
        }
        break;
    }
    return { start, end, axis };
  }

  /**
   * Measures the marquee contents, and starts the marquee animation if needed.
   * The translate animation is applied to the <slot> element.
   * Bouncing and looping is implemented in the finish event handler for the
   * given animation (see _animationFinished()).
   */
  doStart() {
    if (this.animation) {
      return;
    }
    let scrollAmount = this.element.scrollAmount;
    if (!scrollAmount) {
      return;
    }
    let metrics = this.getMetrics();
    let { axis, start, end } = this.getTransformParameters(metrics);
    let duration =
      (Math.abs(end - start) * this.scrollDelayWithTruespeed) / scrollAmount;
    let startValue = start + "px";
    let endValue = end + "px";
    if (axis == "y") {
      startValue = "0 " + startValue;
      endValue = "0 " + endValue;
    }
    // NOTE(emilio): It seems tempting to use `iterations` here, but doing so
    // wouldn't be great because this uses current layout values (via
    // getMetrics()), so sizes wouldn't update. This way we update once per
    // animation iteration.
    //
    // fill: forwards is needed so that behavior=slide doesn't jump back to the
    // start after the animation finishes.
    this.animation = this.slot.animate(
      {
        translate: [startValue, endValue],
      },
      {
        duration,
        easing: "linear",
        fill: "forwards",
      }
    );
    this.animation.addEventListener("finish", this, { once: true });
  }

  doStop() {
    if (!this.animation) {
      return;
    }
    if (this._restartScheduled) {
      this.window.cancelAnimationFrame(this._restartScheduled);
      this._restartScheduled = null;
    }
    this.animation.removeEventListener("finish", this);
    this.animation.cancel();
    this.animation = null;
  }

  init() {
    this.element.stop();
    this.doStart();
  }

  _mutationActor(aMutations) {
    while (aMutations.length) {
      let mutation = aMutations.shift();
      let attrName = mutation.attributeName.toLowerCase();
      let oldValue = mutation.oldValue;
      let newValue = this.element.getAttribute(attrName);
      if (oldValue == newValue) {
        continue;
      }
      if (attrName == "loop") {
        this._currentLoop = this.element.loop;
      }
      if (attrName == "direction" || attrName == "behavior") {
        this._scheduleRestartIfNeeded();
      }
    }
  }

  // Schedule a restart with the new parameters if we're running.
  _scheduleRestartIfNeeded() {
    if (!this.animation || this._restartScheduled != null) {
      return;
    }
    this._restartScheduled = this.window.requestAnimationFrame(() => {
      if (this.animation) {
        this.doStop();
        this.doStart();
      }
    });
  }
};
