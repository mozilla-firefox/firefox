/*
 *  Copyright 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/legacy_stats_collector.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/data_channel_interface.h"
#include "api/legacy_stats_types.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "call/call.h"
#include "media/base/media_channel.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_transport_internal.h"
#include "pc/peer_connection_internal.h"
#include "pc/sctp_data_channel.h"
#include "pc/test/fake_peer_connection_for_stats.h"
#include "pc/test/fake_video_track_source.h"
#include "pc/test/legacy_stats_collector_test_helpers.h"
#include "pc/transport_stats.h"
#include "pc/video_track.h"
#include "rtc_base/checks.h"
#include "rtc_base/fake_ssl_identity.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/null_socket_server.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/thread.h"
#include "test/gtest.h"

namespace webrtc {

class StatsCollectorTrackTest : public LegacyStatsCollectorTest {
 public:
  // Adds a outgoing video track with a given SSRC into the stats.
  void AddOutgoingVideoTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    video_track_ = VideoTrack::Create(
        kLocalTrackId, FakeVideoTrackSource::Create(), Thread::Current());
    stats->AddTrack(video_track_.get());
    pc->AddRtpTransceiver(MediaType::VIDEO,
                          CreateMockSender(video_track_, kSsrcOfTrack));
  }

  // Adds a incoming video track with a given SSRC into the stats.
  void AddIncomingVideoTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    video_track_ = VideoTrack::Create(
        kRemoteTrackId, FakeVideoTrackSource::Create(), Thread::Current());
    stats->AddTrack(video_track_.get());
    pc->AddRtpTransceiver(MediaType::VIDEO, /*sender=*/nullptr,
                          CreateMockReceiver(video_track_, kSsrcOfTrack));
  }

  // Adds a outgoing audio track with a given SSRC into the stats,
  // and register it into the stats object.
  scoped_refptr<RtpSenderInterface> AddOutgoingAudioTrack(
      FakePeerConnectionForStats* pc,
      LegacyStatsCollector* stats) {
    audio_track_ = CreateFakeAudioTrack(kLocalTrackId, true);
    stats->AddTrack(audio_track_.get());
    return pc
        ->AddRtpTransceiver(MediaType::AUDIO,
                            CreateMockSender(audio_track_, kSsrcOfTrack))
        ->sender();
  }

  // Adds a incoming audio track with a given SSRC into the stats.
  void AddIncomingAudioTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    audio_track_ = CreateFakeAudioTrack(kRemoteTrackId, true);
    stats->AddTrack(audio_track_.get());
    pc->AddRtpTransceiver(MediaType::AUDIO, /*sender=*/nullptr,
                          CreateMockReceiver(audio_track_, kSsrcOfTrack));
  }

  scoped_refptr<VideoTrack> video_track_;
  scoped_refptr<AudioTrackInterface> audio_track_;
};

TEST(StatsCollectionTest, DetachAndMerge) {
  StatsCollection collection;
  ASSERT_EQ(collection.size(), 0u);

  // Create a new report with some information.
  StatsReport::Id id(
      StatsReport::NewTypedId(StatsReport::kStatsReportTypeTrack, "track_id"));
  StatsReport* report = collection.ReplaceOrAddNew(id);
  report->AddString(StatsReport::kStatsValueNameTrackId, "track_id");
  ASSERT_TRUE(report);
  // Check that looking it up, yields the same report.
  ASSERT_EQ(report, collection.FindOrAddNew(id));
  // There should be one report now.
  ASSERT_EQ(collection.size(), 1u);

  // Detach the internal container from the StatsCollection.
  StatsCollection::Container container = collection.DetachCollection();
  EXPECT_EQ(container.size(), 1u);
  EXPECT_EQ(collection.size(), 0u);
  EXPECT_EQ(nullptr, collection.Find(id));

  // Merge it back and test if we find the same report.
  collection.MergeCollection(std::move(container));
  EXPECT_EQ(collection.size(), 1u);
  EXPECT_EQ(report, collection.Find(id));
}

