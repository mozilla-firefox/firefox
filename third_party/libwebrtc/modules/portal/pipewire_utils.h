/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_PORTAL_PIPEWIRE_UTILS_H_
#define MODULES_PORTAL_PIPEWIRE_UTILS_H_

#include <asm-generic/ioctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// static
struct dma_buf_sync {
  uint64_t flags;
};
#define DMA_BUF_SYNC_READ (1 << 0)
#define DMA_BUF_SYNC_START (0 << 2)
#define DMA_BUF_SYNC_END (1 << 2)
#define DMA_BUF_BASE 'b'
#define DMA_BUF_IOCTL_SYNC _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

struct pw_thread_loop;
struct spa_buffer;

namespace webrtc {

constexpr int kInvalidPipeWireFd = -1;

struct PipeWireVersion {
  static PipeWireVersion Parse(const std::string_view& version);

  // Returns whether current version is newer or same as required version
  bool operator>=(const PipeWireVersion& other);

  std::string_view ToStringView() const;

  int major = 0;
  int minor = 0;
  int micro = 0;
  std::string full_version;
};

// Prepare PipeWire so that it is ready to be used. If it needs to be dlopen'd
// this will do so. Note that this does not guarantee a PipeWire server is
// running nor does it establish a connection to one.
bool InitializePipeWire();

// Locks pw_thread_loop in the current scope
class PipeWireThreadLoopLock {
 public:
  explicit PipeWireThreadLoopLock(pw_thread_loop* loop);
  ~PipeWireThreadLoopLock();

 private:
  pw_thread_loop* const loop_;
};

// RAII wrapper for PipeWire initialization/deinitialization
class PipeWireInitializer {
 public:
  PipeWireInitializer();
  ~PipeWireInitializer();

  // Non-copyable
  PipeWireInitializer(const PipeWireInitializer&) = delete;
  PipeWireInitializer& operator=(const PipeWireInitializer&) = delete;
};

// Returns whether the first data plane of `buffer` has a chunk that is not
// flagged as corrupted. The chunk size is left to the caller: a zero-sized
// chunk is empty when the data is read through a mapping, but not when a
// DMA-BUF is imported from its plane descriptors.
bool PipeWireBufferHasUncorruptedChunk(const spa_buffer& buffer);

// Returns whether `buffer` has an uncorrupted first chunk with a nonzero size,
// for callers that use the chunk size as the frame length whatever the memory
// type. A nonempty chunk flagged SPA_CHUNK_FLAG_EMPTY can still hold valid
// neutral image data.
bool PipeWireBufferHasFrameData(const spa_buffer& buffer);

// We should synchronize DMA Buffer object access from CPU to avoid potential
// cache incoherency and data loss.
// See
// https://01.org/linuxgraphics/gfx-docs/drm/driver-api/dma-buf.html#cpu-access-to-dma-buffer-objects
static bool SyncDmaBuf(int fd, uint64_t start_or_end) {
  struct dma_buf_sync sync = {0};

  sync.flags = start_or_end | DMA_BUF_SYNC_READ;

  while (true) {
    int ret;
    ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    if (ret == -1 && errno == EINTR) {
      continue;
    } else if (ret == -1) {
      return false;
    } else {
      break;
    }
  }

  return true;
}

class ScopedBuf {
 public:
  enum class AccessMode { kReadOnly, kReadWrite };
  enum class BufferType { kMemFd, kDmaBuf };

  ScopedBuf() {}
  ScopedBuf(const ScopedBuf&) = delete;
  ScopedBuf& operator=(const ScopedBuf&) = delete;
  ScopedBuf(ScopedBuf&&) = delete;
  ScopedBuf& operator=(ScopedBuf&&) = delete;
  ~ScopedBuf() {
    if (map_ != MAP_FAILED) {
      if (buffer_type_ == BufferType::kDmaBuf) {
        SyncDmaBuf(fd_, DMA_BUF_SYNC_END);
      }
      munmap(map_, map_size_);
    }
  }

  explicit operator bool() { return map_ != MAP_FAILED; }

  void initialize(int fd,
                  size_t maxsize,
                  off_t mapoffset,
                  BufferType buffer_type,
                  AccessMode mode = AccessMode::kReadOnly);

  uint8_t* get() { return map_; }

 protected:
  uint8_t* map_ = static_cast<uint8_t*>(MAP_FAILED);
  size_t map_size_ = 0;
  int fd_ = -1;
  BufferType buffer_type_ = BufferType::kMemFd;
};

}  // namespace webrtc

#endif  // MODULES_PORTAL_PIPEWIRE_UTILS_H_
