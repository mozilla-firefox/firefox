/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/portal/pipewire_utils.h"

#include <pipewire/pipewire.h>
#include <spa/buffer/buffer.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "rtc_base/checks.h"
#include "rtc_base/sanitizer.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/string_to_number.h"

#if defined(WEBRTC_DLOPEN_PIPEWIRE)
#include "modules/portal/pipewire_stubs.h"
#endif  // defined(WEBRTC_DLOPEN_PIPEWIRE)

namespace webrtc {

#if defined(__GNUC__) && defined(WEBRTC_MOZILLA_BUILD)
// Mozilla linux builds are using -std=gnu++20, which doesn't approve
// of using std::string in a constexpr.  PipewireVersion now contains
// std::string as of libwebrtc commit d167fe9296.
static const PipeWireVersion kReentrantDeinitMinVersion = {.major = 0,
                                                           .minor = 3,
                                                           .micro = 49};
#else
constexpr PipeWireVersion kReentrantDeinitMinVersion = {.major = 0,
                                                        .minor = 3,
                                                        .micro = 49};
#endif

PipeWireVersion PipeWireVersion::Parse(const std::string_view& version) {
  std::vector<std::string_view> parsed_version = split(version, '.');

  if (parsed_version.size() != 3) {
    return {};
  }

  std::optional<int> major = StringToNumber<int>(parsed_version.at(0));
  std::optional<int> minor = StringToNumber<int>(parsed_version.at(1));
  std::optional<int> micro = StringToNumber<int>(parsed_version.at(2));

  // Return invalid version if we failed to parse it
  if (!major || !minor || !micro) {
    return {};
  }

  return {.major = major.value(),
          .minor = minor.value(),
          .micro = micro.value(),
          .full_version = std::string(version)};
}

bool PipeWireVersion::operator>=(const PipeWireVersion& other) {
  if (!major && !minor && !micro) {
    return false;
  }

  return std::tie(major, minor, micro) >=
         std::tie(other.major, other.minor, other.micro);
}

std::string_view PipeWireVersion::ToStringView() const {
  return full_version;
}

RTC_NO_SANITIZE("cfi-icall")
bool InitializePipeWire() {
#if defined(WEBRTC_DLOPEN_PIPEWIRE)
  static constexpr char kPipeWireLib[] = "libpipewire-0.3.so.0";

  using modules_portal::InitializeStubs;
  using modules_portal::kModulePipewire;

  modules_portal::StubPathMap paths;

  // Check if the PipeWire library is available.
  paths[kModulePipewire].push_back(kPipeWireLib);

  static bool result = InitializeStubs(paths);

  return result;
#else
  return true;
#endif  // defined(WEBRTC_DLOPEN_PIPEWIRE)
}

PipeWireThreadLoopLock::PipeWireThreadLoopLock(pw_thread_loop* loop)
    : loop_(loop) {
  pw_thread_loop_lock(loop_);
}

PipeWireThreadLoopLock::~PipeWireThreadLoopLock() {
  pw_thread_loop_unlock(loop_);
}

RTC_NO_SANITIZE("cfi-icall")
PipeWireInitializer::PipeWireInitializer() {
  pw_init(/*argc=*/nullptr, /*argv=*/nullptr);
}

RTC_NO_SANITIZE("cfi-icall")
PipeWireInitializer::~PipeWireInitializer() {
  PipeWireVersion pw_client_version =
      PipeWireVersion::Parse(pw_get_library_version());
  if (pw_client_version >= kReentrantDeinitMinVersion) {
    pw_deinit();
  }
}

bool PipeWireBufferHasUncorruptedChunk(const spa_buffer& buffer) {
  return buffer.n_datas != 0 && buffer.datas != nullptr &&
         buffer.datas[0].chunk != nullptr &&
         (buffer.datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) == 0;
}

bool PipeWireBufferHasFrameData(const spa_buffer& buffer) {
  return PipeWireBufferHasUncorruptedChunk(buffer) &&
         buffer.datas[0].chunk->size != 0;
}

void ScopedBuf::initialize(int fd,
                           size_t maxsize,
                           off_t mapoffset,
                           BufferType buffer_type,
                           AccessMode mode) {
  // PipeWire supports shared memory mappings <= 4GB in size.
  RTC_CHECK_GE(mapoffset, 0);
  RTC_CHECK_LE(mapoffset, UINT32_MAX);
  RTC_CHECK_LE(maxsize, UINT32_MAX);

  if (buffer_type == BufferType::kMemFd) {
    struct stat sb;
    if (fstat(fd, &sb) != 0 ||
        static_cast<int64_t>(mapoffset) + static_cast<int64_t>(maxsize) >
            static_cast<int64_t>(sb.st_size)) {
      return;
    }
  }

  int prot =
      mode == AccessMode::kReadWrite ? (PROT_READ | PROT_WRITE) : PROT_READ;
  int flags =
      (buffer_type == BufferType::kDmaBuf || mode == AccessMode::kReadWrite)
          ? MAP_SHARED
          : MAP_PRIVATE;

  map_ =
      static_cast<uint8_t*>(mmap(nullptr, maxsize, prot, flags, fd, mapoffset));
  map_size_ = maxsize;
  fd_ = fd;
  buffer_type_ = buffer_type;

  if (buffer_type_ == BufferType::kDmaBuf && map_ != MAP_FAILED) {
    SyncDmaBuf(fd_, DMA_BUF_SYNC_START);
  }
}

}  // namespace webrtc
