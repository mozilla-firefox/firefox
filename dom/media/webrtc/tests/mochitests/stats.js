/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const statsExpectedByType = {
  "inbound-rtp": {
    expected: [
      "trackIdentifier",
      "id",
      "mid",
      "timestamp",
      "type",
      "ssrc",
      "mediaType",
      "kind",
      "codecId",
      "packetsReceived",
      "packetsLost",
      "packetsDiscarded",
      "bytesReceived",
      "jitter",
      "lastPacketReceivedTimestamp",
      "headerBytesReceived",
      "jitterBufferDelay",
      "jitterBufferTargetDelay",
      "jitterBufferMinimumDelay",
      "jitterBufferEmittedCount",
      "transportId",
    ],
    optional: [
      "remoteId",
      "nackCount",
      "qpSum",
      "estimatedPlayoutTimestamp",
      "rtxSsrc",
      "retransmittedPacketsReceived",
      "retransmittedBytesReceived",
    ],
    localVideoOnly: [
      "firCount",
      "pliCount",
      "framesDecoded",
      "keyFramesDecoded",
      "framesDropped",
      "discardedPackets",
      "framesPerSecond",
      "frameWidth",
      "frameHeight",
      "framesReceived",
      "framesAssembledFromMultiplePackets",
      "totalDecodeTime",
      "totalInterFrameDelay",
      "totalProcessingDelay",
      "totalSquaredInterFrameDelay",
      "pauseCount",
      "totalPausesDuration",
      "freezeCount",
      "totalFreezesDuration",
      "totalAssemblyTime",
    ],
    localAudioOnly: [
      "totalSamplesReceived",
      // libwebrtc doesn't seem to do FEC for video
      "fecPacketsReceived",
      "fecPacketsDiscarded",
      "concealedSamples",
      "silentConcealedSamples",
      "concealmentEvents",
      "insertedSamplesForDeceleration",
      "removedSamplesForAcceleration",
      "audioLevel",
      "totalAudioEnergy",
      "totalSamplesDuration",
    ],
    unimplemented: [
      "mediaTrackId",
      "associateStatsId",
      "sliCount",
      "packetsRepaired",
      "fractionLost",
      "burstPacketsLost",
      "burstLossCount",
      "burstDiscardCount",
      "gapDiscardRate",
      "gapLossRate",
    ],
    deprecated: ["mozRtt", "isRemote"],
  },
  "outbound-rtp": {
    expected: [
      "id",
      "mid",
      "timestamp",
      "type",
      "ssrc",
      "mediaType",
      "kind",
      "codecId",
      "packetsSent",
      "bytesSent",
      "remoteId",
      "headerBytesSent",
      "retransmittedPacketsSent",
      "retransmittedBytesSent",
      "transportId",
    ],
    optional: ["nackCount", "qpSum", "rid", "rtxSsrc", "targetBitrate"],
    localAudioOnly: [],
    localVideoOnly: [
      "framesEncoded",
      "keyFramesEncoded",
      "firCount",
      "pliCount",
      "frameWidth",
      "frameHeight",
      "framesPerSecond",
      "framesSent",
      "hugeFramesSent",
      "totalEncodeTime",
    ],
    unimplemented: ["mediaTrackId", "sliCount"],
    deprecated: ["isRemote"],
  },
  "remote-inbound-rtp": {
    expected: [
      "id",
      "timestamp",
      "type",
      "ssrc",
      "mediaType",
      "kind",
      "codecId",
      "packetsLost",
      "jitter",
      "localId",
      "totalRoundTripTime",
      "fractionLost",
      "roundTripTimeMeasurements",
      "transportId",
    ],
    optional: ["roundTripTime", "nackCount", "packetsReceived"],
    unimplemented: [
      "mediaTrackId",
      "packetsDiscarded",
      "associateStatsId",
      "sliCount",
      "packetsRepaired",
      "burstPacketsLost",
      "burstLossCount",
      "burstDiscardCount",
      "gapDiscardRate",
      "gapLossRate",
    ],
    deprecated: ["mozRtt", "isRemote"],
  },
  "remote-outbound-rtp": {
    expected: [
      "id",
      "timestamp",
      "type",
      "ssrc",
      "mediaType",
      "kind",
      "codecId",
      "packetsSent",
      "bytesSent",
      "localId",
      "remoteTimestamp",
      "transportId",
    ],
    optional: ["nackCount"],
    unimplemented: ["mediaTrackId", "sliCount", "targetBitrate"],
    deprecated: ["isRemote"],
  },
  "media-source": {
    expected: ["id", "timestamp", "type", "trackIdentifier", "kind"],
    unimplemented: [
      "echoReturnLoss",
      "echoReturnLossEnhancement",
      "droppedSamplesDuration",
      "droppedSamplesEvents",
      "totalCaptureDelay",
      "totalSamplesCaptured",
    ],
    localAudioOnly: ["audioLevel", "totalAudioEnergy", "totalSamplesDuration"],
    localVideoOnly: ["frames", "framesPerSecond", "width", "height"],
    optional: [],
    deprecated: [],
  },
  codec: {
    expected: [
      "timestamp",
      "type",
      "id",
      "payloadType",
      "transportId",
      "mimeType",
      "clockRate",
    ],
    optional: ["codecType", "channels", "sdpFmtpLine"],
    unimplemented: [],
    deprecated: [],
  },
  "peer-connection": { skip: true },
  "data-channel": { skip: true },
  "candidate-pair": {
    expected: [
      "id",
      "timestamp",
      "type",
      "transportId",
      "localCandidateId",
      "remoteCandidateId",
      "state",
      "priority",
      "nominated",
      "writable",
      "readable",
      "bytesSent",
      "bytesReceived",
      "packetsSent",
      "packetsReceived",
      "lastPacketSentTimestamp",
      "lastPacketReceivedTimestamp",
      "totalRoundTripTime",
      "currentRoundTripTime",
      "responsesReceived",
    ],
    optional: ["selected"],
    unimplemented: [
      "availableOutgoingBitrate",
      "availableIncomingBitrate",
      "requestsReceived",
      "requestsSent",
      "responsesSent",
      "retransmissionsReceived",
      "retransmissionsSent",
      "consentRequestsSent",
    ],
    deprecated: [],
  },
  "local-candidate": {
    expected: [
      "id",
      "timestamp",
      "type",
      "address",
      "protocol",
      "port",
      "candidateType",
      "priority",
      "usernameFragment",
      "foundation",
      "transportId",
    ],
    optional: ["relayProtocol", "proxied", "tcpType"],
    unimplemented: ["networkType", "url"],
    deprecated: [
      "candidateId",
      "portNumber",
      "ipAddress",
      "componentId",
      "mozLocalTransport",
      "transport",
    ],
  },
  "remote-candidate": {
    expected: [
      "id",
      "timestamp",
      "type",
      "address",
      "protocol",
      "port",
      "candidateType",
      "priority",
      "usernameFragment",
      "transportId",
    ],
    optional: ["foundation", "relayProtocol", "proxied", "tcpType"],
    unimplemented: ["networkType", "url"],
    deprecated: [
      "candidateId",
      "portNumber",
      "ipAddress",
      "componentId",
      "mozLocalTransport",
      "transport",
    ],
  },
  transport: {
    expected: [
      "id",
      "timestamp",
      "type",
      "packetsSent",
      "packetsReceived",
      "bytesSent",
      "bytesReceived",
      "iceRole",
      "iceLocalUsernameFragment",
      "dtlsState",
      "iceState",
      "selectedCandidatePairId",
      "localCertificateId",
      "remoteCertificateId",
      "tlsVersion",
      "dtlsCipher",
      "dtlsRole",
      "srtpCipher",
      "selectedCandidatePairChanges",
    ],
    optional: [],
    unimplemented: [],
    deprecated: [],
  },
  certificate: { skip: true },
};

["inbound-rtp", "outbound-rtp", "media-source"].forEach(type => {
  let s = statsExpectedByType[type];
  s.optional = [...s.optional, ...s.localVideoOnly, ...s.localAudioOnly];
});

