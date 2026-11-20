/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _mozilla_widget_GfxDriverInfo_h_
#define _mozilla_widget_GfxDriverInfo_h_

#include "nsCharSeparatedTokenizer.h"
#include "nsString.h"
#include "nsTArray.h"

// Macros for adding a blocklist item to the static list. _EXT variants
// allow one to specify all available parameters, including those available
// only on specific platforms (e.g. desktop environment and driver vendor
// for Linux.) _ADAPTER variants allow one to select which adapter the entry
// is matched against; the others match the primary adapter.

#define APPEND_TO_DRIVER_BLOCKLIST_EXT(                                       \
    os, screen, battery, windowProtocol, driverVendor, devices, feature,      \
    featureStatus, driverComparator, driverVersion, ruleId, suggestedVersion) \
  static_assert(!GfxInfoBase::IsFeatureRetired(feature),                      \
                "Feature is retired, see GfxInfoFeatureDefs.inc!");           \
  static_assert(GfxInfoBase::IsFeatureStatusAllowed(feature, featureStatus),  \
                "Feature status not allowed, see GfxInfoFeatureDefs.inc!");   \
  sDriverInfo->AppendElement(MakeAndAddRef<GfxDriverInfo>(                    \
      os, screen, battery,                                                    \
      (nsAString&)GfxDriverInfo::GetWindowProtocol(windowProtocol),           \
      (nsAString&)GfxDriverInfo::GetDeviceVendor(devices),                    \
      (nsAString&)GfxDriverInfo::GetDriverVendor(driverVendor),               \
      GfxDriverInfo::GetDeviceFamily(devices), feature, featureStatus,        \
      driverComparator, driverVersion, ruleId, suggestedVersion))

#define APPEND_TO_DRIVER_BLOCKLIST(os, devices, feature, featureStatus,     \
                                   driverComparator, driverVersion, ruleId, \
                                   suggestedVersion)                        \
  APPEND_TO_DRIVER_BLOCKLIST_EXT(                                           \
      os, ScreenSizeStatus::All, BatteryStatus::All, WindowProtocol::All,   \
      DriverVendor::All, devices, feature, featureStatus, driverComparator, \
      driverVersion, ruleId, suggestedVersion)

#define APPEND_TO_DRIVER_BLOCKLIST2_ADAPTER_EXT(                              \
    os, adapterMatch, screen, battery, windowProtocol, driverVendor, devices, \
    feature, featureStatus, driverComparator, driverVersion, ruleId)          \
  static_assert(!GfxInfoBase::IsFeatureRetired(feature),                      \
                "Feature is retired, see GfxInfoFeatureDefs.inc!");           \
  static_assert(GfxInfoBase::IsFeatureStatusAllowed(feature, featureStatus),  \
                "Feature status not allowed, see GfxInfoFeatureDefs.inc!");   \
  static_assert((adapterMatch) != AdapterMatch::Unknown &&                    \
                    (adapterMatch) != AdapterMatch::Max,                      \
                "Invalid adapter match, see GfxInfoAdapterMatchDefs.inc!");   \
  sDriverInfo->AppendElement(MakeAndAddRef<GfxDriverInfo>(                    \
      os, screen, battery,                                                    \
      (nsAString&)GfxDriverInfo::GetWindowProtocol(windowProtocol),           \
      (nsAString&)GfxDriverInfo::GetDeviceVendor(devices),                    \
      (nsAString&)GfxDriverInfo::GetDriverVendor(driverVendor),               \
      GfxDriverInfo::GetDeviceFamily(devices), feature, featureStatus,        \
      driverComparator, driverVersion, ruleId, nullptr, false, adapterMatch))

#define APPEND_TO_DRIVER_BLOCKLIST2_ADAPTER(                                   \
    os, adapterMatch, devices, feature, featureStatus, driverComparator,       \
    driverVersion, ruleId)                                                     \
  APPEND_TO_DRIVER_BLOCKLIST2_ADAPTER_EXT(                                     \
      os, adapterMatch, ScreenSizeStatus::All, BatteryStatus::All,             \
      WindowProtocol::All, DriverVendor::All, devices, feature, featureStatus, \
      driverComparator, driverVersion, ruleId)

