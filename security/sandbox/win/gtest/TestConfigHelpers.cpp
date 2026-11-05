/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nsLiteralString.h"
#include "nsWindowsHelpers.h"
#include "sandbox/win/src/app_container.h"
#include "sandbox/win/src/policy_engine_opcodes.h"
#include "sandbox/win/src/sandbox.h"

// clang-format off
#include "../src/sandboxbroker/ConfigHelpers.h"
// clang-format on

using namespace sandbox;
using mozilla::sandboxing::UserFontConfigHelper;
using ::testing::Eq;
using ::testing::Expectation;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::StrictMock;

// Only allow 2 pages to test by default.
constexpr int kDefaultNumberOfPagesForTesting = 2;
static const nsLiteralString sWinUserProfile = uR"(C:\Users\Moz User)"_ns;
static const nsLiteralString sLocalAppData =
    uR"(C:\Users\Moz User\AppData\Local)"_ns;
static const nsLiteralString sRoamingAppData =
    uR"(C:\Users\Moz User\AppData\Roaming)"_ns;
static const wchar_t* sWinUserFonts =
    LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\*)";
static const wchar_t* sAdobeLiveTypeFonts =
    LR"(C:\Users\Moz User\AppData\Roaming\ADOBE\CORESYNC\PLUGINS\LIVETYPE\R\*)";
static const wchar_t* sAdobeUserOwnedFonts =
    LR"(C:\Users\Moz User\AppData\Roaming\ADOBE\USER OWNED FONTS\*)";
static const wchar_t* sTestRegKey = LR"(Software\MozFontsPathsTest)";
static const wchar_t* sTestFailRegKey = LR"(Software\MozFontsPathsTestFail)";

namespace mozilla {

class MockConfig : public TargetConfig {
 public:
  MOCK_METHOD(ResultCode, AllowFileAccess,
              (FileSemantics semantics, const wchar_t* pattern), (override));

  // Remaining methods should not be called during tests.
  MOCK_METHOD(bool, IsConfigured, (), (const, override));
  MOCK_METHOD(ResultCode, SetTokenLevel,
              (TokenLevel initial, TokenLevel lockdown), (override));
  MOCK_METHOD(TokenLevel, GetInitialTokenLevel, (), (const, override));
  MOCK_METHOD(TokenLevel, GetLockdownTokenLevel, (), (const, override));
  MOCK_METHOD(void, SetDoNotUseRestrictingSIDs, (), (override));
  MOCK_METHOD(bool, GetUseRestrictingSIDs, (), (override));
  MOCK_METHOD(void, SetForceKnownDllLoadingFallback, (), (override));
  MOCK_METHOD(ResultCode, SetJobLevel,
              (JobLevel job_level, uint32_t ui_exceptions), (override));
  MOCK_METHOD(JobLevel, GetJobLevel, (), (const, override));
  MOCK_METHOD(void, SetJobMemoryLimit, (size_t memory_limit), (override));
  MOCK_METHOD(ResultCode, AllowRegistryRead, (const wchar_t* pattern),
              (override));
  MOCK_METHOD(ResultCode, AllowExtraDll, (const wchar_t* path), (override));
  MOCK_METHOD(ResultCode, SetFakeGdiInit, (), (override));
  MOCK_METHOD(void, AddDllToUnload, (const wchar_t* dll_name), (override));
  MOCK_METHOD(ResultCode, SetIntegrityLevel, (IntegrityLevel level),
              (override));
  MOCK_METHOD(IntegrityLevel, GetIntegrityLevel, (), (const, override));
  MOCK_METHOD(void, SetDelayedIntegrityLevel, (IntegrityLevel level),
              (override));
  MOCK_METHOD(ResultCode, SetLowBox, (const wchar_t* sid), (override));
  MOCK_METHOD(ResultCode, SetProcessMitigations, (MitigationFlags flags),
              (override));
  MOCK_METHOD(MitigationFlags, GetProcessMitigations, (), (override));
  MOCK_METHOD(ResultCode, SetDelayedProcessMitigations, (MitigationFlags flags),
              (override));
  MOCK_METHOD(MitigationFlags, GetDelayedProcessMitigations, (),
              (const, override));
  MOCK_METHOD(void, AddRestrictingRandomSid, (), (override));
  MOCK_METHOD(void, SetLockdownDefaultDacl, (), (override));
  MOCK_METHOD(ResultCode, AddAppContainerProfile, (const wchar_t* package_name),
              (override));
  MOCK_METHOD(AppContainer*, GetAppContainer, (), (override));
  MOCK_METHOD(void, AddKernelObjectToClose, (HandleToClose handle_info),
              (override));
  MOCK_METHOD(void, SetDisconnectCsrss, (), (override));
  MOCK_METHOD(void, SetDesktop, (Desktop desktop), (override));
  MOCK_METHOD(void, SetFilterEnvironment, (bool filter), (override));
  MOCK_METHOD(bool, GetEnvironmentFiltered, (), (override));
  MOCK_METHOD(void, SetZeroAppShim, (), (override));
};

#define EXPECT_READONLY_EQ(aRulePath)                                     \
  EXPECT_CALL(mConfig, AllowFileAccess(Eq(FileSemantics::kAllowReadonly), \
                                       StrEq(aRulePath)))

#define EXPECT_READONLY_STARTS(aRulePath)                                 \
  EXPECT_CALL(mConfig, AllowFileAccess(Eq(FileSemantics::kAllowReadonly), \
                                       StartsWith(aRulePath)))

static void SetUpPathsInKey(HKEY aKey,
                            const std::vector<std::wstring_view>& aFontPaths) {
  for (size_t i = 0; i < aFontPaths.size(); ++i) {
    const auto* pathBytes = reinterpret_cast<const BYTE*>(aFontPaths[i].data());
    size_t sizeInBytes = (aFontPaths[i].length() + 1) * sizeof(wchar_t);
    ::RegSetValueExW(aKey, std::to_wstring(i).c_str(), 0, REG_SZ, pathBytes,
                     sizeInBytes);
  }
}

class UserFontConfigHelperTest : public testing::Test {
 protected:
  // We always expect the Windows User font dir rule to be added.
  UserFontConfigHelperTest()
      : mWinUserFontCall(EXPECT_READONLY_EQ(sWinUserFonts)) {
    EXPECT_READONLY_EQ(sAdobeLiveTypeFonts);
    EXPECT_READONLY_EQ(sAdobeUserOwnedFonts);
    ::RegCreateKeyExW(HKEY_CURRENT_USER, sTestRegKey, 0, nullptr,
                      REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
                      &mTestUserFontKey, nullptr);
  }