// Similar to `DetachAndMerge` above but detaches on one thread, merges on
// another to test that we don't trigger sequence checker.
TEST(StatsCollectionTest, DetachAndMergeThreaded) {
  Thread new_thread(std::make_unique<NullSocketServer>());
  new_thread.Start();

  StatsReport::Id id(
      StatsReport::NewTypedId(StatsReport::kStatsReportTypeTrack, "track_id"));

  StatsReport* expected_report = nullptr;

  StatsCollection::Container container = new_thread.BlockingCall([&] {
    StatsCollection collection;
    expected_report = collection.ReplaceOrAddNew(id);
    expected_report->AddString(StatsReport::kStatsValueNameTrackId, "track_id");
    return collection.DetachCollection();
  });

  StatsCollection collection;
  collection.MergeCollection(std::move(container));
  EXPECT_EQ(collection.size(), 1u);
  EXPECT_EQ(expected_report, collection.Find(id));

  new_thread.Stop();
}
TEST_F(LegacyStatsCollectorTest, FilterOutNegativeDataChannelId) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  pc->AddSctpDataChannel("hacks");

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  const StatsReport* report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeDataChannel, 1);

  std::string value_in_report;
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNameDataChannelId,
                        &value_in_report));
}

// Verify that ExtractDataInfo populates reports.
TEST_F(LegacyStatsCollectorTest, ExtractDataInfo) {
  const std::string kDataChannelLabel = "hacks";
  constexpr int kDataChannelId = 31337;
  const std::string kConnectingString = DataChannelInterface::DataStateString(
      DataChannelInterface::DataState::kConnecting);

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  InternalDataChannelInit init;
  init.id = kDataChannelId;
  pc->AddSctpDataChannel(kDataChannelLabel, init);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  const StatsReport* report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeDataChannel, 1);

  StatsReport::Id report_id = StatsReport::NewTypedIntId(
      StatsReport::kStatsReportTypeDataChannel, kDataChannelId);

  EXPECT_TRUE(report_id->Equals(report->id()));

  EXPECT_EQ(GetUtcTimeNowMs(), report->timestamp());
  EXPECT_EQ(kDataChannelLabel,
            ExtractStatsValue(StatsReport::kStatsReportTypeDataChannel, reports,
                              StatsReport::kStatsValueNameLabel));
  EXPECT_EQ(absl::StrCat(kDataChannelId),
            ExtractStatsValue(StatsReport::kStatsReportTypeDataChannel, reports,
                              StatsReport::kStatsValueNameDataChannelId));
  EXPECT_EQ(kConnectingString,
            ExtractStatsValue(StatsReport::kStatsReportTypeDataChannel, reports,
                              StatsReport::kStatsValueNameState));
  EXPECT_EQ("",
            ExtractStatsValue(StatsReport::kStatsReportTypeDataChannel, reports,
                              StatsReport::kStatsValueNameProtocol));
}

// This test verifies that 64-bit counters are passed successfully.
TEST_F(StatsCollectorTrackTest, BytesCounterHandles64Bits) {
  // The number of bytes must be larger than 0xFFFFFFFF for this test.
  constexpr int64_t kBytesSent = 12345678901234LL;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.payload_bytes_sent = kBytesSent;
  video_sender_info.header_and_padding_bytes_sent = 0;
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  AddOutgoingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(
      absl::StrCat(kBytesSent),
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameBytesSent));
}

