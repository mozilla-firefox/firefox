/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtp_transceiver.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video_codecs/scalability_mode.h"
#include "call/call.h"
#include "media/base/codec.h"
#include "media/base/codec_comparators.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "pc/audio_rtp_receiver.h"
#include "pc/channel.h"
#include "pc/channel_interface.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/dtls_transport.h"
#include "pc/legacy_stats_collector_interface.h"
#include "pc/rtp_media_utils.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "pc/video_rtp_receiver.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_route.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"

namespace webrtc {
namespace {

bool HasAnyMediaCodec(const std::vector<RtpCodecCapability>& codecs) {
  return absl::c_any_of(codecs, [](const RtpCodecCapability& codec) {
    return codec.IsMediaCodec();
  });
}

RTCError VerifyCodecPreferences(const std::vector<RtpCodecCapability>& codecs,
                                const std::vector<Codec>& send_codecs,
                                const std::vector<Codec>& recv_codecs) {
  // `codec_capabilities` is the union of `send_codecs` and `recv_codecs`.
  std::vector<Codec> codec_capabilities;
  codec_capabilities.reserve(send_codecs.size() + recv_codecs.size());
  codec_capabilities.insert(codec_capabilities.end(), send_codecs.begin(),
                            send_codecs.end());
  codec_capabilities.insert(codec_capabilities.end(), recv_codecs.begin(),
                            recv_codecs.end());
  // If a media codec is not recognized from `codec_capabilities`, throw
  // InvalidModificationError.
  if (!absl::c_all_of(codecs, [&codec_capabilities](
                                  const RtpCodecCapability& codec) {
        return !codec.IsMediaCodec() ||
               absl::c_any_of(codec_capabilities,
                              [&codec](const Codec& codec_capability) {
                                return IsSameRtpCodec(codec_capability, codec);
                              });
      })) {
    return RTC_LOG_ERROR(
        RTCError::InvalidModification()
        << "Invalid codec preferences: Missing codec from codec "
           "capabilities.");
  }
  // If `codecs` only contains entries for RTX, RED, FEC or Comfort Noise, throw
  // InvalidModificationError.
  if (!HasAnyMediaCodec(codecs)) {
    return RTC_LOG_ERROR(
        RTCError::InvalidModification()
        << "Invalid codec preferences: codec list must have a non "
           "RTX, RED, FEC or Comfort Noise entry.");
  }
  return RTCError::OK();
}

// Set default header extensions depending on whether simulcast/SVC is used.
void ConfigureExtraVideoHeaderExtensions(
    const std::vector<RtpEncodingParameters>& encodings,
    std::vector<RtpHeaderExtensionCapability>& extensions) {
  bool uses_simulcast = encodings.size() > 1;
  bool uses_svc = !encodings.empty() &&
                  encodings[0].scalability_mode.has_value() &&
                  encodings[0].scalability_mode !=
                      ScalabilityModeToString(ScalabilityMode::kL1T1);
  if (!uses_simulcast && !uses_svc)
    return;

  // Enable DD and VLA extensions, can be deactivated by the API. Skip this if
  // the GFD extension was enabled via field trial for backward compatibility
  // reasons.
  bool uses_frame_descriptor =
      absl::c_any_of(extensions, [](const RtpHeaderExtensionCapability& ext) {
        return ext.uri == RtpExtension::kGenericFrameDescriptorUri00 &&
               ext.direction != RtpTransceiverDirection::kStopped;
      });
  if (!uses_frame_descriptor) {
    for (RtpHeaderExtensionCapability& ext : extensions) {
      if (ext.uri == RtpExtension::kVideoLayersAllocationUri ||
          ext.uri == RtpExtension::kDependencyDescriptorUri) {
        ext.direction = RtpTransceiverDirection::kSendRecv;
      }
    }
  }
}

scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> CreateSender(
    MediaType media_type,
    const Environment& env,
    ConnectionContext* context,
    LegacyStatsCollectorInterface* legacy_stats,
    RtpSenderBase::SetStreamsObserver* set_streams_observer,
    absl::string_view sender_id,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
    MediaSendChannelInterface* media_send_channel,
    const std::vector<RtpEncodingParameters>& init_send_encodings,
    bool simulcast_rejected,
    const std::vector<SimulcastLayer>& initial_simulcast_layers,
    std::vector<std::string> stream_ids,
    std::vector<Codec> send_codecs) {
  if (media_type == MediaType::AUDIO) {
    return RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
        context->signaling_thread(),
        AudioRtpSender::Create(
            env, context->signaling_thread(), context->worker_thread(),
            sender_id, legacy_stats, set_streams_observer,
            std::move(enable_sframe_at_owner),
            static_cast<VoiceMediaSendChannelInterface*>(media_send_channel),
            std::move(stream_ids), init_send_encodings,
            std::move(send_codecs)));
  }
  RTC_DCHECK_EQ(media_type, MediaType::VIDEO);
  return RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
      context->signaling_thread(),
      VideoRtpSender::Create(
          env, context->signaling_thread(), context->worker_thread(), sender_id,
          set_streams_observer, std::move(enable_sframe_at_owner),
          static_cast<VideoMediaSendChannelInterface*>(media_send_channel),
          init_send_encodings, simulcast_rejected, initial_simulcast_layers,
          std::move(stream_ids), std::move(send_codecs)));
}

template <typename RtpReceiverT, typename ReceiveInterface>
scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>
CreateReceiverOfType(const Environment& env,
                     Thread* signaling_thread,
                     Thread* worker_thread,
                     absl::string_view receiver_id,
                     MediaReceiveChannelInterface* receive_channel,
                     absl::AnyInvocable<RTCError()> enable_sframe_at_owner) {
  return RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
      signaling_thread, worker_thread,
      make_ref_counted<RtpReceiverT>(
          worker_thread, receiver_id, std::vector<std::string>(),
          std::move(enable_sframe_at_owner),
          static_cast<ReceiveInterface*>(receive_channel), &env.clock()));
}

scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>> CreateReceiver(
    const Environment& env,
    MediaType media_type,
    Thread* signaling_thread,
    Thread* worker_thread,
    absl::string_view receiver_id,
    MediaReceiveChannelInterface* receive_channel,
    absl::AnyInvocable<RTCError()> enable_sframe_at_owner) {
  if (media_type == MediaType::AUDIO) {
    return CreateReceiverOfType<AudioRtpReceiver,
                                VoiceMediaReceiveChannelInterface>(
        env, signaling_thread, worker_thread, receiver_id, receive_channel,
        std::move(enable_sframe_at_owner));
  }
  RTC_DCHECK_EQ(media_type, MediaType::VIDEO);
  return CreateReceiverOfType<VideoRtpReceiver,
                              VideoMediaReceiveChannelInterface>(
      env, signaling_thread, worker_thread, receiver_id, receive_channel,
      std::move(enable_sframe_at_owner));
}

std::pair<std::unique_ptr<MediaSendChannelInterface>,
          std::unique_ptr<MediaReceiveChannelInterface>>
