/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/video_capture_pipewire.h"

#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>
#include <spa/param/format-utils.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/vararg.h>
#include <spa/utils/defs.h>
#include <spa/utils/result.h>
#include <spa/utils/type.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "api/sequence_checker.h"
#include "api/video/video_rotation.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/portal/pipewire_utils.h"
#include "modules/video_capture/linux/pipewire_session.h"
#include "modules/video_capture/video_capture_defines.h"
#include "modules/video_capture/video_capture_impl.h"
#include "modules/video_capture/video_capture_options.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/sanitizer.h"
#include "rtc_base/synchronization/mutex.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {
namespace videocapturemodule {

// A reasonable maximum size so "width * height * kBytesPerPixel" doesn't
// overflow
constexpr int32_t kMaxVideoCaptureDimension = 16384;

struct {
  uint32_t spa_format;
  VideoType video_type;
} constexpr kSupportedFormats[] = {
    {.spa_format = SPA_VIDEO_FORMAT_I420, .video_type = VideoType::kI420},
    {.spa_format = SPA_VIDEO_FORMAT_NV12, .video_type = VideoType::kNV12},
    {.spa_format = SPA_VIDEO_FORMAT_YUY2, .video_type = VideoType::kYUY2},
    {.spa_format = SPA_VIDEO_FORMAT_UYVY, .video_type = VideoType::kUYVY},
    // PipeWire is big-endian for the formats, while libyuv is little-endian
    // This means that BGRA == ARGB, RGBA == ABGR and similar
    // This follows mapping in libcamera PipeWire plugin:
    // https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/spa/plugins/libcamera/libcamera-utils.cpp
    {.spa_format = SPA_VIDEO_FORMAT_BGRA, .video_type = VideoType::kARGB},
    {.spa_format = SPA_VIDEO_FORMAT_RGBA, .video_type = VideoType::kABGR},
    {.spa_format = SPA_VIDEO_FORMAT_ARGB, .video_type = VideoType::kBGRA},
    {.spa_format = SPA_VIDEO_FORMAT_RGB, .video_type = VideoType::kBGR24},
    {.spa_format = SPA_VIDEO_FORMAT_BGR, .video_type = VideoType::kRGB24},
    {.spa_format = SPA_VIDEO_FORMAT_RGB16, .video_type = VideoType::kRGB565},
};

VideoType VideoCaptureModulePipeWire::PipeWireRawFormatToVideoType(
    uint32_t spa_format) {
  for (const auto& spa_and_pixel_format : kSupportedFormats) {
    if (spa_and_pixel_format.spa_format == spa_format)
      return spa_and_pixel_format.video_type;
  }
  RTC_LOG(LS_WARNING) << "Unsupported pixel format: " << spa_format;
  return VideoType::kUnknown;
}

uint32_t VideoCaptureModulePipeWire::VideoTypeToPipeWireRawFormat(
    VideoType type) {
  for (const auto& spa_and_pixel_format : kSupportedFormats) {
    if (spa_and_pixel_format.video_type == type)
      return spa_and_pixel_format.spa_format;
  }
  RTC_LOG(LS_WARNING) << "Unsupported video type: " << static_cast<int>(type);
  return SPA_VIDEO_FORMAT_UNKNOWN;
}

VideoCaptureModulePipeWire::VideoCaptureModulePipeWire(
    Clock* clock,
    VideoCaptureOptions* options)
    : VideoCaptureImpl(clock),
      session_(options->pipewire_session()),
      initialized_(false),
      started_(false) {}

VideoCaptureModulePipeWire::~VideoCaptureModulePipeWire() {
  RTC_DCHECK_RUN_ON(&api_checker_);

  StopCapture();
}

int32_t VideoCaptureModulePipeWire::Init(const char* deviceUniqueId) {
  RTC_CHECK_RUNS_SERIALIZED(&capture_checker_);
  RTC_DCHECK_RUN_ON(&api_checker_);

  auto node =
      std::find_if(session_->nodes_.begin(), session_->nodes_.end(),
                   [deviceUniqueId](const PipeWireNode::PipeWireNodePtr& node) {
                     return node->unique_id() == deviceUniqueId;
                   });
  if (node == session_->nodes_.end())
    return -1;

  node_id_ = (*node)->id();

  const int len = strlen(deviceUniqueId);
  _deviceUniqueId = new (std::nothrow) char[len + 1];
  memcpy(_deviceUniqueId, deviceUniqueId, len + 1);

  return 0;
}

static spa_pod* BuildFormat(spa_pod_builder* builder,
                            VideoType video_type,
                            uint32_t width,
                            uint32_t height,
                            float frame_rate) {
  spa_pod_frame frame;

  const uint32_t media_subtype = video_type == VideoType::kMJPEG
                                     ? SPA_MEDIA_SUBTYPE_mjpg
                                     : SPA_MEDIA_SUBTYPE_raw;

  spa_pod_builder_push_object(builder, &frame, SPA_TYPE_OBJECT_Format,
                              SPA_PARAM_EnumFormat);
  spa_pod_builder_add(builder, SPA_FORMAT_mediaType,
                      SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
                      SPA_POD_Id(media_subtype), 0);

  if (media_subtype == SPA_MEDIA_SUBTYPE_raw) {
    const uint32_t format =
        VideoCaptureModulePipeWire::VideoTypeToPipeWireRawFormat(video_type);
    RTC_CHECK(format != SPA_VIDEO_FORMAT_UNKNOWN);
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format),
                        0);
  }

  spa_rectangle resolution = spa_rectangle{.width = width, .height = height};
  spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_size,
                      SPA_POD_Rectangle(&resolution), 0);

  // Framerate can be also set to 0 to be unspecified
  if (frame_rate) {
    spa_fraction framerate =
        spa_fraction{.num = static_cast<uint32_t>(frame_rate), .denom = 1};
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_framerate,
                        SPA_POD_Fraction(&framerate), 0);
  } else {
    // Default to some reasonable values
    spa_fraction preferred_frame_rate =
        spa_fraction{.num = static_cast<uint32_t>(30), .denom = 1};
    spa_fraction min_frame_rate = spa_fraction{.num = 1, .denom = 1};
    spa_fraction max_frame_rate = spa_fraction{.num = 30, .denom = 1};
    spa_pod_builder_add(
        builder, SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&preferred_frame_rate, &min_frame_rate,
                                      &max_frame_rate),
        0);
  }

  return static_cast<spa_pod*>(spa_pod_builder_pop(builder, &frame));
}

