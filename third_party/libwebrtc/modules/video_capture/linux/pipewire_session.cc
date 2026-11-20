/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/pipewire_session.h"

#include <pipewire/pipewire.h>
#include <spa/monitor/device.h>
#include <spa/param/format-utils.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/param/video/raw.h>
#include <spa/pod/iter.h>
#include <spa/pod/pod.h>
#include <spa/utils/defs.h>
#include <spa/utils/dict.h>
#include <spa/utils/hook.h>
#include <spa/utils/type.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/portal/pipewire_utils.h"
#include "modules/portal/portal_request_response.h"
#include "modules/video_capture/linux/camera_portal.h"
#include "modules/video_capture/linux/device_info_pipewire.h"
#include "modules/video_capture/linux/pipewire_video_sizes.h"
#include "modules/video_capture/video_capture_defines.h"
#include "modules/video_capture/video_capture_options.h"
#include "rtc_base/logging.h"
#include "rtc_base/sanitizer.h"
#include "rtc_base/string_to_number.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {
namespace videocapturemodule {

// Checks that the pod matches the expected type and is large enough to hold it.
static bool SpaValueIsType(const spa_pod* val, uint32_t type) {
  return val->type == type && val->size >= spa_pod_type_size(type);
}

VideoType PipeWireRawFormatToVideoType(uint32_t id) {
  switch (id) {
    case SPA_VIDEO_FORMAT_I420:
      return VideoType::kI420;
    case SPA_VIDEO_FORMAT_NV12:
      return VideoType::kNV12;
    case SPA_VIDEO_FORMAT_YUY2:
      return VideoType::kYUY2;
    case SPA_VIDEO_FORMAT_UYVY:
      return VideoType::kUYVY;
    case SPA_VIDEO_FORMAT_RGB16:
      return VideoType::kRGB565;
    case SPA_VIDEO_FORMAT_RGB:
      return VideoType::kBGR24;
    case SPA_VIDEO_FORMAT_BGR:
      return VideoType::kRGB24;
    case SPA_VIDEO_FORMAT_BGRA:
      return VideoType::kARGB;
    case SPA_VIDEO_FORMAT_RGBA:
      return VideoType::kABGR;
    case SPA_VIDEO_FORMAT_ARGB:
      return VideoType::kBGRA;
    default:
      return VideoType::kUnknown;
  }
}

void PipeWireNode::PipeWireNodeDeleter::operator()(
    PipeWireNode* node) const noexcept {
  spa_hook_remove(&node->node_listener_);
  spa_hook_remove(&node->proxy_listener_);
  pw_proxy_destroy(node->proxy_);
  pw_node_info_free(node->info_);
}

// static
PipeWireNode::PipeWireNodePtr PipeWireNode::Create(PipeWireSession* session,
                                                   uint32_t id,
                                                   const spa_dict* props) {
  return PipeWireNodePtr(new PipeWireNode(session, id, props));
}

RTC_NO_SANITIZE("cfi-icall")
PipeWireNode::PipeWireNode(PipeWireSession* session,
                           uint32_t id,
                           const spa_dict* props)
    : session_(session),
      id_(id),
      display_name_(spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION)),
      unique_id_(spa_dict_lookup(props, PW_KEY_NODE_NAME)) {
  RTC_LOG(LS_VERBOSE) << "Found Camera: " << display_name_;

  proxy_ = static_cast<pw_proxy*>(pw_registry_bind(
      session_->pw_registry_, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));

  static const pw_node_events node_events{
      .version = PW_VERSION_NODE_EVENTS,
      .info = OnNodeInfo,
      .param = OnNodeParam,
  };

  pw_node_add_listener(reinterpret_cast<pw_node*>(proxy_), &node_listener_, &node_events, this);

  static const pw_proxy_events proxy_events{
      .version = PW_VERSION_PROXY_EVENTS,
      .done = OnProxyDone,
  };

  pw_proxy_add_listener(proxy_, &proxy_listener_, &proxy_events, this);
}