// Test that audio BWE information is reported via stats.
TEST_F(StatsCollectorTrackTest, AudioBandwidthEstimationInfoIsReported) {
  // Set up an SSRC just to test that we get both kinds of stats back: SSRC and
  // BWE.
  constexpr int64_t kBytesSent = 12345678901234LL;
  constexpr int kSendBandwidth = 1234567;
  constexpr int kRecvBandwidth = 12345678;
  constexpr int kPacerDelay = 123;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VoiceSenderInfo voice_sender_info;
  voice_sender_info.add_ssrc(1234);
  voice_sender_info.payload_bytes_sent = kBytesSent - 12;
  voice_sender_info.header_and_padding_bytes_sent = 12;
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);

  AddOutgoingAudioTrack(pc.get(), stats.get());

  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  Call::Stats call_stats;
  call_stats.send_bandwidth_bps = kSendBandwidth;
  call_stats.recv_bandwidth_bps = kRecvBandwidth;
  call_stats.pacer_delay_ms = kPacerDelay;
  pc->SetCallStats(call_stats);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(
      absl::StrCat(kBytesSent),
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameBytesSent));
  EXPECT_EQ(absl::StrCat(kSendBandwidth),
            ExtractBweStatsValue(
                reports, StatsReport::kStatsValueNameAvailableSendBandwidth));
  EXPECT_EQ(
      absl::StrCat(kRecvBandwidth),
      ExtractBweStatsValue(
          reports, StatsReport::kStatsValueNameAvailableReceiveBandwidth));
  EXPECT_EQ(
      absl::StrCat(kPacerDelay),
      ExtractBweStatsValue(reports, StatsReport::kStatsValueNameBucketDelay));
}

// Test that video BWE information is reported via stats.
TEST_F(StatsCollectorTrackTest, VideoBandwidthEstimationInfoIsReported) {
  // Set up an SSRC just to test that we get both kinds of stats back: SSRC and
  // BWE.
  constexpr int64_t kBytesSent = 12345678901234LL;
  constexpr int kSendBandwidth = 1234567;
  constexpr int kRecvBandwidth = 12345678;
  constexpr int kPacerDelay = 123;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.payload_bytes_sent = kBytesSent - 12;
  video_sender_info.header_and_padding_bytes_sent = 12;

  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  AddOutgoingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  Call::Stats call_stats;
  call_stats.send_bandwidth_bps = kSendBandwidth;
  call_stats.recv_bandwidth_bps = kRecvBandwidth;
  call_stats.pacer_delay_ms = kPacerDelay;
  pc->SetCallStats(call_stats);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(
      absl::StrCat(kBytesSent),
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameBytesSent));
  EXPECT_EQ(absl::StrCat(kSendBandwidth),
            ExtractBweStatsValue(
                reports, StatsReport::kStatsValueNameAvailableSendBandwidth));
  EXPECT_EQ(
      absl::StrCat(kRecvBandwidth),
      ExtractBweStatsValue(
          reports, StatsReport::kStatsValueNameAvailableReceiveBandwidth));
  EXPECT_EQ(
      absl::StrCat(kPacerDelay),
      ExtractBweStatsValue(reports, StatsReport::kStatsValueNameBucketDelay));
}

// This test verifies that an object of type "googSession" always
// exists in the returned stats.
TEST_F(LegacyStatsCollectorTest, SessionObjectExists) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_TRUE(
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSession, 1));
}

// This test verifies that only one object of type "googSession" exists
// in the returned stats.
TEST_F(LegacyStatsCollectorTest, OnlyOneSessionObjectExists) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_TRUE(
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSession, 1));
  EXPECT_FALSE(
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSession, 2));
}

// This test verifies that the empty track report exists in the returned stats
// without calling StatsCollector::UpdateStats.
TEST_F(StatsCollectorTrackTest, TrackObjectExistsWithoutUpdateStats) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());
  pc->AddVideoChannel("video", "transport");

  // Verfies the existence of the track report.
  StatsReports reports;
  stats->GetStats(nullptr, &reports);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(StatsReport::kStatsReportTypeTrack, reports[0]->type());
  EXPECT_EQ(0, reports[0]->timestamp());

  std::string trackValue =
      ExtractStatsValue(StatsReport::kStatsReportTypeTrack, reports,
                        StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kLocalTrackId, trackValue);
}

