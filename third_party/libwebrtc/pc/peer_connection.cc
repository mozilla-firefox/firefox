/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/peer_connection.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/adaptation/resource.h"
#include "api/audio/audio_device.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/data_channel_event_observer_interface.h"
#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/peer_connection_tracer_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtc_event_log_output.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "api/sequence_checker.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/bandwidth_estimation_settings.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/data_channel_transport_interface.h"
#include "api/transport/enums.h"
#include "api/turn_customizer.h"
#include "api/uma_metrics.h"
#include "api/video/video_codec_constants.h"
#include "call/audio_state.h"
#include "call/packet_receiver.h"
#include "call/payload_type.h"
#include "media/base/codec.h"
#include "media/base/media_engine.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/channel_interface.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/data_channel_utils.h"
#include "pc/dtls_transport.h"
#include "pc/ice_server_parsing.h"
#include "pc/jsep_transport_controller.h"
#include "pc/legacy_stats_collector.h"
#include "pc/media_session.h"
#include "pc/rtc_stats_collector.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/rtp_transmission_manager.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/sctp_data_channel.h"
#include "pc/sctp_transport.h"
#include "pc/sdp_offer_answer.h"
#include "pc/sdp_state_provider.h"
#include "pc/session_description.h"
#include "pc/transceiver_list.h"
#include "pc/transport_stats.h"
#include "pc/usage_pattern.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/trace_event.h"
#include "rtc_base/unique_id_generator.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {

namespace {
const int REPORT_USAGE_PATTERN_DELAY_MS = 60000;

class CodecLookupHelperForPeerConnection : public CodecLookupHelper {
 public:
  explicit CodecLookupHelperForPeerConnection(PeerConnection* self)
      : self_(self),
        codec_vendor_(self_->context()->media_engine(),
                      /*rtx_enabled=*/true,
                      self_->trials()) {}

  webrtc::PayloadTypeSuggester* PayloadTypeSuggester() override {
    return self_->pt_suggester();
  }

  CodecVendor* GetCodecVendor() override { return &codec_vendor_; }

 private:
  PeerConnection* self_;
  CodecVendor codec_vendor_;
};

uint32_t ConvertIceTransportTypeToCandidateFilter(
    PeerConnectionInterface::IceTransportsType type) {
  switch (type) {
    case PeerConnectionInterface::kNone:
      return CF_NONE;
    case PeerConnectionInterface::kRelay:
      return CF_RELAY;
    case PeerConnectionInterface::kNoHost:
      return (CF_ALL & ~CF_HOST);
    case PeerConnectionInterface::kAll:
      return CF_ALL;
    default:
      RTC_DCHECK_NOTREACHED();
  }
  return CF_NONE;
}

IceCandidatePairType GetIceCandidatePairType(const Candidate& local,
                                             const Candidate& remote) {
  if (local.is_local() && remote.is_local()) {
    bool local_hostname =
        !local.address().hostname().empty() && local.address().IsUnresolvedIP();
    bool remote_hostname = !remote.address().hostname().empty() &&
                           remote.address().IsUnresolvedIP();
    bool local_private = IPIsPrivate(local.address().ipaddr());
    bool remote_private = IPIsPrivate(remote.address().ipaddr());
    if (local_hostname) {
      if (remote_hostname) {
        return kIceCandidatePairHostNameHostName;
      } else if (remote_private) {
        return kIceCandidatePairHostNameHostPrivate;
      } else {
        return kIceCandidatePairHostNameHostPublic;
      }
    } else if (local_private) {
      if (remote_hostname) {
        return kIceCandidatePairHostPrivateHostName;
      } else if (remote_private) {
        return kIceCandidatePairHostPrivateHostPrivate;
      } else {
        return kIceCandidatePairHostPrivateHostPublic;
      }
    } else {
      if (remote_hostname) {
        return kIceCandidatePairHostPublicHostName;
      } else if (remote_private) {
        return kIceCandidatePairHostPublicHostPrivate;
      } else {
        return kIceCandidatePairHostPublicHostPublic;
      }
    }
  }

  if (local.is_local()) {
    if (remote.is_stun())
      return kIceCandidatePairHostSrflx;
    if (remote.is_relay())
      return kIceCandidatePairHostRelay;
    if (remote.is_prflx())
      return kIceCandidatePairHostPrflx;
  } else if (local.is_stun()) {
    if (remote.is_local())
      return kIceCandidatePairSrflxHost;
    if (remote.is_stun())
      return kIceCandidatePairSrflxSrflx;
    if (remote.is_relay())
      return kIceCandidatePairSrflxRelay;
    if (remote.is_prflx())
      return kIceCandidatePairSrflxPrflx;
  } else if (local.is_relay()) {
    if (remote.is_local())
      return kIceCandidatePairRelayHost;
    if (remote.is_stun())
      return kIceCandidatePairRelaySrflx;
    if (remote.is_relay())
      return kIceCandidatePairRelayRelay;
    if (remote.is_prflx())
      return kIceCandidatePairRelayPrflx;
  } else if (local.is_prflx()) {
    if (remote.is_local())
      return kIceCandidatePairPrflxHost;
    if (remote.is_stun())
      return kIceCandidatePairPrflxSrflx;
    if (remote.is_relay())
      return kIceCandidatePairPrflxRelay;
  }

  return kIceCandidatePairMax;
}

// Check if the changes of IceTransportsType motives an ice restart.
bool NeedIceRestart(bool surface_ice_candidates_on_ice_transport_type_changed,
                    PeerConnectionInterface::IceTransportsType current,
                    PeerConnectionInterface::IceTransportsType modified) {
  if (current == modified) {
    return false;
  }

  if (!surface_ice_candidates_on_ice_transport_type_changed) {
    return true;
  }

  auto current_filter = ConvertIceTransportTypeToCandidateFilter(current);
  auto modified_filter = ConvertIceTransportTypeToCandidateFilter(modified);

  // If surface_ice_candidates_on_ice_transport_type_changed is true and we
  // extend the filter, then no ice restart is needed.
  return (current_filter & modified_filter) != current_filter;
}

// Checks for valid pool size range and if a previous value has already been
// set, which is done via SetLocalDescription.
RTCError ValidateIceCandidatePoolSize(
    int ice_candidate_pool_size,
    std::optional<int> previous_ice_candidate_pool_size) {
  // Note that this isn't possible through chromium, since it's an unsigned
  // short in WebIDL.
  if (ice_candidate_pool_size < 0 ||
      ice_candidate_pool_size > static_cast<int>(UINT16_MAX)) {
    return RTCError(RTCErrorType::INVALID_RANGE);
  }

  // According to JSEP, after setLocalDescription, changing the candidate pool
  // size is not allowed, and changing the set of ICE servers will not result
  // in new candidates being gathered.
  if (previous_ice_candidate_pool_size.has_value() &&
      ice_candidate_pool_size != previous_ice_candidate_pool_size.value()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_MODIFICATION)
                         << "Can't change candidate pool size after calling "
                            "SetLocalDescription.");
  }

  return RTCError::OK();
}

// The simplest (and most future-compatible) way to tell if a config was
// modified in an invalid way is to copy each property we do support modifying,
// then use operator==. There are far more properties we don't support modifying
// than those we do, and more could be added.
// This helper function accepts a proposed new `configuration` object, an
// existing configuration and returns a valid, modified, configuration that's
// based on the existing configuration, with modified properties copied from
// `configuration`.
// If the result of creating a modified configuration doesn't pass the above
// `operator==` test or a call to `ValidateConfiguration()`, then the function
// will return an error. Otherwise, the return value will be the new config.
// Properties left out of the copy list can not be modified after construction:
//   - bundle_policy and rtcp_mux_policy, per RFC 8829 section 4.1.18.
//   - always_negotiate_data_channels, similar to bundle_policy.
//   - crypto_options.srtp.cryptex_policy, similar to bundle_policy.
//   - certificates.
RTCErrorOr<PeerConnectionInterface::RTCConfiguration> ApplyConfiguration(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    const PeerConnectionInterface::RTCConfiguration& existing_configuration) {
  PeerConnectionInterface::RTCConfiguration modified_config =
      existing_configuration;
  modified_config.type = configuration.type;
  modified_config.crypto_options = configuration.crypto_options;
  modified_config.crypto_options.srtp.cryptex_policy =
      existing_configuration.crypto_options.srtp.cryptex_policy;

  // ICE configuration.
  modified_config.servers = configuration.servers;
  modified_config.ice_candidate_pool_size =
      configuration.ice_candidate_pool_size;
  modified_config.prune_turn_ports = configuration.prune_turn_ports;
  modified_config.turn_port_prune_policy = configuration.turn_port_prune_policy;
  modified_config.surface_ice_candidates_on_ice_transport_type_changed =
      configuration.surface_ice_candidates_on_ice_transport_type_changed;
  modified_config.ice_check_min_interval = configuration.ice_check_min_interval;
  modified_config.ice_check_interval_strong_connectivity =
      configuration.ice_check_interval_strong_connectivity;
  modified_config.ice_check_interval_weak_connectivity =
      configuration.ice_check_interval_weak_connectivity;
  modified_config.ice_unwritable_timeout = configuration.ice_unwritable_timeout;
  modified_config.ice_unwritable_min_checks =
      configuration.ice_unwritable_min_checks;
  modified_config.ice_inactive_timeout = configuration.ice_inactive_timeout;
  modified_config.stun_candidate_keepalive_interval =
      configuration.stun_candidate_keepalive_interval;
  modified_config.turn_customizer = configuration.turn_customizer;
  modified_config.network_preference = configuration.network_preference;
  modified_config.turn_logging_id = configuration.turn_logging_id;
  modified_config.stable_writable_connection_ping_interval_ms =
      configuration.stable_writable_connection_ping_interval_ms;

  if (configuration != modified_config) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_MODIFICATION)
        << "Modifying the configuration in an unsupported way.");
  }

  RTCError err = IceConfig(modified_config).IsValid();
  if (!err.ok()) {
    return err;
  }

  return modified_config;
}

bool HasRtcpMuxEnabled(const ContentInfo* content) {
  return content->media_description()->rtcp_mux();
}

bool DtlsEnabled(const PeerConnectionInterface::RTCConfiguration& configuration,
                 const PeerConnectionFactoryInterface::Options& options,
                 const PeerConnectionDependencies& dependencies) {
  if (options.disable_encryption)
    return false;

  // Enable DTLS by default if we have an identity store or a certificate.
  return (dependencies.cert_generator || !configuration.certificates.empty());
}

void NoteServerUsage(UsagePattern& usage_pattern,
                     const ServerAddresses& stun_servers,
                     const std::vector<RelayServerConfig>& turn_servers) {
  if (!stun_servers.empty()) {
    usage_pattern.NoteUsageEvent(UsageEvent::STUN_SERVER_ADDED);
  }
  if (!turn_servers.empty()) {
    usage_pattern.NoteUsageEvent(UsageEvent::TURN_SERVER_ADDED);
  }
}

template <typename Observer,
          typename std::enable_if_t<
              std::is_same_v<Observer, SetSessionDescriptionObserver*>,
              bool> = true>
absl::AnyInvocable<void() &&> ReportFailure(Observer& o, RTCError error) {
  return
      [o = scoped_refptr<SetSessionDescriptionObserver>(o),
       error = std::move(error)]() mutable { o->OnFailure(std::move(error)); };
}

template <
    typename Observer,
    typename std::enable_if_t<
        std::is_same_v<Observer,
                       scoped_refptr<SetLocalDescriptionObserverInterface>>,
        bool> = true>
absl::AnyInvocable<void() &&> ReportFailure(Observer& o, RTCError error) {
  return [o = std::move(o), error = std::move(error)]() mutable {
    o->OnSetLocalDescriptionComplete(std::move(error));
  };
}

template <
    typename Observer,
    typename std::enable_if_t<
        std::is_same_v<Observer,
                       scoped_refptr<SetRemoteDescriptionObserverInterface>>,
        bool> = true>
absl::AnyInvocable<void() &&> ReportFailure(Observer& o, RTCError error) {
  return [o = std::move(o), error = std::move(error)]() mutable {
    o->OnSetRemoteDescriptionComplete(std::move(error));
  };
}

// Checks if the observer and description pointers are valid.
// If there's an error, the function will return false and optionally
// notify the observer asynchronously of the error.
// If both are valid, the function will reset the thread ownership of
// the description object and return true.
template <typename Observer, typename Description>
bool CheckValidSetDescription(Observer& observer,
                              Description& desc,
                              Thread* signaling) {
  if (!observer) {
    RTC_LOG(LS_ERROR) << "Observer is NULL.";
    return false;
  } else if (!desc) {
    signaling->PostTask(
        ReportFailure(observer, RTCError(RTCErrorType::INVALID_PARAMETER,
                                         "SessionDescription is NULL.")));
    return false;
  }
  // Make sure the description object now considers the current thread its home
  // by detaching from any potential previous thread.
  desc->RelinquishThreadOwnership();
  return true;
}

}  // namespace

scoped_refptr<PeerConnection> PeerConnection::Create(
    const Environment& env,
    scoped_refptr<ConnectionContext> context,
    const PeerConnectionFactoryInterface::Options& options,
    std::unique_ptr<Call> call,
    const PeerConnectionInterface::RTCConfiguration& configuration,
    PeerConnectionDependencies& dependencies,
    const ServerAddresses& stun_servers,
    const std::vector<RelayServerConfig>& turn_servers) {
  RTC_DCHECK(IceConfig(configuration).IsValid().ok());
  RTC_DCHECK(dependencies.observer);
  RTC_DCHECK(dependencies.async_dns_resolver_factory);
  RTC_DCHECK(dependencies.allocator);

  bool is_unified_plan =
      configuration.sdp_semantics == SdpSemantics::kUnifiedPlan;
  bool dtls_enabled = DtlsEnabled(configuration, options, dependencies);

  TRACE_EVENT0("webrtc", "PeerConnection::Create");
  return make_ref_counted<PeerConnection>(
      configuration, env, context, options, is_unified_plan, std::move(call),
      dependencies, stun_servers, turn_servers, dtls_enabled);
}

