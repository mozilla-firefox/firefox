/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Helper singleton to compute network timings for a given httpActivity object.
 */
export const NetworkTimings = new (class {
  /**
   * Convert the httpActivity timings in HAR compatible timings. The HTTP
   * activity object holds the raw timing information in |timings| - these are
   * timings stored for each activity notification. The HAR timing information
   * is constructed based on these lower level data.
   *
   * @param {Object} httpActivity
   *     The HTTP activity object we are working with.
   * @return {Object}
   *     This object holds three properties:
   *     - {Object} offsets: the timings computed as offsets from the initial
   *     request start time.
   *     - {Object} timings: the HAR timings object
   *     - {number} total: the total time for all of the request and response
   */
  extractHarTimings(httpActivity) {
    if (httpActivity.fromCache) {
      // If it came from the browser cache, we have no timing
      // information and these should all be 0
      return this.getEmptyHARTimings();
    }

    const timings = httpActivity.timings;
    const harTimings = {};
    // If the TCP Fast Open option or tls1.3 0RTT is used tls and data can
    // be dispatched in SYN packet and not after tcp socket is connected.
    // To demostrate this properly we will calculated TLS and send start time
    // relative to CONNECTING_TO.
    // Similary if 0RTT is used, data can be sent as soon as a TLS handshake
    // starts.

    harTimings.blocked = this.#getBlockedTiming(timings);
    // DNS timing information is available only in when the DNS record is not
    // cached.
    harTimings.dns = this.#getDnsTiming(timings);
    harTimings.connect = this.#getConnectTiming(timings);
    harTimings.ssl = this.#getSslTiming(timings);

    let { secureConnectionStartTime, secureConnectionStartTimeRelative } =
      this.#getSecureConnectionStartTimeInfo(timings);

    // sometimes the connection information events are attached to a speculative
    // channel instead of this one, but necko might glue them back together in the
    // nsITimedChannel interface used by Resource and Navigation Timing
    const timedChannel = httpActivity.channel.QueryInterface(
      Ci.nsITimedChannel
    );

    const {
      tcpConnectEndTimeTc,
      connectStartTimeTc,
      connectEndTimeTc,
      secureConnectionStartTimeTc,
      domainLookupEndTimeTc,
      domainLookupStartTimeTc,
    } = this.#getDataFromTimedChannel(timedChannel);

    if (
      harTimings.connect <= 0 &&
      timedChannel &&
      tcpConnectEndTimeTc != 0 &&
      connectStartTimeTc != 0
    ) {
      harTimings.connect = tcpConnectEndTimeTc - connectStartTimeTc;
      if (secureConnectionStartTimeTc != 0) {
        harTimings.ssl = connectEndTimeTc - secureConnectionStartTimeTc;
        secureConnectionStartTime =
          secureConnectionStartTimeTc - connectStartTimeTc;
        secureConnectionStartTimeRelative = true;
      } else {
        harTimings.ssl = -1;
      }
    } else if (
      timedChannel &&
      timings.STATUS_TLS_STARTING &&
      secureConnectionStartTimeTc != 0
    ) {
      // It can happen that TCP Fast Open actually have not sent any data and
      // timings.STATUS_TLS_STARTING.first value will be corrected in
      // timedChannel.secureConnectionStartTime
      if (secureConnectionStartTimeTc > timings.STATUS_TLS_STARTING.first) {
        // TCP Fast Open actually did not sent any data.
        harTimings.ssl = connectEndTimeTc - secureConnectionStartTimeTc;
        secureConnectionStartTimeRelative = false;
      }
    }

    if (
      harTimings.dns <= 0 &&
      timedChannel &&
      domainLookupEndTimeTc != 0 &&
      domainLookupStartTimeTc != 0
    ) {
      harTimings.dns = domainLookupEndTimeTc - domainLookupStartTimeTc;
    }

    harTimings.send = this.#getSendTiming(timings);
    harTimings.wait = this.#getWaitTiming(timings);
    harTimings.receive = this.#getReceiveTiming(timings);
    let { startSendingTime, startSendingTimeRelative } =
      this.#getStartSendingTimeInfo(timings, connectStartTimeTc);

    if (secureConnectionStartTimeRelative) {
      const time = Math.max(Math.round(secureConnectionStartTime / 1000), -1);
      secureConnectionStartTime = time;
    }
    if (startSendingTimeRelative) {
      const time = Math.max(Math.round(startSendingTime / 1000), -1);
      startSendingTime = time;
    }

    const ot = this.#calculateOffsetAndTotalTime(
      harTimings,
      secureConnectionStartTime,
      startSendingTimeRelative,
      secureConnectionStartTimeRelative,
      startSendingTime
    );
    return {
      total: ot.total,
      timings: harTimings,
      offsets: ot.offsets,
    };
  }

  extractServerTimings(httpActivity) {
    const channel = httpActivity.channel;
    if (!channel || !channel.serverTiming) {
      return null;
    }

    const serverTimings = new Array(channel.serverTiming.length);

    for (let i = 0; i < channel.serverTiming.length; ++i) {
      const { name, duration, description } =
        channel.serverTiming.queryElementAt(i, Ci.nsIServerTiming);
      serverTimings[i] = { name, duration, description };
    }

    return serverTimings;
  }

  extractServiceWorkerTimings(httpActivity) {
    if (!httpActivity.fromServiceWorker) {
      return null;
    }
    const timedChannel = httpActivity.channel.QueryInterface(
      Ci.nsITimedChannel
    );

    return {
      launchServiceWorker:
        timedChannel.launchServiceWorkerEndTime -
        timedChannel.launchServiceWorkerStartTime,
      requestToServiceWorker:
        timedChannel.dispatchFetchEventEndTime -
        timedChannel.dispatchFetchEventStartTime,
      handledByServiceWorker:
        timedChannel.handleFetchEventEndTime -
        timedChannel.handleFetchEventStartTime,
    };
  }

  /**
   * For some requests such as cached or data: URI requests, we don't have
   * access to any timing information so all timings should be 0.
   *
   * @return {Object}
   *     A timings object (@see extractHarTimings), with all values set to 0.
   */
  getEmptyHARTimings() {
    return {
      total: 0,
      timings: {
        blocked: 0,
        dns: 0,
        ssl: 0,
        connect: 0,
        send: 0,
        wait: 0,
        receive: 0,
      },
      offsets: {
        blocked: 0,
        dns: 0,
        ssl: 0,
        connect: 0,
        send: 0,
        wait: 0,
        receive: 0,
      },
    };
  }

  #getBlockedTiming(timings) {
    if (timings.STATUS_RESOLVING && timings.STATUS_CONNECTING_TO) {
      return timings.STATUS_RESOLVING.first - timings.REQUEST_HEADER.first;
    } else if (timings.STATUS_SENDING_TO) {
      return timings.STATUS_SENDING_TO.first - timings.REQUEST_HEADER.first;
    }

    return -1;
  }

  #getDnsTiming(timings) {
    if (timings.STATUS_RESOLVING && timings.STATUS_RESOLVED) {
      return timings.STATUS_RESOLVED.last - timings.STATUS_RESOLVING.first;
    }

    return -1;
  }

  #getConnectTiming(timings) {
    if (timings.STATUS_CONNECTING_TO && timings.STATUS_CONNECTED_TO) {
      return (
        timings.STATUS_CONNECTED_TO.last - timings.STATUS_CONNECTING_TO.first
      );
    }

    return -1;
  }

  #getReceiveTiming(timings) {
    if (timings.RESPONSE_START && timings.RESPONSE_COMPLETE) {
      return timings.RESPONSE_COMPLETE.last - timings.RESPONSE_START.first;
    }

    return -1;
  }

  #getWaitTiming(timings) {
    if (timings.RESPONSE_START) {
      return (
        timings.RESPONSE_START.first -
        (timings.REQUEST_BODY_SENT || timings.STATUS_SENDING_TO).last
      );
    }

    return -1;
  }

  #getSslTiming(timings) {
    if (timings.STATUS_TLS_STARTING && timings.STATUS_TLS_ENDING) {
      return timings.STATUS_TLS_ENDING.last - timings.STATUS_TLS_STARTING.first;
    }

    return -1;
  }

  #getSendTiming(timings) {
    if (timings.STATUS_SENDING_TO) {
      return timings.STATUS_SENDING_TO.last - timings.STATUS_SENDING_TO.first;
    } else if (timings.REQUEST_HEADER && timings.REQUEST_BODY_SENT) {
      return timings.REQUEST_BODY_SENT.last - timings.REQUEST_HEADER.first;
    }

    return -1;
  }

  #getDataFromTimedChannel(timedChannel) {
    const lookUpArr = [
      "tcpConnectEndTime",
      "connectStartTime",
      "connectEndTime",
      "secureConnectionStartTime",
      "domainLookupEndTime",
      "domainLookupStartTime",
    ];

    return lookUpArr.reduce((prev, prop) => {
      const propName = prop + "Tc";
      return {
        ...prev,
        [propName]: (() => {
          if (!timedChannel) {
            return 0;
          }

          const value = timedChannel[prop];

          if (
            value != 0 &&
            timedChannel.asyncOpenTime &&
            value < timedChannel.asyncOpenTime
          ) {
            return 0;
          }

          return value;
        })(),
      };
    }, {});
  }

  #getSecureConnectionStartTimeInfo(timings) {
    let secureConnectionStartTime = 0;
    let secureConnectionStartTimeRelative = false;

    if (timings.STATUS_TLS_STARTING && timings.STATUS_TLS_ENDING) {
      if (timings.STATUS_CONNECTING_TO) {
        secureConnectionStartTime =
          timings.STATUS_TLS_STARTING.first -
          timings.STATUS_CONNECTING_TO.first;
      }

      if (secureConnectionStartTime < 0) {
        secureConnectionStartTime = 0;
      }
      secureConnectionStartTimeRelative = true;
    }

    return {
      secureConnectionStartTime,
      secureConnectionStartTimeRelative,
    };
  }

  #getStartSendingTimeInfo(timings, connectStartTimeTc) {
    let startSendingTime = 0;
    let startSendingTimeRelative = false;

    if (timings.STATUS_SENDING_TO) {
      if (timings.STATUS_CONNECTING_TO) {
        startSendingTime =
          timings.STATUS_SENDING_TO.first - timings.STATUS_CONNECTING_TO.first;
        startSendingTimeRelative = true;
      } else if (connectStartTimeTc != 0) {
        startSendingTime = timings.STATUS_SENDING_TO.first - connectStartTimeTc;
        startSendingTimeRelative = true;
      }

      if (startSendingTime < 0) {
        startSendingTime = 0;
      }
    }
    return { startSendingTime, startSendingTimeRelative };
  }

  #convertTimeToMs(timing) {
    return Math.max(Math.round(timing / 1000), -1);
  }

  #calculateOffsetAndTotalTime(
    harTimings,
    secureConnectionStartTime,
    startSendingTimeRelative,
    secureConnectionStartTimeRelative,
    startSendingTime
  ) {
    let totalTime = 0;
    for (const timing in harTimings) {
      const time = this.#convertTimeToMs(harTimings[timing]);
      harTimings[timing] = time;
      if (time > -1 && timing != "connect" && timing != "ssl") {
        totalTime += time;
      }
    }

    // connect, ssl and send times can be overlapped.
    if (startSendingTimeRelative) {
      totalTime += startSendingTime;
    } else if (secureConnectionStartTimeRelative) {
      totalTime += secureConnectionStartTime;
      totalTime += harTimings.ssl;
    }

    const offsets = {};
    offsets.blocked = 0;
    offsets.dns = harTimings.blocked;
    offsets.connect = offsets.dns + harTimings.dns;
    if (secureConnectionStartTimeRelative) {
      offsets.ssl = offsets.connect + secureConnectionStartTime;
    } else {
      offsets.ssl = offsets.connect + harTimings.connect;
    }
    if (startSendingTimeRelative) {
      offsets.send = offsets.connect + startSendingTime;
      if (!secureConnectionStartTimeRelative) {
        offsets.ssl = offsets.send - harTimings.ssl;
      }
    } else {
      offsets.send = offsets.ssl + harTimings.ssl;
    }
    offsets.wait = offsets.send + harTimings.send;
    offsets.receive = offsets.wait + harTimings.wait;

    return {
      total: totalTime,
      offsets,
    };
  }
})();
