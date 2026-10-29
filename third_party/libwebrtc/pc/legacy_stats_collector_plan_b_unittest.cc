/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// TODO: crbug.com/webrtc/13528 - Delete this file when Plan B is removed.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "api/legacy_stats_types.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "call/call.h"
#include "media/base/media_channel.h"
#include "pc/legacy_stats_collector.h"
#include "pc/media_stream.h"
#include "pc/test/fake_peer_connection_for_stats.h"
#include "pc/test/fake_video_track_source.h"
#include "pc/test/legacy_stats_collector_test_helpers.h"
#include "pc/video_track.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::UnorderedElementsAre;

// Variant of StatsCollectorTrackTest where the tracks are added to the
// stats collector through MediaStreams, as is done in Plan B.
class StatsCollectorTrackPlanBTest : public LegacyStatsCollectorTest {
 public:
  // Adds a outgoing video track with a given SSRC into the stats.
  // The track is also inserted into the local stream, which is created if
  // necessary.
  void AddOutgoingVideoTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    video_track_ = VideoTrack::Create(
        kLocalTrackId, FakeVideoTrackSource::Create(), Thread::Current());
    if (!stream_)
      stream_ = MediaStream::Create("streamid");
    stream_->AddTrack(video_track());
    stats->AddStream(stream_.get());
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    pc->AddSender(CreateMockSender(video_track_, kSsrcOfTrack));
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }

  // Adds a incoming video track with a given SSRC into the stats.
  void AddIncomingVideoTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    video_track_ = VideoTrack::Create(
        kRemoteTrackId, FakeVideoTrackSource::Create(), Thread::Current());
    stream_ = MediaStream::Create("streamid");
    stream_->AddTrack(video_track());
    stats->AddStream(stream_.get());
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    pc->AddReceiver(CreateMockReceiver(video_track_, kSsrcOfTrack));
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }

  // Adds a outgoing audio track with a given SSRC into the stats,
  // and register it into the stats object.
  // The track is also inserted into the local stream, which is created if
  // necessary.
  scoped_refptr<RtpSenderInterface> AddOutgoingAudioTrack(
      FakePeerConnectionForStats* pc,
      LegacyStatsCollector* stats) {
    audio_track_ = CreateFakeAudioTrack(kLocalTrackId, true);
    if (!stream_)
      stream_ = MediaStream::Create("streamid");
    stream_->AddTrack(audio_track());
    stats->AddStream(stream_.get());
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    auto sender = pc->AddSender(CreateMockSender(audio_track_, kSsrcOfTrack));
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
    return sender;
  }

  // Adds a incoming audio track with a given SSRC into the stats.
  void AddIncomingAudioTrack(FakePeerConnectionForStats* pc,
                             LegacyStatsCollector* stats) {
    audio_track_ = CreateFakeAudioTrack(kRemoteTrackId, true);
    if (stream_ == nullptr)
      stream_ = MediaStream::Create("streamid");
    stream_->AddTrack(audio_track());
    stats->AddStream(stream_.get());
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    pc->AddReceiver(CreateMockReceiver(audio_track_, kSsrcOfTrack));
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }

  scoped_refptr<AudioTrackInterface> audio_track() { return audio_track_; }
  scoped_refptr<VideoTrackInterface> video_track() { return video_track_; }

  scoped_refptr<MediaStream> stream_;
  scoped_refptr<VideoTrack> video_track_;
  scoped_refptr<AudioTrackInterface> audio_track_;
};

// This test verifies that 64-bit counters are passed successfully.
TEST_F(StatsCollectorTrackPlanBTest, BytesCounterHandles64Bits) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(
      absl::StrCat(kBytesSent),
      ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameBytesSent));
}

// Test that audio BWE information is reported via stats.
TEST_F(StatsCollectorTrackPlanBTest, AudioBandwidthEstimationInfoIsReported) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
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
TEST_F(StatsCollectorTrackPlanBTest, VideoBandwidthEstimationInfoIsReported) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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

// This test verifies that the empty track report exists in the returned stats
// without calling StatsCollector::UpdateStats.
TEST_F(StatsCollectorTrackPlanBTest, TrackObjectExistsWithoutUpdateStats) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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
TEST_F(StatsCollectorTrackPlanBTest,
       TrackAndSsrcObjectExistAfterUpdateSsrcStats) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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