// This test verifies that the empty track report exists in the returned stats
// when StatsCollector::UpdateStats is called with ssrc stats.
TEST_F(StatsCollectorTrackTest, TrackAndSsrcObjectExistAfterUpdateSsrcStats) {
  constexpr int64_t kBytesSent = 12345678901234LL;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.payload_bytes_sent = kBytesSent - 12;
  video_sender_info.header_and_padding_bytes_sent = 12;
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  AddOutgoingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  // `reports` should contain at least one session report, one track report,
  // and one ssrc report.
  EXPECT_LE(3u, reports.size());
  const StatsReport* track_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeTrack, 1);
  EXPECT_TRUE(track_report);

  // Get report for the specific `track`.
  reports.clear();
  stats->GetStats(video_track_.get(), &reports);
  // `reports` should contain at least one session report, one track report,
  // and one ssrc report.
  EXPECT_LE(3u, reports.size());
  track_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeTrack, 1);
  ASSERT_TRUE(track_report);
  EXPECT_EQ(GetUtcTimeNowMs(), track_report->timestamp());

  std::string ssrc_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameSsrc);
  EXPECT_EQ(absl::StrCat(kSsrcOfTrack), ssrc_id);

  std::string track_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kLocalTrackId, track_id);

  std::string media_type =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameMediaType);
  EXPECT_EQ("video", media_type);
}

// This test verifies that an SSRC object has the identifier of a Transport
// stats object, and that this transport stats object exists in stats.
TEST_F(StatsCollectorTrackTest, TransportObjectLinkedFromSsrcObject) {
  constexpr int64_t kBytesSent = 12345678901234LL;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.payload_bytes_sent = kBytesSent - 12;
  video_sender_info.header_and_padding_bytes_sent = 12;
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  AddOutgoingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  std::string transport_id =
      ExtractStatsValue(StatsReport::kStatsReportTypeSsrc, reports,
                        StatsReport::kStatsValueNameTransportId);
  ASSERT_NE(kNotFound, transport_id);

  // Transport id component ID will always be 1.
  // This has assumptions about how the ID is constructed.  As is, this is
  // OK since this is for testing purposes only, but if we ever need this
  // in production, we should add a generic method that does this.
  size_t index = transport_id.find('-');
  ASSERT_NE(std::string::npos, index);
  std::string content = transport_id.substr(index + 1);
  index = content.rfind('-');
  ASSERT_NE(std::string::npos, index);
  content = content.substr(0, index);
  StatsReport::Id id(StatsReport::NewComponentId(content, 1));
  ASSERT_EQ(transport_id, id->ToString());
  const StatsReport* transport_report = FindReportById(reports, id);
  ASSERT_TRUE(transport_report);
}

// This test verifies that a remote stats object will not be created for
// an outgoing SSRC where remote stats are not returned.
TEST_F(StatsCollectorTrackTest, RemoteSsrcInfoIsAbsent) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());
  pc->AddVideoChannel("video", "transport");

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  const StatsReport* remote_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeRemoteSsrc, 1);
  EXPECT_FALSE(remote_report);
}

// This test verifies that a remote stats object will be created for
// an outgoing SSRC where stats are returned.
TEST_F(StatsCollectorTrackTest, RemoteSsrcInfoIsPresent) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  SsrcReceiverInfo remote_ssrc_stats;
  remote_ssrc_stats.timestamp = 12345.678;
  remote_ssrc_stats.ssrc = kSsrcOfTrack;
  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(kSsrcOfTrack);
  video_sender_info.remote_stats.push_back(remote_ssrc_stats);
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  AddOutgoingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  const StatsReport* remote_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeRemoteSsrc, 1);
  ASSERT_TRUE(remote_report);
  EXPECT_EQ(12345.678, remote_report->timestamp());
}

