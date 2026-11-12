/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 * Test application for core FEC algorithm. Calls encoding and decoding
 * functions in ForwardErrorCorrection directly.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "modules/include/module_fec_types.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/forward_error_correction.h"
#include "modules/rtp_rtcp/source/forward_error_correction_internal.h"
#include "rtc_base/checks.h"
#include "rtc_base/random.h"
#include "test/gtest.h"

// #define VERBOSE_OUTPUT

namespace webrtc {
namespace fec_private_tables {
extern const uint8_t** kPacketMaskBurstyTbl[12];
}  // namespace fec_private_tables
namespace test {
using fec_private_tables::kPacketMaskBurstyTbl;

void ReceivePackets(
    std::vector<std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>>*
        to_decode_list,
    std::vector<std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>>*
        received_packet_list,
    size_t num_packets_to_decode,
    float reorder_rate,
    float duplicate_rate,
    Random* random) {
  RTC_DCHECK(to_decode_list->empty());
  RTC_DCHECK_LE(num_packets_to_decode, received_packet_list->size());

  for (size_t i = 0; i < num_packets_to_decode; i++) {
    auto it = received_packet_list->begin();
    // Reorder packets.
    float random_variable = random->Rand<float>();
    while (random_variable < reorder_rate) {
      ++it;
      if (it == received_packet_list->end()) {
        --it;
        break;
      }
      random_variable = random->Rand<float>();
    }
    to_decode_list->push_back(std::move(*it));
    received_packet_list->erase(it);

    // Duplicate packets.
    ForwardErrorCorrection::ReceivedPacket* received_packet =
        to_decode_list->back().get();
    random_variable = random->Rand<float>();
    while (random_variable < duplicate_rate) {
      std::unique_ptr<ForwardErrorCorrection::ReceivedPacket> duplicate_packet(
          new ForwardErrorCorrection::ReceivedPacket());
      *duplicate_packet = *received_packet;
      duplicate_packet->pkt = new ForwardErrorCorrection::Packet();
      duplicate_packet->pkt->data = received_packet->pkt->data;

      to_decode_list->push_back(std::move(duplicate_packet));
      random_variable = random->Rand<float>();
    }
  }
}

void RunTest(bool use_flexfec) {
  // TODO(marpan): Split this function into subroutines/helper functions.
  enum { kMaxNumberMediaPackets = 48 };
  enum { kMaxNumberFecPackets = 48 };

  const uint32_t kNumMaskBytesL0 = 2;
  const uint32_t kNumMaskBytesL1 = 6;

  // FOR UEP
  const bool kUseUnequalProtection = true;

  // FEC mask types.
  const FecMaskType kMaskTypes[] = {kFecMaskRandom, kFecMaskBursty};
  const int kNumFecMaskTypes = sizeof(kMaskTypes) / sizeof(*kMaskTypes);

  // Maximum number of media packets allowed for the mask type.
  const uint16_t kMaxMediaPackets[] = {
      kMaxNumberMediaPackets,
      sizeof(kPacketMaskBurstyTbl) / sizeof(*kPacketMaskBurstyTbl)};

  ASSERT_EQ(12, kMaxMediaPackets[1]) << "Max media packets for bursty mode not "
                                        "equal to 12.";

  ForwardErrorCorrection::PacketList media_packet_list;
  std::list<ForwardErrorCorrection::Packet*> fec_packet_list;
  std::vector<std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>>
      to_decode_list;
  std::vector<std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>>
      received_packet_list;
  ForwardErrorCorrection::RecoveredPacketList recovered_packet_list;
  std::list<uint8_t*> fec_mask_list;

  // Running over only two loss rates to limit execution time.
  const float loss_rate[] = {0.05f, 0.01f};
  const uint32_t loss_rate_size = sizeof(loss_rate) / sizeof(*loss_rate);
  const float reorder_rate = 0.1f;
  const float duplicate_rate = 0.1f;

  uint8_t media_loss_mask[kMaxNumberMediaPackets];
  uint8_t fec_loss_mask[kMaxNumberFecPackets];
  uint8_t fec_packet_masks[kMaxNumberFecPackets][kMaxNumberMediaPackets];

  // Seed the random number generator.
  const unsigned int random_seed = static_cast<unsigned int>(time(nullptr));
  Random random(random_seed);

  uint16_t seq_num = 0;
  uint32_t timestamp = random.Rand<uint32_t>();
  const uint32_t media_ssrc = random.Rand(1u, 0xfffffffe);
  uint32_t fec_ssrc;
  uint16_t fec_seq_num_offset;
  if (use_flexfec) {
    fec_ssrc = random.Rand(1u, 0xfffffffe);
    fec_seq_num_offset = random.Rand(0, 1 << 15);
  } else {
    fec_ssrc = media_ssrc;
    fec_seq_num_offset = 0;
  }

  std::unique_ptr<ForwardErrorCorrection> fec;
  if (use_flexfec) {
    fec = ForwardErrorCorrection::CreateFlexfec(fec_ssrc, media_ssrc);
  } else {
    RTC_DCHECK_EQ(media_ssrc, fec_ssrc);
    fec = ForwardErrorCorrection::CreateUlpfec(fec_ssrc);
  }

  // Loop over the mask types: random and bursty.
  for (int mask_type_idx = 0; mask_type_idx < kNumFecMaskTypes;
       ++mask_type_idx) {
    for (uint32_t loss_rate_idx = 0; loss_rate_idx < loss_rate_size;
         ++loss_rate_idx) {
      printf("Loss rate: %.2f, Mask type %d \n", loss_rate[loss_rate_idx],
             mask_type_idx);

      const uint32_t packet_mask_max = kMaxMediaPackets[mask_type_idx];
      std::vector<uint8_t> packet_mask(packet_mask_max * kNumMaskBytesL1);

      FecMaskType fec_mask_type = kMaskTypes[mask_type_idx];

      for (uint32_t num_media_packets = 1; num_media_packets <= packet_mask_max;
           num_media_packets++) {
        internal::PacketMaskTable mask_table(fec_mask_type, num_media_packets);

        for (uint32_t num_fec_packets = 1;
             num_fec_packets <= num_media_packets &&
             num_fec_packets <= packet_mask_max;
             num_fec_packets++) {
          // Loop over num_imp_packets: usually <= (0.3*num_media_packets).
          // For this test we check up to ~ (num_media_packets / 4).
          uint32_t max_num_imp_packets = num_media_packets / 4 + 1;
          for (uint32_t num_imp_packets = 0;
               num_imp_packets <= max_num_imp_packets &&
               num_imp_packets <= packet_mask_max;
               num_imp_packets++) {
            uint8_t protection_factor =
                static_cast<uint8_t>(num_fec_packets * 255 / num_media_packets);

            const uint32_t mask_bytes_per_fec_packet =
                (num_media_packets > 16) ? kNumMaskBytesL1 : kNumMaskBytesL0;

            std::ranges::fill(
                std::span(packet_mask)
                    .first(num_media_packets * mask_bytes_per_fec_packet),
                0);

            // Transfer packet masks from bit-mask to byte-mask.
            internal::GeneratePacketMasks(
                num_media_packets, num_fec_packets, num_imp_packets,
                kUseUnequalProtection, &mask_table, packet_mask);

#ifdef VERBOSE_OUTPUT
            printf(
                "%u media packets, %u FEC packets, %u num_imp_packets, "
                "loss rate = %.2f \n",
                num_media_packets, num_fec_packets, num_imp_packets,
                loss_rate[loss_rate_idx]);
            printf("Packet mask matrix \n");
#endif

            for (uint32_t i = 0; i < num_fec_packets; i++) {
              for (uint32_t j = 0; j < num_media_packets; j++) {
                const uint8_t byte_mask =
                    packet_mask[i * mask_bytes_per_fec_packet + j / 8];
                const uint32_t bit_position = (7 - j % 8);
                fec_packet_masks[i][j] =
                    (byte_mask & (1 << bit_position)) >> bit_position;
#ifdef VERBOSE_OUTPUT
                printf("%u ", fec_packet_masks[i][j]);
#endif
              }
#ifdef VERBOSE_OUTPUT
              printf("\n");
#endif
            }
#ifdef VERBOSE_OUTPUT
            printf("\n");
#endif
            // Check for all zero rows or columns: indicates incorrect mask.
            uint32_t row_limit = num_media_packets;
            for (uint32_t i = 0; i < num_fec_packets; ++i) {
              uint32_t row_sum = 0;
              for (uint32_t j = 0; j < row_limit; ++j) {
                row_sum += fec_packet_masks[i][j];
              }
              ASSERT_NE(0u, row_sum) << "Row is all zero " << i;
            }
            for (uint32_t j = 0; j < row_limit; ++j) {
              uint32_t column_sum = 0;
              for (uint32_t i = 0; i < num_fec_packets; ++i) {
                column_sum += fec_packet_masks[i][j];
              }
              ASSERT_NE(0u, column_sum) << "Column is all zero " << j;
            }

            // Construct media packets.
            // Reset the sequence number here for each FEC code/mask tested
            // below, to avoid sequence number wrap-around. In actual decoding,
            // old FEC packets in list are dropped if sequence number wrap
            // around is detected. This case is currently not handled below.
            seq_num = 0;
            for (uint32_t i = 0; i < num_media_packets; ++i) {
              std::unique_ptr<ForwardErrorCorrection::Packet> media_packet(
                  new ForwardErrorCorrection::Packet());
              const uint32_t kMinPacketSize = 12;
              const uint32_t kMaxPacketSize = static_cast<uint32_t>(
                  IP_PACKET_SIZE - 12 - 28 - fec->MaxPacketOverhead());
              size_t packet_length =
                  random.Rand(kMinPacketSize, kMaxPacketSize);
              media_packet->data.SetSize(packet_length);

              uint8_t* data = media_packet->data.MutableData();
              // Generate random values for the first 2 bytes.
              data[0] = random.Rand<uint8_t>();
              data[1] = random.Rand<uint8_t>();

              // The first two bits are assumed to be 10 by the
              // FEC encoder. In fact the FEC decoder will set the
              // two first bits to 10 regardless of what they
              // actually were. Set the first two bits to 10
              // so that a memcmp can be performed for the
              // whole restored packet.
              data[0] |= 0x80;
              data[0] &= 0xbf;

              // FEC is applied to a whole frame.
              // A frame is signaled by multiple packets without
              // the marker bit set followed by the last packet of
              // the frame for which the marker bit is set.
              // Only push one (fake) frame to the FEC.
              data[1] &= 0x7f;

              ByteWriter<uint16_t>::WriteBigEndian(&data[2], seq_num);
              ByteWriter<uint32_t>::WriteBigEndian(&data[4], timestamp);
              ByteWriter<uint32_t>::WriteBigEndian(&data[8], media_ssrc);
              // Generate random values for payload
              for (size_t j = 12; j < packet_length; ++j) {
                data[j] = random.Rand<uint8_t>();
              }
              media_packet_list.push_back(std::move(media_packet));
              seq_num++;
            }
            media_packet_list.back()->data.MutableData()[1] |= 0x80;

            ASSERT_EQ(0, fec->EncodeFec(media_packet_list, protection_factor,
                                        num_imp_packets, kUseUnequalProtection,
                                        fec_mask_type, &fec_packet_list))
                << "EncodeFec() failed";

            ASSERT_EQ(num_fec_packets, fec_packet_list.size())
                << "We requested " << num_fec_packets
                << " FEC packets, but "
                   "EncodeFec() produced "
                << fec_packet_list.size();

            memset(media_loss_mask, 0, sizeof(media_loss_mask));
            uint32_t media_packet_idx = 0;
            for (const auto& media_packet : media_packet_list) {
              // We want a value between 0 and 1.
              const float loss_random_variable = random.Rand<float>();

              if (loss_random_variable >= loss_rate[loss_rate_idx]) {
                media_loss_mask[media_packet_idx] = 1;
                std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>
                    received_packet(
                        new ForwardErrorCorrection::ReceivedPacket());
                received_packet->pkt = new ForwardErrorCorrection::Packet();
                received_packet->pkt->data = media_packet->data;
                received_packet->ssrc = media_ssrc;
                received_packet->seq_num = ByteReader<uint16_t>::ReadBigEndian(
                    media_packet->data.data() + 2);
                received_packet->is_fec = false;
                received_packet_list.push_back(std::move(received_packet));
              }
              media_packet_idx++;
            }

            memset(fec_loss_mask, 0, sizeof(fec_loss_mask));
            uint32_t fec_packet_idx = 0;
            for (auto* fec_packet : fec_packet_list) {
              const float loss_random_variable = random.Rand<float>();
              if (loss_random_variable >= loss_rate[loss_rate_idx]) {
                fec_loss_mask[fec_packet_idx] = 1;
                std::unique_ptr<ForwardErrorCorrection::ReceivedPacket>
                    received_packet(
                        new ForwardErrorCorrection::ReceivedPacket());
                received_packet->pkt = new ForwardErrorCorrection::Packet();
                received_packet->pkt->data = fec_packet->data;
                received_packet->seq_num = fec_seq_num_offset + seq_num;
                received_packet->is_fec = true;
                received_packet->ssrc = fec_ssrc;
                received_packet_list.push_back(std::move(received_packet));

                fec_mask_list.push_back(fec_packet_masks[fec_packet_idx]);
              }
              ++fec_packet_idx;
              ++seq_num;
            }

#ifdef VERBOSE_OUTPUT
            printf("Media loss mask:\n");
            for (uint32_t i = 0; i < num_media_packets; i++) {
              printf("%u ", media_loss_mask[i]);
            }
            printf("\n\n");

            printf("FEC loss mask:\n");
            for (uint32_t i = 0; i < num_fec_packets; i++) {
              printf("%u ", fec_loss_mask[i]);
            }
            printf("\n\n");
#endif

            auto fec_mask_it = fec_mask_list.begin();
            while (fec_mask_it != fec_mask_list.end()) {
              uint32_t hamming_dist = 0;
              uint32_t recovery_position = 0;
              for (uint32_t i = 0; i < num_media_packets; i++) {
                if (media_loss_mask[i] == 0 && (*fec_mask_it)[i] == 1) {
                  recovery_position = i;
                  ++hamming_dist;
                }
              }
              auto item_to_delete = fec_mask_it;
              ++fec_mask_it;

              if (hamming_dist == 1) {
                // Recovery possible. Restart search.
                media_loss_mask[recovery_position] = 1;
                fec_mask_it = fec_mask_list.begin();
              } else if (hamming_dist == 0) {
                // FEC packet cannot provide further recovery.
                fec_mask_list.erase(item_to_delete);
              }
            }
#ifdef VERBOSE_OUTPUT
            printf("Recovery mask:\n");
            for (uint32_t i = 0; i < num_media_packets; ++i) {
              printf("%u ", media_loss_mask[i]);
            }
            printf("\n\n");
#endif
            // For error-checking frame completion.
            bool fec_packet_received = false;
            while (!received_packet_list.empty()) {
              size_t num_packets_to_decode = random.Rand(
                  1u, static_cast<uint32_t>(received_packet_list.size()));
              ReceivePackets(&to_decode_list, &received_packet_list,
                             num_packets_to_decode, reorder_rate,
                             duplicate_rate, &random);

              if (fec_packet_received == false) {
                for (const auto& received_packet : to_decode_list) {
                  if (received_packet->is_fec) {
                    fec_packet_received = true;
                  }
                }
              }
              for (const auto& received_packet : to_decode_list) {
                fec->DecodeFec(*received_packet, &recovered_packet_list);
              }
              to_decode_list.clear();
            }
            media_packet_idx = 0;
            for (const auto& media_packet : media_packet_list) {
              if (media_loss_mask[media_packet_idx] == 1) {
                // Should have recovered this packet.
                auto recovered_packet_list_it = recovered_packet_list.cbegin();

                ASSERT_FALSE(recovered_packet_list_it ==
                             recovered_packet_list.end())
                    << "Insufficient number of recovered packets.";
                ForwardErrorCorrection::RecoveredPacket* recovered_packet =
                    recovered_packet_list_it->get();

                ASSERT_EQ(recovered_packet->pkt->data.size(),
                          media_packet->data.size())
                    << "Recovered packet length not identical to original "
                       "media packet";
                ASSERT_EQ(0, memcmp(recovered_packet->pkt->data.cdata(),
                                    media_packet->data.cdata(),
                                    media_packet->data.size()))
                    << "Recovered packet payload not identical to original "
                       "media packet";
                recovered_packet_list.pop_front();
              }
              ++media_packet_idx;
            }
            fec->ResetState(&recovered_packet_list);
            ASSERT_TRUE(recovered_packet_list.empty())
                << "Excessive number of recovered packets.\t size is: "
                << recovered_packet_list.size();
            // -- Teardown --
            media_packet_list.clear();

            // Clear FEC packet list, so we don't pass in a non-empty
            // list in the next call to DecodeFec().
            fec_packet_list.clear();

            // Delete received packets we didn't pass to DecodeFec(), due to
            // early frame completion.
            received_packet_list.clear();

            while (!fec_mask_list.empty()) {
              fec_mask_list.pop_front();
            }
            timestamp += 90000 / 30;
          }  // loop over num_imp_packets
        }    // loop over FecPackets
      }      // loop over num_media_packets
    }        // loop over loss rates
  }          // loop over mask types

  // Have DecodeFec clear the recovered packet list.
  fec->ResetState(&recovered_packet_list);
  ASSERT_TRUE(recovered_packet_list.empty())
      << "Recovered packet list is not empty";
}

TEST(FecTest, UlpfecTest) {
  RunTest(false);
}

TEST(FecTest, FlexfecTest) {
  RunTest(true);
}

}  // namespace test
}  // namespace webrtc