TEST_F(StatsCollectorTrackPlanBTest, TransportObjectLinkedFromSsrcObject) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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
TEST_F(StatsCollectorTrackPlanBTest, RemoteSsrcInfoIsAbsent) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  const StatsReport* remote_report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeRemoteSsrc, 1);
  EXPECT_FALSE(remote_report);
}

// This test verifies that a remote stats object will be created for
// an outgoing SSRC where stats are returned.
TEST_F(StatsCollectorTrackPlanBTest, RemoteSsrcInfoIsPresent) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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
TEST_F(StatsCollectorTrackPlanBTest, ReportsFromRemoteTrack) {
  constexpr int64_t kNumOfPacketsConcealed = 54321;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  VideoReceiverInfo video_receiver_info;
  video_receiver_info.add_ssrc(1234);
  video_receiver_info.packets_concealed = kNumOfPacketsConcealed;
  VideoMediaInfo video_info;
  video_info.receivers.push_back(video_receiver_info);

  AddIncomingVideoTrack(pc.get(), stats.get());

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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

// This test verifies that a local stats object can get statistics via
// AudioTrackInterface::GetStats() method.
TEST_F(StatsCollectorTrackPlanBTest, GetStatsFromLocalAudioTrack) {
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

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
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
TEST_F(StatsCollectorTrackPlanBTest, GetStatsFromRemoteStream) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddIncomingAudioTrack(pc.get(), stats.get());

  VoiceReceiverInfo voice_receiver_info;
  InitVoiceReceiverInfo(&voice_receiver_info);
  voice_receiver_info.codec_name = "fake_codec";
  VoiceMediaInfo voice_info;
  voice_info.receivers.push_back(voice_receiver_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  StatsReports reports;  // returned values.
  VerifyAudioTrackStats(audio_track_.get(), stats.get(), voice_info, &reports);
}

// This test verifies that a local stats object won't update its statistics
// after a RemoveLocalAudioTrack() call.
TEST_F(StatsCollectorTrackPlanBTest, GetStatsAfterRemoveAudioStream) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingAudioTrack(pc.get(), stats.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  VoiceSenderInfo voice_sender_info;
  InitVoiceSenderInfo(&voice_sender_info);
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
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
TEST_F(StatsCollectorTrackPlanBTest, LocalAndRemoteTracksWithSameSsrc) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // Create a local stream with a local audio track and adds it to the stats.
  AddOutgoingAudioTrack(pc.get(), stats.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  // Create a remote stream with a remote audio track and adds it to the stats.
  scoped_refptr<MediaStream> remote_stream(
      MediaStream::Create("remotestreamid"));
  scoped_refptr<AudioTrackInterface> remote_track =
      CreateFakeAudioTrack(kRemoteTrackId, true);
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddReceiver(CreateMockReceiver(remote_track, kSsrcOfTrack));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  remote_stream->AddTrack(remote_track);
  stats->AddStream(remote_stream.get());

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
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("audio", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
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
TEST_F(StatsCollectorTrackPlanBTest, VerifyVideoSendSsrcStats) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddOutgoingVideoTrack(pc.get(), stats.get());

  VideoSenderInfo video_sender_info;
  video_sender_info.add_ssrc(1234);
  video_sender_info.frames_encoded = 10;
  video_sender_info.qp_sum = 11;
  VideoMediaInfo video_info;
  video_info.aggregated_senders.push_back(video_sender_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

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
TEST_F(StatsCollectorTrackPlanBTest, VerifyVideoReceiveSsrcStatsNew) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  AddIncomingVideoTrack(pc.get(), stats.get());

  VideoReceiverInfo video_receiver_info;
  video_receiver_info.add_ssrc(1234);
  video_receiver_info.frames_decoded = 10;
  video_receiver_info.qp_sum = 11;
  VideoMediaInfo video_info;
  video_info.receivers.push_back(video_receiver_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddVideoChannel("video", "transport", video_info);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
  StatsReports reports;
  stats->GetStats(nullptr, &reports);

  EXPECT_EQ(absl::StrCat(video_receiver_info.frames_decoded),
            ExtractSsrcStatsValue(reports,
                                  StatsReport::kStatsValueNameFramesDecoded));
  EXPECT_EQ(absl::StrCat(*video_receiver_info.qp_sum),
            ExtractSsrcStatsValue(reports, StatsReport::kStatsValueNameQpSum));
}

// This test verifies that the audio/video related stats which are -1 initially
// will be filtered out.
TEST_F(StatsCollectorTrackPlanBTest, FilterOutNegativeInitialValues) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // Create a local stream with a local audio track and adds it to the stats.
  stream_ = MediaStream::Create("streamid");
  auto local_track = CreateFakeAudioTrack(kLocalTrackId, false);
  stream_->AddTrack(scoped_refptr<AudioTrackInterface>(local_track.get()));
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddSender(CreateMockSender(local_track, kSsrcOfTrack));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  stats->AddStream(stream_.get());
  stats->AddLocalAudioTrack(local_track.get(), kSsrcOfTrack);

  // Create a remote stream with a remote audio track and adds it to the stats.
  scoped_refptr<MediaStream> remote_stream(
      MediaStream::Create("remotestreamid"));
  scoped_refptr<AudioTrackInterface> remote_track =
      CreateFakeAudioTrack(kRemoteTrackId, false);
  remote_stream->AddTrack(remote_track);
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddReceiver(CreateMockReceiver(remote_track, kSsrcOfTrack));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  stats->AddStream(remote_stream.get());

  VoiceSenderInfo voice_sender_info;
  voice_sender_info.add_ssrc(kSsrcOfTrack);
  // These values are set to -1 initially in audio_send_stream.
  // The voice_sender_info will read the values from audio_send_stream.
  voice_sender_info.rtt_ms = -1;
  voice_sender_info.packets_lost = -1;
  voice_sender_info.jitter_ms = -1;

  // Some of the contents in `voice_sender_info` needs to be updated from the
  // `audio_track_`.
  UpdateVoiceSenderInfoFromAudioTrack(local_track.get(), &voice_sender_info,
                                      true);

  VoiceReceiverInfo voice_receiver_info;
  voice_receiver_info.add_ssrc(kSsrcOfTrack);
  voice_receiver_info.capture_start_ntp_time_ms = -1;
  voice_receiver_info.audio_level = -1;

  // Constructs an ssrc stats update.
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);
  voice_info.receivers.push_back(voice_receiver_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("voice", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);

  // Get stats for the local track.
  StatsReports reports;
  stats->GetStats(local_track.get(), &reports);
  const StatsReport* report =
      FindNthReportByType(reports, StatsReport::kStatsReportTypeSsrc, 1);
  ASSERT_TRUE(report);
  // The -1 will not be added to the stats report.
  std::string value_in_report;
  EXPECT_FALSE(
      GetValue(report, StatsReport::kStatsValueNameRtt, &value_in_report));
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNamePacketsLost,
                        &value_in_report));
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNameJitterReceived,
                        &value_in_report));
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNameEchoDelayMedian,
                        &value_in_report));
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNameEchoDelayStdDev,
                        &value_in_report));

  // Get stats for the remote track.
  reports.clear();
  stats->GetStats(remote_track.get(), &reports);
  report = FindNthReportByType(reports, StatsReport::kStatsReportTypeSsrc, 1);
  ASSERT_TRUE(report);
  EXPECT_FALSE(GetValue(report,
                        StatsReport::kStatsValueNameCaptureStartNtpTimeMs,
                        &value_in_report));
  EXPECT_FALSE(GetValue(report, StatsReport::kStatsValueNameAudioInputLevel,
                        &value_in_report));
}