// This test verifies that the empty track report exists in the returned stats
// when StatsCollector::UpdateStats is called with ssrc stats.
TEST_F(StatsCollectorTrackTest, ReportsFromRemoteTrack) {
  constexpr int64_t kNumOfPacketsConcealed = 54321;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoReceiverInfo video_receiver_info;
  video_receiver_info.add_ssrc(1234);
  video_receiver_info.packets_concealed = kNumOfPacketsConcealed;
  VideoMediaInfo video_info;
  video_info.receivers.push_back(video_receiver_info);

  AddIncomingVideoTrack(pc.get(), stats.get());

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  // `reports` should contain at least one session report, one track report,
  // and one ssrc report.
  EXPECT_LE(3u, reports.size());
  const StatsReport* track_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeTrack, 1);
  ASSERT_TRUE(track_report);
  EXPECT_EQ(GetUtcTimeNowMs(), track_report->timestamp());

  std::string ssrc_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameSsrc);
  EXPECT_EQ(absl::StrCat(kSsrcOfTrack), ssrc_id);

  std::string track_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kRemoteTrackId, track_id);
}

// This test verifies the Ice Candidate report should contain the correct
// information from local/remote candidates.
TEST_F(LegacyStatsCollectorTest, IceCandidateReport) {
  const std::string kTransportName = "audio";
  const AdapterType kNetworkType = ADAPTER_TYPE_ETHERNET;
  constexpr uint32_t kPriority = 1000;

  constexpr int kLocalPort = 2000;
  const std::string kLocalIp = "192.168.0.1";
  const SocketAddress kLocalAddress(kLocalIp, kLocalPort);

  constexpr int kRemotePort = 2001;
  const std::string kRemoteIp = "192.168.0.2";
  const SocketAddress kRemoteAddress(kRemoteIp, kRemotePort);

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  Candidate local;
  EXPECT_GT(local.id().length(), 0u);
  RTC_DCHECK_EQ(local.type(), IceCandidateType::kHost);
  local.set_protocol(UDP_PROTOCOL_NAME);
  local.set_address(kLocalAddress);
  local.set_priority(kPriority);
  local.set_network_type(kNetworkType);

  Candidate remote;
  EXPECT_GT(remote.id().length(), 0u);
  remote.set_type(IceCandidateType::kPrflx);
  remote.set_protocol(UDP_PROTOCOL_NAME);
  remote.set_address(kRemoteAddress);
  remote.set_priority(kPriority);
  remote.set_network_type(kNetworkType);

  ConnectionInfo connection_info;
  connection_info.local_candidate = local;
  connection_info.remote_candidate = remote;
  TransportChannelStats channel_stats;
  channel_stats.ice_transport_stats.connection_infos.push_back(connection_info);

  pc->AddVoiceChannel("audio", kTransportName);
  pc->SetTransportStats(kTransportName, channel_stats);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  // Verify the local candidate report is populated correctly.
  EXPECT_EQ(
      "Cand-" + local.id(),
      ExtractStatsValue(StatsReport::kStatsReportTypeCandidatePair, reports,
                        StatsReport::kStatsValueNameLocalCandidateId));
  EXPECT_EQ(
      kLocalIp,
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidateIPAddress));
  EXPECT_EQ(
      absl::StrCat(kLocalPort),
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidatePortNumber));
  EXPECT_EQ(
      UDP_PROTOCOL_NAME,
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidateTransportType));
  EXPECT_EQ(
      absl::StrCat(kPriority),
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidatePriority));
  EXPECT_EQ(
      IceCandidateTypeToStatsType(local),
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidateType));
  EXPECT_EQ(
      AdapterTypeToStatsType(kNetworkType),
      ExtractStatsValue(StatsReport::kStatsReportTypeIceLocalCandidate, reports,
                        StatsReport::kStatsValueNameCandidateNetworkType));

  // Verify the remote candidate report is populated correctly.
  EXPECT_EQ(
      "Cand-" + remote.id(),
      ExtractStatsValue(StatsReport::kStatsReportTypeCandidatePair, reports,
                        StatsReport::kStatsValueNameRemoteCandidateId));
  EXPECT_EQ(kRemoteIp,
            ExtractStatsValue(StatsReport::kStatsReportTypeIceRemoteCandidate,
                              reports,
                              StatsReport::kStatsValueNameCandidateIPAddress));
  EXPECT_EQ(absl::StrCat(kRemotePort),
            ExtractStatsValue(StatsReport::kStatsReportTypeIceRemoteCandidate,
                              reports,
                              StatsReport::kStatsValueNameCandidatePortNumber));
  EXPECT_EQ(UDP_PROTOCOL_NAME,
            ExtractStatsValue(
                StatsReport::kStatsReportTypeIceRemoteCandidate, reports,
                StatsReport::kStatsValueNameCandidateTransportType));
  EXPECT_EQ(absl::StrCat(kPriority),
            ExtractStatsValue(StatsReport::kStatsReportTypeIceRemoteCandidate,
                              reports,
                              StatsReport::kStatsValueNameCandidatePriority));
  EXPECT_EQ(
      IceCandidateTypeToStatsType(remote),
      ExtractStatsValue(StatsReport::kStatsReportTypeIceRemoteCandidate,
                        reports, StatsReport::kStatsValueNameCandidateType));
  EXPECT_EQ(kNotFound,
            ExtractStatsValue(
                StatsReport::kStatsReportTypeIceRemoteCandidate, reports,
                StatsReport::kStatsValueNameCandidateNetworkType));
}

