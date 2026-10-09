/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include "WebMBufferedParser.h"
#include "gtest/gtest.h"
#include "nsTArray.h"

using namespace mozilla;

std::ostream& operator<<(std::ostream& aStream, nsresult aResult) {
  return aStream << GetStaticErrorName(aResult);
}

namespace mozilla {
std::ostream& operator<<(std::ostream& aStream, const MediaResult& aResult) {
  aStream << aResult.Code();
  if (!aResult.Message().IsEmpty()) {
    aStream << " (" << aResult.Message() << ")";
  }
  return aStream;
}
}  // namespace mozilla

// "test.webm" contains 8 SimpleBlocks in a single Cluster.  The blocks with
// timecodes 100000000 and are 133000000 skipped by WebMBufferedParser
// because they occur after a block with timecode 160000000 and the parser
// expects in-order timecodes per the WebM spec.  The remaining 6
// SimpleBlocks have the following attributes:
static const uint64_t gTimecodes[] = {66000000,  160000000, 166000000,
                                      200000000, 233000000, 320000000};
static const int64_t gEndOffsets[] = {466, 737, 1209, 1345, 1508, 1980};

TEST(WebMBuffered, BasicTests)
{
  WebMBufferedParser parser(0);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(nullptr, 0, mapping), NS_OK);
  EXPECT_TRUE(mapping.IsEmpty());
  EXPECT_EQ(parser.mStartOffset, 0);
  EXPECT_EQ(parser.mCurrentOffset, 0);

  unsigned char buf[] = {0x1a, 0x45, 0xdf, 0xa3};
  EXPECT_EQ(parser.Append(buf, std::size(buf), mapping), NS_OK);
  EXPECT_TRUE(mapping.IsEmpty());
  EXPECT_EQ(parser.mStartOffset, 0);
  EXPECT_EQ(parser.mCurrentOffset, 4);
}

static void ReadFile(const char* aPath, nsTArray<uint8_t>& aBuffer) {
  FILE* f = fopen(aPath, "rb");
  ASSERT_NE(f, (FILE*)nullptr);

  int r = fseek(f, 0, SEEK_END);
  ASSERT_EQ(r, 0);

  long size = ftell(f);
  ASSERT_NE(size, -1);
  aBuffer.SetLength(size);

  r = fseek(f, 0, SEEK_SET);
  ASSERT_EQ(r, 0);

  size_t got = fread(aBuffer.Elements(), 1, size, f);
  ASSERT_EQ(got, size_t(size));

  r = fclose(f);
  ASSERT_EQ(r, 0);
}

TEST(WebMBuffered, RealData)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  ReadFile("test.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_OK);
  EXPECT_EQ(mapping.Length(), 6u);
  EXPECT_EQ(parser.mStartOffset, 0);
  EXPECT_EQ(parser.mCurrentOffset, int64_t(webmData.Length()));
  EXPECT_EQ(parser.GetTimecodeScale(), 500000u);

  for (uint32_t i = 0; i < mapping.Length(); ++i) {
    EXPECT_EQ(mapping[i].mEndOffset, gEndOffsets[i]);
    EXPECT_EQ(mapping[i].mSyncOffset, 326);
    EXPECT_EQ(mapping[i].mTimecode, gTimecodes[i]);
  }
}

TEST(WebMBuffered, RealDataAppend)
{
  WebMBufferedParser parser(0);
  nsTArray<WebMTimeDataOffset> mapping;

  nsTArray<uint8_t> webmData;
  ReadFile("test.webm", webmData);

  uint32_t arrayEntries = mapping.Length();
  size_t offset = 0;
  while (offset < webmData.Length()) {
    EXPECT_EQ(parser.Append(webmData.Elements() + offset, 1, mapping), NS_OK);
    offset += 1;
    EXPECT_EQ(parser.mCurrentOffset, int64_t(offset));
    if (mapping.Length() != arrayEntries) {
      arrayEntries = mapping.Length();
      ASSERT_LE(arrayEntries, 6u);
      uint32_t i = arrayEntries - 1;
      EXPECT_EQ(mapping[i].mEndOffset, gEndOffsets[i]);
      EXPECT_EQ(mapping[i].mSyncOffset, 326);
      EXPECT_EQ(mapping[i].mTimecode, gTimecodes[i]);
      EXPECT_EQ(parser.GetTimecodeScale(), 500000u);
    }
  }
  EXPECT_EQ(mapping.Length(), 6u);
  EXPECT_EQ(parser.mStartOffset, 0);
  EXPECT_EQ(parser.mCurrentOffset, int64_t(webmData.Length()));
  EXPECT_EQ(parser.GetTimecodeScale(), 500000u);

  for (uint32_t i = 0; i < mapping.Length(); ++i) {
    EXPECT_EQ(mapping[i].mEndOffset, gEndOffsets[i]);
    EXPECT_EQ(mapping[i].mSyncOffset, 326);
    EXPECT_EQ(mapping[i].mTimecode, gTimecodes[i]);
  }
}