//
//  Checks that the fields in a report conform to the expectations in
// statExpectedByType
//
function checkExpectedFields(report) {
  report.forEach(stat => {
    let expectations = statsExpectedByType[stat.type];
    ok(expectations, "Stats type " + stat.type + " was expected");
    // If the type is not expected or if it is flagged for skipping continue to
    // the next
    if (!expectations || expectations.skip) {
      return;
    }
    // Check that all required fields exist
    expectations.expected.forEach(field => {
      ok(
        field in stat,
        "Expected stat field " + stat.type + "." + field + " exists"
      );
    });
    // Check that each field is either expected or optional
    let allowed = [...expectations.expected, ...expectations.optional];
    Object.keys(stat).forEach(field => {
      ok(
        allowed.includes(field),
        "Stat field " +
          stat.type +
          "." +
          field +
          ` is allowed. ${JSON.stringify(stat)}`
      );
    });

    //
    // Ensure that unimplemented fields are not implemented
    //   note: if a field is implemented it should be moved to expected or
    //   optional.
    //
    expectations.unimplemented.forEach(field => {
      ok(
        !Object.keys(stat).includes(field),
        "Unimplemented field " + stat.type + "." + field + " does not exist."
      );
    });

    //
    // Ensure that all deprecated fields are not present
    //
    expectations.deprecated.forEach(field => {
      ok(
        !Object.keys(stat).includes(field),
        "Deprecated field " + stat.type + "." + field + " does not exist."
      );
    });
  });
}

