/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/rtp_parameters.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/rtp_header_extension_id.h"
#include "rtc_base/checks.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;
RtpParameters CreateRtpParametersWithCodecs(
    const std::vector<bool>& active,
    const std::vector<std::optional<RtpCodec>>& codecs) {
  RTC_DCHECK_EQ(active.size(), codecs.size());

  RtpParameters parameters;
  for (size_t i = 0; i < codecs.size(); ++i) {
    RtpEncodingParameters encoding;
    encoding.active = active[i];
    encoding.codec = codecs[i];
    parameters.encodings.push_back(encoding);
  }
  return parameters;
}

const char kExtensionUri1[] = "extension-uri1";
const char kExtensionUri2[] = "extension-uri2";

const RtpExtension kExtension1(kExtensionUri1, RtpHeaderExtensionId(1));
const RtpExtension kExtension1Encrypted(kExtensionUri1,
                                        RtpHeaderExtensionId(10),
                                        true);
const RtpExtension kExtension2(kExtensionUri2, RtpHeaderExtensionId(2));

TEST(RtpExtensionTest, DeduplicateHeaderExtensions) {
  std::vector<RtpExtension> extensions;
  std::vector<RtpExtension> filtered;

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kDiscardEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kPreferEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1Encrypted}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kRequireEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1Encrypted}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kDiscardEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kPreferEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1Encrypted}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kRequireEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ(std::vector<RtpExtension>{kExtension1Encrypted}, filtered);

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension2);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kDiscardEncryptedExtension);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ(extensions, filtered);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kPreferEncryptedExtension);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ(extensions, filtered);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kRequireEncryptedExtension);
  EXPECT_EQ(0u, filtered.size());

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension2);
  extensions.push_back(kExtension1Encrypted);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kDiscardEncryptedExtension);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ((std::vector<RtpExtension>{kExtension1, kExtension2}), filtered);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kPreferEncryptedExtension);
  EXPECT_EQ(2u, filtered.size());
  EXPECT_EQ((std::vector<RtpExtension>{kExtension1Encrypted, kExtension2}),
            filtered);
  filtered = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kRequireEncryptedExtension);
  EXPECT_EQ(1u, filtered.size());
  EXPECT_EQ((std::vector<RtpExtension>{kExtension1Encrypted}), filtered);
}

// Test that the filtered vector is sorted so that for a given unsorted array of
// extensions, the filtered vector will always be laied out the same (for easy
// comparison).
TEST(RtpExtensionTest, DeduplicateHeaderExtensionsSorted) {
  const std::vector<RtpExtension> extensions = {
      RtpExtension("cde1", RtpHeaderExtensionId(11), false),
      RtpExtension("cde2", RtpHeaderExtensionId(12), true),
      RtpExtension("abc1", RtpHeaderExtensionId(3), false),
      RtpExtension("abc2", RtpHeaderExtensionId(4), true),
      RtpExtension("cde3", RtpHeaderExtensionId(9), true),
      RtpExtension("cde4", RtpHeaderExtensionId(10), false),
      RtpExtension("abc3", RtpHeaderExtensionId(1), true),
      RtpExtension("abc4", RtpHeaderExtensionId(2), false),
      RtpExtension("bcd3", RtpHeaderExtensionId(7), false),
      RtpExtension("bcd1", RtpHeaderExtensionId(8), true),
      RtpExtension("bcd2", RtpHeaderExtensionId(5), true),
      RtpExtension("bcd4", RtpHeaderExtensionId(6), false),
  };

  auto encrypted = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kRequireEncryptedExtension);

  const std::vector<RtpExtension> expected_sorted_encrypted = {
      RtpExtension("abc2", RtpHeaderExtensionId(4), true),
      RtpExtension("abc3", RtpHeaderExtensionId(1), true),
      RtpExtension("bcd1", RtpHeaderExtensionId(8), true),
      RtpExtension("bcd2", RtpHeaderExtensionId(5), true),
      RtpExtension("cde2", RtpHeaderExtensionId(12), true),
      RtpExtension("cde3", RtpHeaderExtensionId(9), true)};
  EXPECT_EQ(expected_sorted_encrypted, encrypted);

  auto unencypted = RtpExtension::DeduplicateHeaderExtensions(
      extensions, RtpExtension::Filter::kDiscardEncryptedExtension);

  const std::vector<RtpExtension> expected_sorted_unencrypted = {
      RtpExtension("abc1", RtpHeaderExtensionId(3), false),
      RtpExtension("abc4", RtpHeaderExtensionId(2), false),
      RtpExtension("bcd3", RtpHeaderExtensionId(7), false),
      RtpExtension("bcd4", RtpHeaderExtensionId(6), false),
      RtpExtension("cde1", RtpHeaderExtensionId(11), false),
      RtpExtension("cde4", RtpHeaderExtensionId(10), false)};
  EXPECT_EQ(expected_sorted_unencrypted, unencypted);
}

