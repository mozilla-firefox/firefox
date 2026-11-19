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

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "api/task_queue/task_queue_base.h"
#include "modules/sframe/mock_sframe_media_decryptor.h"
#include "rtc_base/checks.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::InSequence;
using ::testing::Return;

constexpr uint64_t kKeyId = 7;

// Returns a callback that stores the result and stops `run_loop`.
SframeKeyUpdateCallback StoreResult(test::RunLoop& run_loop, RTCError& result) {
  return [&run_loop, &result](RTCError error) {
    EXPECT_EQ(TaskQueueBase::Current(), run_loop.task_queue());
    result = std::move(error);
    run_loop.Quit();
  };
}

TEST(SframeDecryptorHandleTest, AddsKeyOnDecryptorQueue) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  const std::vector<uint8_t> key_material(16, 0xab);
  EXPECT_CALL(*decryptor,
              AddDecryptionKey(kKeyId, ElementsAreArray(key_material)))
      .WillOnce([&] {
        EXPECT_TRUE(decryptor_queue.IsCurrent());
        return RTCError::OK();
      });

  RTCError result = RTCError::InternalError("Not called");
  handle->AddDecryptionKey(kKeyId, key_material, StoreResult(run_loop, result));
  run_loop.Run();

  EXPECT_TRUE(result.ok());
}

TEST(SframeDecryptorHandleTest, RemovesKeyOnDecryptorQueue) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  EXPECT_CALL(*decryptor, RemoveDecryptionKey(kKeyId)).WillOnce([&] {
    EXPECT_TRUE(decryptor_queue.IsCurrent());
    return RTCError::OK();
  });

  RTCError result = RTCError::InternalError("Not called");
  handle->RemoveDecryptionKey(kKeyId, StoreResult(run_loop, result));
  run_loop.Run();

  EXPECT_TRUE(result.ok());
}

TEST(SframeDecryptorHandleTest, ReportsKeyUpdateError) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  EXPECT_CALL(*decryptor, AddDecryptionKey)
      .WillOnce(Return(RTCError::InternalError("Bad key")));

  RTCError result = RTCError::OK();
  handle->AddDecryptionKey(kKeyId, std::vector<uint8_t>(16, 0xab),
                           StoreResult(run_loop, result));
  run_loop.Run();

  EXPECT_EQ(result.type(), RTCErrorType::INTERNAL_ERROR);
}

TEST(SframeDecryptorHandleTest, KeepsCopyOfKeyMaterial) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  const std::vector<uint8_t> expected_key(16, 0xab);
  EXPECT_CALL(*decryptor,
              AddDecryptionKey(kKeyId, ElementsAreArray(expected_key)))
      .WillOnce(Return(RTCError::OK()));

  RTCError result = RTCError::InternalError("Not called");
  {
    std::vector<uint8_t> key_material = expected_key;
    handle->AddDecryptionKey(kKeyId, key_material,
                             StoreResult(run_loop, result));
    key_material.assign(key_material.size(), 0);
  }
  run_loop.Run();

  EXPECT_TRUE(result.ok());
}

TEST(SframeDecryptorHandleTest, AppliesKeyUpdatesInOrder) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  InSequence s;
  EXPECT_CALL(*decryptor, AddDecryptionKey(kKeyId, _))
      .WillOnce(Return(RTCError::OK()));
  EXPECT_CALL(*decryptor, RemoveDecryptionKey(kKeyId))
      .WillOnce(Return(RTCError::OK()));

  RTCError add_result = RTCError::InternalError("Not called");
  handle->AddDecryptionKey(
      kKeyId, std::vector<uint8_t>(16, 0xab),
      [&add_result](RTCError error) { add_result = std::move(error); });
  RTCError remove_result = RTCError::InternalError("Not called");
  handle->RemoveDecryptionKey(kKeyId, StoreResult(run_loop, remove_result));
  run_loop.Run();

  EXPECT_TRUE(add_result.ok());
  EXPECT_TRUE(remove_result.ok());
}

TEST(SframeDecryptorHandleTest,
     AppliesKeyUpdateButDropsCallbackAfterHandleIsReleased) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  EXPECT_CALL(*decryptor, RemoveDecryptionKey(kKeyId))
      .WillOnce(Return(RTCError::OK()));

  bool callback_called = false;
  handle->RemoveDecryptionKey(
      kKeyId, [&callback_called](RTCError) { callback_called = true; });
  handle = nullptr;
  // Wait for the key update, then run the task it posted to the callback queue.
  decryptor_queue.WaitForPreviouslyPostedTasks();
  run_loop.Flush();

  EXPECT_FALSE(callback_called);
}

TEST(SframeDecryptorHandleTest, ReportsResultOnCallbackQueue) {
  test::RunLoop run_loop;
  TaskQueueForTest decryptor_queue;
  scoped_refptr<MockSframeMediaDecryptor> decryptor =
      make_ref_counted<MockSframeMediaDecryptor>();
  scoped_refptr<SframeDecryptorHandle> handle = SframeDecryptorHandle::Create(
      *run_loop.task_queue(), *decryptor_queue.Get(), decryptor);
  EXPECT_CALL(*decryptor, RemoveDecryptionKey(kKeyId))
      .WillOnce(Return(RTCError::OK()));

  RTCError result = RTCError::InternalError("Not called");
  TaskQueueForTest caller_queue("Caller");
  caller_queue.SendTask([&] {
    handle->RemoveDecryptionKey(kKeyId, StoreResult(run_loop, result));
  });
  run_loop.Run();

  EXPECT_TRUE(result.ok());
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST(SframeDecryptorHandleDeathTest, ReleasingOffCallbackQueueDies) {
  // The task queues are created inside EXPECT_DEATH, so that they exist in the
  // child process that runs the statement.
  EXPECT_DEATH(
      {
        test::RunLoop run_loop;
        TaskQueueForTest decryptor_queue;
        TaskQueueForTest other_queue("Other");
        scoped_refptr<SframeDecryptorHandle> handle =
            SframeDecryptorHandle::Create(
                *run_loop.task_queue(), *decryptor_queue.Get(),
                make_ref_counted<MockSframeMediaDecryptor>());
        other_queue.SendTask([&] { handle = nullptr; });
      },
      "");
}
#endif

}  // namespace
}  // namespace webrtc