// static
RTC_NO_SANITIZE("cfi-icall")
void PipeWireNode::OnNodeInfo(void* data, const pw_node_info* info) {
  PipeWireNode* that = static_cast<PipeWireNode*>(data);

  that->info_ = pw_node_info_update(that->info_, info);
  if (!that->info_)
    return;

  info = that->info_;

  if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
    const char* vid_str;
    const char* pid_str;
    std::optional<int> vid;
    std::optional<int> pid;

    vid_str = spa_dict_lookup(info->props, SPA_KEY_DEVICE_VENDOR_ID);
    pid_str = spa_dict_lookup(info->props, SPA_KEY_DEVICE_PRODUCT_ID);
    vid = vid_str ? webrtc::StringToNumber<int>(vid_str) : std::nullopt;
    pid = pid_str ? webrtc::StringToNumber<int>(pid_str) : std::nullopt;

    if (vid && pid) {
      char model_str[10];
      snprintf(model_str, sizeof(model_str), "%04x:%04x", vid.value(),
               pid.value());
      that->model_id_ = model_str;
    }
  }

  if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
    for (uint32_t i = 0; i < info->n_params; i++) {
      if (info->params[i].user == 0)
        continue;
      info->params[i].user = 0;

      uint32_t id = info->params[i].id;
      if (id == SPA_PARAM_EnumFormat &&
          info->params[i].flags & SPA_PARAM_INFO_READ) {
        that->pending_capabilities_.clear();
        pw_node_enum_params(reinterpret_cast<pw_node*>(that->proxy_), 0, id, 0, UINT32_MAX, nullptr);
        that->sync_seq_ = pw_proxy_sync(that->proxy_, that->sync_seq_);
        that->session_->PipeWireSync();
        break;
      }
    }
  }
}

// static
RTC_NO_SANITIZE("cfi-icall")
void PipeWireNode::OnNodeParam(void* data,
                               int seq,
                               uint32_t id,
                               uint32_t index,
                               uint32_t next,
                               const spa_pod* param) {
  PipeWireNode* that = static_cast<PipeWireNode*>(data);
  auto* obj = reinterpret_cast<const spa_pod_object*>(param);
  const spa_pod_prop* prop = nullptr;
  VideoCaptureCapability cap;
  spa_pod* val;
  uint32_t n_items, choice;

  cap.videoType = VideoType::kUnknown;
  cap.maxFPS = 0;

  prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_framerate);
  if (prop) {
    val = spa_pod_get_values(&prop->value, &n_items, &choice);
    if (SpaValueIsType(val, SPA_TYPE_Fraction)) {
      spa_fraction* fract;

      fract = static_cast<spa_fraction*>(SPA_POD_BODY(val));

      if (choice == SPA_CHOICE_None && n_items >= 1) {
        cap.maxFPS = 1.0 * fract[0].num / fract[0].denom;
      } else if (choice == SPA_CHOICE_Enum && n_items >= 2) {
        for (uint32_t i = 1; i < n_items; i++) {
          cap.maxFPS = std::max(
              static_cast<int32_t>(1.0 * fract[i].num / fract[i].denom),
              cap.maxFPS);
        }
      } else if (choice == SPA_CHOICE_Range && n_items >= 2 &&
                 fract[1].num > 0) {
        cap.maxFPS = 1.0 * fract[1].num / fract[1].denom;
      }
    }
  }

  prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_size);
  if (!prop)
    return;

  const std::vector<spa_rectangle> sizes = GetPipeWireVideoSizes(&prop->value);
  if (sizes.empty())
    return;

  if (!ParseFormat(param, &cap))
    return;

  for (const spa_rectangle& size : sizes) {
    cap.width = size.width;
    cap.height = size.height;

    RTC_LOG(LS_VERBOSE) << "Found Format(" << that->display_name_
                        << "): " << static_cast<int>(cap.videoType) << "("
                        << cap.width << "x" << cap.height << "@" << cap.maxFPS
                        << ")";

    that->pending_capabilities_.push_back(cap);
  }
}

// static
void PipeWireNode::OnProxyDone(void* data, int seq) {
  PipeWireNode* that = static_cast<PipeWireNode*>(data);

  if (seq != that->sync_seq_)
    return;

  that->capabilities_ = std::move(that->pending_capabilities_);
  that->pending_capabilities_.clear();
}

