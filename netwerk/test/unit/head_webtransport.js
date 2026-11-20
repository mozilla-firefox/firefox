/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cookies.js */

let WebTransportListener = function () {};

WebTransportListener.prototype = {
  onSessionReady(sessionId) {
    info("SessionId " + sessionId);
    this.ready();
  },
  onSessionClosed(errorCode, reason) {
    info("Error: " + errorCode + " reason: " + reason);
    if (this.closed) {
      this.closed();
    }
  },
  onStopSending(streamId, error) {
    info("got stop sending for stream " + streamId + " error: " + error);
    if (this.stopSending) {
      this.stopSending(streamId, error);
    }
  },
  onIncomingBidirectionalStreamAvailable(stream) {
    info("got incoming bidirectional stream");
    this.streamAvailable(stream);
  },
  onIncomingUnidirectionalStreamAvailable(stream) {
    info("got incoming unidirectional stream");
    this.streamAvailable(stream);
  },
  onDatagramReceived(data) {
    info("got datagram");
    if (this.onDatagram) {
      this.onDatagram(data);
    }
  },
  onMaxDatagramSize(size) {
    info("max datagram size: " + size);
    if (this.onMaxDatagramSize) {
      this.onMaxDatagramSize(size);
    }
  },
  onOutgoingDatagramOutCome(id, outcome) {
    if (this.onDatagramOutcome) {
      this.onDatagramOutcome({ id, outcome });
    }
  },

  QueryInterface: ChromeUtils.generateQI(["WebTransportSessionEventListener"]),
};

function WebTransportStreamCallback() {}

WebTransportStreamCallback.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIWebTransportStreamCallback"]),

  onBidirectionalStreamReady(aStream) {
    Assert.notEqual(aStream, null);
    this.finish(aStream);
  },
  onUnidirectionalStreamReady(aStream) {
    Assert.notEqual(aStream, null);
    this.finish(aStream);
  },
  onError(aError) {
    this.finish(aError);
  },
};

function StreamStatsCallback() {}

StreamStatsCallback.prototype = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIWebTransportStreamStatsCallback",
  ]),

  onSendStatsAvailable(aStats) {
    Assert.notEqual(aStats, null);
    this.finish(aStats);
  },
  onReceiveStatsAvailable(aStats) {
    Assert.notEqual(aStats, null);
    this.finish(aStats);
  },
};

function inputStreamReader() {}

inputStreamReader.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIInputStreamCallback"]),

  onInputStreamReady(input) {
    let data = NetUtil.readInputStreamToString(input, input.available());
    this.finish(data);
  },
};

function streamCreatePromise(transport, bidi) {
  return new Promise(resolve => {
    let listener = new WebTransportStreamCallback().QueryInterface(
      Ci.nsIWebTransportStreamCallback
    );
    listener.finish = resolve;

    if (bidi) {
      transport.createOutgoingBidirectionalStream(listener);
    } else {
      transport.createOutgoingUnidirectionalStream(listener);
    }
  });
}

function sendStreamStatsPromise(stream) {
  return new Promise(resolve => {
    let listener = new StreamStatsCallback().QueryInterface(
      Ci.nsIWebTransportStreamStatsCallback
    );
    listener.finish = resolve;

    stream.QueryInterface(Ci.nsIWebTransportSendStream);
    stream.getSendStreamStats(listener);
  });
}

function receiveStreamStatsPromise(stream) {
  return new Promise(resolve => {
    let listener = new StreamStatsCallback().QueryInterface(
      Ci.nsIWebTransportStreamStatsCallback
    );
    listener.finish = resolve;

    stream.QueryInterface(Ci.nsIWebTransportReceiveStream);
    stream.getReceiveStreamStats(listener);
  });
}