PeerConnection::PeerConnection(
    const PeerConnectionInterface::RTCConfiguration& configuration,
    const Environment& env,
    scoped_refptr<ConnectionContext> context,
    const PeerConnectionFactoryInterface::Options& options,
    bool is_unified_plan,
    std::unique_ptr<Call> call,
    PeerConnectionDependencies& dependencies,
    const ServerAddresses& stun_servers,
    const std::vector<RelayServerConfig>& turn_servers,
    bool dtls_enabled)
    : env_(env),
      context_(std::move(context)),
      options_(options),
      observer_(dependencies.observer),
      is_unified_plan_(is_unified_plan),
      dtls_enabled_(dtls_enabled),
      configuration_(configuration),
      async_dns_resolver_factory_(
          std::move(dependencies.async_dns_resolver_factory)),
      port_allocator_(std::move(dependencies.allocator)),
      lna_permission_factory_(std::move(dependencies.lna_permission_factory)),
      ice_transport_factory_(std::move(dependencies.ice_transport_factory)),
      dtls_transport_factory_(std::move(dependencies.dtls_transport_factory)),
      rtp_transport_factory_(std::move(dependencies.rtp_transport_factory)),
      tls_cert_verifier_(std::move(dependencies.tls_cert_verifier)),
      call_(std::move(call)),
      network_thread_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(true,
                                                           network_thread())),
      worker_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/call_ != nullptr,
          worker_thread())),
      call_ptr_(call_.get()),
      legacy_stats_(std::make_unique<LegacyStatsCollector>(this, env_.clock())),
      stats_collector_(this, env_),
      // RFC 3264: The numeric value of the session id and version in the
      // o line MUST be representable with a "64 bit signed integer".
      // Due to this constraint session id `session_id_` is max limited to
      // LLONG_MAX.
      session_id_(absl::StrCat(CreateRandomId64() & LLONG_MAX)),
      data_channel_controller_(this),
      message_handler_(signaling_thread()),
      tracer_(std::move(dependencies.tracer)),
      codec_lookup_helper_(
          std::make_unique<CodecLookupHelperForPeerConnection>(this)) {
  // Field trials specific to the peerconnection should be owned by the `env`,
  RTC_DCHECK(dependencies.trials == nullptr);

  // Enable SNAP from field trial unless enabled explicitly.
  if (!configuration_.enable_sctp_snap) {
    configuration_.enable_sctp_snap =
        env.field_trials().IsEnabled("WebRTC-Sctp-Snap");
  }

  if (tracer_) {
    tracer_->OnCreate(configuration_);
  }

  std::vector<IceParameters> pooled_credentials;
  std::tie(transport_controller_copy_, pooled_credentials) =
      InitializeNetworkThread(stun_servers, turn_servers);

  if (call_ptr_) {
    worker_thread()->BlockingCall([this] {
      RTC_DCHECK_RUN_ON(worker_thread());
      if (context_->is_configured_for_media()) {
        media_engine_ref_ =
            std::make_unique<ConnectionContext::MediaEngineReference>(context_);
      }
    });
  }

  sdp_handler_ = SdpOfferAnswerHandler::Create(
      env_, this, std::move(dependencies.cert_generator),
      std::move(dependencies.video_bitrate_allocator_factory), context_.get(),
      codec_lookup_helper_.get());
  sdp_handler_->UpdateCachedIceCredentials(std::move(pooled_credentials));
  rtp_manager_ = std::make_unique<RtpTransmissionManager>(
      env_, call_ptr_, IsUnifiedPlan(), context_.get(),
      codec_lookup_helper_.get(), &usage_pattern_, observer_,
      legacy_stats_.get(), [this]() {
        RTC_DCHECK_RUN_ON(signaling_thread());
        sdp_handler_->UpdateNegotiationNeeded();
      });
  // Add default audio/video transceivers for Plan B SDP.
  if (!IsUnifiedPlan() && ConfiguredForMedia()) {
    rtp_manager_->transceivers()->Add(
        RtpTransceiverProxyWithInternal<RtpTransceiver>::Create(
            signaling_thread(),
            make_ref_counted<RtpTransceiver>(
                env_, MediaType::AUDIO, context_.get(),
                codec_lookup_helper_.get(), legacy_stats_.get(),
                sdp_handler_->audio_options(), sdp_handler_->video_options())));
    rtp_manager_->transceivers()->Add(
        RtpTransceiverProxyWithInternal<RtpTransceiver>::Create(
            signaling_thread(),
            make_ref_counted<RtpTransceiver>(
                env_, MediaType::VIDEO, context_.get(),
                codec_lookup_helper_.get(), legacy_stats_.get(),
                sdp_handler_->audio_options(), sdp_handler_->video_options())));
  }

  const int delay_ms = configuration_.report_usage_pattern_delay_ms
                           ? *configuration_.report_usage_pattern_delay_ms
                           : REPORT_USAGE_PATTERN_DELAY_MS;
  message_handler_.RequestUsagePatternReport(
      [this]() {
        RTC_DCHECK_RUN_ON(signaling_thread());
        ReportUsagePattern();
      },
      delay_ms);
}

PeerConnection::~PeerConnection() {
  TRACE_EVENT0("webrtc", "PeerConnection::~PeerConnection");
  RTC_DCHECK_RUN_ON(signaling_thread());

  sdp_handler_->PrepareForShutdown();

  // In case `Close()` wasn't called, always make sure the controller cancels
  // potentially pending operations.
  data_channel_controller_.PrepareForShutdown();

  ScopedOperationsBatcher network_tasks(network_thread());
  ScopedOperationsBatcher worker_tasks(worker_thread());

  // Stop transceivers before destroying the stats collector because
  // AudioRtpSender has a reference to the LegacyStatsCollector that it will
  // update when stopping. The BaseChannels will eventually be deleted below
  // when all the network and worker tasks are executed.
  sdp_handler_->GetMediaChannelTeardownTasks(network_tasks, worker_tasks);

  legacy_stats_.reset(nullptr);
  stats_collector_.CancelPendingRequestAndGetShutdownTasks(network_tasks,
                                                           worker_tasks);
  network_tasks.AddWithFinalizer(MakeCloseOnNetworkThreadTask());

  // call_ must be destroyed on the worker thread.
  worker_tasks.Add([this]() {
    RTC_DCHECK_RUN_ON(worker_thread());
    worker_thread_safety_->SetNotAlive();
    call_.reset();
    media_engine_ref_.reset();
  });

  network_tasks.Run();
  worker_tasks.Run();

  if (sdp_handler_) {
    sdp_handler_->ResetSessionDescFactory();
  }

  data_channel_controller_.PrepareForShutdown();
}

std::pair<JsepTransportController*, std::vector<IceParameters>>
PeerConnection::InitializeNetworkThread(
    const ServerAddresses& stun_servers,
    const std::vector<RelayServerConfig>& turn_servers) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  NoteServerUsage(usage_pattern_, stun_servers, turn_servers);

  JsepTransportController::Config config;
  config.redetermine_role_on_ice_restart =
      configuration_.redetermine_role_on_ice_restart;
  config.ssl_max_version = options_.ssl_max_version;
  config.disable_encryption = options_.disable_encryption;
  config.bundle_policy = configuration_.bundle_policy;
  config.rtcp_mux_policy = configuration_.rtcp_mux_policy;
  config.crypto_options = configuration_.crypto_options;

  // Maybe enable or disable PQC from FieldTrials
  config.crypto_options.ephemeral_key_exchange_cipher_groups.Update(&trials());
  config.transport_observer = this;
  config.rtcp_handler = InitializeRtcpCallback();
  config.un_demuxable_packet_handler = InitializeUnDemuxablePacketHandler();

  // DTLS has to be enabled to use SCTP.
  if (dtls_enabled_) {
    config.sctp_factory = context_->sctp_transport_factory();
  }

  config.ice_transport_factory = ice_transport_factory_.get();
  config.dtls_transport_factory = dtls_transport_factory_.get();
  config.rtp_transport_factory = rtp_transport_factory_.get();
  config.on_dtls_handshake_error = [](SSLHandshakeError s) {
    RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.DtlsHandshakeError",
                              static_cast<int>(s),
                              static_cast<int>(SSLHandshakeError::MAX_VALUE));
  };
  config.signal_ice_candidates_gathered =
      [this](absl::string_view transport,
             const std::vector<Candidate>& candidates) {
        RTC_DCHECK_RUN_ON(network_thread());
        signaling_thread()->PostTask(
            SafeTask(signaling_thread_safety_.flag(),
                     [this, t = std::string(transport), c = candidates]() {
                       RTC_DCHECK_RUN_ON(signaling_thread());
                       OnTransportControllerCandidatesGathered(t, c);
                     }));
      };
  config.signal_ice_connection_state = [this](webrtc::IceConnectionState s) {
    RTC_DCHECK_RUN_ON(network_thread());
    signaling_thread()->PostTask(
        SafeTask(signaling_thread_safety_.flag(), [this, s]() {
          RTC_DCHECK_RUN_ON(signaling_thread());
          OnTransportControllerConnectionState(s);
        }));
  };
  config.signal_connection_state =
      [this](PeerConnectionInterface::PeerConnectionState s) {
        RTC_DCHECK_RUN_ON(network_thread());
        signaling_thread()->PostTask(
            SafeTask(signaling_thread_safety_.flag(), [this, s]() {
              RTC_DCHECK_RUN_ON(signaling_thread());
              SetConnectionState(s);
            }));
      };
  config.signal_standardized_ice_connection_state =
      [this](PeerConnectionInterface::IceConnectionState s) {
        RTC_DCHECK_RUN_ON(network_thread());
        signaling_thread()->PostTask(
            SafeTask(signaling_thread_safety_.flag(), [this, s]() {
              RTC_DCHECK_RUN_ON(signaling_thread());
              SetStandardizedIceConnectionState(s);
            }));
      };
  config.signal_ice_gathering_state = [this](::webrtc::IceGatheringState s) {
    RTC_DCHECK_RUN_ON(network_thread());
    signaling_thread()->PostTask(
        SafeTask(signaling_thread_safety_.flag(), [this, s]() {
          RTC_DCHECK_RUN_ON(signaling_thread());
          OnTransportControllerGatheringState(s);
        }));
  };
  config.signal_ice_candidate_error =
      [this](const IceCandidateErrorEvent& event) {
        RTC_DCHECK_RUN_ON(network_thread());
        signaling_thread()->PostTask(
            SafeTask(signaling_thread_safety_.flag(), [this, event = event]() {
              RTC_DCHECK_RUN_ON(signaling_thread());
              OnTransportControllerCandidateError(event);
            }));
      };
  config.signal_ice_candidates_removed =
      [this](IceTransportInternal* transport, const std::vector<Candidate>& c) {
        RTC_DCHECK_RUN_ON(network_thread());
        std::string mid = transport->transport_name();
        signaling_thread()->PostTask(SafeTask(
            signaling_thread_safety_.flag(), [this, mid = mid, c = c]() {
              RTC_DCHECK_RUN_ON(signaling_thread());
              OnTransportControllerCandidatesRemoved(mid, c);
            }));
      };
  config.signal_ice_candidate_pair_changed =
      [this](const CandidatePairChangeEvent& event) {
        RTC_DCHECK_RUN_ON(network_thread());
        signaling_thread()->PostTask(
            SafeTask(signaling_thread_safety_.flag(), [this, event = event]() {
              RTC_DCHECK_RUN_ON(signaling_thread());
              OnTransportControllerCandidateChanged(event);
            }));
      };

  auto transport_controller = std::make_unique<JsepTransportController>(
      env_, signaling_thread(), network_thread(), port_allocator_.get(),
      async_dns_resolver_factory_.get(), lna_permission_factory_.get(),
      std::move(config));

  return network_thread()->BlockingCall([&, config = &configuration_,
                                         controller = std::move(
                                             transport_controller)]() mutable {
    RTC_DCHECK_RUN_ON(network_thread());
    RTC_DCHECK(network_thread_safety_->alive());
    InitializePortAllocatorResult pa_result =
        InitializePortAllocator_n(stun_servers, turn_servers, *config);
    // Send information about IPv4/IPv6 status.
    PeerConnectionAddressFamilyCounter address_family =
        pa_result.enable_ipv6 ? kPeerConnection_IPv6 : kPeerConnection_IPv4;
    RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.IPMetrics", address_family,
                              kPeerConnectionAddressFamilyCounter_Max);
    JsepTransportController* controller_ptr =
        InitializeTransportController_n(std::move(controller), *config);
    std::vector<IceParameters> credentials =
        port_allocator_->GetPooledIceCredentials();
    return std::make_pair(controller_ptr, credentials);
  });
}

ScopedOperationsBatcher::BatchTaskWithFinalizer
PeerConnection::MakeCloseOnNetworkThreadTask() {
  RTC_DCHECK_RUN_ON(signaling_thread());

  if (!transport_controller_copy_) {
    return nullptr;
  }

  auto jsep_close_task = transport_controller_copy_->MakeCloseTask();

  return [this, jsep_close_task = std::move(jsep_close_task)]() mutable
             -> ScopedOperationsBatcher::BatchTaskResult {
    RTC_DCHECK_RUN_ON(network_thread());
    if (network_thread_safety_->alive()) {
      // port_allocator_ and transport_controller_ live on the network thread
      // and must be destroyed there.
      TeardownDataChannelTransport_n(RTCError::OK());
      if (call_ptr_) {
        call_ptr_->DisconnectFromNetworkThread();
      }
      port_allocator_->DiscardCandidatePool();

      std::move(jsep_close_task)();

      transport_controller_.reset();
      port_allocator_.reset();
      network_thread_safety_->SetNotAlive();
    }

    return ScopedOperationsBatcher::FinalizerTask([this]() {
      RTC_DCHECK_RUN_ON(signaling_thread());
      if (transport_controller_copy_) {
        transport_controller_copy_ = nullptr;
        sctp_mid_s_.reset();
        SetSctpTransportName("");
      } else {
        RTC_DCHECK(!sctp_mid_s_);
        RTC_DCHECK(sctp_transport_name_s_.empty());
      }
    });
  };
}

JsepTransportController* PeerConnection::InitializeTransportController_n(
    std::unique_ptr<JsepTransportController> controller,
    const RTCConfiguration& configuration) {
  transport_controller_ = std::move(controller);

  IceConfig ice_config(configuration);
  ice_config.dtls_handshake_in_stun = CanAttemptDtlsStunPiggybacking();

  transport_controller_->SetIceConfig(ice_config);
  return transport_controller_.get();
}

