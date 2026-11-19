/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_SFRAME_SFRAME_DECRYPTOR_HANDLE_H_
#define MODULES_SFRAME_SFRAME_DECRYPTOR_HANDLE_H_

#include <cstdint>
#include <span>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/sframe/sframe_types.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"

namespace webrtc {

// Implements the public key management API on top of a decryptor that is used
// on a single sequence. Results are reported on `callback_queue`.
// `callback_queue` and `decryptor_queue` must outlive instances of this class,
// and `callback_queue` must also outlive tasks pending on `decryptor_queue`.
class SframeDecryptorHandle : public SframeDecryptorInterface {
 public:
  static absl_nonnull scoped_refptr<SframeDecryptorHandle> Create(
      TaskQueueBase& callback_queue,
      TaskQueueBase& decryptor_queue,
      absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor);

  // Must be destroyed on `callback_queue`. Key updates that are still pending
  // are applied, but their callbacks are dropped.
  ~SframeDecryptorHandle() override;

  // SframeDecryptorInterface implementation.
  void AddDecryptionKey(uint64_t key_id,
                        std::span<const uint8_t> key_material,
                        absl_nonnull SframeKeyUpdateCallback callback) override;
  void RemoveDecryptionKey(uint64_t key_id,
                           absl_nonnull SframeKeyUpdateCallback
                               callback) override;

 protected:
  SframeDecryptorHandle(
      TaskQueueBase& callback_queue,
      TaskQueueBase& decryptor_queue,
      absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor);

 private:
  using KeyUpdate =
      absl::AnyInvocable<RTCError(SframeMediaDecryptorInterface&) &&>;

  // Runs `update` on `decryptor_queue_`, then `callback` on `callback_queue_`.
  void PostKeyUpdate(KeyUpdate update, SframeKeyUpdateCallback callback);

  TaskQueueBase& callback_queue_;
  TaskQueueBase& decryptor_queue_;

  // Alive until the handle is destroyed; no callback runs after that.
  const absl_nonnull scoped_refptr<PendingTaskSafetyFlag> callback_safety_;

  // Used only on `decryptor_queue_`.
  const absl_nonnull scoped_refptr<SframeMediaDecryptorInterface> decryptor_;
};

}  // namespace webrtc

#endif  // MODULES_SFRAME_SFRAME_DECRYPTOR_HANDLE_H_