// This test verifies that all chained certificates are correctly
// reported
TEST_F(LegacyStatsCollectorTest, ChainedCertificateReportsCreated) {
  // Build local certificate chain.
  std::vector<std::string> local_ders(5);
  local_ders[0] = "These";
  local_ders[1] = "are";
  local_ders[2] = "some";
  local_ders[3] = "der";
  local_ders[4] = "values";
  FakeSSLIdentity local_identity(DersToPems(local_ders));

  // Build remote certificate chain
  std::vector<std::string> remote_ders(4);
  remote_ders[0] = "A";
  remote_ders[1] = "non-";
  remote_ders[2] = "intersecting";
  remote_ders[3] = "set";
  FakeSSLIdentity remote_identity(DersToPems(remote_ders));

  TestCertificateReports(local_identity, local_ders, remote_identity,
                         remote_ders);
}

TEST_F(LegacyStatsCollectorTest,
       AddCertificateReports_DuplicateFingerprintDoesNotCrash) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // Create duplicate fingerprints chain
  auto issuer_stats = std::make_unique<SSLCertificateStats>(
      "same_fingerprint", "sha-1", "cert_issuer", nullptr);
  auto cert_stats = std::make_unique<SSLCertificateStats>(
      "same_fingerprint", "sha-1", "cert_leaf", std::move(issuer_stats));

  // This should not crash (Use-After-Free)
  stats->AddCertificateReportsForTest(std::move(cert_stats));
}

// This test verifies that all certificates without chains are correctly
// reported.
TEST_F(LegacyStatsCollectorTest, ChainlessCertificateReportsCreated) {
  // Build local certificate.
  std::string local_der = "This is the local der.";
  FakeSSLIdentity local_identity(DerToPem(local_der));

  // Build remote certificate.
  std::string remote_der = "This is somebody else's der.";
  FakeSSLIdentity remote_identity(DerToPem(remote_der));

  TestCertificateReports(local_identity, std::vector<std::string>(1, local_der),
                         remote_identity,
                         std::vector<std::string>(1, remote_der));
}