// This test verifies that when two outgoing audio tracks are using the same
// ssrc at different times, they populate stats reports correctly.
// TODO(xians): Figure out if it is possible to encapsulate the setup and
// avoid duplication of code in test cases.
TEST_F(StatsCollectorTrackPlanBTest, TwoLocalTracksWithSameSsrc) {
  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  // Create a local stream with a local audio track and adds it to the stats.
  auto sender = AddOutgoingAudioTrack(pc.get(), stats.get());
  stats->AddLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);

  VoiceSenderInfo voice_sender_info;
  InitVoiceSenderInfo(&voice_sender_info);
  UpdateVoiceSenderInfoFromAudioTrack(audio_track_.get(), &voice_sender_info,
                                      false);
  voice_sender_info.add_ssrc(kSsrcOfTrack);
  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(voice_sender_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("voice", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  StatsReports reports;  // returned values.
  VerifyAudioTrackStats(audio_track_.get(), stats.get(), voice_info, &reports);

  // Remove the previous audio track from the stream.
  stream_->RemoveTrack(audio_track());
  stats->RemoveLocalAudioTrack(audio_track_.get(), kSsrcOfTrack);
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->RemoveSender(sender);
  RTC_ALLOW_PLAN_B_DEPRECATION_END();

  // Create a new audio track and adds it to the stream and stats.
  static const std::string kNewTrackId = "new_track_id";
  auto new_audio_track = CreateFakeAudioTrack(kNewTrackId, true);
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddSender(CreateMockSender(new_audio_track, kSsrcOfTrack));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  stream_->AddTrack(scoped_refptr<AudioTrackInterface>(new_audio_track.get()));

  stats->AddLocalAudioTrack(new_audio_track.get(), kSsrcOfTrack);
  stats->InvalidateCache();

  VoiceSenderInfo new_voice_sender_info;
  InitVoiceSenderInfo(&new_voice_sender_info);
  UpdateVoiceSenderInfoFromAudioTrack(new_audio_track.get(),
                                      &new_voice_sender_info, false);
  VoiceMediaInfo new_voice_info;
  new_voice_info.senders.push_back(new_voice_sender_info);
  voice_media_channels.first->SetStats(new_voice_info);
  voice_media_channels.second->SetStats(new_voice_info);

  reports.clear();
  VerifyAudioTrackStats(new_audio_track.get(), stats.get(), new_voice_info,
                        &reports);
}

// Test that if there are two local senders with the same track then two SSRC
// reports will be created, one for each sender, with the same track ID and one
// track report will be created for the shared track.
TEST_F(StatsCollectorTrackPlanBTest, TwoLocalSendersWithSameTrack) {
  constexpr uint32_t kFirstSsrc = 22;
  constexpr uint32_t kSecondSsrc = 33;

  auto pc = CreatePeerConnection();
  auto stats = CreateStatsCollector(pc.get());

  auto local_track = CreateFakeAudioTrack(kLocalTrackId, false);
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  pc->AddSender(CreateMockSender(local_track, kFirstSsrc));
  stats->AddLocalAudioTrack(local_track.get(), kFirstSsrc);
  pc->AddSender(CreateMockSender(local_track, kSecondSsrc));
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  stats->AddLocalAudioTrack(local_track.get(), kSecondSsrc);

  VoiceSenderInfo first_sender_info;
  InitVoiceSenderInfo(&first_sender_info, kFirstSsrc);
  UpdateVoiceSenderInfoFromAudioTrack(local_track.get(), &first_sender_info,
                                      false);

  VoiceSenderInfo second_sender_info;
  InitVoiceSenderInfo(&second_sender_info, kSecondSsrc);
  UpdateVoiceSenderInfoFromAudioTrack(local_track.get(), &second_sender_info,
                                      false);

  VoiceMediaInfo voice_info;
  voice_info.senders.push_back(first_sender_info);
  voice_info.senders.push_back(second_sender_info);

  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
  auto voice_media_channels = pc->AddVoiceChannel("voice", "transport");
  RTC_ALLOW_PLAN_B_DEPRECATION_END();
  voice_media_channels.first->SetStats(voice_info);
  voice_media_channels.second->SetStats(voice_info);

  stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);

  StatsReports reports;
  stats->GetStats(local_track.get(), &reports);

  // Both SSRC reports have the same track ID.
  EXPECT_EQ(kLocalTrackId, GetValueInNthReportByType(
                               reports, StatsReport::kStatsReportTypeSsrc,
                               StatsReport::kStatsValueNameTrackId, 1));
  EXPECT_EQ(kLocalTrackId, GetValueInNthReportByType(
                               reports, StatsReport::kStatsReportTypeSsrc,
                               StatsReport::kStatsValueNameTrackId, 2));

  // The SSRC in each SSRC report is different and correspond to the sender
  // SSRC.
  std::vector<std::optional<std::string>> ssrcs = {
      GetValueInNthReportByType(reports, StatsReport::kStatsReportTypeSsrc,
                                StatsReport::kStatsValueNameSsrc, 1),
      GetValueInNthReportByType(reports, StatsReport::kStatsReportTypeSsrc,
                                StatsReport::kStatsValueNameSsrc, 2)};
  EXPECT_THAT(ssrcs, UnorderedElementsAre(absl::StrCat(kFirstSsrc),
                                          absl::StrCat(kSecondSsrc)));

  // There is one track report with the same track ID as the SSRC reports.
  EXPECT_EQ(
      1u, GetReportsByType(reports, StatsReport::kStatsReportTypeTrack).size());
  EXPECT_EQ(kLocalTrackId, GetValueInNthReportByType(
                               reports, StatsReport::kStatsReportTypeTrack,
                               StatsReport::kStatsValueNameTrackId, 1));
}

}  // namespace webrtc