// static
bool PipeWireNode::ParseFormat(const spa_pod* param,
                               VideoCaptureCapability* cap) {
  auto* obj = reinterpret_cast<const spa_pod_object*>(param);
  uint32_t media_type, media_subtype;

  if (spa_format_parse(param, &media_type, &media_subtype) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to parse video format.";
    return false;
  }

  if (media_type != SPA_MEDIA_TYPE_video)
    return false;

  if (media_subtype == SPA_MEDIA_SUBTYPE_raw) {
    const spa_pod_prop* prop = nullptr;
    uint32_t n_items, choice;
    spa_pod* val;
    uint32_t* id;

    prop = spa_pod_object_find_prop(obj, prop, SPA_FORMAT_VIDEO_format);
    if (!prop)
      return false;

    val = spa_pod_get_values(&prop->value, &n_items, &choice);
    if (!SpaValueIsType(val, SPA_TYPE_Id))
      return false;

    if (choice != SPA_CHOICE_None || n_items < 1)
      return false;

    id = static_cast<uint32_t*>(SPA_POD_BODY(val));

    cap->videoType = PipeWireRawFormatToVideoType(id[0]);
    if (cap->videoType == VideoType::kUnknown) {
      RTC_LOG(LS_INFO) << "Unsupported PipeWire pixel format " << id[0];
      return false;
    }

  } else if (media_subtype == SPA_MEDIA_SUBTYPE_mjpg) {
    cap->videoType = VideoType::kMJPEG;
  } else {
    RTC_LOG(LS_INFO) << "Unsupported PipeWire media subtype " << media_subtype;
  }

  return cap->videoType != VideoType::kUnknown;
}

CameraPortalNotifier::CameraPortalNotifier(PipeWireSession* session)
    : session_(session) {}

void CameraPortalNotifier::OnCameraRequestResult(
    xdg_portal::RequestResponse result,
    int fd) {
  if (result == xdg_portal::RequestResponse::kSuccess) {
    session_->InitPipeWire(fd);
  } else if (result == xdg_portal::RequestResponse::kUserCancelled) {
    session_->Finish(VideoCaptureOptions::Status::DENIED);
  } else {
    session_->Finish(VideoCaptureOptions::Status::ERROR);
  }
}

PipeWireSession::PipeWireSession()
    : status_(VideoCaptureOptions::Status::UNINITIALIZED) {}

PipeWireSession::~PipeWireSession() {
  Cleanup();
}

void PipeWireSession::Init(VideoCaptureOptions::Callback* callback, int fd) {
  {
    webrtc::MutexLock lock(&callback_lock_);
    callback_ = callback;
  }

  if (fd != kInvalidPipeWireFd) {
    InitPipeWire(fd);
  } else {
    portal_notifier_ = std::make_unique<CameraPortalNotifier>(this);
    portal_ = std::make_unique<CameraPortal>(portal_notifier_.get());
    portal_->Start();
  }
}

void PipeWireSession::InitPipeWire(int fd) {
  if (!InitializePipeWire())
    Finish(VideoCaptureOptions::Status::UNAVAILABLE);

  if (!StartPipeWire(fd))
    Finish(VideoCaptureOptions::Status::ERROR);
}

bool PipeWireSession::RegisterDeviceInfo(DeviceInfoPipeWire* device_info) {
  RTC_CHECK(device_info);
  MutexLock lock(&device_info_lock_);
  auto it = std::find(device_info_list_.begin(), device_info_list_.end(),
                      device_info);
  if (it == device_info_list_.end()) {
    device_info_list_.push_back(device_info);
    return true;
  }
  return false;
}

bool PipeWireSession::DeRegisterDeviceInfo(DeviceInfoPipeWire* device_info) {
  RTC_CHECK(device_info);
  MutexLock lock(&device_info_lock_);
  auto it = std::find(device_info_list_.begin(), device_info_list_.end(),
                      device_info);
  if (it != device_info_list_.end()) {
    device_info_list_.erase(it);
    return true;
  }
  return false;
}

RTC_NO_SANITIZE("cfi-icall")
bool PipeWireSession::StartPipeWire(int fd) {
  pw_initializer_ = std::make_unique<PipeWireInitializer>();

  pw_main_loop_ = pw_thread_loop_new("pipewire-main-loop", nullptr);

  pw_context_ =
      pw_context_new(pw_thread_loop_get_loop(pw_main_loop_), nullptr, 0);
  if (!pw_context_) {
    RTC_LOG(LS_ERROR) << "Failed to create PipeWire context";
    return false;
  }

  pw_core_ = pw_context_connect_fd(pw_context_, fd, nullptr, 0);
  if (!pw_core_) {
    RTC_LOG(LS_ERROR) << "Failed to connect PipeWire context";
    return false;
  }

  static const pw_core_events core_events{
      .version = PW_VERSION_CORE_EVENTS,
      .done = &OnCoreDone,
      .error = &OnCoreError,
  };

  pw_core_add_listener(pw_core_, &core_listener_, &core_events, this);

  static const pw_registry_events registry_events{
      .version = PW_VERSION_REGISTRY_EVENTS,
      .global = OnRegistryGlobal,
      .global_remove = OnRegistryGlobalRemove,
  };

  pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(pw_registry_, &registry_listener_, &registry_events,
                           this);

  PipeWireSync();

  if (pw_thread_loop_start(pw_main_loop_) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to start main PipeWire loop";
    return false;
  }

  return true;
}