#define APPEND_TO_DRIVER_BLOCKLIST2_EXT(                                 \
    os, screen, battery, windowProtocol, driverVendor, devices, feature, \
    featureStatus, driverComparator, driverVersion, ruleId)              \
  APPEND_TO_DRIVER_BLOCKLIST2_ADAPTER_EXT(                               \
      os, AdapterMatch::Primary, screen, battery, windowProtocol,        \
      driverVendor, devices, feature, featureStatus, driverComparator,   \
      driverVersion, ruleId)

#define APPEND_TO_DRIVER_BLOCKLIST2(os, devices, feature, featureStatus,     \
                                    driverComparator, driverVersion, ruleId) \
  APPEND_TO_DRIVER_BLOCKLIST2_EXT(                                           \
      os, ScreenSizeStatus::All, BatteryStatus::All, WindowProtocol::All,    \
      DriverVendor::All, devices, feature, featureStatus, driverComparator,  \
      driverVersion, ruleId)

#define APPEND_TO_DRIVER_BLOCKLIST_REFRESH_RATE(                             \
    os, devices, feature, featureStatus, refreshRateStatus,                  \
    minRefreshRateComparator, minRefreshRate, minRefreshRateMax,             \
    maxRefreshRateComparator, maxRefreshRate, maxRefreshRateMax, ruleId,     \
    suggestedVersion)                                                        \
  static_assert(!GfxInfoBase::IsFeatureRetired(feature),                     \
                "Feature is retired, see GfxInfoFeatureDefs.inc!");          \
  static_assert(GfxInfoBase::IsFeatureStatusAllowed(feature, featureStatus), \
                "Feature status not allowed, see GfxInfoFeatureDefs.inc!");  \
  sDriverInfo->AppendElement(MakeAndAddRef<GfxDriverInfo>(                   \
      os, (nsAString&)GfxDriverInfo::GetDeviceVendor(devices),               \
      GfxDriverInfo::GetDeviceFamily(devices), feature, featureStatus,       \
      refreshRateStatus, minRefreshRateComparator, minRefreshRate,           \
      minRefreshRateMax, maxRefreshRateComparator, maxRefreshRate,           \
      maxRefreshRateMax, ruleId, suggestedVersion))

#define APPEND_TO_DRIVER_BLOCKLIST_RANGE_ADAPTER_EXT(                          \
    os, adapterMatch, screen, battery, windowProtocol, driverVendor, devices,  \
    feature, featureStatus, driverComparator, driverVersion, driverVersionMax, \
    ruleId, suggestedVersion)                                                  \
  static_assert(!GfxInfoBase::IsFeatureRetired(feature),                       \
                "Feature is retired, see GfxInfoFeatureDefs.inc!");            \
  static_assert(GfxInfoBase::IsFeatureStatusAllowed(feature, featureStatus),   \
                "Feature status not allowed, see GfxInfoFeatureDefs.inc!");    \
  static_assert((adapterMatch) != AdapterMatch::Unknown &&                     \
                    (adapterMatch) != AdapterMatch::Max,                       \
                "Invalid adapter match, see GfxInfoAdapterMatchDefs.inc!");    \
  static_assert((driverComparator) == DRIVER_BETWEEN_EXCLUSIVE ||              \
                (driverComparator) == DRIVER_BETWEEN_INCLUSIVE ||              \
                (driverComparator) == DRIVER_BETWEEN_INCLUSIVE_START);         \
  do {                                                                         \
    auto info = MakeRefPtr<GfxDriverInfo>(                                     \
        os, screen, battery,                                                   \
        (nsAString&)GfxDriverInfo::GetWindowProtocol(windowProtocol),          \
        (nsAString&)GfxDriverInfo::GetDeviceVendor(devices),                   \
        (nsAString&)GfxDriverInfo::GetDriverVendor(driverVendor),              \
        GfxDriverInfo::GetDeviceFamily(devices), feature, featureStatus,       \
        driverComparator, driverVersion, ruleId, suggestedVersion, false,      \
        adapterMatch);                                                         \
    info->mDriverVersionMax = driverVersionMax;                                \
    sDriverInfo->AppendElement(info.forget());                                 \
  } while (false)

#define APPEND_TO_DRIVER_BLOCKLIST_RANGE_ADAPTER(                              \
    os, adapterMatch, devices, feature, featureStatus, driverComparator,       \
    driverVersion, driverVersionMax, ruleId, suggestedVersion)                 \
  APPEND_TO_DRIVER_BLOCKLIST_RANGE_ADAPTER_EXT(                                \
      os, adapterMatch, ScreenSizeStatus::All, BatteryStatus::All,             \
      WindowProtocol::All, DriverVendor::All, devices, feature, featureStatus, \
      driverComparator, driverVersion, driverVersionMax, ruleId,               \
      suggestedVersion)

