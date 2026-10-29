/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/connection_context.h"

#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "api/audio_options.h"
#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/transport/sctp_transport_factory_interface.h"
#include "media/base/media_engine.h"
#include "media/sctp/sctp_transport_factory.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "pc/media_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/network.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"

namespace webrtc {

namespace {

Thread* MaybeStartNetworkThread(
    Thread* old_thread,
    std::unique_ptr<SocketFactory>& socket_factory_holder,
    std::unique_ptr<Thread>& thread_holder) {
  if (old_thread) {
    return old_thread;
  }
  std::unique_ptr<SocketServer> socket_server = CreateDefaultSocketServer();
  thread_holder = std::make_unique<Thread>(socket_server.get());
  socket_factory_holder = std::move(socket_server);

  thread_holder->SetName("pc_network_thread", nullptr);
  thread_holder->Start();
  return thread_holder.get();
}

Thread* MaybeWrapThread(Thread* signaling_thread, bool& wraps_current_thread) {
  wraps_current_thread = false;
  if (signaling_thread) {
    return signaling_thread;
  }
  auto this_thread = Thread::Current();
  if (!this_thread) {
    // If this thread isn't already wrapped by an webrtc::Thread, create a
    // wrapper and own it in this class.
    this_thread = ThreadManager::Instance()->WrapCurrentThread();
    wraps_current_thread = true;
  }
  return this_thread;
}

std::unique_ptr<SctpTransportFactoryInterface> MaybeCreateSctpFactory(
    std::unique_ptr<SctpTransportFactoryInterface> factory,
    Thread* network_thread) {
  if (factory) {
    return factory;
  }
#ifdef WEBRTC_HAVE_SCTP
  return std::make_unique<SctpTransportFactory>(network_thread);
#else
  return nullptr;
#endif
}

}  // namespace

// Static
scoped_refptr<ConnectionContext> ConnectionContext::Create(
    const Environment& env,
    PeerConnectionFactoryDependencies* absl_nonnull dependencies) {
  return scoped_refptr<ConnectionContext>(
      new ConnectionContext(env, dependencies));
}

// Access to the media engine operations is constrained to the worker thread.
// This accessor via `MediaEngineReference` is provided to help ensure that a
// reference is held and that the call is being issued on the worker thread.
MediaEngineInterface* ConnectionContext::MediaEngineReference::media_engine()
    const {
  RTC_DCHECK_RUN_ON(c_->worker_thread());
  RTC_DCHECK(c_->media_engine_w());
  return c_->media_engine_w();
}

ConnectionContext::ConnectionContext(
    const Environment& env,
    PeerConnectionFactoryDependencies* absl_nonnull dependencies)
    : is_configured_for_media_(dependencies->media_factory != nullptr),
      network_thread_(MaybeStartNetworkThread(dependencies->network_thread,
                                              owned_socket_factory_,
                                              owned_network_thread_)),
      worker_thread_(network_thread_),
      signaling_thread_(MaybeWrapThread(dependencies->signaling_thread,
                                        wraps_current_thread_)),
      media_engine_(
          is_configured_for_media_
              ? dependencies->media_factory->CreateMediaEngine(env,
                                                               *dependencies)
              : nullptr),
      network_monitor_factory_(
          std::move(dependencies->network_monitor_factory)),
      default_network_manager_(std::move(dependencies->network_manager)),
      call_factory_(std::move(dependencies->media_factory)),
      default_socket_factory_(std::move(dependencies->packet_socket_factory)),
      sctp_factory_(
          MaybeCreateSctpFactory(std::move(dependencies->sctp_factory),
                                 network_thread())) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(network_thread_ != nullptr);
  RTC_DCHECK(worker_thread_ != nullptr);
  RTC_DCHECK(!(default_network_manager_ && network_monitor_factory_))
      << "You can't set both network_manager and network_monitor_factory.";

  signaling_thread_->AllowInvokesToThread(worker_thread());
  signaling_thread_->AllowInvokesToThread(network_thread_);
  worker_thread_->AllowInvokesToThread(network_thread_);
  if (!network_thread_->IsCurrent()) {
    // network_thread_->IsCurrent() == true means signaling_thread_ is
    // network_thread_. In this case, no further action is required as
    // signaling_thread_ can already invoke network_thread_.
    network_thread_->PostTask(
        [thread = network_thread_, worker_thread = worker_thread_] {
          thread->DisallowBlockingCalls();
          thread->DisallowAllInvokes();
          if (worker_thread == thread) {
            // In this case, worker_thread_ == network_thread_
            thread->AllowInvokesToThread(thread);
          }
        });
  }