function pedanticChecks(report) {
  // Check that report is only-maplike
  [...report.keys()].forEach(key =>
    is(
      report[key],
      undefined,
      `Report is not dictionary like, it lacks a property for key ${key}`
    )
  );
  report.forEach((statObj, mapKey) => {
    info(`"${mapKey} = ${JSON.stringify(statObj, null, 2)}`);
  });

  // These matter when checking candidate-pair stats for bytes sent/received
  let sending = false;
  let receiving = false;

  // eslint-disable-next-line complexity
  report.forEach((statObj, mapKey) => {
    let tested = {};
    // Record what fields get tested.
    // To access a field foo without marking it as tested use stat.inner.foo
    let stat = new Proxy(statObj, {
      get(stat, key) {
        if (key == "inner") {
          return stat;
        }
        tested[key] = true;
        return stat[key];
      },
    });

    let expectations = statsExpectedByType[stat.type];

    if (expectations.skip) {
      return;
    }

    // All stats share the following attributes inherited from RTCStats
    is(stat.id, mapKey, stat.type + ".id is the same as the report key.");

    // timestamp
    ok(stat.timestamp >= 0, stat.type + ".timestamp is not less than 0");
    // If the timebase for the timestamp is not properly set the timestamp
    // will appear relative to the year 1970; Bug 1495446
    const date = new Date(stat.timestamp);
    ok(
      date.getFullYear() > 1970,
      `${stat.type}.timestamp is relative to current time, date=${date}`
    );

    //
    // RTCStreamStats attributes with common behavior
    //
    // inbound-rtp, outbound-rtp, remote-inbound-rtp, remote-outbound-rtp
    // inherit from RTCStreamStats
    if (
      [
        "inbound-rtp",
        "outbound-rtp",
        "remote-inbound-rtp",
        "remote-outbound-rtp",
      ].includes(stat.type)
    ) {
      const isRemote = stat.type.startsWith("remote-");
      //
      // Common RTCStreamStats fields
      //

      // SSRC
      ok(stat.ssrc, stat.type + ".ssrc has a value");

      // kind
      ok(
        ["audio", "video"].includes(stat.kind),
        stat.type + ".kind is 'audio' or 'video'"
      );

      // mediaType, renamed to kind but remains for backward compability.
      ok(
        ["audio", "video"].includes(stat.mediaType),
        stat.type + ".mediaType is 'audio' or 'video'"
      );

      ok(stat.kind == stat.mediaType, "kind equals legacy mediaType");

      // codecId
      ok(stat.codecId, `${stat.type}.codecId has a value`);
      ok(report.has(stat.codecId), `codecId ${stat.codecId} exists in report`);
      is(
        report.get(stat.codecId).type,
        "codec",
        `codecId ${stat.codecId} in report is codec type`
      );
      is(
        report.get(stat.codecId).mimeType.slice(0, 5),
        stat.kind,
        `codecId ${stat.codecId} in report is for a mimeType of the same ` +
          `media type as the referencing rtp stream stat`
      );

      // transportId
      ok(stat.transportId, `{stat.type}.transportId has a value`);
      ok(
        report.has(stat.transportId),
        `{stat.type} transportId ${stat.transportId} exists in report`
      );

      if (isRemote) {
        // local id
        if (stat.localId) {
          ok(
            report.has(stat.localId),
            `localId ${stat.localId} exists in report.`
          );
          is(
            report.get(stat.localId).ssrc,
            stat.ssrc,
            "remote ssrc and local ssrc match."
          );
          is(
            report.get(stat.localId).remoteId,
            stat.id,
            "local object has remote object as it's own remote object."
          );
        }
      } else {
        // remote id
        if (stat.remoteId) {
          ok(
            report.has(stat.remoteId),
            `remoteId ${stat.remoteId} exists in report.`
          );
          is(
            report.get(stat.remoteId).ssrc,
            stat.ssrc,
            "remote ssrc and local ssrc match."
          );
          is(
            report.get(stat.remoteId).localId,
            stat.id,
            "remote object has local object as it's own local object."
          );
        }
      }

      // nackCount
      if (stat.nackCount) {
        ok(
          stat.nackCount >= 0,
          `${stat.type}.nackCount is sane (${stat.kind}).`
        );
      }

      if (!isRemote && stat.inner.kind == "video") {
        // firCount
        ok(
          stat.firCount >= 0 && stat.firCount < 100,
          `${stat.type}.firCount is a sane number for a short ` +
            `${stat.kind} test. value=${stat.firCount}`
        );

        // pliCount
        ok(
          stat.pliCount >= 0 && stat.pliCount < 200,
          `${stat.type}.pliCount is a sane number for a short ` +
            `${stat.kind} test. value=${stat.pliCount}`
        );

        // qpSum
        if (stat.qpSum !== undefined) {
          ok(
            stat.qpSum >= 0,
            `${stat.type}.qpSum is at least 0 ` +
              `${stat.kind} test. value=${stat.qpSum}`
          );
        }
      } else {
        is(
          stat.qpSum,
          undefined,
          `${stat.type}.qpSum does not exist when stat.kind != video`
        );
      }
    }

    if (stat.type == "inbound-rtp") {
      receiving = true;

      //
      // Required fields
      //

      // trackIdentifier
      is(typeof stat.trackIdentifier, "string");
      isnot(stat.trackIdentifier, "");

      // mid
      ok(
        parseInt(stat.mid) >= 0,
        `${stat.type}.mid is a positive integer. value=${stat.mid}`
      );
      let inboundRtpMids = [];
      report.forEach(r => {
        if (r.type == "inbound-rtp") {
          inboundRtpMids.push(r.mid);
        }
      });
      is(
        inboundRtpMids.filter(mid => mid == stat.mid).length,
        1,
        `${stat.type}.mid is distinct. value=${
          stat.mid
        }, others=${JSON.stringify(inboundRtpMids)}`
      );

      // packetsReceived
      ok(
        stat.packetsReceived >= 0 && stat.packetsReceived < 10 ** 5,
        `${stat.type}.packetsReceived is a sane number for a short ` +
          `${stat.kind} test. value=${stat.packetsReceived}`
      );

      // packetsDiscarded
      ok(
        stat.packetsDiscarded >= 0 && stat.packetsDiscarded < 100,
        `${stat.type}.packetsDiscarded is sane number for a short test. ` +
          `value=${stat.packetsDiscarded}`
      );
      // bytesReceived
      ok(
        stat.bytesReceived >= 0 && stat.bytesReceived < 10 ** 9, // Not a magic number, just a guess
        `${stat.type}.bytesReceived is a sane number for a short ` +
          `${stat.kind} test. value=${stat.bytesReceived}`
      );

      // packetsLost
      ok(
        stat.packetsLost < 100,
        `${stat.type}.packetsLost is a sane number for a short ` +
          `${stat.kind} test. value=${stat.packetsLost}`
      );

      // This should be much lower for audio, TODO: Bug 1330575
      let expectedJitter = stat.kind == "video" ? 0.5 : 1;
      // jitter
      ok(
        stat.jitter < expectedJitter,
        `${stat.type}.jitter is sane number for a ${stat.kind} ` +
          `local only test. value=${stat.jitter}`
      );

      // lastPacketReceivedTimestamp
      ok(
        stat.lastPacketReceivedTimestamp !== undefined,
        `${stat.type}.lastPacketReceivedTimestamp has a value`
      );

      // headerBytesReceived
      ok(
        stat.headerBytesReceived >= 0 && stat.headerBytesReceived < 500000,
        `${stat.type}.headerBytesReceived is sane for a short test. ` +
          `value=${stat.headerBytesReceived}`
      );

      // estimatedPlayoutTimestamp
      if (stat.estimatedPlayoutTimestamp !== undefined) {
        ok(
          stat.estimatedPlayoutTimestamp < stat.timestamp + 100000,
          `${stat.type}.estimatedPlayoutTimestamp is not too far in the future`
        );
        ok(
          stat.estimatedPlayoutTimestamp > stat.timestamp - 100000,
          `${stat.type}.estimatedPlayoutTimestamp is not too far in the past`
        );
      }

      // jitterBufferEmittedCount
      ok(
        stat.jitterBufferEmittedCount > 0,
        `${stat.type}.jitterBufferEmittedCount is a sane number for a short ` +
          `${stat.kind} test. value=${stat.jitterBufferEmittedCount}`
      );

      // jitterBufferTargetDelay
      ok(
        stat.jitterBufferTargetDelay >= 0,
        `${stat.type}.jitterBufferTargetDelay is a sane number for a short ` +
          `${stat.kind} test. value=${stat.jitterBufferTargetDelay}`
      );

      // jitterBufferMinimumDelay
      ok(
        stat.jitterBufferMinimumDelay >= 0,
        `${stat.type}.jitterBufferMinimumDelay is a sane number for a short ` +
          `${stat.kind} test. value=${stat.jitterBufferMinimumDelay}`
      );

      // jitterBufferDelay
      let avgJitterBufferDelay =
        stat.jitterBufferDelay / stat.jitterBufferEmittedCount;
      ok(
        avgJitterBufferDelay > 0 && avgJitterBufferDelay < 10,
        `${stat.type}.jitterBufferDelay is a sane number for a short ` +
          `${stat.kind} test. value=${stat.jitterBufferDelay}/${stat.jitterBufferEmittedCount}=${avgJitterBufferDelay}`
      );

      //
      // Optional fields
      //

      //
      // Local audio only stats
      //
      if (stat.inner.kind != "audio") {
        expectations.localAudioOnly.forEach(field => {
          ok(
            stat[field] === undefined,
            `${stat.type} does not have field ${field}` +
              ` when kind is not 'audio'`
          );
        });
      } else {
        expectations.localAudioOnly.forEach(field => {
          ok(
            stat.inner[field] !== undefined,
            stat.type + " has field " + field + " when kind is video"
          );
        });
        // totalSamplesReceived
        ok(
          stat.totalSamplesReceived > 1000,
          `${stat.type}.totalSamplesReceived is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalSamplesReceived}`
        );

        // fecPacketsReceived
        ok(
          stat.fecPacketsReceived >= 0 && stat.fecPacketsReceived < 10 ** 5,
          `${stat.type}.fecPacketsReceived is a sane number for a short ` +
            `${stat.kind} test. value=${stat.fecPacketsReceived}`
        );

        // fecPacketsDiscarded
        ok(
          stat.fecPacketsDiscarded >= 0 && stat.fecPacketsDiscarded < 100,
          `${stat.type}.fecPacketsDiscarded is sane number for a short test. ` +
            `value=${stat.fecPacketsDiscarded}`
        );
        // concealedSamples
        ok(
          stat.concealedSamples >= 0 &&
            stat.concealedSamples <= stat.totalSamplesReceived,
          `${stat.type}.concealedSamples is a sane number for a short ` +
            `${stat.kind} test. value=${stat.concealedSamples}`
        );

        // silentConcealedSamples
        ok(
          stat.silentConcealedSamples >= 0 &&
            stat.silentConcealedSamples <= stat.concealedSamples,
          `${stat.type}.silentConcealedSamples is a sane number for a short ` +
            `${stat.kind} test. value=${stat.silentConcealedSamples}`
        );

        // concealmentEvents
        ok(
          stat.concealmentEvents >= 0 &&
            stat.concealmentEvents <= stat.packetsReceived,
          `${stat.type}.concealmentEvents is a sane number for a short ` +
            `${stat.kind} test. value=${stat.concealmentEvents}`
        );

        // insertedSamplesForDeceleration
        ok(
          stat.insertedSamplesForDeceleration >= 0 &&
            stat.insertedSamplesForDeceleration <= stat.totalSamplesReceived,
          `${stat.type}.insertedSamplesForDeceleration is a sane number for a short ` +
            `${stat.kind} test. value=${stat.insertedSamplesForDeceleration}`
        );

        // removedSamplesForAcceleration
        ok(
          stat.removedSamplesForAcceleration >= 0 &&
            stat.removedSamplesForAcceleration <= stat.totalSamplesReceived,
          `${stat.type}.removedSamplesForAcceleration is a sane number for a short ` +
            `${stat.kind} test. value=${stat.removedSamplesForAcceleration}`
        );

        // audioLevel
        ok(
          stat.audioLevel >= 0 && stat.audioLevel <= 128,
          `${stat.type}.bytesReceived is a sane number for a short ` +
            `${stat.kind} test. value=${stat.audioLevel}`
        );

        // totalAudioEnergy
        ok(
          stat.totalAudioEnergy >= 0 && stat.totalAudioEnergy <= 128,
          `${stat.type}.totalAudioEnergy is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalAudioEnergy}`
        );

        // totalSamplesDuration
        ok(
          stat.totalSamplesDuration >= 0 && stat.totalSamplesDuration <= 300,
          `${stat.type}.totalSamplesDuration is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalSamplesDuration}`
        );
      }

      // rtxSsrc and the retransmitted counters are only present when rtx is
      // negotiated, which is not always the case (e.g. when the offer only
      // contains a single non-rtx payload type). Access them unconditionally
      // so they are marked as tested, but only assert sanity when present.
      if (stat.rtxSsrc !== undefined) {
        ok(
          stat.rtxSsrc > 0,
          `${stat.type}.rtxSsrc is a sane ssrc. value=${stat.rtxSsrc}`
        );

        ok(
          stat.retransmittedPacketsReceived >= 0 &&
            stat.retransmittedPacketsReceived < 10 ** 5,
          `${stat.type}.retransmittedPacketsReceived is a sane number for a ` +
            `short ${stat.kind} test. value=${stat.retransmittedPacketsReceived}`
        );

        ok(
          stat.retransmittedBytesReceived >= 0 &&
            stat.retransmittedBytesReceived < 10 ** 9,
          `${stat.type}.retransmittedBytesReceived is a sane number for a ` +
            `short ${stat.kind} test. value=${stat.retransmittedBytesReceived}`
        );
      } else {
        // In the non-rtx case these counters will be undefined.
        ok(
          stat.retransmittedPacketsReceived === undefined,
          `${stat.type}.retransmittedPacketsReceived is undefined if rtx is not negotiated.` +
            `value=${stat.retransmittedPacketsReceived}`
        );

        ok(
          stat.retransmittedBytesReceived === undefined &&
            `${stat.type}.retransmittedBytesReceived is undefined if rtx is not negotiated.` +
              `value=${stat.retransmittedBytesReceived}`
        );
      }

      //
      // Local video only stats
      //
      if (stat.inner.kind != "video") {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat[field] === undefined,
            `${stat.type} does not have field ${field}` +
              ` when kind is not 'video'`
          );
        });
      } else {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat.inner[field] !== undefined,
            stat.type + " has field " + field + " when kind is video"
          );
        });

        // discardedPackets
        ok(
          stat.discardedPackets < 100,
          `${stat.type}.discardedPackets is a sane number for a short test. ` +
            `value=${stat.discardedPackets}`
        );
        // framesPerSecond
        ok(
          stat.framesPerSecond > 0 && stat.framesPerSecond < 70,
          `${stat.type}.framesPerSecond is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesPerSecond}`
        );

        // framesDecoded
        ok(
          stat.framesDecoded > 0 && stat.framesDecoded < 1000000,
          `${stat.type}.framesDecoded is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesDecoded}`
        );

        // keyFramesDecoded
        ok(
          stat.keyFramesDecoded >= 0 && stat.keyFramesDecoded < 1000000,
          `${stat.type}.keyFramesDecoded is a sane number for a short ` +
            `${stat.kind} test. value=${stat.keyFramesDecoded}`
        );

        // framesDropped
        ok(
          stat.framesDropped >= 0 && stat.framesDropped < 100,
          `${stat.type}.framesDropped is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesDropped}`
        );

        // frameWidth
        ok(
          stat.frameWidth > 0 && stat.frameWidth < 100000,
          `${stat.type}.frameWidth is a sane number for a short ` +
            `${stat.kind} test. value=${stat.frameWidth}`
        );

        // frameHeight
        ok(
          stat.frameHeight > 0 && stat.frameHeight < 100000,
          `${stat.type}.frameHeight is a sane number for a short ` +
            `${stat.kind} test. value=${stat.frameHeight}`
        );

        // totalDecodeTime
        ok(
          stat.totalDecodeTime >= 0 && stat.totalDecodeTime < 300,
          `${stat.type}.totalDecodeTime is sane for a short test. ` +
            `value=${stat.totalDecodeTime}`
        );

        // totalProcessingDelay
        ok(
          stat.totalProcessingDelay <
            (navigator.userAgent.includes("Android") ? 2000 : 1000),
          `${stat.type}.totalProcessingDelay is sane number for a short test ` +
            `local only test. value=${stat.totalProcessingDelay}`
        );

        // totalInterFrameDelay
        ok(
          stat.totalInterFrameDelay >= 0 && stat.totalInterFrameDelay < 1000,
          `${stat.type}.totalInterFrameDelay is sane for a short test. ` +
            `value=${stat.totalInterFrameDelay}`
        );

        // totalSquaredInterFrameDelay
        ok(
          stat.totalSquaredInterFrameDelay >= 0 &&
            stat.totalSquaredInterFrameDelay < 10000,
          `${stat.type}.totalSquaredInterFrameDelay is sane for a short test. ` +
            `value=${stat.totalSquaredInterFrameDelay}`
        );

        // pauseCount
        ok(
          stat.pauseCount >= 0 && stat.pauseCount < 100,
          `${stat.type}.pauseCount is a sane number for a short ` +
            `${stat.kind} test. value=${stat.pauseCount}`
        );

        // totalPausesDuration
        ok(
          stat.totalPausesDuration >= 0 && stat.totalPausesDuration < 10000,
          `${stat.type}.totalPausesDuration is sane for a short test. ` +
            `value=${stat.totalPausesDuration}`
        );

        // freezeCount
        ok(
          stat.freezeCount >= 0 && stat.freezeCount < 100,
          `${stat.type}.freezeCount is a sane number for a short ` +
            `${stat.kind} test. value=${stat.freezeCount}`
        );

        // totalFreezesDuration
        ok(
          stat.totalFreezesDuration >= 0 && stat.totalFreezesDuration < 10000,
          `${stat.type}.totalFreezesDuration is sane for a short test. ` +
            `value=${stat.totalFreezesDuration}`
        );

        // framesReceived
        ok(
          stat.framesReceived >= 0 && stat.framesReceived < 100000,
          `${stat.type}.framesReceived is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesReceived}`
        );

        // framesAssembledFromMultiplePackets
        ok(
          stat.framesAssembledFromMultiplePackets >= 0 &&
            stat.framesAssembledFromMultiplePackets < 200,
          `${stat.type}.framesAssembledFromMultiplePackets is a sane number ` +
            `for a short ${stat.kind} test.` +
            `value=${stat.framesAssembledFromMultiplePackets}`
        );

        // totalAssemblyTime
        ok(
          stat.totalAssemblyTime >= 0 && stat.totalAssemblyTime < 10000,
          `${stat.type}.totalAssemblyTime is sane for a short test. ` +
            `value=${stat.totalAssemblyTime}`
        );
      }
    } else if (stat.type == "remote-inbound-rtp") {
      // roundTripTime
      ok(
        stat.roundTripTime >= 0,
        `${stat.type}.roundTripTime is sane with` +
          `value of: ${stat.roundTripTime} (${stat.kind})`
      );
      //
      // Required fields
      //

      // packetsLost
      ok(
        stat.packetsLost < 100,
        `${stat.type}.packetsLost is a sane number for a short ` +
          `${stat.kind} test. value=${stat.packetsLost}`
      );

      // jitter
      ok(
        stat.jitter >= 0,
        `${stat.type}.jitter is sane number (${stat.kind}). ` +
          `value=${stat.jitter}`
      );

      //
      // Optional fields
      //

      // packetsReceived
      if (stat.packetsReceived) {
        ok(
          stat.packetsReceived >= 0 && stat.packetsReceived < 10 ** 5,
          `${stat.type}.packetsReceived is a sane number for a short ` +
            `${stat.kind} test. value=${stat.packetsReceived}`
        );
      }

      // totalRoundTripTime
      ok(
        stat.totalRoundTripTime < 50000,
        `${stat.type}.totalRoundTripTime is a sane number for a short ` +
          `${stat.kind} test. value=${stat.totalRoundTripTime}`
      );

      // fractionLost
      ok(
        stat.fractionLost < 0.2,
        `${stat.type}.fractionLost is a sane number for a short ` +
          `${stat.kind} test. value=${stat.fractionLost}`
      );

      // roundTripTimeMeasurements
      ok(
        stat.roundTripTimeMeasurements >= 1 &&
          stat.roundTripTimeMeasurements < 500,
        `${stat.type}.roundTripTimeMeasurements is a sane number for a short ` +
          `${stat.kind} test. value=${stat.roundTripTimeMeasurements}`
      );
    } else if (stat.type == "outbound-rtp") {
      sending = true;

      //
      // Required fields
      //

      // mid
      ok(
        parseInt(stat.mid) >= 0,
        `${stat.type}.mid a positive integer. value=${stat.mid}`
      );

      // packetsSent
      ok(
        stat.packetsSent > 0 && stat.packetsSent < 10000,
        `${stat.type}.packetsSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.packetsSent}`
      );

      // bytesSent
      const audio1Min = 16000 * 60; // 128kbps
      const video1Min = 250000 * 60; // 2Mbps
      ok(
        stat.bytesSent > 0 &&
          stat.bytesSent < (stat.kind == "video" ? video1Min : audio1Min),
        `${stat.type}.bytesSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.bytesSent}`
      );

      // headerBytesSent
      ok(
        stat.headerBytesSent > 0 &&
          stat.headerBytesSent < (stat.kind == "video" ? video1Min : audio1Min),
        `${stat.type}.headerBytesSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.headerBytesSent}`
      );

      // retransmittedPacketsSent
      ok(
        stat.retransmittedPacketsSent >= 0 &&
          stat.retransmittedPacketsSent <
            (stat.kind == "video" ? video1Min : audio1Min),
        `${stat.type}.retransmittedPacketsSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.retransmittedPacketsSent}`
      );

      // retransmittedBytesSent
      ok(
        stat.retransmittedBytesSent >= 0 &&
          stat.retransmittedBytesSent <
            (stat.kind == "video" ? video1Min : audio1Min),
        `${stat.type}.retransmittedBytesSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.retransmittedBytesSent}`
      );

      //
      // Optional fields
      //

      // rid
      if (stat.kind == "audio") {
        ok(
          stat.rid === undefined,
          `${stat.type}.rid" MUST NOT exist for audio. value=${stat.rid}`
        );
        ok(
          stat.rtxSsrc === undefined,
          `${stat.type}.rtxSsrc" MUST NOT exist for audio. value=${stat.rtxSsrc}`
        );
        ok(
          stat.retransmittedBytesSent === 0,
          `${stat.type}.retransmittedBytesSent is 0 audio. value=${stat.retransmittedBytesSent}`
        );
        ok(
          stat.retransmittedPacketsSent === 0,
          `${stat.type}.retransmittedPacketsSent is 0 for audio. value=${stat.retransmittedPacketsSent}`
        );
      } else {
        let numSendVideoStreamsForMid = 0;
        report.forEach(r => {
          if (
            r.type == "outbound-rtp" &&
            r.kind == "video" &&
            r.mid == stat.mid
          ) {
            numSendVideoStreamsForMid += 1;
          }
        });
        if (numSendVideoStreamsForMid == 1) {
          is(
            stat.rid,
            undefined,
            `${stat.type}.rid" does not exist for singlecast video. value=${stat.rid}`
          );
        } else {
          isnot(
            stat.rid,
            undefined,
            `${stat.type}.rid" does exist for simulcast video. value=${stat.rid}`
          );
        }
        // rtxSsrc is only present when rtx is negotiated, which is not always
        // the case (e.g. simulcast offers do not negotiate rtx). When present
        // it must be a sane ssrc.
        if (stat.rtxSsrc !== undefined) {
          ok(
            stat.rtxSsrc > 0,
            `${stat.type}.rtxSsrc is a sane ssrc. value=${stat.rtxSsrc}`
          );
        }
      }

      // targetBitrate is only present once the bitrate allocator has told the
      // encoder what to aim for, so it may be absent early in a call.
      if (stat.targetBitrate !== undefined) {
        ok(
          stat.targetBitrate > 0,
          `${stat.type}.targetBitrate is a sane number for a short ` +
            `${stat.kind} test. value=${stat.targetBitrate}`
        );
      }

      // qpSum
      // This is supported for all of our vpx codecs and AV1 (on the encode
      // side, see bug 1519590)
      const mimeType = report.get(stat.codecId).mimeType;
      if (mimeType.includes("VP") || mimeType.includes("AV1")) {
        ok(
          stat.qpSum >= 0,
          `${stat.type}.qpSum is a sane number (${stat.kind}) ` +
            `for ${report.get(stat.codecId).mimeType}. value=${stat.qpSum}`
        );
      } else if (mimeType.includes("H264")) {
        // OpenH264 encoder records QP so we check for either condition.
        if (!stat.qpSum && !("qpSum" in stat)) {
          ok(
            !stat.qpSum && !("qpSum" in stat),
            `${stat.type}.qpSum absent for ${report.get(stat.codecId).mimeType}`
          );
        } else {
          ok(
            stat.qpSum >= 0,
            `${stat.type}.qpSum is a sane number (${stat.kind}) ` +
              `for ${report.get(stat.codecId).mimeType}. value=${stat.qpSum}`
          );
        }
      } else {
        ok(
          !stat.qpSum && !("qpSum" in stat),
          `${stat.type}.qpSum absent for ${report.get(stat.codecId).mimeType}`
        );
      }

      //
      // Local video only stats
      //
      if (stat.inner.kind != "video") {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat[field] === undefined,
            `${stat.type} does not have field ` +
              `${field} when kind is not 'video'`
          );
        });
      } else {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat.inner[field] !== undefined,
            `${stat.type} has field ` +
              `${field} when kind is video and isRemote is false`
          );
        });

        // framesEncoded
        ok(
          stat.framesEncoded >= 0 && stat.framesEncoded < 100000,
          `${stat.type}.framesEncoded is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesEncoded}`
        );

        // keyFramesEncoded
        ok(
          stat.keyFramesEncoded >= 0 && stat.keyFramesEncoded < 1000000,
          `${stat.type}.keyFramesEncoded is a sane number for a short ` +
            `${stat.kind} test. value=${stat.keyFramesEncoded}`
        );
        ok(
          stat.keyFramesEncoded <= stat.framesEncoded,
          `${stat.type}.keyFramesEncoded is less than or equal to ${stat.type}.framesEncoded`
        );

        // frameWidth
        ok(
          stat.frameWidth >= 0 && stat.frameWidth < 100000,
          `${stat.type}.frameWidth is a sane number for a short ` +
            `${stat.kind} test. value=${stat.frameWidth}`
        );

        // frameHeight
        ok(
          stat.frameHeight >= 0 && stat.frameHeight < 100000,
          `${stat.type}.frameHeight is a sane number for a short ` +
            `${stat.kind} test. value=${stat.frameHeight}`
        );

        // framesPerSecond
        ok(
          stat.framesPerSecond >= 0 && stat.framesPerSecond < 60,
          `${stat.type}.framesPerSecond is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesPerSecond}`
        );

        // framesSent
        ok(
          stat.framesSent >= 0 && stat.framesSent < 100000,
          `${stat.type}.framesSent is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesSent}`
        );

        // hugeFramesSent
        ok(
          stat.hugeFramesSent >= 0 && stat.hugeFramesSent < 100000,
          `${stat.type}.hugeFramesSent is a sane number for a short ` +
            `${stat.kind} test. value=${stat.hugeFramesSent}`
        );

        // totalEncodeTime
        ok(
          stat.totalEncodeTime >= 0,
          `${stat.type}.totalEncodeTime is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalEncodeTime}`
        );
      }
    } else if (stat.type == "remote-outbound-rtp") {
      //
      // Required fields
      //

      // packetsSent
      ok(
        stat.packetsSent > 0 && stat.packetsSent < 10000,
        `${stat.type}.packetsSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.packetsSent}`
      );

      // bytesSent
      const audio1Min = 16000 * 60; // 128kbps
      const video1Min = 250000 * 60; // 2Mbps
      ok(
        stat.bytesSent > 0 &&
          stat.bytesSent < (stat.kind == "video" ? video1Min : audio1Min),
        `${stat.type}.bytesSent is a sane number for a short ` +
          `${stat.kind} test. value=${stat.bytesSent}`
      );

      ok(
        stat.remoteTimestamp !== undefined,
        `${stat.type}.remoteTimestamp ` + `is not undefined (${stat.kind})`
      );
      const ageSeconds = (stat.timestamp - stat.remoteTimestamp) / 1000;
      // remoteTimestamp is exact (so it can be mapped to a packet), whereas
      // timestamp has reduced precision. It is possible that
      // remoteTimestamp occurs a millisecond into the future from
      // timestamp. We also subtract half a millisecond when reducing
      // precision on libwebrtc timestamps, to counteract the potential
      // rounding up that libwebrtc may do since it tends to round its
      // internal timestamps to whole milliseconds. In the worst case
      // remoteTimestamp may therefore occur 2 milliseconds ahead of
      // timestamp.
      ok(
        ageSeconds >= -0.002 && ageSeconds < 30,
        `${stat.type}.remoteTimestamp is on the same timeline as ` +
          `${stat.type}.timestamp, and no older than 30 seconds. ` +
          `difference=${ageSeconds}s`
      );
    } else if (stat.type == "media-source") {
      // trackIdentifier
      is(typeof stat.trackIdentifier, "string");
      isnot(stat.trackIdentifier, "");

      // kind
      is(typeof stat.kind, "string");
      ok(stat.kind == "audio" || stat.kind == "video");
      if (stat.inner.kind == "video") {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat.inner[field] !== undefined,
            `${stat.type} has field ` +
              `${field} when kind is video and isRemote is false`
          );
        });

        // frames
        ok(
          stat.frames >= 0 && stat.frames < 100000,
          `${stat.type}.frames is a sane number for a short ` +
            `${stat.kind} test. value=${stat.frames}`
        );

        // framesPerSecond
        ok(
          stat.framesPerSecond >= 0 && stat.framesPerSecond < 100,
          `${stat.type}.framesPerSecond is a sane number for a short ` +
            `${stat.kind} test. value=${stat.framesPerSecond}`
        );

        // width
        ok(
          stat.width >= 0 && stat.width < 1000000,
          `${stat.type}.width is a sane number for a ` +
            `${stat.kind} test. value=${stat.width}`
        );

        // height
        ok(
          stat.height >= 0 && stat.height < 1000000,
          `${stat.type}.height is a sane number for a ` +
            `${stat.kind} test. value=${stat.height}`
        );
      } else {
        expectations.localVideoOnly.forEach(field => {
          ok(
            stat[field] === undefined,
            `${stat.type} does not have field ` +
              `${field} when kind is not 'video'`
          );
        });
      }
      if (stat.inner.kind == "audio") {
        expectations.localAudioOnly.forEach(field => {
          ok(
            stat.inner[field] !== undefined,
            `${stat.type} has field ${field} when kind is audio`
          );
        });

        // audioLevel
        ok(
          stat.audioLevel >= 0 && stat.audioLevel <= 1,
          `${stat.type}.audioLevel is a sane number for a ` +
            `${stat.kind} test. value=${stat.audioLevel}`
        );

        // totalSamplesDuration
        ok(
          stat.totalSamplesDuration >= 0 && stat.totalSamplesDuration < 300,
          `${stat.type}.totalSamplesDuration is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalSamplesDuration}`
        );

        // totalAudioEnergy
        ok(
          stat.totalAudioEnergy >= 0 &&
            stat.totalAudioEnergy <= stat.totalSamplesDuration,
          `${stat.type}.totalAudioEnergy is a sane number for a short ` +
            `${stat.kind} test. value=${stat.totalAudioEnergy}`
        );
      } else {
        expectations.localAudioOnly.forEach(field => {
          ok(
            stat[field] === undefined,
            `${stat.type} does not have field ` +
              `${field} when kind is not 'audio'`
          );
        });
      }
    } else if (stat.type == "codec") {
      //
      // Required fields
      //

      // mimeType & payloadType
      switch (stat.mimeType) {
        case "audio/opus":
          is(stat.payloadType, 109, "codec.payloadType for opus");
          break;
        case "video/VP8":
          is(stat.payloadType, 120, "codec.payloadType for VP8");
          break;
        case "video/VP9":
          is(stat.payloadType, 121, "codec.payloadType for VP9");
          break;
        case "video/H264":
          ok(
            stat.payloadType == 97 ||
              stat.payloadType == 126 ||
              stat.payloadType == 103 ||
              stat.payloadType == 105,
            `codec.payloadType for H264 was ${stat.payloadType}, exp. 97, 126, 103, or 105`
          );
          break;
        case "video/AV1":
          is(stat.payloadType, 99, "codec.payloadType for AV1");
          break;
        default:
          ok(
            false,
            `Unexpected codec.mimeType ${stat.mimeType} for payloadType ` +
              `${stat.payloadType}`
          );
          break;
      }

      // transportId
      ok(stat.transportId, `{stat.type}.transportId has a value`);
      ok(
        report.has(stat.transportId),
        `{stat.type} transportId ${stat.transportId} exists in report`
      );

      // clockRate
      if (stat.mimeType.startsWith("audio")) {
        is(stat.clockRate, 48000, "codec.clockRate for audio/opus");
      } else if (stat.mimeType.startsWith("video")) {
        is(stat.clockRate, 90000, "codec.clockRate for video");
      }

      // sdpFmtpLine
      // (not technically mandated by spec, but expected here)
      // AV1 has no required parameters, so don't require sdpFmtpLine for it.
      if (stat.mimeType != "video/AV1") {
        ok(stat.sdpFmtpLine, "codec.sdp FmtpLine is set");
      }
      const opusParams = [
        "maxplaybackrate",
        "maxaveragebitrate",
        "usedtx",
        "stereo",
        "useinbandfec",
        "cbr",
        "ptime",
        "minptime",
        "maxptime",
      ];
      const vpxParams = ["max-fs", "max-fr"];
      const h264Params = [
        "packetization-mode",
        "level-asymmetry-allowed",
        "profile-level-id",
        "max-fs",
        "max-cpb",
        "max-dpb",
        "max-br",
        "max-mbps",
      ];
      // AV1 parameters:
      //  https://aomediacodec.github.io/av1-rtp-spec/#sdp-mapping-media-subtype
      const av1Params = ["profile", "level-idx", "tier"];
      // Check that the parameters are as expected. AV1 may have no parameters.
      for (const param of (stat.sdpFmtpLine || "").split(";")) {
        const [key, value] = param.split("=");
        if (stat.payloadType == 99) {
          // AV1 might not have any parameters, if it does make sure they are as expected.
          if (key) {
            ok(
              av1Params.includes(key),
              `codec.sdpFmtpLine param ${key}=${value} for AV1`
            );
          }
        } else if (stat.payloadType == 109) {
          ok(
            opusParams.includes(key),
            `codec.sdpFmtpLine param ${key}=${value} for opus`
          );
        } else if (stat.payloadType == 120 || stat.payloadType == 121) {
          ok(
            vpxParams.includes(key),
            `codec.sdpFmtpLine param ${key}=${value} for VPx`
          );
        } else if (stat.payloadType == 97 || stat.payloadType == 126) {
          ok(
            h264Params.includes(key),
            `codec.sdpFmtpLine param ${key}=${value} for H264`
          );
          if (key == "packetization-mode") {
            if (stat.payloadType == 97) {
              is(value, "0", "codec.sdpFmtpLine: H264 (97) packetization-mode");
            } else if (stat.payloadType == 126) {
              is(
                value,
                "1",
                "codec.sdpFmtpLine: H264 (126) packetization-mode"
              );
            }
          }
          if (key == "profile-level-id") {
            is(value, "42e01f", "codec.sdpFmtpLine: H264 profile-level-id");
          }
        }
      }

      //
      // Optional fields
      //

      // codecType
      ok(
        !Object.keys(stat).includes("codecType") ||
          stat.codecType == "encode" ||
          stat.codecType == "decode",
        "codec.codecType (${codec.codecType}) is an expected value or absent"
      );
      let numRecvStreams = 0;
      let numSendStreams = 0;
      const counts = {
        "inbound-rtp": 0,
        "outbound-rtp": 0,
        "remote-inbound-rtp": 0,
        "remote-outbound-rtp": 0,
      };
      const [kind] = stat.mimeType.split("/");
      report.forEach(other => {
        if (other.type == "inbound-rtp" && other.kind == kind) {
          numRecvStreams += 1;
        } else if (other.type == "outbound-rtp" && other.kind == kind) {
          numSendStreams += 1;
        }
        if (other.codecId == stat.id) {
          counts[other.type] += 1;
        }
      });
      const expectedCounts = {
        encode: {
          "inbound-rtp": 0,
          "outbound-rtp": numSendStreams,
          "remote-inbound-rtp": numSendStreams,
          "remote-outbound-rtp": 0,
        },
        decode: {
          "inbound-rtp": numRecvStreams,
          "outbound-rtp": 0,
          "remote-inbound-rtp": 0,
          "remote-outbound-rtp": numRecvStreams,
        },
        absent: {
          "inbound-rtp": numRecvStreams,
          "outbound-rtp": numSendStreams,
          "remote-inbound-rtp": numSendStreams,
          "remote-outbound-rtp": numRecvStreams,
        },
      };
      // Note that the logic above assumes at most one sender and at most one
      // receiver was used to generate this stats report. If more senders or
      // receivers are present, they'd be referring to not only this codec stat,
      // skewing `numSendStreams` and `numRecvStreams` above.
      // This could be fixed when we support `senderId` and `receiverId` in
      // RTCOutboundRtpStreamStats and RTCInboundRtpStreamStats respectively.
      for (const [key, value] of Object.entries(counts)) {
        is(
          value,
          expectedCounts[stat.codecType || "absent"][key],
          `codec.codecType ${stat.codecType || "absent"} ref from ${key} stat`
        );
      }

      // channels
      if (stat.mimeType.startsWith("audio")) {
        ok(stat.channels, "codec.channels should exist for audio");
        if (stat.channels) {
          if (stat.sdpFmtpLine.includes("stereo=1")) {
            is(stat.channels, 2, "codec.channels for stereo audio");
          } else {
            is(stat.channels, 1, "codec.channels for mono audio");
          }
        }
      } else {
        ok(!stat.channels, "codec.channels should not exist for video");
      }
    } else if (stat.type == "candidate-pair") {
      info("candidate-pair is: " + JSON.stringify(stat));
      //
      // Required fields
      //

      // transportId
      ok(stat.transportId, `{stat.type}.transportId has a value`);
      ok(
        report.has(stat.transportId),
        `{stat.type} transportId ${stat.transportId} exists in report`
      );

      // localCandidateId
      ok(
        stat.localCandidateId,
        `${stat.type}.localCandidateId has a value. value=` +
          `${stat.localCandidateId} (${stat.kind})`
      );

      // remoteCandidateId
      ok(
        stat.remoteCandidateId,
        `${stat.type}.remoteCandidateId has a value. value=` +
          `${stat.remoteCandidateId} (${stat.kind})`
      );

      // priority
      ok(
        stat.priority,
        `${stat.type}.priority has a value. value=` +
          `${stat.priority} (${stat.kind})`
      );

      // readable
      ok(
        stat.readable,
        `${stat.type}.readable is true. value=${stat.readable} ` +
          `(${stat.kind})`
      );

      // writable
      ok(
        stat.writable,
        `${stat.type}.writable is true. value=${stat.writable} ` +
          `(${stat.kind})`
      );

      // totalRoundTripTime
      ok(
        stat.totalRoundTripTime !== undefined && stat.totalRoundTripTime >= 0,
        `${stat.type}.totalRoundTripTime has a value. value=` +
          `${stat.totalRoundTripTime} (${stat.kind})`
      );

      // currentRoundTripTime
      ok(
        stat.currentRoundTripTime !== undefined &&
          stat.currentRoundTripTime >= 0,
        `${stat.type}.currentRoundTripTime has a value. value=` +
          `${stat.currentRoundTripTime} (${stat.kind})`
      );

      // responsesReceived
      ok(
        stat.responsesReceived !== undefined && stat.responsesReceived >= 0,
        `${stat.type}.responsesReceived has a value. value=` +
          `${stat.responsesReceived} (${stat.kind})`
      );

      const stateValues = [
        "frozen",
        "waiting",
        "in-progress",
        "failed",
        "succeeded",
        "cancelled",
      ];
      ok(
        stateValues.includes(stat.state),
        `${stat.type}.state '${stat.state}' not in ${stateValues}`
      );

      // state
      if (
        stat.state == "succeeded" &&
        stat.selected !== undefined &&
        stat.selected
      ) {
        info("candidate-pair state is succeeded and selected is true");
        // nominated
        ok(
          stat.nominated,
          `${stat.type}.nominated is true. value=${stat.nominated} ` +
            `(${stat.kind})`
        );

        const sentExpectation = sending ? 100 : 0;

        // bytesSent
        ok(
          stat.bytesSent >= sentExpectation,
          `${stat.type}.bytesSent is a sane number (>${sentExpectation}) if media is flowing. ` +
            `value=${stat.bytesSent}`
        );

        const recvExpectation = receiving ? 100 : 0;

        // bytesReceived
        ok(
          stat.bytesReceived >= recvExpectation,
          `${stat.type}.bytesReceived is a sane number (>${recvExpectation}) if media is flowing. ` +
            `value=${stat.bytesReceived}`
        );

        // packetsSent
        ok(
          stat.packetsSent > 0 && stat.packetsSent < 10000,
          `${stat.type}.packetsSent is a sane number for a short ` +
            `test. value=${stat.packetsSent}`
        );

        ok(
          stat.packetsReceived >= 0 && stat.packetsReceived < 10000,
          `${stat.type}.packetsReceived is a sane number for a short ` +
            `test. value=${stat.packetsReceived}`
        );

        // lastPacketSentTimestamp
        ok(
          stat.lastPacketSentTimestamp,
          `${stat.type}.lastPacketSentTimestamp has a value. value=` +
            `${stat.lastPacketSentTimestamp} (${stat.kind})`
        );

        // lastPacketReceivedTimestamp
        ok(
          stat.lastPacketReceivedTimestamp,
          `${stat.type}.lastPacketReceivedTimestamp has a value. value=` +
            `${stat.lastPacketReceivedTimestamp} (${stat.kind})`
        );
      } else {
        info("candidate-pair is _not_ both state == succeeded and selected");
        // nominated
        ok(
          stat.nominated !== undefined,
          `${stat.type}.nominated exists. value=${stat.nominated} ` +
            `(${stat.kind})`
        );
        ok(
          stat.bytesSent !== undefined,
          `${stat.type}.bytesSent exists. value=${stat.bytesSent} ` +
            `(${stat.kind})`
        );
        ok(
          stat.bytesReceived !== undefined,
          `${stat.type}.bytesReceived exists. value=${stat.bytesReceived} ` +
            `(${stat.kind})`
        );
        ok(
          stat.lastPacketSentTimestamp !== undefined,
          `${stat.type}.lastPacketSentTimestamp exists. value=` +
            `${stat.lastPacketSentTimestamp} (${stat.kind})`
        );
        ok(
          stat.lastPacketReceivedTimestamp !== undefined,
          `${stat.type}.lastPacketReceivedTimestamp exists. value=` +
            `${stat.lastPacketReceivedTimestamp} (${stat.kind})`
        );
      }

      //
      // Optional fields
      //
      // selected
      ok(
        stat.selected === undefined ||
          (stat.state == "succeeded" && stat.selected) ||
          !stat.selected,
        `${stat.type}.selected is undefined, true when state is succeeded, ` +
          `or false. value=${stat.selected} (${stat.kind})`
      );
    } else if (stat.type === "transport") {
      info(`transport is ${JSON.stringify(stat)}`);

      // packetsSent
      ok(
        stat.packetsSent > 0 && stat.packetsSent < 10000,
        `${stat.type}.packetsSent is a sane number for a short test. value=${stat.packetsSent}`
      );

      // packetsReceived
      ok(
        stat.packetsReceived > 0 && stat.packetsReceived < 10000,
        `${stat.type}.packetsReceived is a sane number for a short test. value=${stat.packetsReceived}`
      );

      // bytesSent
      const transport1Min = 250000 * 60; // 2Mbps
      ok(
        stat.bytesSent > 0 && stat.bytesSent < transport1Min,
        `${stat.type}.bytesSent is a sane number for a short test. value=${stat.bytesSent}`
      );

      // bytesReceived
      ok(
        stat.bytesReceived >= 0 && stat.bytesReceived < 10 ** 9, // Not a magic number, just a guess
        `${stat.type}.bytesReceived is a sane number for a short test. value=${stat.bytesReceived}`
      );

      // iceRole
      is(typeof stat.iceRole, "string", `${stat.type}.iceRole is a string`);
      ok(
        ["unknown", "controlling", "controlled"].includes(stat.iceRole),
        `${stat.type}.iceRole has a valid value. value=${stat.iceRole}`
      );

      // iceLocalUsernameFragment
      is(
        typeof stat.iceLocalUsernameFragment,
        "string",
        `${stat.type}.iceLocalUsernameFragment is a string`
      );
      ok(
        stat.iceLocalUsernameFragment.length >= 4 &&
          stat.iceLocalUsernameFragment.length <= 256,
        `${stat.type}.iceLocalUsernameFragment has a valid length`
      );

      // dtlsState
      is(typeof stat.dtlsState, "string", `${stat.type}.dtlsState is a string`);
      ok(
        ["new", "connecting", "connected", "closed", "failed"].includes(
          stat.dtlsState
        ),
        `${stat.type}.dtlsState has a valid value. value=${stat.dtlsState}`
      );

      // iceState
      is(typeof stat.iceState, "string", `${stat.type}.iceState is a string`);
      ok(
        [
          "closed",
          "failed",
          "disconnected",
          "new",
          "checking",
          "completed",
          "connected",
        ].includes(stat.iceState),
        `${stat.type}.iceState has a valid value. value=${stat.iceState}`
      );

      // selectedCandidatePairId
      ok(
        report.has(stat.selectedCandidatePairId),
        `${stat.type}.selectedCandidatePairId ${stat.selectedCandidatePairId} exists in report`
      );
      is(
        report.get(stat.selectedCandidatePairId).type,
        "candidate-pair",
        `${stat.type}.selectedCandidatePairId ${stat.selectedCandidatePairId} is a candidate-pair`
      );
      ok(
        report.has(stat.selectedCandidatePairId),
        `selectedCandidatePairId ${stat.selectedCandidatePairId} exists in report`
      );
      is(
        report.get(stat.selectedCandidatePairId).type,
        "candidate-pair",
        `selectedCandidatePairId ${stat.selectedCandidatePairId} in report is candidate-pair type`
      );

      // tlsVersion
      is(
        typeof stat.tlsVersion,
        "string",
        `${stat.type}.tlsVersion is a string`
      );
      ok(
        !!stat.tlsVersion.length,
        `${stat.type}.tlsVersionis not the empty string`
      );

      // dtlsCipher
      is(
        typeof stat.dtlsCipher,
        "string",
        `${stat.type}.dtlsCipher is a string`
      );
      ok(
        !!stat.dtlsCipher.length,
        `${stat.type}.dtlsCipher is not the empty string`
      );

      // dtlsRole
      is(typeof stat.dtlsRole, "string", `${stat.type}.dtlsRole is a string`);
      ok(
        ["unknown", "client", "server"].includes(stat.dtlsRole),
        `${stat.type}.dtlsRole has a valid value. value=${stat.dtlsRole}`
      );

      // srtpCipher
      is(
        typeof stat.srtpCipher,
        "string",
        `${stat.type}.srtpCipher is a string`
      );
      ok(
        !!stat.srtpCipher.length,
        `${stat.type}.ѕrtpCipher is not the empty string`
      );

      // selectedCandidatePairChanges
      ok(
        stat.selectedCandidatePairChanges > 0,
        `${stat.type}.selectedCandidatePairChanges is greater than zero. value=${stat.selectedCandidatePairChanges}`
      );
    } else if (
      stat.type == "local-candidate" ||
      stat.type == "remote-candidate"
    ) {
      info(`candidate is ${JSON.stringify(stat)}`);

      if (stat.candidateType != "prflx") {
        // address
        ok(
          stat.address,
          `${stat.type} has address. value=${stat.address} (${stat.kind})`
        );
      }

      // protocol
      ok(
        stat.protocol,
        `${stat.type} has protocol. value=${stat.protocol} ` + `(${stat.kind})`
      );

      // tcpType
      if (stat.protocol == "tcp") {
        ok(
          stat.tcpType == "active",
          `${stat.type} TCP candidate has tcpType 'active'`
        );
      } else if (stat.protocol == "udp") {
        ok(
          stat.tcpType == undefined,
          `${stat.type} UDP candidate has no tcpType`
        );
      }

      // foundation
      if (stat.candidateType == "prflx") {
        ok(
          stat.foundation == undefined,
          `${stat.type} prflx candidate has no foundation`
        );
      } else {
        ok(
          stat.foundation != undefined,
          `${stat.type} candidate has foundation. value=${stat.foundation} ` +
            `(${stat.kind}}`
        );
      }

      // port
      ok(
        stat.port >= 0,
        `${stat.type} has port >= 0. value=${stat.port} ` + `(${stat.kind})`
      );
      ok(
        stat.port <= 65535,
        `${stat.type} has port <= 65535. value=${stat.port} ` + `(${stat.kind})`
      );

      // candidateType
      ok(
        stat.candidateType,
        `${stat.type} has candidateType. value=${stat.candidateType} ` +
          `(${stat.kind})`
      );

      // priority
      ok(
        stat.priority > 0 && stat.priority < 2 ** 32 - 1,
        `${stat.type} has priority between 1 and 2^32 - 1 inc. ` +
          `value=${stat.priority} (${stat.kind})`
      );

      // relayProtocol
      if (stat.type == "local-candidate" && stat.candidateType == "relay") {
        ok(
          stat.relayProtocol,
          `relay ${stat.type} has relayProtocol. value=${stat.relayProtocol} ` +
            `(${stat.kind})`
        );
      } else {
        is(
          stat.relayProtocol,
          undefined,
          `relayProtocol is undefined for candidates that are not relay and ` +
            `local. value=${stat.relayProtocol} (${stat.kind})`
        );
      }

      // proxied
      if (stat.proxied) {
        ok(
          stat.proxied == "proxied" || stat.proxied == "non-proxied",
          `${stat.type} has proxied. value=${stat.proxied} (${stat.kind})`
        );
      }
    }

    //
    // Ensure everything was tested
    //
    [...expectations.expected, ...expectations.optional].forEach(field => {
      ok(
        Object.keys(tested).includes(field),
        `${stat.type}.${field} was tested.`
      );
    });
  });
}