#define APPEND_TO_DRIVER_BLOCKLIST_RANGE_EXT(                                 \
    os, screen, battery, windowProtocol, driverVendor, devices, feature,      \
    featureStatus, driverComparator, driverVersion, driverVersionMax, ruleId, \
    suggestedVersion)                                                         \
  APPEND_TO_DRIVER_BLOCKLIST_RANGE_ADAPTER_EXT(                               \
      os, AdapterMatch::Primary, screen, battery, windowProtocol,             \
      driverVendor, devices, feature, featureStatus, driverComparator,        \
      driverVersion, driverVersionMax, ruleId, suggestedVersion)

#define APPEND_TO_DRIVER_BLOCKLIST_RANGE(                                   \
    os, devices, feature, featureStatus, driverComparator, driverVersion,   \
    driverVersionMax, ruleId, suggestedVersion)                             \
  APPEND_TO_DRIVER_BLOCKLIST_RANGE_EXT(                                     \
      os, ScreenSizeStatus::All, BatteryStatus::All, WindowProtocol::All,   \
      DriverVendor::All, devices, feature, featureStatus, driverComparator, \
      driverVersion, driverVersionMax, ruleId, suggestedVersion)

namespace mozilla {
namespace widget {

enum class OperatingSystem : uint8_t {
  Unknown,
#define GFXINFO_OS(id, name) id,
#include "mozilla/widget/GfxInfoOperatingSystemDefs.inc"
#undef GFXINFO_OS
  Count
};

enum VersionComparisonOp {
#define GFXINFO_DRIVER_VERSION_CMP(id) DRIVER_##id,
#include "mozilla/widget/GfxInfoDriverVersionCmpDefs.inc"
#undef GFXINFO_DRIVER_VERSION_CMP
  DRIVER_COUNT
};

enum class DeviceFamily : uint8_t {
  All,
  IntelAll,
  NvidiaAll,
  AtiAll,
  MicrosoftAll,
  ParallelsAll,
  QualcommAll,
  AppleAll,
  AmazonAll,
  IntelGMA500,
  IntelGMA900,
  IntelGMA950,
  IntelGMA3150,
  IntelGMAX3000,
  IntelGMAX4500HD,
  IntelHDGraphicsToIvyBridge,
  IntelHDGraphicsToSandyBridge,
  IntelHaswell,
  IntelSandyBridge,
  IntelGen7Baytrail,
  IntelSkylake,
  IntelKabyLake,
  IntelHD520,
  IntelMobileHDGraphics,
  IntelMeteorLake,
  IntelArrowlake,
  IntelAlchemist,
  IntelGen12,
  NvidiaBlockD3D9Layers,
  RadeonX1000,
  RadeonCaicos,
  RadeonBlockZeroVideoCopy,
  Geforce7300GT,
  Nvidia310M,
  Nvidia8800GTS,
  NvidiaPascal,
  Bug1137716,
  Bug1116812,
  Bug1155608,
  Bug1207665,
  Bug1447141,
  AmdR600,
  IntelWebRenderBlocked,
  NvidiaWebRenderBlocked,

  Max
};

enum class DeviceVendor : uint8_t {
#define GFXINFO_DEVICE_VENDOR(id, name) id,
#include "mozilla/widget/GfxInfoDeviceVendorDefs.inc"
#undef GFXINFO_DEVICE_VENDOR
  Max
};

enum DriverVendor : uint8_t {
#define GFXINFO_DRIVER_VENDOR(id, name) id,
#include "mozilla/widget/GfxInfoDriverVendorDefs.inc"
#undef GFXINFO_DRIVER_VENDOR
  Max
};

enum class AdapterMatch : uint8_t {
  Unknown,
#define GFXINFO_ADAPTER_MATCH(id, name) id,
#include "mozilla/widget/GfxInfoAdapterMatchDefs.inc"
#undef GFXINFO_ADAPTER_MATCH
  Max
};

enum class WindowProtocol : uint8_t {
#define GFXINFO_WINDOW_PROTOCOL(id, name) id,
#include "mozilla/widget/GfxInfoWindowProtocolDefs.inc"
#undef GFXINFO_WINDOW_PROTOCOL
  Max
};

enum class RefreshRateStatus {
#define GFXINFO_REFRESH_RATE_STATUS(id, name) id,
#include "mozilla/widget/GfxInfoRefreshRateStatusDefs.inc"
#undef GFXINFO_REFRESH_RATE_STATUS
  Unknown,
  Count
};

enum class BatteryStatus : uint8_t { All, Present, None };

enum class ScreenSizeStatus : uint8_t {
  All,
  Small,           // <= 1900x1200
  SmallAndMedium,  // <= 3440x1440
  Medium,          // <= 3440x1440 && > 1900x1200
  MediumAndLarge,  // >1900x1200
  Large            // > 3440x1440
};

class GfxVersionEx final {
  static constexpr size_t MAX_PARTS = 4;