CreateMediaContentChannels(
    MediaType media_type,
    const Environment& env,
    VoiceChannelFactoryInterface* voice_factory,
    VideoChannelFactoryInterface* video_factory,
    Call* call,
    const MediaConfig& media_config,
    const AudioOptions& audio_options,
    const VideoOptions& video_options,
    const CryptoOptions& crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
        video_encoder_switch_request_callback,
    absl::AnyInvocable<void()> parameters_changed_callback,
    absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
    absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                           const> on_frame_delivered_callback) {
  if (media_type == MediaType::AUDIO) {
    RTC_DCHECK(voice_factory);
    return {voice_factory->CreateSendChannel(
                env, call, media_config, audio_options, crypto_options,
                std::move(parameters_changed_callback)),
            voice_factory->CreateReceiveChannel(
                env, call, media_config, audio_options, crypto_options,
                std::move(on_first_packet),
                std::move(on_frame_delivered_callback))};
  }
  RTC_DCHECK(video_factory);
  return {
      video_factory->CreateSendChannel(
          env, call, media_config, video_options, crypto_options,
          video_bitrate_allocator_factory,
          std::move(video_encoder_switch_request_callback),
          std::move(parameters_changed_callback)),
      video_factory->CreateReceiveChannel(
          env, call, media_config, crypto_options, std::move(on_first_packet),
          std::move(on_frame_delivered_callback))};
}

std::vector<absl::AnyInvocable<void() &&>> DetachAndGetStopTasksForSenders(
    std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>&
        senders) {
  std::vector<absl::AnyInvocable<void() &&>> tasks;
  for (const auto& sender : senders) {
    auto task = sender->internal()->DetachTrackAndGetStopTask();
    if (task)
      tasks.push_back(std::move(task));
  }
  return tasks;
}

}  // namespace

std::vector<Codec> RtpTransceiver::GetSendCodecs() {
  RTC_DCHECK_RUN_ON(thread_);
  return media_type_ == MediaType::VIDEO
             ? codec_vendor().video_send_codecs().codecs()
             : codec_vendor().audio_send_codecs().codecs();
}

RtpTransceiver::RtpTransceiver(const Environment& env,
                               MediaType media_type,
                               ConnectionContext* context,
                               CodecLookupHelper* codec_lookup_helper,
                               LegacyStatsCollectorInterface* legacy_stats,
                               const AudioOptions& audio_options,
                               const VideoOptions& video_options)
    : env_(env),
      thread_(context->signaling_thread()),
      unified_plan_(false),
      media_type_(media_type),
      signaling_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/false,
          context->signaling_thread())),
      network_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/true,
          context->network_thread())),
      context_(context),
      codec_lookup_helper_(codec_lookup_helper),
      legacy_stats_(legacy_stats),
      audio_options_(audio_options),
      video_options_(video_options) {
  RTC_DCHECK(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO);
  RTC_DCHECK(context_);
  RTC_DCHECK(context_->is_configured_for_media());
  RTC_DCHECK(codec_lookup_helper_);
}

RtpTransceiver::RtpTransceiver(
    const Environment& env,
    scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> sender,
    scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>> receiver,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper,
    std::vector<RtpHeaderExtensionCapability> header_extensions_to_negotiate,
    absl::AnyInvocable<void()> on_negotiation_needed,
    const AudioOptions& audio_options,
    const VideoOptions& video_options)
    : env_(env),
      thread_(context->signaling_thread()),
      unified_plan_(true),
      media_type_(sender->media_type()),
      signaling_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/false,
          context->signaling_thread())),
      network_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/true,
          context->network_thread())),
      context_(context),
      codec_lookup_helper_(codec_lookup_helper),
      legacy_stats_(nullptr),
      header_extensions_to_negotiate_(
          std::move(header_extensions_to_negotiate)),
      on_negotiation_needed_(std::move(on_negotiation_needed)),
      audio_options_(audio_options),
      video_options_(video_options) {
  RTC_DCHECK(context_);
  RTC_DCHECK(context_->is_configured_for_media());
  RTC_DCHECK(media_type_ == MediaType::AUDIO ||
             media_type_ == MediaType::VIDEO);
  RTC_DCHECK(codec_lookup_helper_);
  RTC_DCHECK_EQ(sender->media_type(), receiver->media_type());
  RTC_DCHECK_EQ(media_type_, sender->media_type());
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  auto* sender_internal = sender->internal();
  senders_.push_back(std::move(sender));
  receivers_.push_back(std::move(receiver));
  if (media_type_ == MediaType::VIDEO) {
    ConfigureExtraVideoHeaderExtensions(
        sender_internal
            ->GetParametersInternal(/*may_use_cache*/ true,
                                    /*with_all_layers=*/false)
            .encodings,
        header_extensions_to_negotiate_);
  }
  sender_internal->SetSendCodecs(GetSendCodecs());
}

RtpTransceiver::RtpTransceiver(
    const Environment& env,
    Call* call,
    const MediaConfig& media_config,
    absl::string_view sender_id,
    absl::string_view receiver_id,
    MediaType media_type,
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>& init_send_encodings,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper,
    LegacyStatsCollectorInterface* legacy_stats,
    RtpSenderBase::SetStreamsObserver* set_streams_observer,
    const AudioOptions& audio_options,
    const VideoOptions& video_options,
    const CryptoOptions& crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    std::vector<RtpHeaderExtensionCapability> header_extensions_to_negotiate,
    bool simulcast_rejected,
    const std::vector<SimulcastLayer>& initial_simulcast_layers,
    absl::AnyInvocable<void()> on_negotiation_needed)
    : env_(env),
      thread_(context->signaling_thread()),
      unified_plan_(true),
      media_type_(media_type),
      signaling_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/false,
          context->signaling_thread())),
      network_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/true,
          context->network_thread())),
      context_(context),
      codec_lookup_helper_(codec_lookup_helper),
      legacy_stats_(legacy_stats),
      set_streams_observer_(set_streams_observer),
      header_extensions_to_negotiate_(
          std::move(header_extensions_to_negotiate)),
      on_negotiation_needed_(std::move(on_negotiation_needed)),
      audio_options_(audio_options),
      video_options_(video_options) {
  RTC_DCHECK(context_);
  RTC_DCHECK(context_->is_configured_for_media());
  RTC_DCHECK(media_type_ == MediaType::AUDIO ||
             media_type_ == MediaType::VIDEO);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  if (media_type_ == MediaType::VIDEO) {
    ConfigureExtraVideoHeaderExtensions(init_send_encodings,
                                        header_extensions_to_negotiate_);
  }

  auto encoder_switch_callback = GetEncoderSwitchRequestCallback();
  std::vector<Codec> send_codecs = GetSendCodecs();

  auto channels = CreateMediaContentChannels(
      media_type_, env_, voice_channel_factory(), video_channel_factory(), call,
      media_config, audio_options, video_options, crypto_options,
      video_bitrate_allocator_factory, std::move(encoder_switch_callback),
      GetParametersChangedCallback(),
      [thread = thread_, safety = signaling_thread_safety_,
       this](uint32_t ssrc) {
        thread->PostTask(SafeTask(safety, [this, ssrc]() {
          RTC_DCHECK_RUN_ON(thread_);
          OnFirstPacketReceived_s(ssrc);
        }));
      },
      GetOnFrameDeliveredCallback());

  auto sender = CreateSender(
      media_type_, env_, context_, legacy_stats_, set_streams_observer_,
      sender_id, absl::bind_front(&RtpTransceiver::TryToEnableSframe, this),
      channels.first.get(), init_send_encodings, simulcast_rejected,
      initial_simulcast_layers, stream_ids, std::move(send_codecs));

  owned_send_channel_ = std::move(channels.first);
  owned_receive_channel_ = std::move(channels.second);
  senders_.push_back(std::move(sender));

  bool set_track_succeeded = senders_.back()->SetTrack(track.get());
  RTC_DCHECK(set_track_succeeded);

  receivers_.push_back(CreateReceiver(
      env_, media_type_, context_->signaling_thread(),
      context_->worker_thread(),
      receiver_id.empty() ? CreateRandomUuid() : receiver_id,
      owned_receive_channel_.get(),
      absl::bind_front(&RtpTransceiver::TryToEnableSframe, this)));
}

