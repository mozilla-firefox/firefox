/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConfigHelpers.h"

#include <windows.h>

#include "mozilla/Logging.h"
#include "mozilla/UniquePtrExtensions.h"  // For getter_Transfers()
#include "mozilla/Vector.h"
#include "nsExceptionHandler.h"
#include "nsStringFwd.h"
#include "nsUnicharUtils.h"
#include "nsWindowsHelpers.h"
#include "sandbox/win/src/policy_engine_opcodes.h"

namespace mozilla {

extern LazyLogModule sSandboxBrokerLog;

#define LOG_E(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Error, (__VA_ARGS__))
#define LOG_W(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Warning, (__VA_ARGS__))

namespace sandboxing {

namespace {

bool ContainsSandboxWildcard(const nsAString& aPath) {
  // '?' is no longer a valid character wildcard, but if used with the old '/'
  // escape character it can still cause a crash. We forbid '/' to prevent this
  // because it is also not a valid path char and the NT prefix contains '?'s.
  static constexpr std::u16string_view kForbidden = u"*/";

  return aPath.FindCharInSet(kForbidden) != kNotFound;
}

}  // namespace

SizeTrackingConfig::SizeTrackingConfig(sandbox::TargetConfig* aConfig,
                                       int32_t aStoragePages)
    : mConfig(aConfig) {
  MOZ_ASSERT(mConfig);

  // The calculation uses the kPolMemPageCount constant in sandbox_policy.h.
  // We reduce the allowable size by 2 to account for the PolicyGlobal and
  // padding that occurs during LowLevelPolicy::Done. See bug 2009140.
  MOZ_ASSERT(aStoragePages > 0);
  MOZ_ASSERT(static_cast<size_t>(aStoragePages) <=
             sandbox::kPolMemPageCount - 2);

  constexpr int32_t kOneMemPage = 4096;
  mRemainingSize = kOneMemPage * aStoragePages;
}

sandbox::ResultCode SizeTrackingConfig::AllowFileAccess(
    sandbox::FileSemantics aSemantics, const wchar_t* aPattern) {
  // This calculation doesn't allow for wild-cards, pipes or things that have an
  // an NT prefix, but in our use cases this would result in an overestimate, so
  // that is fine for our purposes. Wild-cards mid pattern would be undersized,
  // because of extra opcodes, but we don't have any rules like these.

  // Add 4 to length to allow for \??\ NT prefix added to most rules. The
  // pattern is stored with a length and so the null-terminator is not stored.
  auto patternRuleSize = (wcslen(aPattern) + 4) * sizeof(wchar_t);

  // Each brokered function has a copy of the pattern and a number of opcodes
  // depending on aSemantics. This is generally 1 opcode for the string match
  // and 1 for the action (ASK_BROKER) opcode added when Done is called on the
  // rule. For kAllowReadonly access and disposition checks are also added for
  // create and open making 4 opcodes in total.
  int32_t requiredSize;
  constexpr auto opcodeSize = sizeof(sandbox::PolicyOpcode);
  switch (aSemantics) {
    case sandbox::FileSemantics::kAllowAny:
      // create, open, query, query_full and rename brokered with 2 opcodes.
      requiredSize = (patternRuleSize * 5) + (opcodeSize * 10);
      break;
    case sandbox::FileSemantics::kAllowReadonly:
      // create and open brokered with 4 opcodes
      // query and query_full brokered with 2 opcodes
      requiredSize = (patternRuleSize * 4) + (opcodeSize * 12);
      break;
    case sandbox::FileSemantics::kAllowQuery:
      // query and query_full brokered with 2 opcodes
      requiredSize = (patternRuleSize * 2) + (opcodeSize * 4);
      break;
    default:
      MOZ_CRASH("Unknown FileSemantics");
  }

  if (requiredSize > mRemainingSize) {
    return sandbox::SBOX_ERROR_NO_SPACE;
  }

  mRemainingSize -= requiredSize;
  return mConfig->AllowFileAccess(aSemantics, aPattern);
}

UserFontConfigHelper::UserFontConfigHelper(const wchar_t* aUserFontKeyPath,
                                           const nsString& aWinUserProfile,
                                           const nsString& aLocalAppData,
                                           const nsString& aRoamingAppData)
    : mWinUserProfile(aWinUserProfile),
      mLocalAppData(aLocalAppData),
      mRoamingAppData(aRoamingAppData) {
  LSTATUS lStatus =
      ::RegOpenKeyExW(HKEY_CURRENT_USER, aUserFontKeyPath, 0,
                      KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &mUserFontKey);
  if (lStatus != ERROR_SUCCESS) {
    // Ensure that mUserFontKey is null on failure.
    mUserFontKey = nullptr;
  }
}

UserFontConfigHelper::~UserFontConfigHelper() {
  if (mUserFontKey) {
    ::RegCloseKey(mUserFontKey);
  }
}

static auto AddRulesForKey(HKEY aFontKey, const nsAString& aWindowsUserFontDir,
                           const nsAString& aWinUserProfile,
                           SizeTrackingConfig& aConfig,
                           Vector<nsString>& aNonUserDirFonts) {
  for (DWORD valueIndex = 0; /* break on ERROR_NO_MORE_ITEMS */; ++valueIndex) {
    DWORD keyType;
    wchar_t name[1024];
    wchar_t data[2048];
    auto* dataAsBytes = reinterpret_cast<LPBYTE>(data);
    DWORD nameLength = std::size(name);
    // Pass 1 less wchar_t worth, in case we have to add a null.
    DWORD dataSizeInBytes = sizeof(data) - sizeof(wchar_t);
    LSTATUS lStatus =
        ::RegEnumValueW(aFontKey, valueIndex, name, &nameLength, NULL, &keyType,
                        dataAsBytes, &dataSizeInBytes);
    if (lStatus == ERROR_NO_MORE_ITEMS) {
      break;
    }

    // Skip if we failed to retrieve the value.
    if (lStatus != ERROR_SUCCESS) {
      continue;
    }

    // Only strings are used, REG_EXPAND_SZ is not recognized by Fonts panel.
    if (keyType != REG_SZ) {
      continue;
    }

    auto dataSizeInWChars = dataSizeInBytes / sizeof(wchar_t);

    // We test data[dataSizeInWChars - 1] below and might test again after
    // decrementing it, so ensure we have at least 2 wchar_ts. A valid font path
    // couldn't be this short anyway.
    if (dataSizeInWChars < 2) {
      continue;
    }

    // Size might include a null.
    if (data[dataSizeInWChars - 1] == L'\0') {
      --dataSizeInWChars;
    } else {
      // Ensure null terminated.
      data[dataSizeInWChars] = L'\0';
    }

    // Should be path to font file so reject directories.
    if (data[dataSizeInWChars - 1] == L'\\') {
      continue;
    }

    // Shouldn't contain any wildcard chars.
    if (ContainsSandboxWildcard(nsDependentSubstring(data, dataSizeInWChars))) {
      continue;
    }

    // If not in the user's dir, store until the end.
    if (dataSizeInWChars < aWinUserProfile.Length() ||
        !aWinUserProfile.Equals(
            nsDependentSubstring(data, aWinUserProfile.Length()),
            nsCaseInsensitiveStringComparator)) {
      (void)aNonUserDirFonts.emplaceBack(data, dataSizeInWChars);
      continue;
    }

    // Skip if in windows user font dir.
    if (dataSizeInWChars > aWindowsUserFontDir.Length() &&
        aWindowsUserFontDir.Equals(
            nsDependentSubstring(data, aWindowsUserFontDir.Length()),
            nsCaseInsensitiveStringComparator)) {
      continue;
    }

    auto result =
        aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly, data);
    if (result != sandbox::SBOX_ALL_OK) {
      NS_WARNING("Failed to add specific user font policy rule.");
      LOG_W("Failed (ResultCode %d) to add read access to: %S", result, data);
      if (result == sandbox::SBOX_ERROR_NO_SPACE) {
        return result;
      }
    }
  }

