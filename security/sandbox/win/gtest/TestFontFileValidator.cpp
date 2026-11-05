/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <dwrite.h>
#include <shlobj.h>
#include <windows.h>

#include "gtest/gtest.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/FontPathSet.h"
#include "nsString.h"

// clang-format off
#include "../src/sandboxbroker/FontFileValidator.h"
// clang-format on

using namespace mozilla;
using namespace mozilla::sandboxing;

static std::wstring GetSystemFontNtPath(const wchar_t* aFileName) {
  std::wstring path(LR"(\??\)");
  PWSTR fontsDir = nullptr;
  if (SUCCEEDED(
          ::SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fontsDir))) {
    path += fontsDir;
    path += L'\\';
    path += aFileName;
  }
  ::CoTaskMemFree(fontsDir);
  return path;
}

TEST(SandboxFontFileValidator, Win32PathStripsPrefix)
{
  nsDependentSubstring path;
  EXPECT_TRUE(
      GetFontFileWin32Path(LR"(\??\c:\Users\Moz\Fonts\font.ttf)", path));
  EXPECT_TRUE(path.EqualsLiteral(uR"(c:\Users\Moz\Fonts\font.ttf)"));
}

TEST(SandboxFontFileValidator, Win32PathRejectsNonDriveLetterPaths)
{
  nsDependentSubstring path;
  EXPECT_FALSE(GetFontFileWin32Path(LR"(C:\Fonts\font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\UNC\server\share\font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\pipe\font.ttf)", path));
  EXPECT_FALSE(
      GetFontFileWin32Path(LR"(\Device\HarddiskVolume1\Fonts\font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\1:\Fonts\font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\C:font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\C:\)", path));
}

TEST(SandboxFontFileValidator, Win32PathRejectsParentDirs)
{
  nsDependentSubstring path;
  EXPECT_FALSE(
      GetFontFileWin32Path(LR"(\??\C:\Fonts\..\Secret\font.ttf)", path));
  EXPECT_FALSE(GetFontFileWin32Path(LR"(\??\C:\Fonts\..)", path));
}

TEST(SandboxFontFileValidator, FontFileExtensions)
{
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.TTF)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.OTF)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.TTC)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\FONTS\FONT.OTC)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\Fonts\font.ttf)"_ns));
  EXPECT_TRUE(HasFontFileExtension(uR"(C:\Fonts\font.Otc)"_ns));

  // Other font file types that can be added with AddFontResource are not
  // supported by DirectWrite, so they are never in the installed font set.
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.FON)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.FNT)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.FOT)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.PFB)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.PFM)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.MMM)"_ns));

  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\FONT.DLL)"_ns));
  EXPECT_FALSE(HasFontFileExtension(uR"(C:\FONTS\TTF)"_ns));
}

// The font list normally does this, but it might not have been initialized.
static void RebuildFromSystemFonts() {
  ASSERT_TRUE(gfx::Factory::EnsureDWriteFactory());
  RefPtr<IDWriteFontCollection> systemFonts =
      gfx::Factory::GetDWriteSystemFonts();
  ASSERT_TRUE(systemFonts);
  gfx::FontPathSet::RebuildFrom(systemFonts);
}

TEST(SandboxFontFileValidator, ValidatesInstalledFonts)
{
  ASSERT_TRUE(gfx::FontPathSet::Enable());
  RebuildFromSystemFonts();

  EXPECT_TRUE(ValidateFontFile(GetSystemFontNtPath(L"arial.ttf")));
  EXPECT_FALSE(ValidateFontFile(GetSystemFontNtPath(L"notafont.ttf")));
}

TEST(SandboxFontFileValidator, NonFontExtensionMissDoesNotWait)
{
  ASSERT_TRUE(gfx::FontPathSet::Enable());
  RebuildFromSystemFonts();

  TimeStamp start = TimeStamp::Now();
  EXPECT_FALSE(ValidateFontFile(GetSystemFontNtPath(L"notafont.dll")));
  EXPECT_LT((TimeStamp::Now() - start).ToSeconds(), 1.0);
}