RtpTransceiver::~RtpTransceiver() {
  // TODO(tommi): On Android, when running PeerConnectionClientTest (e.g.
  // PeerConnectionClientTest#testCameraSwitch), the instance doesn't get
  // deleted on `thread_`. See if we can fix that.
  if (!stopped_) {
    RTC_DCHECK_RUN_ON(thread_);
    StopInternal();
  }

  RTC_CHECK(!channel_) << "Missing call to ClearChannel?";
  RTC_DCHECK(!owned_send_channel_);
  RTC_DCHECK(!owned_receive_channel_);
}

void RtpTransceiver::CreateChannel(
    absl::string_view mid,
    Call* call_ptr,
    const MediaConfig& media_config,
    bool srtp_required,
    CryptoOptions crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup,
    ScopedOperationsBatcher& worker_tasks,
    ScopedOperationsBatcher& network_tasks) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!channel_);
  RTC_DCHECK(!mid_ || mid_.value() == mid);
  RTC_DCHECK(!stopped_);

  mid_ = mid;

  if (!signaling_thread_safety_) {
    // This code path is hit during rollback.
    signaling_thread_safety_ = PendingTaskSafetyFlag::Create();
  } else {
    // Newly constructed.
    RTC_DCHECK(!signaling_thread_safety_->alive());
    signaling_thread_safety_->SetAlive();
  }

  ChannelCallbacks callbacks;
  callbacks.on_first_packet_sent =
      [thread = thread_, flag = signaling_thread_safety_, this]() mutable {
        thread->PostTask(
            SafeTask(std::move(flag), [this]() { OnFirstPacketSent(); }));
      };
  callbacks.on_packet_received =
      [this, flag = signaling_thread_safety_](const RtpPacketReceived& packet) {
        RTC_DCHECK_RUN_ON(context()->network_thread());
        OnPacketReceived(packet.Ssrc(), flag);
      };

  auto encoder_switch_callback = GetEncoderSwitchRequestCallback();
  auto parameters_changed_callback = GetParametersChangedCallback();

  std::unique_ptr<MediaSendChannelInterface> media_send_channel;
  std::unique_ptr<MediaReceiveChannelInterface> media_receive_channel;
  bool needs_set_media_channels = false;

  if (owned_send_channel_) {
    RTC_DCHECK(owned_receive_channel_);
    media_send_channel = std::move(owned_send_channel_);
    media_receive_channel = std::move(owned_receive_channel_);
  } else {
    // TODO(bugs.webrtc.org/11992): CreateVideoChannel internally switches to
    // the worker thread. We shouldn't be using the `call_ptr_` hack here but
    // simply be on the worker thread and use `call_` (update upstream code).
    auto channels = CreateMediaContentChannels(
        media_type(), env_, voice_channel_factory(), video_channel_factory(),
        call_ptr, media_config, audio_options_, video_options_, crypto_options,
        video_bitrate_allocator_factory, std::move(encoder_switch_callback),
        std::move(parameters_changed_callback),
        [thread = thread_, safety = signaling_thread_safety_,
         this](uint32_t ssrc) {
          thread->PostTask(SafeTask(safety, [this, ssrc]() {
            RTC_DCHECK_RUN_ON(thread_);
            OnFirstPacketReceived_s(ssrc);
          }));
        },
        GetOnFrameDeliveredCallback());
    media_send_channel = std::move(channels.first);
    media_receive_channel = std::move(channels.second);
    needs_set_media_channels = true;
  }

  auto construct_channel =
      [this, srtp_required, crypto_options, callbacks = std::move(callbacks)](
          std::unique_ptr<MediaSendChannelInterface> send,
          std::unique_ptr<MediaReceiveChannelInterface> receive) mutable {
        RTC_DCHECK_RUN_ON(thread_);
        RTC_DCHECK(mid_.has_value());
        channel_ = std::make_unique<BaseChannel>(
            context()->worker_thread(), context()->network_thread(),
            context()->signaling_thread(), std::move(send), std::move(receive),
            *mid_, media_type(), srtp_required, crypto_options,
            context()->ssrc_generator(), std::move(callbacks));
        transport_name_ = std::nullopt;
      };

  if (needs_set_media_channels) {
    worker_tasks.AddWithFinalizer(
        [this, media_send_channel = std::move(media_send_channel),
         media_receive_channel = std::move(media_receive_channel),
         construct_channel = std::move(construct_channel)]() mutable
            -> ScopedOperationsBatcher::BatchTaskResult {
          RTC_DCHECK_RUN_ON(context()->worker_thread());
          SetMediaChannels(media_send_channel.get(),
                           media_receive_channel.get());
          return ScopedOperationsBatcher::FinalizerTask(
              [send = std::move(media_send_channel),
               recv = std::move(media_receive_channel),
               construct_channel = std::move(construct_channel)]() mutable {
                construct_channel(std::move(send), std::move(recv));
              });
        });
  } else {
    construct_channel(std::move(media_send_channel),
                      std::move(media_receive_channel));
  }

  network_tasks.AddWithFinalizer(
      [this, transport_lookup = std::move(transport_lookup)]() mutable
          -> ScopedOperationsBatcher::BatchTaskResult {
        RTC_DCHECK_RUN_ON(context()->network_thread());
        auto result = InitializeOnNetworkThread(std::move(transport_lookup));
        if (!result.ok()) {
          return result.MoveError();
        }
        std::optional<std::string> transport_name = std::move(result.value());
        return ScopedOperationsBatcher::FinalizerTask(
            [this, transport_name = std::move(transport_name)]() mutable {
              RTC_DCHECK_RUN_ON(thread_);
              transport_name_ = std::move(transport_name);
            });
      });
}

RTCError RtpTransceiver::SetChannelForTest(
    std::unique_ptr<ChannelInterface> channel,
    absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(channel);
  RTC_DCHECK(transport_lookup);
  RTC_DCHECK(!channel_);
  // Cannot set a channel on a stopped transceiver.
  if (stopped_) {
    return RTCError::InvalidState();
  }

  RTC_LOG_THREAD_BLOCK_COUNT();

  RTC_DCHECK_EQ(media_type(), channel->media_type());
  RTC_DCHECK(mid_ || channel->mid().empty());
  channel_ = std::move(channel);
  transport_name_ = std::nullopt;

  // An alternative to this, could be to require SetChannel to be called
  // on the network thread. The channel object operates for the most part
  // on the network thread, as part of its initialization being on the network
  // thread is required, so setting a channel object as part of the construction
  // (without thread hopping) might be the more efficient thing to do than
  // how SetChannel works today.
  // Similarly, if the channel() accessor is limited to the network thread, that
  // helps with keeping the channel implementation requirements being met and
  // avoids synchronization for accessing the pointer or network related state.
  std::optional<std::string> transport_name;
  RTCError err = context()->network_thread()->BlockingCall(
      [&, flag = signaling_thread_safety_, channel = channel_.get()]() {
        RTC_DCHECK_RUN_ON(context()->network_thread());
        auto result = InitializeOnNetworkThread(std::move(transport_lookup));
        if (result.ok()) {
          transport_name = std::move(result.value());
        }
        return result.MoveError();
      });

  if (err.ok()) {
    transport_name_ = std::move(transport_name);
  }

  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(2);

  return err;
}