TEST(RtpExtensionTest, FindHeaderExtensionByUriAndEncryption) {
  std::vector<RtpExtension> extensions;

  extensions.clear();
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUriAndEncryption(
                         extensions, kExtensionUri1, false));

  extensions.clear();
  extensions.push_back(kExtension1);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUriAndEncryption(
                             extensions, kExtensionUri1, false));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUriAndEncryption(
                         extensions, kExtensionUri1, true));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUriAndEncryption(
                         extensions, kExtensionUri2, false));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension2);
  extensions.push_back(kExtension1Encrypted);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUriAndEncryption(
                             extensions, kExtensionUri1, false));
  EXPECT_EQ(kExtension2, *RtpExtension::FindHeaderExtensionByUriAndEncryption(
                             extensions, kExtensionUri2, false));
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUriAndEncryption(
                extensions, kExtensionUri1, true));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUriAndEncryption(
                         extensions, kExtensionUri2, true));
}

TEST(RtpExtensionTest, FindHeaderExtensionByUri) {
  std::vector<RtpExtension> extensions;

  extensions.clear();
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri1,
                         RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri1,
                         RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri1,
                         RtpExtension::Filter::kRequireEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri1,
                         RtpExtension::Filter::kRequireEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri2,
                         RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri2,
                         RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri2,
                         RtpExtension::Filter::kRequireEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kDiscardEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kPreferEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension1Encrypted);
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kRequireEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kDiscardEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kPreferEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1Encrypted);
  extensions.push_back(kExtension1);
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kRequireEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension2);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri1,
                         RtpExtension::Filter::kRequireEncryptedExtension));
  EXPECT_EQ(kExtension2, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri2,
                             RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(kExtension2, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri2,
                             RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri2,
                         RtpExtension::Filter::kRequireEncryptedExtension));

  extensions.clear();
  extensions.push_back(kExtension1);
  extensions.push_back(kExtension2);
  extensions.push_back(kExtension1Encrypted);
  EXPECT_EQ(kExtension1, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri1,
                             RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(kExtension1Encrypted,
            *RtpExtension::FindHeaderExtensionByUri(
                extensions, kExtensionUri1,
                RtpExtension::Filter::kRequireEncryptedExtension));
  EXPECT_EQ(kExtension2, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri2,
                             RtpExtension::Filter::kDiscardEncryptedExtension));
  EXPECT_EQ(kExtension2, *RtpExtension::FindHeaderExtensionByUri(
                             extensions, kExtensionUri2,
                             RtpExtension::Filter::kPreferEncryptedExtension));
  EXPECT_EQ(nullptr, RtpExtension::FindHeaderExtensionByUri(
                         extensions, kExtensionUri2,
                         RtpExtension::Filter::kRequireEncryptedExtension));
}

TEST(CodecParameterMap, ParsesKeyValueFmtpParameterSet) {
  std::string params = "key1=value1;key2=value2";
  CodecParameterMap codec_params;
  ASSERT_TRUE(ParseFmtpParameterSet(params, codec_params).ok());
  EXPECT_EQ(2U, codec_params.size());
  EXPECT_EQ(codec_params["key1"], "value1");
  EXPECT_EQ(codec_params["key2"], "value2");
}

TEST(CodecParameterMap, ParsesNonKeyValueFmtpParameterSet) {
  std::string params = "not-in-key-value-format";
  CodecParameterMap codec_params;
  ASSERT_TRUE(ParseFmtpParameterSet(params, codec_params).ok());
  EXPECT_EQ(1U, codec_params.size());
  EXPECT_EQ(codec_params[""], "not-in-key-value-format");
}

