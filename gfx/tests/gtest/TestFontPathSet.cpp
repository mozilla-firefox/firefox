/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <dwrite.h>
#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/FontPathSet.h"

using namespace mozilla;
using namespace mozilla::gfx;

static TimeDuration MaxWait() { return TimeDuration::FromSeconds(30); }

static nsString GetSystemFontPath(const nsAString& aFileName) {
  nsString path;
  PWSTR fontsDir = nullptr;
  if (SUCCEEDED(
          ::SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fontsDir))) {
    path.Assign(reinterpret_cast<const char16_t*>(fontsDir));
    path.Append(u'\\');
    path.Append(aFileName);
  }
  ::CoTaskMemFree(fontsDir);
  return path;
}

// The font list normally does this, but it might not have been initialized.
static void RebuildFromSystemFonts() {
  ASSERT_TRUE(FontPathSet::Enable());
  ASSERT_TRUE(Factory::EnsureDWriteFactory());
  RefPtr<IDWriteFontCollection> systemFonts = Factory::GetDWriteSystemFonts();
  ASSERT_TRUE(systemFonts);
  FontPathSet::RebuildFrom(systemFonts);
}

TEST(FontPathSet, ContainsSystemFont)
{
  RebuildFromSystemFonts();
  EXPECT_TRUE(
      FontPathSet::Contains(GetSystemFontPath(u"arial.ttf"_ns), MaxWait()));
  EXPECT_TRUE(FontPathSet::Contains(GetSystemFontPath(u"ARIAL.TTF"_ns)));
  EXPECT_TRUE(FontPathSet::Contains(GetSystemFontPath(u"Arial.ttf"_ns)));
  EXPECT_FALSE(FontPathSet::Contains(GetSystemFontPath(u"notafont.ttf"_ns)));
}

TEST(FontPathSet, MissWaitsOnlyWhileBuilding)
{
  RebuildFromSystemFonts();
  // Waits for the build to finish.
  ASSERT_TRUE(
      FontPathSet::Contains(GetSystemFontPath(u"arial.ttf"_ns), MaxWait()));

  TimeStamp start = TimeStamp::Now();
  EXPECT_FALSE(
      FontPathSet::Contains(GetSystemFontPath(u"notafont.ttf"_ns), MaxWait()));
  EXPECT_LT((TimeStamp::Now() - start).ToSeconds(), 1.0);
}

TEST(FontPathSet, MissWithoutMaxWaitDoesNotWait)
{
  RebuildFromSystemFonts();

  TimeStamp start = TimeStamp::Now();
  EXPECT_FALSE(FontPathSet::Contains(GetSystemFontPath(u"notafont.ttf"_ns)));
  EXPECT_LT((TimeStamp::Now() - start).ToSeconds(), 1.0);
}