  ~UserFontConfigHelperTest() {
    if (mTestUserFontKey) {
      ::RegCloseKey(mTestUserFontKey);
    }
    ::RegDeleteTreeW(HKEY_CURRENT_USER, sTestRegKey);
  }

  void SetUpPaths(const std::vector<std::wstring_view>& aFontPaths) {
    SetUpPathsInKey(mTestUserFontKey, aFontPaths);
  }

  bool CreateHelperAndCallAddRules() {
    UserFontConfigHelper policyHelper(sTestRegKey, sWinUserProfile,
                                      sLocalAppData, sRoamingAppData);
    sandboxing::SizeTrackingConfig trackingPolicy(&mConfig,
                                                  mNumberOfStoragePages);
    return policyHelper.AddRules(trackingPolicy);
  }

  // StrictMock because we only expect AllowFileAccess to be called.
  StrictMock<MockConfig> mConfig;
  const Expectation mWinUserFontCall;
  HKEY mTestUserFontKey = nullptr;
  int32_t mNumberOfStoragePages = kDefaultNumberOfPagesForTesting;
};

TEST_F(UserFontConfigHelperTest, WindowsDirRuleAddedOnKeyFailure) {
  // Create helper with incorrect key name.
  UserFontConfigHelper policyHelper(sTestFailRegKey, sWinUserProfile,
                                    sLocalAppData, sRoamingAppData);
  sandboxing::SizeTrackingConfig trackingPolicy(&mConfig, 1);
  EXPECT_TRUE(policyHelper.AddRules(trackingPolicy));
}

TEST_F(UserFontConfigHelperTest, PathsInsideUsersDirAdded) {
  SetUpPaths({LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)"});

  // We expect the windows user font rule to be added first.
  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsInsideUsersDirAddedIgnoringCase) {
  SetUpPaths({LR"(C:\users\moz uSER\Fonts\FontFile1.ttf)"});

  EXPECT_READONLY_EQ(LR"(C:\users\moz uSER\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsOutsideUsersDirAdded) {
  SetUpPaths({LR"(C:\ProgramData\Fonts\FontFile1.ttf)",
              LR"(C:\programdata\Fonts\FontFile2.ttf)"});

  EXPECT_READONLY_EQ(LR"(C:\ProgramData\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);
  EXPECT_READONLY_EQ(LR"(C:\programdata\Fonts\FontFile2.ttf)")
      .After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, SubKeyPathsInsideUsersDirAdded) {
  SetUpPaths({LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)"});
  std::unique_ptr<HKEY, RegCloseKeyDeleter> subKey;
  auto lStatus = ::RegCreateKeyExW(mTestUserFontKey, L"SubKey", 0, nullptr,
                                   REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
                                   getter_Transfers(subKey), nullptr);
  ASSERT_EQ(lStatus, ERROR_SUCCESS);
  SetUpPathsInKey(subKey.get(), {LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)"});

  // We expect the windows user font rule to be added first.
  auto& fontFile1 =
      EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
          .After(mWinUserFontCall);
  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)")
      .After(fontFile1);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsOutsideUsersDirAddedAtEnd) {
  // We set up the paths in a particular order, but this doesn't guarantee the
  // order returned from registry calls. However the rule adding code should
  // guarantee that non-user dir fonts are added at the end.
  const auto* userFont1 = LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)";
  const auto* userFont2 = LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)";
  const auto* userFont3 = LR"(C:\Users\Moz User\Fonts\FontFile3.ttf)";
  const auto* pdFont1 = LR"(C:\ProgramData\Fonts\FontFile1.ttf)";
  const auto* pdFont2 = LR"(C:\ProgramData\Fonts\FontFile2.ttf)";
  SetUpPaths({pdFont1, userFont1, pdFont2, userFont2, userFont3});

  auto& userDirFont1 = EXPECT_READONLY_EQ(userFont1).After(mWinUserFontCall);
  auto& userDirFont2 = EXPECT_READONLY_EQ(userFont2).After(mWinUserFontCall);
  auto& userDirFont3 = EXPECT_READONLY_EQ(userFont3).After(mWinUserFontCall);
  EXPECT_READONLY_EQ(pdFont1).After(userDirFont1, userDirFont2, userDirFont3);
  EXPECT_READONLY_EQ(pdFont2).After(userDirFont1, userDirFont2, userDirFont3);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, SubKeyPathsOutsideUsersDirAddedAtEnd) {
  // We set up the paths in a particular order, but this doesn't guarantee the
  // order returned from registry calls. However the rule adding code should
  // guarantee that non-user dir fonts are added at the end.
  const auto* userFont1 = LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)";
  const auto* userFont2 = LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)";
  const auto* userFont3 = LR"(C:\Users\Moz User\Fonts\FontFile3.ttf)";
  const auto* pdFont1 = LR"(C:\ProgramData\Fonts\FontFile1.ttf)";
  const auto* pdFont2 = LR"(C:\ProgramData\Fonts\FontFile2.ttf)";
  SetUpPaths({pdFont1, userFont1, userFont2});
  std::unique_ptr<HKEY, RegCloseKeyDeleter> subKey;
  auto lStatus = ::RegCreateKeyExW(mTestUserFontKey, L"SubKey", 0, nullptr,
                                   REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
                                   getter_Transfers(subKey), nullptr);
  ASSERT_EQ(lStatus, ERROR_SUCCESS);
  SetUpPathsInKey(subKey.get(), {pdFont2, userFont3});

  auto& userDirFont1 = EXPECT_READONLY_EQ(userFont1).After(mWinUserFontCall);
  auto& userDirFont2 = EXPECT_READONLY_EQ(userFont2).After(mWinUserFontCall);
  auto& userDirFont3 =
      EXPECT_READONLY_EQ(userFont3).After(userDirFont1, userDirFont2);
  EXPECT_READONLY_EQ(pdFont1).After(userDirFont3);
  EXPECT_READONLY_EQ(pdFont2).After(userDirFont3);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, NonStringValueIsIgnored) {
  DWORD regValue = 42;
  ::RegSetValueExW(mTestUserFontKey, L"Liff", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&regValue), sizeof(regValue));
  wchar_t multiPath[] = LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)";
  ::RegSetValueExW(mTestUserFontKey, L"MultiStr", 0, REG_MULTI_SZ,
                   reinterpret_cast<const BYTE*>(multiPath), sizeof(multiPath));
  ::RegSetValueExW(mTestUserFontKey, L"ExpandStr", 0, REG_EXPAND_SZ,
                   reinterpret_cast<const BYTE*>(multiPath), sizeof(multiPath));

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)").Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathNotNullTerminated) {
  // If you just miss the null in the size it stills gets stored with it, so you
  // have to make the next character non-null.
  wchar_t fontPath[] = LR"(C:\Users\Moz User\Fonts\FontFile1.ttfx)";
  ::RegSetValueExW(mTestUserFontKey, L"NoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath),
                   sizeof(fontPath) - (2 * sizeof(wchar_t)));

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathEmpty) {
  wchar_t fontPath[] = L"";
  ::RegSetValueExW(mTestUserFontKey, L"EmptyNoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath), sizeof(fontPath));

  EXPECT_READONLY_EQ(fontPath).Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathEmptyNotNullTerminated) {
  // If you just miss the null in the size it stills gets stored with it, so you
  // have to make the next character non-null.
  wchar_t fontPath[] = L"F";
  ::RegSetValueExW(mTestUserFontKey, L"EmptyNoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath), 0);

  EXPECT_READONLY_EQ(L"").Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, DirsAreIgnored) {
  SetUpPaths({LR"(C:\Users\Moz User\Fonts\)"});

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz Us]er\Fonts\)").Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsWithWildcardsAreIgnored) {
  const auto* validCharsPath1 = LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)";
  const auto* validCharsPath2 = LR"(\??\C:\Users\Moz User\Fonts\FontFile2.ttf)";
  SetUpPaths({validCharsPath1, LR"(C:\Users\Moz User\Font/s\invalid.ttf)",
              LR"(C:\Users\Moz User\Fonts\*)",
              LR"(C:\Users\Moz User\Fonts\invalid*)",
              LR"(C:\Users\Moz User\Fonts\invalid/?)", validCharsPath2});

  EXPECT_READONLY_EQ(validCharsPath1).After(mWinUserFontCall);
  EXPECT_READONLY_EQ(validCharsPath2).After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsInWindowsUsersFontDirNotAdded) {
  SetUpPaths({
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\FontFile1.ttf)",
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\Sub\FontFile2.ttf)",
  });

  EXPECT_READONLY_EQ(
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\FontFile1.ttf)")
      .Times(0);
  EXPECT_READONLY_EQ(
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\Sub\FontFile2.ttf)")
      .Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest,
       PathsInWindowsUsersFontDirNotAddedIgnoringCase) {
  SetUpPaths({
      LR"(c:\Users\mOZ user\aPPdATA\Local\microsoft\wINDows\Fonts\FontFile1.ttf)",
      LR"(c:\uSERS\moz user\aPPdATA\lOCAL\MICRosoft\WindOWS\fONTS\Sub\FontFile2.ttf)",
  });

  EXPECT_READONLY_EQ(
      LR"(c:\Users\mOZ user\aPPdATA\Local\microsoft\wINDows\Fonts\FontFile1.ttf)")
      .Times(0);
  EXPECT_READONLY_EQ(
      LR"(c:\uSERS\moz user\aPPdATA\lOCAL\MICRosoft\WindOWS\fONTS\Sub\FontFile2.ttf)")
      .Times(0);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

auto RuleSize(const wchar_t* aRulePath) {
  return (12 * sizeof(sandbox::PolicyOpcode)) +
         ((wcslen(aRulePath) + 4) * sizeof(wchar_t) * 4);
}

std::wstring MakeLongFontPath(const wchar_t* aPrefix, const wchar_t* aSuffix) {
  static size_t sReqPathLen = []() {
    // Take the bytes required for the static rules from the starting memory
    // allowance for tests.
    size_t remainingSpace =
        (4096 * kDefaultNumberOfPagesForTesting) - RuleSize(sWinUserFonts) -
        RuleSize(sAdobeLiveTypeFonts) - RuleSize(sAdobeUserOwnedFonts);

    // We want 3 paths to be too big, so divide by 3 and reverse the formula.
    size_t spacePerFontPath = remainingSpace / 3;
    size_t reqFontLen =
        ((spacePerFontPath - (12 * sizeof(sandbox::PolicyOpcode))) /
         (4 * sizeof(wchar_t))) -
        4;

    // Add on 1 to make it too long for 3 paths.
    return reqFontLen + 1;
  }();

  std::wstring longFontPath(aPrefix);
  longFontPath.append(sReqPathLen - longFontPath.length() - wcslen(aSuffix),
                      'F');
  longFontPath.append(aSuffix);
  return longFontPath;
}

TEST_F(UserFontConfigHelperTest, PathsTooLongForStorage) {
  // These font paths take up enough storage such that, with the Windows user
  // font dir rule, only two will fit in the available 4K of storage. Note that
  // we can't guarantee the order they are returned from the registry.
  auto path1 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"1");
  auto path2 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"2");
  auto path3 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"3");
  SetUpPaths({
      path1,
      path2,
      path3,
  });

  path1.pop_back();
  EXPECT_READONLY_STARTS(path1).Times(2).After(mWinUserFontCall);

  EXPECT_FALSE(CreateHelperAndCallAddRules());
}

TEST_F(UserFontConfigHelperTest, PathsTooLongOneOutsideUserProfile) {
  // These font paths take up enough storage such that, with the Windows user
  // font dir rule, only two will fit in the available 4K of storage.
  // However one is outside the user profile, so we can be certain about which
  // rules should be added.
  auto path1 = MakeLongFontPath(LR"(C:\ProgramData\)", L"1");
  auto path2 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"2");
  auto path3 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"3");
  SetUpPaths({
      path1,
      path2,
      path3,
  });

  EXPECT_READONLY_EQ(path1).Times(0);
  EXPECT_READONLY_EQ(path2).After(mWinUserFontCall);
  EXPECT_READONLY_EQ(path3).After(mWinUserFontCall);

  EXPECT_TRUE(CreateHelperAndCallAddRules());
}

}  // namespace mozilla