absl::AnyInvocable<void() &&> RtpTransceiver::GetClearChannelNetworkTask() {
  RTC_DCHECK_RUN_ON(thread_);
  // GetClearChannelNetworkTask must be called before GetDeleteChannelWorkerTask
  // since that's where we clear the `channel_` pointer. Perhaps we should
  // combine these into one function to avoid an ordering mistake?

  if (!channel_) {
    RTC_DCHECK(!signaling_thread_safety_ || !signaling_thread_safety_->alive());
    return nullptr;
  }

  signaling_thread_safety_->SetNotAlive();
  signaling_thread_safety_ = nullptr;

  ChannelInterface* channel = channel_.get();
  return [this, channel, flag = network_thread_safety_] {
    RTC_DCHECK_RUN_ON(context()->network_thread());
    flag->SetNotAlive();
    channel->SetRtpTransport(nullptr);
    ClearRtpTransportState();
  };
}

absl::AnyInvocable<void() &&> RtpTransceiver::GetDeleteChannelWorkerTask(
    bool stop_senders) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!signaling_thread_safety_ || !signaling_thread_safety_->alive())
      << "GetClearChannelNetworkTask() must be called first";

  if (!channel_) {
    return nullptr;
  }

  std::vector<absl::AnyInvocable<void() &&>> stop_sender_actions;
  if (stop_senders) {
    stop_sender_actions = DetachAndGetStopTasksForSenders(senders_);
  }

  transport_name_ = std::nullopt;

  // Ensure that channel_ is not reachable via the transceiver, but is deleted
  // only after clearing the references in senders_ and receivers_.
  return [this, channel = std::move(channel_), senders = senders_,
          receivers = receivers_,
          stop_sender_actions = std::move(stop_sender_actions)]() mutable {
    RTC_DCHECK_RUN_ON(context()->worker_thread());
    for (auto& task : stop_sender_actions) {
      std::move(task)();
    }
    ClearMediaChannelReferences();
    channel.reset();
  };
}

void RtpTransceiver::ClearChannel() {
  RTC_DCHECK_RUN_ON(thread_);
  if (!channel_) {
    return;
  }

  absl::AnyInvocable<void() &&> network_task = GetClearChannelNetworkTask();
  if (network_task) {
    context()->network_thread()->BlockingCall(
        [&] { std::move(network_task)(); });
  }

  absl::AnyInvocable<void() &&> worker_task =
      GetDeleteChannelWorkerTask(/*stop_senders=*/false);
  if (worker_task) {
    context()->worker_thread()->BlockingCall([&] { std::move(worker_task)(); });
  }
}

// RTC_RUN_ON(context()->worker_thread());
void RtpTransceiver::SetMediaChannels(MediaSendChannelInterface* send,
                                      MediaReceiveChannelInterface* receive) {
  for (const auto& sender : senders_) {
    sender->internal()->SetMediaChannel(send);
  }
  for (const auto& receiver : receivers_) {
    receiver->internal()->SetMediaChannel(receive);
  }
}
VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
RtpTransceiver::GetEncoderSwitchRequestCallback() {
  if (media_type() != MediaType::VIDEO) {
    return nullptr;
  }
  RTC_DCHECK(signaling_thread_safety_);
  // Return a task that first clears the sender parameter cache on the signaling
  // thread and then posts a task to apply the codec switch changes to the
  // parameters on the worker.
  return
      [this, signaling_thread = context_->signaling_thread(),
       worker_thread = context_->worker_thread(),
       signaling_safety = signaling_thread_safety_](
          VideoMediaSendChannelInterface::EncoderSwitchRequestAction action) {
        // Called on the encoder task queue.
        signaling_thread->PostTask(SafeTask(
            signaling_safety,
            [this, worker_thread, action = std::move(action)]() mutable {
              for (const auto& sender : senders_) {
                sender->internal()->SetCachedParameters(std::nullopt);
              }
              worker_thread->PostTask([action = std::move(action)]() mutable {
                std::move(action)();
              });
            }));
      };
}

absl::AnyInvocable<void()> RtpTransceiver::GetParametersChangedCallback() {
  RTC_DCHECK(signaling_thread_safety_);
  return [this, signaling_safety = signaling_thread_safety_]() {
    thread_->PostTask(SafeTask(signaling_safety, [this]() {
      RTC_DCHECK_RUN_ON(thread_);
      for (const auto& sender : senders_) {
        sender->internal()->OnParametersChanged();
      }
    }));
  };
}

// RTC_RUN_ON(context()->worker_thread());
void RtpTransceiver::ClearMediaChannelReferences() {
  SetMediaChannels(nullptr, nullptr);
  owned_send_channel_ = nullptr;
  owned_receive_channel_ = nullptr;
}

PLAN_B_ONLY void RtpTransceiver::AddSenderPlanB(
    scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> sender) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!stopped_);
  RTC_DCHECK(!unified_plan_);
  RTC_DCHECK(sender);
  RTC_DCHECK_EQ(media_type(), sender->media_type());
  RTC_DCHECK(!absl::c_linear_search(senders_, sender));
  sender->internal()->SetSendCodecs(GetSendCodecs());
  senders_.push_back(sender);
}

PLAN_B_ONLY scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>
RtpTransceiver::AddSenderPlanB(
    scoped_refptr<MediaStreamTrackInterface> track,
    absl::string_view sender_id,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>& send_encodings) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!stopped_);
  RTC_DCHECK(!unified_plan_);
  RTC_DCHECK(media_type_ == MediaType::AUDIO ||
             media_type_ == MediaType::VIDEO);
  std::vector<Codec> send_codecs = GetSendCodecs();
  context_->worker_thread()->BlockingCall([&]() mutable {
    RTC_DCHECK_RUN_ON(context()->worker_thread());
    senders_.push_back(CreateSender(
        media_type_, env_, context_, legacy_stats_, set_streams_observer_,
        sender_id, /*enable_sframe_at_owner=*/nullptr,
        channel_ ? channel_->media_send_channel() : nullptr, send_encodings,
        false, {}, stream_ids, std::move(send_codecs)));
  });
  bool set_track_succeeded = senders_.back()->SetTrack(track.get());
  RTC_DCHECK(set_track_succeeded);
  return senders_.back();
}

PLAN_B_ONLY bool RtpTransceiver::RemoveSenderPlanB(RtpSenderInterface* sender) {
  RTC_DCHECK(!unified_plan_);
  RTC_DCHECK_EQ(media_type(), sender->media_type());
  auto it = absl::c_find(senders_, sender);
  if (it == senders_.end()) {
    return false;
  }
  (*it)->internal()->Stop();
  senders_.erase(it);
  return true;
}

PLAN_B_ONLY void RtpTransceiver::AddReceiverPlanB(
    scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>> receiver) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!stopped_);
  RTC_DCHECK(!unified_plan_);
  RTC_DCHECK(receiver);
  RTC_DCHECK_EQ(media_type(), receiver->media_type());
  RTC_DCHECK(!absl::c_linear_search(receivers_, receiver));
  receivers_.push_back(receiver);
}