  SocketFactory* socket_factory = dependencies->socket_factory;
  if (socket_factory == nullptr) {
    if (owned_socket_factory_) {
      socket_factory = owned_socket_factory_.get();
    } else {
      // TODO(bugs.webrtc.org/13145): This case should be deleted. Either
      // require that a PacketSocketFactory and NetworkManager always are
      // injected (with no need to construct these default objects), or require
      // that if a network_thread is injected, an approprite
      // webrtc::SocketServer should be injected too.
      socket_factory = network_thread()->socketserver();
    }
  }
  if (!default_network_manager_) {
    // If network_monitor_factory_ is non-null, it will be used to create a
    // network monitor while on the network thread.
    default_network_manager_ = std::make_unique<BasicNetworkManager>(
        env, socket_factory, network_monitor_factory_.get());
  }
  if (!default_socket_factory_) {
    default_socket_factory_ =
        std::make_unique<BasicPacketSocketFactory>(socket_factory);
  }
  // Set warning levels on the threads, to give warnings when response
  // may be slower than is expected of the thread.
  // Since some of the threads may be the same, start with the least
  // restrictive limits and end with the least permissive ones.
  // This will give warnings for all cases.
  signaling_thread_->SetDispatchWarningMs(100);
  worker_thread_->SetDispatchWarningMs(30);
  network_thread_->SetDispatchWarningMs(10);

  blocking_media_engine_destruction_ =
      env.field_trials().IsEnabled("WebRTC-SynchronousDestructors");
}

ConnectionContext::~ConnectionContext() {
  RTC_DCHECK_RUN_ON(signaling_thread_);

  // Now that the `media_engine_reference_count_` will be 0 when we get here,
  // the blocking terminate operation that previously ran as part of the
  // destructor of the media engine, has already run and there's not
  // a need any longer to do the blocking call behind the
  // `blocking_media_engine_destruction_` flag.
  RTC_DCHECK_EQ(media_engine_reference_count_, 0);

  // `media_engine_` requires destruction to happen on the worker thread.
  if (blocking_media_engine_destruction_) {
    // The media engine shares its Environment with objects that may outlive
    // the ConnectionContext if this call is not blocking. If Environment is
    // destroyed when ConnectionContext's destruction completes, this may
    // cause Use-After-Free.
    //
    // The plan is to address the problem with a new Terminate(callback) method,
    // which is referenced in webrtc:443588673, but pending this one can
    // control this with field trial `WebRTC-SynchronousDestructors`.
    worker_thread_->BlockingCall([&] { media_engine_ = nullptr; });
  } else {
    worker_thread_->PostTask([media_engine = std::move(media_engine_)] {});
  }

  // Make sure `worker_thread()` and `signaling_thread()` outlive
  // `default_socket_factory_` and `default_network_manager_`.
  default_socket_factory_ = nullptr;
  default_network_manager_ = nullptr;

  if (wraps_current_thread_)
    ThreadManager::Instance()->UnwrapCurrentThread();
}

MediaEngineInterface* ConnectionContext::media_engine_w() {
  RTC_DCHECK_RUN_ON(worker_thread());
  return media_engine_.get();
}

void ConnectionContext::ApplyGlobalAudioOptions(const AudioOptions& options) {
  RTC_DCHECK_RUN_ON(worker_thread());
  global_audio_options_.SetAll(options);
  if (media_engine_reference_count_ > 0) {
    media_engine_->voice().ApplyGlobalOptions(global_audio_options_);
  }
}

void ConnectionContext::AddRefMediaEngine() {
  RTC_DCHECK_RUN_ON(worker_thread());
  RTC_DCHECK_GE(media_engine_reference_count_, 0);
  RTC_DCHECK(media_engine_);
  ++media_engine_reference_count_;
  if (media_engine_reference_count_ == 1) {
    media_engine_->Init();
    media_engine_->voice().ApplyGlobalOptions(global_audio_options_);
  }
}

void ConnectionContext::ReleaseMediaEngine() {
  RTC_DCHECK_RUN_ON(worker_thread());
  RTC_DCHECK_GT(media_engine_reference_count_, 0);
  RTC_DCHECK(media_engine_);
  --media_engine_reference_count_;
  if (media_engine_reference_count_ == 0) {
    media_engine_->Terminate();
  }
}

VoiceChannelFactoryInterface* ConnectionContext::voice_channel_factory() {
  RTC_DCHECK(is_configured_for_media());
  return media_engine_ != nullptr ? &media_engine_->voice() : nullptr;
}

VideoChannelFactoryInterface* ConnectionContext::video_channel_factory() {
  RTC_DCHECK(is_configured_for_media());
  return media_engine_ != nullptr ? &media_engine_->video() : nullptr;
}

}  // namespace webrtc