scoped_refptr<StreamCollectionInterface> PeerConnection::local_streams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "local_streams is not available with Unified "
                                 "Plan SdpSemantics. Please use GetSenders "
                                 "instead.";
  return sdp_handler_->local_streams();
}

scoped_refptr<StreamCollectionInterface> PeerConnection::remote_streams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "remote_streams is not available with Unified "
                                 "Plan SdpSemantics. Please use GetReceivers "
                                 "instead.";
  return sdp_handler_->remote_streams();
}

bool PeerConnection::AddStream(MediaStreamInterface* local_stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(!IsUnifiedPlan()) << "AddStream is not available with Unified Plan "
                                 "SdpSemantics. Please use AddTrack instead.";
  TRACE_EVENT0("webrtc", "PeerConnection::AddStream");
  if (!ConfiguredForMedia()) {
    RTC_LOG(LS_ERROR) << "AddStream: Not configured for media";
    return false;
  }
  return sdp_handler_->AddStream(local_stream);
}

void PeerConnection::RemoveStream(MediaStreamInterface* local_stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(ConfiguredForMedia());
  RTC_CHECK(!IsUnifiedPlan()) << "RemoveStream is not available with Unified "
                                 "Plan SdpSemantics. Please use RemoveTrack "
                                 "instead.";
  TRACE_EVENT0("webrtc", "PeerConnection::RemoveStream");
  sdp_handler_->RemoveStream(local_stream);
}

RTCErrorOr<scoped_refptr<RtpSenderInterface>> PeerConnection::AddTrack(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids) {
  return AddTrack(std::move(track), stream_ids, nullptr);
}

RTCErrorOr<scoped_refptr<RtpSenderInterface>> PeerConnection::AddTrack(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>& init_send_encodings) {
  return AddTrack(std::move(track), stream_ids, &init_send_encodings);
}

RTCErrorOr<scoped_refptr<RtpSenderInterface>> PeerConnection::AddTrack(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>* init_send_encodings) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "PeerConnection::AddTrack");
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }
  if (!track) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Track is null.");
  }
  if (!(track->kind() == MediaStreamTrackInterface::kAudioKind ||
        track->kind() == MediaStreamTrackInterface::kVideoKind)) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Track has invalid kind: " << track->kind());
  }
  if (IsClosed()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_STATE)
                         << "PeerConnection is closed.");
  }
  if (rtp_manager()->FindSenderForTrack(track.get())) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Sender already exists for track " << track->id()
                         << ".");
  }
  RTCErrorOr<scoped_refptr<RtpSenderInterface>> sender_or_error;
  if (IsUnifiedPlan()) {
    sender_or_error = rtp_manager()->AddTrackUnifiedPlan(
        configuration_.media_config, sdp_handler_->audio_options(),
        sdp_handler_->video_options(), GetCryptoOptions(),
        sdp_handler_->video_bitrate_allocator_factory(), track, stream_ids,
        init_send_encodings);
  } else {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    sender_or_error =
        rtp_manager()->AddTrackPlanB(track, stream_ids, init_send_encodings);
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }
  if (sender_or_error.ok()) {
    if (tracer_) {
      tracer_->OnAddTrack(*track, stream_ids);
    }
    sdp_handler_->UpdateNegotiationNeeded();
    legacy_stats_->AddTrack(track.get());
  }
  return sender_or_error;
}

RTCError PeerConnection::RemoveTrackOrError(
    scoped_refptr<RtpSenderInterface> sender) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }
  if (!sender) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Sender is null.");
  }
  if (IsClosed()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_STATE)
                         << "PeerConnection is closed.");
  }
  if (IsUnifiedPlan()) {
    auto transceiver = FindTransceiverBySender(sender);
    if (!transceiver || !sender->track()) {
      return RTCError::OK();
    }
    sender->SetTrack(nullptr);
    if (transceiver->direction() == RtpTransceiverDirection::kSendRecv) {
      transceiver->internal()->set_direction(
          RtpTransceiverDirection::kRecvOnly);
    } else if (transceiver->direction() == RtpTransceiverDirection::kSendOnly) {
      transceiver->internal()->set_direction(
          RtpTransceiverDirection::kInactive);
    }
  } else {
    bool removed;
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    if (sender->media_type() == webrtc::MediaType::AUDIO) {
      removed =
          rtp_manager()->GetAudioTransceiver()->internal()->RemoveSenderPlanB(
              sender.get());
    } else {
      RTC_DCHECK_EQ(webrtc::MediaType::VIDEO, sender->media_type());
      removed =
          rtp_manager()->GetVideoTransceiver()->internal()->RemoveSenderPlanB(
              sender.get());
    }
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
    if (!removed) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "Couldn't find sender " << sender->id()
                           << " to remove.");
    }
  }
  sdp_handler_->UpdateNegotiationNeeded();
  return RTCError::OK();
}

scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
PeerConnection::FindTransceiverBySender(
    scoped_refptr<RtpSenderInterface> sender) {
  return rtp_manager()->transceivers()->FindBySender(sender);
}

RTCErrorOr<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::AddTransceiver(scoped_refptr<MediaStreamTrackInterface> track) {
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }

  return AddTransceiver(track, RtpTransceiverInit());
}

RTCErrorOr<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::AddTransceiver(scoped_refptr<MediaStreamTrackInterface> track,
                               const RtpTransceiverInit& init) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }
  RTC_CHECK(IsUnifiedPlan())
      << "AddTransceiver is only available with Unified Plan SdpSemantics";
  if (!track) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "track is null");
  }
  MediaType media_type;
  if (track->kind() == MediaStreamTrackInterface::kAudioKind) {
    media_type = MediaType::AUDIO;
  } else if (track->kind() == MediaStreamTrackInterface::kVideoKind) {
    media_type = MediaType::VIDEO;
  } else {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Track kind is not audio or video");
  }
  return AddTransceiver(media_type, track, init);
}

RTCErrorOr<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::AddTransceiver(MediaType media_type) {
  return AddTransceiver(media_type, RtpTransceiverInit());
}

RTCErrorOr<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::AddTransceiver(MediaType media_type,
                               const RtpTransceiverInit& init) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }
  RTC_CHECK(IsUnifiedPlan())
      << "AddTransceiver is only available with Unified Plan SdpSemantics";
  if (!(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO)) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "media type is not audio or video");
  }
  return AddTransceiver(media_type, nullptr, init);
}

RTCErrorOr<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::AddTransceiver(MediaType media_type,
                               scoped_refptr<MediaStreamTrackInterface> track,
                               const RtpTransceiverInit& init,
                               bool update_negotiation_needed) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::UNSUPPORTED_OPERATION)
                         << "Not configured for media");
  }
  RTC_DCHECK(
      (media_type == MediaType::AUDIO || media_type == MediaType::VIDEO));
  if (track) {
    RTC_DCHECK_EQ(media_type,
                  (track->kind() == MediaStreamTrackInterface::kAudioKind
                       ? MediaType::AUDIO
                       : MediaType::VIDEO));
  }
  if (IsClosed()) {
    return RTC_LOG_ERROR(RTCError::InvalidState()
                         << "PeerConnection is closed.");
  }

  size_t num_rids = absl::c_count_if(init.send_encodings,
                                     [](const RtpEncodingParameters& encoding) {
                                       return !encoding.rid.empty();
                                     });
  if (num_rids > 0 && num_rids != init.send_encodings.size()) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_PARAMETER)
        << "RIDs must be provided for either all or none of the "
           "send encodings.");
  }

  if (num_rids > 0 && absl::c_any_of(init.send_encodings,
                                     [](const RtpEncodingParameters& encoding) {
                                       return !IsLegalRsidName(encoding.rid);
                                     })) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "Invalid RID value provided.");
  }

  if (absl::c_any_of(init.send_encodings,
                     [](const RtpEncodingParameters& encoding) {
                       return encoding.ssrc.has_value();
                     })) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::UNSUPPORTED_PARAMETER)
        << "Attempted to set an unimplemented parameter of RtpParameters.");
  }

  RtpParameters parameters;
  parameters.encodings = init.send_encodings;

  const bool has_scale_resolution_down_by =
      media_type == MediaType::VIDEO &&
      absl::c_any_of(parameters.encodings,
                     [](const RtpEncodingParameters& encoding) {
                       return encoding.scale_resolution_down_by.has_value();
                     });
  if (has_scale_resolution_down_by) {
    for (RtpEncodingParameters& encoding : parameters.encodings) {
      if (!encoding.scale_resolution_down_by.has_value()) {
        encoding.scale_resolution_down_by = 1.0;
      }
    }
  }

  // Encodings are dropped from the tail if too many are provided.
  size_t max_simulcast_streams =
      media_type == MediaType::VIDEO ? kMaxSimulcastStreams : 1u;
  if (parameters.encodings.size() > max_simulcast_streams) {
    parameters.encodings.erase(
        parameters.encodings.begin() + max_simulcast_streams,
        parameters.encodings.end());
  }

  // Single RID should be removed.
  if (parameters.encodings.size() == 1 &&
      !parameters.encodings[0].rid.empty()) {
    RTC_LOG(LS_INFO) << "Removing RID: " << parameters.encodings[0].rid << ".";
    parameters.encodings[0].rid.clear();
  }

  // If RIDs were not provided, they are generated for simulcast scenario.
  if (parameters.encodings.size() > 1 && num_rids == 0) {
    UniqueStringGenerator rid_generator;
    for (RtpEncodingParameters& encoding : parameters.encodings) {
      encoding.rid = rid_generator.GenerateString();
    }
  }

  // If no encoding parameters were provided, a default entry is created.
  if (parameters.encodings.empty()) {
    parameters.encodings.push_back({});
  }

  if (media_type == MediaType::VIDEO && !has_scale_resolution_down_by) {
    double scale_resolution_down_by = 1.0;
    for (auto encoding = parameters.encodings.rbegin();
         encoding != parameters.encodings.rend(); ++encoding) {
      encoding->scale_resolution_down_by = scale_resolution_down_by;
      scale_resolution_down_by *= 2.0;
    }
  }

  if (UnimplementedRtpParameterHasValue(parameters)) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::UNSUPPORTED_PARAMETER)
        << "Attempted to set an unimplemented parameter of RtpParameters.");
  }

  std::vector<Codec> codecs;
  // Gather the current codec capabilities to allow checking scalabilityMode and
  // codec selection against supported values.
  CodecVendor codec_vendor(context_->media_engine(), /*rtx_enabled=*/false,
                           trials());
  if (media_type == MediaType::VIDEO) {
    codecs = codec_vendor.video_send_codecs().codecs();
  } else {
    codecs = codec_vendor.audio_send_codecs().codecs();
  }

  auto result = CheckRtpParametersValues(parameters, codecs, std::nullopt,
                                         env_.field_trials());
  if (!result.ok()) {
    if (result.type() == RTCErrorType::INVALID_MODIFICATION) {
      result.set_type(RTCErrorType::UNSUPPORTED_OPERATION);
    }
    return RTC_LOG_ERROR(RTCError(result.type()) << result.message());
  }

  RTC_LOG(LS_INFO) << "Adding " << MediaTypeToString(media_type)
                   << " transceiver in response to a call to AddTransceiver.";
  // Set the sender ID equal to the track ID if the track is specified unless
  // that sender ID is already in use.
  std::string sender_id = (track && !rtp_manager()->FindSenderById(track->id())
                               ? track->id()
                               : CreateRandomUuid());
  auto transceiver = rtp_manager()->CreateAndAddTransceiver(
      configuration_.media_config, sdp_handler_->audio_options(),
      sdp_handler_->video_options(), configuration_.crypto_options,
      sdp_handler_->video_bitrate_allocator_factory(), media_type, track,
      init.stream_ids, parameters.encodings,
      /*header_extensions_to_negotiate=*/{},
      /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{}, sender_id);
  transceiver->internal()->set_direction(init.direction);

  // Only the internal offerToReceive path passes update_negotiation_needed
  // false, so it doubles as the "called by the application" condition.
  if (update_negotiation_needed) {
    if (tracer_) {
      tracer_->OnAddTransceiver(media_type, track.get(), init);
    }
    sdp_handler_->UpdateNegotiationNeeded();
  }

  return scoped_refptr<RtpTransceiverInterface>(transceiver);
}


scoped_refptr<RtpSenderInterface> PeerConnection::CreateSender(
    const std::string& kind,
    const std::string& stream_id) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!ConfiguredForMedia()) {
    RTC_LOG(LS_ERROR) << "Not configured for media";
    return nullptr;
  }
  RTC_CHECK(!IsUnifiedPlan()) << "CreateSender is not available with Unified "
                                 "Plan SdpSemantics. Please use AddTransceiver "
                                 "instead.";
  TRACE_EVENT0("webrtc", "PeerConnection::CreateSender");
  if (IsClosed()) {
    return nullptr;
  }

  // Internally we need to have one stream with Plan B semantics, so we
  // generate a random stream ID if not specified.
  std::vector<std::string> stream_ids;
  if (stream_id.empty()) {
    stream_ids.push_back(CreateRandomUuid());
    RTC_LOG(LS_INFO)
        << "No stream_id specified for sender. Generated stream ID: "
        << stream_ids[0];
  } else {
    stream_ids.push_back(stream_id);
  }

  scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> new_sender;
  CodecVendor codec_vendor(context_->media_engine(), /*rtx_enabled=*/false,
                           trials());

  if (kind == MediaStreamTrackInterface::kAudioKind) {
    auto audio_sender = AudioRtpSender::Create(
        env_, signaling_thread(), worker_thread(), CreateRandomUuid(),
        legacy_stats_.get(), nullptr,
        /*enable_sframe_at_owner=*/nullptr,
        rtp_manager()->voice_media_send_channel(), stream_ids,
        /*init_send_encodings=*/std::vector<RtpEncodingParameters>(1),
        codec_vendor.audio_send_codecs().codecs());
    new_sender = RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
        signaling_thread(), audio_sender);
    rtp_manager()->GetAudioTransceiver()->internal()->AddSenderPlanB(
        new_sender);
  } else if (kind == MediaStreamTrackInterface::kVideoKind) {
    auto video_sender = VideoRtpSender::Create(
        env_, signaling_thread(), worker_thread(), CreateRandomUuid(), nullptr,
        /*enable_sframe_at_owner=*/nullptr,
        rtp_manager()->video_media_send_channel(),
        /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
        /*initial_simulcast_layers=*/{}, stream_ids,
        codec_vendor.video_send_codecs().codecs());
    new_sender = RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
        signaling_thread(), video_sender);
    rtp_manager()->GetVideoTransceiver()->internal()->AddSenderPlanB(
        new_sender);
  } else {
    RTC_LOG(LS_ERROR) << "CreateSender called with invalid kind: " << kind;
  }

  return new_sender;
}