PLAN_B_ONLY bool RtpTransceiver::RemoveReceiverPlanB(
    RtpReceiverInterface* receiver) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!unified_plan_);
  RTC_DCHECK_EQ(media_type(), receiver->media_type());
  auto it = absl::c_find(receivers_, receiver);
  if (it == receivers_.end()) {
    return false;
  }

  (*it)->internal()->Stop();
  context()->worker_thread()->BlockingCall([&]() {
    (*it)->internal()->SetMediaChannel(nullptr);
  });

  receivers_.erase(it);
  return true;
}

scoped_refptr<RtpSenderInternal> RtpTransceiver::sender_internal() const {
  RTC_DCHECK(unified_plan_);
  RTC_CHECK_EQ(1u, senders_.size());
  return scoped_refptr<RtpSenderInternal>(senders_[0]->internal());
}

scoped_refptr<RtpReceiverInternal> RtpTransceiver::receiver_internal() const {
  RTC_DCHECK(unified_plan_);
  RTC_CHECK_EQ(1u, receivers_.size());
  return scoped_refptr<RtpReceiverInternal>(receivers_[0]->internal());
}

MediaType RtpTransceiver::media_type() const {
  return media_type_;
}

std::optional<std::string> RtpTransceiver::mid() const {
  return mid_;
}

void RtpTransceiver::OnFirstPacketReceived_s(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(thread_);
  for (const auto& receiver : receivers_) {
    if (receiver->internal()->ssrc_s() == ssrc) {
      receiver->internal()->NotifyFirstPacketReceived(ssrc);
      return;
    }
  }
  for (const auto& receiver : receivers_) {
    if (!receiver->internal()->ssrc_s().has_value()) {
      receiver->internal()->SetSsrc_s(ssrc);
      receiver->internal()->NotifyFirstPacketReceived(ssrc);
      break;  // Bind first unbound receiver.
    }
  }
}

// RTC_RUN_ON(context()->network_thread())
void RtpTransceiver::OnPacketReceived(
    uint32_t ssrc,
    scoped_refptr<PendingTaskSafetyFlag> safety) {
  if (!receptive_n_) {
    return;
  }
  if (packet_notified_after_receptive_) {
    return;
  }
  packet_notified_after_receptive_ = true;
  thread_->PostTask(SafeTask(safety, [this, ssrc]() {
    RTC_DCHECK_RUN_ON(thread_);
    if (stopping() || stopped() || !receptive_) {
      return;
    }
    for (const auto& receiver : receivers_) {
      receiver->internal()->NotifyFirstPacketReceivedAfterReceptiveChange(ssrc);
    }
  }));
}

void RtpTransceiver::OnFirstPacketSent() {
  for (const auto& sender : senders_) {
    sender->internal()->NotifyFirstPacketSent();
  }
}

scoped_refptr<RtpSenderInterface> RtpTransceiver::sender() const {
  RTC_DCHECK(unified_plan_);
  RTC_CHECK_EQ(1u, senders_.size());
  return senders_[0];
}

scoped_refptr<RtpReceiverInterface> RtpTransceiver::receiver() const {
  RTC_DCHECK(unified_plan_);
  RTC_CHECK_EQ(1u, receivers_.size());
  return receivers_[0];
}

void RtpTransceiver::set_current_direction(RtpTransceiverDirection direction) {
  if (current_direction_ == direction)
    return;
  RTC_LOG(LS_INFO) << "Changing transceiver (MID=" << mid_.value_or("<not set>")
                   << ") current direction from "
                   << (current_direction_ ? RtpTransceiverDirectionToString(
                                                *current_direction_)
                                          : "<not set>")
                   << " to " << RtpTransceiverDirectionToString(direction);
  current_direction_ = direction;
  if (RtpTransceiverDirectionHasSend(*current_direction_)) {
    has_ever_been_used_to_send_ = true;
  }
}

void RtpTransceiver::set_fired_direction(
    std::optional<RtpTransceiverDirection> direction) {
  fired_direction_ = direction;
}

bool RtpTransceiver::stopped() const {
  RTC_DCHECK_RUN_ON(thread_);
  return stopped_;
}

bool RtpTransceiver::stopping() const {
  RTC_DCHECK_RUN_ON(thread_);
  return stopping_;
}

RtpTransceiverDirection RtpTransceiver::direction() const {
  if (unified_plan_ && stopping())
    return RtpTransceiverDirection::kStopped;

  return direction_;
}

RTCError RtpTransceiver::SetDirectionWithError(
    RtpTransceiverDirection new_direction) {
  if (unified_plan_ && stopping()) {
    return RTC_LOG_ERROR(RTCError::InvalidState()
                         << "Cannot set direction on a stopping transceiver.");
  }
  if (new_direction == direction_)
    return RTCError::OK();

  if (new_direction == RtpTransceiverDirection::kStopped) {
    return RTC_LOG_ERROR(RTCError::InvalidParameter()
                         << "The set direction 'stopped' is invalid.");
  }

  direction_ = new_direction;
  on_negotiation_needed_();

  return RTCError::OK();
}

std::optional<RtpTransceiverDirection> RtpTransceiver::current_direction()
    const {
  if (unified_plan_ && stopped())
    return RtpTransceiverDirection::kStopped;

  return current_direction_;
}

std::optional<RtpTransceiverDirection> RtpTransceiver::fired_direction() const {
  return fired_direction_;
}

RTCError RtpTransceiver::TryToEnableSframe() {
  RTC_DCHECK_RUN_ON(thread_);

  if (sframe_enabled_.has_value() && sframe_enabled_.value() == false) {
    return RTC_LOG_ERROR(RTCError::InvalidModification()
                         << "Cannot enable Sframe after it has been "
                            "disabled by a completed negotiation.");
  }

  sframe_enabled_ = true;

  on_negotiation_needed_();

  return RTCError::OK();
}

void RtpTransceiver::ApplySframeEnabled(bool sframe_enabled) {
  RTC_DCHECK_RUN_ON(thread_);
  // Cannot re-enable Sframe after it has been negotiated to disabled.
  RTC_DCHECK(!(sframe_enabled_ == false && sframe_enabled == true));

  sframe_enabled_ = sframe_enabled;

  if (sframe_enabled && channel_) {
    channel_->EnableSframe();
  }
}

std::optional<bool> RtpTransceiver::SframeEnabled() const {
  RTC_DCHECK_RUN_ON(thread_);
  return sframe_enabled_;
}

bool RtpTransceiver::receptive() const {
  RTC_DCHECK_RUN_ON(thread_);
  return receptive_;
}

void RtpTransceiver::set_receptive(bool receptive) {
  RTC_DCHECK_RUN_ON(thread_);
  if (receptive != receptive_) {
    receptive_ = receptive;
    context()->network_thread()->PostTask(
        SafeTask(network_thread_safety_, [this, receptive = receptive]() {
          RTC_DCHECK_RUN_ON(context()->network_thread());
          receptive_n_ = receptive;
          packet_notified_after_receptive_ = false;
        }));
  }
}