RTC_NO_SANITIZE("cfi-icall")
int32_t VideoCaptureModulePipeWire::StartCapture(
    const VideoCaptureCapability& capability) {
  RTC_DCHECK_RUN_ON(&api_checker_);

  if (initialized_) {
    if (capability == _requestedCapability) {
      return 0;
    } else {
      StopCapture();
    }
  }

  uint8_t buffer[1024] = {};

  // We don't want members above to be guarded by capture_checker_ as
  // it's meant to be for members that are accessed on the API thread
  // only when we are not capturing. The code above can be called many
  // times while sharing instance of VideoCapturePipeWire between
  // websites and therefore it would not follow the requirements of this
  // checker.
  RTC_CHECK_RUNS_SERIALIZED(&capture_checker_);
  PipeWireThreadLoopLock thread_loop_lock(session_->pw_main_loop_);

  RTC_LOG(LS_VERBOSE) << "Creating new PipeWire stream for node " << node_id_;

  pw_properties* reuse_props =
      pw_properties_new_string("pipewire.client.reuse=1");
  stream_ = pw_stream_new(session_->pw_core_, "camera-stream", reuse_props);

  if (!stream_) {
    RTC_LOG(LS_ERROR) << "Failed to create camera stream!";
    return -1;
  }

  static const pw_stream_events stream_events{
      .version = PW_VERSION_STREAM_EVENTS,
      .state_changed = &OnStreamStateChanged,
      .param_changed = &OnStreamParamChanged,
      .process = &OnStreamProcess,
  };

  pw_stream_add_listener(stream_, &stream_listener_, &stream_events, this);

  spa_pod_builder builder =
      spa_pod_builder{.data = buffer, .size = sizeof(buffer)};
  std::vector<const spa_pod*> params;
  uint32_t width = capability.width;
  uint32_t height = capability.height;
  uint32_t frame_rate = capability.maxFPS;
  VideoType video_type = capability.videoType;

  params.push_back(
      BuildFormat(&builder, video_type, width, height, frame_rate));

  int res = pw_stream_connect(
      stream_, PW_DIRECTION_INPUT, node_id_,
      static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                        PW_STREAM_FLAG_DONT_RECONNECT),
      params.data(), params.size());
  if (res != 0) {
    RTC_LOG(LS_ERROR) << "Could not connect to camera stream: "
                      << spa_strerror(res);
    return -1;
  }

  _requestedCapability = capability;
  initialized_ = true;

  return 0;
}

RTC_NO_SANITIZE("cfi-icall")
int32_t VideoCaptureModulePipeWire::StopCapture() {
  RTC_DCHECK_RUN_ON(&api_checker_);

  PipeWireThreadLoopLock thread_loop_lock(session_->pw_main_loop_);

  // PipeWireSession is guarded by API checker so just make sure we do
  // race detection when the PipeWire loop is locked/stopped to not run
  // any callback at this point.
  RTC_CHECK_RUNS_SERIALIZED(&capture_checker_);
  if (stream_) {
    // Removing the listener first guarantees no callbacks will fire after this
    // point.
    spa_hook_remove(&stream_listener_);
    pw_stream_destroy(stream_);
    stream_ = nullptr;
  }

  _requestedCapability = VideoCaptureCapability();
  return 0;
}