std::vector<scoped_refptr<RtpSenderInterface>> PeerConnection::GetSenders()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<RtpSenderInterface>> ret;
  if (ConfiguredForMedia()) {
    for (const auto& sender : rtp_manager()->GetSendersInternal()) {
      ret.push_back(sender);
    }
  }
  return ret;
}

std::vector<scoped_refptr<RtpReceiverInterface>> PeerConnection::GetReceivers()
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<RtpReceiverInterface>> ret;
  if (ConfiguredForMedia()) {
    for (const auto& receiver : rtp_manager()->GetReceiversInternal()) {
      ret.push_back(receiver);
    }
  }
  return ret;
}

std::vector<scoped_refptr<RtpTransceiverInterface>>
PeerConnection::GetTransceivers() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_CHECK(IsUnifiedPlan())
      << "GetTransceivers is only supported with Unified Plan SdpSemantics.";
  std::vector<scoped_refptr<RtpTransceiverInterface>> all_transceivers;
  if (ConfiguredForMedia()) {
    for (const auto& transceiver : rtp_manager()->transceivers()->List()) {
      all_transceivers.push_back(transceiver);
    }
  }
  return all_transceivers;
}

bool PeerConnection::GetStats(StatsObserver* observer,
                              MediaStreamTrackInterface* track,
                              StatsOutputLevel level) {
  TRACE_EVENT0("webrtc", "PeerConnection::GetStats (legacy)");
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!observer) {
    RTC_LOG(LS_ERROR) << "Legacy GetStats - observer is NULL.";
    return false;
  }
  RTC_LOG_THREAD_BLOCK_COUNT();

  legacy_stats_->UpdateStats(level);

  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(4);

  // The LegacyStatsCollector is used to tell if a track is valid because it may
  // remember tracks that the PeerConnection previously removed.
  if (track && !legacy_stats_->IsValidTrack(track->id())) {
    RTC_LOG(LS_WARNING) << "Legacy GetStats is called with an invalid track: "
                        << track->id();
    return false;
  }
  message_handler_.PostGetStats(observer, legacy_stats_.get(), track);

  return true;
}

void PeerConnection::GetStats(RTCStatsCollectorCallback* callback) {
  TRACE_EVENT0("webrtc", "PeerConnection::GetStats");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(callback);
  RTC_LOG_THREAD_BLOCK_COUNT();
  stats_collector_.GetStatsReport(
      scoped_refptr<RTCStatsCollectorCallback>(callback));
  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(2);
}

void PeerConnection::GetStats(
    scoped_refptr<RtpSenderInterface> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback) {
  TRACE_EVENT0("webrtc", "PeerConnection::GetStats");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(callback);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  scoped_refptr<RtpSenderInternal> internal_sender;
  if (selector) {
    for (const auto& proxy_transceiver :
         rtp_manager()->transceivers()->List()) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
      for (const auto& proxy_sender :
           proxy_transceiver->internal()->senders()) {
        if (proxy_sender == selector) {
          internal_sender = proxy_sender->internal();
          break;
        }
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END()
      if (internal_sender)
        break;
    }
  }
  // If there is no `internal_sender` then `selector` is either null or does not
  // belong to the PeerConnection (in Plan B, senders can be removed from the
  // PeerConnection). This means that "all the stats objects representing the
  // selector" is an empty set. Invoking GetStatsReport() with a null selector
  // produces an empty stats report.
  stats_collector_.GetStatsReport(internal_sender, callback);
}

void PeerConnection::GetStats(
    scoped_refptr<RtpReceiverInterface> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback) {
  TRACE_EVENT0("webrtc", "PeerConnection::GetStats");
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(callback);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  scoped_refptr<RtpReceiverInternal> internal_receiver;
  if (selector) {
    for (const auto& proxy_transceiver :
         rtp_manager()->transceivers()->List()) {
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
      for (const auto& proxy_receiver :
           proxy_transceiver->internal()->receivers()) {
        if (proxy_receiver == selector) {
          internal_receiver = proxy_receiver->internal();
          break;
        }
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END()
      if (internal_receiver)
        break;
    }
  }
  // If there is no `internal_receiver` then `selector` is either null or does
  // not belong to the PeerConnection (in Plan B, receivers can be removed from
  // the PeerConnection). This means that "all the stats objects representing
  // the selector" is an empty set. Invoking GetStatsReport() with a null
  // selector produces an empty stats report.
  stats_collector_.GetStatsReport(internal_receiver, callback);
}

PeerConnectionInterface::SignalingState PeerConnection::signaling_state() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return sdp_handler_->signaling_state();
}

PeerConnectionInterface::IceConnectionState
PeerConnection::ice_connection_state() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return ice_connection_state_;
}

PeerConnectionInterface::IceConnectionState
PeerConnection::standardized_ice_connection_state() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return standardized_ice_connection_state_;
}

PeerConnectionInterface::PeerConnectionState
PeerConnection::peer_connection_state() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return connection_state_;
}

PeerConnectionInterface::IceGatheringState
PeerConnection::ice_gathering_state() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return ice_gathering_state_;
}

std::optional<bool> PeerConnection::can_trickle_ice_candidates() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  const SessionDescriptionInterface* description = current_remote_description();
  if (!description) {
    description = pending_remote_description();
  }
  if (!description) {
    return std::nullopt;
  }
  // TODO(bugs.webrtc.org/7443): Change to retrieve from session-level option.
  if (description->description()->transport_infos().empty()) {
    return std::nullopt;
  }
  return description->description()->transport_infos()[0].description.HasOption(
      "trickle");
}

RTCErrorOr<scoped_refptr<DataChannelInterface>>
PeerConnection::CreateDataChannelOrError(const std::string& label,
                                         const DataChannelInit* config) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "PeerConnection::CreateDataChannel");

  if (IsClosed()) {
    return RTC_LOG_ERROR(
        RTCError(RTCErrorType::INVALID_STATE)
        << "CreateDataChannelOrError: PeerConnection is closed.");
  }

  bool first_datachannel = !data_channel_controller_.HasUsedDataChannels();

  InternalDataChannelInit internal_config;
  if (config) {
    internal_config = InternalDataChannelInit(*config);
  }

  internal_config.fallback_ssl_role = sdp_handler_->GuessSslRole();
  RTCErrorOr<scoped_refptr<DataChannelInterface>> ret =
      data_channel_controller_.InternalCreateDataChannelWithProxy(
          label, internal_config);
  if (!ret.ok()) {
    return ret.MoveError();
  }

  ClearStatsCache();
  scoped_refptr<DataChannelInterface> channel = ret.MoveValue();
  if (tracer_) {
    std::optional<int> id;
    if (internal_config.id >= 0) {
      id = internal_config.id;
    }
    tracer_->OnCreateDataChannel(*channel, id);
  }

  // Check the onRenegotiationNeeded event (with plan-b backward compat)
  if (configuration_.sdp_semantics == SdpSemantics::kUnifiedPlan ||
      (configuration_.sdp_semantics == SdpSemantics::kPlanB_DEPRECATED &&
       first_datachannel)) {
    sdp_handler_->UpdateNegotiationNeeded();
  }
  NoteUsageEvent(UsageEvent::DATA_ADDED);
  return channel;
}

void PeerConnection::RestartIce() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Traced first because RestartIce() may synchronously fire
  // OnNegotiationNeeded().
  if (tracer_) {
    tracer_->OnRestartIce();
  }
  sdp_handler_->RestartIce();
}

void PeerConnection::CreateOffer(CreateSessionDescriptionObserver* observer,
                                 const RTCOfferAnswerOptions& options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  sdp_handler_->CreateOffer(observer, options);
}

void PeerConnection::CreateAnswer(CreateSessionDescriptionObserver* observer,
                                  const RTCOfferAnswerOptions& options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  sdp_handler_->CreateAnswer(observer, options);
}

void PeerConnection::SetLocalDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  auto desc_ptr = std::unique_ptr<SessionDescriptionInterface>(desc);
  if (!CheckValidSetDescription(observer, desc_ptr, signaling_thread()))
    return;

  RunOnSignalingThread([this, desc = std::move(desc_ptr),
                        observer = scoped_refptr<SetSessionDescriptionObserver>(
                            observer)]() mutable {
    RTC_DCHECK_RUN_ON(signaling_thread());
    sdp_handler_->SetLocalDescription(std::move(observer), std::move(desc));
  });
}

// This method bypasses the proxy, so can be called from any thread.
void PeerConnection::SetLocalDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    scoped_refptr<SetLocalDescriptionObserverInterface> observer) {
  if (!CheckValidSetDescription(observer, desc, signaling_thread()))
    return;

  RunOnSignalingThread(
      [this, desc = std::move(desc), observer = std::move(observer)]() mutable {
        RTC_DCHECK_RUN_ON(signaling_thread());
        sdp_handler_->SetLocalDescription(std::move(desc), observer);
      });
}

void PeerConnection::SetLocalDescription(
    SetSessionDescriptionObserver* observer) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  sdp_handler_->SetLocalDescription(
      scoped_refptr<SetSessionDescriptionObserver>(observer));
}

void PeerConnection::SetLocalDescription(
    scoped_refptr<SetLocalDescriptionObserverInterface> observer) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  sdp_handler_->SetLocalDescription(observer);
}

void PeerConnection::SetRemoteDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  auto desc_ptr = std::unique_ptr<SessionDescriptionInterface>(desc);
  if (!CheckValidSetDescription(observer, desc_ptr, signaling_thread()))
    return;

  RunOnSignalingThread([this, desc = std::move(desc_ptr),
                        observer = scoped_refptr<SetSessionDescriptionObserver>(
                            observer)]() mutable {
    RTC_DCHECK_RUN_ON(signaling_thread());
    sdp_handler_->SetRemoteDescription(std::move(observer), std::move(desc));
  });
}

void PeerConnection::SetRemoteDescription(
    std::unique_ptr<SessionDescriptionInterface> desc,
    scoped_refptr<SetRemoteDescriptionObserverInterface> observer) {
  if (!CheckValidSetDescription(observer, desc, signaling_thread()))
    return;

  RunOnSignalingThread(
      [this, desc = std::move(desc), observer = std::move(observer)]() mutable {
        RTC_DCHECK_RUN_ON(signaling_thread());
        sdp_handler_->SetRemoteDescription(std::move(desc), observer);
      });
}

PeerConnectionInterface::RTCConfiguration PeerConnection::GetConfiguration() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return configuration_;
}

RTCError PeerConnection::SetConfiguration(
    const RTCConfiguration& configuration) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "PeerConnection::SetConfiguration");
  if (IsClosed()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_STATE)
                         << "SetConfiguration: PeerConnection is closed.");
  }

  const bool has_local_description = local_description() != nullptr;

  RTCError validate_error = ValidateIceCandidatePoolSize(
      configuration.ice_candidate_pool_size,
      has_local_description
          ? std::optional<int>(configuration_.ice_candidate_pool_size)
          : std::nullopt);
  if (!validate_error.ok()) {
    return validate_error;
  }

  if (has_local_description &&
      configuration.crypto_options != configuration_.crypto_options) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_MODIFICATION)
                         << "Can't change crypto_options after calling "
                            "SetLocalDescription.");
  }

  // Create a new, configuration object whose peerconnection config
  // will have been validated.
  RTCErrorOr<RTCConfiguration> validated_config =
      ApplyConfiguration(configuration, configuration_);
  if (!validated_config.ok()) {
    return validated_config.error();
  }

  // Parse ICE servers before hopping to network thread.
  ServerAddresses stun_servers;
  std::vector<RelayServerConfig> turn_servers;
  validate_error = ParseAndValidateIceServersFromConfiguration(
      configuration, stun_servers, turn_servers);
  if (!validate_error.ok()) {
    return validate_error;
  }
  NoteServerUsage(usage_pattern_, stun_servers, turn_servers);

  const RTCConfiguration& modified_config = validated_config.value();
  const bool needs_ice_restart =
      modified_config.servers != configuration_.servers ||
      NeedIceRestart(
          configuration_.surface_ice_candidates_on_ice_transport_type_changed,
          configuration_.type, modified_config.type) ||
      modified_config.GetTurnPortPrunePolicy() !=
          configuration_.GetTurnPortPrunePolicy();
  IceConfig ice_config(modified_config);
  ice_config.dtls_handshake_in_stun = CanAttemptDtlsStunPiggybacking();

  // Apply part of the configuration on the network thread.  In theory this
  // shouldn't fail.
  std::vector<IceParameters> pooled_credentials;
  // TODO: webrtc:42222117 - For carrying `new_states` from the transport
  // controller on the network thread to the transport controller on the
  // signaling thread, we should instead use a
  // ScopedOperationsBatcher::BatchTaskWithFinalizer.
  JsepTransportController::TransportStateSnapshot new_states;
  if (!network_thread()->BlockingCall([&] {
        RTC_DCHECK_RUN_ON(network_thread());
        // As described in JSEP, calling setConfiguration with new ICE
        // servers or candidate policy must set a "needs-ice-restart" bit so
        // that the next offer triggers an ICE restart which will pick up
        // the changes.
        if (needs_ice_restart)
          transport_controller_->SetNeedsIceRestartFlag();

        transport_controller_->SetIceConfig(ice_config);
        bool result = ReconfigurePortAllocator_n(
            stun_servers, turn_servers, modified_config.type,
            modified_config.ice_candidate_pool_size,
            modified_config.GetTurnPortPrunePolicy(),
            modified_config.turn_customizer,
            modified_config.stun_candidate_keepalive_interval,
            has_local_description);
        pooled_credentials = port_allocator_->GetPooledIceCredentials();
        new_states = transport_controller_n()->GetTransportStates_n();
        return result;
      })) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to apply configuration to PortAllocator.");
  }

  transport_controller_s()->SetTransportStates(std::move(new_states));
  sdp_handler_->UpdateCachedIceCredentials(std::move(pooled_credentials));
  configuration_ = modified_config;

  if (needs_ice_restart) {
    sdp_handler_->UpdateNegotiationNeeded();
  }

  if (tracer_) {
    tracer_->OnSetConfiguration(configuration_);
  }
  return RTCError::OK();
}