absl_nonnull absl::AnyInvocable<void() &&>
RtpTransceiver::GetStopSendingAndReceiving() {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(!stopped_);
  RTC_DCHECK(!stopping_);
  // 1. Let sender be transceiver.[[Sender]].
  // 2. Let receiver be transceiver.[[Receiver]].

  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();

  // Signal to receiver sources that we're stopping.
  for (const auto& receiver : receivers_) {
    receiver->internal()->Stop();
  }

  // 4. Stop sending media with sender.
  // We do this *after* the media channel has been set to nullptr on the
  // worker thread to avoid each sender doing that within `Stop()`.
  // Senders will have already cleared send when the media channel was set to
  // nullptr.
  std::vector<absl::AnyInvocable<void() &&>> stop_sender_actions =
      DetachAndGetStopTasksForSenders(senders_);

  stopping_ = true;
  direction_ = RtpTransceiverDirection::kInactive;

  // 3. Send an RTCP BYE for each RTP stream that was being sent by sender, as
  // specified in [RFC3550].

  return
      [this, stop_sender_actions = std::move(stop_sender_actions)]() mutable {
        RTC_DCHECK_RUN_ON(context()->worker_thread());
        for (auto& task : stop_sender_actions) {
          std::move(task)();
        }
        ClearMediaChannelReferences();
      };
}

RTCError RtpTransceiver::StopStandard() {
  RTC_DCHECK_RUN_ON(thread_);
  // If we're on Plan B, do what Stop() used to do there.
  if (!unified_plan_) {
    StopInternal();
    return RTCError::OK();
  }
  // 1. Let transceiver be the RTCRtpTransceiver object on which the method is
  // invoked.
  //
  // 2. Let connection be the RTCPeerConnection object associated with
  // transceiver.
  //
  // 3. If connection.[[IsClosed]] is true, throw an InvalidStateError.
  //    (Note: Checking for IsClosed() is implemented by the user agent).
  //
  // 4. If transceiver.[[Stopping]] is true, abort these steps.
  if (stopping_) {
    return RTCError::OK();
  }

  // 5. Stop sending and receiving given transceiver, and update the
  // negotiation-needed flag for connection.
  auto stop_task = GetStopSendingAndReceiving();
  context_->worker_thread()->BlockingCall(
      [&]() mutable { std::move(stop_task)(); });
  on_negotiation_needed_();

  return RTCError::OK();
}

ScopedOperationsBatcher::BatchTaskWithFinalizer
RtpTransceiver::StopStandardAsync() {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(unified_plan_);

  if (stopping_) {
    return nullptr;
  }

  auto stop_task = GetStopSendingAndReceiving();

  return [this, stop_task = std::move(stop_task)]() mutable
             -> ScopedOperationsBatcher::BatchTaskResult {
    RTC_DCHECK_RUN_ON(context()->worker_thread());
    std::move(stop_task)();
    return ScopedOperationsBatcher::FinalizerTask([this]() {
      RTC_DCHECK_RUN_ON(thread_);
      on_negotiation_needed_();
    });
  };
}

void RtpTransceiver::StopInternal() {
  RTC_DCHECK_RUN_ON(thread_);
  auto stop_task = GetStopTransceiverProcedure();
  if (stop_task) {
    context_->worker_thread()->BlockingCall(
        [stop_task = std::move(stop_task)]() mutable {
          std::move(stop_task)();
        });
  }
}

absl_nullable absl::AnyInvocable<void() &&>
RtpTransceiver::GetStopTransceiverProcedure() {
  RTC_DCHECK_RUN_ON(thread_);
  // As specified in the "Stop the RTCRtpTransceiver" procedure
  // 1. If transceiver.[[Stopping]] is false, stop sending and receiving given
  // transceiver.
  absl::AnyInvocable<void() &&> stop_task;
  if (!stopping_)
    stop_task = GetStopSendingAndReceiving();

  // 2. Set transceiver.[[Stopped]] to true.
  stopped_ = true;

  // 3. Set transceiver.[[Receptive]] to false.
  receptive_ = false;

  // 4. Set transceiver.[[CurrentDirection]] to null.
  current_direction_ = std::nullopt;

  return stop_task;
}

RTCError RtpTransceiver::SetCodecPreferences(
    std::span<RtpCodecCapability> codec_capabilities) {
  RTC_DCHECK(unified_plan_);
  RTCError error = RTCError::OK();
  // 3. If codecs is an empty list, set transceiver's [[PreferredCodecs]] slot
  // to codecs and abort these steps.
  if (codec_capabilities.empty()) {
    codec_preferences_.clear();
    sendrecv_codec_preferences_.clear();
    sendonly_codec_preferences_.clear();
    recvonly_codec_preferences_.clear();
  } else {
    // 4. Remove any duplicate values in codecs.
    std::vector<RtpCodecCapability> codecs;
    absl::c_remove_copy_if(codec_capabilities, std::back_inserter(codecs),
                           [&codecs](const RtpCodecCapability& codec) {
                             return absl::c_linear_search(codecs, codec);
                           });
    // TODO(https://crbug.com/webrtc/391530822): Move logic in
    // MediaSessionDescriptionFactory to this level.
    error = UpdateCodecPreferencesCaches(codecs);
  }
  if (error.ok() && on_negotiation_needed_) {
    on_negotiation_needed_();
  }
  return error;
}

RTCError RtpTransceiver::UpdateCodecPreferencesCaches(
    const std::vector<RtpCodecCapability>& codecs) {
  // Get codec capabilities from media engine.
  std::vector<Codec> send_codecs, recv_codecs;
  if (media_type_ == MediaType::AUDIO) {
    send_codecs = codec_vendor().audio_send_codecs().codecs();
    recv_codecs = codec_vendor().audio_recv_codecs().codecs();
  } else if (media_type_ == MediaType::VIDEO) {
    send_codecs = codec_vendor().video_send_codecs().codecs();
    recv_codecs = codec_vendor().video_recv_codecs().codecs();
  }
  RTCError error = VerifyCodecPreferences(codecs, send_codecs, recv_codecs);
  if (!error.ok()) {
    return error;
  }
  codec_preferences_ = codecs;
  // Update the filtered views of `codec_preferences_` so that we don't have
  // to query codec capabilities when calling filtered_codec_preferences() or
  // every time the direction changes.
  sendrecv_codec_preferences_.clear();
  sendonly_codec_preferences_.clear();
  recvonly_codec_preferences_.clear();
  for (const RtpCodecCapability& codec : codec_preferences_) {
    if (!codec.IsMediaCodec()) {
      // Non-media codecs don't need to be filtered at this level.
      sendrecv_codec_preferences_.push_back(codec);
      sendonly_codec_preferences_.push_back(codec);
      recvonly_codec_preferences_.push_back(codec);
      continue;
    }
    // Is this a send codec, receive codec or both?
    bool is_send_codec =
        absl::c_any_of(send_codecs, [&codec](const Codec& send_codec) {
          return IsSameRtpCodecIgnoringLevel(send_codec, codec);
        });
    bool is_recv_codec =
        absl::c_any_of(recv_codecs, [&codec](const Codec& recv_codec) {
          return IsSameRtpCodecIgnoringLevel(recv_codec, codec);
        });
    // The codec being neither for sending or receving is not possible because
    // of prior validation by VerifyCodecPreferences().
    RTC_CHECK(is_send_codec || is_recv_codec);
    if (is_send_codec && is_recv_codec) {
      sendrecv_codec_preferences_.push_back(codec);
    }
    if (is_send_codec) {
      sendonly_codec_preferences_.push_back(codec);
    }
    if (is_recv_codec) {
      recvonly_codec_preferences_.push_back(codec);
    }
  }
  // If filtering results in an empty list this is the same as not having any
  // preferences.
  if (!HasAnyMediaCodec(sendrecv_codec_preferences_)) {
    sendrecv_codec_preferences_.clear();
  }
  if (!HasAnyMediaCodec(sendonly_codec_preferences_)) {
    sendonly_codec_preferences_.clear();
  }
  if (!HasAnyMediaCodec(recvonly_codec_preferences_)) {
    recvonly_codec_preferences_.clear();
  }
  return RTCError::OK();
}