function dumpStats(stats) {
  const dict = {};
  for (const [k, v] of stats.entries()) {
    dict[k] = v;
  }
  info(`Got stats: ${JSON.stringify(dict)}`);
}

async function waitForSyncedRtcp(pc) {
  // Ensures that RTCP is present
  let ensureSyncedRtcp = async () => {
    let report = await pc.getStats();
    for (const v of report.values()) {
      if (v.type.endsWith("bound-rtp") && !(v.remoteId || v.localId)) {
        info(`${v.id} is missing remoteId or localId: ${JSON.stringify(v)}`);
        return null;
      }
      if (v.type == "remote-inbound-rtp" && v.roundTripTime === undefined) {
        info(`${v.id} is missing roundTripTime: ${JSON.stringify(v)}`);
        return null;
      }
    }
    return report;
  };
  // Returns true if there is proof in aStats of rtcp flow for all remote stats
  // objects, compared to baseStats.
  const hasAllRtcpUpdated = (baseStats, stats) => {
    let hasRtcpStats = false;
    for (const v of stats.values()) {
      if (v.type == "remote-outbound-rtp") {
        hasRtcpStats = true;
        if (!v.remoteTimestamp) {
          // `remoteTimestamp` is 0 or not present.
          return false;
        }
        if (v.remoteTimestamp <= baseStats.get(v.id)?.remoteTimestamp) {
          // `remoteTimestamp` has not advanced further than the base stats,
          // i.e., no new sender report has been received.
          return false;
        }
      } else if (v.type == "remote-inbound-rtp") {
        hasRtcpStats = true;
        // The ideal thing here would be to check `reportsReceived`, but it's
        // not yet implemented.
        if (!v.packetsReceived) {
          // `packetsReceived` is 0 or not present.
          return false;
        }
        if (v.packetsReceived <= baseStats.get(v.id)?.packetsReceived) {
          // `packetsReceived` has not advanced further than the base stats,
          // i.e., no new receiver report has been received.
          return false;
        }
      }
    }
    return hasRtcpStats;
  };
  let attempts = 0;
  const baseStats = await pc.getStats();
  // Time-units are MS
  const waitPeriod = 100;
  const maxTime = 20000;
  for (let totalTime = maxTime; totalTime > 0; totalTime -= waitPeriod) {
    try {
      let syncedStats = await ensureSyncedRtcp();
      if (syncedStats && hasAllRtcpUpdated(baseStats, syncedStats)) {
        dumpStats(syncedStats);
        return syncedStats;
      }
    } catch (e) {
      info(e);
      info(e.stack);
      throw e;
    }
    attempts += 1;
    info(`waitForSyncedRtcp: no sync on attempt ${attempts}, retrying.`);
    await wait(waitPeriod);
  }
  throw Error(
    "Waiting for synced RTCP timed out after at least " + maxTime + "ms"
  );
}