void PipeWireSession::StopPipeWire() {
  if (pw_main_loop_)
    pw_thread_loop_stop(pw_main_loop_);

  if (pw_core_) {
    pw_core_disconnect(pw_core_);
    pw_core_ = nullptr;
  }

  if (pw_context_) {
    pw_context_destroy(pw_context_);
    pw_context_ = nullptr;
  }

  if (pw_main_loop_) {
    pw_thread_loop_destroy(pw_main_loop_);
    pw_main_loop_ = nullptr;
  }
}

RTC_NO_SANITIZE("cfi-icall")
void PipeWireSession::PipeWireSync() {
  sync_seq_ = pw_core_sync(pw_core_, PW_ID_CORE, sync_seq_);
}

void PipeWireSession::NotifyDeviceChange() {
  RTC_LOG(LS_INFO) << "Notify about device list changes";
  MutexLock lock(&device_info_lock_);

  // It makes sense to notify about device changes only once we are
  // properly initialized.
  if (status_ != VideoCaptureOptions::Status::SUCCESS) {
    return;
  }

  for (auto* deviceInfo : device_info_list_) {
    deviceInfo->DeviceChange();
  }
}

// static
void PipeWireSession::OnCoreError(void* data,
                                  uint32_t id,
                                  int seq,
                                  int res,
                                  const char* message) {
  RTC_LOG(LS_ERROR) << "PipeWire remote error: " << message;
}

// static
void PipeWireSession::OnCoreDone(void* data, uint32_t id, int seq) {
  PipeWireSession* that = static_cast<PipeWireSession*>(data);

  if (id == PW_ID_CORE) {
    if (seq == that->sync_seq_) {
      RTC_LOG(LS_VERBOSE) << "Enumerating PipeWire camera devices complete.";

      // Remove camera devices with no capabilities
      std::erase_if(that->nodes_,
                    [](const PipeWireNode::PipeWireNodePtr& node) {
                      return node->capabilities().empty();
                    });

      that->Finish(VideoCaptureOptions::Status::SUCCESS);
    }
  }
}

// static
RTC_NO_SANITIZE("cfi-icall")
void PipeWireSession::OnRegistryGlobal(void* data,
                                       uint32_t id,
                                       uint32_t permissions,
                                       const char* type,
                                       uint32_t version,
                                       const spa_dict* props) {
  PipeWireSession* that = static_cast<PipeWireSession*>(data);

  // Skip already added nodes to avoid duplicate camera entries
  if (std::find_if(that->nodes_.begin(), that->nodes_.end(),
                   [id](const PipeWireNode::PipeWireNodePtr& node) {
                     return node->id() == id;
                   }) != that->nodes_.end())
    return;

  if (type != absl::string_view(PW_TYPE_INTERFACE_Node))
    return;

  if (!spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION))
    return;

  auto node_role = spa_dict_lookup(props, PW_KEY_MEDIA_ROLE);
  if (!node_role || strcmp(node_role, "Camera"))
    return;

  that->nodes_.push_back(PipeWireNode::Create(that, id, props));
  that->PipeWireSync();

  that->NotifyDeviceChange();
}

// static
void PipeWireSession::OnRegistryGlobalRemove(void* data, uint32_t id) {
  PipeWireSession* that = static_cast<PipeWireSession*>(data);

  std::erase_if(that->nodes_, [id](const PipeWireNode::PipeWireNodePtr& node) {
    return node->id() == id;
  });

  that->NotifyDeviceChange();
}

void PipeWireSession::Finish(VideoCaptureOptions::Status status) {
  {
    MutexLock lock(&device_info_lock_);
    status_ = status;
  }

  webrtc::MutexLock lock(&callback_lock_);

  if (callback_) {
    callback_->OnInitialized(status);
    callback_ = nullptr;
  }
}

void PipeWireSession::Cleanup() {
  webrtc::MutexLock lock(&callback_lock_);
  callback_ = nullptr;

  StopPipeWire();
}

}  // namespace videocapturemodule
}  // namespace webrtc
