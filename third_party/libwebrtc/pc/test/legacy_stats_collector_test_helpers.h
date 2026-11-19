/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_LEGACY_STATS_COLLECTOR_TEST_HELPERS_H_
#define PC_TEST_LEGACY_STATS_COLLECTOR_TEST_HELPERS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "api/legacy_stats_types.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "media/base/media_channel.h"
#include "pc/legacy_stats_collector.h"
#include "pc/peer_connection_internal.h"
#include "pc/test/fake_peer_connection_for_stats.h"
#include "pc/test/mock_rtp_receiver_internal.h"
#include "pc/test/mock_rtp_sender_internal.h"
#include "pc/transport_stats.h"
#include "rtc_base/fake_ssl_identity.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

// Error return values
inline constexpr char kNotFound[] = "NOT FOUND";

// Constant names for track identification.
inline constexpr char kLocalTrackId[] = "local_track_id";
inline constexpr char kRemoteTrackId[] = "remote_track_id";
inline constexpr uint32_t kSsrcOfTrack = 1234;

// Returns a FakeAudioTrack with the given ID and configuration.
scoped_refptr<AudioTrackInterface> CreateFakeAudioTrack(const std::string& id,
                                                        bool return_stats);

bool GetValue(const StatsReport* report,
              StatsReport::StatsValueName name,
              std::string* value);

std::string ExtractStatsValue(const StatsReport::StatsType& type,
                              const StatsReports& reports,
                              StatsReport::StatsValueName name);

StatsReport::Id TypedIdFromIdString(StatsReport::StatsType type,
                                    const std::string& value);

StatsReport::Id IdFromCertIdString(const std::string& cert_id);

// Finds the `n`-th report of type `type` in `reports`.
// `n` starts from 1 for finding the first report.
const StatsReport* FindNthReportByType(const StatsReports& reports,
                                       const StatsReport::StatsType& type,
                                       int n);

// Returns the value of the stat identified by `name` in the `n`-th report of
// type `type` in `reports`.
// `n` starts from 1 for finding the first report.
// If either the `n`-th report is not found, or the stat is not present in that
// report, then nullopt is returned.
std::optional<std::string> GetValueInNthReportByType(
    const StatsReports& reports,
    StatsReport::StatsType type,
    StatsReport::StatsValueName name,
    int n);

std::vector<const StatsReport*> GetReportsByType(const StatsReports& reports,
                                                 StatsReport::StatsType type);

const StatsReport* FindReportById(const StatsReports& reports,
                                  const StatsReport::Id& id);

std::string ExtractSsrcStatsValue(const StatsReports& reports,
                                  StatsReport::StatsValueName name);

std::string ExtractBweStatsValue(const StatsReports& reports,
                                 StatsReport::StatsValueName name);

std::string DerToPem(const std::string& der);

std::vector<std::string> DersToPems(const std::vector<std::string>& ders);

void CheckCertChainReports(const StatsReports& reports,
                           const std::vector<std::string>& ders,
                           const StatsReport::Id& start_id);

void VerifyVoiceReceiverInfoReport(const StatsReport* report,
                                   const VoiceReceiverInfo& info);

void VerifyVoiceSenderInfoReport(const StatsReport* report,
                                 const VoiceSenderInfo& sinfo);

// Helper methods to avoid duplication of code.
void InitVoiceSenderInfo(VoiceSenderInfo* voice_sender_info,
                         uint32_t ssrc = kSsrcOfTrack);

void UpdateVoiceSenderInfoFromAudioTrack(AudioTrackInterface* audio_track,
                                         VoiceSenderInfo* voice_sender_info,
                                         bool has_remote_tracks);

void InitVoiceReceiverInfo(VoiceReceiverInfo* voice_receiver_info);

class LegacyStatsCollectorTest : public ::testing::Test {
 protected:
  scoped_refptr<FakePeerConnectionForStats> CreatePeerConnection() {
    return make_ref_counted<FakePeerConnectionForStats>(
        CreateTestEnvironment({.time = &clock_}));
  }

