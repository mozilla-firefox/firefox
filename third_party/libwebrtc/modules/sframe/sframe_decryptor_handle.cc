/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/sframe_decryptor_handle.h"

#include <cstdint>
#include <span>
#include <utility>

#include "absl/base/nullability.h"
#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_types.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"

namespace webrtc {

scoped_refptr<SframeDecryptorHandle> SframeDecryptorHandle::Create(
    TaskQueueBase& callback_queue,
    TaskQueueBase& decryptor_queue,
    absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor) {
  return make_ref_counted<SframeDecryptorHandle>(
      callback_queue, decryptor_queue, std::move(decryptor));
}

SframeDecryptorHandle::SframeDecryptorHandle(
    TaskQueueBase& callback_queue,
    TaskQueueBase& decryptor_queue,
    absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor)
    : callback_queue_(callback_queue),
      decryptor_queue_(decryptor_queue),
      callback_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(/*alive=*/true,
                                                           &callback_queue)),
      decryptor_(std::move(decryptor)) {
  RTC_DCHECK(decryptor_);
}

SframeDecryptorHandle::~SframeDecryptorHandle() {
  RTC_DCHECK_RUN_ON(&callback_queue_);
  callback_safety_->SetNotAlive();
}

void SframeDecryptorHandle::AddDecryptionKey(
    uint64_t key_id,
    std::span<const uint8_t> key_material,
    absl_nonnull SframeKeyUpdateCallback callback) {
  // `key_material` may not outlive this call, so the update keeps its own
  // copy, which is wiped when the update is destroyed.
  ZeroOnFreeBuffer<uint8_t> key(key_material.data(), key_material.size());
  PostKeyUpdate(
      [key_id, key = std::move(key)](SframeMediaDecryptorInterface& decryptor) {
        return decryptor.AddDecryptionKey(key_id, key);
      },
      std::move(callback));
}

void SframeDecryptorHandle::RemoveDecryptionKey(
    uint64_t key_id,
    absl_nonnull SframeKeyUpdateCallback callback) {
  PostKeyUpdate(
      [key_id](SframeMediaDecryptorInterface& decryptor) {
        return decryptor.RemoveDecryptionKey(key_id);
      },
      std::move(callback));
}

void SframeDecryptorHandle::PostKeyUpdate(KeyUpdate update,
                                          SframeKeyUpdateCallback callback) {
  RTC_DCHECK(callback);
  // Always post, even when already on `decryptor_queue_`, so that updates are
  // applied in the order they were requested. The tasks hold their own
  // reference to the decryptor, so the handle may go away in the meantime.
  decryptor_queue_.PostTask(
      [decryptor = decryptor_, callback_queue = &callback_queue_,
       callback_safety = callback_safety_, update = std::move(update),
       callback = std::move(callback)]() mutable {
        RTCError error = std::move(update)(*decryptor);

        callback_queue->PostTask(SafeTask(
            std::move(callback_safety), [callback = std::move(callback),
                                         error = std::move(error)]() mutable {
              std::move(callback)(std::move(error));
            }));
      });
}

}  // namespace webrtc
