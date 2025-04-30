/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EventEmitter: "resource://gre/modules/EventEmitter.sys.mjs",
  NetworkUtils:
    "resource://devtools/shared/network-observer/NetworkUtils.sys.mjs",
});

const OBSERVER_TOPIC_DATA_CHANNEL_OPENED = "data-channel-opened";

/**
 * The DataChannelListener can be used to listen for
 * data-channel-opened notifications emitted for data channels.
 * This notification should be monitored in content processes.
 * Note that parent process notifications will be emitted for navigation
 * requests and therefore should be ignored here.
 *
 * Example:
 * ```
 * const listener = new DataChannelListener();
 * listener.on("data-channel-opened", DataChannelListener);
 * listener.startListening();
 *
 * const onDataChannelOpened = (eventName, data = {}) => {
 *   const { channel } = data;
 *   ...
 * };
 * ```
 *
 * @fires cached-resource-sent
 *    The DataChannelListener emits "data-channel-opened" event with the
 *    following object as payload:
 *      - {nsIDataChannel} channel
 *            The data channel for which the observer notification was emitted.
 */

export class DataChannelListener {
  #context;
  #listening;

  /**
   * Create a new DataChannelListener instance.
   *
   * @param {BrowsingContext} context
   *     The browsing context to filter the events for.
   */
  constructor(context) {
    lazy.EventEmitter.decorate(this);

    this.#listening = false;
    this.#context = context;
  }

  destroy() {
    this.stopListening();
  }

  observe(subject, topic) {
    switch (topic) {
      case OBSERVER_TOPIC_DATA_CHANNEL_OPENED: {
        if (!(subject instanceof Ci.nsIDataChannel)) {
          return;
        }
        const channel = subject.QueryInterface(Ci.nsIDataChannel);
        channel.QueryInterface(Ci.nsIChannel);

        if (channel.isDocument) {
          // navigation data URIs are handled in the parent process, where we have
          // access to the navigation manager.
          return;
        }

        const id = lazy.NetworkUtils.getChannelBrowsingContextID(channel);
        const browsingContext = BrowsingContext.get(id);

        // Send the event only if the notification comes for the observed
        // context.
        if (browsingContext === this.#context) {
          this.emit("data-channel-opened", {
            channel,
          });
        }
        break;
      }
    }
  }

  startListening() {
    if (this.#listening) {
      return;
    }

    Services.obs.addObserver(this, OBSERVER_TOPIC_DATA_CHANNEL_OPENED);

    this.#listening = true;
  }

  stopListening() {
    if (!this.#listening) {
      return;
    }

    Services.obs.removeObserver(this, OBSERVER_TOPIC_DATA_CHANNEL_OPENED);

    this.#listening = false;
  }
}