TEST(CodecParameterMap, ParsesFmtpParameterSetEdgeCases) {
  CodecParameterMap codec_params;
  // For duplicate keys, the last value wins.
  ASSERT_TRUE(ParseFmtpParameterSet("a=1;a=2", codec_params).ok());
  EXPECT_THAT(codec_params, UnorderedElementsAre(Pair("a", "2")));

  // Whitespace around parameters is trimmed and empty values are kept.
  codec_params.clear();
  ASSERT_TRUE(ParseFmtpParameterSet("a=1; b=", codec_params).ok());
  EXPECT_THAT(codec_params,
              UnorderedElementsAre(Pair("a", "1"), Pair("b", "")));

  // A trailing delimiter adds an empty key with an empty value.
  codec_params.clear();
  ASSERT_TRUE(ParseFmtpParameterSet("a=1;", codec_params).ok());
  EXPECT_THAT(codec_params, UnorderedElementsAre(Pair("", ""), Pair("a", "1")));

  // Repeated '=' after the key are skipped.
  codec_params.clear();
  ASSERT_TRUE(ParseFmtpParameterSet("a==1", codec_params).ok());
  EXPECT_THAT(codec_params, UnorderedElementsAre(Pair("a", "1")));

  // An empty parameter adds an empty key with an empty value, which replaces
  // the value that the non-key-value parameter "a" stored under the empty key.
  codec_params.clear();
  ASSERT_TRUE(ParseFmtpParameterSet("a;;b=1", codec_params).ok());
  EXPECT_THAT(codec_params, UnorderedElementsAre(Pair("", ""), Pair("b", "1")));
}

TEST(RtpParametersTest, IsMixedCodec) {
  RtpParameters parameters;
  RtpCodec codec1, codec2;
  codec1.name = "codec1";
  codec2.name = "codec2";

  parameters = CreateRtpParametersWithCodecs({}, {});
  EXPECT_FALSE(parameters.IsMixedCodec());

  parameters = CreateRtpParametersWithCodecs({true}, {codec1});
  EXPECT_FALSE(parameters.IsMixedCodec());

  parameters = CreateRtpParametersWithCodecs({true}, {std::nullopt});
  EXPECT_FALSE(parameters.IsMixedCodec());

  parameters = CreateRtpParametersWithCodecs({true, true}, {codec1, codec2});
  EXPECT_TRUE(parameters.IsMixedCodec());

  // Inactive encoding parameters are ignored.
  parameters = CreateRtpParametersWithCodecs({false, true}, {codec1, codec2});
  EXPECT_FALSE(parameters.IsMixedCodec());

  // Even if some codecs are nullopt, differing codec presence/values
  // among active encodings is considered mixed.
  parameters =
      CreateRtpParametersWithCodecs({true, true}, {std::nullopt, codec2});
  EXPECT_TRUE(parameters.IsMixedCodec());

  parameters =
      CreateRtpParametersWithCodecs({true, true}, {std::nullopt, std::nullopt});
  EXPECT_FALSE(parameters.IsMixedCodec());

  parameters = CreateRtpParametersWithCodecs({true, true, true},
                                             {std::nullopt, codec1, codec2});
  EXPECT_TRUE(parameters.IsMixedCodec());
}

TEST(RtpExtensionTest, ToStringAndStringifySanitize) {
  RtpExtension ext("http://example.com/test\r\n\\foo", RtpHeaderExtensionId(1));

  // ToString() should escape raw control characters and backslash
  EXPECT_EQ(ext.ToString(),
            "{uri: http://example.com/test\\r\\n\\\\foo, id: 1}");

  // AbslStringify should do the same
  EXPECT_EQ(absl::StrCat(ext), "[1 http://example.com/test\\r\\n\\\\foo]");
}

TEST(CodecParameterMapTest, InitializerListWithAbslStringView) {
  absl::string_view key1 = "key1";
  absl::string_view val1 = "val1";
  absl::string_view key2 = "key2";
  absl::string_view val2 = "val2";

  // Test constructor
  CodecParameterMap map1 = {{key1, val1}, {key2, val2}};
  EXPECT_THAT(map1,
              UnorderedElementsAre(Pair("key1", "val1"), Pair("key2", "val2")));

  // Test assignment
  CodecParameterMap map2;
  map2 = {{key1, val2}, {key2, val1}};
  EXPECT_THAT(map2,
              UnorderedElementsAre(Pair("key1", "val2"), Pair("key2", "val1")));
}

}  // namespace
}  // namespace webrtc