bool PeerConnection::AddIceCandidate(const IceCandidate* ice_candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  ClearStatsCache();
  return sdp_handler_->AddIceCandidate(ice_candidate);
}

void PeerConnection::AddIceCandidate(std::unique_ptr<IceCandidate> candidate,
                                     std::function<void(RTCError)> callback) {
  AddIceCandidate(
      std::move(candidate),
      [callback = std::move(callback)](
          RTCError result,
          absl::AnyInvocable<void() &&> operation_complete_callback) mutable {
        callback(std::move(result));
        std::move(operation_complete_callback)();
      });
}

void PeerConnection::AddIceCandidate(
    std::unique_ptr<IceCandidate> candidate,
    absl::AnyInvocable<void(RTCError, absl::AnyInvocable<void() &&>) &&>
        callback) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Traced before chaining, so the order is the one the application called in.
  const bool trace_candidate = tracer_ != nullptr && candidate != nullptr;
  if (trace_candidate) {
    tracer_->OnAddIceCandidate(*candidate);
  }
  sdp_handler_->AddIceCandidate(
      std::move(candidate),
      [this, safety = signaling_thread_safety_.flag(),
       callback = std::move(callback), trace_candidate](
          RTCError result,
          absl::AnyInvocable<void() &&> operation_complete_callback) mutable {
        RTC_DCHECK_RUN_ON(signaling_thread());
        if (safety->alive()) {
          ClearStatsCache();
          if (trace_candidate) {
            if (result.ok()) {
              tracer_->OnAddIceCandidateSuccess();
            } else {
              tracer_->OnAddIceCandidateFailure(result);
            }
          }
        }
        std::move(callback)(std::move(result),
                            std::move(operation_complete_callback));
      });
}

bool PeerConnection::RemoveIceCandidate(const IceCandidate* candidate) {
  TRACE_EVENT0("webrtc", "PeerConnection::RemoveIceCandidate");
  RTC_DCHECK_RUN_ON(signaling_thread());
  return sdp_handler_->RemoveIceCandidate(candidate);
}

RTCError PeerConnection::SetBitrate(const BitrateSettings& bitrate) {
  if (!worker_thread()->IsCurrent()) {
    return worker_thread()->BlockingCall([&]() { return SetBitrate(bitrate); });
  }
  RTC_DCHECK_RUN_ON(worker_thread());

  if (!call_) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_STATE)
                         << "PeerConnection is closed.");
  }

  const bool has_min = bitrate.min_bitrate_bps.has_value();
  const bool has_start = bitrate.start_bitrate_bps.has_value();
  const bool has_max = bitrate.max_bitrate_bps.has_value();
  if (has_min && *bitrate.min_bitrate_bps < 0) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                         << "min_bitrate_bps <= 0");
  }
  if (has_start) {
    if (has_min && *bitrate.start_bitrate_bps < *bitrate.min_bitrate_bps) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "start_bitrate_bps < min_bitrate_bps");
    } else if (*bitrate.start_bitrate_bps < 0) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "start_bitrate_bps < 0");
    }
  }
  if (has_max) {
    if (has_start && *bitrate.max_bitrate_bps < *bitrate.start_bitrate_bps) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "max_bitrate_bps < start_bitrate_bps");
    } else if (has_min && *bitrate.max_bitrate_bps < *bitrate.min_bitrate_bps) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "max_bitrate_bps < min_bitrate_bps");
    } else if (*bitrate.max_bitrate_bps < 0) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_PARAMETER)
                           << "max_bitrate_bps < 0");
    }
  }

  call_->SetClientBitratePreferences(bitrate);

  return RTCError::OK();
}

void PeerConnection::ReconfigureBandwidthEstimation(
    const BandwidthEstimationSettings& settings) {
  worker_thread()->PostTask(SafeTask(worker_thread_safety_, [this, settings]() {
    RTC_DCHECK_RUN_ON(worker_thread());
    call_->GetTransportControllerSend()->ReconfigureBandwidthEstimation(
        settings);
  }));
}

void PeerConnection::SetAudioPlayout(bool playout) {
  RTC_DCHECK(ConfiguredForMedia());
  if (!worker_thread()->IsCurrent()) {
    worker_thread()->BlockingCall(
        [this, playout] { SetAudioPlayout(playout); });
    return;
  }
  RTC_DCHECK_RUN_ON(worker_thread());
  auto audio_state = media_engine()->voice().GetAudioState();
  audio_state->SetPlayout(playout);
}

void PeerConnection::SetAudioRecording(bool recording) {
  RTC_DCHECK(ConfiguredForMedia());
  if (!worker_thread()->IsCurrent()) {
    worker_thread()->BlockingCall(
        [this, recording] { SetAudioRecording(recording); });
    return;
  }
  RTC_DCHECK_RUN_ON(worker_thread());
  auto audio_state = media_engine()->voice().GetAudioState();
  audio_state->SetRecording(recording);
}

void PeerConnection::AddAdaptationResource(scoped_refptr<Resource> resource) {
  if (!worker_thread()->IsCurrent()) {
    return worker_thread()->BlockingCall(
        [this, resource]() { return AddAdaptationResource(resource); });
  }
  RTC_DCHECK_RUN_ON(worker_thread());
  if (!call_) {
    // The PeerConnection has been closed.
    return;
  }
  call_->AddAdaptationResource(resource);
}

bool PeerConnection::ConfiguredForMedia() const {
  return context_->is_configured_for_media();
}

bool PeerConnection::StartRtcEventLog(std::unique_ptr<RtcEventLogOutput> output,
                                      int64_t output_period_ms) {
  return worker_thread()->BlockingCall(
      [this, output = std::move(output), output_period_ms]() mutable {
        return StartRtcEventLog_w(std::move(output), output_period_ms);
      });
}

bool PeerConnection::StartRtcEventLog(
    std::unique_ptr<RtcEventLogOutput> output) {
  int64_t output_period_ms = 5000;
  return StartRtcEventLog(std::move(output), output_period_ms);
}

void PeerConnection::StopRtcEventLog() {
  worker_thread()->BlockingCall([this] { StopRtcEventLog_w(); });
}

void PeerConnection::SetDataChannelEventObserver(
    std::unique_ptr<DataChannelEventObserverInterface> observer) {
  network_thread()->PostTask(SafeTask(
      network_thread_safety_, [this, obs = std::move(observer)]() mutable {
        RTC_DCHECK_RUN_ON(network_thread());
        data_channel_controller_.SetEventObserver(std::move(obs));
      }));
}

scoped_refptr<DtlsTransportInterface> PeerConnection::LookupDtlsTransportByMid(
    const std::string& mid) {
  RTC_DCHECK_RUN_ON(network_thread());
  return transport_controller_->LookupDtlsTransportByMid_n(mid);
}

scoped_refptr<SctpTransportInterface> PeerConnection::GetSctpTransport() const {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!sctp_mid_n_)
    return nullptr;

  return transport_controller_->GetSctpTransport(*sctp_mid_n_);
}

const SessionDescriptionInterface* PeerConnection::local_description() const {
  return HandleSessionDescriptionAccessor<&SdpStateProvider::local_description>(
      local_description_clone_);
}

const SessionDescriptionInterface* PeerConnection::remote_description() const {
  return HandleSessionDescriptionAccessor<
      &SdpStateProvider::remote_description>(remote_description_clone_);
}

const SessionDescriptionInterface* PeerConnection::current_local_description()
    const {
  return HandleSessionDescriptionAccessor<
      &SdpStateProvider::current_local_description>(
      current_local_description_clone_);
}

const SessionDescriptionInterface* PeerConnection::current_remote_description()
    const {
  return HandleSessionDescriptionAccessor<
      &SdpStateProvider::current_remote_description>(
      current_remote_description_clone_);
}

const SessionDescriptionInterface* PeerConnection::pending_local_description()
    const {
  return HandleSessionDescriptionAccessor<
      &SdpStateProvider::pending_local_description>(
      pending_local_description_clone_);
}

const SessionDescriptionInterface* PeerConnection::pending_remote_description()
    const {
  return HandleSessionDescriptionAccessor<
      &SdpStateProvider::pending_remote_description>(
      pending_remote_description_clone_);
}

void PeerConnection::Close() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  TRACE_EVENT0("webrtc", "PeerConnection::Close");

  RTC_LOG_THREAD_BLOCK_COUNT();

  if (IsClosed()) {
    return;
  }
  if (tracer_) {
    tracer_->OnClose();
  }
  // Update stats here so that we have the most recent stats for tracks and
  // streams before the channels are closed.
  legacy_stats_->UpdateStats(kStatsOutputLevelStandard);

  ice_connection_state_ = PeerConnectionInterface::kIceConnectionClosed;
  RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnIceConnectionChange(ice_connection_state_);
  });
  standardized_ice_connection_state_ =
      PeerConnectionInterface::IceConnectionState::kIceConnectionClosed;
  connection_state_ = PeerConnectionInterface::PeerConnectionState::kClosed;
  RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnConnectionChange(connection_state_);
  });

  sdp_handler_->Close();

  NoteUsageEvent(UsageEvent::CLOSE_CALLED);

  ScopedOperationsBatcher worker_tasks(worker_thread());

  if (ConfiguredForMedia()) {
    for (RtpTransceiver* transceiver :
         rtp_manager()->transceivers()->ListInternal()) {
      if (!transceiver->stopped()) {
        worker_tasks.Add(transceiver->GetStopTransceiverProcedure());
      }
    }
  }

  // Don't destroy BaseChannels until after stats has been cleaned up so that
  // the last stats request can still read from the channels.
  // TODO(tommi): The voice/video channels will be partially uninitialized on
  // the network thread (see `RtpTransceiver::ClearChannel`), partially on the
  // worker thread (see `PushNewMediaChannelAndDeleteChannel`) and then
  // eventually freed on the signaling thread.
  // It would be good to combine those steps with the teardown steps here.
  {
    ScopedOperationsBatcher network_tasks(network_thread());
    sdp_handler_->GetMediaChannelTeardownTasks(network_tasks, worker_tasks);
    network_tasks.AddWithFinalizer(MakeCloseOnNetworkThreadTask());
  }

  // The event log is used in the transport controller, which must be outlived
  // by the former. CreateOffer by the peer connection is implemented
  // asynchronously and if the peer connection is closed without resetting the
  // WebRTC session description factory, the session description factory would
  // call the transport controller.
  sdp_handler_->ResetSessionDescFactory();
  if (ConfiguredForMedia()) {
    rtp_manager_->Close();
  }

  worker_tasks.Add([this]() {
    RTC_DCHECK_RUN_ON(worker_thread());
    worker_thread_safety_->SetNotAlive();
    call_.reset();
    StopRtcEventLog_w();
  });

  worker_tasks.Run();
  ReportUsagePattern();
  ReportCloseUsageMetrics();

  // Signal shutdown to the sdp handler. This invalidates weak pointers for
  // internal pending callbacks.
  sdp_handler_->PrepareForShutdown();
  data_channel_controller_.PrepareForShutdown();

  // The .h file says that observer can be discarded after close() returns.
  // Make sure this is true.
  observer_ = nullptr;
}

void PeerConnection::SetIceConnectionState(IceConnectionState new_state) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (ice_connection_state_ == new_state) {
    return;
  }

  // After transitioning to "closed", ignore any additional states from
  // TransportController (such as "disconnected").
  if (IsClosed()) {
    return;
  }

  RTC_LOG(LS_INFO) << "Changing IceConnectionState " << ice_connection_state_
                   << " => " << new_state;
  RTC_DCHECK(ice_connection_state_ !=
             PeerConnectionInterface::kIceConnectionClosed);

  ice_connection_state_ = new_state;
  RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnIceConnectionChange(ice_connection_state_);
  });
}

void PeerConnection::SetStandardizedIceConnectionState(
    PeerConnectionInterface::IceConnectionState new_state) {
  if (standardized_ice_connection_state_ == new_state) {
    return;
  }

  if (IsClosed()) {
    return;
  }

  RTC_LOG(LS_INFO) << "Changing standardized IceConnectionState "
                   << standardized_ice_connection_state_ << " => " << new_state;

  standardized_ice_connection_state_ = new_state;
  if (tracer_) {
    tracer_->OnIceConnectionStateChanged(new_state);
  }
  RunWithObserver([&](auto observer) {
    observer->OnStandardizedIceConnectionChange(new_state);
  });
}

void PeerConnection::SetConnectionState(
    PeerConnectionInterface::PeerConnectionState new_state) {
  if (connection_state_ == new_state)
    return;
  if (IsClosed())
    return;
  connection_state_ = new_state;
  if (tracer_) {
    tracer_->OnConnectionStateChanged(new_state);
  }
  RunWithObserver(
      [&](auto observer) { observer->OnConnectionChange(new_state); });

  // The first connection state change to connected happens once per
  // connection which makes it a good point to report metrics.
  if (new_state == PeerConnectionState::kConnected && !was_ever_connected_) {
    was_ever_connected_ = true;
    ReportFirstConnectUsageMetrics();
  }
}