bool VideoCaptureModulePipeWire::CaptureStarted() {
  RTC_DCHECK_RUN_ON(&api_checker_);
  MutexLock lock(&api_lock_);

  return started_;
}

int32_t VideoCaptureModulePipeWire::CaptureSettings(
    VideoCaptureCapability& settings) {
  RTC_DCHECK_RUN_ON(&api_checker_);

  settings = _requestedCapability;

  return 0;
}

void VideoCaptureModulePipeWire::OnStreamParamChanged(
    void* data,
    uint32_t id,
    const struct spa_pod* format) {
  VideoCaptureModulePipeWire* that =
      static_cast<VideoCaptureModulePipeWire*>(data);
  RTC_DCHECK(that);
  RTC_CHECK_RUNS_SERIALIZED(&that->capture_checker_);

  if (format && id == SPA_PARAM_Format)
    that->OnFormatChanged(format);
}

static int32_t MaxFPSFromFractions(const spa_fraction& framerate,
                                   const spa_fraction& max_framerate) {
  if (framerate.num && framerate.denom)
    return framerate.num / framerate.denom;
  if (max_framerate.num && max_framerate.denom)
    return max_framerate.num / max_framerate.denom;
  return 30;
}

RTC_NO_SANITIZE("cfi-icall")
void VideoCaptureModulePipeWire::OnFormatChanged(const struct spa_pod* format) {
  RTC_CHECK_RUNS_SERIALIZED(&capture_checker_);

  uint32_t media_type, media_subtype;

  if (spa_format_parse(format, &media_type, &media_subtype) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to parse video format.";
    return;
  }

  switch (media_subtype) {
    case SPA_MEDIA_SUBTYPE_raw: {
      struct spa_video_info_raw f = SPA_VIDEO_INFO_RAW_INIT();
      spa_format_video_raw_parse(format, &f);
      configured_capability_.width = f.size.width;
      configured_capability_.height = f.size.height;
      configured_capability_.videoType = PipeWireRawFormatToVideoType(f.format);
      configured_capability_.maxFPS =
          MaxFPSFromFractions(f.framerate, f.max_framerate);
      break;
    }
    case SPA_MEDIA_SUBTYPE_mjpg: {
      struct spa_video_info_mjpg f = {};
      spa_format_video_mjpg_parse(format, &f);
      configured_capability_.width = f.size.width;
      configured_capability_.height = f.size.height;
      configured_capability_.videoType = VideoType::kMJPEG;
      configured_capability_.maxFPS =
          MaxFPSFromFractions(f.framerate, f.max_framerate);
      break;
    }
    default:
      configured_capability_.videoType = VideoType::kUnknown;
  }

  if (configured_capability_.videoType == VideoType::kUnknown) {
    RTC_LOG(LS_ERROR) << "Unsupported video format.";
    return;
  }

  if (configured_capability_.width <= 0 || configured_capability_.height <= 0 ||
      configured_capability_.width > kMaxVideoCaptureDimension ||
      configured_capability_.height > kMaxVideoCaptureDimension) {
    RTC_LOG(LS_ERROR) << "Unsupported video resolution.";
    configured_capability_.videoType = VideoType::kUnknown;
    return;
  }

  RTC_LOG(LS_VERBOSE) << "Configured capture format = "
                      << static_cast<int>(configured_capability_.videoType);

  uint8_t buffer[1024] = {};
  auto builder = spa_pod_builder{.data = buffer, .size = sizeof(buffer)};

  // Setup buffers and meta header for new format.
  std::vector<const spa_pod*> params;
  spa_pod_frame frame;
  spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_ParamBuffers,
                              SPA_PARAM_Buffers);

  const int buffer_types =
      (1 << SPA_DATA_DmaBuf) | (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr);
  spa_pod_builder_add(
      &builder, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 1, 32),
      SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(buffer_types), 0);
  params.push_back(
      static_cast<spa_pod*>(spa_pod_builder_pop(&builder, &frame)));

  params.push_back(reinterpret_cast<spa_pod*>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type,
      SPA_POD_Id(SPA_META_Header), SPA_PARAM_META_size,
      SPA_POD_Int(sizeof(struct spa_meta_header)))));
  params.push_back(reinterpret_cast<spa_pod*>(spa_pod_builder_add_object(
      &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type,
      SPA_POD_Id(SPA_META_VideoTransform), SPA_PARAM_META_size,
      SPA_POD_Int(sizeof(struct spa_meta_videotransform)))));
  pw_stream_update_params(stream_, params.data(), params.size());
}

