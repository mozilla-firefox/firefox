/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Schedule a task to be run no sooner than a specified instant in time.
 * If the computer goes to sleep, the task will be run as soon as possible
 * after the computer wakes again.
 */

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "ScheduledTask",
    maxLogLevelPref: "app.update.compulsory_restart_log",
  })
);

const topics = ["wake_notification", "sleep_notification"];

export class ScheduledTask {
  /**
   * Constructor for a ScheduledTask. Created the task in a disarmed state. Call arm()
   * to activate the task.
   *
   * @param {Function} callback
   *  Function to execute at or after the specified time
   * @param {number} epochMilliseconds
   *  The time (in milliseconds since the Unix epoch) to execute the specified function
   * @param {number} postWakeDeferralMilliseconds
   *  (optional) If the process goes to sleep and then wakes up after the specified
   *     epochMilliseconds, wait for the specified interval before executing the task
   */
  constructor(callback, epochMilliseconds, postWakeDeferralMilliseconds = 0) {
    this.epochMilliseconds = epochMilliseconds;
    this.armed = false;
    this.timer = null;
    this.callback = callback;
    this.postWakeDeferralMilliseconds = postWakeDeferralMilliseconds;
    this.promise = Promise.resolve();
    this.observedTopics = [];
    lazy.logConsole.debug(
      `Created task to execute at ${epochMilliseconds} with ${postWakeDeferralMilliseconds} post-wake deferreal`
    );
  }

  async _callbackHandler() {
    try {
      lazy.logConsole.debug("_callbackHandler invoked");
      await this.callback();
      lazy.logConsole.debug("callback succeeded");
      this.resolve();
    } catch (err) {
      lazy.logConsole.debug("callback failed");
      this.reject(err);
    } finally {
      this._disableTask();
    }
  }

  _createTimer() {
    let delay = this.epochMilliseconds - Date.now();
    if (delay < 0) {
      // We're already past the scheduled time.
      delay = this.postWakeDeferralMilliseconds;
      this.postWakeDeferralMilliseconds = 0;
    }
    lazy.logConsole.debug(
      `_createTimer: requested delay of ${delay}, calculated delay of ${delay} milliseconds`
    );
    const newTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    newTimer.initWithCallback(
      () => {
        this._callbackHandler();
      },
      delay,
      Ci.nsITimer.TYPE_ONE_SHOT
    );
    return newTimer;
  }

  _destroyTimer() {
    if (this.timer) {
      lazy.logConsole.debug("cancelling timer");
      this.timer.cancel();
      this.timer = null;
    }
  }

  // Callback needed for Services.obs.addObserver
  observe(_subject, topic, _data) {
    lazy.logConsole.debug(`Received message on topic ${topic}`);
    if (this.armed) {
      switch (topic) {
        case "sleep_notification":
          // Going to sleep now.
          // Apparently nsITimer (and anything that uses it directly) doesn't count milliseconds during
          // sleep as part of the time. So the existing timer is no longer useful when going to sleep.
          // Destroy it.
          if (this.timer) {
            this._destroyTimer();
          }
          break;
        case "wake_notification":
          // We're back! Create a timer.
          if (!this.timer) {
            this.timer = this._createTimer();
          }
          break;
      }
    }
  }

  _enableObservers() {
    topics.forEach(topic => {
      Services.obs.addObserver(this, topic);
      this.observedTopics.push(topic);
    });
  }

  _disableObservers() {
    const iter = this.observedTopics.values();
    this.observedTopics = [];
    for (const topic of iter) {
      Services.obs.removeObserver(this, topic);
    }
  }

  _disableTask() {
    if (this.armed) {
      lazy.logConsole.debug(`Disabling task`);
      this._destroyTimer();
      this._disableObservers();
      this.armed = false;
    }
  }

  /**
   * Arming the task means that, when the computer is not sleeping, there is a timer
   * set to execute the callback after an appropriate number of milliseconds. If the
   * computer wakes from sleep, if the time period has passed, the callback is
   * executed immediately.
   */
  arm() {
    if (!this.armed) {
      lazy.logConsole.debug(`Arming task`);
      const { promise, resolve, reject } = Promise.withResolvers();
      this.promise = promise;
      this.resolve = resolve;
      this.reject = reject;

      this._enableObservers();
      this.armed = true;
      this.timer = this._createTimer();
    }
    return this; // Enable fluent chaining
  }

  /**
   * Disarm the task.
   */
  disarm() {
    if (this.armed) {
      lazy.logConsole.debug(`Disarming task`);
      this.resolve();
      this._disableTask();
    }
    return this; // Enable fluent chaining
  }

  get isArmed() {
    return this.armed;
  }

  /**
   * Returns a promise that resolves or rejects when the callback is invoked
   */
  asPromise() {
    return this.promise;
  }
}