// This test verifies that the stats are generated correctly when no
// transport is present.
TEST_F(LegacyStatsCollectorTest, NoTransport) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // This will cause the fake PeerConnection to generate a TransportStats entry
  // but with only a single dummy TransportChannelStats.
  pc->AddVoiceChannel("audio", "transport");

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  // Check that the local certificate is absent.
  std::string local_certificate_id =
      ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                        StatsReport::kStatsValueNameLocalCertificateId);
  ASSERT_EQ(kNotFound, local_certificate_id);

  // Check that the remote certificate is absent.
  std::string remote_certificate_id =
      ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                        StatsReport::kStatsValueNameRemoteCertificateId);
  ASSERT_EQ(kNotFound, remote_certificate_id);

  // Check that the negotiated ciphers are absent.
  std::string dtls_cipher_suite =
      ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                        StatsReport::kStatsValueNameDtlsCipher);
  ASSERT_EQ(kNotFound, dtls_cipher_suite);
  std::string srtp_crypto_suite =
      ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                        StatsReport::kStatsValueNameSrtpCipher);
  ASSERT_EQ(kNotFound, srtp_crypto_suite);
}

// This test verifies that a remote certificate with an unsupported digest
// algorithm is correctly ignored.
TEST_F(LegacyStatsCollectorTest, UnsupportedDigestIgnored) {
  // Build a local certificate.
  std::string local_der = "This is the local der.";
  FakeSSLIdentity local_identity(DerToPem(local_der));

  // Build a remote certificate with an unsupported digest algorithm.
  std::string remote_der = "This is somebody else's der.";
  FakeSSLCertificate remote_cert(DerToPem(remote_der));
  remote_cert.set_digest_algorithm("foobar");
  FakeSSLIdentity remote_identity(remote_cert);

  TestCertificateReports(local_identity, std::vector<std::string>(1, local_der),
                         remote_identity, std::vector<std::string>());
}

// This test verifies that a local stats object can get statistics via
// AudioTrackInterface::GetStats() method.
TEST_F(StatsCollectorTrackTest, GetStatsFromLocalAudioTrack) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingAudioTrack(pc.get(), stats.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  VoiceSenderInfo voice_sender_info;
  InitVoiceSenderInfo(&voice_sender_info);
  UpdateVoiceSenderInfoFromAudioTrack(audio_track_.get(), &voice_sender_info,
                                      false);
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);

  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  StatsReports reports;  // returned values.
  VerifyAudioTrackStats(audio_track_.get(), stats.get(), voice_info, &reports);

  // Verify that there is no remote report for the local audio track because
  // we did not set it up.
  const StatsReport* remote_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeRemoteSsrc, 1);
  EXPECT_TRUE(remote_report == nullptr);
}

// This test verifies that audio receive streams populate stats reports
// correctly.
TEST_F(StatsCollectorTrackTest, GetStatsFromRemoteStream) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddIncomingAudioTrack(pc.get(), stats.get());

  VoiceReceiverInfo voice_receiver_info;
  InitVoiceReceiverInfo(&voice_receiver_info);
  voice_receiver_info.codec_name = "fake_codec";
  VoiceMediaInfo voice_info;
  voice_info.receivers.push_back(voice_receiver_info);

  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  StatsReports reports;  // returned values.
  VerifyAudioTrackStats(audio_track_.get(), stats.get(), voice_info, &reports);
}

// This test verifies that a local stats object won't update its statistics
// after a RemoveLocalAudioTrack() call.
TEST_F(StatsCollectorTrackTest, GetStatsAfterRemoveAudioStream) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingAudioTrack(pc.get(), stats.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  VoiceSenderInfo voice_sender_info;
  InitVoiceSenderInfo(&voice_sender_info);
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);

  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  stats->RemoveLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  // The report will exist since we don't remove them in RemoveStream().
  const StatsReport* report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSsrc, 1);
  ASSERT_TRUE(report);
  EXPECT_EQ(GetUtcTimeNowMs(), report->timestamp());
  std::string track_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kLocalTrackId, track_id);
  std::string ssrc_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameSsrc);
  EXPECT_EQ(absl::StrCat(kSsrcOfTrack), ssrc_id);

  // Verifies the values in the track report, no value will be changed by the
  // AudioTrackInterface::GetSignalValue() and
  // AudioProcessorInterface::GetStats();
  VerifyVoiceSenderInfoReport(report, voice_sender_info);
}