void VideoCaptureModulePipeWire::OnStreamStateChanged(
    void* data,
    pw_stream_state old_state,
    pw_stream_state state,
    const char* error_message) {
  VideoCaptureModulePipeWire* that =
      static_cast<VideoCaptureModulePipeWire*>(data);
  RTC_DCHECK(that);

  MutexLock lock(&that->api_lock_);
  switch (state) {
    case PW_STREAM_STATE_STREAMING:
      that->started_ = true;
      break;
    case PW_STREAM_STATE_ERROR:
      RTC_LOG(LS_ERROR) << "PipeWire stream state error: " << error_message;
      [[fallthrough]];
    case PW_STREAM_STATE_PAUSED:
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      that->started_ = false;
      break;
  }
  RTC_LOG(LS_VERBOSE) << "PipeWire stream state change: "
                      << pw_stream_state_as_string(old_state) << " -> "
                      << pw_stream_state_as_string(state);
}

void VideoCaptureModulePipeWire::OnStreamProcess(void* data) {
  VideoCaptureModulePipeWire* that =
      static_cast<VideoCaptureModulePipeWire*>(data);
  RTC_DCHECK(that);
  RTC_CHECK_RUNS_SERIALIZED(&that->capture_checker_);
  that->ProcessBuffers();
}

static VideoRotation VideorotationFromPipeWireTransform(uint32_t transform) {
  switch (transform) {
    case SPA_META_TRANSFORMATION_90:
      return kVideoRotation_90;
    case SPA_META_TRANSFORMATION_180:
      return kVideoRotation_180;
    case SPA_META_TRANSFORMATION_270:
      return kVideoRotation_270;
    default:
      return kVideoRotation_0;
  }
}

RTC_NO_SANITIZE("cfi-icall")
void VideoCaptureModulePipeWire::ProcessBuffers() {
  RTC_CHECK_RUNS_SERIALIZED(&capture_checker_);

  while (pw_buffer* buffer = pw_stream_dequeue_buffer(stream_)) {
    spa_buffer* spaBuffer = buffer->buffer;
    struct spa_meta_header* h;
    h = static_cast<struct spa_meta_header*>(
        spa_buffer_find_meta_data(spaBuffer, SPA_META_Header, sizeof(*h)));

    if (h && (h->flags & SPA_META_HEADER_FLAG_CORRUPTED)) {
      RTC_LOG(LS_INFO) << "Dropping corrupted frame.";
      pw_stream_queue_buffer(stream_, buffer);
      continue;
    }

    if (static_cast<uint64_t>(spaBuffer->datas[0].chunk->offset) +
            spaBuffer->datas[0].chunk->size >
        spaBuffer->datas[0].maxsize) {
      RTC_LOG(LS_ERROR) << "Dropping frame with invalid size";
      pw_stream_queue_buffer(stream_, buffer);
      continue;
    }

    SetStride(spaBuffer->datas[0].chunk->stride);

    struct spa_meta_videotransform* videotransform;
    videotransform =
        static_cast<struct spa_meta_videotransform*>(spa_buffer_find_meta_data(
            spaBuffer, SPA_META_VideoTransform, sizeof(*videotransform)));
    if (videotransform) {
      VideoRotation rotation =
          VideorotationFromPipeWireTransform(videotransform->transform);
      SetCaptureRotation(rotation);
      SetApplyRotation(rotation != kVideoRotation_0);
    }

    if (spaBuffer->datas[0].type == SPA_DATA_DmaBuf ||
        spaBuffer->datas[0].type == SPA_DATA_MemFd) {
      ScopedBuf frame;
      frame.initialize(
          static_cast<uint8_t*>(
              mmap(nullptr, spaBuffer->datas[0].maxsize, PROT_READ, MAP_SHARED,
                   spaBuffer->datas[0].fd, spaBuffer->datas[0].mapoffset)),
          spaBuffer->datas[0].maxsize, spaBuffer->datas[0].fd,
          spaBuffer->datas[0].type == SPA_DATA_DmaBuf);

      if (!frame) {
        RTC_LOG(LS_ERROR) << "Failed to mmap the memory: "
                          << std::strerror(errno);
        pw_stream_queue_buffer(stream_, buffer);
        return;
      }

      IncomingFrame(frame.get(), spaBuffer->datas[0].chunk->size,
                    configured_capability_);
    } else {  // SPA_DATA_MemPtr
      IncomingFrame(static_cast<uint8_t*>(spaBuffer->datas[0].data),
                    spaBuffer->datas[0].chunk->size, configured_capability_);
    }

    pw_stream_queue_buffer(stream_, buffer);
  }
}

}  // namespace videocapturemodule
}  // namespace webrtc