  for (DWORD keyIndex = 0; /* break on ERROR_NO_MORE_ITEMS */; ++keyIndex) {
    wchar_t name[1024];
    DWORD nameLength = std::size(name);
    LSTATUS lStatus = ::RegEnumKeyExW(aFontKey, keyIndex, name, &nameLength,
                                      nullptr, nullptr, nullptr, nullptr);
    if (lStatus == ERROR_NO_MORE_ITEMS) {
      break;
    }

    // Skip if we failed to retrieve the key name.
    if (lStatus != ERROR_SUCCESS) {
      continue;
    }

    std::unique_ptr<HKEY, RegCloseKeyDeleter> subKey;
    lStatus = ::RegOpenKeyExW(aFontKey, name, 0,
                              KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                              getter_Transfers(subKey));
    // Skip if we failed to retrieve the key.
    if (lStatus != ERROR_SUCCESS) {
      continue;
    }

    auto result = AddRulesForKey(subKey.get(), aWindowsUserFontDir,
                                 aWinUserProfile, aConfig, aNonUserDirFonts);
    if (result == sandbox::SBOX_ERROR_NO_SPACE) {
      return result;
    }
  }

  return sandbox::SBOX_ALL_OK;
}

bool UserFontConfigHelper::AddRules(SizeTrackingConfig& aConfig) const {
  // Always add rule to allow access to windows user specific fonts dir first.
  nsAutoString windowsUserFontDir(mLocalAppData);
  windowsUserFontDir += uR"(\Microsoft\Windows\Fonts\*)"_ns;
  auto result = aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                        windowsUserFontDir.getW());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add Windows user font dir policy rule.");
    LOG_E("Failed (ResultCode %d) to add read access to: %S", result,
          windowsUserFontDir.getW());
  }

  // Add two hard coded rules for Adobe Creative Cloud fonts, as it uses
  // AddFontResource to register its fonts so they don't appear in the registry.
  nsAutoString adobeLiveTypeFonts(mRoamingAppData);
  adobeLiveTypeFonts += uR"(\ADOBE\CORESYNC\PLUGINS\LIVETYPE\R\*)"_ns;
  result = aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                   adobeLiveTypeFonts.getW());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add Adobe LiveType font dir policy rule.");
    LOG_E("Failed (ResultCode %d) to add read access to: %S", result,
          adobeLiveTypeFonts.getW());
  }
  nsAutoString adobeUserOwnedFonts(mRoamingAppData);
  adobeUserOwnedFonts += uR"(\ADOBE\USER OWNED FONTS\*)"_ns;
  result = aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                   adobeUserOwnedFonts.getW());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add Adobe user owned font dir policy rule.");
    LOG_E("Failed (ResultCode %d) to add read access to: %S", result,
          adobeUserOwnedFonts.getW());
  }

  // We failed to open the registry key, we can't do any more.
  if (!mUserFontKey) {
    // In the unlikely event we can't open the user font registry key we return
    // true to apply the stronger sandbox settings.
    return true;
  }

  // We don't want the wild-card for comparisons.
  windowsUserFontDir.SetLength(windowsUserFontDir.Length() - 1);

  // Windows user's profile dir with trailing slash for comparisons.
  nsAutoString winUserProfile(mWinUserProfile);
  winUserProfile += L'\\';

  // We will add rules for fonts outside of the User's directory at the end, in
  // case we run out of space.
  Vector<nsString> nonUserDirFonts;

  result = AddRulesForKey(mUserFontKey, windowsUserFontDir, winUserProfile,
                          aConfig, nonUserDirFonts);
  if (result == sandbox::SBOX_ERROR_NO_SPACE) {
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::UserFontRulesExhausted, "inside");
    // We've run out of room for font rules in the user's directory, so return
    // false to fall back to weaker sandbox settings to avoid errors.
    return false;
  }

  // Finally add rules for fonts outside the user's dir. These are less likely
  // to have access blocked by USER_LIMITED.
  for (const auto& fontPath : nonUserDirFonts) {
    result = aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                     fontPath.getW());
    if (result != sandbox::SBOX_ALL_OK) {
      NS_WARNING("Failed to add specific user font policy rule.");
      LOG_W("Failed (ResultCode %d) to add read access to: %S", result,
            fontPath.getW());
      if (result == sandbox::SBOX_ERROR_NO_SPACE) {
        CrashReporter::RecordAnnotationCString(
            CrashReporter::Annotation::UserFontRulesExhausted, "outside");
        // We return true here because we don't expect font paths outside the
        // user's directory to be blocked by the sandbox.
        return true;
      }
    }
  }

  return true;
}

}  // namespace sandboxing

}  // namespace mozilla
