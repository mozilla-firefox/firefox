/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/device_info_impl.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/video_capture/video_capture_defines.h"
#include "rtc_base/thread_annotations.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

class DeviceInfoImplForTest final : public videocapturemodule::DeviceInfoImpl {
 public:
  explicit DeviceInfoImplForTest(VideoCaptureCapabilities capabilities)
      : capabilities_(std::move(capabilities)) {}

  uint32_t NumberOfDevices() override { return 1; }

  int32_t GetDeviceName(uint32_t deviceNumber,
                        char* deviceNameUTF8,
                        uint32_t deviceNameLength,
                        char* deviceUniqueIdUTF8,
                        uint32_t deviceUniqueIdUTF8Length,
                        char* /* productUniqueIdUTF8 */,
                        uint32_t /* productUniqueIdUTF8Length */) override {
    if (deviceNumber != 0)
      return -1;
    const char kName[] = "Fake Device";
    const char kId[] = "fake";
    if (deviceNameLength < sizeof(kName) ||
        deviceUniqueIdUTF8Length < sizeof(kId)) {
      return -1;
    }
    snprintf(deviceNameUTF8, deviceNameLength, "%s", kName);
    snprintf(deviceUniqueIdUTF8, deviceUniqueIdUTF8Length, "%s", kId);
    return 0;
  }

  int32_t DisplayCaptureSettingsDialogBox(const char* /* deviceUniqueIdUTF8 */,
                                          const char* /* dialogTitle */,
                                          void* /* parentWindow */,
                                          uint32_t /* positionX */,
                                          uint32_t /* positionY */) override {
    return -1;
  }

 protected:
  int32_t Init() override { return 0; }

  int32_t CreateCapabilityMap(const char* deviceUniqueIdUTF8) override
      RTC_EXCLUSIVE_LOCKS_REQUIRED(_apiLock) {
    _captureCapabilities = capabilities_;
    UpdateDeviceName(deviceUniqueIdUTF8);
    return static_cast<int32_t>(_captureCapabilities.size());
  }

 private:
  void UpdateDeviceName(const char* deviceUniqueIdUTF8)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(_apiLock) {
    free(_lastUsedDeviceName);
    _lastUsedDeviceNameLength =
        static_cast<uint32_t>(strlen(deviceUniqueIdUTF8));
    _lastUsedDeviceName =
        static_cast<char*>(malloc(_lastUsedDeviceNameLength + 1));
    memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8,
           _lastUsedDeviceNameLength + 1);
  }

  VideoCaptureCapabilities capabilities_;
};

TEST(DeviceInfoImplTest, PrefersRequestedFormatOverSameFpsExactMatch) {
  VideoCaptureCapability requested;
  requested.width = 640;
  requested.height = 480;
  requested.maxFPS = 30;
  requested.videoType = VideoType::kI420;

  VideoCaptureCapability cap0;
  cap0.width = 640;
  cap0.height = 480;
  cap0.maxFPS = 60;
  cap0.videoType = VideoType::kYUY2;

  VideoCaptureCapability cap1;
  cap1.width = 640;
  cap1.height = 480;
  cap1.maxFPS = 30;
  cap1.videoType = VideoType::kI420;

  VideoCaptureCapability cap2;
  cap2.width = 640;
  cap2.height = 480;
  cap2.maxFPS = 30;
  cap2.videoType = VideoType::kNV12;

  DeviceInfoImplForTest device_info({cap0, cap1, cap2});
  VideoCaptureCapability resulting;

  // Keep the preferred format when a later capability has the same size/fps.
  // Without this, a later same-fps match can override the preferred format.
  int32_t index =
      device_info.GetBestMatchedCapability("fake", requested, resulting);

  EXPECT_EQ(index, 1);
  EXPECT_EQ(resulting.width, requested.width);
  EXPECT_EQ(resulting.height, requested.height);
  EXPECT_EQ(resulting.maxFPS, requested.maxFPS);
  EXPECT_EQ(resulting.videoType, VideoType::kI420);
}

}  // namespace
}  // namespace webrtc