  std::unique_ptr<LegacyStatsCollector> CreateStatsCollector(
      PeerConnectionInternal* pc) {
    return std::make_unique<LegacyStatsCollector>(
        pc, clock_, [this]() { return GetUtcTimeNowMs(); });
  }

  void VerifyAudioTrackStats(AudioTrackInterface* audio_track,
                             LegacyStatsCollector* stats,
                             const VoiceMediaInfo& voice_info,
                             StatsReports* reports) {
    stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);
    stats->InvalidateCache();
    stats->GetStats(nullptr, reports);

    // Verify the existence of the track report.
    const StatsReport* report =
        FindNthReportByType(*reports, StatsReport::kStatsReportTypeSsrc, 1);
    ASSERT_TRUE(report);
    EXPECT_EQ(GetUtcTimeNowMs(), report->timestamp());
    std::string track_id =
        ExtractSsrcStatsValue(*reports, StatsReport::kStatsValueNameTrackId);
    EXPECT_EQ(audio_track->id(), track_id);
    std::string ssrc_id =
        ExtractSsrcStatsValue(*reports, StatsReport::kStatsValueNameSsrc);
    EXPECT_EQ(absl::StrCat(kSsrcOfTrack), ssrc_id);

    std::string media_type =
        ExtractSsrcStatsValue(*reports, StatsReport::kStatsValueNameMediaType);
    EXPECT_EQ("audio", media_type);

    // Verifies the values in the track report.
    if (!voice_info.senders.empty()) {
      VerifyVoiceSenderInfoReport(report, voice_info.senders[0]);
    }
    if (!voice_info.receivers.empty()) {
      VerifyVoiceReceiverInfoReport(report, voice_info.receivers[0]);
    }