 public:
  GfxVersionEx() = default;
  GfxVersionEx(const GfxVersionEx& aOther) = default;
  GfxVersionEx(GfxVersionEx&& aOther) = default;
  GfxVersionEx& operator=(const GfxVersionEx& aOther) = default;
  GfxVersionEx& operator=(GfxVersionEx&& aOther) = default;

  GfxVersionEx(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild)
      : mParts{aMajor, aMinor, aBuild} {}

  GfxVersionEx(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild,
               uint32_t aRevision)
      : mParts{aMajor, aMinor, aBuild, aRevision} {}

  bool Parse(const nsACString& aVersion) {
    size_t i = 0;
    for (const auto& part : aVersion.Split('.')) {
      nsresult rv;
      mParts[i] = part.ToUnsignedInteger(&rv);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return false;
      }

      if (++i == MAX_PARTS) {
        break;
      }
    }

    while (i < MAX_PARTS) {
      mParts[i++] = 0;
    }

    return true;
  }

  int32_t Compare(const GfxVersionEx& aOther) const {
    for (size_t i = 0; i < MAX_PARTS; ++i) {
      if (mParts[i] < aOther.mParts[i]) {
        return -1;
      }
      if (mParts[i] > aOther.mParts[i]) {
        return 1;
      }
    }
    return 0;
  }

  bool Compare(const GfxVersionEx& aOther, const GfxVersionEx& aOtherMax,
               VersionComparisonOp aCmp) const {
    if (aCmp == DRIVER_COMPARISON_IGNORED) {
      return true;
    }

    switch (aCmp) {
      case DRIVER_LESS_THAN:
        return Compare(aOther) < 0;
      case DRIVER_LESS_THAN_OR_EQUAL:
        return Compare(aOther) <= 0;
      case DRIVER_GREATER_THAN:
        return Compare(aOther) > 0;
      case DRIVER_GREATER_THAN_OR_EQUAL:
        return Compare(aOther) >= 0;
      case DRIVER_EQUAL:
        return Compare(aOther) == 0;
      case DRIVER_NOT_EQUAL:
        return Compare(aOther) != 0;
      case DRIVER_BETWEEN_EXCLUSIVE:
        return Compare(aOther) > 0 && Compare(aOtherMax) < 0;
      case DRIVER_BETWEEN_INCLUSIVE:
        return Compare(aOther) >= 0 && Compare(aOtherMax) <= 0;
      case DRIVER_BETWEEN_INCLUSIVE_START:
        return Compare(aOther) >= 0 && Compare(aOtherMax) < 0;
      default:
        NS_WARNING("Unsupported op in GfxDriverInfo");
        break;
    }

    return false;
  }

 private:
  uint32_t mParts[MAX_PARTS]{};
};

/* Array of devices to match, or an empty array for all devices */
class GfxDeviceFamily final {
 public:
  NS_INLINE_DECL_REFCOUNTING(GfxDeviceFamily);

  GfxDeviceFamily() = default;

  void Append(const nsAString& aDeviceId);
  void AppendRange(int32_t aBeginDeviceId, int32_t aEndDeviceId);

  bool IsEmpty() const { return mIds.IsEmpty() && mRanges.IsEmpty(); }

  nsresult Contains(nsAString& aDeviceId) const;

 private:
  ~GfxDeviceFamily() = default;

  struct DeviceRange {
    int32_t mBegin;
    int32_t mEnd;
  };

  nsTArray<nsString> mIds;
  nsTArray<DeviceRange> mRanges;
};

class GfxDriverInfo final {
 public:
  NS_INLINE_DECL_REFCOUNTING(GfxDriverInfo);