// This test verifies that when ongoing and incoming audio tracks are using
// the same ssrc, they populate stats reports correctly.
TEST_F(StatsCollectorTrackTest, LocalAndRemoteTracksWithSameSsrc) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // Create a local audio track and add it to the stats.
  audio_track_ = CreateFakeAudioTrack(kLocalTrackId, true);
  stats->AddTrack(audio_track_.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  // Create a remote audio track and add it to the stats.
  scoped_refptr<AudioTrackInterface> remote_track =
      CreateFakeAudioTrack(kRemoteTrackId, true);
  stats->AddTrack(remote_track.get());

  // The local and the remote track share a transceiver.
  pc->AddRtpTransceiver(MediaType::AUDIO,
                        CreateMockSender(audio_track_, kSsrcOfTrack),
                        CreateMockReceiver(remote_track, kSsrcOfTrack));

  VoiceSenderInfo voice_sender_info;
  InitVoiceSenderInfo(&voice_sender_info);
  // Some of the contents in `voice_sender_info` needs to be updated from the
  // `audio_track_`.
  UpdateVoiceSenderInfoFromAudioTrack(audio_track_.get(), &voice_sender_info,
                                      true);

  VoiceReceiverInfo voice_receiver_info;
  InitVoiceReceiverInfo(&voice_receiver_info);

  // Constructs an ssrc stats update.
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);
  voice_info.receivers.push_back(voice_receiver_info);

  // Instruct the session to return stats containing the transport channel.
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);

  // Get stats for the local track.
  StatsReports reports;  // returned values.
  stats->GetStats(audio_track_.get(), &reports);

  const StatsReport* track_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSsrc, 1);
  ASSERT_TRUE(track_report);
  EXPECT_EQ(GetUtcTimeNowMs(), track_report->timestamp());
  std::string track_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kLocalTrackId, track_id);
  VerifyVoiceSenderInfoReport(track_report, voice_sender_info);

  // Get stats for the remote track.
  reports.clear();
  stats->GetStats(remote_track.get(), &reports);
  track_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSsrc, 1);
  ASSERT_TRUE(track_report);
  EXPECT_EQ(GetUtcTimeNowMs(), track_report->timestamp());
  track_id =
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameTrackId);
  EXPECT_EQ(kRemoteTrackId, track_id);
  VerifyVoiceReceiverInfoReport(track_report, voice_receiver_info);
}

// This test verifies that stats are correctly set in video send ssrc stats.
TEST_F(StatsCollectorTrackTest, VerifyVideoSendSsrcStats) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.frames_encoded = 10;
  video_sender_info.qp_sum = 11;
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(absl::StrCat(video_sender_info.frames_encoded),
            ExtractSsrcStatsValue(reports,
                                  StatsReport::kStatsValueNameFramesEncoded));
  EXPECT_EQ(absl::StrCat(*video_sender_info.qp_sum),
            ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameQpSum));
}

// This test verifies that stats are correctly set in video receive ssrc stats.
TEST_F(StatsCollectorTrackTest, VerifyVideoReceiveSsrcStatsNew) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddIncomingVideoTrack(pc.get(), stats.get());

  VideoReceiverInfo video_receiver_info;
  video_receiver_info.add_ssrc(1234);
  video_receiver_info.frames_decoded = 10;
  video_receiver_info.qp_sum = 11;
  VideoMediaInfo video_info;
  video_info.receivers.push_back(video_receiver_info);

  pc->AddVideoChannel("video", "transport", video_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(absl::StrCat(video_receiver_info.frames_decoded),
            ExtractSsrcStatsValue(reports,
                                  StatsReport::kStatsValueNameFramesDecoded));
  EXPECT_EQ(absl::StrCat(*video_receiver_info.qp_sum),
            ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameQpSum));
}

}  // namespace webrtc