TEST(WebMBuffered, InvalidEBMLMaxIdLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxIdLength=3, but a Segment element (and maybe
  // others) whose Id VInt has length 4.
  ReadFile("test_InvalidElementId.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_ERROR_FAILURE);
}

TEST(WebMBuffered, InvalidLargeElementIdLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxIdLength=4, but a dummy element whose Id VInt has
  // length 5.
  ReadFile("test_InvalidLargeElementId.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_ERROR_FAILURE);
}

TEST(WebMBuffered, InvalidSmallEBMLMaxIdLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxIdLength=3.
  // Per draft-ietf-cellar-matroska-13 EBMLMaxIdLength MUST be 4. But element
  // ids can also be between 1 and 5 octets long. 5 only if EBMLMaxIdLength
  // specifies it. At least 3 is too short.
  ReadFile("test_InvalidSmallEBMLMaxIdLength.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_ERROR_FAILURE);
}

TEST(WebMBuffered, ValidLargeEBMLMaxIdLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxIdLength=5 and a dummy element with a 5 octet
  // long id. Per draft-ietf-cellar-matroska-13 EBMLMaxIdLength MUST be 4. But
  // element ids can also be between 1 and 5 octets long. 5 only if
  // EBMLMaxIdLength specifies it. We better tolerate this.
  ReadFile("test_ValidLargeEBMLMaxIdLength.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_OK);
}

TEST(WebMBuffered, InvalidLargeEBMLMaxIdLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxIdLength=6.
  // Per draft-ietf-cellar-matroska-13 EBMLMaxIdLength MUST be 4. But
  // element ids can also be between 1 and 5 octets long. 5 only if
  // EBMLMaxIdLength specifies it. At least 6 is too long.
  ReadFile("test_InvalidLargeEBMLMaxIdLength.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_ERROR_FAILURE);
}

TEST(WebMBuffered, ValidSmallEBMLMaxSizeLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxSizeLength=7 and no element with an element size
  // longer than 7 bytes.
  ReadFile("test_ValidSmallEBMLMaxSizeLength.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_OK);
}

TEST(WebMBuffered, InvalidEBMLMaxSizeLength)
{
  WebMBufferedParser parser(0);

  nsTArray<uint8_t> webmData;
  // This file contains EBMLMaxSizeLength=7, but the Segment element size VInt
  // has length 8.
  ReadFile("test_InvalidElementSize.webm", webmData);

  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(webmData.Elements(), webmData.Length(), mapping),
            NS_ERROR_FAILURE);
}

// Parse aData and return where the parser placed the init segment boundary.
static int64_t InitEndOffsetOf(const nsTArray<uint8_t>& aData) {
  WebMBufferedParser parser(0);
  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(aData.Elements(), aData.Length(), mapping), NS_OK);
  return parser.mInitEndOffset;
}

// The init segment ends at the first Cluster, so trailing non-Cluster
// top-level elements such as Cues belong to it.
TEST(WebMBuffered, InitSegmentIncludesCues)
{
  nsTArray<uint8_t> init;
  ReadFile("bug1341228_init.webm", init);
  // EBML header, Segment, SeekHead, Void, Info, Tracks, then Cues.
  EXPECT_EQ(init.Length(), 591u);

  EXPECT_EQ(InitEndOffsetOf(init), 591);
}

// An init segment whose last element is Tracks ends where Tracks ends.
TEST(WebMBuffered, InitSegmentEndingAtTracks)
{
  nsTArray<uint8_t> init;
  ReadFile("bug2065262_init.webm", init);
  // As above, truncated after Tracks so that no Cues follow.
  EXPECT_EQ(init.Length(), 272u);

  EXPECT_EQ(InitEndOffsetOf(init), 272);
}

// A SourceBuffer's input buffer can hold two init segments back to back with
// no Cluster between them: TrackBuffersManager re-seeds the input buffer with
// a copy of the previous init segment on abort(), and the next appendBuffer()
// is concatenated onto it. The boundary must stay at the end of the first
// segment rather than running into the second.
TEST(WebMBuffered, BackToBackInitSegmentsWithCues)
{
  nsTArray<uint8_t> init;
  ReadFile("bug1341228_init.webm", init);

  nsTArray<uint8_t> doubled;
  doubled.AppendElements(init);
  doubled.AppendElements(init);

  EXPECT_EQ(InitEndOffsetOf(doubled), 591);
}

TEST(WebMBuffered, BackToBackInitSegmentsEndingAtTracks)
{
  nsTArray<uint8_t> init;
  ReadFile("bug2065262_init.webm", init);

  nsTArray<uint8_t> doubled;
  doubled.AppendElements(init);
  doubled.AppendElements(init);

  EXPECT_EQ(InitEndOffsetOf(doubled), 272);
}

// The boundary must also hold when the second init segment arrives in its own
// Append(), since offsets accumulate across calls.
TEST(WebMBuffered, BackToBackInitSegmentsAcrossAppends)
{
  nsTArray<uint8_t> init;
  ReadFile("bug2065262_init.webm", init);

  WebMBufferedParser parser(0);
  nsTArray<WebMTimeDataOffset> mapping;
  EXPECT_EQ(parser.Append(init.Elements(), init.Length(), mapping), NS_OK);
  EXPECT_EQ(parser.mInitEndOffset, 272);
  EXPECT_EQ(parser.Append(init.Elements(), init.Length(), mapping), NS_OK);
  EXPECT_EQ(parser.mInitEndOffset, 272);
}
