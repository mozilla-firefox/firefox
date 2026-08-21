/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <cstring>
#include <limits>

#include "AnnexB.h"
#include "BitWriter.h"
#include "BufferReader.h"
#include "ByteWriter.h"
#include "H264.h"
#include "H265.h"
#include "gtest/gtest.h"
#include "mozilla/EndianUtils.h"

namespace mozilla {

// Create AVCC style extra data (the contents on an AVCC box). Note
// NALLengthSize will be 4 so AVCC samples need to set their data up
// accordingly.
static already_AddRefed<MediaByteBuffer> GetExtraData() {
  // Extra data with
  // - baseline profile(0x42 == 66).
  // - constraint flags 0 and 1 set(0xc0) -- normal for baseline profile.
  // - level 4.0 (0x28 == 40).
  // - 1280 * 720 resolution.
  return H264::CreateExtraData(0x42, 0xc0, H264_LEVEL{0x28}, {1280, 720});
}

// Create an AVCC style sample with requested size in bytes. This sample is
// setup to contain a single NAL (in practice samples can contain many). The
// sample sets its NAL size to aSampleSize - 4 and stores that size in the first
// 4 bytes. Aside from the NAL size at the start, the data is uninitialized
// (beware)! aSampleSize is a uint32_t as samples larger than can be expressed
// by a uint32_t are not to spec.
static already_AddRefed<MediaRawData> GetAvccSample(uint32_t aSampleSize) {
  if (aSampleSize < 4) {
    // Stop tests asking for insane samples.
    EXPECT_FALSE(true) << "Samples should be requested with sane sizes";
  }
  nsTArray<uint8_t> sampleData;

  // Write the NAL size.
  ByteWriter<BigEndian> writer(sampleData);
  EXPECT_TRUE(writer.WriteU32(aSampleSize - 4));

  // Write the 'NAL'. Beware, this data is uninitialized.
  sampleData.AppendElements(static_cast<size_t>(aSampleSize) - 4);
  RefPtr<MediaRawData> rawData =
      new MediaRawData{sampleData.Elements(), sampleData.Length()};
  EXPECT_NE(rawData->Data(), nullptr);

  // Set extra data.
  rawData->mExtraData = GetExtraData();
  return rawData.forget();
}

static const uint8_t sHvccBytesBuffer[] = {
    1 /* version */,
    1 /* general_profile_space/general_tier_flag/general_profile_idc */,
    0x60 /* general_profile_compatibility_flags 1/4 */,
    0 /* general_profile_compatibility_flags 2/4 */,
    0 /* general_profile_compatibility_flags 3/4 */,
    0 /* general_profile_compatibility_flags 4/4 */,
    0x90 /* general_constraint_indicator_flags 1/6 */,
    0 /* general_constraint_indicator_flags 2/6 */,
    0 /* general_constraint_indicator_flags 3/6 */,
    0 /* general_constraint_indicator_flags 4/6 */,
    0 /* general_constraint_indicator_flags 5/6 */,
    0 /* general_constraint_indicator_flags 6/6 */,
    0x5A /* general_level_idc */,
    0 /* min_spatial_segmentation_idc 1/2 */,
    0 /* min_spatial_segmentation_idc 2/2 */,
    0 /* parallelismType */,
    1 /* chroma_format_idc */,
    0 /* bit_depth_luma_minus8 */,
    0 /* bit_depth_chroma_minus8 */,
    0 /* avgFrameRate 1/2 */,
    0 /* avgFrameRate 2/2 */,
    0x0F /* constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
          */
    ,
    2 /* numOfArrays */,
    /* SPS Array */
    0x21 /* NAL_unit_type (SPS) */,
    0 /* numNalus 1/2 */,
    1 /* numNalus 2/2 */,

    /* SPS */
    0 /* nalUnitLength 1/2 */,
    8 /* nalUnitLength 2/2 (header + rsbp) */,
    0x42 /* NALU header 1/2 */,
    0 /* NALU header 2/2 */,
    0 /* rbsp 1/6 */,
    0 /* rbsp 2/6 */,
    0 /* rbsp 3/6 */,
    0 /* rbsp 4/6 */,
    0 /* rbsp 5/6 */,
    0 /* rbsp 6/6 */,

    /* PPS Array */
    0x22 /* NAL_unit_type (PPS) */,
    0 /* numNalus 1/2 */,
    1 /* numNalus 2/2 */,

    /* PPS */
    0 /* nalUnitLength 1/2 */,
    3 /* nalUnitLength 2/2 (header + rsbp) */,
    0x44 /* NALU header 1/2 */,
    0 /* NALU header 2/2 */,
    0 /* rbsp */,
};

// Extend the shared fake HVCC extradata with a single PREFIX_SEI_NUT so tests
// can exercise WMF-specific SEI filtering without relying on external media.
static already_AddRefed<MediaByteBuffer> GetHvccExtraDataWithPrefixSEI(
    uint8_t aPayloadType, size_t aPayloadSize) {
  RefPtr<MediaByteBuffer> extradata = new MediaByteBuffer;
  extradata->AppendElements(sHvccBytesBuffer, std::size(sHvccBytesBuffer));

  auto rv = HVCCConfig::Parse(extradata);
  EXPECT_TRUE(rv.isOk());
  if (rv.isErr()) {
    return nullptr;
  }

  const HVCCConfig hvcc = rv.unwrap();
  nsTArray<H265NALU> nalus;
  for (const auto& nalu : hvcc.mNALUs) {
    nalus.AppendElement(nalu);
  }

  nsTArray<uint8_t> seiNalu;
  seiNalu.AppendElement(0x4e);
  seiNalu.AppendElement(0x01);
  seiNalu.AppendElement(aPayloadType);

  size_t payloadSize = aPayloadSize;
  while (payloadSize >= 0xff) {
    seiNalu.AppendElement(0xff);
    payloadSize -= 0xff;
  }
  seiNalu.AppendElement(static_cast<uint8_t>(payloadSize));
  // AppendElements does not zero POD bytes; a zeroed payload would make
  // EncodeH265NALUnit insert emulation-prevention bytes and change the
  // round-trip length. Fill with a fixed non-zero pattern (no 0x00 0x00 runs)
  // so the result size is deterministic regardless of allocator state.
  uint8_t* payload = seiNalu.AppendElements(aPayloadSize);
  memset(payload, 0xAA, aPayloadSize);
  seiNalu.AppendElement(0x80);

  nalus.AppendElement(H265NALU(seiNalu.Elements(), seiNalu.Length()));
  return H265::CreateNewExtraData(hvcc, nalus);
}

// Create a HVCC sample, which contain fake data, in given size.
static already_AddRefed<MediaRawData> GetHVCCSample(uint32_t aSampleSize) {
  if (aSampleSize < 4) {
    // Stop tests asking for insane samples.
    EXPECT_FALSE(true) << "Samples should be requested with sane sizes";
  }
  auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  extradata->AppendElements(sHvccBytesBuffer, std::size(sHvccBytesBuffer));

  // Write the NAL size.
  nsTArray<uint8_t> sampleData;
  ByteWriter<BigEndian> writer(sampleData);
  EXPECT_TRUE(writer.WriteU32(aSampleSize - 4));  // Assume it's a 4 bytes NALU

  // Fill fake empty data
  for (uint32_t idx = 0; idx < aSampleSize - 4; idx++) {
    sampleData.AppendElement(0);
  }
  RefPtr<MediaRawData> rawData =
      new MediaRawData{sampleData.Elements(), sampleData.Length()};
  EXPECT_NE(rawData->Data(), nullptr);
  EXPECT_EQ(rawData->Size(), aSampleSize);
  rawData->mExtraData = extradata;
  return rawData.forget();
}

// Create a HVCC sample by using given data in given size.
static already_AddRefed<MediaRawData> GetHVCCSample(
    const uint8_t* aData, const uint32_t aDataLength) {
  if (aDataLength < 4) {
    // Stop tests asking for insane samples.
    EXPECT_FALSE(true) << "Samples should be requested with sane sizes";
  }
  auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  extradata->AppendElements(sHvccBytesBuffer, std::size(sHvccBytesBuffer));

  // Write the NAL size.
  nsTArray<uint8_t> sampleData;
  ByteWriter<BigEndian> writer(sampleData);
  EXPECT_TRUE(writer.WriteU32(aDataLength));  // Assume it's a 4 bytes NALU
  sampleData.AppendElements(aData, aDataLength);

  RefPtr<MediaRawData> rawData =
      new MediaRawData{sampleData.Elements(), sampleData.Length()};
  EXPECT_NE(rawData->Data(), nullptr);
  EXPECT_EQ(rawData->Size(), aDataLength + 4);
  rawData->mExtraData = extradata;
  return rawData.forget();
}

// Create a HVCC samples by given NALUs.
static already_AddRefed<MediaRawData> GetHVCCSamples(
    const nsTArray<Span<const uint8_t>>& aNALUs) {
  nsTArray<uint8_t> data;
  ByteWriter<BigEndian> writer(data);

  size_t totalSize = 0;

  for (const auto& nalu : aNALUs) {
    if (nalu.size() < 2) {
      // NAL unit header is at least 2 bytes.
      EXPECT_FALSE(true) << "Samples should be requested with sane sizes";
      return nullptr;
    }
    totalSize += nalu.size();
    EXPECT_TRUE(writer.WriteU32(nalu.size()));  // Assume it's a 4 bytes NALU
    data.AppendElements(nalu.data(), nalu.size());
  }

  RefPtr<MediaRawData> rawData =
      new MediaRawData{data.Elements(), data.Length()};
  auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  extradata->AppendElements(sHvccBytesBuffer, std::size(sHvccBytesBuffer));
  rawData->mExtraData = extradata;

  EXPECT_NE(rawData->Data(), nullptr);
  EXPECT_EQ(rawData->Size(), totalSize + 4 * aNALUs.Length());
  return rawData.forget();
}

// Test that conversion from AVCC to AnnexB works as expected.
TEST(AnnexB, AVCCToAnnexBConversion)
{
  RefPtr<MediaRawData> rawData{GetAvccSample(128)};

  {
    // Test conversion of data when not adding SPS works as expected.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    Result<Ok, nsresult> result =
        AnnexB::ConvertAVCCSampleToAnnexB(rawDataClone, /* aAddSps */ false);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_EQ(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be the same size as the AVCC sample -- the 4 "
           "byte NAL length data (AVCC) is replaced with 4 bytes of NAL "
           "separator (AnnexB)";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
  }

  {
    // Test that the SPS data is not added if the frame is not a keyframe.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    rawDataClone->mKeyframe =
        false;  // false is the default, but let's be sure.
    Result<Ok, nsresult> result =
        AnnexB::ConvertAVCCSampleToAnnexB(rawDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_EQ(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be the same size as the AVCC sample -- the 4 "
           "byte NAL length data (AVCC) is replaced with 4 bytes of NAL "
           "separator (AnnexB) and SPS data is not added as the frame is not a "
           "keyframe";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
  }

  {
    // Test that the SPS data is added to keyframes.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    rawDataClone->mKeyframe = true;
    Result<Ok, nsresult> result =
        AnnexB::ConvertAVCCSampleToAnnexB(rawDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_GT(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be larger than the AVCC sample because we've "
           "added SPS data";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
    // We could verify the SPS and PPS data we add, but we don't have great
    // tooling to do so. Consider doing so in future.
  }

  {
    // Test conversion involving subsample encryption doesn't overflow vlaues.
    const uint32_t sampleSize = UINT16_MAX * 2;
    RefPtr<MediaRawData> rawCryptoData{GetAvccSample(sampleSize)};
    // Need to be a keyframe to test prepending SPS + PPS to sample.
    rawCryptoData->mKeyframe = true;
    UniquePtr<MediaRawDataWriter> rawDataWriter = rawCryptoData->CreateWriter();

    rawDataWriter->mCrypto.mCryptoScheme = CryptoScheme::Cenc;

    // We want to check that the clear size doesn't overflow during conversion.
    // This size originates in a uint16_t, but since it can grow during AnnexB
    // we cover it here.
    const uint16_t clearSize = UINT16_MAX - 10;
    // Set a clear size very close to uint16_t max value.
    rawDataWriter->mCrypto.mPlainSizes.AppendElement(clearSize);
    rawDataWriter->mCrypto.mEncryptedSizes.AppendElement(sampleSize -
                                                         clearSize);

    RefPtr<MediaRawData> rawCryptoDataClone = rawCryptoData->Clone();
    Result<Ok, nsresult> result = AnnexB::ConvertAVCCSampleToAnnexB(
        rawCryptoDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_GT(rawCryptoDataClone->Size(), rawCryptoData->Size())
        << "AnnexB sample should be larger than the AVCC sample because we've "
           "added SPS data";
    EXPECT_GT(rawCryptoDataClone->mCrypto.mPlainSizes[0],
              rawCryptoData->mCrypto.mPlainSizes[0])
        << "Conversion should have increased clear data sizes without overflow";
    EXPECT_EQ(rawCryptoDataClone->mCrypto.mEncryptedSizes[0],
              rawCryptoData->mCrypto.mEncryptedSizes[0])
        << "Conversion should not affect encrypted sizes";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawCryptoDataClone))
        << "The sample should be AnnexB following conversion";
  }
}

TEST(AnnexB, HVCCToAnnexBConversion)
{
  RefPtr<MediaRawData> rawData{GetHVCCSample(128)};
  {
    // Test conversion of data when not adding SPS works as expected.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    Result<Ok, nsresult> result =
        AnnexB::ConvertHVCCSampleToAnnexB(rawDataClone, /* aAddSps */ false);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_EQ(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be the same size as the HVCC sample -- the 4 "
           "byte NAL length data (HVCC) is replaced with 4 bytes of NAL "
           "separator (AnnexB)";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
  }
  {
    // Test that the SPS data is not added if the frame is not a keyframe.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    rawDataClone->mKeyframe =
        false;  // false is the default, but let's be sure.
    Result<Ok, nsresult> result =
        AnnexB::ConvertHVCCSampleToAnnexB(rawDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_EQ(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be the same size as the HVCC sample -- the 4 "
           "byte NAL length data (HVCC) is replaced with 4 bytes of NAL "
           "separator (AnnexB) and SPS data is not added as the frame is not a "
           "keyframe";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
  }
  {
    // Test that the SPS data is added to keyframes.
    RefPtr<MediaRawData> rawDataClone = rawData->Clone();
    rawDataClone->mKeyframe = true;
    Result<Ok, nsresult> result =
        AnnexB::ConvertHVCCSampleToAnnexB(rawDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_GT(rawDataClone->Size(), rawData->Size())
        << "AnnexB sample should be larger than the HVCC sample because we've "
           "added SPS data";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
        << "The sample should be AnnexB following conversion";
    // We could verify the SPS and PPS data we add, but we don't have great
    // tooling to do so. Consider doing so in future.
  }
  {
    // Test conversion involving subsample encryption doesn't overflow values.
    const uint32_t sampleSize = UINT16_MAX * 2;
    RefPtr<MediaRawData> rawCryptoData{GetHVCCSample(sampleSize)};
    // Need to be a keyframe to test prepending SPS + PPS to sample.
    rawCryptoData->mKeyframe = true;
    UniquePtr<MediaRawDataWriter> rawDataWriter = rawCryptoData->CreateWriter();

    rawDataWriter->mCrypto.mCryptoScheme = CryptoScheme::Cenc;

    // We want to check that the clear size doesn't overflow during conversion.
    // This size originates in a uint16_t, but since it can grow during AnnexB
    // we cover it here.
    const uint16_t clearSize = UINT16_MAX - 10;
    // Set a clear size very close to uint16_t max value.
    rawDataWriter->mCrypto.mPlainSizes.AppendElement(clearSize);
    rawDataWriter->mCrypto.mEncryptedSizes.AppendElement(sampleSize -
                                                         clearSize);

    RefPtr<MediaRawData> rawCryptoDataClone = rawCryptoData->Clone();
    Result<Ok, nsresult> result = AnnexB::ConvertHVCCSampleToAnnexB(
        rawCryptoDataClone, /* aAddSps */ true);
    EXPECT_TRUE(result.isOk()) << "Conversion should succeed";
    EXPECT_GT(rawCryptoDataClone->Size(), rawCryptoData->Size())
        << "AnnexB sample should be larger than the HVCC sample because we've "
           "added SPS data";
    EXPECT_GT(rawCryptoDataClone->mCrypto.mPlainSizes[0],
              rawCryptoData->mCrypto.mPlainSizes[0])
        << "Conversion should have increased clear data sizes without overflow";
    EXPECT_EQ(rawCryptoDataClone->mCrypto.mEncryptedSizes[0],
              rawCryptoData->mCrypto.mEncryptedSizes[0])
        << "Conversion should not affect encrypted sizes";
    EXPECT_TRUE(AnnexB::IsAnnexB(*rawCryptoDataClone))
        << "The sample should be AnnexB following conversion";
  }
}

TEST(H264, AVCCParsingSuccess)
{
  {
    // AVCC without SPS, PPS and SPSExt
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t avccBytesBuffer[] = {
        1 /* version */,
        0x64 /* profile (High) */,
        0 /* profile compat (0) */,
        40 /* level (40) */,
        0xfc | 3 /* nal size - 1 */,
        0xe0 /* num SPS (0) */,
        0 /* num PPS (0) */
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 40);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 0u);
    EXPECT_EQ(avcc.NumPPS(), 0u);
    EXPECT_TRUE(avcc.mChromaFormat.isNothing());
    EXPECT_TRUE(avcc.mBitDepthLumaMinus8.isNothing());
    EXPECT_TRUE(avcc.mBitDepthChromaMinus8.isNothing());
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
    EXPECT_EQ(avcc.mSPSExts.Length(), 0u);
  }
  {
    // AVCC with SPS, PPS but no chroma format, lumn/chrom bit depth and SPSExt.
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        // configurationVersion
        0x01,
        // AVCProfileIndication (e.g., High Profile = 100)
        0x64,
        // profile_compatibility
        0x00,
        // AVCLevelIndication
        0x1E,
        // 6 bits reserved (111111) + 2 bits lengthSizeMinusOne (3 -> 4 bytes)
        0xFF,
        // 3 bits reserved (111) + 5 bits numOfSPS (1)
        0xE1,
        // SPS[0] length = 0x0004
        0x00,
        0x04,
        // SPS NAL unit (fake)
        0x67,
        0x64,
        0x00,
        0x1F,
        // numOfPPS = 1
        0x01,
        // PPS[0] length = 0x0002
        0x00,
        0x02,
        // PPS NAL unit (fake)
        0x68,
        0xCE,
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_TRUE(avcc.mChromaFormat.isNothing());
    EXPECT_TRUE(avcc.mBitDepthLumaMinus8.isNothing());
    EXPECT_TRUE(avcc.mBitDepthChromaMinus8.isNothing());
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
    EXPECT_EQ(avcc.mSPSExts.Length(), 0u);
  }
  {
    // AVCC with SPS, PPS and SPSExt.
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        // configurationVersion
        0x01,
        // AVCProfileIndication (e.g., High Profile = 100)
        0x64,
        // profile_compatibility
        0x00,
        // AVCLevelIndication
        0x1E,
        // 6 bits reserved (111111) + 2 bits lengthSizeMinusOne (3 -> 4 bytes)
        0xFF,
        // 3 bits reserved (111) + 5 bits numOfSPS (1)
        0xE1,
        // SPS[0] length = 0x0004
        0x00, 0x04,
        // SPS NAL unit (fake)
        0x67, 0x64, 0x00, 0x1F,
        // numOfPPS = 1
        0x01,
        // PPS[0] length = 0x0002
        0x00, 0x02,
        // PPS NAL unit (fake)
        0x68, 0xCE,
        // 6 bits reserved (111111) + 2 bits chroma_format (0 -> 4:2:0)
        0xFC,
        // 5 bits reserved (11111) + 3 bits bit_depth_luma_minus8 (0 -> 8-bit)
        0xF8,
        // 5 bits reserved (11111) + 3 bits bit_depth_chroma_minus8 (0 -> 8-bit)
        0xF8,
        // numOfSPSext = 1
        0x01,
        // SPS Ext[0] length = 0x0003
        0x00, 0x03,
        // SPS Ext NAL unit (fake)
        0x6D, 0xB2, 0x20};
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_EQ(*avcc.mChromaFormat, 0);
    EXPECT_EQ(*avcc.mBitDepthLumaMinus8, 0);
    EXPECT_EQ(*avcc.mBitDepthChromaMinus8, 0);
    EXPECT_EQ(avcc.NumSPSExt(), 1u);
  }
  // Following part are optional, fail to parse them won't cause an actual error
  {
    // SPS Ext length = 0x0004, but only provides 2 bytes of data
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // High profile
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x01,        // SPS length = 1
        0x67,              // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x01,        // PPS length = 1
        0x68,              // PPS NAL
        0xFC,              // expect at least 32 bits but not enough
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_TRUE(avcc.mChromaFormat.isNothing());
    EXPECT_TRUE(avcc.mBitDepthLumaMinus8.isNothing());
    EXPECT_TRUE(avcc.mBitDepthChromaMinus8.isNothing());
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
  }
  {
    // SPS Ext length = 0x0004, but only provides 2 bytes of data
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // High profile
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x01,        // SPS length = 1
        0x67,              // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x01,        // PPS length = 1
        0x68,              // PPS NAL
        0xFC,              // reserved + chroma_format=0
        0xF8,              // reserved + bit_depth_luma_minus8=0
        0xF8,              // reserved + bit_depth_chroma_minus8=0
        0x01,              // numOfSPSExt = 1
        0x00, 0x04,        // SPS Ext length = 4
        0x6A, 0x01         // Only 2 bytes of SPSExt NAL
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_EQ(*avcc.mChromaFormat, 0);
    EXPECT_EQ(*avcc.mBitDepthLumaMinus8, 0);
    EXPECT_EQ(*avcc.mBitDepthChromaMinus8, 0);
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
  }
  {
    // Insuffient data, wrong SPSEXT length
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // High profile
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x01,        // SPS length = 1
        0x67,              // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x01,        // PPS length = 1
        0x68,              // PPS NAL
        0xFC,              // reserved + chroma_format=0
        0xF8,              // reserved + bit_depth_luma_minus8=0
        0xF8,              // reserved + bit_depth_chroma_minus8=0
        0x01,              // numOfSPSExt = 1
        0x00,              // Wrong SPS Ext length, should be 16 bits
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_EQ(*avcc.mChromaFormat, 0);
    EXPECT_EQ(*avcc.mBitDepthLumaMinus8, 0);
    EXPECT_EQ(*avcc.mBitDepthChromaMinus8, 0);
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
  }
  {
    //  Expect SPSExt payload, but the payload is an incorrect NALU type
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // High profile
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x01,        // SPS length = 1
        0x67,              // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x01,        // PPS length = 1
        0x68,              // PPS NAL
        0xFC,              // reserved + chroma_format=0
        0xF8,              // reserved + bit_depth_luma_minus8=0
        0xF8,              // reserved + bit_depth_chroma_minus8=0
        0x01,              // numOfSPSExt = 1
        0x00, 0x03,        // SPS Ext[0] length = 0x0003
        0x77, 0xB2, 0x20,  // Expect SPSExt, but wrong NALU type
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isOk());
    const auto avcc = res.unwrap();
    EXPECT_EQ(avcc.mConfigurationVersion, 1);
    EXPECT_EQ(avcc.mAVCProfileIndication, 0x64);
    EXPECT_EQ(avcc.mProfileCompatibility, 0);
    EXPECT_EQ(avcc.mAVCLevelIndication, 0x1E);
    EXPECT_EQ(avcc.NALUSize(), 4);
    EXPECT_EQ(avcc.NumSPS(), 1u);
    EXPECT_EQ(avcc.NumPPS(), 1u);
    EXPECT_EQ(*avcc.mChromaFormat, 0);
    EXPECT_EQ(*avcc.mBitDepthLumaMinus8, 0);
    EXPECT_EQ(*avcc.mBitDepthChromaMinus8, 0);
    EXPECT_EQ(avcc.NumSPSExt(), 0u);
  }
}

TEST(H264, AVCCParsingFailure)
{
  {
    // Incorrect version
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t avccBytesBuffer[] = {
        2 /* version */,
        0x64 /* profile (High) */,
        0 /* profile compat (0) */,
        40 /* level (40) */,
        0xfc | 3 /* nal size - 1 */,
        0xe0 /* num SPS (0) */,
        0 /* num PPS (0) */
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto avcc = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(avcc.isErr());
  }
  {
    // Insuffient data (lacking of PPS)
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t avccBytesBuffer[] = {
        1 /* version */,
        0x64 /* profile (High) */,
        0 /* profile compat (0) */,
        40 /* level (40) */,
        0xfc | 3 /* nal size - 1 */,
        0xe0 /* num SPS (0) */,
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto avcc = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(avcc.isErr());
  }
  {
    // Insuffient data, wrong SPS length
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne (2 bits)
        0xE1,              // reserved + 1 SPS
        0x00,              // Wrong SPS length, should be 16 bits
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // SPS length = 0x0004, but only provides 2 bytes of data
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne (2 bits)
        0xE1,              // reserved + 1 SPS
        0x00, 0x04,        // SPS length = 4
        0x67, 0x42         // Only 2 bytes of SPS payload (should be 4)
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // Expect SPS payload, but the payload is an incorrect NALU type
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne (2 bits)
        0xE1,              // reserved + 1 SPS
        0x00, 0x02,        // SPS length = 2
        0x55, 0xCE,        // Expect SPS, but wrong NALU type
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // Missing numOfPictureParameterSets
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x02,        // SPS length = 2
        0x67, 0x42,        // SPS NAL
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // PPS length = 0x0003, but only provides 1 byte of data
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x02,        // SPS length = 2
        0x67, 0x42,        // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x03,        // PPS length = 3
        0x68               // Only 1 byte instead of 3
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // Insufficient data, wrong PPS length
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // profile, compat, level
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x02,        // SPS length = 2
        0x67, 0x42,        // SPS NAL
        0x01,              // 1 PPS
        0x00               // Wrong PPS length, should be 16 bits
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
  {
    // Expect PPS payload, but the payload is an incorrect NALU type
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    const uint8_t avccBytesBuffer[] = {
        0x01,              // configurationVersion
        0x64, 0x00, 0x1E,  // High profile
        0xFF,              // reserved + lengthSizeMinusOne
        0xE1,              // reserved + 1 SPS
        0x00, 0x01,        // SPS length = 1
        0x67,              // SPS NAL
        0x01,              // 1 PPS
        0x00, 0x01,        // PPS length = 1
        0x70               // Expect PPS, but wrong NALU type
    };
    extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
    auto res = AVCCConfig::Parse(extradata);
    EXPECT_TRUE(res.isErr());
  }
}

TEST(H264, CreateNewExtraData)
{
  // First create an AVCC config without sps, pps
  auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  const uint8_t avccBytesBuffer[] = {
      0x01,  // configurationVersion
      0x64,  // AVCProfileIndication (High Profile = 100)
      0x00,  // profile_compatibility
      0x1E,  // AVCLevelIndication (Level 3.0)
      0xFF,  // 6 bits reserved (111111) + 2 bits lengthSizeMinusOne (3 -> 4
             // bytes)
      0xE0,  // 3 bits reserved (111) + 5 bits numOfSPS = 0
      0x00,  // numOfPPS = 0
      0xFC,  // 6 bits reserved (111111) + 2 bits chroma_format = 0 (4:2:0)
      0xF8,  // 5 bits reserved (11111) + 3 bits bit_depth_luma_minus8 = 0
             // (8-bit)
      0xF8,  // 5 bits reserved (11111) + 3 bits bit_depth_chroma_minus8 = 0
             // (8-bit)
      0x00   // numOfSequenceParameterSetExt = 0
  };
  extradata->AppendElements(avccBytesBuffer, std::size(avccBytesBuffer));
  auto res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  auto avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 0u);
  EXPECT_EQ(avcc.NumPPS(), 0u);

  // Create new extradata with 1 SPS
  const uint8_t sps[] = {
      0x67,
      0x64,
      0x00,
      0x1F,
  };
  H264NALU spsNALU = H264NALU(sps, std::size(sps));
  avcc.mSPSs.AppendElement(spsNALU);
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 1u);
  EXPECT_EQ(avcc.NumPPS(), 0u);

  // Create new extradata with 1 SPS and 1 PPS
  const uint8_t pps[] = {
      0x68,
      0xCE,
  };
  H264NALU ppsNALU = H264NALU(pps, std::size(pps));
  avcc.mPPSs.AppendElement(ppsNALU);
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 1u);
  EXPECT_EQ(avcc.NumPPS(), 1u);

  // Create new extradata with 2 SPS and 1 PPS
  avcc.mSPSs.AppendElement(spsNALU);
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 2u);
  EXPECT_EQ(avcc.NumPPS(), 1u);

  // Create new extradata with 2 SPS and 2 PPS
  avcc.mPPSs.AppendElement(ppsNALU);
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 2u);
  EXPECT_EQ(avcc.NumPPS(), 2u);

  // Besides SPS and PPS, let's ensure chroma_format, bit_depth_luma_minus8 and
  // bit_depth_chroma_minus8 are preserved correctly as well
  EXPECT_EQ(*avcc.mChromaFormat, 0);
  EXPECT_EQ(*avcc.mBitDepthLumaMinus8, 0);
  EXPECT_EQ(*avcc.mBitDepthChromaMinus8, 0);

  // Create new extradata with non-zero chroma_format,
  // bit_depth_luma_minus8 and bit_depth_chroma_minus8.
  *avcc.mChromaFormat = 2;          // 2 bits
  *avcc.mBitDepthLumaMinus8 = 5;    // 3 bits
  *avcc.mBitDepthChromaMinus8 = 3;  // 3 bits
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isOk());
  avcc = res.unwrap();
  EXPECT_EQ(avcc.mChromaFormat.value(), 2);
  EXPECT_EQ(avcc.mBitDepthLumaMinus8.value(), 5);
  EXPECT_EQ(avcc.mBitDepthChromaMinus8.value(), 3);

  // Use a wrong attribute, which will generate an invalid config
  avcc.mConfigurationVersion = 5;
  extradata = avcc.CreateNewExtraData();
  res = AVCCConfig::Parse(extradata);
  EXPECT_TRUE(res.isErr());
}

TEST(H264, AnnexBExtractExtraDataForAVCC)
{
  // First create an AnnexB config
  const uint8_t annexBBytesBuffer[]{// AnnexB delimiter
                                    0x00, 0x00, 0x00, 0x01,
                                    // SPS NAL unit
                                    0x67, 0x64, 0x00, 0x1F,
                                    // AnnexB delimiter
                                    0x00, 0x00, 0x00, 0x01,
                                    // PPS NAL unit
                                    0x68, 0xCE};
  auto annexBExtradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  annexBExtradata->AppendElements(annexBBytesBuffer,
                                  std::size(annexBBytesBuffer));

  // Extract the AVCC extradata from the AnnexB bytestream
  auto avccExtradata = AnnexB::ExtractExtraDataForAVCC(*annexBExtradata);
  ASSERT_TRUE(!!avccExtradata);

  // Now parse that extradata to make sure it matches the original AnnexB
  auto res = AVCCConfig::Parse(avccExtradata);
  ASSERT_TRUE(res.isOk());
  auto avcc = res.unwrap();
  EXPECT_EQ(avcc.NumSPS(), 1u);
  EXPECT_EQ(avcc.NumSPSExt(), 0u);
  EXPECT_EQ(avcc.NumPPS(), 1u);
  EXPECT_EQ(avcc.NALUSize(), 4);
  EXPECT_EQ(avcc.mConfigurationVersion, 1u);
  EXPECT_EQ(avcc.mAVCProfileIndication, 100u);
  EXPECT_EQ(avcc.mProfileCompatibility, 0u);
  EXPECT_EQ(avcc.mAVCLevelIndication, 31u);
}

TEST(H264, DecodeSPSFromExtraData)
{
  uint8_t sps_pps[] = {0x01, 0x4d, 0x40, 0x0c, 0xff, 0xe1, 0x00, 0x1b, 0x67,
                       0x4d, 0x40, 0x0c, 0xe8, 0x80, 0x80, 0x9d, 0x80, 0xb5,
                       0x01, 0x01, 0x01, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40,
                       0x00, 0x00, 0x0f, 0x03, 0xc5, 0x0a, 0x44, 0x80, 0x01,
                       0x00, 0x04, 0x68, 0xeb, 0xef, 0x20};

  RefPtr<MediaByteBuffer> extraData = new MediaByteBuffer();
  extraData->AppendElements(sps_pps, sizeof(sps_pps));
  SPSData spsdata1;
  bool success = H264::DecodeSPSFromExtraData(extraData, spsdata1);
  EXPECT_EQ(success, true);

  auto testOutput = [&](uint8_t aProfile, uint8_t aConstraints, uint8_t aLevel,
                        gfx::IntSize aSize, char const* aDesc) {
    RefPtr<MediaByteBuffer> extraData = H264::CreateExtraData(
        aProfile, aConstraints, H264_LEVEL{aLevel}, aSize);
    SPSData spsData;
    success = H264::DecodeSPSFromExtraData(extraData, spsData);
    EXPECT_EQ(success, true) << aDesc;
    EXPECT_EQ(spsData.profile_idc, aProfile) << aDesc;
    EXPECT_EQ(spsData.constraint_set0_flag, (aConstraints >> 7) & 1) << aDesc;
    EXPECT_EQ(spsData.constraint_set1_flag, (aConstraints >> 6) & 1) << aDesc;
    EXPECT_EQ(spsData.constraint_set2_flag, (aConstraints >> 5) & 1) << aDesc;
    EXPECT_EQ(spsData.constraint_set3_flag, (aConstraints >> 4) & 1) << aDesc;
    EXPECT_EQ(spsData.constraint_set4_flag, (aConstraints >> 3) & 1) << aDesc;
    EXPECT_EQ(spsData.constraint_set5_flag, (aConstraints >> 2) & 1) << aDesc;

    EXPECT_EQ(spsData.level_idc, aLevel) << aDesc;
    EXPECT_TRUE(!aSize.IsEmpty());
    EXPECT_EQ(spsData.pic_width, static_cast<uint32_t>(aSize.width)) << aDesc;
    EXPECT_EQ(spsData.pic_height, static_cast<uint32_t>(aSize.height)) << aDesc;
  };

  testOutput(0x42, 0x40, 0x1E, {1920, 1080}, "Constrained Baseline Profile");
  testOutput(0x4D, 0x00, 0x0B, {300, 300}, "Main Profile");
  testOutput(0x64, 0x0C, 0x33, {1280, 720}, "Constrained High Profile");
}

static RefPtr<MediaByteBuffer> BuildSPSWithRawFields(
    uint32_t aPicWidthInMBSMinus1, uint32_t aPicHeightInMapUnitsMinus1,
    bool aFrameMbsOnlyFlag) {
  RefPtr<MediaByteBuffer> sps = new MediaByteBuffer();
  BitWriter bw(sps);
  bw.WriteU8(0);       // profile_idc (no High Profile chroma section)
  bw.WriteU8(0);       // constraint_set flags + reserved
  bw.WriteU8(0);       // level_idc
  bw.WriteUE(0);       // seq_parameter_set_id
  bw.WriteUE(0);       // log2_max_frame_num_minus4
  bw.WriteUE(0);       // pic_order_cnt_type
  bw.WriteUE(0);       // log2_max_pic_order_cnt_lsb_minus4
  bw.WriteUE(0);       // max_num_ref_frames
  bw.WriteBit(false);  // gaps_in_frame_num_value_allowed_flag
  bw.WriteUE(aPicWidthInMBSMinus1);
  bw.WriteUE(aPicHeightInMapUnitsMinus1);
  bw.WriteBit(aFrameMbsOnlyFlag);
  if (!aFrameMbsOnlyFlag) {
    bw.WriteBit(false);  // mb_adaptive_frame_field_flag
  }
  bw.WriteBit(false);  // direct_8x8_inference_flag
  bw.WriteBit(false);  // frame_cropping_flag
  bw.WriteBit(false);  // vui_parameters_present_flag
  bw.CloseWithRbspTrailing();
  return sps;
}

static RefPtr<MediaByteBuffer> BuildSPSWithRawWidthMBS(
    uint32_t aPicWidthInMBSMinus1) {
  return BuildSPSWithRawFields(aPicWidthInMBSMinus1, 44, true);
}

TEST(H264, RejectsInvalidPicWidthMbs)
{
  // pic_width_in_mbs_minus1 = 0x0FFFFFFF makes pic_width_in_mbs * 16 exceed
  // the uint32_t range.
  SPSData spsdata;
  RefPtr<MediaByteBuffer> sps = BuildSPSWithRawWidthMBS(0x0FFFFFFF);
  EXPECT_FALSE(H264::DecodeSPS(sps, spsdata));
}

TEST(H264, RejectsInvalidPicHeightMapUnits)
{
  // pic_height_in_map_units_minus1 above the uint32-safe bound for * 16.
  SPSData spsdata;
  RefPtr<MediaByteBuffer> sps = BuildSPSWithRawFields(79, 0x0FFFFFFF, true);
  EXPECT_FALSE(H264::DecodeSPS(sps, spsdata));
}

TEST(H264, RejectsInvalidPicHeightMapUnitsRegardlessOfFlag)
{
  // Same height value as above but with frame_mbs_only_flag=false in the
  // payload. Verifies rejection happens at the picture-dimension
  // multiplication regardless of the field-coded *= 2 step.
  SPSData spsdata;
  RefPtr<MediaByteBuffer> sps = BuildSPSWithRawFields(79, 0x0FFFFFFF, false);
  EXPECT_FALSE(H264::DecodeSPS(sps, spsdata));
}

TEST(H264, CheckBoundaryPicWidth)
{
  // Largest pic_width_in_mbs_minus1 whose computed pic_width still fits the
  // supported size limit. The helper sets frame_cropping_flag=false, so
  // DecodeSPS computes pic_width = pic_width_in_mbs * 16 with no crop.
  constexpr uint32_t kMaxMbs = std::numeric_limits<int32_t>::max() / 16;
  SPSData spsdata;
  RefPtr<MediaByteBuffer> sps = BuildSPSWithRawWidthMBS(kMaxMbs - 1);
  EXPECT_TRUE(H264::DecodeSPS(sps, spsdata));
  EXPECT_EQ(spsdata.pic_width, kMaxMbs * 16);

  // One macroblock past the boundary makes pic_width exceed the supported
  // size limit (still a valid uint32_t), so DecodeSPS rejects it.
  RefPtr<MediaByteBuffer> tooWide = BuildSPSWithRawWidthMBS(kMaxMbs);
  EXPECT_FALSE(H264::DecodeSPS(tooWide, spsdata));
}

TEST(H264, CheckBoundaryPicHeight)
{
  // Same size limit for the frame-coded path
  // (frame_mbs_only_flag=true): pic_height = pic_height_in_map_units * 16.
  // Field-coded mode (flag=false) has a tighter limit because of the *= 2
  // step; not exercised here.
  constexpr uint32_t kMaxMbs = std::numeric_limits<int32_t>::max() / 16;
  SPSData spsdata;
  RefPtr<MediaByteBuffer> sps = BuildSPSWithRawFields(79, kMaxMbs - 1, true);
  EXPECT_TRUE(H264::DecodeSPS(sps, spsdata));
  EXPECT_EQ(spsdata.pic_height, kMaxMbs * 16);

  // One map-unit past the boundary makes pic_height exceed the supported
  // size limit (still a valid uint32_t), so DecodeSPS rejects it.
  RefPtr<MediaByteBuffer> tooTall = BuildSPSWithRawFields(79, kMaxMbs, true);
  EXPECT_FALSE(H264::DecodeSPS(tooTall, spsdata));
}

TEST(H265, HVCCParsingSuccess)
{
  {
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t hvccBytesBuffer[] = {
        1 /* version */,
        1 /* general_profile_space/general_tier_flag/general_profile_idc */,
        0x60 /* general_profile_compatibility_flags 1/4 */,
        0 /* general_profile_compatibility_flags 2/4 */,
        0 /* general_profile_compatibility_flags 3/4 */,
        0 /* general_profile_compatibility_flags 4/4 */,
        0x90 /* general_constraint_indicator_flags 1/6 */,
        0 /* general_constraint_indicator_flags 2/6 */,
        0 /* general_constraint_indicator_flags 3/6 */,
        0 /* general_constraint_indicator_flags 4/6 */,
        0 /* general_constraint_indicator_flags 5/6 */,
        0 /* general_constraint_indicator_flags 6/6 */,
        0x5A /* general_level_idc */,
        0 /* min_spatial_segmentation_idc 1/2 */,
        0 /* min_spatial_segmentation_idc 2/2 */,
        0 /* parallelismType */,
        1 /* chroma_format_idc */,
        0 /* bit_depth_luma_minus8 */,
        0 /* bit_depth_chroma_minus8 */,
        0 /* avgFrameRate 1/2 */,
        0 /* avgFrameRate 2/2 */,
        0x0F /* constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
              */
        ,
        0 /* numOfArrays */,
    };
    extradata->AppendElements(hvccBytesBuffer, std::size(hvccBytesBuffer));
    auto rv = HVCCConfig::Parse(extradata);
    EXPECT_TRUE(rv.isOk());
    auto hvcc = rv.unwrap();
    EXPECT_EQ(hvcc.configurationVersion, 1);
    EXPECT_EQ(hvcc.general_profile_space, 0);
    EXPECT_EQ(hvcc.general_tier_flag, false);
    EXPECT_EQ(hvcc.general_profile_idc, 1);
    EXPECT_EQ(hvcc.general_profile_compatibility_flags, (uint32_t)0x60000000);
    EXPECT_EQ(hvcc.general_constraint_indicator_flags,
              (uint64_t)0x900000000000);
    EXPECT_EQ(hvcc.general_level_idc, 0x5A);
    EXPECT_EQ(hvcc.min_spatial_segmentation_idc, 0);
    EXPECT_EQ(hvcc.parallelismType, 0);
    EXPECT_EQ(hvcc.chroma_format_idc, 1);
    EXPECT_EQ(hvcc.bit_depth_luma_minus8, 0);
    EXPECT_EQ(hvcc.bit_depth_chroma_minus8, 0);
    EXPECT_EQ(hvcc.avgFrameRate, 0);
    EXPECT_EQ(hvcc.constantFrameRate, 0);
    EXPECT_EQ(hvcc.numTemporalLayers, 1);
    EXPECT_EQ(hvcc.temporalIdNested, true);
    EXPECT_EQ(hvcc.NALUSize(), 4);
    EXPECT_EQ(hvcc.mNALUs.Length(), uint32_t(0));
  }
  {
    // Multple NALUs
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t hvccBytesBuffer[] = {
        1 /* version */,
        1 /* general_profile_space/general_tier_flag/general_profile_idc */,
        0x60 /* general_profile_compatibility_flags 1/4 */,
        0 /* general_profile_compatibility_flags 2/4 */,
        0 /* general_profile_compatibility_flags 3/4 */,
        0 /* general_profile_compatibility_flags 4/4 */,
        0x90 /* general_constraint_indicator_flags 1/6 */,
        0 /* general_constraint_indicator_flags 2/6 */,
        0 /* general_constraint_indicator_flags 3/6 */,
        0 /* general_constraint_indicator_flags 4/6 */,
        0 /* general_constraint_indicator_flags 5/6 */,
        0 /* general_constraint_indicator_flags 6/6 */,
        0x5A /* general_level_idc */,
        0 /* min_spatial_segmentation_idc 1/2 */,
        0 /* min_spatial_segmentation_idc 2/2 */,
        0 /* parallelismType */,
        1 /* chroma_format_idc */,
        0 /* bit_depth_luma_minus8 */,
        0 /* bit_depth_chroma_minus8 */,
        0 /* avgFrameRate 1/2 */,
        0 /* avgFrameRate 2/2 */,
        0x0F /* constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
              */
        ,
        2 /* numOfArrays */,
        /* SPS Array */
        0x21 /* NAL_unit_type (SPS) */,
        0 /* numNalus 1/2 */,
        1 /* numNalus 2/2 */,

        /* SPS */
        0 /* nalUnitLength 1/2 */,
        8 /* nalUnitLength 2/2 (header + rsbp) */,
        0x42 /* NALU header 1/2 */,
        0 /* NALU header 2/2 */,
        0 /* rbsp 1/6 */,
        0 /* rbsp 2/6 */,
        0 /* rbsp 3/6 */,
        0 /* rbsp 4/6 */,
        0 /* rbsp 5/6 */,
        0 /* rbsp 6/6 */,

        /* PPS Array */
        0x22 /* NAL_unit_type (PPS) */,
        0 /* numNalus 1/2 */,
        2 /* numNalus 2/2 */,

        /* PPS 1 */
        0 /* nalUnitLength 1/2 */,
        3 /* nalUnitLength 2/2 (header + rsbp) */,
        0x44 /* NALU header 1/2 */,
        0 /* NALU header 2/2 */,
        0 /* rbsp */,

        /* PPS 2 */
        0 /* nalUnitLength 1/2 */,
        3 /* nalUnitLength 2/2 (header + rsbp) */,
        0x44 /* NALU header 1/2 */,
        0 /* NALU header 2/2 */,
        0 /* rbsp */,
    };
    extradata->AppendElements(hvccBytesBuffer, std::size(hvccBytesBuffer));
    auto rv = HVCCConfig::Parse(extradata);
    EXPECT_TRUE(rv.isOk());
    auto hvcc = rv.unwrap();
    // Check NALU, it should contain 1 SPS and 2 PPS.
    EXPECT_EQ(hvcc.mNALUs.Length(), uint32_t(3));
    EXPECT_EQ(hvcc.mNALUs[0].mNalUnitType, H265NALU::NAL_TYPES::SPS_NUT);
    EXPECT_EQ(hvcc.mNALUs[0].mNuhLayerId, 0);
    EXPECT_EQ(hvcc.mNALUs[0].mNuhTemporalIdPlus1, 0);
    EXPECT_EQ(hvcc.mNALUs[0].IsSPS(), true);
    EXPECT_EQ(hvcc.mNALUs[0].mNALU.Length(), 8u);

    EXPECT_EQ(hvcc.mNALUs[1].mNalUnitType, H265NALU::NAL_TYPES::PPS_NUT);
    EXPECT_EQ(hvcc.mNALUs[1].mNuhLayerId, 0);
    EXPECT_EQ(hvcc.mNALUs[1].mNuhTemporalIdPlus1, 0);
    EXPECT_EQ(hvcc.mNALUs[1].IsSPS(), false);
    EXPECT_EQ(hvcc.mNALUs[1].mNALU.Length(), 3u);

    EXPECT_EQ(hvcc.mNALUs[2].mNalUnitType, H265NALU::NAL_TYPES::PPS_NUT);
    EXPECT_EQ(hvcc.mNALUs[2].mNuhLayerId, 0);
    EXPECT_EQ(hvcc.mNALUs[2].mNuhTemporalIdPlus1, 0);
    EXPECT_EQ(hvcc.mNALUs[2].IsSPS(), false);
    EXPECT_EQ(hvcc.mNALUs[2].mNALU.Length(), 3u);
  }
}

TEST(H265, HVCCParsingFailure)
{
  {
    // Incorrect version
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t hvccBytesBuffer[] = {
        2 /* version */,
        1 /* general_profile_space/general_tier_flag/general_profile_idc */,
        0x60 /* general_profile_compatibility_flags 1/4 */,
        0 /* general_profile_compatibility_flags 2/4 */,
        0 /* general_profile_compatibility_flags 3/4 */,
        0 /* general_profile_compatibility_flags 4/4 */,
        0x90 /* general_constraint_indicator_flags 1/6 */,
        0 /* general_constraint_indicator_flags 2/6 */,
        0 /* general_constraint_indicator_flags 3/6 */,
        0 /* general_constraint_indicator_flags 4/6 */,
        0 /* general_constraint_indicator_flags 5/6 */,
        0 /* general_constraint_indicator_flags 6/6 */,
        0x5A /* general_level_idc */,
        0 /* min_spatial_segmentation_idc 1/2 */,
        0 /* min_spatial_segmentation_idc 2/2 */,
        0 /* parallelismType */,
        1 /* chroma_format_idc */,
        0 /* bit_depth_luma_minus8 */,
        0 /* bit_depth_chroma_minus8 */,
        0 /* avgFrameRate 1/2 */,
        0 /* avgFrameRate 2/2 */,
        0x0F /* constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
              */
        ,
        0 /* numOfArrays */,
    };
    extradata->AppendElements(hvccBytesBuffer, std::size(hvccBytesBuffer));
    auto avcc = HVCCConfig::Parse(extradata);
    EXPECT_TRUE(avcc.isErr());
  }
  {
    // Insuffient data
    auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
    uint8_t hvccBytesBuffer[] = {
        1 /* version */,
        1 /* general_profile_space/general_tier_flag/general_profile_idc */,
        0x60 /* general_profile_compatibility_flags 1/4 */,
        0 /* general_profile_compatibility_flags 2/4 */,
        0 /* general_profile_compatibility_flags 3/4 */,
        0 /* general_profile_compatibility_flags 4/4 */,
        0x90 /* general_constraint_indicator_flags 1/6 */,
        0 /* general_constraint_indicator_flags 2/6 */,
        0 /* general_constraint_indicator_flags 3/6 */,
        0 /* general_constraint_indicator_flags 4/6 */,
        0 /* general_constraint_indicator_flags 5/6 */,
        0 /* general_constraint_indicator_flags 6/6 */,
        0x5A /* general_level_idc */
    };
    extradata->AppendElements(hvccBytesBuffer, std::size(hvccBytesBuffer));
    auto avcc = HVCCConfig::Parse(extradata);
    EXPECT_TRUE(avcc.isErr());
  }
}

TEST(H265, HVCCToAnnexB)
{
  auto extradata = MakeRefPtr<mozilla::MediaByteBuffer>();
  uint8_t hvccBytesBuffer[] = {
      1 /* version */,
      1 /* general_profile_space/general_tier_flag/general_profile_idc */,
      0x60 /* general_profile_compatibility_flags 1/4 */,
      0 /* general_profile_compatibility_flags 2/4 */,
      0 /* general_profile_compatibility_flags 3/4 */,
      0 /* general_profile_compatibility_flags 4/4 */,
      0x90 /* general_constraint_indicator_flags 1/6 */,
      0 /* general_constraint_indicator_flags 2/6 */,
      0 /* general_constraint_indicator_flags 3/6 */,
      0 /* general_constraint_indicator_flags 4/6 */,
      0 /* general_constraint_indicator_flags 5/6 */,
      0 /* general_constraint_indicator_flags 6/6 */,
      0x5A /* general_level_idc */,
      0 /* min_spatial_segmentation_idc 1/2 */,
      0 /* min_spatial_segmentation_idc 2/2 */,
      0 /* parallelismType */,
      1 /* chroma_format_idc */,
      0 /* bit_depth_luma_minus8 */,
      0 /* bit_depth_chroma_minus8 */,
      0 /* avgFrameRate 1/2 */,
      0 /* avgFrameRate 2/2 */,
      0x0F /* constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
            */
      ,
      2 /* numOfArrays */,
      /* SPS Array */
      0x21 /* NAL_unit_type (SPS) */,
      0 /* numNalus 1/2 */,
      1 /* numNalus 2/2 */,

      /* SPS */
      0 /* nalUnitLength 1/2 */,
      3 /* nalUnitLength 2/2 (header + rsbp) */,
      0x42 /* NALU header 1/2 */,
      0 /* NALU header 2/2 */,
      0 /* rbsp */,

      /* PPS Array */
      0x22 /* NAL_unit_type (PPS) */,
      0 /* numNalus 1/2 */,
      1 /* numNalus 2/2 */,

      /* PPS */
      0 /* nalUnitLength 1/2 */,
      3 /* nalUnitLength 2/2 (header + rsbp) */,
      0x44 /* NALU header 1/2 */,
      0 /* NALU header 2/2 */,
      0 /* rbsp */,
  };
  extradata->AppendElements(hvccBytesBuffer, std::size(hvccBytesBuffer));

  // We convert hvcc extra-data to annexb format, then parse each nalu to see if
  // they are still correct or not.
  const size_t naluBytesSize = 3;  // NAL size is 3, see nalUnitLength above
  const size_t delimiterBytesSize = 4;  // 0x00000001
  const size_t naluPlusDelimiterBytesSize = naluBytesSize + delimiterBytesSize;
  RefPtr<mozilla::MediaByteBuffer> annexBExtraData =
      AnnexB::ConvertHVCCExtraDataToAnnexB(extradata);
  // 2 NALU, sps and pps
  EXPECT_EQ(annexBExtraData->Length(), naluPlusDelimiterBytesSize * 2);

  H265NALU sps(
      static_cast<uint8_t*>(annexBExtraData->Elements() + delimiterBytesSize),
      naluBytesSize);
  EXPECT_EQ(sps.mNalUnitType, H265NALU::NAL_TYPES::SPS_NUT);
  EXPECT_EQ(sps.mNuhLayerId, 0);
  EXPECT_EQ(sps.mNuhTemporalIdPlus1, 0);
  EXPECT_EQ(sps.IsSPS(), true);
  EXPECT_EQ(sps.mNALU.Length(), 3u);

  H265NALU pps(
      static_cast<uint8_t*>(annexBExtraData->Elements() +
                            naluPlusDelimiterBytesSize + delimiterBytesSize),
      naluBytesSize);
  EXPECT_EQ(pps.mNalUnitType, H265NALU::NAL_TYPES::PPS_NUT);
  EXPECT_EQ(pps.mNuhLayerId, 0);
  EXPECT_EQ(pps.mNuhTemporalIdPlus1, 0);
  EXPECT_EQ(pps.IsSPS(), false);
  EXPECT_EQ(pps.mNALU.Length(), 3u);
}

TEST(H265, HVCCToAnnexBExtraDataWithSmallPrefixSEI)
{
  RefPtr<MediaByteBuffer> extradata = GetHvccExtraDataWithPrefixSEI(137, 32);
  RefPtr<MediaByteBuffer> annexBExtraData =
      AnnexB::ConvertHVCCExtraDataToAnnexB(extradata);

  const size_t delimiterBytesSize = 4;
  // sHvccBytesBuffer has SPS (8 bytes) and PPS (3 bytes).
  // 37 = 2 (NALU header) + 1 (payloadType) + 1 (payloadSize) + 32 (payload)
  //      + 1 (RBSP stop bit)
  const size_t expectedLength = (delimiterBytesSize + 8) +
                                (delimiterBytesSize + 3) +
                                (delimiterBytesSize + 37);
  EXPECT_EQ(annexBExtraData->Length(), expectedLength);
}

TEST(H265, HVCCToAnnexBExtraDataSkipsUserDataUnregisteredPrefixSEIOnWindows)
{
  RefPtr<MediaByteBuffer> extradata = GetHvccExtraDataWithPrefixSEI(5, 32);
  RefPtr<MediaByteBuffer> annexBExtraData =
      AnnexB::ConvertHVCCExtraDataToAnnexB(extradata);

  const size_t delimiterBytesSize = 4;
  const size_t spsAndPpsLength =
      (delimiterBytesSize + 8) + (delimiterBytesSize + 3);
#ifdef MOZ_WMF
  EXPECT_EQ(annexBExtraData->Length(), spsAndPpsLength);
#else
  EXPECT_EQ(annexBExtraData->Length(),
            spsAndPpsLength + delimiterBytesSize + 37);
#endif
}

TEST(H265, AnnexBToHVCC)
{
  RefPtr<MediaRawData> rawData{GetHVCCSample(128)};
  RefPtr<MediaRawData> rawDataClone = rawData->Clone();
  Result<Ok, nsresult> result =
      AnnexB::ConvertHVCCSampleToAnnexB(rawDataClone, /* aAddSps */ false);
  EXPECT_TRUE(result.isOk()) << "HVCC to AnnexB Conversion should succeed";
  EXPECT_TRUE(AnnexB::IsAnnexB(*rawDataClone))
      << "The sample should be AnnexB following conversion";

  auto rv = AnnexB::ConvertSampleToHVCC(rawDataClone);
  EXPECT_TRUE(rv.isOk()) << "AnnexB to HVCC Conversion should succeed";
  EXPECT_TRUE(AnnexB::IsHVCC(rawDataClone))
      << "The sample should be HVCC following conversion";
}

// This is SPS from 'hevc_white_frame.mp4'
static const uint8_t sSps[] = {
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0xa0, 0x02, 0x00, 0x80,
    0x30, 0x16, 0x59, 0x59, 0xa4, 0x93, 0x2b, 0xc0, 0x5a, 0x02, 0x00,
    0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x3c, 0x10};

// This is VPS from 'hevc_white_frame.mp4'
static const uint8_t sVps[] = {0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFF, 0x01, 0x60,
                               0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
                               0x00, 0x00, 0x03, 0x00, 0x3F, 0x95, 0x98, 0x09};

// This is PPS from 'hevc_white_frame.mp4'
static const uint8_t sPps[] = {0x44, 0x01, 0xC1, 0x72, 0xB4, 0x62, 0x40};

TEST(H265, ExtractHVCCExtraData)
{
  RefPtr<MediaRawData> rawData{GetHVCCSample(sSps, std::size(sSps))};
  RefPtr<MediaByteBuffer> extradata = H265::ExtractHVCCExtraData(rawData);
  EXPECT_TRUE(extradata);
  auto rv = HVCCConfig::Parse(extradata);
  EXPECT_TRUE(rv.isOk());
  auto hvcc = rv.unwrap();
  EXPECT_EQ(hvcc.mNALUs.Length(), 1u);
  EXPECT_EQ(hvcc.mNALUs[0].mNalUnitType, H265NALU::NAL_TYPES::SPS_NUT);
  EXPECT_EQ(hvcc.mNALUs[0].mNuhLayerId, 0u);
  EXPECT_EQ(hvcc.mNALUs[0].mNuhTemporalIdPlus1, 1);
  EXPECT_EQ(hvcc.mNALUs[0].IsSPS(), true);
  EXPECT_EQ(hvcc.mNALUs[0].mNALU.Length(), 43u);

  nsTArray<Span<const uint8_t>> nalus;
  nalus.AppendElement(Span<const uint8_t>{sSps, std::size(sSps)});
  nalus.AppendElement(Span<const uint8_t>{sVps, std::size(sVps)});
  nalus.AppendElement(Span<const uint8_t>{sPps, std::size(sPps)});

  RefPtr<MediaRawData> rawData2{GetHVCCSamples(nalus)};
  RefPtr<MediaByteBuffer> extradata2 = H265::ExtractHVCCExtraData(rawData2);
  EXPECT_TRUE(extradata2);
  auto rv2 = HVCCConfig::Parse(extradata2);
  EXPECT_TRUE(rv2.isOk());
  auto hvcc2 = rv2.unwrap();
  EXPECT_EQ(hvcc2.mNALUs.Length(), 3u);

  EXPECT_EQ(hvcc2.mNALUs[0].mNalUnitType, H265NALU::NAL_TYPES::VPS_NUT);
  EXPECT_EQ(hvcc2.mNALUs[0].mNuhLayerId, 0u);
  EXPECT_EQ(hvcc2.mNALUs[0].mNuhTemporalIdPlus1, 1);
  EXPECT_EQ(hvcc2.mNALUs[0].IsVPS(), true);
  EXPECT_EQ(hvcc2.mNALUs[0].mNALU.Length(), std::size(sVps));

  EXPECT_EQ(hvcc2.mNALUs[1].mNalUnitType, H265NALU::NAL_TYPES::SPS_NUT);
  EXPECT_EQ(hvcc2.mNALUs[1].mNuhLayerId, 0u);
  EXPECT_EQ(hvcc2.mNALUs[1].mNuhTemporalIdPlus1, 1);
  EXPECT_EQ(hvcc2.mNALUs[1].IsSPS(), true);
  EXPECT_EQ(hvcc2.mNALUs[1].mNALU.Length(), std::size(sSps));

  EXPECT_EQ(hvcc2.mNALUs[2].mNalUnitType, H265NALU::NAL_TYPES::PPS_NUT);
  EXPECT_EQ(hvcc2.mNALUs[2].mNuhLayerId, 0u);
  EXPECT_EQ(hvcc2.mNALUs[2].mNuhTemporalIdPlus1, 1);
  EXPECT_EQ(hvcc2.mNALUs[2].IsPPS(), true);
  EXPECT_EQ(hvcc2.mNALUs[2].mNALU.Length(), std::size(sPps));
}

TEST(H265, DecodeSPSFromSPSNALU)
{
  H265NALU nalu{sSps, std::size(sSps)};
  auto rv = H265::DecodeSPSFromSPSNALU(nalu);
  EXPECT_TRUE(rv.isOk());
  auto sps = rv.unwrap();
  // Examine the value by using HEVCESBrowser.
  EXPECT_EQ(sps.sps_video_parameter_set_id, 0u);
  EXPECT_EQ(sps.sps_max_sub_layers_minus1, 0u);
  EXPECT_EQ(sps.sps_temporal_id_nesting_flag, 1);
  EXPECT_EQ(sps.profile_tier_level.general_profile_space, 0u);
  EXPECT_EQ(sps.profile_tier_level.general_tier_flag, false);
  EXPECT_EQ(sps.profile_tier_level.general_profile_idc, 1u);
  EXPECT_EQ(sps.profile_tier_level.general_profile_compatibility_flags,
            0x60000000u);
  EXPECT_EQ(sps.profile_tier_level.general_progressive_source_flag, true);
  EXPECT_EQ(sps.profile_tier_level.general_interlaced_source_flag, false);
  EXPECT_EQ(sps.profile_tier_level.general_non_packed_constraint_flag, false);
  EXPECT_EQ(sps.profile_tier_level.general_frame_only_constraint_flag, true);
  EXPECT_EQ(sps.profile_tier_level.general_level_idc, 93u);
  EXPECT_EQ(sps.sps_seq_parameter_set_id, 0u);
  EXPECT_EQ(sps.chroma_format_idc, 1u);
  EXPECT_EQ(sps.separate_colour_plane_flag, false);
  EXPECT_EQ(sps.pic_width_in_luma_samples, 1024u);
  EXPECT_EQ(sps.pic_height_in_luma_samples, 768u);
  EXPECT_EQ(sps.conformance_window_flag, false);
  EXPECT_EQ(sps.bit_depth_luma_minus8, 0u);
  EXPECT_EQ(sps.bit_depth_chroma_minus8, 0u);
  EXPECT_EQ(sps.log2_max_pic_order_cnt_lsb_minus4, 4u);
  EXPECT_EQ(sps.sps_sub_layer_ordering_info_present_flag, true);
  EXPECT_EQ(sps.sps_max_dec_pic_buffering_minus1[0], 4u);
  EXPECT_EQ(sps.sps_max_num_reorder_pics[0], 2u);
  EXPECT_EQ(sps.sps_max_latency_increase_plus1[0], 5u);
  EXPECT_EQ(sps.log2_min_luma_coding_block_size_minus3, 0u);
  EXPECT_EQ(sps.log2_diff_max_min_luma_coding_block_size, 3u);
  EXPECT_EQ(sps.log2_min_luma_transform_block_size_minus2, 0u);
  EXPECT_EQ(sps.log2_diff_max_min_luma_transform_block_size, 3u);
  EXPECT_EQ(sps.max_transform_hierarchy_depth_inter, 0u);
  EXPECT_EQ(sps.max_transform_hierarchy_depth_inter, 0u);
  EXPECT_EQ(sps.pcm_enabled_flag, false);
  EXPECT_EQ(sps.num_short_term_ref_pic_sets, 0u);
  EXPECT_EQ(sps.sps_temporal_mvp_enabled_flag, true);
  EXPECT_EQ(sps.strong_intra_smoothing_enabled_flag, true);
  EXPECT_TRUE(sps.vui_parameters);
  EXPECT_EQ(sps.vui_parameters->video_full_range_flag, false);

  // Test public methods
  EXPECT_EQ(sps.BitDepthLuma(), 8u);
  EXPECT_EQ(sps.BitDepthChroma(), 8u);
  const auto imgSize = sps.GetImageSize();
  EXPECT_EQ(imgSize.Width(), 1024);
  EXPECT_EQ(imgSize.Height(), 768);
  const auto disSize = sps.GetDisplaySize();
  EXPECT_EQ(disSize, imgSize);
  EXPECT_EQ(sps.ColorDepth(), gfx::ColorDepth::COLOR_8);
  EXPECT_EQ(sps.ColorSpace(), gfx::YUVColorSpace::BT709);
  EXPECT_EQ(sps.IsFullColorRange(), false);
  EXPECT_EQ(sps.ColorPrimaries(), 2u);
  EXPECT_EQ(sps.TransferFunction(), 2u);
}

TEST(H265, SPSIteratorAndCreateNewExtraData)
{
  // The fake extradata has 3 NALUs (1 vps, 1 sps and 1 pps).
  RefPtr<MediaByteBuffer> extradata = H265::CreateFakeExtraData();
  EXPECT_TRUE(extradata);
  auto rv = HVCCConfig::Parse(extradata);
  EXPECT_TRUE(rv.isOk());
  auto hvcc = rv.unwrap();
  EXPECT_EQ(hvcc.mNALUs.Length(), 3u);
  EXPECT_EQ(hvcc.NumSPS(), 1u);

  // SPSIterator should be able to access the SPS
  SPSIterator it(hvcc);
  auto* sps = *it;
  EXPECT_TRUE(sps);

  // This SPS should match the one retrieved from the HVCC.
  auto spsMaybe = hvcc.GetFirstAvaiableNALU(H265NALU::NAL_TYPES::SPS_NUT);
  EXPECT_TRUE(spsMaybe);
  auto rv1 = H265::DecodeSPSFromSPSNALU(*sps);
  auto rv2 = H265::DecodeSPSFromSPSNALU(spsMaybe.ref());
  EXPECT_TRUE(rv1.isOk());
  EXPECT_TRUE(rv2.isOk());
  EXPECT_EQ(rv1.unwrap(), rv2.unwrap());

  // The iterator becomes invalid after advancing, as there is only one SPS.
  EXPECT_FALSE(*(++it));

  // Retrieve other NALUs to test the creation of new extradata.
  auto ppsMaybe = hvcc.GetFirstAvaiableNALU(H265NALU::NAL_TYPES::PPS_NUT);
  EXPECT_TRUE(ppsMaybe);
  auto vpsMaybe = hvcc.GetFirstAvaiableNALU(H265NALU::NAL_TYPES::VPS_NUT);
  EXPECT_TRUE(vpsMaybe);
  nsTArray<H265NALU> nalus;
  nalus.AppendElement(*spsMaybe);
  nalus.AppendElement(*ppsMaybe);
  nalus.AppendElement(*vpsMaybe);
  RefPtr<MediaByteBuffer> newExtradata = H265::CreateNewExtraData(hvcc, nalus);
  EXPECT_TRUE(newExtradata);

  // The new extradata should match the original extradata.
  auto rv3 = HVCCConfig::Parse(extradata);
  EXPECT_TRUE(rv3.isOk());
  auto hvcc2 = rv3.unwrap();
  EXPECT_EQ(hvcc.mNALUs.Length(), hvcc2.mNALUs.Length());
  EXPECT_EQ(hvcc.NumSPS(), hvcc2.NumSPS());
}

TEST(H265, ConfWindowTest)
{
  // This sps contains some cropping information, which will crop video from the
  // resolution 3840x2176 to 3840x2160.
  static const uint8_t sSpsConfWindow[] = {
      0x42, 0x01, 0x01, 0x01, 0x40, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x99, 0xA0, 0x01, 0xE0, 0x20, 0x02, 0x20, 0x7C, 0x4E, 0x59,
      0x95, 0x29, 0x08, 0x46, 0x46, 0xFF, 0xC3, 0x01, 0x6A, 0x02, 0x02, 0x02,
      0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0xE3, 0x00, 0x2E, 0xF2,
      0x88, 0x00, 0x02, 0x62, 0x5A, 0x00, 0x00, 0x13, 0x12, 0xD0, 0x20};

  H265NALU nalu{sSpsConfWindow, std::size(sSpsConfWindow)};
  auto rv = H265::DecodeSPSFromSPSNALU(nalu);
  EXPECT_TRUE(rv.isOk());
  auto sps = rv.unwrap();
  EXPECT_EQ(sps.chroma_format_idc, 1u);
  EXPECT_EQ(sps.pic_width_in_luma_samples, 3840u);
  EXPECT_EQ(sps.pic_height_in_luma_samples, 2176u);
  EXPECT_EQ(sps.conformance_window_flag, true);
  EXPECT_EQ(sps.conf_win_left_offset, 0u);
  EXPECT_EQ(sps.conf_win_right_offset, 0u);
  EXPECT_EQ(sps.conf_win_top_offset, 0u);
  EXPECT_EQ(sps.conf_win_bottom_offset, 8u);

  const auto imgSize = sps.GetImageSize();
  EXPECT_EQ(imgSize.Width(), 3840);
  EXPECT_EQ(imgSize.Height(), 2160);  // cropped height

  const auto disSize = sps.GetDisplaySize();
  EXPECT_EQ(disSize, imgSize);
}

TEST(H265, ColorPrimariesTest)
{
  // This sps contains a BT2020 color primaries information.
  static const uint8_t sSPSColorPrimariesBT2020[] = {
      0x42, 0x01, 0x01, 0x02, 0x20, 0x00, 0x00, 0x03, 0x00, 0xB0, 0x00, 0x00,
      0x03, 0x00, 0x00, 0x03, 0x00, 0xB4, 0xA0, 0x01, 0xF8, 0x20, 0x02, 0xF4,
      0x4D, 0x88, 0x17, 0xB9, 0x16, 0x55, 0x35, 0x09, 0x10, 0x09, 0x00, 0x80};

  H265NALU nalu{sSPSColorPrimariesBT2020, std::size(sSPSColorPrimariesBT2020)};
  auto rv = H265::DecodeSPSFromSPSNALU(nalu);
  EXPECT_TRUE(rv.isOk());
  auto sps = rv.unwrap();
  EXPECT_EQ(sps.ColorPrimaries(), 9 /* CP_BT2020 */);
}

static constexpr float kSEIPrimariesDivisor = 50000.0f;

static void AppendU16BE(nsTArray<uint8_t>& aDest, uint16_t aValue) {
  aDest.AppendElement(static_cast<uint8_t>(aValue >> 8));
  aDest.AppendElement(static_cast<uint8_t>(aValue & 0xff));
}

static void AppendU32BE(nsTArray<uint8_t>& aDest, uint32_t aValue) {
  aDest.AppendElement(static_cast<uint8_t>(aValue >> 24));
  aDest.AppendElement(static_cast<uint8_t>((aValue >> 16) & 0xff));
  aDest.AppendElement(static_cast<uint8_t>((aValue >> 8) & 0xff));
  aDest.AppendElement(static_cast<uint8_t>(aValue & 0xff));
}

// Build a PREFIX_SEI_NUT NALU with SEI type 137 (mastering display) and/or
// type 144 (CLL) payloads depending on the flags.
// Values: primaries raw=50000 → 1.0f, luminance raw=10000 → 1.0f.
static nsTArray<uint8_t> BuildSEINALU(bool aInclude137, bool aInclude144) {
  nsTArray<uint8_t> nalu;
  // 2-byte NALU header: nal_unit_type=39 (PREFIX_SEI_NUT), nuh_temporal_id=1
  nalu.AppendElement(0x4E);  // (39 << 1) | 0
  nalu.AppendElement(0x01);  // nuh_temporal_id_plus1=1

  if (aInclude137) {
    // SEI type 137 (mastering display colour volume), payload size 24
    nalu.AppendElement(137);  // payloadType
    nalu.AppendElement(24);   // payloadSize
    // H.265 primary order: G[0], B[1], R[2] — values chosen as 50000 = 1.0f
    AppendU16BE(nalu, 50000);     // G x
    AppendU16BE(nalu, 25000);     // G y
    AppendU16BE(nalu, 15000);     // B x
    AppendU16BE(nalu, 7500);      // B y
    AppendU16BE(nalu, 35000);     // R x
    AppendU16BE(nalu, 17500);     // R y
    AppendU16BE(nalu, 15635);     // white point x
    AppendU16BE(nalu, 16450);     // white point y
    AppendU32BE(nalu, 10000000);  // max luminance (10000000/10000 = 1000.0f)
    AppendU32BE(nalu, 100);       // min luminance (100/10000 = 0.01f)
  }

  if (aInclude144) {
    // SEI type 144 (content light level), payload size 4
    nalu.AppendElement(144);  // payloadType
    nalu.AppendElement(4);    // payloadSize
    AppendU16BE(nalu, 1000);  // max CLL
    AppendU16BE(nalu, 400);   // max FALL
  }

  // RBSP trailing bits
  nalu.AppendElement(0x80);
  return nalu;
}

TEST(H265, ParseSEIHDRMetadata_Type137Only)
{
  auto naluData = BuildSEINALU(true, false);
  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  ASSERT_TRUE(result.isSome());
  const auto& hdr = result.value();

  ASSERT_TRUE(hdr.mSmpte2086.isSome());
  const auto& s = hdr.mSmpte2086.value();
  // R primaries (raw G[0]=50000,25000 → Green; B[1]=15000,7500 → Blue;
  //              R[2]=35000,17500 → Red — remapped per H.265 §D.3.4 Table D.2)
  EXPECT_FLOAT_EQ(s.displayPrimaryRed.x, 35000.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.displayPrimaryRed.y, 17500.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.displayPrimaryGreen.x, 50000.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.displayPrimaryGreen.y, 25000.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.displayPrimaryBlue.x, 15000.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.displayPrimaryBlue.y, 7500.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.whitePoint.x, 15635.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.whitePoint.y, 16450.0f / kSEIPrimariesDivisor);
  EXPECT_FLOAT_EQ(s.maxLuminance, 1000.0f);
  EXPECT_FLOAT_EQ(s.minLuminance, 0.01f);
  EXPECT_TRUE(hdr.mContentLightLevel.isNothing());
}

TEST(H265, ParseSEIHDRMetadata_Type144Only)
{
  auto naluData = BuildSEINALU(false, true);
  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  ASSERT_TRUE(result.isSome());
  const auto& hdr = result.value();

  EXPECT_TRUE(hdr.mSmpte2086.isNothing());
  ASSERT_TRUE(hdr.mContentLightLevel.isSome());
  EXPECT_EQ(hdr.mContentLightLevel->maxContentLightLevel, 1000u);
  EXPECT_EQ(hdr.mContentLightLevel->maxFrameAverageLightLevel, 400u);
}

TEST(H265, ParseSEIHDRMetadata_Type137And144)
{
  auto naluData = BuildSEINALU(true, true);
  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  ASSERT_TRUE(result.isSome());
  const auto& hdr = result.value();

  EXPECT_TRUE(hdr.mSmpte2086.isSome());
  EXPECT_TRUE(hdr.mContentLightLevel.isSome());
}

TEST(H265, ParseSEIHDRMetadata_NoHDRSEI)
{
  // A NALU with only user_data_unregistered (type 5) — no HDR SEI types
  nsTArray<uint8_t> naluData;
  naluData.AppendElement(0x4E);  // PREFIX_SEI_NUT header byte 1
  naluData.AppendElement(0x01);  // header byte 2
  naluData.AppendElement(5);     // payloadType = user_data_unregistered
  naluData.AppendElement(2);     // payloadSize = 2
  naluData.AppendElement(0xDE);
  naluData.AppendElement(0xAD);
  naluData.AppendElement(0x80);  // RBSP trailing bits

  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  EXPECT_TRUE(result.isNothing());
}

TEST(H265, ParseSEIHDRMetadata_WrongPayloadSize)
{
  // SEI type 137 with wrong payload size (not 24) — should be skipped.
  nsTArray<uint8_t> naluData;
  naluData.AppendElement(0x4E);
  naluData.AppendElement(0x01);
  naluData.AppendElement(137);  // payloadType = mastering display
  naluData.AppendElement(5);    // wrong payloadSize (should be 24)
  naluData.AppendElement(0x00);
  naluData.AppendElement(0x01);
  naluData.AppendElement(0x02);
  naluData.AppendElement(0x03);
  naluData.AppendElement(0x04);
  naluData.AppendElement(0x80);  // RBSP trailing bits

  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  EXPECT_TRUE(result.isNothing());
}

TEST(H265, ParseSEIHDRMetadata_WrongCLLPayloadSize)
{
  // SEI type 144 with wrong payload size (not 4) — should be skipped.
  nsTArray<uint8_t> naluData;
  naluData.AppendElement(0x4E);
  naluData.AppendElement(0x01);
  naluData.AppendElement(144);  // payloadType = content light level
  naluData.AppendElement(2);    // wrong payloadSize (should be 4)
  naluData.AppendElement(0x03);
  naluData.AppendElement(0xE8);
  naluData.AppendElement(0x80);  // RBSP trailing bits

  H265NALU nalu{naluData.Elements(), static_cast<uint32_t>(naluData.Length())};
  auto result = H265::ParseSEIHDRMetadata(nalu);
  EXPECT_TRUE(result.isNothing());
}

TEST(H265, CompareSPSLessConfigs)
{
  auto base = MakeRefPtr<MediaByteBuffer>();
  base->AppendElements(sHvccBytesBuffer, std::size(sHvccBytesBuffer));
  auto baseRv = HVCCConfig::Parse(base);
  ASSERT_TRUE(baseRv.isOk());
  const HVCCConfig hvcc = baseRv.unwrap();

  RefPtr<MediaByteBuffer> extraData1 = H265::CreateNewExtraData(hvcc, {});
  RefPtr<MediaByteBuffer> extraData2 = H265::CreateNewExtraData(hvcc, {});
  ASSERT_NE(extraData1.get(), extraData2.get());

  auto config1 = HVCCConfig::Parse(extraData1);
  auto config2 = HVCCConfig::Parse(extraData2);
  ASSERT_TRUE(config1.isOk());
  ASSERT_TRUE(config2.isOk());
  EXPECT_EQ(config1.unwrap().NumSPS(), 0u);
  EXPECT_EQ(config2.unwrap().NumSPS(), 0u);

  EXPECT_TRUE(H265::CompareExtraData(extraData1, extraData2));
}

}  // namespace mozilla