void PeerConnection::ReportFirstConnectUsageMetrics() {
  // Record bundle-policy from configuration. Done here from
  // connectionStateChange to limit to actually established connections.
  BundlePolicyUsage policy = kBundlePolicyUsageMax;
  switch (configuration_.bundle_policy) {
    case kBundlePolicyBalanced:
      policy = kBundlePolicyUsageBalanced;
      break;
    case kBundlePolicyMaxBundle:
      policy = kBundlePolicyUsageMaxBundle;
      break;
    case kBundlePolicyMaxCompat:
      policy = kBundlePolicyUsageMaxCompat;
      break;
  }
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.BundlePolicy", policy,
                            kBundlePolicyUsageMax);

  // Record whether there was a local or remote provisional answer.
  ProvisionalAnswerUsage pranswer = kProvisionalAnswerNotUsed;
  if (local_description()->GetType() == SdpType::kPrAnswer) {
    pranswer = kProvisionalAnswerLocal;
  } else if (remote_description()->GetType() == SdpType::kPrAnswer) {
    pranswer = kProvisionalAnswerRemote;
  }
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.ProvisionalAnswer", pranswer,
                            kProvisionalAnswerMax);

  auto transport_infos = remote_description()->description()->transport_infos();
  if (!transport_infos.empty()) {
    // Record the number of valid / invalid ice-ufrag. We do allow certain
    // non-spec ice-char for backward-compat reasons. At this point we know
    // that the ufrag/pwd consists of a valid ice-char or one of the four
    // not allowed characters since we have passed the IsIceChar check done
    // by the p2p transport description on setRemoteDescription calls.
    // The ice-options are not validated when parsing so any character may
    // show up there, RFC 8839 section 5.6 defines them as 1*ice-char.
    const TransportDescription& description = transport_infos[0].description;
    auto is_invalid_char = [](char c) {
      return c == '-' || c == '=' || c == '#' || c == '_';
    };
    auto is_valid_ice_char = [](char c) {
      return absl::ascii_isalnum(c) || c == '+' || c == '/';
    };
    bool valid_ufrag = absl::c_none_of(description.ice_ufrag, is_invalid_char);
    bool valid_pwd = absl::c_none_of(description.ice_pwd, is_invalid_char);
    bool valid_ice_options = true;
    for (const std::string& option : description.transport_options) {
      valid_ice_options &=
          !option.empty() && absl::c_all_of(option, is_valid_ice_char);
    }
    RTC_HISTOGRAM_BOOLEAN("WebRTC.PeerConnection.ValidIceChars.Ufrag",
                          valid_ufrag);
    RTC_HISTOGRAM_BOOLEAN("WebRTC.PeerConnection.ValidIceChars.Pwd", valid_pwd);
    RTC_HISTOGRAM_BOOLEAN("WebRTC.PeerConnection.ValidIceChars.IceOptions",
                          valid_ice_options);

    // Record whether the hash algorithm of the first transport's
    // DTLS fingerprint is still using SHA-1.
    if (transport_infos[0].description.identity_fingerprint) {
      RTC_HISTOGRAM_BOOLEAN(
          "WebRTC.PeerConnection.DtlsFingerprintLegacySha1",
          absl::EqualsIgnoreCase(
              transport_infos[0].description.identity_fingerprint->algorithm,
              "sha-1"));
    }
  }

  // Record RtcpMuxPolicy setting.
  RtcpMuxPolicyUsage rtcp_mux_policy = kRtcpMuxPolicyUsageMax;
  switch (configuration_.rtcp_mux_policy) {
    case kRtcpMuxPolicyNegotiate:
      rtcp_mux_policy = kRtcpMuxPolicyUsageNegotiate;
      break;
    case kRtcpMuxPolicyRequire:
      rtcp_mux_policy = kRtcpMuxPolicyUsageRequire;
      break;
  }
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.RtcpMuxPolicy",
                            rtcp_mux_policy, kRtcpMuxPolicyUsageMax);
  switch (local_description()->GetType()) {
    case SdpType::kOffer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.Offer.ConnectionEstablished",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kAnswer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.Answer.ConnectionEstablished",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kPrAnswer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.PrAnswer.ConnectionEstablished",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kRollback:
      // Rollback does not have SDP so can not be munged.
      break;
  }

  // Below this point are features where we check what was negotiated in
  // SDP. `desc` will contain the answer, i.e. what was negotiated.
  const SessionDescription* desc = nullptr;
  if (local_description()->GetType() == SdpType::kAnswer ||
      local_description()->GetType() == SdpType::kPrAnswer) {
    desc = local_description()->description();
  } else if (remote_description()->GetType() == SdpType::kAnswer ||
             remote_description()->GetType() == SdpType::kPrAnswer) {
    desc = remote_description()->description();
  }
  if (!desc) {
    RTC_LOG(LS_INFO) << "Connection established without an answer, local="
                     << local_description()->GetType()
                     << ", remote=" << remote_description()->GetType();
    return;
  }

  bool negotiated_sctp_snap = false;
  const ContentInfo* sctp_content = GetFirstDataContent(desc);
  if (sctp_content && !sctp_content->rejected) {
    const SctpDataContentDescription* sctp_desc =
        sctp_content->media_description()->as_sctp();
    if (sctp_desc) {
      negotiated_sctp_snap |= sctp_desc->sctp_init().has_value();
    }
  }
  RTC_HISTOGRAM_BOOLEAN("WebRTC.PeerConnection.NegotiatedSctpSnap",
                        negotiated_sctp_snap);

  // Record congestion control mechanism in use, if any.
  // The information is taken from the last seen answer SDP.
  std::optional<RtcpFeedbackType> feedback_type;
  for (const auto& content : desc->contents()) {
    std::optional<RtcpFeedbackType> this_feedback_type =
        content.media_description()->preferred_rtcp_cc_ack_type();
    if (this_feedback_type) {
      feedback_type = this_feedback_type;
      break;
    }
  }
  if (!feedback_type) {
    feedback_type = RtcpFeedbackType::NONE;
  }
  // Note that NONE will be reported for datachannel-only calls.
  // Only TRANSPORT_CC and CCFB are currently reported.
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.NegotiatedFeedbackType",
                            static_cast<int>(*feedback_type),
                            static_cast<int>(RtcpFeedbackType::MAX));

  CryptexPolicyUsage cryptex = kCryptexPolicyUsageMax;
  switch (configuration_.crypto_options.srtp.cryptex_policy) {
    case CryptoOptions::Srtp::CryptexPolicy::kDisabled:
      cryptex = kCryptexPolicyUsageDisabled;
      break;
    case CryptoOptions::Srtp::CryptexPolicy::kNegotiate:
      cryptex = kCryptexPolicyUsageNegotiate;
      // Special-case for when cryptex was not required but negotiated.
      if (desc->cryptex()) {
        cryptex = kCryptexPolicyUsageNegotiated;
      }
      break;
    case CryptoOptions::Srtp::CryptexPolicy::kRequire:
      cryptex = kCryptexPolicyUsageRequire;
      break;
  }
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.CryptexUsage", cryptex,
                            kCryptexPolicyUsageMax);
}

void PeerConnection::ReportCloseUsageMetrics() {
  if (!was_ever_connected_) {
    return;
  }
  RTC_DCHECK(local_description());
  RTC_DCHECK(sdp_handler_);
  switch (local_description()->GetType()) {
    case SdpType::kOffer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.Offer.ConnectionClosed",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kAnswer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.Answer.ConnectionClosed",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kPrAnswer:
      RTC_HISTOGRAM_ENUMERATION(
          "WebRTC.PeerConnection.SdpMunging.PrAnswer.ConnectionClosed",
          sdp_handler_->sdp_munging_type(), SdpMungingType::kMaxValue);
      break;
    case SdpType::kRollback:
      // Rollback does not have SDP so can not be munged.
      break;
  }
}

void PeerConnection::OnIceGatheringChange(
    PeerConnectionInterface::IceGatheringState new_state) {
  if (IsClosed()) {
    return;
  }
  ice_gathering_state_ = new_state;
  if (tracer_) {
    tracer_->OnIceGatheringStateChanged(new_state);
  }
  RunWithObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    observer->OnIceGatheringChange(ice_gathering_state_);
  });
}

void PeerConnection::OnIceCandidate(std::unique_ptr<IceCandidate> candidate) {
  if (IsClosed()) {
    return;
  }
  ReportIceCandidateCollected(candidate->candidate());
  ClearStatsCache();
  if (tracer_) {
    tracer_->OnIceCandidate(*candidate);
  }
  RunWithObserver(
      [&](auto observer) { observer->OnIceCandidate(candidate.get()); });
}

void PeerConnection::OnIceCandidateError(const std::string& address,
                                         int port,
                                         const std::string& url,
                                         int error_code,
                                         const std::string& error_text) {
  if (IsClosed()) {
    return;
  }
  if (tracer_) {
    tracer_->OnIceCandidateError(address, port, url, error_code, error_text);
  }
  RunWithObserver([&](auto observer) {
    observer->OnIceCandidateError(address, port, url, error_code, error_text);
  });
}

void PeerConnection::OnIceCandidatesRemoved(
    absl::string_view mid,
    const std::vector<Candidate>& candidates) {
  if (IsClosed()) {
    return;
  }

  for (const Candidate& candidate : candidates) {
    IceCandidate c(mid, -1, candidate);
    RunWithObserver([&](auto o) { o->OnIceCandidateRemoved(&c); });
  }
}

void PeerConnection::OnSelectedCandidatePairChanged(
    const CandidatePairChangeEvent& event) {
  if (IsClosed()) {
    return;
  }

  if (event.selected_candidate_pair.local_candidate().is_local() &&
      event.selected_candidate_pair.remote_candidate().is_local()) {
    NoteUsageEvent(UsageEvent::DIRECT_CONNECTION_SELECTED);
  }

  RunWithObserver([&](auto observer) {
    observer->OnIceSelectedCandidatePairChanged(event);
  });
}

bool PeerConnection::CreateDataChannelTransport(absl::string_view mid) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!sctp_mid().has_value() || mid == sctp_mid().value());
  RTC_LOG(LS_INFO) << "Creating data channel, mid=" << mid;

  std::optional<std::string> transport_name =
      network_thread()->BlockingCall([&] {
        RTC_DCHECK_RUN_ON(network_thread());
        return SetupDataChannelTransport_n(mid);
      });
  if (!transport_name)
    return false;

  sctp_mid_s_ = std::string(mid);
  SetSctpTransportName(transport_name.value());

  return true;
}

void PeerConnection::DestroyDataChannelTransport(RTCError error) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  if (!sctp_mid_s_.has_value()) {
    RTC_LOG(LS_INFO) << "Data channel not configured.";
    RTC_DCHECK(sctp_transport_name_s_.empty());
    return;
  }

  network_thread()->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread());
    TeardownDataChannelTransport_n(error);
  });
  sctp_mid_s_.reset();
  SetSctpTransportName("");
}

void PeerConnection::OnSctpDataChannelStateChanged(
    int channel_id,
    DataChannelInterface::DataState state) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  stats_collector_.OnSctpDataChannelStateChanged(channel_id, state);
}

PeerConnection::InitializePortAllocatorResult
PeerConnection::InitializePortAllocator_n(
    const ServerAddresses& stun_servers,
    const std::vector<RelayServerConfig>& turn_servers,
    const RTCConfiguration& configuration) {
  RTC_DCHECK_RUN_ON(network_thread());

  port_allocator_->Initialize();
  // To handle both internal and externally created port allocator, we will
  // enable BUNDLE here.
  int port_allocator_flags = port_allocator_->flags();
  port_allocator_flags |= PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                          PORTALLOCATOR_ENABLE_IPV6 |
                          PORTALLOCATOR_ENABLE_IPV6_ON_WIFI;
  if (trials().IsDisabled("WebRTC-IPv6Default")) {
    port_allocator_flags &= ~(PORTALLOCATOR_ENABLE_IPV6);
  }
  if (configuration.disable_ipv6_on_wifi) {
    port_allocator_flags &= ~(PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
    RTC_LOG(LS_INFO) << "IPv6 candidates on Wi-Fi are disabled.";
  }

  if (configuration.tcp_candidate_policy == kTcpCandidatePolicyDisabled) {
    port_allocator_flags |= PORTALLOCATOR_DISABLE_TCP;
    RTC_LOG(LS_INFO) << "TCP candidates are disabled.";
  }

  if (configuration.candidate_network_policy ==
      kCandidateNetworkPolicyLowCost) {
    port_allocator_flags |= PORTALLOCATOR_DISABLE_COSTLY_NETWORKS;
    RTC_LOG(LS_INFO) << "Do not gather candidates on high-cost networks";
  }

  if (configuration.disable_link_local_networks) {
    port_allocator_flags |= PORTALLOCATOR_DISABLE_LINK_LOCAL_NETWORKS;
    RTC_LOG(LS_INFO) << "Disable candidates on link-local network interfaces.";
  }

  port_allocator_->set_flags(port_allocator_flags);
  // No step delay is used while allocating ports.
  port_allocator_->set_step_delay(kMinimumStepDelay);
  port_allocator_->SetCandidateFilter(
      ConvertIceTransportTypeToCandidateFilter(configuration.type));
  port_allocator_->set_max_ipv6_networks(configuration.max_ipv6_networks);

  auto turn_servers_copy = turn_servers;
  for (auto& turn_server : turn_servers_copy) {
    turn_server.tls_cert_verifier = tls_cert_verifier_.get();
  }
  // Call this last since it may create pooled allocator sessions using the
  // properties set above.
  port_allocator_->SetConfiguration(
      stun_servers, std::move(turn_servers_copy),
      configuration.ice_candidate_pool_size,
      configuration.GetTurnPortPrunePolicy(), configuration.turn_customizer,
      configuration.stun_candidate_keepalive_interval);

  InitializePortAllocatorResult res;
  res.enable_ipv6 = port_allocator_flags & PORTALLOCATOR_ENABLE_IPV6;
  return res;
}

bool PeerConnection::ReconfigurePortAllocator_n(
    const ServerAddresses& stun_servers,
    const std::vector<RelayServerConfig>& turn_servers,
    IceTransportsType type,
    int candidate_pool_size,
    PortPrunePolicy turn_port_prune_policy,
    TurnCustomizer* turn_customizer,
    std::optional<int> stun_candidate_keepalive_interval,
    bool have_local_description) {
  RTC_DCHECK_RUN_ON(network_thread());
  port_allocator_->SetCandidateFilter(
      ConvertIceTransportTypeToCandidateFilter(type));
  // Add the custom tls turn servers if they exist.
  auto turn_servers_copy = turn_servers;
  for (auto& turn_server : turn_servers_copy) {
    turn_server.tls_cert_verifier = tls_cert_verifier_.get();
  }
  // Call this last since it may create pooled allocator sessions using the
  // candidate filter set above.
  return port_allocator_->SetConfiguration(
      stun_servers, std::move(turn_servers_copy), candidate_pool_size,
      turn_port_prune_policy, turn_customizer,
      stun_candidate_keepalive_interval);
}

bool PeerConnection::StartRtcEventLog_w(
    std::unique_ptr<RtcEventLogOutput> output,
    int64_t output_period_ms) {
  RTC_DCHECK_RUN_ON(worker_thread());
  if (!worker_thread_safety_->alive()) {
    return false;
  }
  return env_.event_log().StartLogging(std::move(output), output_period_ms);
}

void PeerConnection::StopRtcEventLog_w() {
  RTC_DCHECK_RUN_ON(worker_thread());
  env_.event_log().StopLogging();
}

bool PeerConnection::GetSslRole(const std::string& content_name,
                                SSLRole* role) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (!local_description() || !remote_description()) {
    RTC_LOG(LS_INFO)
        << "Local and Remote descriptions must be applied to get the "
           "SSL Role of the session.";
    return false;
  }

  auto dtls_role = transport_controller_s()->GetDtlsRole(content_name);
  if (dtls_role) {
    *role = *dtls_role;
    return true;
  }
  return false;
}