  // If |ownDevices| is true, you are transferring ownership of the devices
  // array, and it will be deleted when this GfxDriverInfo is destroyed.
  GfxDriverInfo(OperatingSystem os, ScreenSizeStatus aScreen,
                BatteryStatus aBattery, const nsAString& windowProtocol,
                const nsAString& vendor, const nsAString& driverVendor,
                already_AddRefed<const GfxDeviceFamily> devices,
                int32_t feature, int32_t featureStatus, VersionComparisonOp op,
                uint64_t driverVersion, const char* ruleId,
                const char* suggestedVersion = nullptr, bool ownDevices = false,
                AdapterMatch aAdapterMatch = AdapterMatch::Primary);

  // For blocking on refresh rates rather than driver versions.
  GfxDriverInfo(OperatingSystem os, const nsAString& vendor,
                already_AddRefed<const GfxDeviceFamily> devices,
                int32_t feature, int32_t featureStatus,
                RefreshRateStatus refreshRateStatus,
                VersionComparisonOp minRefreshRateOp, uint32_t minRefreshRate,
                uint32_t minRefreshRateMax,
                VersionComparisonOp maxRefreshRateOp, uint32_t maxRefreshRate,
                uint32_t maxRefreshRateMax, const char* ruleId,
                const char* suggestedVersion = nullptr);

  GfxDriverInfo();

  OperatingSystem mOperatingSystem = OperatingSystem::Unknown;
  uint32_t mOperatingSystemVersion = 0;

  GfxVersionEx mOperatingSystemVersionEx;
  GfxVersionEx mOperatingSystemVersionExMax;
  VersionComparisonOp mOperatingSystemVersionExComparisonOp =
      DRIVER_COMPARISON_IGNORED;

  uint32_t mMinRefreshRate = 0;
  uint32_t mMinRefreshRateMax = 0;
  VersionComparisonOp mMinRefreshRateComparisonOp = DRIVER_COMPARISON_IGNORED;

  uint32_t mMaxRefreshRate = 0;
  uint32_t mMaxRefreshRateMax = 0;
  VersionComparisonOp mMaxRefreshRateComparisonOp = DRIVER_COMPARISON_IGNORED;

  RefreshRateStatus mRefreshRateStatus = RefreshRateStatus::Any;

  ScreenSizeStatus mScreen = ScreenSizeStatus::All;
  BatteryStatus mBattery = BatteryStatus::All;
  nsString mWindowProtocol;

  nsString mAdapterVendor;
  nsString mDriverVendor;

  RefPtr<const GfxDeviceFamily> mDevices;

  /* Block all features */
  static constexpr int32_t allFeatures = -1;
  /* Block all features not permitted by OnlyAllowFeatureOnKnownConfig */
  static constexpr int32_t optionalFeatures = -2;
  /* A feature from nsIGfxInfo, or a wildcard set of features */
  int32_t mFeature = optionalFeatures;

  /* A feature status from nsIGfxInfo */
  int32_t mFeatureStatus;

  VersionComparisonOp mComparisonOp = DRIVER_COMPARISON_IGNORED;

  /* versions are assumed to be A.B.C.D packed as 0xAAAABBBBCCCCDDDD */
  uint64_t mDriverVersion = 0;
  uint64_t mDriverVersionMax = 0;
  static constexpr uint64_t allDriverVersions = ~(uint64_t(0));

  const char* mSuggestedVersion = nullptr;
  nsCString mRuleId;

  static already_AddRefed<const GfxDeviceFamily> GetDeviceFamily(
      DeviceFamily id);
  static RefPtr<GfxDeviceFamily>
      sDeviceFamilies[static_cast<size_t>(DeviceFamily::Max)];

  static const nsAString& GetWindowProtocol(WindowProtocol id);
  static nsString* sWindowProtocol[static_cast<size_t>(WindowProtocol::Max)];

  static const nsAString& GetDeviceVendor(DeviceVendor id);
  static const nsAString& GetDeviceVendor(DeviceFamily id);
  static nsString* sDeviceVendors[static_cast<size_t>(DeviceVendor::Max)];

  static const nsAString& GetDriverVendor(DriverVendor id);
  static nsString* sDriverVendors[static_cast<size_t>(DriverVendor::Max)];

  nsString mModel, mHardware, mProduct, mManufacturer;

  AdapterMatch mAdapterMatch = AdapterMatch::Primary;

