/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
});

const SLEEP_TOPIC = "sleep_notification";
const WAKE_TOPIC = "wake_notification";
const BACKGROUND_TOPIC = "application-background";
const FOREGROUND_TOPIC = "application-foreground";

/**
 * Keeps the proxy connection healthy across interruptions that can drop the
 * underlying network: system sleep/wake on desktop and the app moving to the
 * background/foreground on Android.
 *
 * While ACTIVE, suspends the channel filter when going to sleep/background and,
 * on wake/foreground, resumes it if the pass is still good or rotates otherwise.
 */
export class IPPLifeCycleHelperClass {
  #observing = false;
  #suspended = false;

  constructor() {
    this.handleEvent = this.#handleEvent.bind(this);
  }

  init() {
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:StateChanged",
      this.handleEvent
    );
  }

  initOnStartupCompleted() {}

  uninit() {
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:StateChanged",
      this.handleEvent
    );
    this.stop();
  }

  #handleEvent(_event) {
    switch (lazy.IPPProxyManager.state) {
      case lazy.IPPProxyStates.ACTIVE:
        this.start();
        break;
      default:
        this.stop();
        break;
    }
  }

  start() {
    if (this.#observing) {
      return;
    }
    Services.obs.addObserver(this, SLEEP_TOPIC);
    Services.obs.addObserver(this, WAKE_TOPIC);
    Services.obs.addObserver(this, BACKGROUND_TOPIC);
    Services.obs.addObserver(this, FOREGROUND_TOPIC);
    this.#observing = true;
  }

  stop() {
    if (!this.#observing) {
      return;
    }
    Services.obs.removeObserver(this, SLEEP_TOPIC);
    Services.obs.removeObserver(this, WAKE_TOPIC);
    Services.obs.removeObserver(this, BACKGROUND_TOPIC);
    Services.obs.removeObserver(this, FOREGROUND_TOPIC);
    this.#observing = false;
    this.#suspended = false;
  }

  /**
   * True while the device is asleep or the app is backgrounded, i.e. between a
   * sleep/background notification and the following wake/foreground one.
   */
  get isSuspended() {
    return this.#suspended;
  }

  observe(_subject, topic) {
    switch (topic) {
      case SLEEP_TOPIC:
      case BACKGROUND_TOPIC:
        this.#onSleep();
        break;
      case WAKE_TOPIC:
      case FOREGROUND_TOPIC:
        this.#onWake();
        break;
    }
  }

  #onSleep() {
    this.#suspended = true;
    const channelFilter = lazy.IPPProxyManager.channelFilter();
    if (!channelFilter?.active) {
      return;
    }
    channelFilter.suspend();
  }

  #onWake() {
    this.#suspended = false;
    const channelFilter = lazy.IPPProxyManager.channelFilter();
    if (!channelFilter?.active) {
      return;
    }
    if (channelFilter.canResume) {
      channelFilter.resume();
    } else {
      // The network is usually not back yet right after wake, so retry through
      // network errors instead of erroring out.
      lazy.IPPProxyManager.rotateProxyPass({
        abortOnNetworkError: false,
      }).catch(() => {});
    }
  }
}

export const IPPLifecycleHelper = new IPPLifeCycleHelperClass();