function checkSendCodecsMimeType(senderStats, mimeType, sdpFmtpLine = null) {
  const codecReports = senderStats.values().filter(s => s.type == "codec");
  isnot(codecReports.length, 0, "Should have send codecs");
  for (const c of codecReports) {
    is(c.codecType, "encode", "Send codec is always encode");
    is(c.mimeType, mimeType, "Mime type as expected");
    if (sdpFmtpLine) {
      is(c.sdpFmtpLine, sdpFmtpLine, "Sdp fmtp line as expected");
    }
  }
}

function checkSenderStats(senderStats, streamCount) {
  const outboundRtpReports = [];
  const remoteInboundRtpReports = [];
  for (const v of senderStats.values()) {
    if (v.type == "outbound-rtp") {
      outboundRtpReports.push(v);
    } else if (v.type == "remote-inbound-rtp") {
      remoteInboundRtpReports.push(v);
    }
  }
  is(
    outboundRtpReports.length,
    streamCount,
    `Sender with ${streamCount} simulcast streams has ${streamCount} outbound-rtp reports`
  );
  is(
    remoteInboundRtpReports.length,
    streamCount,
    `Sender with ${streamCount} simulcast streams has ${streamCount} remote-inbound-rtp reports`
  );
  for (const outboundRtpReport of outboundRtpReports) {
    is(
      outboundRtpReports.filter(r => r.ssrc == outboundRtpReport.ssrc).length,
      1,
      "Simulcast send track SSRCs are distinct"
    );
    is(
      outboundRtpReports.filter(r => r.mid == outboundRtpReport.mid).length,
      streamCount,
      "Simulcast send track MIDs are identical"
    );
    if (outboundRtpReport.kind == "video" && streamCount > 1) {
      is(
        outboundRtpReports.filter(r => r.rid == outboundRtpReport.rid).length,
        1,
        "Simulcast send track RIDs are distinct"
      );
    }
    const remoteReports = remoteInboundRtpReports.filter(
      r => r.id == outboundRtpReport.remoteId
    );
    is(
      remoteReports.length,
      1,
      "Simulcast send tracks have exactly one remote counterpart"
    );
    const remoteInboundRtpReport = remoteReports[0];
    is(
      outboundRtpReport.ssrc,
      remoteInboundRtpReport.ssrc,
      "SSRC matches for outbound-rtp and remote-inbound-rtp"
    );
  }
}

function PC_LOCAL_TEST_LOCAL_STATS(test) {
  return waitForSyncedRtcp(test.pcLocal._pc).then(stats => {
    checkExpectedFields(stats);
    pedanticChecks(stats);
    return Promise.all([
      test.pcLocal._pc.getSenders().map(async s => {
        checkSenderStats(
          await s.getStats(),
          Math.max(1, s.getParameters()?.encodings?.length ?? 0)
        );
      }),
    ]);
  });
}

function PC_REMOTE_TEST_REMOTE_STATS(test) {
  return waitForSyncedRtcp(test.pcRemote._pc).then(stats => {
    checkExpectedFields(stats);
    pedanticChecks(stats);
    return Promise.all([
      test.pcRemote._pc.getSenders().map(async s => {
        checkSenderStats(
          await s.getStats(),
          s.track ? Math.max(1, s.getParameters()?.encodings?.length ?? 0) : 0
        );
      }),
    ]);
  });
}