 private:
  ~GfxDriverInfo() = default;
};

inline uint64_t DriverVersion(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return (uint64_t(a) << 48) | (uint64_t(b) << 32) | (uint64_t(c) << 16) |
         uint64_t(d);
}

inline uint64_t V(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
#ifdef XP_WIN
  // We make sure every driver number is padded by 0s, this will allow us the
  // easiest 'compare as if decimals' approach. See ParseDriverVersion for a
  // more extensive explanation of this approach.
  while (b > 0 && b < 1000) {
    b *= 10;
  }
  while (c > 0 && c < 1000) {
    c *= 10;
  }
  while (d > 0 && d < 1000) {
    d *= 10;
  }
#endif
  return DriverVersion(a, b, c, d);
}

// All destination string storage needs to have at least 5 bytes available.
inline bool SplitDriverVersion(const char* aSource, char* aAStr, char* aBStr,
                               char* aCStr, char* aDStr) {
  // sscanf doesn't do what we want here to we parse this manually.
  int len = strlen(aSource);

  // This "4" is hardcoded in a few places, including once as a 3.
  char* dest[4] = {aAStr, aBStr, aCStr, aDStr};
  unsigned destIdx = 0;
  unsigned destPos = 0;

  for (int i = 0; i < len; i++) {
    if (destIdx >= 4) {
      // Invalid format found. Ensure we don't access dest beyond bounds.
      return false;
    }

    if (aSource[i] == '.') {
      MOZ_ASSERT(destIdx < 4 && destPos <= 4);
      dest[destIdx++][destPos] = 0;
      destPos = 0;
      continue;
    }

    if (destPos > 3) {
      // Ignore more than 4 chars. Ensure we never access dest[destIdx]
      // beyond its bounds.
      continue;
    }

    MOZ_ASSERT(destIdx < 4 && destPos < 4);
    dest[destIdx][destPos++] = aSource[i];
  }

  // Take care of the trailing period
  if (destIdx >= 4) {
    return false;
  }

  // Add last terminator.
  MOZ_ASSERT(destIdx < 4 && destPos <= 4);
  dest[destIdx][destPos] = 0;
  for (int unusedDestIdx = destIdx + 1; unusedDestIdx < 4; unusedDestIdx++) {
    dest[unusedDestIdx][0] = 0;
  }

  if (destIdx != 3) {
    return false;
  }
  return true;
}

// This allows us to pad driver version 'substrings' with 0s, this
// effectively allows us to treat the version numbers as 'decimals'. This is
// a little strange but this method seems to do the right thing for all
// different vendor's driver strings. i.e. .98 will become 9800, which is
// larger than .978 which would become 9780.
inline void PadDriverDecimal(char* aString) {
  for (int i = 0; i < 4; i++) {
    if (!aString[i]) {
      for (int c = i; c < 4; c++) {
        aString[c] = '0';
      }
      break;
    }
  }
  aString[4] = 0;
}

inline bool ParseDriverVersion(const nsAString& aVersion,
                               uint64_t* aNumericVersion) {
  *aNumericVersion = 0;

#ifndef ANDROID
  int a, b, c, d;
  char aStr[8], bStr[8], cStr[8], dStr[8];
  /* honestly, why do I even bother */
  if (!SplitDriverVersion(NS_LossyConvertUTF16toASCII(aVersion).get(), aStr,
                          bStr, cStr, dStr))
    return false;

#  ifdef XP_WIN
  PadDriverDecimal(bStr);
  PadDriverDecimal(cStr);
  PadDriverDecimal(dStr);
#  endif

  a = atoi(aStr);
  b = atoi(bStr);
  c = atoi(cStr);
  d = atoi(dStr);

  if (a < 0 || a > 0xffff) return false;
  if (b < 0 || b > 0xffff) return false;
  if (c < 0 || c > 0xffff) return false;
  if (d < 0 || d > 0xffff) return false;

  *aNumericVersion = DriverVersion(a, b, c, d);
#else
  // Can't use aVersion.ToInteger() because that's not compiled into our code
  // unless we have XPCOM_GLUE_AVOID_NSPR disabled.
  *aNumericVersion = atoi(NS_LossyConvertUTF16toASCII(aVersion).get());
#endif
  MOZ_ASSERT(*aNumericVersion != GfxDriverInfo::allDriverVersions);
  return true;
}

}  // namespace widget
}  // namespace mozilla

#endif /*_mozilla_widget_GfxDriverInfo_h_ */