    // Verify we get the same result by passing a track to GetStats().
    StatsReports track_reports;  // returned values.
    stats->GetStats(audio_track, &track_reports);
    const StatsReport* track_report = FindNthReportByType(
        track_reports, StatsReport::kStatsReportTypeSsrc, 1);
    ASSERT_TRUE(track_report);
    EXPECT_EQ(GetUtcTimeNowMs(), track_report->timestamp());
    track_id = ExtractSsrcStatsValue(track_reports,
                                     StatsReport::kStatsValueNameTrackId);
    EXPECT_EQ(audio_track->id(), track_id);
    ssrc_id =
        ExtractSsrcStatsValue(track_reports, StatsReport::kStatsValueNameSsrc);
    EXPECT_EQ(absl::StrCat(kSsrcOfTrack), ssrc_id);
    if (!voice_info.senders.empty()) {
      VerifyVoiceSenderInfoReport(track_report, voice_info.senders[0]);
    }
    if (!voice_info.receivers.empty()) {
      VerifyVoiceReceiverInfoReport(track_report, voice_info.receivers[0]);
    }
  }

  void TestCertificateReports(const FakeSSLIdentity& local_identity,
                              const std::vector<std::string>& local_ders,
                              const FakeSSLIdentity& remote_identity,
                              const std::vector<std::string>& remote_ders) {
    const std::string kTransportName = "audio";  // Same as the mid.

    auto pc = CreatePeerConnection();
    auto stats = CreateStatsCollector(pc.get());

    pc->AddVoiceChannel(/*mid=*/"audio", kTransportName);

    // Fake stats to process.
    TransportChannelStats channel_stats;
    channel_stats.component = 1;
    channel_stats.srtp_crypto_suite = kSrtpAes128CmSha1_80;
    channel_stats.tls_cipher_suite_name = "cipher_suite_for_test";
    pc->SetTransportStats(kTransportName, channel_stats);

    // Fake certificate to report.
    scoped_refptr<RTCCertificate> local_certificate(
        RTCCertificate::Create(local_identity.Clone()));
    pc->SetLocalCertificate(kTransportName, local_certificate);
    pc->SetRemoteCertChain(kTransportName,
                           remote_identity.cert_chain().Clone());

    stats->UpdateStats(PeerConnectionInterface::kStatsOutputLevelStandard);

    StatsReports reports;
    stats->GetStats(nullptr, &reports);

    const StatsReport* channel_report =
        FindNthReportByType(reports, StatsReport::kStatsReportTypeComponent, 1);
    EXPECT_TRUE(channel_report);

    // Check local certificate chain.
    std::string local_certificate_id =
        ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                          StatsReport::kStatsValueNameLocalCertificateId);
    if (!local_ders.empty()) {
      EXPECT_NE(kNotFound, local_certificate_id);
      StatsReport::Id id(IdFromCertIdString(local_certificate_id));
      CheckCertChainReports(reports, local_ders, id);
    } else {
      EXPECT_EQ(kNotFound, local_certificate_id);
    }

    // Check remote certificate chain.
    std::string remote_certificate_id =
        ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                          StatsReport::kStatsValueNameRemoteCertificateId);
    if (!remote_ders.empty()) {
      EXPECT_NE(kNotFound, remote_certificate_id);
      StatsReport::Id id(IdFromCertIdString(remote_certificate_id));
      CheckCertChainReports(reports, remote_ders, id);
    } else {
      EXPECT_EQ(kNotFound, remote_certificate_id);
    }

    // Check negotiated ciphers.
    std::string dtls_cipher_suite =
        ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                          StatsReport::kStatsValueNameDtlsCipher);
    EXPECT_EQ(dtls_cipher_suite, "cipher_suite_for_test");
    std::string srtp_crypto_suite =
        ExtractStatsValue(StatsReport::kStatsReportTypeComponent, reports,
                          StatsReport::kStatsValueNameSrtpCipher);
    EXPECT_EQ(SrtpCryptoSuiteToName(kSrtpAes128CmSha1_80), srtp_crypto_suite);
  }

  int64_t GetUtcTimeNowMs() {
    return Clock::NtpToUtc(clock_.CurrentNtpTime()).ms();
  }

 protected:
  // Slight enhancement to SimulatedClock.
  // SimulatedClock uses a simplified model where its internal monotonic time is
  // assumed to be the Wall Clock time (relative to the Unix Epoch). It maps
  // CurrentTime() directly to NTP time by just adding the 1900-1970 offset.
  // Therefore, converting Ntp back to Utc just cancels the offset, ending up
  // returning the same value as CurrentTime(). This utility class makes sure
  // that these values are not the same so that we can accurately test when the
  // UTC time is being used.
  class OffsetSimulatedClock : public SimulatedClock {
   public:
    OffsetSimulatedClock(Timestamp start_time, TimeDelta utc_offset)
        : SimulatedClock(start_time), offset_(utc_offset) {}
    // Shifts the NTP calculation by the offset, simulating a Wall Clock
    // that is ahead of the system monotonic clock.
    NtpTime ConvertTimestampToNtpTime(Timestamp timestamp) override {
      return SimulatedClock::ConvertTimestampToNtpTime(timestamp + offset_);
    }

   private:
    const TimeDelta offset_;
  };

  OffsetSimulatedClock clock_{
      Timestamp::Millis(1337),
      TimeDelta::Seconds(int64_t{365} * 50 * 86400)};  // 50 years offset.

 private:
  test::RunLoop main_thread_;
};

scoped_refptr<MockRtpSenderInternal> CreateMockSender(
    scoped_refptr<MediaStreamTrackInterface> track,
    uint32_t ssrc);

scoped_refptr<MockRtpReceiverInternal> CreateMockReceiver(
    scoped_refptr<MediaStreamTrackInterface> track,
    uint32_t ssrc);

}  // namespace webrtc

#endif  // PC_TEST_LEGACY_STATS_COLLECTOR_TEST_HELPERS_H_
