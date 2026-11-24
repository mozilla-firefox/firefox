/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gtest/gtest.h>

#include "ContainerParser.h"
#include "mozilla/gtest/MozAssertions.h"

using namespace mozilla;
using TimeUnit = mozilla::media::TimeUnit;

TEST(ContainerParser, MIMETypes)
{
  const char* containerTypes[] = {"video/webm", "audio/webm", "video/mp4",
                                  "audio/mp4",  "audio/aac",  "audio/mpeg"};
  UniquePtr<ContainerParser> parser;
  for (size_t i = 0; i < std::size(containerTypes); ++i) {
    Maybe<MediaContainerType> containerType =
        MakeMediaContainerType(containerTypes[i]);
    ASSERT_TRUE(containerType.isSome());
    parser = ContainerParser::CreateForMIMEType(*containerType);
    ASSERT_NE(parser, nullptr);
  }
}