std::vector<DataChannelStats> PeerConnection::GetDataChannelStats() const {
  RTC_DCHECK_RUN_ON(network_thread());
  return data_channel_controller_.GetDataChannelStats();
}

std::optional<std::string> PeerConnection::sctp_transport_name() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (sctp_mid_s_ && transport_controller_copy_)
    return sctp_transport_name_s_;
  return std::optional<std::string>();
}

void PeerConnection::SetSctpTransportName(std::string sctp_transport_name) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  sctp_transport_name_s_ = std::move(sctp_transport_name);
  ClearStatsCache();
}

// RTC_RUN_ON(worker_thread())
MediaEngineInterface* PeerConnection::media_engine() const {
  RTC_DCHECK(media_engine_ref_);
  return media_engine_ref_->media_engine();
}

std::optional<std::string> PeerConnection::sctp_mid() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return sctp_mid_s_;
}

CandidateStatsList PeerConnection::GetPooledCandidateStats() const {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!network_thread_safety_->alive())
    return {};
  CandidateStatsList candidate_stats_list;
  port_allocator_->GetCandidateStatsFromPooledSessions(&candidate_stats_list);
  return candidate_stats_list;
}

std::map<std::string, TransportStats> PeerConnection::GetTransportStatsByNames(
    const std::set<std::string>& transport_names) {
  TRACE_EVENT0("webrtc", "PeerConnection::GetTransportStatsByNames");
  RTC_DCHECK_RUN_ON(network_thread());
  if (!network_thread_safety_->alive())
    return {};

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;
  std::map<std::string, TransportStats> transport_stats_by_name;
  for (const std::string& transport_name : transport_names) {
    TransportStats transport_stats;
    bool success =
        transport_controller_->GetStats(transport_name, &transport_stats);
    if (success) {
      transport_stats_by_name[transport_name] = std::move(transport_stats);
    } else {
      RTC_LOG(LS_ERROR) << "Failed to get transport stats for transport_name="
                        << transport_name;
    }
  }
  return transport_stats_by_name;
}

bool PeerConnection::GetLocalCertificate(
    const std::string& transport_name,
    scoped_refptr<RTCCertificate>* certificate) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!network_thread_safety_->alive() || !certificate) {
    return false;
  }
  *certificate = transport_controller_->GetLocalCertificate(transport_name);
  return *certificate != nullptr;
}

std::unique_ptr<SSLCertChain> PeerConnection::GetRemoteSSLCertChain(
    const std::string& transport_name) {
  RTC_DCHECK_RUN_ON(network_thread());
  return transport_controller_->GetRemoteSSLCertChain(transport_name);
}

bool PeerConnection::IceRestartPending(const std::string& content_name) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return sdp_handler_->IceRestartPending(content_name);
}

bool PeerConnection::NeedsIceRestart(const std::string& content_name) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return transport_controller_s()->NeedsIceRestart(content_name);
}

void PeerConnection::OnTransportControllerConnectionState(
    webrtc::IceConnectionState state) {
  switch (state) {
    case webrtc::kIceConnectionConnecting:
      // If the current state is Connected or Completed, then there were
      // writable channels but now there are not, so the next state must
      // be Disconnected.
      // kIceConnectionConnecting is currently used as the default,
      // un-connected state by the TransportController, so its only use is
      // detecting disconnections.
      if (ice_connection_state_ ==
              PeerConnectionInterface::kIceConnectionConnected ||
          ice_connection_state_ ==
              PeerConnectionInterface::kIceConnectionCompleted) {
        SetIceConnectionState(
            PeerConnectionInterface::kIceConnectionDisconnected);
      }
      break;
    case webrtc::kIceConnectionFailed:
      SetIceConnectionState(PeerConnectionInterface::kIceConnectionFailed);
      break;
    case webrtc::kIceConnectionConnected:
      RTC_LOG(LS_INFO) << "Changing to ICE connected state because "
                          "all transports are writable.";
      {
        std::vector<std::pair<std::string, MediaType>> transceiver_info;
        if (ConfiguredForMedia()) {
          for (const auto& t : rtp_manager()->transceivers()->List()) {
            if (t->internal()->HasChannel()) {
              std::optional<std::string> mid = t->mid();
              if (mid) {
                transceiver_info.emplace_back(*mid, t->media_type());
              }
            }
          }
        }

        network_thread()->PostTask(SafeTask(
            network_thread_safety_,
            [this, transceiver_info = std::move(transceiver_info)]() mutable {
              RTC_DCHECK_RUN_ON(network_thread());
              ReportTransportStats(std::move(transceiver_info));
            }));
      }

      SetIceConnectionState(PeerConnectionInterface::kIceConnectionConnected);
      NoteUsageEvent(UsageEvent::ICE_STATE_CONNECTED);
      break;
    case webrtc::kIceConnectionCompleted:
      RTC_LOG(LS_INFO) << "Changing to ICE completed state because "
                          "all transports are complete.";
      if (ice_connection_state_ !=
          PeerConnectionInterface::kIceConnectionConnected) {
        // If jumping directly from "checking" to "connected",
        // signal "connected" first.
        SetIceConnectionState(PeerConnectionInterface::kIceConnectionConnected);
      }
      SetIceConnectionState(PeerConnectionInterface::kIceConnectionCompleted);

      NoteUsageEvent(UsageEvent::ICE_STATE_CONNECTED);
      break;
    default:
      RTC_DCHECK_NOTREACHED();
  }
}

void PeerConnection::OnTransportControllerCandidatesGathered(
    absl::string_view transport_name,
    const Candidates& candidates) {
  // TODO(bugs.webrtc.org/12427): Expect this to come in on the network thread
  // (not signaling as it currently does), handle appropriately.
  int sdp_mline_index;
  if (!GetLocalCandidateMediaIndex(transport_name, &sdp_mline_index)) {
    RTC_LOG(LS_ERROR)
        << "OnTransportControllerCandidatesGathered: content name "
        << transport_name << " not found";
    return;
  }

  for (Candidates::const_iterator citer = candidates.begin();
       citer != candidates.end(); ++citer) {
    // Use transport_name as the candidate media id.
    std::unique_ptr<IceCandidate> candidate(
        new IceCandidate(transport_name, sdp_mline_index, *citer));
    sdp_handler_->AddLocalIceCandidate(candidate.get());
    OnIceCandidate(std::move(candidate));
  }
}

void PeerConnection::OnTransportControllerCandidateError(
    const IceCandidateErrorEvent& event) {
  OnIceCandidateError(event.address, event.port, event.url, event.error_code,
                      event.error_text);
}

void PeerConnection::OnTransportControllerCandidatesRemoved(
    absl::string_view mid,
    const std::vector<Candidate>& candidates) {
  RTC_DCHECK(!mid.empty());
  sdp_handler_->RemoveLocalIceCandidates(mid, candidates);
  OnIceCandidatesRemoved(mid, candidates);
}

void PeerConnection::OnTransportControllerCandidateChanged(
    const CandidatePairChangeEvent& event) {
  OnSelectedCandidatePairChanged(event);
}

// Returns the media index for a local ice candidate given the content name.
bool PeerConnection::GetLocalCandidateMediaIndex(absl::string_view content_name,
                                                 int* sdp_mline_index) {
  if (!local_description() || !sdp_mline_index) {
    return false;
  }

  bool content_found = false;
  const ContentInfos& contents = local_description()->description()->contents();
  for (size_t index = 0; index < contents.size(); ++index) {
    if (contents[index].mid() == content_name) {
      *sdp_mline_index = static_cast<int>(index);
      content_found = true;
      break;
    }
  }
  return content_found;
}

Call::Stats PeerConnection::GetCallStats() {
  if (!worker_thread()->IsCurrent()) {
    return worker_thread()->BlockingCall([this] { return GetCallStats(); });
  }
  RTC_DCHECK_RUN_ON(worker_thread());
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;
  if (call_) {
    return call_->GetStats();
  } else {
    return Call::Stats();
  }
}

std::optional<AudioDeviceModule::Stats> PeerConnection::GetAudioDeviceStats() {
  RTC_DCHECK_RUN_ON(worker_thread());
  if (context_->is_configured_for_media()) {
    return media_engine()->voice().GetAudioDeviceStats();
  }
  return std::nullopt;
}

std::optional<std::string> PeerConnection::SetupDataChannelTransport_n(
    absl::string_view mid) {
  sctp_mid_n_ = std::string(mid);
  DataChannelTransportInterface* transport =
      transport_controller_->GetDataChannelTransport(*sctp_mid_n_);
  if (!transport) {
#ifndef WEBRTC_HAVE_SCTP
    RTC_LOG(LS_ERROR) << "Data channel transport is not available"
                      << " as WebRTC has been compiled without SCTP support "
                         "(WEBRTC_HAVE_SCTP), mid="
                      << mid;
#else
    RTC_LOG(LS_ERROR)
        << "Data channel transport is not available for data channels, mid="
        << mid;
#endif
    sctp_mid_n_ = std::nullopt;
    return std::nullopt;
  }

  std::optional<std::string> transport_name;
  DtlsTransportInternal* dtls_transport =
      transport_controller_->GetDtlsTransport(*sctp_mid_n_);
  if (dtls_transport) {
    transport_name = dtls_transport->transport_name();
  } else {
    // Make sure we still set a valid string.
    transport_name = std::string("");
  }

  data_channel_controller_.SetupDataChannelTransport_n(transport);

  return transport_name;
}

void PeerConnection::TeardownDataChannelTransport_n(RTCError error) {
  if (sctp_mid_n_) {
    // `sctp_mid_` may still be active through an SCTP transport.  If not, unset
    // it.
    RTC_LOG(LS_INFO) << "Tearing down data channel transport for mid="
                     << *sctp_mid_n_;
    sctp_mid_n_.reset();
  }

  data_channel_controller_.TeardownDataChannelTransport_n(error);
}

// Returns false if bundle is enabled and rtcp_mux is disabled.
bool PeerConnection::ValidateBundleSettings(
    const SessionDescription* desc,
    const flat_map<std::string, const ContentGroup*>& bundle_groups_by_mid) {
  if (bundle_groups_by_mid.empty())
    return true;

  const ContentInfos& contents = desc->contents();
  for (ContentInfos::const_iterator citer = contents.begin();
       citer != contents.end(); ++citer) {
    const ContentInfo* content = (&*citer);
    RTC_DCHECK(content != nullptr);
    auto it = bundle_groups_by_mid.find(content->mid());
    if (it != bundle_groups_by_mid.end() &&
        !(content->rejected || content->bundle_only) &&
        content->type == MediaProtocolType::kRtp) {
      if (!HasRtcpMuxEnabled(content))
        return false;
    }
  }
  // RTCP-MUX is enabled in all the contents.
  return true;
}

void PeerConnection::ReportSdpBundleUsage(
    const SessionDescriptionInterface& remote_description) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  bool using_bundle =
      remote_description.description()->HasGroup(GROUP_TYPE_BUNDLE);
  int num_audio_mlines = 0;
  int num_video_mlines = 0;
  int num_data_mlines = 0;
  for (const ContentInfo& content :
       remote_description.description()->contents()) {
    MediaType media_type = content.media_description()->type();
    if (media_type == MediaType::AUDIO) {
      num_audio_mlines += 1;
    } else if (media_type == MediaType::VIDEO) {
      num_video_mlines += 1;
    } else if (media_type == MediaType::DATA) {
      num_data_mlines += 1;
    }
  }
  bool simple = num_audio_mlines <= 1 && num_video_mlines <= 1;
  BundleUsage usage = kBundleUsageMax;
  if (num_audio_mlines == 0 && num_video_mlines == 0) {
    if (num_data_mlines > 0) {
      usage = using_bundle ? kBundleUsageBundleDatachannelOnly
                           : kBundleUsageNoBundleDatachannelOnly;
    } else {
      usage = kBundleUsageEmpty;
    }
  } else if (configuration_.sdp_semantics == SdpSemantics::kPlanB_DEPRECATED) {
    // In plan-b, simple/complex usage will not show up in the number of
    // m-lines or BUNDLE.
    usage = using_bundle ? kBundleUsageBundlePlanB : kBundleUsageNoBundlePlanB;
  } else {
    if (simple) {
      usage =
          using_bundle ? kBundleUsageBundleSimple : kBundleUsageNoBundleSimple;
    } else {
      usage = using_bundle ? kBundleUsageBundleComplex
                           : kBundleUsageNoBundleComplex;
    }
  }
  RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.BundleUsage", usage,
                            kBundleUsageMax);
}

void PeerConnection::ReportIceCandidateCollected(const Candidate& candidate) {
  NoteUsageEvent(UsageEvent::CANDIDATE_COLLECTED);
  if (candidate.address().IsPrivateIP()) {
    NoteUsageEvent(UsageEvent::PRIVATE_CANDIDATE_COLLECTED);
  }
  if (candidate.address().IsUnresolvedIP()) {
    NoteUsageEvent(UsageEvent::MDNS_CANDIDATE_COLLECTED);
  }
  if (candidate.address().family() == AF_INET6) {
    NoteUsageEvent(UsageEvent::IPV6_CANDIDATE_COLLECTED);
  }
}

void PeerConnection::NoteUsageEvent(UsageEvent event) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  usage_pattern_.NoteUsageEvent(event);
}