std::vector<RtpCodecCapability> RtpTransceiver::codec_preferences() const {
  return codec_preferences_;
}

std::vector<RtpCodecCapability> RtpTransceiver::filtered_codec_preferences()
    const {
  switch (direction_) {
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kInactive:
    case RtpTransceiverDirection::kStopped:
      return sendrecv_codec_preferences_;
    case RtpTransceiverDirection::kSendOnly:
      return sendonly_codec_preferences_;
    case RtpTransceiverDirection::kRecvOnly:
      return recvonly_codec_preferences_;
  }
  return codec_preferences_;
}

std::vector<RtpHeaderExtensionCapability>
RtpTransceiver::GetHeaderExtensionsToNegotiate() const {
  RTC_DCHECK_RUN_ON(thread_);
  return header_extensions_to_negotiate_;
}

std::vector<RtpHeaderExtensionCapability> ModifyCapabilitiesAccordingToHeaders(
    const std::vector<RtpHeaderExtensionCapability>& old_values,
    const std::vector<RtpExtension>& extension_list) {
  std::vector<RtpHeaderExtensionCapability> result;
  result.reserve(old_values.size());
  // Create new capability objects that start as a copy of the old values.
  for (RtpHeaderExtensionCapability capability : old_values) {
    auto negotiated = absl::c_find_if(
        extension_list, [&capability](const RtpExtension& negotiated) {
          return negotiated.uri == capability.uri;
        });
    // TODO(bugs.webrtc.org/7477): extend when header extensions support
    // direction.
    if (negotiated != extension_list.end()) {
      capability.direction = RtpTransceiverDirection::kSendRecv;
      capability.preferred_id = negotiated->id;
      capability.preferred_encrypt = negotiated->encrypt;
    } else {
      capability.direction = RtpTransceiverDirection::kStopped;
    }
    result.push_back(capability);
  }
  return result;
}
std::vector<RtpHeaderExtensionCapability>
RtpTransceiver::GetNegotiatedHeaderExtensions() const {
  RTC_DCHECK_RUN_ON(thread_);
  return ModifyCapabilitiesAccordingToHeaders(header_extensions_to_negotiate_,
                                              negotiated_header_extensions_);
}

std::vector<RtpHeaderExtensionCapability>
RtpTransceiver::GetOfferedAndImplementedHeaderExtensions(
    const MediaContentDescription* content) const {
  RTC_DCHECK_RUN_ON(thread_);
  return ModifyCapabilitiesAccordingToHeaders(header_extensions_to_negotiate_,
                                              content->rtp_header_extensions());
}

// Helper function to determine mandatory-to-negotiate extensions.
// See https://www.rfc-editor.org/rfc/rfc8834#name-header-extensions
// and https://w3c.github.io/webrtc-extensions/#rtcrtptransceiver-interface
// Since BUNDLE is offered by default, MID is mandatory and can not be turned
// off via this API.
bool IsMandatoryHeaderExtension(absl::string_view uri) {
  return uri == RtpExtension::kMidUri;
}

RTCError RtpTransceiver::SetHeaderExtensionsToNegotiate(
    std::span<const RtpHeaderExtensionCapability> header_extensions) {
  RTC_DCHECK_RUN_ON(thread_);
  // https://w3c.github.io/webrtc-extensions/#dom-rtcrtptransceiver-setheaderextensionstonegotiate
  if (header_extensions.size() != header_extensions_to_negotiate_.size()) {
    return RTCError::InvalidModification()
           << "Size of extensions to negotiate does not match.";
  }
  // For each index i of extensions, run the following steps: ...
  for (size_t i = 0; i < header_extensions.size(); i++) {
    const auto& extension = header_extensions[i];
    if (extension.uri != header_extensions_to_negotiate_[i].uri) {
      return RTCError::InvalidModification()
             << "Reordering extensions is not allowed.";
    }
    if (IsMandatoryHeaderExtension(extension.uri) &&
        extension.direction != RtpTransceiverDirection::kSendRecv) {
      return RTCError::InvalidModification()
             << "Attempted to stop a mandatory extension.";
    }

    // TODO(bugs.webrtc.org/7477): Currently there are no recvonly extensions so
    // this can not be checked: "When there exists header extension capabilities
    // that have directions other than kSendRecv, restrict extension.direction
    // as to not exceed that capability."
  }

  // Apply mutation after error checking.
  for (size_t i = 0; i < header_extensions.size(); i++) {
    header_extensions_to_negotiate_[i].direction =
        header_extensions[i].direction;
  }

  if (on_negotiation_needed_) {
    on_negotiation_needed_();
  }

  return RTCError::OK();
}

void RtpTransceiver::OnNegotiationUpdate(
    SdpType sdp_type,
    const MediaContentDescription* content) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(content);
  if (sdp_type == SdpType::kAnswer || sdp_type == SdpType::kPrAnswer) {
    negotiated_header_extensions_ = content->rtp_header_extensions();
    if (!env_.field_trials().IsDisabled(
            "WebRTC-HeaderExtensionNegotiateMemory")) {
      header_extensions_to_negotiate_ = GetNegotiatedHeaderExtensions();
    }
  } else if (sdp_type == SdpType::kOffer) {
    if (!env_.field_trials().IsDisabled(
            "WebRTC-HeaderExtensionNegotiateMemory")) {
      header_extensions_for_rollback_ = header_extensions_to_negotiate_;
      header_extensions_to_negotiate_ =
          GetOfferedAndImplementedHeaderExtensions(content);
    }
  } else if (sdp_type == SdpType::kRollback) {
    if (!env_.field_trials().IsDisabled(
            "WebRTC-HeaderExtensionNegotiateMemory")) {
      RTC_CHECK(!header_extensions_for_rollback_.empty());
      header_extensions_to_negotiate_ = header_extensions_for_rollback_;
    }
  }
}

bool RtpTransceiver::SetRtpTransport(RtpTransportInternal* transport) {
  RTC_DCHECK_RUN_ON(context()->network_thread());

  if (!channel_) {
    return true;
  }

  if (transport == rtp_transport_) {
    return true;
  }

  ClearRtpTransportState();

  if (!channel_->SetRtpTransport(transport)) {
    return false;
  }

  SetRtpTransportState(transport);
  return true;
}

void RtpTransceiver::ClearRtpTransportState() {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  if (rtp_transport_) {
    rtp_transport_->UnsubscribeNetworkRouteChanged(this);
    rtp_transport_ = nullptr;
  }
}

void RtpTransceiver::SetRtpTransportState(RtpTransportInternal* transport) {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  RTC_DCHECK(!rtp_transport_);
  rtp_transport_ = transport;
  if (rtp_transport_) {
    // Safe to capture raw pointer to channel because we unsubscribe on the
    // network thread (via GetClearChannelNetworkTask) before the channel is
    // destroyed on the worker thread (via GetDeleteChannelWorkerTask).
    // Capturing raw pointer avoids reading `channel_` member on the network
    // thread, which races with signaling thread moving it during teardown.
    rtp_transport_->SubscribeNetworkRouteChanged(
        this,
        [this, channel = channel_.get()](std::optional<NetworkRoute> route) {
          RTC_DCHECK_RUN_ON(context()->network_thread());
          OnNetworkRouteChanged(channel, route);
        });
  }
}

