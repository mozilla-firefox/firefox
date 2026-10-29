/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/sframe/synchronized_sframe_encryptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <variant>
#include <vector>

#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_decryptor.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"
#include "rtc_base/event.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAreArray;

constexpr uint64_t kKeyId = 7;
constexpr std::array<uint8_t, 16> kKeyMaterial = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr std::array<uint8_t, 10> kPlaintext = {0xde, 0xad, 0xbe, 0xef, 0x01,
                                                0x02, 0x03, 0x04, 0x05, 0x06};

TEST(SynchronizedSframeEncryptorTest, ForwardsEncryptionErrors) {
  scoped_refptr<SynchronizedSframeEncryptor> controller =
      SynchronizedSframeEncryptor::Create(
          SframeCipherSuite::kAes128GcmSha256_128);
  std::vector<uint8_t> ciphertext(
      controller->GetMaxCiphertextByteSize(kPlaintext.size()));
  RTCErrorOr<size_t> no_key = controller->Encrypt(kPlaintext, {}, ciphertext);
  ASSERT_FALSE(no_key.ok());
  EXPECT_EQ(no_key.error().type(), RTCErrorType::INVALID_STATE);
  ASSERT_TRUE(controller->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  RTCErrorOr<size_t> short_buffer = controller->Encrypt(kPlaintext, {}, {});
  ASSERT_FALSE(short_buffer.ok());
  EXPECT_EQ(short_buffer.error().type(), RTCErrorType::INTERNAL_ERROR);
  EXPECT_TRUE(controller->Encrypt(kPlaintext, {}, ciphertext).ok());
}

TEST(SynchronizedSframeEncryptorTest, ConcurrentEncryptionAndKeyRotation) {
  constexpr size_t kFramesPerStream = 512;
  constexpr size_t kRotations = 8;
  scoped_refptr<SynchronizedSframeEncryptor> controller =
      SynchronizedSframeEncryptor::Create(
          SframeCipherSuite::kAes128GcmSha256_128);
  scoped_refptr<SframeDecryptor> receiver =
      SframeDecryptor::Create(SframeCipherSuite::kAes128GcmSha256_128);
  std::vector<std::vector<uint8_t>> keys;
  for (size_t index = 0; index <= kRotations; ++index) {
    keys.emplace_back(16, static_cast<uint8_t>(index));
    ASSERT_TRUE(receiver->AddDecryptionKey(kKeyId + index, keys.back()).ok());
  }
  ASSERT_TRUE(controller->SetEncryptionKey(kKeyId, keys.front()).ok());
  constexpr std::array<uint8_t, 3> additional_data = {0xaa, 0xbb, 0xcc};
  std::vector<std::vector<uint8_t>> first_stream;
  std::vector<std::vector<uint8_t>> second_stream;
  Event start(true, false);
  Event first_ready;
  Event second_ready;
  Event keys_ready;
  TaskQueueForTest first_queue("FirstStream");
  TaskQueueForTest second_queue("SecondStream");
  TaskQueueForTest key_queue("KeyUpdates");
  auto encrypt_stream = [&](Event& ready,
                            std::vector<std::vector<uint8_t>>& ciphertexts) {
    ready.Set();
    ASSERT_TRUE(start.Wait(Event::kForever));
    for (size_t index = 0; index < kFramesPerStream; ++index) {
      ciphertexts.emplace_back(
          controller->GetMaxCiphertextByteSize(kPlaintext.size()));
      RTCErrorOr<size_t> result =
          controller->Encrypt(kPlaintext, additional_data, ciphertexts.back());
      ASSERT_TRUE(result.ok());
      ciphertexts.back().resize(result.value());
    }
  };
  first_queue.PostTask([&encrypt_stream, &first_ready, &first_stream] {
    encrypt_stream(first_ready, first_stream);
  });
  second_queue.PostTask([&encrypt_stream, &second_ready, &second_stream] {
    encrypt_stream(second_ready, second_stream);
  });
  key_queue.PostTask([&keys_ready, &start, &controller, &keys] {
    keys_ready.Set();
    ASSERT_TRUE(start.Wait(Event::kForever));
    for (size_t index = 1; index <= kRotations; ++index) {
      EXPECT_TRUE(
          controller->SetEncryptionKey(kKeyId + index, keys[index]).ok());
    }
  });
  EXPECT_TRUE(first_ready.Wait(Event::kForever));
  EXPECT_TRUE(second_ready.Wait(Event::kForever));
  EXPECT_TRUE(keys_ready.Wait(Event::kForever));
  start.Set();
  first_queue.WaitForPreviouslyPostedTasks();
  second_queue.WaitForPreviouslyPostedTasks();
  key_queue.WaitForPreviouslyPostedTasks();

  ASSERT_EQ(first_stream.size(), kFramesPerStream);
  ASSERT_EQ(second_stream.size(), kFramesPerStream);
  std::set<std::vector<uint8_t>> unique_ciphertexts;
  for (const std::vector<std::vector<uint8_t>>* stream :
       {&first_stream, &second_stream}) {
    for (const std::vector<uint8_t>& ciphertext : *stream) {
      EXPECT_TRUE(unique_ciphertexts.insert(ciphertext).second);
      std::vector<uint8_t> plaintext(kPlaintext.size());
      SframeDecryptResult result =
          receiver->Decrypt(ciphertext, additional_data, plaintext);
      SframeDecryptSuccess* success =
          std::get_if<SframeDecryptSuccess>(&result);
      ASSERT_NE(success, nullptr);
      EXPECT_EQ(success->bytes_written, kPlaintext.size());
      EXPECT_THAT(plaintext, ElementsAreArray(kPlaintext));
    }
  }
  EXPECT_EQ(unique_ciphertexts.size(), 2 * kFramesPerStream);
}

}  // namespace
}  // namespace webrtc