// Asynchronously adds remote candidates on the network thread.
void PeerConnection::AddRemoteCandidate(absl::string_view mid,
                                        const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  if (candidate.network_type() != ADAPTER_TYPE_UNKNOWN) {
    RTC_DLOG(LS_WARNING) << "Using candidate with adapter type set - this "
                            "should only happen in test";
  }

  // Clear fields that do not make sense as remote candidates.
  Candidate new_candidate(candidate);
  new_candidate.set_network_type(ADAPTER_TYPE_UNKNOWN);
  new_candidate.set_relay_protocol("");
  new_candidate.set_underlying_type_for_vpn(ADAPTER_TYPE_UNKNOWN);
  new_candidate.set_network_slice(NetworkSlice::NO_SLICE);

  network_thread()->PostTask(SafeTask(
      network_thread_safety_, [this, mid = std::string(mid),
                               candidate = std::move(new_candidate)]() mutable {
        RTC_DCHECK_RUN_ON(network_thread());
        std::vector<Candidate> candidates = {candidate};
        RTCError error =
            transport_controller_->AddRemoteCandidates(mid, candidates);
        if (error.ok()) {
          signaling_thread()->PostTask(SafeTask(
              signaling_thread_safety_.flag(),
              [this, candidate = std::move(candidate)]() mutable {
                ReportRemoteIceCandidateAdded(candidate);
                // Candidates successfully submitted for checking.
                if (ice_connection_state() ==
                        PeerConnectionInterface::kIceConnectionNew ||
                    ice_connection_state() ==
                        PeerConnectionInterface::kIceConnectionDisconnected) {
                  // If state is New, then the session has just gotten its first
                  // remote ICE candidates, so go to Checking. If state is
                  // Disconnected, the session is re-using old candidates or
                  // receiving additional ones, so go to Checking. If state is
                  // Connected, stay Connected.
                  // TODO(bemasc): If state is Connected, and the new candidates
                  // are for a newly added transport, then the state actually
                  // _should_ move to checking.  Add a way to distinguish that
                  // case.
                  SetIceConnectionState(
                      PeerConnectionInterface::kIceConnectionChecking);
                }
                // TODO(bemasc): If state is Completed, go back to Connected.
              }));
        } else {
          RTC_LOG(LS_WARNING) << error.message();
        }
      }));
}

void PeerConnection::ReportUsagePattern() const {
  RunWithMaybeNullObserver([&](auto observer) {
    RTC_DCHECK_RUN_ON(signaling_thread());
    usage_pattern_.ReportUsagePattern(observer);
  });
}

void PeerConnection::ReportRemoteIceCandidateAdded(const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  NoteUsageEvent(UsageEvent::REMOTE_CANDIDATE_ADDED);

  if (candidate.address().IsPrivateIP()) {
    NoteUsageEvent(UsageEvent::REMOTE_PRIVATE_CANDIDATE_ADDED);
  }
  if (candidate.address().IsUnresolvedIP()) {
    NoteUsageEvent(UsageEvent::REMOTE_MDNS_CANDIDATE_ADDED);
  }
  if (candidate.address().family() == AF_INET6) {
    NoteUsageEvent(UsageEvent::REMOTE_IPV6_CANDIDATE_ADDED);
  }
}

bool PeerConnection::SrtpRequired() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return dtls_enabled_;
}

void PeerConnection::OnTransportControllerGatheringState(
    webrtc::IceGatheringState state) {
  RTC_DCHECK(signaling_thread()->IsCurrent());
  if (state == webrtc::kIceGatheringGathering) {
    OnIceGatheringChange(PeerConnectionInterface::kIceGatheringGathering);
  } else if (state == webrtc::kIceGatheringComplete) {
    OnIceGatheringChange(PeerConnectionInterface::kIceGatheringComplete);
  } else if (state == webrtc::kIceGatheringNew) {
    OnIceGatheringChange(PeerConnectionInterface::kIceGatheringNew);
  } else {
    RTC_LOG(LS_ERROR) << "Unknown state received: " << state;
    RTC_DCHECK_NOTREACHED();
  }
}

// Runs on network_thread().
void PeerConnection::ReportTransportStats(
    std::vector<std::pair<std::string, MediaType>> transceiver_info) {
  RTC_DCHECK_RUN_ON(network_thread());
  TRACE_EVENT0("webrtc", "PeerConnection::ReportTransportStats");
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;
  flat_map<absl::string_view, std::set<MediaType>>
      media_types_by_transport_name;
  for (const auto& item : transceiver_info) {
    const auto& mid = item.first;
    const auto& media_type = item.second;
    RtpTransportInternal* transport =
        transport_controller_->GetRtpTransport(mid);
    if (transport) {
      media_types_by_transport_name[transport->transport_name()].insert(
          media_type);
    }
  }

  if (sctp_mid_n_) {
    DtlsTransportInternal* dtls_transport =
        transport_controller_->GetDtlsTransport(*sctp_mid_n_);
    if (dtls_transport) {
      media_types_by_transport_name[dtls_transport->transport_name()].insert(
          MediaType::DATA);
    }
  }

  for (const auto& entry : media_types_by_transport_name) {
    TransportStats stats;
    if (transport_controller_->GetStats(entry.first, &stats)) {
      ReportBestConnectionState(stats);
      ReportNegotiatedCiphers(dtls_enabled_, stats, entry.second);
    }
  }
}

// Walk through the ConnectionInfos to gather best connection usage
// for IPv4 and IPv6.
// static (no member state required)
void PeerConnection::ReportBestConnectionState(const TransportStats& stats) {
  for (const TransportChannelStats& channel_stats : stats.channel_stats) {
    for (const ConnectionInfo& connection_info :
         channel_stats.ice_transport_stats.connection_infos) {
      if (!connection_info.best_connection) {
        continue;
      }

      const Candidate& local = connection_info.local_candidate;
      const Candidate& remote = connection_info.remote_candidate;

      // Increment the counter for IceCandidatePairType.
      if (local.protocol() == TCP_PROTOCOL_NAME ||
          (local.is_relay() && local.relay_protocol() == TCP_PROTOCOL_NAME)) {
        RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.CandidatePairType_TCP",
                                  GetIceCandidatePairType(local, remote),
                                  kIceCandidatePairMax);
      } else if (local.protocol() == UDP_PROTOCOL_NAME) {
        RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.CandidatePairType_UDP",
                                  GetIceCandidatePairType(local, remote),
                                  kIceCandidatePairMax);
      } else {
        RTC_LOG(LS_WARNING) << "ReportBestConnectionState: No histogram for "
                            << local.protocol();
      }

      // Increment the counter for IP type.
      if (local.address().family() == AF_INET) {
        RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.IPMetrics",
                                  kBestConnections_IPv4,
                                  kPeerConnectionAddressFamilyCounter_Max);
      } else if (local.address().family() == AF_INET6) {
        RTC_HISTOGRAM_ENUMERATION("WebRTC.PeerConnection.IPMetrics",
                                  kBestConnections_IPv6,
                                  kPeerConnectionAddressFamilyCounter_Max);
      } else {
        RTC_CHECK(!local.address().hostname().empty() &&
                  local.address().IsUnresolvedIP());
      }

      return;
    }
  }
}

// static
void PeerConnection::ReportNegotiatedCiphers(
    bool dtls_enabled,
    const TransportStats& stats,
    const std::set<MediaType>& media_types) {
  if (!dtls_enabled || stats.channel_stats.empty()) {
    return;
  }

  int srtp_crypto_suite = stats.channel_stats[0].srtp_crypto_suite;
  int ssl_cipher_suite = stats.channel_stats[0].ssl_cipher_suite;
  if (srtp_crypto_suite == kSrtpInvalidCryptoSuite &&
      ssl_cipher_suite == kTlsNullWithNullNull) {
    return;
  }

  if (ssl_cipher_suite != kTlsNullWithNullNull) {
    for (MediaType media_type : media_types) {
      switch (media_type) {
        case MediaType::AUDIO:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslCipherSuite.Audio", ssl_cipher_suite,
              kSslCipherSuiteMaxValue);
          break;
        case MediaType::VIDEO:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslCipherSuite.Video", ssl_cipher_suite,
              kSslCipherSuiteMaxValue);
          break;
        case MediaType::DATA:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslCipherSuite.Data", ssl_cipher_suite,
              kSslCipherSuiteMaxValue);
          break;
        default:
          RTC_DCHECK_NOTREACHED();
          continue;
      }
    }
  }

  uint16_t ssl_peer_signature_algorithm =
      stats.channel_stats[0].ssl_peer_signature_algorithm;
  if (ssl_peer_signature_algorithm != kSslSignatureAlgorithmUnknown) {
    for (MediaType media_type : media_types) {
      switch (media_type) {
        case MediaType::AUDIO:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslPeerSignatureAlgorithm.Audio",
              ssl_peer_signature_algorithm, kSslSignatureAlgorithmMaxValue);
          break;
        case MediaType::VIDEO:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslPeerSignatureAlgorithm.Video",
              ssl_peer_signature_algorithm, kSslSignatureAlgorithmMaxValue);
          break;
        case MediaType::DATA:
          RTC_HISTOGRAM_ENUMERATION_SPARSE(
              "WebRTC.PeerConnection.SslPeerSignatureAlgorithm.Data",
              ssl_peer_signature_algorithm, kSslSignatureAlgorithmMaxValue);
          break;
        default:
          RTC_DCHECK_NOTREACHED();
          continue;
      }
    }
  }
}

bool PeerConnection::OnTransportChanged(
    absl::string_view mid,
    RtpTransportInternal* rtp_transport,
    scoped_refptr<DtlsTransport> dtls_transport,
    DataChannelTransportInterface* data_channel_transport) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK(!mid.empty());
  bool ret = true;
  if (ConfiguredForMedia()) {
    for (const auto& transceiver :
         rtp_manager()->transceivers()->UnsafeList()) {
      auto internal = transceiver->internal();
      // Match on the MID that the channel was created for rather than on the
      // transceiver's current MID. The channel is bound to the transport of
      // that MID and must be notified when that transport changes.
      if (internal->channel_mid() == mid) {
        ret = internal->SetRtpTransport(rtp_transport);
      }
    }
  }

  if (mid == sctp_mid_n_) {
    data_channel_controller_.OnTransportChanged(data_channel_transport);
    if (dtls_transport) {
      signaling_thread()->PostTask(
          SafeTask(signaling_thread_safety_.flag(),
                   [this, name = std::string(rtp_transport->transport_name())] {
                     RTC_DCHECK_RUN_ON(signaling_thread());
                     SetSctpTransportName(std::move(name));
                   }));
    }
  }

  return ret;
}

void PeerConnection::RunWithObserver(
    absl::AnyInvocable<void(PeerConnectionObserver*) &&> task) {  // NOLINT
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(observer_);
  std::move(task)(observer_);
}

void PeerConnection::RunWithMaybeNullObserver(
    absl::AnyInvocable<void(PeerConnectionObserver*) &&> task)  // NOLINT
    const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::move(task)(observer_);
}

RTCError PeerConnection::StartSctpTransport(const SctpOptions& options) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(sctp_mid_s_);

  return network_thread()->BlockingCall([this, mid = *sctp_mid_s_, options] {
    scoped_refptr<SctpTransport> sctp_transport =
        transport_controller_n()->GetSctpTransport(mid);
    RTC_DCHECK(sctp_transport);
    if (!sctp_transport || !sctp_transport->Start(options)) {
      return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_MODIFICATION)
                           << "Failed to start SCTP transport.");
    }
    return RTCError::OK();
  });
}

CryptoOptions PeerConnection::GetCryptoOptions() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return configuration_.crypto_options;
}

void PeerConnection::ClearStatsCache() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  if (legacy_stats_) {
    legacy_stats_->InvalidateCache();
  }
  stats_collector_.ClearCachedStatsReport();
}

bool PeerConnection::ShouldFireNegotiationNeededEvent(uint32_t event_id) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  bool should_fire = sdp_handler_->ShouldFireNegotiationNeededEvent(event_id);
  if (should_fire && tracer_) {
    tracer_->OnNegotiationNeeded();
  }
  return should_fire;
}

void PeerConnection::RequestUsagePatternReportForTesting() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  message_handler_.RequestUsagePatternReport(
      [this]() {
        RTC_DCHECK_RUN_ON(signaling_thread());
        ReportUsagePattern();
      },
      /* delay_ms= */ 0);
}

int PeerConnection::FeedbackAccordingToRfc8888CountForTesting() const {
  return worker_thread()->BlockingCall([this]() {
    RTC_DCHECK_RUN_ON(worker_thread());
    return call_->FeedbackAccordingToRfc8888Count().value_or(0);
  });
}

int PeerConnection::FeedbackAccordingToTransportCcCountForTesting() const {
  return worker_thread()->BlockingCall([this]() {
    RTC_DCHECK_RUN_ON(worker_thread());
    return call_->FeedbackAccordingToTransportCcCount().value_or(0);
  });
}

absl::AnyInvocable<void(const CopyOnWriteBuffer& packet, int64_t packet_time_us)
                       const>
PeerConnection::InitializeRtcpCallback() {
  return [this](const CopyOnWriteBuffer& packet, int64_t /*packet_time_us*/) {
    RTC_DCHECK_RUN_ON(network_thread());
    call_ptr_->Receiver()->DeliverRtcpPacket(packet);
  };
}

absl::AnyInvocable<void(const RtpPacketReceived& parsed_packet) const>
PeerConnection::InitializeUnDemuxablePacketHandler() {
  return [this](const RtpPacketReceived& parsed_packet) {
    RTC_DCHECK_RUN_ON(network_thread());
    // Deliver the packet anyway to Call to allow Call to do BWE.
    // Even if there is no media receiver, the packet has still
    // been received on the network and has been correctly parsed.
    call_ptr_->Receiver()->DeliverRtpPacket(
        MediaType::ANY, parsed_packet,
        /*undemuxable_packet_handler=*/
        [](const RtpPacketReceived& packet) { return false; });
  };
}

bool PeerConnection::CanAttemptDtlsStunPiggybacking() const {
  return dtls_enabled_ && SSLStreamAdapter::IsBoringSsl() &&
         env_.field_trials().IsEnabled("WebRTC-IceHandshakeDtls");
}

void PeerConnection::RunOnSignalingThread(absl::AnyInvocable<void() &&> task) {
  if (signaling_thread()->IsCurrent()) {
    std::move(task)();
  } else {
    signaling_thread()->PostTask(
        SafeTask(signaling_thread_safety_.flag(), std::move(task)));
  }
}

}  // namespace webrtc