RTCErrorOr<std::optional<std::string>>
RtpTransceiver::InitializeOnNetworkThread(
    absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup) {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  RTC_DCHECK(!rtp_transport_);
  RTC_DCHECK(channel_);
  RtpTransportInternal* transport = std::move(transport_lookup)();
  if (!SetRtpTransport(transport)) {
    return RTCError::InvalidParameter()
           << "Invalid transport for mid=" << channel_->mid();
  }

  std::optional<std::string> transport_name;
  if (transport) {
    transport_name = transport->transport_name();
  }
  return transport_name;
}

void RtpTransceiver::OnNetworkRouteChanged(
    ChannelInterface* channel,
    std::optional<NetworkRoute> network_route) {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  if (channel && rtp_transport_) {
    RTC_LOG(LS_INFO) << "Network route changed for mid=" << channel->mid();
    channel->media_send_channel()->OnNetworkRouteChanged(
        rtp_transport_->transport_name(),
        network_route.value_or(NetworkRoute()));
  }
}

void RtpTransceiver::SetChannelLocalContent(
    const MediaContentDescription* content,
    SdpType type,
    ScopedOperationsBatcher& batcher) {
  RTC_DCHECK_RUN_ON(context()->signaling_thread());
  SetChannelContent(
      [this, content, type]() {
        return channel_->SetLocalContent(content, type);
      },
      batcher);
}

void RtpTransceiver::SetChannelRemoteContent(
    const MediaContentDescription* content,
    SdpType type,
    ScopedOperationsBatcher& batcher) {
  RTC_DCHECK_RUN_ON(context()->signaling_thread());
  SetChannelContent(
      [this, content, type]() {
        return channel_->SetRemoteContent(content, type);
      },
      batcher);
}

void RtpTransceiver::SetChannelContent(
    absl::AnyInvocable<RTCError() &&> set_content,
    ScopedOperationsBatcher& batcher) {
  RTC_DCHECK_RUN_ON(context()->signaling_thread());

  struct SenderParameters {
    const uint32_t ssrc;
    RtpSenderInternal* const sender;
    std::optional<RtpParameters> parameters;
  };

  std::vector<SenderParameters> sender_parameters;
  sender_parameters.reserve(senders_.size());
  for (const auto& sender : senders_) {
    sender_parameters.push_back(
        {.ssrc = sender->ssrc(), .sender = sender->internal()});
  }

  batcher.AddWithFinalizer(
      [this, set_content = std::move(set_content),
       sender_parameters = std::move(sender_parameters)]() mutable
          -> ScopedOperationsBatcher::BatchTaskResult {
        RTC_DCHECK_RUN_ON(context()->worker_thread());
        if (!channel_) {
          return RTCError::InvalidState() << "No channel";
        }
        RTCError result = std::move(set_content)();
        if (!result.ok()) {
          return result;
        }
        for (auto& entry : sender_parameters) {
          if (entry.ssrc != 0) {
            entry.parameters =
                channel_->media_send_channel()->GetRtpSendParameters(
                    entry.ssrc);
          }
        }
        return ScopedOperationsBatcher::FinalizerTask(
            [sender_parameters = std::move(sender_parameters)]() mutable {
              for (auto& entry : sender_parameters) {
                if (entry.parameters) {
                  entry.sender->SetCachedParameters(
                      std::move(*entry.parameters));
                }
              }
            });
      });
}
void RtpTransceiver::EnableChannel(bool enable) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(channel_);
  channel_->Enable(enable);
}

const std::vector<StreamParams>& RtpTransceiver::channel_local_streams() const {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(channel_);
  return channel_->local_streams();
}

const std::vector<StreamParams>& RtpTransceiver::channel_remote_streams()
    const {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(channel_);
  return channel_->remote_streams();
}

absl::string_view RtpTransceiver::channel_transport_name() const {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  RTC_DCHECK(channel_);
  return channel_->transport_name();
}

absl::string_view RtpTransceiver::channel_mid() const {
  RTC_DCHECK_RUN_ON(context()->network_thread());
  return channel_ ? absl::string_view(channel_->mid()) : absl::string_view();
}

MediaSendChannelInterface* RtpTransceiver::media_send_channel() {
  RTC_DCHECK_RUN_ON(thread_);
  return channel_ ? channel_->media_send_channel() : nullptr;
}

const MediaSendChannelInterface* RtpTransceiver::media_send_channel() const {
  RTC_DCHECK_RUN_ON(thread_);
  return channel_ ? channel_->media_send_channel() : nullptr;
}

MediaReceiveChannelInterface* RtpTransceiver::media_receive_channel() {
  RTC_DCHECK_RUN_ON(thread_);
  return channel_ ? channel_->media_receive_channel() : nullptr;
}

const MediaReceiveChannelInterface* RtpTransceiver::media_receive_channel()
    const {
  RTC_DCHECK_RUN_ON(thread_);
  return channel_ ? channel_->media_receive_channel() : nullptr;
}

VideoMediaSendChannelInterface* RtpTransceiver::video_media_send_channel() {
  // Accessed from multiple threads.
  // See https://issues.webrtc.org/475126742
  return channel_ ? channel_->video_media_send_channel() : nullptr;
}

VoiceMediaSendChannelInterface* RtpTransceiver::voice_media_send_channel() {
  // Accessed from multiple threads.
  // See https://issues.webrtc.org/475126742
  return channel_ ? channel_->voice_media_send_channel() : nullptr;
}

VideoMediaReceiveChannelInterface*
RtpTransceiver::video_media_receive_channel() {
  // Accessed from multiple threads.
  // See https://issues.webrtc.org/475126742
  return channel_ ? channel_->video_media_receive_channel() : nullptr;
}

VoiceMediaReceiveChannelInterface*
RtpTransceiver::voice_media_receive_channel() {
  // Accessed from multiple threads.
  // See https://issues.webrtc.org/475126742
  return channel_ ? channel_->voice_media_receive_channel() : nullptr;
}

void RtpTransceiver::SetTransport(scoped_refptr<DtlsTransport> transport,
                                  std::optional<std::string> transport_name) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(HasChannel() || !transport);
  RTC_DCHECK((transport && transport_name.has_value()) ||
             (!transport && !transport_name));
  RTC_DCHECK(!transport_name.has_value() || !transport_name.value().empty());
  transport_name_ = std::move(transport_name);
  for (auto& sender : senders_) {
    sender->internal()->set_transport(transport);
  }
  for (auto& receiver : receivers_) {
    receiver->internal()->set_transport(transport);
  }
}

void RtpTransceiver::OnFrameDeliveredOnSignalingThread(
    uint32_t ssrc,
    const RtpPacketInfos& infos,
    Timestamp timestamp) {
  RTC_DCHECK_RUN_ON(thread_);
  for (const auto& receiver : receivers_) {
    if (receiver->internal()->ssrc_s() == ssrc) {
      receiver->internal()->OnFrameDelivered(infos, timestamp);
    }
  }
}

absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp) const>
RtpTransceiver::GetOnFrameDeliveredCallback() {
  TaskQueueBase* thread = thread_;
  scoped_refptr<PendingTaskSafetyFlag> safety = signaling_thread_safety_;
  return [thread, safety, this](uint32_t ssrc, const RtpPacketInfos& infos,
                                Timestamp timestamp) {
    thread->PostTask(SafeTask(safety, [this, ssrc, infos, timestamp]() {
      OnFrameDeliveredOnSignalingThread(ssrc, infos, timestamp);
    }));
  };
}

}  // namespace webrtc
