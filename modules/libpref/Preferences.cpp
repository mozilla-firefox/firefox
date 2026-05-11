/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Documentation for libpref is in modules/libpref/docs/index.rst.

#include <stdlib.h>
#include <string.h>

#include "SharedPrefMap.h"

#include "base/basictypes.h"
#include "MainThreadUtils.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ArenaAllocatorExtensions.h"
#include "mozilla/ArenaAllocator.h"
#include "mozilla/Attributes.h"
#include "mozilla/Components.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/glean/LibprefMetrics.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/IdleTaskRunner.h"
#include "mozilla/HashTable.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Omnijar.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefsAll.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/Try.h"
#include "mozilla/URLPreloader.h"
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCategoryManagerUtils.h"
#include "nsClassHashtable.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsCRT.h"
#include "nsTHashMap.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIConsoleService.h"
#include "nsIFile.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIPrefBranch.h"
#include "nsIPrefLocalizedString.h"
#include "nsIPrefOverrideMap.h"
#include "nsIRelativeFilePref.h"
#include "nsISafeOutputStream.h"
#include "nsISimpleEnumerator.h"
#include "nsIStringBundle.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPrimitives.h"
#include "nsIZipReader.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsReadableUtils.h"
#include "nsRefPtrHashtable.h"
#include "nsRelativeFilePref.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsTStringHasher.h"
#include "nsUTF8Utils.h"
#include "nsWeakReference.h"
#include "nsXPCOMCID.h"
#include "nsXPCOM.h"
#include "nsXULAppAPI.h"
#include "nsZipArchive.h"
#include "plbase64.h"
#include "PLDHashTable.h"
#include "prdtoa.h"
#include "prlink.h"
#include "xpcpublic.h"
#include "js/RootingAPI.h"
#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasks.h"
#endif

#ifdef DEBUG
#  include <map>
#endif

#ifdef MOZ_MEMORY
#  include "mozmemory.h"
#endif

#ifdef XP_WIN
#  include "windows.h"
#endif

#if defined(MOZ_WIDGET_GTK)
#  include "mozilla/WidgetUtilsGtk.h"
#endif  // defined(MOZ_WIDGET_GTK)

#ifdef MOZ_WIDGET_COCOA
#  include "ChannelPrefsUtil.h"
#endif

using namespace mozilla;

using dom::Promise;
using ipc::FileDescriptor;

#ifdef DEBUG

#  define ENSURE_PARENT_PROCESS(func, pref)                                   \
    do {                                                                      \
      if (MOZ_UNLIKELY(!XRE_IsParentProcess())) {                             \
        nsPrintfCString msg(                                                  \
            "ENSURE_PARENT_PROCESS: called %s on %s in a non-parent process", \
            func, pref);                                                      \
        NS_ERROR(msg.get());                                                  \
        return NS_ERROR_NOT_AVAILABLE;                                        \
      }                                                                       \
    } while (0)

#else  // DEBUG

#  define ENSURE_PARENT_PROCESS(func, pref)     \
    if (MOZ_UNLIKELY(!XRE_IsParentProcess())) { \
      return NS_ERROR_NOT_AVAILABLE;            \
    }

#endif  // DEBUG

static mozilla::LazyLogModule sPrefLog("Preferences");

// Forward declarations.
namespace mozilla::StaticPrefs {

static void InitAll();
static void StartObservingAlwaysPrefs();
static void InitOncePrefs();
static void InitStaticPrefsFromShared();
static void RegisterOncePrefs(SharedPrefMapBuilder& aBuilder);
static void ShutdownAlwaysPrefs();

}  // namespace mozilla::StaticPrefs

//===========================================================================
// Low-level types and operations
//===========================================================================

typedef nsTArray<nsCString> PrefSaveData;

// 1 MB should be enough for everyone.
static const uint32_t MAX_PREF_LENGTH = 1 * 1024 * 1024;
// Actually, 4kb should be enough for everyone.
static const uint32_t MAX_ADVISABLE_PREF_LENGTH = 4 * 1024;

// This is used for pref names and string pref values. We encode the string
// length, then a '/', then the string chars. This encoding means there are no
// special chars that are forbidden or require escaping.
static void SerializeAndAppendString(const nsCString& aChars, nsCString& aStr) {
  aStr.AppendInt(uint64_t(aChars.Length()));
  aStr.Append('/');
  aStr.Append(aChars);
}

static const char* DeserializeString(const char* aChars, nsCString& aStr) {
  const char* p = aChars;
  uint32_t length = strtol(p, const_cast<char**>(&p), 10);
  MOZ_ASSERT(p[0] == '/');
  p++;  // move past the '/'
  aStr.Assign(p, length);
  p += length;  // move past the string itself
  return p;
}

static void GetPrefsJsPreamble(nsACString& aPreamble) {
  // clang-format off
  aPreamble.AssignLiteral(
    "// Mozilla User Preferences" NS_LINEBREAK
    NS_LINEBREAK
    "// DO NOT EDIT THIS FILE." NS_LINEBREAK
    "//" NS_LINEBREAK
    "// If you make changes to this file while the application is running," NS_LINEBREAK
    "// the changes will be overwritten when the application exits." NS_LINEBREAK
    "//" NS_LINEBREAK
    "// To change a preference value, you can either:" NS_LINEBREAK
    "// - modify it via the UI (e.g. via about:config in the browser); or" NS_LINEBREAK
    "// - set it within a user.js file in your profile." NS_LINEBREAK
    NS_LINEBREAK);
  // clang-format on
}

// Keep this in sync with PrefValue in parser/src/lib.rs.
union PrefValue {
  // PrefValues within Pref objects own their chars. PrefValues passed around
  // as arguments don't own their chars.
  const char* mStringVal;
  int32_t mIntVal;
  bool mBoolVal;

  PrefValue() = default;

  explicit PrefValue(bool aVal) : mBoolVal(aVal) {}

  explicit PrefValue(int32_t aVal) : mIntVal(aVal) {}

  explicit PrefValue(const char* aVal) : mStringVal(aVal) {}

  bool Equals(PrefType aType, PrefValue aValue) {
    switch (aType) {
      case PrefType::String: {
        if (mStringVal && aValue.mStringVal) {
          return strcmp(mStringVal, aValue.mStringVal) == 0;
        }
        if (!mStringVal && !aValue.mStringVal) {
          return true;
        }
        return false;
      }

      case PrefType::Int:
        return mIntVal == aValue.mIntVal;

      case PrefType::Bool:
        return mBoolVal == aValue.mBoolVal;

      default:
        MOZ_CRASH("Unhandled enum value");
    }
  }

  template <typename T>
  T Get() const;

  void Init(PrefType aNewType, PrefValue aNewValue) {
    if (aNewType == PrefType::String) {
      MOZ_ASSERT(aNewValue.mStringVal);
      aNewValue.mStringVal = moz_xstrdup(aNewValue.mStringVal);
    }
    *this = aNewValue;
  }

  void Clear(PrefType aType) {
    if (aType == PrefType::String) {
      free(const_cast<char*>(mStringVal));
    }

    // Zero the entire value (regardless of type) via mStringVal.
    mStringVal = nullptr;
  }

  void Replace(bool aHasValue, PrefType aOldType, PrefType aNewType,
               PrefValue aNewValue) {
    if (aHasValue) {
      Clear(aOldType);
    }
    Init(aNewType, aNewValue);
  }

  void ToDomPrefValue(PrefType aType, dom::PrefValue* aDomValue) {
    switch (aType) {
      case PrefType::String:
        *aDomValue = nsDependentCString(mStringVal);
        return;

      case PrefType::Int:
        *aDomValue = mIntVal;
        return;

      case PrefType::Bool:
        *aDomValue = mBoolVal;
        return;

      default:
        MOZ_CRASH();
    }
  }

  PrefType FromDomPrefValue(const dom::PrefValue& aDomValue) {
    switch (aDomValue.type()) {
      case dom::PrefValue::TnsCString:
        mStringVal = aDomValue.get_nsCString().get();
        return PrefType::String;

      case dom::PrefValue::Tint32_t:
        mIntVal = aDomValue.get_int32_t();
        return PrefType::Int;

      case dom::PrefValue::Tbool:
        mBoolVal = aDomValue.get_bool();
        return PrefType::Bool;

      default:
        MOZ_CRASH();
    }
  }

  void SerializeAndAppend(PrefType aType, nsCString& aStr) {
    switch (aType) {
      case PrefType::Bool:
        aStr.Append(mBoolVal ? 'T' : 'F');
        break;

      case PrefType::Int:
        aStr.AppendInt(mIntVal);
        break;

      case PrefType::String: {
        SerializeAndAppendString(nsDependentCString(mStringVal), aStr);
        break;
      }

      case PrefType::None:
      default:
        MOZ_CRASH();
    }
  }

  void ToString(PrefType aType, nsCString& aStr) {
    switch (aType) {
      case PrefType::Bool:
        aStr.Append(mBoolVal ? "true" : "false");
        break;

      case PrefType::Int:
        aStr.AppendInt(mIntVal);
        break;

      case PrefType::String: {
        aStr.Append(nsDependentCString(mStringVal));
        break;
      }

      case PrefType::None:
      default:;
    }
  }

  static const char* Deserialize(PrefType aType, const char* aStr,
                                 Maybe<dom::PrefValue>* aDomValue) {
    const char* p = aStr;

    switch (aType) {
      case PrefType::Bool:
        if (*p == 'T') {
          *aDomValue = Some(true);
        } else if (*p == 'F') {
          *aDomValue = Some(false);
        } else {
          *aDomValue = Some(false);
          NS_ERROR("bad bool pref value");
        }
        p++;
        return p;

      case PrefType::Int: {
        *aDomValue = Some(int32_t(strtol(p, const_cast<char**>(&p), 10)));
        return p;
      }

      case PrefType::String: {
        nsCString str;
        p = DeserializeString(p, str);
        *aDomValue = Some(str);
        return p;
      }

      default:
        MOZ_CRASH();
    }
  }
};

template <>
bool PrefValue::Get() const {
  return mBoolVal;
}

template <>
int32_t PrefValue::Get() const {
  return mIntVal;
}

template <>
nsDependentCString PrefValue::Get() const {
  return nsDependentCString(mStringVal);
}

// Like PrefValue but strings are owned.
class OwnedPrefValue {
 public:
  explicit OwnedPrefValue(bool aVal) : mPrefValue(aVal) {}
  explicit OwnedPrefValue(int32_t aVal) : mPrefValue(aVal) {}
  explicit OwnedPrefValue(const char* aVal)
      : mOwnedStr(aVal), mPrefValue(mOwnedStr.get()) {}

  PrefValue GetPrefValue() { return mPrefValue; }

 private:
  nsCString mOwnedStr;
  PrefValue mPrefValue;
};

class nsPrefOverrideMap : public nsIPrefOverrideMap {
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPREFOVERRIDEMAP
 public:
  const HashMap<nsCString, Maybe<OwnedPrefValue>>& Ref() const { return mMap; }

 private:
  virtual ~nsPrefOverrideMap() = default;
  // Map full pref name to value or Nothing if pref was not defined.
  HashMap<nsCString, Maybe<OwnedPrefValue>> mMap;
};

#ifdef DEBUG
const char* PrefTypeToString(PrefType aType) {
  switch (aType) {
    case PrefType::None:
      return "none";
    case PrefType::String:
      return "string";
    case PrefType::Int:
      return "int";
    case PrefType::Bool:
      return "bool";
    default:
      MOZ_CRASH("Unhandled enum value");
  }
}
#endif

// Assign to aResult a quoted, escaped copy of aOriginal.
static void StrEscape(const char* aOriginal, nsCString& aResult) {
  if (aOriginal == nullptr) {
    aResult.AssignLiteral("\"\"");
    return;
  }

  // JavaScript does not allow quotes, slashes, or line terminators inside
  // strings so we must escape them. ECMAScript defines four line terminators,
  // but we're only worrying about \r and \n here.  We currently feed our pref
  // script to the JS interpreter as Latin-1 so  we won't encounter \u2028
  // (line separator) or \u2029 (paragraph separator).
  //
  // WARNING: There are hints that we may be moving to storing prefs as utf8.
  // If we ever feed them to the JS compiler as UTF8 then we'll have to worry
  // about the multibyte sequences that would be interpreted as \u2028 and
  // \u2029.
  const char* p;

  aResult.Assign('"');

  // Paranoid worst case all slashes will free quickly.
  for (p = aOriginal; *p; ++p) {
    switch (*p) {
      case '\n':
        aResult.AppendLiteral("\\n");
        break;

      case '\r':
        aResult.AppendLiteral("\\r");
        break;

      case '\\':
        aResult.AppendLiteral("\\\\");
        break;

      case '\"':
        aResult.AppendLiteral("\\\"");
        break;

      default:
        aResult.Append(*p);
        break;
    }
  }

  aResult.Append('"');
}

// Mimic the behaviour of nsTStringRepr::ToFloat before bug 840706 to preserve
// error case handling for parsing pref strings. Many callers do not check error
// codes, so the returned values may be used even if an error is set.
//
// This method should never return NaN, but may return +-inf if the provided
// number is too large to fit in a float.
static float ParsePrefFloat(const nsCString& aString, nsresult* aError) {
  if (aString.IsEmpty()) {
    *aError = NS_ERROR_ILLEGAL_VALUE;
    return 0.f;
  }

  // PR_strtod does a locale-independent conversion.
  char* stopped = nullptr;
  float result = PR_strtod(aString.get(), &stopped);

  // Defensively avoid potential breakage caused by returning NaN into
  // unsuspecting code. AFAIK this should never happen as PR_strtod cannot
  // return NaN as currently configured.
  if (std::isnan(result)) {
    MOZ_ASSERT_UNREACHABLE("PR_strtod shouldn't return NaN");
    *aError = NS_ERROR_ILLEGAL_VALUE;
    return 0.f;
  }

  *aError = (stopped == aString.EndReading()) ? NS_OK : NS_ERROR_ILLEGAL_VALUE;
  return result;
}

struct PreferenceMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("Preference");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aPrefName,
                                   const Maybe<PrefValueKind>& aPrefKind,
                                   PrefType aPrefType,
                                   const ProfilerString8View& aPrefValue) {
    aWriter.StringProperty("prefName", aPrefName);
    aWriter.StringProperty("prefKind", PrefValueKindToString(aPrefKind));
    aWriter.StringProperty("prefType", PrefTypeToString(aPrefType));
    aWriter.StringProperty("prefValue", aPrefValue);
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyLabelFormat("prefName", "Name", MS::Format::String);
    schema.AddKeyLabelFormat("prefKind", "Kind", MS::Format::String);
    schema.AddKeyLabelFormat("prefType", "Type", MS::Format::String);
    schema.AddKeyLabelFormat("prefValue", "Value", MS::Format::String);
    schema.SetTableLabel(
        "{marker.data.prefName}: {marker.data.prefValue} "
        "({marker.data.prefType})");
    return schema;
  }

 private:
  static Span<const char> PrefValueKindToString(
      const Maybe<PrefValueKind>& aKind) {
    if (aKind) {
      return *aKind == PrefValueKind::Default ? MakeStringSpan("Default")
                                              : MakeStringSpan("User");
    }
    return "Shared";
  }

  static Span<const char> PrefTypeToString(PrefType type) {
    switch (type) {
      case PrefType::None:
        return "None";
      case PrefType::Int:
        return "Int";
      case PrefType::Bool:
        return "Bool";
      case PrefType::String:
        return "String";
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown preference type.");
        return "Unknown";
    }
  }
};

namespace mozilla {
struct PrefsSizes {
  PrefsSizes()
      : mHashTable(0),
        mPrefValues(0),
        mStringValues(0),
        mRootBranches(0),
        mPrefNameArena(0),
        mCallbacksObjects(0),
        mCallbacksDomains(0),
        mMisc(0) {}

  size_t mHashTable;
  size_t mPrefValues;
  size_t mStringValues;
  size_t mRootBranches;
  size_t mPrefNameArena;
  size_t mCallbacksObjects;
  size_t mCallbacksDomains;
  size_t mMisc;
};
}  // namespace mozilla

static StaticRefPtr<SharedPrefMap> gSharedMap;

// Arena for Pref names.
// Never access sPrefNameArena directly, always use PrefNameArena()
// because it must only be accessed on the Main Thread
typedef ArenaAllocator<4096, 1> NameArena;
static NameArena* sPrefNameArena;

static inline NameArena& PrefNameArena() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!sPrefNameArena) {
    sPrefNameArena = new NameArena();
  }
  return *sPrefNameArena;
}

class PrefWrapper;

// Three forward declarations for immediately below
class Pref;
static bool IsPreferenceSanitized(const Pref* const aPref);
static bool ShouldSanitizePreference(const Pref* const aPref);

// Note that this never changes in the parent process, and is only read in
// content processes.
static bool gContentProcessPrefsAreInited = false;

class Pref {
 public:
  explicit Pref(const nsACString& aName)
      : mName(ArenaStrdup(aName, PrefNameArena()), aName.Length()),
        mType(static_cast<uint32_t>(PrefType::None)),
        mIsSticky(false),
        mIsLocked(false),
        mIsSanitized(false),
        mHasDefaultValue(false),
        mHasUserValue(false),
        mIsSkippedByIteration(false),
        mDefaultValue(),
        mUserValue() {}

  ~Pref() {
    // There's no need to free mName because it's allocated in memory owned by
    // sPrefNameArena.

    mDefaultValue.Clear(Type());
    mUserValue.Clear(Type());
  }

  const char* Name() const { return mName.get(); }
  const nsDependentCString& NameString() const { return mName; }

  // Types.

  PrefType Type() const { return static_cast<PrefType>(mType); }
  void SetType(PrefType aType) { mType = static_cast<uint32_t>(aType); }

  bool IsType(PrefType aType) const { return Type() == aType; }
  bool IsTypeNone() const { return IsType(PrefType::None); }
  bool IsTypeString() const { return IsType(PrefType::String); }
  bool IsTypeInt() const { return IsType(PrefType::Int); }
  bool IsTypeBool() const { return IsType(PrefType::Bool); }

  // Other properties.

  bool IsLocked() const { return mIsLocked; }
  void SetIsLocked(bool aValue) { mIsLocked = aValue; }
  bool IsSkippedByIteration() const { return mIsSkippedByIteration; }
  void SetIsSkippedByIteration(bool aValue) { mIsSkippedByIteration = aValue; }

  bool IsSticky() const { return mIsSticky; }

  bool IsSanitized() const { return mIsSanitized; }

  bool HasDefaultValue() const { return mHasDefaultValue; }
  bool HasUserValue() const { return mHasUserValue; }

  template <typename T>
  void AddToMap(SharedPrefMapBuilder& aMap) {
    // Sanitized preferences should never be added to the shared pref map
    MOZ_ASSERT(!ShouldSanitizePreference(this));
    aMap.Add(NameString(),
             {HasDefaultValue(), HasUserValue(), IsSticky(), IsLocked(),
              /* isSanitized */ false, IsSkippedByIteration()},
             HasDefaultValue() ? mDefaultValue.Get<T>() : T(),
             HasUserValue() ? mUserValue.Get<T>() : T());
  }

  void AddToMap(SharedPrefMapBuilder& aMap) {
    if (IsTypeBool()) {
      AddToMap<bool>(aMap);
    } else if (IsTypeInt()) {
      AddToMap<int32_t>(aMap);
    } else if (IsTypeString()) {
      AddToMap<nsDependentCString>(aMap);
    } else {
      MOZ_ASSERT_UNREACHABLE("Unexpected preference type");
    }
  }

  // Other operations.

#define CHECK_SANITIZATION()                                                \
  if (IsPreferenceSanitized(this)) {                                        \
    glean::security::pref_usage_content_process.Record(                     \
        Some(glean::security::PrefUsageContentProcessExtra{Some(Name())})); \
    if (sCrashOnBlocklistedPref) {                                          \
      MOZ_CRASH_UNSAFE_PRINTF(                                              \
          "Should not access the preference '%s' in the Content Processes", \
          Name());                                                          \
    }                                                                       \
  }

  bool GetBoolValue(PrefValueKind aKind = PrefValueKind::User) const {
    MOZ_ASSERT(IsTypeBool());
    MOZ_ASSERT(aKind == PrefValueKind::Default ? HasDefaultValue()
                                               : HasUserValue());

    CHECK_SANITIZATION();

    return aKind == PrefValueKind::Default ? mDefaultValue.mBoolVal
                                           : mUserValue.mBoolVal;
  }

  int32_t GetIntValue(PrefValueKind aKind = PrefValueKind::User) const {
    MOZ_ASSERT(IsTypeInt());
    MOZ_ASSERT(aKind == PrefValueKind::Default ? HasDefaultValue()
                                               : HasUserValue());

    CHECK_SANITIZATION();

    return aKind == PrefValueKind::Default ? mDefaultValue.mIntVal
                                           : mUserValue.mIntVal;
  }

  const char* GetBareStringValue(
      PrefValueKind aKind = PrefValueKind::User) const {
    MOZ_ASSERT(IsTypeString());
    MOZ_ASSERT(aKind == PrefValueKind::Default ? HasDefaultValue()
                                               : HasUserValue());

    CHECK_SANITIZATION();

    return aKind == PrefValueKind::Default ? mDefaultValue.mStringVal
                                           : mUserValue.mStringVal;
  }

#undef CHECK_SANITIZATION

  nsDependentCString GetStringValue(
      PrefValueKind aKind = PrefValueKind::User) const {
    return nsDependentCString(GetBareStringValue(aKind));
  }

  void ToDomPref(dom::Pref* aDomPref, bool aIsDestinationWebContentProcess) {
    MOZ_ASSERT(XRE_IsParentProcess());

    aDomPref->name() = mName;

    aDomPref->isLocked() = mIsLocked;

    aDomPref->isSanitized() =
        aIsDestinationWebContentProcess && ShouldSanitizePreference(this);

    if (mHasDefaultValue) {
      aDomPref->defaultValue() = Some(dom::PrefValue());
      mDefaultValue.ToDomPrefValue(Type(), &aDomPref->defaultValue().ref());
    } else {
      aDomPref->defaultValue() = Nothing();
    }

    if (mHasUserValue &&
        !(aDomPref->isSanitized() && sOmitBlocklistedPrefValues)) {
      aDomPref->userValue() = Some(dom::PrefValue());
      mUserValue.ToDomPrefValue(Type(), &aDomPref->userValue().ref());
    } else {
      aDomPref->userValue() = Nothing();
    }

    MOZ_ASSERT(aDomPref->defaultValue().isNothing() ||
               aDomPref->userValue().isNothing() ||
               (mIsSanitized && sOmitBlocklistedPrefValues) ||
               (aDomPref->defaultValue().ref().type() ==
                aDomPref->userValue().ref().type()));
  }

  void FromDomPref(const dom::Pref& aDomPref, bool* aValueChanged) {
    MOZ_ASSERT(!XRE_IsParentProcess());
    MOZ_ASSERT(mName == aDomPref.name());

    mIsLocked = aDomPref.isLocked();
    mIsSanitized = aDomPref.isSanitized();

    const Maybe<dom::PrefValue>& defaultValue = aDomPref.defaultValue();
    bool defaultValueChanged = false;
    if (defaultValue.isSome()) {
      PrefValue value;
      PrefType type = value.FromDomPrefValue(defaultValue.ref());
      if (!ValueMatches(PrefValueKind::Default, type, value)) {
        // Type() is PrefType::None if it's a newly added pref. This is ok.
        mDefaultValue.Replace(mHasDefaultValue, Type(), type, value);
        SetType(type);
        mHasDefaultValue = true;
        defaultValueChanged = true;
      }
    } else if (mHasDefaultValue) {
      ClearDefaultValue();
      defaultValueChanged = true;
    }

    const Maybe<dom::PrefValue>& userValue = aDomPref.userValue();
    bool userValueChanged = false;
    if (userValue.isSome()) {
      PrefValue value;
      PrefType type = value.FromDomPrefValue(userValue.ref());
      if (!ValueMatches(PrefValueKind::User, type, value)) {
        // Type() is PrefType::None if it's a newly added pref. This is ok.
        mUserValue.Replace(mHasUserValue, Type(), type, value);
        SetType(type);
        mHasUserValue = true;
        userValueChanged = true;
      }
    } else if (mHasUserValue) {
      ClearUserValue();
      userValueChanged = true;
    }

    if (userValueChanged || (defaultValueChanged && !mHasUserValue)) {
      *aValueChanged = true;
    }
  }

  void FromWrapper(PrefWrapper& aWrapper);

  bool HasAdvisablySizedValues() {
    MOZ_ASSERT(XRE_IsParentProcess());

    if (!IsTypeString()) {
      return true;
    }

    if (mHasDefaultValue &&
        strlen(mDefaultValue.mStringVal) > MAX_ADVISABLE_PREF_LENGTH) {
      return false;
    }

    if (mHasUserValue &&
        strlen(mUserValue.mStringVal) > MAX_ADVISABLE_PREF_LENGTH) {
      return false;
    }

    return true;
  }

 private:
  bool ValueMatches(PrefValueKind aKind, PrefType aType, PrefValue aValue) {
    return IsType(aType) &&
           (aKind == PrefValueKind::Default
                ? mHasDefaultValue && mDefaultValue.Equals(aType, aValue)
                : mHasUserValue && mUserValue.Equals(aType, aValue));
  }

 public:
  void ClearUserValue() {
    mUserValue.Clear(Type());
    mHasUserValue = false;
  }
  void ClearDefaultValue() {
    mDefaultValue.Clear(Type());
    mHasDefaultValue = false;
  }

  nsresult SetDefaultValue(PrefType aType, PrefValue aValue, bool aIsSticky,
                           bool aIsLocked, bool* aValueChanged) {
    // Types must always match when setting the default value.
    if (!IsType(aType)) {
      return NS_ERROR_UNEXPECTED;
    }

    // Should we set the default value? Only if the pref is not locked, and
    // doing so would change the default value.
    if (!IsLocked()) {
      if (aIsLocked) {
        SetIsLocked(true);
      }
      if (aIsSticky) {
        mIsSticky = true;
      }
      if (!ValueMatches(PrefValueKind::Default, aType, aValue)) {
        mDefaultValue.Replace(mHasDefaultValue, Type(), aType, aValue);
        mHasDefaultValue = true;
        if (!mHasUserValue) {
          *aValueChanged = true;
        }
        // What if we change the default to be the same as the user value?
        // Should we clear the user value? Currently we don't.
      }
    }
    return NS_OK;
  }

  nsresult SetUserValue(PrefType aType, PrefValue aValue, bool aFromInit,
                        bool* aValueChanged) {
    // If we have a default value, types must match when setting the user
    // value.
    if (mHasDefaultValue && !IsType(aType)) {
      return NS_ERROR_UNEXPECTED;
    }

    // Should we clear the user value, if present? Only if the new user value
    // matches the default value, and the pref isn't sticky, and we aren't
    // force-setting it during initialization.
    if (ValueMatches(PrefValueKind::Default, aType, aValue) && !mIsSticky &&
        !aFromInit) {
      if (mHasUserValue) {
        ClearUserValue();
        if (!IsLocked()) {
          *aValueChanged = true;
        }
      }

      // Otherwise, should we set the user value? Only if doing so would
      // change the user value.
    } else if (!ValueMatches(PrefValueKind::User, aType, aValue)) {
      mUserValue.Replace(mHasUserValue, Type(), aType, aValue);
      SetType(aType);  // needed because we may have changed the type
      mHasUserValue = true;
      if (!IsLocked()) {
        *aValueChanged = true;
      }
    }
    return NS_OK;
  }

  // Prefs are serialized in a manner that mirrors dom::Pref. The two should be
  // kept in sync. E.g. if something is added to one it should also be added to
  // the other. (It would be nice to be able to use the code generated from
  // IPDL for serializing dom::Pref here instead of writing by hand this
  // serialization/deserialization. Unfortunately, that generated code is
  // difficult to use directly, outside of the IPDL IPC code.)
  //
  // The grammar for the serialized prefs has the following form.
  //
  // <pref>         = <type> <locked> <sanitized> ':' <name> ':' <value>? ':'
  //                  <value>? '\n'
  // <type>         = 'B' | 'I' | 'S'
  // <locked>       = 'L' | '-'
  // <sanitized>    = 'S' | '-'
  // <name>         = <string-value>
  // <value>        = <bool-value> | <int-value> | <string-value>
  // <bool-value>   = 'T' | 'F'
  // <int-value>    = an integer literal accepted by strtol()
  // <string-value> = <int-value> '/' <chars>
  // <chars>        = any char sequence of length dictated by the preceding
  //                  <int-value>.
  //
  // No whitespace is tolerated between tokens. <type> must match the types of
  // the values.
  //
  // The serialization is text-based, rather than binary, for the following
  // reasons.
  //
  // - The size difference wouldn't be much different between text-based and
  //   binary. Most of the space is for strings (pref names and string pref
  //   values), which would be the same in both styles. And other differences
  //   would be minimal, e.g. small integers are shorter in text but long
  //   integers are longer in text.
  //
  // - Likewise, speed differences should be negligible.
  //
  // - It's much easier to debug a text-based serialization. E.g. you can
  //   print it and inspect it easily in a debugger.
  //
  // Examples of unlocked boolean prefs:
  // - "B--:8/my.bool1:F:T\n"
  // - "B--:8/my.bool2:F:\n"
  // - "B--:8/my.bool3::T\n"
  //
  // Examples of sanitized, unlocked boolean prefs:
  // - "B-S:8/my.bool1:F:T\n"
  // - "B-S:8/my.bool2:F:\n"
  // - "B-S:8/my.bool3::T\n"
  //
  // Examples of locked integer prefs:
  // - "IL-:7/my.int1:0:1\n"
  // - "IL-:7/my.int2:123:\n"
  // - "IL-:7/my.int3::-99\n"
  //
  // Examples of unlocked string prefs:
  // - "S--:10/my.string1:3/abc:4/wxyz\n"
  // - "S--:10/my.string2:5/1.234:\n"
  // - "S--:10/my.string3::7/string!\n"

  void SerializeAndAppend(nsCString& aStr, bool aSanitizeUserValue) {
    switch (Type()) {
      case PrefType::Bool:
        aStr.Append('B');
        break;

      case PrefType::Int:
        aStr.Append('I');
        break;

      case PrefType::String: {
        aStr.Append('S');
        break;
      }

      case PrefType::None:
      default:
        MOZ_CRASH();
    }

    aStr.Append(mIsLocked ? 'L' : '-');
    aStr.Append(aSanitizeUserValue ? 'S' : '-');
    aStr.Append(':');

    SerializeAndAppendString(mName, aStr);
    aStr.Append(':');

    if (mHasDefaultValue) {
      mDefaultValue.SerializeAndAppend(Type(), aStr);
    }
    aStr.Append(':');

    if (mHasUserValue && !(aSanitizeUserValue && sOmitBlocklistedPrefValues)) {
      mUserValue.SerializeAndAppend(Type(), aStr);
    }
    aStr.Append('\n');
  }

  static const char* Deserialize(const char* aStr, dom::Pref* aDomPref) {
    const char* p = aStr;

    // The type.
    PrefType type;
    if (*p == 'B') {
      type = PrefType::Bool;
    } else if (*p == 'I') {
      type = PrefType::Int;
    } else if (*p == 'S') {
      type = PrefType::String;
    } else {
      NS_ERROR("bad pref type");
      type = PrefType::None;
    }
    p++;  // move past the type char

    // Locked?
    bool isLocked;
    if (*p == 'L') {
      isLocked = true;
    } else if (*p == '-') {
      isLocked = false;
    } else {
      NS_ERROR("bad pref locked status");
      isLocked = false;
    }
    p++;  // move past the isLocked char

    // Sanitize?
    bool isSanitized;
    if (*p == 'S') {
      isSanitized = true;
    } else if (*p == '-') {
      isSanitized = false;
    } else {
      NS_ERROR("bad pref sanitized status");
      isSanitized = false;
    }
    p++;  // move past the isSanitized char

    MOZ_ASSERT(*p == ':');
    p++;  // move past the ':'

    // The pref name.
    nsCString name;
    p = DeserializeString(p, name);

    MOZ_ASSERT(*p == ':');
    p++;  // move past the ':' preceding the default value

    Maybe<dom::PrefValue> maybeDefaultValue;
    if (*p != ':') {
      dom::PrefValue defaultValue;
      p = PrefValue::Deserialize(type, p, &maybeDefaultValue);
    }

    MOZ_ASSERT(*p == ':');
    p++;  // move past the ':' between the default and user values

    Maybe<dom::PrefValue> maybeUserValue;
    if (*p != '\n') {
      dom::PrefValue userValue;
      p = PrefValue::Deserialize(type, p, &maybeUserValue);
    }

    MOZ_ASSERT(*p == '\n');
    p++;  // move past the '\n' following the user value

    *aDomPref = dom::Pref(name, isLocked, isSanitized, maybeDefaultValue,
                          maybeUserValue);

    return p;
  }

  void AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf, PrefsSizes& aSizes) {
    // Note: mName is allocated in sPrefNameArena, measured elsewhere.
    aSizes.mPrefValues += aMallocSizeOf(this);
    if (IsTypeString()) {
      if (mHasDefaultValue) {
        aSizes.mStringValues += aMallocSizeOf(mDefaultValue.mStringVal);
      }
      if (mHasUserValue) {
        aSizes.mStringValues += aMallocSizeOf(mUserValue.mStringVal);
      }
    }
  }

  void RelocateName(NameArena* aArena) {
    mName.Rebind(ArenaStrdup(mName.get(), *aArena), mName.Length());
  }

 private:
  nsDependentCString mName;  // allocated in sPrefNameArena

  uint32_t mType : 2;
  uint32_t mIsSticky : 1;
  uint32_t mIsLocked : 1;
  uint32_t mIsSanitized : 1;
  uint32_t mHasDefaultValue : 1;
  uint32_t mHasUserValue : 1;
  uint32_t mIsSkippedByIteration : 1;

  PrefValue mDefaultValue;
  PrefValue mUserValue;
};

struct PrefHasher {
  using Key = UniquePtr<Pref>;
  using Lookup = const char*;

  static HashNumber hash(const Lookup aLookup) { return HashString(aLookup); }

  static bool match(const Key& aKey, const Lookup aLookup) {
    if (!aLookup || !aKey->Name()) {
      return false;
    }

    return strcmp(aLookup, aKey->Name()) == 0;
  }
};

using PrefWrapperBase = Variant<Pref*, SharedPrefMap::Pref>;
class MOZ_STACK_CLASS PrefWrapper : public PrefWrapperBase {
  using SharedPref = const SharedPrefMap::Pref;

 public:
  MOZ_IMPLICIT PrefWrapper(Pref* aPref) : PrefWrapperBase(AsVariant(aPref)) {}

  MOZ_IMPLICIT PrefWrapper(const SharedPrefMap::Pref& aPref)
      : PrefWrapperBase(AsVariant(aPref)) {}

  // Types.

  bool IsType(PrefType aType) const { return Type() == aType; }
  bool IsTypeNone() const { return IsType(PrefType::None); }
  bool IsTypeString() const { return IsType(PrefType::String); }
  bool IsTypeInt() const { return IsType(PrefType::Int); }
  bool IsTypeBool() const { return IsType(PrefType::Bool); }

#define FORWARD(retType, method)                                        \
  retType method() const {                                              \
    struct Matcher {                                                    \
      retType operator()(const Pref* aPref) { return aPref->method(); } \
      retType operator()(SharedPref& aPref) { return aPref.method(); }  \
    };                                                                  \
    return match(Matcher());                                            \
  }

  FORWARD(bool, IsLocked)
  FORWARD(bool, IsSanitized)
  FORWARD(bool, IsSticky)
  FORWARD(bool, HasDefaultValue)
  FORWARD(bool, HasUserValue)
  FORWARD(const char*, Name)
  FORWARD(nsCString, NameString)
  FORWARD(PrefType, Type)
#undef FORWARD

#define FORWARD(retType, method)                                             \
  retType method(PrefValueKind aKind = PrefValueKind::User) const {          \
    struct Matcher {                                                         \
      PrefValueKind mKind;                                                   \
                                                                             \
      retType operator()(const Pref* aPref) { return aPref->method(mKind); } \
      retType operator()(SharedPref& aPref) { return aPref.method(mKind); }  \
    };                                                                       \
    return match(Matcher{aKind});                                            \
  }

  FORWARD(bool, GetBoolValue)
  FORWARD(int32_t, GetIntValue)
  FORWARD(nsCString, GetStringValue)
  FORWARD(const char*, GetBareStringValue)
#undef FORWARD

  PrefValue GetValue(PrefValueKind aKind = PrefValueKind::User) const {
    switch (Type()) {
      case PrefType::Bool:
        return PrefValue{GetBoolValue(aKind)};
      case PrefType::Int:
        return PrefValue{GetIntValue(aKind)};
      case PrefType::String:
        return PrefValue{GetBareStringValue(aKind)};
      case PrefType::None:
        // This check will be performed in the above functions; but for NoneType
        // we need to do it explicitly, then fall-through.
        if (IsPreferenceSanitized(Name())) {
          glean::security::pref_usage_content_process.Record(Some(
              glean::security::PrefUsageContentProcessExtra{Some(Name())}));

          if (sCrashOnBlocklistedPref) {
            MOZ_CRASH_UNSAFE_PRINTF(
                "Should not access the preference '%s' in the Content "
                "Processes",
                Name());
          }
        }
        [[fallthrough]];
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected pref type");
        return PrefValue{};
    }
  }

  Result<PrefValueKind, nsresult> WantValueKind(PrefType aType,
                                                PrefValueKind aKind) const {
    // WantValueKind may short-circuit GetValue functions and cause them to
    // return early, before this check occurs in GetFooValue()
    if (this->is<Pref*>() && IsPreferenceSanitized(this->as<Pref*>())) {
      glean::security::pref_usage_content_process.Record(
          Some(glean::security::PrefUsageContentProcessExtra{Some(Name())}));

      if (sCrashOnBlocklistedPref) {
        MOZ_CRASH_UNSAFE_PRINTF(
            "Should not access the preference '%s' in the Content Processes",
            Name());
      }
    } else if (!this->is<Pref*>()) {
      // While we could use Name() above, and avoid the Variant checks, it
      // would less efficient than needed and we can instead do a debug-only
      // assert here to limit the inefficientcy
      MOZ_ASSERT(!IsPreferenceSanitized(Name()),
                 "We should never have a sanitized SharedPrefMap::Pref.");
    }

    if (Type() != aType) {
      return Err(NS_ERROR_UNEXPECTED);
    }

    if (aKind == PrefValueKind::Default || IsLocked() || !HasUserValue()) {
      if (!HasDefaultValue()) {
        return Err(NS_ERROR_UNEXPECTED);
      }
      return PrefValueKind::Default;
    }
    return PrefValueKind::User;
  }

  nsresult GetValue(PrefValueKind aKind, bool* aResult) const {
    PrefValueKind kind = MOZ_TRY(WantValueKind(PrefType::Bool, aKind));

    *aResult = GetBoolValue(kind);
    return NS_OK;
  }

  nsresult GetValue(PrefValueKind aKind, int32_t* aResult) const {
    PrefValueKind kind = MOZ_TRY(WantValueKind(PrefType::Int, aKind));

    *aResult = GetIntValue(kind);
    return NS_OK;
  }

  nsresult GetValue(PrefValueKind aKind, uint32_t* aResult) const {
    return GetValue(aKind, reinterpret_cast<int32_t*>(aResult));
  }

  nsresult GetValue(PrefValueKind aKind, float* aResult) const {
    nsAutoCString result;
    nsresult rv = GetValue(aKind, result);
    if (NS_SUCCEEDED(rv)) {
      // ParsePrefFloat() does a locale-independent conversion.
      // FIXME: Other `GetValue` overloads don't clobber `aResult` on error.
      *aResult = ParsePrefFloat(result, &rv);
    }
    return rv;
  }

  nsresult GetValue(PrefValueKind aKind, nsACString& aResult) const {
    PrefValueKind kind = MOZ_TRY(WantValueKind(PrefType::String, aKind));

    aResult = GetStringValue(kind);
    return NS_OK;
  }

  nsresult GetValue(PrefValueKind aKind, nsACString* aResult) const {
    return GetValue(aKind, *aResult);
  }

  // Returns false if this pref doesn't have a user value worth saving.
  bool UserValueToStringForSaving(nsCString& aStr,
                                  const nsIPrefOverrideMap* aPrefOverrideMap) {
    auto getPrefValue = [&]() -> Result<PrefValue, bool> {
      if (aPrefOverrideMap) {
        auto& overrideMap =
            static_cast<const nsPrefOverrideMap*>(aPrefOverrideMap)->Ref();
        if (auto it = overrideMap.lookup(NameString())) {
          if (it->value().isNothing()) {
            // Don't save pref if pref was not defined before Nimbus set it.
            return Err(false);
          }
          return it->value()->GetPrefValue();
        }
      }
      // Use the user value.
      if (!HasUserValue()) {
        return Err(false);
      }
      return GetValue();
    };
    PrefValue prefValue = MOZ_TRY(getPrefValue());

    // Should we save the value, if present? Only if it does not match the
    // default value, or it is sticky.
    if (!ValueMatches(PrefValueKind::Default, Type(), prefValue) ||
        IsSticky()) {
      if (IsTypeString()) {
        StrEscape(prefValue.Get<nsDependentCString>().get(), aStr);

      } else if (IsTypeInt()) {
        aStr.AppendInt(prefValue.Get<int32_t>());

      } else if (IsTypeBool()) {
        aStr = prefValue.Get<bool>() ? "true" : "false";
      }
      return true;
    }

    // Do not save default prefs that haven't changed.
    return false;
  }

  bool Matches(PrefType aType, PrefValueKind aKind, PrefValue& aValue,
               bool aIsSticky, bool aIsLocked) const {
    return (ValueMatches(aKind, aType, aValue) && aIsSticky == IsSticky() &&
            aIsLocked == IsLocked());
  }

  bool ValueMatches(PrefValueKind aKind, PrefType aType,
                    const PrefValue& aValue) const {
    if (!IsType(aType)) {
      return false;
    }
    if (!(aKind == PrefValueKind::Default ? HasDefaultValue()
                                          : HasUserValue())) {
      return false;
    }
    switch (aType) {
      case PrefType::Bool:
        return GetBoolValue(aKind) == aValue.mBoolVal;
      case PrefType::Int:
        return GetIntValue(aKind) == aValue.mIntVal;
      case PrefType::String:
        return strcmp(GetBareStringValue(aKind), aValue.mStringVal) == 0;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected preference type");
        return false;
    }
  }
};

void Pref::FromWrapper(PrefWrapper& aWrapper) {
  MOZ_ASSERT(aWrapper.is<SharedPrefMap::Pref>());
  auto pref = aWrapper.as<SharedPrefMap::Pref>();

  MOZ_ASSERT(IsTypeNone());
  MOZ_ASSERT(mName == pref.NameString());

  mType = uint32_t(pref.Type());

  mIsLocked = pref.IsLocked();
  mIsSanitized = pref.IsSanitized();
  mIsSticky = pref.IsSticky();

  mHasDefaultValue = pref.HasDefaultValue();
  mHasUserValue = pref.HasUserValue();

  if (mHasDefaultValue) {
    mDefaultValue.Init(Type(), aWrapper.GetValue(PrefValueKind::Default));
  }
  if (mHasUserValue) {
    mUserValue.Init(Type(), aWrapper.GetValue(PrefValueKind::User));
  }
}

class CallbackNode {
 public:
  CallbackNode(const nsACString& aDomain, PrefChangedFunc aFunc, void* aData,
               Preferences::MatchKind aMatchKind)
      : mDomain(AsVariant(nsCString(aDomain))),
        mFunc(aFunc),
        mData(aData),
        mNextAndMatchKind(aMatchKind) {}

  CallbackNode(const char* const* aDomains, PrefChangedFunc aFunc, void* aData,
               Preferences::MatchKind aMatchKind)
      : mDomain(AsVariant(aDomains)),
        mFunc(aFunc),
        mData(aData),
        mNextAndMatchKind(aMatchKind) {}

  // mDomain is a UniquePtr<>, so any uses of Domain() should only be temporary
  // borrows.
  const Variant<nsCString, const char* const*>& Domain() const {
    return mDomain;
  }

  const char* DomainForLog() const {
    return mDomain.is<nsCString>() ? mDomain.as<nsCString>().get() : "(multi)";
  }

  PrefChangedFunc Func() const { return mFunc; }
  void ClearFunc() { mFunc = nullptr; }

  void* Data() const { return mData; }

  Preferences::MatchKind MatchKind() const {
    return static_cast<Preferences::MatchKind>(mNextAndMatchKind &
                                               kMatchKindMask);
  }

  bool DomainIs(const nsACString& aDomain) const {
    return mDomain.is<nsCString>() && mDomain.as<nsCString>() == aDomain;
  }

  bool DomainIs(const char* const* aPrefs) const {
    return mDomain == AsVariant(aPrefs);
  }

  bool Matches(const nsACString& aPrefName) const {
    auto match = [&](const nsACString& aStr) {
      return MatchKind() == Preferences::ExactMatch
                 ? aPrefName == aStr
                 : StringBeginsWith(aPrefName, aStr);
    };

    if (mDomain.is<nsCString>()) {
      return match(mDomain.as<nsCString>());
    }
    for (const char* const* ptr = mDomain.as<const char* const*>(); *ptr;
         ptr++) {
      if (match(nsDependentCString(*ptr))) {
        return true;
      }
    }
    return false;
  }

  CallbackNode* Next() const {
    return reinterpret_cast<CallbackNode*>(mNextAndMatchKind & kNextMask);
  }

  void SetNext(CallbackNode* aNext) {
    uintptr_t matchKind = mNextAndMatchKind & kMatchKindMask;
    mNextAndMatchKind = reinterpret_cast<uintptr_t>(aNext);
    MOZ_ASSERT((mNextAndMatchKind & kMatchKindMask) == 0);
    mNextAndMatchKind |= matchKind;
  }

  void AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf, PrefsSizes& aSizes) {
    aSizes.mCallbacksObjects += aMallocSizeOf(this);
    if (mDomain.is<nsCString>()) {
      aSizes.mCallbacksDomains +=
          mDomain.as<nsCString>().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }
  }

 private:
  static const uintptr_t kMatchKindMask = uintptr_t(0x1);
  static const uintptr_t kNextMask = ~kMatchKindMask;

  Variant<nsCString, const char* const*> mDomain;

  // If someone attempts to remove the node from the callback list while
  // NotifyCallbacks() is running, |func| is set to nullptr. Such nodes will
  // be removed at the end of NotifyCallbacks().
  PrefChangedFunc mFunc;
  void* mData;

  // Conceptually this is two fields:
  // - CallbackNode* mNext;
  // - Preferences::MatchKind mMatchKind;
  // They are combined into a tagged pointer to save memory.
  uintptr_t mNextAndMatchKind;
};

using PrefsHashTable = HashSet<UniquePtr<Pref>, PrefHasher>;

// The main prefs hash table. Inside a function so we can assert it's only
// accessed on the main thread. (That assertion can be avoided but only do so
// with great care!)
static inline PrefsHashTable*& HashTable(bool aOffMainThread = false) {
  MOZ_ASSERT(NS_IsMainThread() || ServoStyleSet::IsInServoTraversal());
  static PrefsHashTable* sHashTable = nullptr;
  return sHashTable;
}

#ifdef DEBUG
// This defines the type used to store our `once` mirrors checker. We can't use
// HashMap for now due to alignment restrictions when dealing with
// std::function<void()> (see bug 1557617).
typedef std::function<void()> AntiFootgunCallback;
struct CompareStr {
  bool operator()(char const* a, char const* b) const {
    return std::strcmp(a, b) < 0;
  }
};
typedef std::map<const char*, AntiFootgunCallback, CompareStr> AntiFootgunMap;
static StaticAutoPtr<AntiFootgunMap> gOnceStaticPrefsAntiFootgun;
#endif

// The callback list contains all the priority callbacks followed by the
// non-priority callbacks. gLastPriorityNode records where the first part ends.
static CallbackNode* gFirstCallback = nullptr;
static CallbackNode* gLastPriorityNode = nullptr;

#ifdef DEBUG
#  define ACCESS_COUNTS
#endif

#ifdef ACCESS_COUNTS
using AccessCountsHashTable = nsTHashMap<nsCStringHashKey, uint32_t>;
static StaticAutoPtr<AccessCountsHashTable> gAccessCounts;

static void AddAccessCount(const nsACString& aPrefName) {
  // FIXME: Servo reads preferences from background threads in unsafe ways (bug
  // 1474789), and triggers assertions here if we try to add usage count entries
  // from background threads.
  if (NS_IsMainThread()) {
    JS::AutoSuppressGCAnalysis nogc;  // Hash functions will not GC.
    uint32_t& count = gAccessCounts->LookupOrInsert(aPrefName);
    count++;
  }
}

static void AddAccessCount(const char* aPrefName) {
  AddAccessCount(nsDependentCString(aPrefName));
}
#else
[[maybe_unused]] static void AddAccessCount(const nsACString& aPrefName) {}

static void AddAccessCount(const char* aPrefName) {}
#endif

// These are only used during the call to NotifyCallbacks().
static bool gCallbacksInProgress = false;
static bool gShouldCleanupDeadNodes = false;

// IdleTaskRunner for sweeping expired weak observer callbacks.
static StaticRefPtr<mozilla::IdleTaskRunner> sSweepRunner;

class PrefsHashIter {
  using Iterator = decltype(HashTable()->modIter());
  using ElemType = Pref*;

  Iterator mIter;

 public:
  explicit PrefsHashIter(PrefsHashTable* aTable) : mIter(aTable->modIter()) {}

  class Elem {
    friend class PrefsHashIter;

    PrefsHashIter& mParent;
    bool mDone;

    Elem(PrefsHashIter& aIter, bool aDone) : mParent(aIter), mDone(aDone) {}

    Iterator& Iter() { return mParent.mIter; }

   public:
    Elem& operator*() { return *this; }

    ElemType get() {
      if (mDone) {
        return nullptr;
      }
      return Iter().get().get();
    }
    ElemType get() const { return const_cast<Elem*>(this)->get(); }

    ElemType operator->() { return get(); }
    ElemType operator->() const { return get(); }

    operator ElemType() { return get(); }

    void Remove() { Iter().remove(); }

    Elem& operator++() {
      MOZ_ASSERT(!mDone);
      Iter().next();
      mDone = Iter().done();
      return *this;
    }

    bool operator!=(Elem& other) {
      return mDone != other.mDone || this->get() != other.get();
    }
  };

  Elem begin() { return Elem(*this, mIter.done()); }

  Elem end() { return Elem(*this, true); }
};

class PrefsIter {
  using Iterator = decltype(HashTable()->iter());
  using ElemType = PrefWrapper;

  using HashElem = PrefsHashIter::Elem;
  using SharedElem = SharedPrefMap::Pref;

  using ElemTypeVariant = Variant<HashElem, SharedElem>;

  SharedPrefMap* mSharedMap;
  PrefsHashTable* mHashTable;
  PrefsHashIter mIter;

  ElemTypeVariant mPos;
  ElemTypeVariant mEnd;

  Maybe<PrefWrapper> mEntry;

 public:
  PrefsIter(PrefsHashTable* aHashTable, SharedPrefMap* aSharedMap)
      : mSharedMap(aSharedMap),
        mHashTable(aHashTable),
        mIter(aHashTable),
        mPos(AsVariant(mIter.begin())),
        mEnd(AsVariant(mIter.end())) {
    if (Done()) {
      NextIterator();
    }
  }

 private:
#define MATCH(type, ...)                                                \
  do {                                                                  \
    struct Matcher {                                                    \
      PrefsIter& mIter;                                                 \
      type operator()(HashElem& pos) {                                  \
        [[maybe_unused]] HashElem& end = mIter.mEnd.as<HashElem>();     \
        __VA_ARGS__;                                                    \
      }                                                                 \
      type operator()(SharedElem& pos) {                                \
        [[maybe_unused]] SharedElem& end = mIter.mEnd.as<SharedElem>(); \
        __VA_ARGS__;                                                    \
      }                                                                 \
    };                                                                  \
    return mPos.match(Matcher{*this});                                  \
  } while (0);

  bool Done() { MATCH(bool, return pos == end); }

  PrefWrapper MakeEntry() { MATCH(PrefWrapper, return PrefWrapper(pos)); }

  void NextEntry() {
    mEntry.reset();
    MATCH(void, ++pos);
  }
#undef MATCH

  bool Next() {
    NextEntry();
    return !Done() || NextIterator();
  }

  bool NextIterator() {
    if (mPos.is<HashElem>() && mSharedMap) {
      mPos = AsVariant(mSharedMap->begin());
      mEnd = AsVariant(mSharedMap->end());
      return !Done();
    }
    return false;
  }

  bool IteratingBase() { return mPos.is<SharedElem>(); }

  PrefWrapper& Entry() {
    MOZ_ASSERT(!Done());

    if (!mEntry.isSome()) {
      mEntry.emplace(MakeEntry());
    }
    return mEntry.ref();
  }

 public:
  class Elem {
    friend class PrefsIter;

    PrefsIter& mParent;
    bool mDone;

    Elem(PrefsIter& aIter, bool aDone) : mParent(aIter), mDone(aDone) {
      SkipDuplicates();
    }

    void Next() { mDone = !mParent.Next(); }

    void SkipDuplicates() {
      while (!mDone &&
             (mParent.IteratingBase() ? mParent.mHashTable->has(ref().Name())
                                      : ref().IsTypeNone())) {
        Next();
      }
    }

   public:
    Elem& operator*() { return *this; }

    ElemType& ref() { return mParent.Entry(); }
    const ElemType& ref() const { return const_cast<Elem*>(this)->ref(); }

    ElemType* operator->() { return &ref(); }
    const ElemType* operator->() const { return &ref(); }

    operator ElemType() { return ref(); }

    Elem& operator++() {
      MOZ_ASSERT(!mDone);
      Next();
      SkipDuplicates();
      return *this;
    }

    bool operator!=(Elem& other) {
      if (mDone != other.mDone) {
        return true;
      }
      if (mDone) {
        return false;
      }
      return &this->ref() != &other.ref();
    }
  };

  Elem begin() { return {*this, Done()}; }

  Elem end() { return {*this, true}; }
};

static Pref* pref_HashTableLookup(const char* aPrefName);

static void NotifyCallbacks(const nsCString& aPrefName,
                            const PrefWrapper* aPref = nullptr);

static void NotifyCallbacks(const nsCString& aPrefName,
                            const PrefWrapper& aPref) {
  NotifyCallbacks(aPrefName, &aPref);
}

// The approximate number of preferences in the dynamic hashtable for the parent
// and content processes, respectively. These numbers are used to determine the
// initial size of the dynamic preference hashtables, and should be chosen to
// avoid rehashing during normal usage. The actual number of preferences will,
// or course, change over time, but these numbers only need to be within a
// binary order of magnitude of the actual values to remain effective.
//
// The number for the parent process should reflect the total number of
// preferences in the database, since the parent process needs to initially
// build a dynamic hashtable of the entire preference database. The number for
// the child process should reflect the number of preferences which are likely
// to change after the startup of the first content process, since content
// processes only store changed preferences on top of a snapshot of the database
// created at startup.
//
// Note: The capacity of a hashtable doubles when its length reaches an exact
// power of two. A table with an initial length of 64 is twice as large as one
// with an initial length of 63. This is important in content processes, where
// lookup speed is less critical and we pay the price of the additional overhead
// for each content process. So the initial content length should generally be
// *under* the next power-of-two larger than its expected length.
constexpr size_t kHashTableInitialLengthParent = 3000;
constexpr size_t kHashTableInitialLengthContent = 64;

static PrefSaveData pref_savePrefs(const nsIPrefOverrideMap* aPrefOverrideMap) {
  MOZ_ASSERT(NS_IsMainThread());

  PrefSaveData savedPrefs(HashTable()->count());

  for (auto& pref : PrefsIter(HashTable(), gSharedMap)) {
    nsAutoCString prefValueStr;
    if (!pref->UserValueToStringForSaving(prefValueStr, aPrefOverrideMap)) {
      continue;
    }

    nsAutoCString prefNameStr;
    StrEscape(pref->Name(), prefNameStr);

    nsPrintfCString str("user_pref(%s, %s);", prefNameStr.get(),
                        prefValueStr.get());

    savedPrefs.AppendElement(str);
  }

  return savedPrefs;
}

static Pref* pref_HashTableLookup(const char* aPrefName) {
  MOZ_ASSERT(NS_IsMainThread() || ServoStyleSet::IsInServoTraversal());

  MOZ_ASSERT_IF(!XRE_IsParentProcess(), gContentProcessPrefsAreInited);

  // We use readonlyThreadsafeLookup() because we often have concurrent lookups
  // from multiple Stylo threads. This is safe because those threads cannot
  // modify sHashTable, and the main thread is blocked while Stylo threads are
  // doing these lookups.
  auto p = HashTable()->readonlyThreadsafeLookup(aPrefName);
  return p ? p->get() : nullptr;
}

// While notifying preference callbacks, this holds the wrapper for the
// preference being notified, in order to optimize lookups.
//
// Note: Callbacks and lookups only happen on the main thread, so this is safe
// to use without locking.
static const PrefWrapper* gCallbackPref;

Maybe<PrefWrapper> pref_SharedLookup(const char* aPrefName) {
  MOZ_DIAGNOSTIC_ASSERT(gSharedMap, "gSharedMap must be initialized");
  if (Maybe<SharedPrefMap::Pref> pref = gSharedMap->Get(aPrefName)) {
    return Some(*pref);
  }
  return Nothing();
}

Maybe<PrefWrapper> pref_Lookup(const char* aPrefName,
                               bool aIncludeTypeNone = false) {
  MOZ_ASSERT(NS_IsMainThread() || ServoStyleSet::IsInServoTraversal());

  AddAccessCount(aPrefName);

  if (gCallbackPref && strcmp(aPrefName, gCallbackPref->Name()) == 0) {
    return Some(*gCallbackPref);
  }
  if (Pref* pref = pref_HashTableLookup(aPrefName)) {
    if (aIncludeTypeNone || !pref->IsTypeNone() || pref->IsSanitized()) {
      return Some(pref);
    }
  } else if (gSharedMap) {
    return pref_SharedLookup(aPrefName);
  }

  return Nothing();
}

static Result<Pref*, nsresult> pref_LookupForModify(
    const char* aPrefName,
    const std::function<bool(const PrefWrapper&)>& aCheckFn) {
  Maybe<PrefWrapper> wrapper =
      pref_Lookup(aPrefName, /* includeTypeNone */ true);
  if (wrapper.isNothing()) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  if (!aCheckFn(*wrapper)) {
    return nullptr;
  }
  if (wrapper->is<Pref*>()) {
    return wrapper->as<Pref*>();
  }

  Pref* pref = new Pref(nsDependentCString{aPrefName});
  if (!HashTable()->putNew(aPrefName, pref)) {
    delete pref;
    return Err(NS_ERROR_OUT_OF_MEMORY);
  }
  pref->FromWrapper(*wrapper);
  return pref;
}

static nsresult pref_SetPref(const nsCString& aPrefName, PrefType aType,
                             PrefValueKind aKind, PrefValue aValue,
                             bool aIsSticky, bool aIsLocked, bool aFromInit) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads)) {
    printf(
        "pref_SetPref: Attempt to write pref %s after XPCOMShutdownThreads "
        "started.\n",
        aPrefName.get());
    if (nsContentUtils::IsInitialized()) {
      xpc_DumpJSStack(true, true, false);
    }
    MOZ_ASSERT(false, "Late preference writes should be avoided.");
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!HashTable()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  Pref* pref = nullptr;
  if (gSharedMap) {
    auto result =
        pref_LookupForModify(aPrefName.get(), [&](const PrefWrapper& aWrapper) {
          return !aWrapper.Matches(aType, aKind, aValue, aIsSticky, aIsLocked);
        });
    if (result.isOk() && !(pref = result.unwrap())) {
      // No changes required.
      return NS_OK;
    }
  }

  if (!pref) {
    auto p = HashTable()->lookupForAdd(aPrefName.get());
    if (!p) {
      pref = new Pref(aPrefName);
      pref->SetType(aType);
      if (!HashTable()->add(p, pref)) {
        delete pref;
        return NS_ERROR_OUT_OF_MEMORY;
      }
    } else {
      pref = p->get();
    }
  }

  bool valueChanged = false;
  nsresult rv;
  if (aKind == PrefValueKind::Default) {
    rv = pref->SetDefaultValue(aType, aValue, aIsSticky, aIsLocked,
                               &valueChanged);
  } else {
    MOZ_ASSERT(!aIsLocked);  // `locked` is disallowed in user pref files
    rv = pref->SetUserValue(aType, aValue, aFromInit, &valueChanged);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING(
        nsPrintfCString("Rejected attempt to change type of pref %s's %s value "
                        "from %s to %s",
                        aPrefName.get(),
                        (aKind == PrefValueKind::Default) ? "default" : "user",
                        PrefTypeToString(pref->Type()), PrefTypeToString(aType))
            .get());

    return rv;
  }

  if (valueChanged) {
    if (!aFromInit && profiler_thread_is_being_profiled_for_markers()) {
      nsAutoCString value;
      aValue.ToString(aType, value);
      profiler_add_marker(
          "Preference Write", baseprofiler::category::OTHER_PreferenceRead, {},
          PreferenceMarker{}, aPrefName, Some(aKind), aType, value);
    }

    if (aKind == PrefValueKind::User) {
      Preferences::HandleDirty();
    }
    NotifyCallbacks(aPrefName, PrefWrapper(pref));
  }

  return NS_OK;
}

// Removes |node| from callback list. Returns the node after the deleted one.
static CallbackNode* pref_RemoveCallbackNode(CallbackNode* aNode,
                                             CallbackNode* aPrevNode) {
  MOZ_ASSERT(!aPrevNode || aPrevNode->Next() == aNode);
  MOZ_ASSERT(aPrevNode || gFirstCallback == aNode);
  MOZ_ASSERT(!gCallbacksInProgress);

  CallbackNode* next_node = aNode->Next();
  if (aPrevNode) {
    aPrevNode->SetNext(next_node);
  } else {
    gFirstCallback = next_node;
  }
  if (gLastPriorityNode == aNode) {
    gLastPriorityNode = aPrevNode;
  }
  delete aNode;
  return next_node;
}

static void NotifyCallbacks(const nsCString& aPrefName,
                            const PrefWrapper* aPref) {
  bool reentered = gCallbacksInProgress;

  gCallbackPref = aPref;
  auto cleanup = MakeScopeExit([]() { gCallbackPref = nullptr; });

  // Nodes must not be deleted while gCallbacksInProgress is true.
  // Nodes that need to be deleted are marked for deletion by nulling
  // out the |func| pointer. We release them at the end of this function
  // if we haven't reentered.
  gCallbacksInProgress = true;

  for (CallbackNode* node = gFirstCallback; node; node = node->Next()) {
    if (node->Func()) {
      if (node->Matches(aPrefName)) {
        MOZ_LOG(sPrefLog, LogLevel::Debug,
                ("NotifyCallbacks: pref='%s' -> domain='%s'", aPrefName.get(),
                 node->DomainForLog()));
        (node->Func())(aPrefName.get(), node->Data());
      }
    }
  }

  gCallbacksInProgress = reentered;

  if (gShouldCleanupDeadNodes && !gCallbacksInProgress) {
    CallbackNode* prev_node = nullptr;
    CallbackNode* node = gFirstCallback;

    while (node) {
      if (!node->Func()) {
        node = pref_RemoveCallbackNode(node, prev_node);
      } else {
        prev_node = node;
        node = node->Next();
      }
    }
    gShouldCleanupDeadNodes = false;
  }

#ifdef DEBUG
  if (XRE_IsParentProcess() &&
      !StaticPrefs::preferences_force_disable_check_once_policy() &&
      (StaticPrefs::preferences_check_once_policy() || xpc::IsInAutomation())) {
    // Check that we aren't modifying a `once`-mirrored pref using that pref
    // name. We have about 100 `once`-mirrored prefs. std::map performs a
    // search in O(log n), so this is fast enough.
    MOZ_ASSERT(gOnceStaticPrefsAntiFootgun);
    auto search = gOnceStaticPrefsAntiFootgun->find(aPrefName.get());
    if (search != gOnceStaticPrefsAntiFootgun->end()) {
      // Run the callback.
      (search->second)();
    }
  }
#endif
}

//===========================================================================
// Prefs parsing
//===========================================================================

extern "C" {

// Keep this in sync with PrefFn in parser/src/lib.rs.
typedef void (*PrefsParserPrefFn)(const char* aPrefName, PrefType aType,
                                  PrefValueKind aKind, PrefValue aValue,
                                  bool aIsSticky, bool aIsLocked);

// Keep this in sync with ErrorFn in parser/src/lib.rs.
//
// `aFullMsg` is just a borrow of the string, and must be copied if it is used
// outside the lifetime of the prefs_parser_parse() call.
// `aStaticMsgOffset` is the offset of the static part of the string, which
// extends to the end of `aFullMsg` and is safe to send to telemetry.
typedef void (*PrefsParserErrorFn)(const char* aFullMsg,
                                   uint64_t aStaticMsgOffset);

// Keep this in sync with prefs_parser_parse() in parser/src/lib.rs.
bool prefs_parser_parse(const char* aPath, PrefValueKind aKind,
                        const char* aBuf, size_t aLen,
                        PrefsParserPrefFn aPrefFn, PrefsParserErrorFn aErrorFn);
}

class Parser {
 public:
  Parser() = default;
  ~Parser() = default;

  bool Parse(PrefValueKind aKind, const char* aPath, const nsCString& aBuf) {
    MOZ_ASSERT(XRE_IsParentProcess());
    return prefs_parser_parse(aPath, aKind, aBuf.get(), aBuf.Length(),
                              HandlePref, HandleError);
  }

  static void HandlePref(const char* aPrefName, PrefType aType,
                         PrefValueKind aKind, PrefValue aValue, bool aIsSticky,
                         bool aIsLocked) {
    MOZ_ASSERT(XRE_IsParentProcess());
    pref_SetPref(nsDependentCString(aPrefName), aType, aKind, aValue, aIsSticky,
                 aIsLocked,
                 /* fromInit */ true);
  }

  static void HandleError(const char* aFullMsg, uint64_t aStaticMsgOffset) {
    nsresult rv;
    nsCOMPtr<nsIConsoleService> console =
        do_GetService("@mozilla.org/consoleservice;1", &rv);
    if (NS_SUCCEEDED(rv)) {
      console->LogStringMessage(NS_ConvertUTF8toUTF16(aFullMsg).get());
    }
    // We're just going to log one of these per session of Firefox,
    // because we just want to get a sample of errors users are
    // encountering.
    static Atomic<bool, Relaxed> haveLoggedError(false);
    if (haveLoggedError.compareExchange(false, true)) {
      MOZ_RELEASE_ASSERT(aStaticMsgOffset < strlen(aFullMsg));
      // Since aFullMsg is a borrow of the string, we need to copy it
      // if we want to use it outside the lifetime of the prefs_parser_parse()
      // call, which I think Glean does. So don't use nsDependentCString here.
      glean::preferences::prefs_file_first_parse_error.Set(
          nsCString(aFullMsg + aStaticMsgOffset));
    }
#ifdef DEBUG
    NS_ERROR(aFullMsg);
#else
    printf_stderr("%s\n", aFullMsg);
#endif
  }
};

static nsresult parsePrefFileData(PrefValueKind aKind, const char* aPath,
                                  const nsCString& aData,
                                  PrefsParserPrefFn aPrefFn,
                                  PrefsParserErrorFn aErrorFn) {
  if (!prefs_parser_parse(aPath, aKind, aData.get(), aData.Length(), aPrefFn,
                          aErrorFn)) {
    return NS_ERROR_FILE_CORRUPTED;
  }
  return NS_OK;
}

// The following code is test code for the gtest.

static void TestParseErrorHandlePref(const char* aPrefName, PrefType aType,
                                     PrefValueKind aKind, PrefValue aValue,
                                     bool aIsSticky, bool aIsLocked) {}

constinit static nsCString gTestParseErrorMsgs;

static void TestParseErrorHandleError(const char* aFullMsg,
                                      uint64_t aStaticMsgOffset) {
  gTestParseErrorMsgs.Append(aFullMsg);
  gTestParseErrorMsgs.Append('\n');
  gTestParseErrorMsgs.Append(aFullMsg + aStaticMsgOffset);
  gTestParseErrorMsgs.Append('\n');
}

// Keep this in sync with the declaration in test/gtest/Parser.cpp.
nsresult TestParseError(PrefValueKind aKind, const char* aText,
                        nsCString& aErrorMsg) {
  nsCString text(aText);
  gTestParseErrorMsgs.Truncate();
  nsresult rv = parsePrefFileData(aKind, "test", text, TestParseErrorHandlePref,
                                  TestParseErrorHandleError);

  // Copy the error messages into the outparam
  aErrorMsg.Assign(gTestParseErrorMsgs);
  return rv;
}

//===========================================================================
// nsPrefBranch et al.
//===========================================================================

namespace mozilla {
class PreferenceServiceReporter;
}  // namespace mozilla

class PrefCallback : public PLDHashEntryHdr {
  friend class mozilla::PreferenceServiceReporter;

 public:
  typedef PrefCallback* KeyType;
  typedef const PrefCallback* KeyTypePointer;

  static const PrefCallback* KeyToPointer(PrefCallback* aKey) { return aKey; }

  static PLDHashNumber HashKey(const PrefCallback* aKey) {
    uint32_t hash = HashString(aKey->mDomain);
    // For weak callbacks, use the weak reference proxy as identity
    // (each observer gets a unique proxy that outlives the observer).
    if (aKey->IsWeak()) {
      return AddToHash(hash, aKey->mWeakRef.get());
    }
    return AddToHash(hash, aKey->mStrongRef.get());
  }

 public:
  // Create a PrefCallback with a strong reference to its observer.
  PrefCallback(const nsACString& aDomain, nsIObserver* aObserver,
               nsPrefBranch* aBranch)
      : mDomain(aDomain), mBranch(aBranch), mStrongRef(aObserver) {
    MOZ_COUNT_CTOR(PrefCallback);
  }

  // Create a PrefCallback with a weak reference to its observer.
  PrefCallback(const nsACString& aDomain, nsISupportsWeakReference* aObserver,
               nsPrefBranch* aBranch)
      : mDomain(aDomain),
        mBranch(aBranch),
        mWeakRef(do_GetWeakReference(aObserver)) {
    MOZ_COUNT_CTOR(PrefCallback);
  }

  // This is explicitly not a copy constructor.
  explicit PrefCallback(const PrefCallback*& aCopy)
      : mDomain(aCopy->mDomain),
        mBranch(aCopy->mBranch),
        mWeakRef(aCopy->mWeakRef),
        mStrongRef(aCopy->mStrongRef) {
    MOZ_COUNT_CTOR(PrefCallback);
  }

  PrefCallback(const PrefCallback&) = delete;
  PrefCallback(PrefCallback&&) = default;

  MOZ_COUNTED_DTOR(PrefCallback)

  bool KeyEquals(const PrefCallback* aKey) const {
    if (!mDomain.Equals(aKey->mDomain)) {
      return false;
    }
    // For weak callbacks, compare the weak reference proxy objects.
    // Each observer has a unique proxy that outlives the observer, so
    // this is safe even if the observer's address is reused.
    if (IsWeak()) {
      return mWeakRef == aKey->mWeakRef;
    }
    return mStrongRef == aKey->mStrongRef;
  }

  PrefCallback* GetKey() const { return const_cast<PrefCallback*>(this); }

  // Get a reference to the callback's observer, or null if the observer was
  // weakly referenced and has been destroyed.
  already_AddRefed<nsIObserver> GetObserver() const {
    if (!IsWeak()) {
      nsCOMPtr<nsIObserver> copy = mStrongRef;
      return copy.forget();
    }

    nsCOMPtr<nsIObserver> observer = do_QueryReferent(mWeakRef);
    return observer.forget();
  }

  const nsCString& GetDomain() const { return mDomain; }

  nsPrefBranch* GetPrefBranch() const { return mBranch; }

  // Has this callback's weak reference died?
  bool IsExpired() const {
    if (!IsWeak()) return false;

    nsCOMPtr<nsIObserver> observer(do_QueryReferent(mWeakRef));
    return !observer;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t n = aMallocSizeOf(this);
    n += mDomain.SizeOfExcludingThisIfUnshared(aMallocSizeOf);

    // All the other fields are non-owning pointers, so we don't measure them.

    return n;
  }

  enum { ALLOW_MEMMOVE = true };

 private:
  nsCString mDomain;
  nsPrefBranch* mBranch;

  // Exactly one of mWeakRef and mStrongRef should be non-null.
  nsWeakPtr mWeakRef;
  nsCOMPtr<nsIObserver> mStrongRef;

  bool IsWeak() const { return !!mWeakRef; }
};

class nsPrefBranch final : public nsIPrefBranch,
                           public nsIObserver,
                           public nsSupportsWeakReference {
  friend class mozilla::PreferenceServiceReporter;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPREFBRANCH
  NS_DECL_NSIOBSERVER

  nsPrefBranch(const char* aPrefRoot, PrefValueKind aKind);
  nsPrefBranch() = delete;

  static void NotifyObserver(const char* aNewpref, void* aData);
  static void SweepExpiredWeakObservers();

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

 private:
  using PrefName = nsCString;

  virtual ~nsPrefBranch();

  int32_t GetRootLength() const { return mPrefRoot.Length(); }

  nsresult GetDefaultFromPropertiesFile(const char* aPrefName,
                                        nsAString& aReturn);

  // As SetCharPref, but without any check on the length of |aValue|.
  nsresult SetCharPrefNoLengthCheck(const char* aPrefName,
                                    const nsACString& aValue);

  // Reject strings that are more than 1Mb, warn if strings are more than 16kb.
  nsresult CheckSanityOfStringLength(const char* aPrefName,
                                     const nsAString& aValue);
  nsresult CheckSanityOfStringLength(const char* aPrefName,
                                     const nsACString& aValue);
  nsresult CheckSanityOfStringLength(const char* aPrefName,
                                     const uint32_t aLength);

  PrefName GetPrefName(const char* aPrefName) const {
    return GetPrefName(nsDependentCString(aPrefName));
  }

  PrefName GetPrefName(const nsACString& aPrefName) const;

  void FreeObserverList(void);

  const nsCString mPrefRoot;
  PrefValueKind mKind;

  bool mFreeingObserverList;
  nsClassHashtable<PrefCallback, PrefCallback> mObservers;
};

class nsPrefLocalizedString final : public nsIPrefLocalizedString {
 public:
  nsPrefLocalizedString();

  NS_DECL_ISUPPORTS
  NS_FORWARD_NSISUPPORTSPRIMITIVE(mUnicodeString->)
  NS_FORWARD_NSISUPPORTSSTRING(mUnicodeString->)

  nsresult Init();

 private:
  virtual ~nsPrefLocalizedString();

  nsCOMPtr<nsISupportsString> mUnicodeString;
};

//----------------------------------------------------------------------------
// nsPrefBranch
//----------------------------------------------------------------------------

nsPrefBranch::nsPrefBranch(const char* aPrefRoot, PrefValueKind aKind)
    : mPrefRoot(aPrefRoot), mKind(aKind), mFreeingObserverList(false) {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    ++mRefCnt;  // must be > 0 when we call this, or we'll get deleted!

    // Add weakly so we don't have to clean up at shutdown.
    observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
    --mRefCnt;
  }
}

nsPrefBranch::~nsPrefBranch() { FreeObserverList(); }

NS_IMPL_ISUPPORTS(nsPrefBranch, nsIPrefBranch, nsIObserver,
                  nsISupportsWeakReference)

NS_IMETHODIMP
nsPrefBranch::GetRoot(nsACString& aRoot) {
  aRoot = mPrefRoot;
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::GetPrefType(const char* aPrefName,
                          nsIPrefBranch::PreferenceType* aRetVal) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& prefName = GetPrefName(aPrefName);
  *aRetVal = Preferences::GetType(prefName.get());
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::GetBoolPrefWithDefault(const char* aPrefName, bool aDefaultValue,
                                     uint8_t aArgc, bool* aRetVal) {
  nsresult rv = GetBoolPref(aPrefName, aRetVal);
  if (NS_FAILED(rv) && aArgc == 1) {
    *aRetVal = aDefaultValue;
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::GetBoolPref(const char* aPrefName, bool* aRetVal) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::GetBool(pref.get(), aRetVal, mKind);
}

NS_IMETHODIMP
nsPrefBranch::SetBoolPref(const char* aPrefName, bool aValue) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::SetBool(pref.get(), aValue, mKind);
}

NS_IMETHODIMP
nsPrefBranch::GetFloatPrefWithDefault(const char* aPrefName,
                                      float aDefaultValue, uint8_t aArgc,
                                      float* aRetVal) {
  nsresult rv = GetFloatPref(aPrefName, aRetVal);

  if (NS_FAILED(rv) && aArgc == 1) {
    *aRetVal = aDefaultValue;
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::GetFloatPref(const char* aPrefName, float* aRetVal) {
  NS_ENSURE_ARG(aPrefName);

  nsAutoCString stringVal;
  nsresult rv = GetCharPref(aPrefName, stringVal);
  if (NS_SUCCEEDED(rv)) {
    // ParsePrefFloat() does a locale-independent conversion.
    *aRetVal = ParsePrefFloat(stringVal, &rv);
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::GetCharPrefWithDefault(const char* aPrefName,
                                     const nsACString& aDefaultValue,
                                     uint8_t aArgc, nsACString& aRetVal) {
  nsresult rv = GetCharPref(aPrefName, aRetVal);

  if (NS_FAILED(rv) && aArgc == 1) {
    aRetVal = aDefaultValue;
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::GetCharPref(const char* aPrefName, nsACString& aRetVal) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::GetCString(pref.get(), aRetVal, mKind);
}

NS_IMETHODIMP
nsPrefBranch::SetCharPref(const char* aPrefName, const nsACString& aValue) {
  nsresult rv = CheckSanityOfStringLength(aPrefName, aValue);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return SetCharPrefNoLengthCheck(aPrefName, aValue);
}

nsresult nsPrefBranch::SetCharPrefNoLengthCheck(const char* aPrefName,
                                                const nsACString& aValue) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::SetCString(pref.get(), aValue, mKind);
}

NS_IMETHODIMP
nsPrefBranch::GetStringPref(const char* aPrefName,
                            const nsACString& aDefaultValue, uint8_t aArgc,
                            nsACString& aRetVal) {
  nsCString utf8String;
  nsresult rv = GetCharPref(aPrefName, utf8String);
  if (NS_SUCCEEDED(rv)) {
    aRetVal = std::move(utf8String);
    return rv;
  }

  if (aArgc == 1) {
    aRetVal = aDefaultValue;
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::SetStringPref(const char* aPrefName, const nsACString& aValue) {
  nsresult rv = CheckSanityOfStringLength(aPrefName, aValue);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return SetCharPrefNoLengthCheck(aPrefName, aValue);
}

NS_IMETHODIMP
nsPrefBranch::GetIntPrefWithDefault(const char* aPrefName,
                                    int32_t aDefaultValue, uint8_t aArgc,
                                    int32_t* aRetVal) {
  nsresult rv = GetIntPref(aPrefName, aRetVal);

  if (NS_FAILED(rv) && aArgc == 1) {
    *aRetVal = aDefaultValue;
    return NS_OK;
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::GetIntPref(const char* aPrefName, int32_t* aRetVal) {
  NS_ENSURE_ARG(aPrefName);
  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::GetInt(pref.get(), aRetVal, mKind);
}

NS_IMETHODIMP
nsPrefBranch::SetIntPref(const char* aPrefName, int32_t aValue) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::SetInt(pref.get(), aValue, mKind);
}

NS_IMETHODIMP
nsPrefBranch::GetComplexValue(const char* aPrefName, const nsIID& aType,
                              void** aRetVal) {
  NS_ENSURE_ARG(aPrefName);

  nsresult rv;
  nsAutoCString utf8String;

  // We have to do this one first because it's different to all the rest.
  if (aType.Equals(NS_GET_IID(nsIPrefLocalizedString))) {
    nsCOMPtr<nsIPrefLocalizedString> theString(
        do_CreateInstance(NS_PREFLOCALIZEDSTRING_CONTRACTID, &rv));
    if (NS_FAILED(rv)) {
      return rv;
    }

    const PrefName& pref = GetPrefName(aPrefName);
    bool bNeedDefault = false;

    if (mKind == PrefValueKind::Default) {
      bNeedDefault = true;
    } else {
      // if there is no user (or locked) value
      if (!Preferences::HasUserValue(pref.get()) &&
          !Preferences::IsLocked(pref.get())) {
        bNeedDefault = true;
      }
    }

    // if we need to fetch the default value, do that instead, otherwise use the
    // value we pulled in at the top of this function
    if (bNeedDefault) {
      nsAutoString utf16String;
      rv = GetDefaultFromPropertiesFile(pref.get(), utf16String);
      if (NS_SUCCEEDED(rv)) {
        theString->SetData(utf16String);
      }
    } else {
      rv = GetCharPref(aPrefName, utf8String);
      if (NS_SUCCEEDED(rv)) {
        theString->SetData(NS_ConvertUTF8toUTF16(utf8String));
      }
    }

    if (NS_SUCCEEDED(rv)) {
      theString.forget(reinterpret_cast<nsIPrefLocalizedString**>(aRetVal));
    }

    return rv;
  }

  // if we can't get the pref, there's no point in being here
  rv = GetCharPref(aPrefName, utf8String);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (aType.Equals(NS_GET_IID(nsIFile))) {
    ENSURE_PARENT_PROCESS("GetComplexValue(nsIFile)", aPrefName);
    MOZ_TRY(NS_NewLocalFileWithPersistentDescriptor(
        utf8String, reinterpret_cast<nsIFile**>(aRetVal)));
    return NS_OK;
  }

  if (aType.Equals(NS_GET_IID(nsIRelativeFilePref))) {
    ENSURE_PARENT_PROCESS("GetComplexValue(nsIRelativeFilePref)", aPrefName);

    nsACString::const_iterator keyBegin, strEnd;
    utf8String.BeginReading(keyBegin);
    utf8String.EndReading(strEnd);

    // The pref has the format: [fromKey]a/b/c
    if (*keyBegin++ != '[') {
      return NS_ERROR_FAILURE;
    }

    nsACString::const_iterator keyEnd(keyBegin);
    if (!FindCharInReadable(']', keyEnd, strEnd)) {
      return NS_ERROR_FAILURE;
    }

    nsAutoCString key(Substring(keyBegin, keyEnd));

    nsCOMPtr<nsIFile> fromFile;
    nsCOMPtr<nsIProperties> directoryService(
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = directoryService->Get(key.get(), NS_GET_IID(nsIFile),
                               getter_AddRefs(fromFile));
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsCOMPtr<nsIFile> theFile;
    MOZ_TRY(NS_NewLocalFileWithRelativeDescriptor(
        fromFile, Substring(++keyEnd, strEnd), getter_AddRefs(theFile)));

    nsCOMPtr<nsIRelativeFilePref> relativePref = new nsRelativeFilePref();
    (void)relativePref->SetFile(theFile);
    (void)relativePref->SetRelativeToKey(key);

    relativePref.forget(reinterpret_cast<nsIRelativeFilePref**>(aRetVal));
    return NS_OK;
  }

  NS_WARNING("nsPrefBranch::GetComplexValue - Unsupported interface type");
  return NS_NOINTERFACE;
}

nsresult nsPrefBranch::CheckSanityOfStringLength(const char* aPrefName,
                                                 const nsAString& aValue) {
  return CheckSanityOfStringLength(aPrefName, aValue.Length());
}

nsresult nsPrefBranch::CheckSanityOfStringLength(const char* aPrefName,
                                                 const nsACString& aValue) {
  return CheckSanityOfStringLength(aPrefName, aValue.Length());
}

nsresult nsPrefBranch::CheckSanityOfStringLength(const char* aPrefName,
                                                 const uint32_t aLength) {
  if (aLength > MAX_PREF_LENGTH) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (aLength <= MAX_ADVISABLE_PREF_LENGTH) {
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIConsoleService> console =
      do_GetService("@mozilla.org/consoleservice;1", &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString message(nsPrintfCString(
      "Warning: attempting to write %d bytes to preference %s. This is bad "
      "for general performance and memory usage. Such an amount of data "
      "should rather be written to an external file.",
      aLength, GetPrefName(aPrefName).get()));

  rv = console->LogStringMessage(NS_ConvertUTF8toUTF16(message).get());
  if (NS_FAILED(rv)) {
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::SetComplexValue(const char* aPrefName, const nsIID& aType,
                              nsISupports* aValue) {
  ENSURE_PARENT_PROCESS("SetComplexValue", aPrefName);
  NS_ENSURE_ARG(aPrefName);

  nsresult rv = NS_NOINTERFACE;

  if (aType.Equals(NS_GET_IID(nsIFile))) {
    nsCOMPtr<nsIFile> file = do_QueryInterface(aValue);
    if (!file) {
      return NS_NOINTERFACE;
    }

    nsAutoCString descriptorString;
    rv = file->GetPersistentDescriptor(descriptorString);
    if (NS_SUCCEEDED(rv)) {
      rv = SetCharPrefNoLengthCheck(aPrefName, descriptorString);
    }
    return rv;
  }

  if (aType.Equals(NS_GET_IID(nsIRelativeFilePref))) {
    nsCOMPtr<nsIRelativeFilePref> relFilePref = do_QueryInterface(aValue);
    if (!relFilePref) {
      return NS_NOINTERFACE;
    }

    nsCOMPtr<nsIFile> file;
    relFilePref->GetFile(getter_AddRefs(file));
    if (!file) {
      return NS_NOINTERFACE;
    }

    nsAutoCString relativeToKey;
    (void)relFilePref->GetRelativeToKey(relativeToKey);

    nsCOMPtr<nsIFile> relativeToFile;
    nsCOMPtr<nsIProperties> directoryService(
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = directoryService->Get(relativeToKey.get(), NS_GET_IID(nsIFile),
                               getter_AddRefs(relativeToFile));
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsAutoCString relDescriptor;
    rv = file->GetRelativeDescriptor(relativeToFile, relDescriptor);
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsAutoCString descriptorString;
    descriptorString.Append('[');
    descriptorString.Append(relativeToKey);
    descriptorString.Append(']');
    descriptorString.Append(relDescriptor);
    return SetCharPrefNoLengthCheck(aPrefName, descriptorString);
  }

  if (aType.Equals(NS_GET_IID(nsIPrefLocalizedString))) {
    nsCOMPtr<nsISupportsString> theString = do_QueryInterface(aValue);

    if (theString) {
      nsString wideString;

      rv = theString->GetData(wideString);
      if (NS_SUCCEEDED(rv)) {
        // Check sanity of string length before any lengthy conversion
        rv = CheckSanityOfStringLength(aPrefName, wideString);
        if (NS_FAILED(rv)) {
          return rv;
        }
        rv = SetCharPrefNoLengthCheck(aPrefName,
                                      NS_ConvertUTF16toUTF8(wideString));
      }
    }
    return rv;
  }

  NS_WARNING("nsPrefBranch::SetComplexValue - Unsupported interface type");
  return NS_NOINTERFACE;
}

NS_IMETHODIMP
nsPrefBranch::ClearUserPref(const char* aPrefName) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::ClearUser(pref.get());
}

NS_IMETHODIMP
nsPrefBranch::PrefHasUserValue(const char* aPrefName, bool* aRetVal) {
  NS_ENSURE_ARG_POINTER(aRetVal);
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  *aRetVal = Preferences::HasUserValue(pref.get());
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::PrefHasDefaultValue(const char* aPrefName, bool* aRetVal) {
  NS_ENSURE_ARG_POINTER(aRetVal);
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  *aRetVal = Preferences::HasDefaultValue(pref.get());
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::LockPref(const char* aPrefName) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::Lock(pref.get());
}

NS_IMETHODIMP
nsPrefBranch::PrefIsLocked(const char* aPrefName, bool* aRetVal) {
  NS_ENSURE_ARG_POINTER(aRetVal);
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  *aRetVal = Preferences::IsLocked(pref.get());
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::PrefIsSanitized(const char* aPrefName, bool* aRetVal) {
  NS_ENSURE_ARG_POINTER(aRetVal);
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  *aRetVal = Preferences::IsSanitized(pref.get());
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::UnlockPref(const char* aPrefName) {
  NS_ENSURE_ARG(aPrefName);

  const PrefName& pref = GetPrefName(aPrefName);
  return Preferences::Unlock(pref.get());
}

NS_IMETHODIMP
nsPrefBranch::DeleteBranch(const char* aStartingAt) {
  ENSURE_PARENT_PROCESS("DeleteBranch", aStartingAt);
  NS_ENSURE_ARG(aStartingAt);

  MOZ_ASSERT(NS_IsMainThread());

  if (!HashTable()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const PrefName& pref = GetPrefName(aStartingAt);
  nsAutoCString branchName(pref.get());

  // Add a trailing '.' if it doesn't already have one.
  if (branchName.Length() > 1 && !StringEndsWith(branchName, "."_ns)) {
    branchName += '.';
  }

  const nsACString& branchNameNoDot =
      Substring(branchName, 0, branchName.Length() - 1);

  // Collect the list of prefs to remove
  AutoTArray<const char*, 32> prefNames;
  for (auto& pref : PrefsIter(HashTable(), gSharedMap)) {
    // Match preferences that start with branchName or equal branchNameNoDot.
    // For inputs ending with "..", this matches the node without the trailing
    // dot and all children with the double dot prefix.
    // For other inputs, this matches both the node itself and all children.
    if (StringBeginsWith(pref->NameString(), branchName) ||
        pref->NameString() == branchNameNoDot) {
      prefNames.AppendElement(pref->Name());
    }
  }

  // Remove the listed preferences.
  for (auto& prefName : prefNames) {
    auto result = pref_LookupForModify(
        prefName, [](const PrefWrapper& aPref) { return !aPref.IsTypeNone(); });
    if (result.isErr()) {
      // Pref was likely removed by a previously-notified callback
      continue;
    }

    if (Pref* pref = result.unwrap()) {
      pref->ClearUserValue();
      pref->ClearDefaultValue();

      MOZ_ASSERT(
          !gSharedMap || !pref->IsSanitized() || !gSharedMap->Has(pref->Name()),
          "A sanitized pref should never be in the shared pref map.");
      if (!pref->IsSanitized() &&
          (!gSharedMap || !gSharedMap->Has(pref->Name()))) {
        HashTable()->remove(prefName);
      } else {
        // If there is a matching shared pref, it must be shadowed by an empty
        // entry in the HashTable().
        pref->SetType(PrefType::None);
      }
      NotifyCallbacks(nsDependentCString{prefName});
    }
  }

  Preferences::HandleDirty();
  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::GetChildList(const char* aStartingAt,
                           nsTArray<nsCString>& aChildArray) {
  NS_ENSURE_ARG(aStartingAt);

  MOZ_ASSERT(NS_IsMainThread());

  // This will contain a list of all the pref name strings. Allocated on the
  // stack for speed.
  AutoTArray<nsCString, 32> prefArray;

  const PrefName& parent = GetPrefName(aStartingAt);
  size_t parentLen = parent.Length();
  for (auto& pref : PrefsIter(HashTable(), gSharedMap)) {
    if (strncmp(pref->Name(), parent.get(), parentLen) == 0) {
      prefArray.AppendElement(pref->NameString());
    }
  }

  // Now that we've built up the list, run the callback on all the matching
  // elements.
  aChildArray.SetCapacity(prefArray.Length());
  for (auto& element : prefArray) {
    // we need to lop off mPrefRoot in case the user is planning to pass this
    // back to us because if they do we are going to add mPrefRoot again.
    aChildArray.AppendElement(Substring(element, mPrefRoot.Length()));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::AddObserverImpl(const nsACString& aDomain, nsIObserver* aObserver,
                              bool aHoldWeak) {
  UniquePtr<PrefCallback> pCallback;

  NS_ENSURE_ARG(aObserver);

  const nsCString& prefName = GetPrefName(aDomain);

  // Hold a weak reference to the observer if so requested.
  if (aHoldWeak) {
    nsCOMPtr<nsISupportsWeakReference> weakRefFactory =
        do_QueryInterface(aObserver);
    if (!weakRefFactory) {
      // The caller didn't give us a object that supports weak reference...
      // tell them.
      return NS_ERROR_INVALID_ARG;
    }

    // Construct a PrefCallback with a weak reference to the observer.
    pCallback = MakeUnique<PrefCallback>(prefName, weakRefFactory, this);

  } else {
    // Construct a PrefCallback with a strong reference to the observer.
    pCallback = MakeUnique<PrefCallback>(prefName, aObserver, this);
  }

  // Since weak and strong PrefCallbacks hash differently, WithEntryHandle
  // below cannot catch a cross-type duplicate. Handle gracefully:
  // - Adding weak over existing strong: keep the strong, nothing to do.
  // - Adding strong over existing weak: upgrade to strong.
  if (aHoldWeak) {
    PrefCallback strongKey(prefName, aObserver, this);
    if (mObservers.Contains(&strongKey)) {
      return NS_OK;
    }
  } else {
    nsCOMPtr<nsISupportsWeakReference> wrf = do_QueryInterface(aObserver);
    if (wrf) {
      PrefCallback weakKey(prefName, wrf, this);
      mozilla::UniquePtr<PrefCallback> existing;
      mObservers.Remove(&weakKey, &existing);
      if (existing) {
        Preferences::UnregisterCallback(
            NotifyObserver, prefName, existing.get(), Preferences::PrefixMatch);
      }
    }
  }

  mObservers.WithEntryHandle(pCallback.get(), [&](auto&& p) {
    if (p) {
      NS_WARNING(
          nsPrintfCString("Ignoring duplicate observer: %s", prefName.get())
              .get());
    } else {
      // We must pass a fully qualified preference name to the callback
      // aDomain == nullptr is the only possible failure, and we trapped it with
      // NS_ENSURE_ARG above.
      Preferences::RegisterCallback(NotifyObserver, prefName, pCallback.get(),
                                    Preferences::PrefixMatch,
                                    /* isPriority */ false);

      p.Insert(std::move(pCallback));
    }
  });

  return NS_OK;
}

NS_IMETHODIMP
nsPrefBranch::RemoveObserverImpl(const nsACString& aDomain,
                                 nsIObserver* aObserver) {
  NS_ENSURE_ARG(aObserver);

  nsresult rv = NS_OK;

  // If we're in the middle of a call to FreeObserverList, don't process this
  // RemoveObserver call -- the observer in question will be removed soon, if
  // it hasn't been already.
  //
  // It's important that we don't touch mObservers in any way -- even a Get()
  // which returns null might cause the hashtable to resize itself, which will
  // break the iteration in FreeObserverList.
  if (mFreeingObserverList) {
    return NS_OK;
  }

  // Remove the relevant PrefCallback from mObservers and get an owning pointer
  // to it. Unregister the callback first, and then let the owning pointer go
  // out of scope and destroy the callback.
  const nsCString& prefName = GetPrefName(aDomain);
  PrefCallback key(prefName, aObserver, this);
  mozilla::UniquePtr<PrefCallback> pCallback;
  mObservers.Remove(&key, &pCallback);

  // If the observer was added with aHoldWeak=true, the stored PrefCallback
  // uses a weak reference for identity. Try again with a weak-reference key.
  if (!pCallback) {
    nsCOMPtr<nsISupportsWeakReference> weakRefFactory =
        do_QueryInterface(aObserver);
    if (weakRefFactory) {
      PrefCallback weakKey(prefName, weakRefFactory, this);
      mObservers.Remove(&weakKey, &pCallback);
    }
  }

  if (pCallback) {
    rv = Preferences::UnregisterCallback(
        NotifyObserver, prefName, pCallback.get(), Preferences::PrefixMatch);
  }

  return rv;
}

NS_IMETHODIMP
nsPrefBranch::Observe(nsISupports* aSubject, const char* aTopic,
                      const char16_t* aData) {
  // Watch for xpcom shutdown and free our observers to eliminate any cyclic
  // references.
  if (!nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    FreeObserverList();
  }
  return NS_OK;
}

// Schedule an idle sweep of expired weak observer callbacks. This is purely
// a memory optimization — expired callbacks are inert (notifications skip
// them and they do not keep alive anything else), so there is no urgency.
static void MaybeScheduleExpiredWeakObserverSweep() {
  if (sSweepRunner) {
    return;
  }
  PROFILER_MARKER_UNTYPED("ScheduleWeakObserverSweep", OTHER);
  static const TimeDuration kMaxDelay = TimeDuration::FromSeconds(2);
  static const TimeDuration kMinBudget = TimeDuration::FromMilliseconds(15);
  sSweepRunner = IdleTaskRunner::Create(
      [](TimeStamp aDeadline) {
        nsPrefBranch::SweepExpiredWeakObservers();
        sSweepRunner = nullptr;
        return true;
      },
      "SweepExpiredWeakObservers"_ns, TimeDuration(), kMaxDelay, kMinBudget,
      false, nullptr);
}

/* static */
void nsPrefBranch::NotifyObserver(const char* aNewPref, void* aData) {
  PrefCallback* pCallback = (PrefCallback*)aData;

  nsCOMPtr<nsIObserver> observer = pCallback->GetObserver();
  if (!observer) {
    MaybeScheduleExpiredWeakObserverSweep();
    return;
  }

  // Remove any root this string may contain so as to not confuse the observer
  // by passing them something other than what they passed us as a topic.
  uint32_t len = pCallback->GetPrefBranch()->GetRootLength();
  nsDependentCString suffix(aNewPref + len);

  observer->Observe(static_cast<nsIPrefBranch*>(pCallback->GetPrefBranch()),
                    NS_PREFBRANCH_PREFCHANGE_TOPIC_ID,
                    NS_ConvertASCIItoUTF16(suffix).get());
}

size_t nsPrefBranch::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  n += mPrefRoot.SizeOfExcludingThisIfUnshared(aMallocSizeOf);

  n += mObservers.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mObservers) {
    const PrefCallback* data = entry.GetWeak();
    n += data->SizeOfIncludingThis(aMallocSizeOf);
  }

  return n;
}

void nsPrefBranch::FreeObserverList() {
  // Clearing mObservers may release the last strong reference to observers,
  // whose destructors may call RemoveObserver() re-entrantly. We set
  // mFreeingObserverList to suppress those calls, both to avoid modifying
  // mObservers during Clear() and to avoid redundant UnregisterCallback walks
  // (the bulk removal is already handled by UnregisterCallbacksForBranch).
  mFreeingObserverList = true;

  // Remove all callback nodes for this branch in a single pass through the
  // global callback list, instead of one pass per observer.
  DebugOnly<uint32_t> removed = Preferences::UnregisterCallbacksForBranch(this);
  MOZ_ASSERT(removed == mObservers.Count() || Preferences::sShutdown,
             "Callback list and mObservers are out of sync");
  mObservers.Clear();

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }

  mFreeingObserverList = false;
}

nsresult nsPrefBranch::GetDefaultFromPropertiesFile(const char* aPrefName,
                                                    nsAString& aReturn) {
  // The default value contains a URL to a .properties file.

  nsAutoCString propertyFileURL;
  nsresult rv = Preferences::GetCString(aPrefName, propertyFileURL,
                                        PrefValueKind::Default);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIStringBundleService> bundleService =
      components::StringBundle::Service();
  if (!bundleService) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIStringBundle> bundle;
  rv = bundleService->CreateBundle(propertyFileURL.get(),
                                   getter_AddRefs(bundle));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return bundle->GetStringFromName(aPrefName, aReturn);
}

nsPrefBranch::PrefName nsPrefBranch::GetPrefName(
    const nsACString& aPrefName) const {
  if (mPrefRoot.IsEmpty()) {
    return PrefName(PromiseFlatCString(aPrefName));
  }

  return PrefName(mPrefRoot + aPrefName);
}

// static
void nsPrefBranch::SweepExpiredWeakObservers() {
  MOZ_ASSERT(!gCallbacksInProgress);

  CallbackNode* prev_node = nullptr;
  CallbackNode* node = gFirstCallback;

  while (node) {
    if (node->Func() == nsPrefBranch::NotifyObserver) {
      auto* pCallback = static_cast<PrefCallback*>(node->Data());
      if (pCallback->IsExpired()) {
        // Remove will leave a dangling pointer on the node (and in pCallback),
        // but we're going to delete the node immediately after.
        pCallback->GetPrefBranch()->mObservers.Remove(pCallback);
        node = pref_RemoveCallbackNode(node, prev_node);
        continue;
      }
    }
    prev_node = node;
    node = node->Next();
  }
}

//----------------------------------------------------------------------------
// nsPrefLocalizedString
//----------------------------------------------------------------------------

nsPrefLocalizedString::nsPrefLocalizedString() = default;

nsPrefLocalizedString::~nsPrefLocalizedString() = default;

NS_IMPL_ISUPPORTS(nsPrefLocalizedString, nsIPrefLocalizedString,
                  nsISupportsString)

nsresult nsPrefLocalizedString::Init() {
  nsresult rv;
  mUnicodeString = do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);

  return rv;
}

//----------------------------------------------------------------------------
// nsPrefOverrideMap
//----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsPrefOverrideMap, nsIPrefOverrideMap)

NS_IMETHODIMP
nsPrefOverrideMap::AddEntry(const nsACString& aPrefName,
                            JS::Handle<JS::Value> aPrefValue, JSContext* aCx) {
  nsCString prefName(aPrefName);
  auto maybePrefWrapper = pref_Lookup(prefName.get());
  if (NS_WARN_IF(!maybePrefWrapper)) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }
  nsAutoCString str;
  auto jsValueToPrefValue = [&]() -> Result<Maybe<OwnedPrefValue>, nsresult> {
    switch (aPrefValue.type()) {
      case JS::ValueType::Boolean:
        if (NS_WARN_IF(!maybePrefWrapper->IsTypeBool())) {
          return Err(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
        }
        return Some(OwnedPrefValue(aPrefValue.toBoolean()));
      case JS::ValueType::Int32:
        if (NS_WARN_IF(!maybePrefWrapper->IsTypeInt())) {
          return Err(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
        }
        return Some(OwnedPrefValue(aPrefValue.toInt32()));
      case JS::ValueType::String: {
        if (NS_WARN_IF(!maybePrefWrapper->IsTypeString())) {
          return Err(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
        }
        if (NS_WARN_IF(!AssignJSString(aCx, str, aPrefValue.toString()))) {
          return Err(NS_ERROR_OUT_OF_MEMORY);
        }
        return Some(OwnedPrefValue(str.get()));
      }
      case JS::ValueType::Null:
        return Result<Maybe<OwnedPrefValue>, nsresult>(Nothing());
      default:
        NS_WARNING("Invalid type in nsPrefOverrideMap::AddEntry");
        return Err(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
    }
  };
  Maybe<OwnedPrefValue> prefValue = MOZ_TRY(jsValueToPrefValue());
  if (NS_WARN_IF(!mMap.put(prefName, prefValue))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPrefOverrideMap::GetEntry(const nsACString& aPrefName, JSContext* aCx,
                            JS::MutableHandle<JS::Value> aPrefValue) {
  nsCString prefName(aPrefName);
  auto maybePrefWrapper = pref_Lookup(prefName.get());
  if (NS_WARN_IF(!maybePrefWrapper)) {
    return NS_ERROR_DOM_NOT_FOUND_ERR;
  }
  auto prefType = maybePrefWrapper->Type();
  auto prefValueToJsValue = [&]() -> mozilla::Result<JS::Value, nsresult> {
    if (auto it = mMap.lookup(prefName)) {
      if (it->value().isNothing()) {
        return JS::NullValue();
      }
      switch (prefType) {
        case PrefType::Bool:
          return JS::BooleanValue(it->value()->GetPrefValue().Get<bool>());
        case PrefType::Int:
          return JS::Int32Value(it->value()->GetPrefValue().Get<int32_t>());
        case PrefType::String: {
          auto str = it->value()->GetPrefValue().Get<nsDependentCString>();
          return JS::StringValue(
              JS_NewStringCopyN(aCx, str.get(), str.Length()));
        }
        default:
          // Do not expect type NONE
          NS_WARNING("Invalid type in nsPrefOverrideMap::GetEntry");
          return Err(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
      }
    }
    return Err(NS_ERROR_ILLEGAL_VALUE);
  };
  auto ret = MOZ_TRY(prefValueToJsValue());
  aPrefValue.set(ret);
  return NS_OK;
}

//----------------------------------------------------------------------------
// nsRelativeFilePref
//----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsRelativeFilePref, nsIRelativeFilePref)

nsRelativeFilePref::nsRelativeFilePref() = default;

nsRelativeFilePref::~nsRelativeFilePref() = default;

NS_IMETHODIMP
nsRelativeFilePref::GetFile(nsIFile** aFile) {
  NS_ENSURE_ARG_POINTER(aFile);
  *aFile = mFile;
  NS_IF_ADDREF(*aFile);
  return NS_OK;
}

NS_IMETHODIMP
nsRelativeFilePref::SetFile(nsIFile* aFile) {
  mFile = aFile;
  return NS_OK;
}

NS_IMETHODIMP
nsRelativeFilePref::GetRelativeToKey(nsACString& aRelativeToKey) {
  aRelativeToKey.Assign(mRelativeToKey);
  return NS_OK;
}

NS_IMETHODIMP
nsRelativeFilePref::SetRelativeToKey(const nsACString& aRelativeToKey) {
  mRelativeToKey.Assign(aRelativeToKey);
  return NS_OK;
}

//===========================================================================
// class Preferences and related things
//===========================================================================

namespace mozilla {

#define INITIAL_PREF_FILES 10

void Preferences::HandleDirty() {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!HashTable() || !sPreferences) {
    return;
  }

  if (sPreferences->mProfileShutdown) {
    NS_WARNING("Setting user pref after profile shutdown.");
    return;
  }

  if (!sPreferences->mDirty) {
    sPreferences->mDirty = true;

    if (sPreferences->mCurrentFile && sPreferences->AllowOffMainThreadSave() &&
        !sPreferences->mSavePending) {
      sPreferences->mSavePending = true;
      static const int PREF_DELAY_MS = 500;
      NS_DelayedDispatchToCurrentThread(
          NewRunnableMethod("Preferences::SavePrefFileAsynchronous",
                            sPreferences.get(),
                            &Preferences::SavePrefFileAsynchronous),
          PREF_DELAY_MS);
    }
  }
}

static nsresult openPrefFile(nsIFile* aFile, PrefValueKind aKind);

static nsresult parsePrefData(const nsCString& aData, PrefValueKind aKind);

// Note: if sShutdown is true, sPreferences will be nullptr.
StaticRefPtr<Preferences> Preferences::sPreferences;
bool Preferences::sShutdown = false;

// This globally enables or disables OMT pref writing, both sync and async.
static int32_t sAllowOMTPrefWrite = -1;

// Write the preference data to a file.
class PreferencesWriter final {
 public:
  PreferencesWriter() = default;

  static nsresult Write(nsIFile* aFile, PrefSaveData& aPrefs) {
    nsCOMPtr<nsIOutputStream> outStreamSink;
    nsCOMPtr<nsIOutputStream> outStream;
    uint32_t writeAmount;
    nsresult rv;

    // Execute a "safe" save by saving through a tempfile.
    rv = NS_NewSafeLocalFileOutputStream(getter_AddRefs(outStreamSink), aFile,
                                         -1, 0600);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = NS_NewBufferedOutputStream(getter_AddRefs(outStream),
                                    outStreamSink.forget(), 4096);
    if (NS_FAILED(rv)) {
      return rv;
    }

    struct CharComparator {
      bool LessThan(const nsCString& aA, const nsCString& aB) const {
        return aA < aB;
      }

      bool Equals(const nsCString& aA, const nsCString& aB) const {
        return aA == aB;
      }
    };

    // Sort the preferences to make a readable file on disk.
    aPrefs.Sort(CharComparator());

    // Write out the file header.
    nsAutoCString preamble;
    ::GetPrefsJsPreamble(preamble);
    outStream->Write(preamble.get(), preamble.Length(), &writeAmount);

    for (nsCString& pref : aPrefs) {
      outStream->Write(pref.get(), pref.Length(), &writeAmount);
      outStream->Write(NS_LINEBREAK, NS_LINEBREAK_LEN, &writeAmount);
    }

    // Tell the safe output stream to overwrite the real prefs file.
    // (It'll abort if there were any errors during writing.)
    nsCOMPtr<nsISafeOutputStream> safeStream = do_QueryInterface(outStream);
    MOZ_ASSERT(safeStream, "expected a safe output stream!");
    if (safeStream) {
      rv = safeStream->Finish();
    }

#ifdef DEBUG
    if (NS_FAILED(rv)) {
      NS_WARNING("failed to save prefs file! possible data loss");
    }
#endif

    return rv;
  }

  static void Flush() {
    MOZ_DIAGNOSTIC_ASSERT(sPendingWriteCount >= 0);
    // SpinEventLoopUntil is unfortunate, but ultimately it's the best thing
    // we can do here given the constraint that we need to ensure that
    // the preferences on disk match what we have in memory. We could
    // easily perform the write here ourselves by doing exactly what
    // happens in PWRunnable::Run. This would be the right thing to do
    // if we're stuck here because other unrelated runnables are taking
    // a long time, and the wrong thing to do if PreferencesWriter::Write
    // is what takes a long time, as we would be trading a SpinEventLoopUntil
    // for a synchronous disk write, wherein we could not even spin the
    // event loop. Given that PWRunnable generally runs on a thread pool,
    // if we're stuck here, it's likely because of PreferencesWriter::Write
    // and not some other runnable. Thus, spin away.
    mozilla::SpinEventLoopUntil("PreferencesWriter::Flush"_ns,
                                []() { return sPendingWriteCount <= 0; });
  }

  // This is the data that all of the runnables (see below) will attempt
  // to write.  It will always have the most up to date version, or be
  // null, if the up to date information has already been written out.
  static Atomic<PrefSaveData*> sPendingWriteData;

  // This is the number of writes via PWRunnables which have been dispatched
  // but not yet completed. This is intended to be used by Flush to ensure
  // that there are no outstanding writes left incomplete, and thus our prefs
  // on disk are in sync with what we have in memory.
  static Atomic<int> sPendingWriteCount;

  // See PWRunnable::Run for details on why we need this lock.
  static StaticMutex sWritingToFile MOZ_UNANNOTATED;
};

Atomic<PrefSaveData*> PreferencesWriter::sPendingWriteData(nullptr);
Atomic<int> PreferencesWriter::sPendingWriteCount(0);
StaticMutex PreferencesWriter::sWritingToFile;

class PWRunnable : public Runnable {
 public:
  explicit PWRunnable(
      nsIFile* aFile,
      UniquePtr<MozPromiseHolder<Preferences::WritePrefFilePromise>>
          aPromiseHolder)
      : Runnable("PWRunnable"),
        mFile(aFile),
        mPromiseHolder(std::move(aPromiseHolder)) {}

  NS_IMETHOD Run() override {
    // Preference writes are handled a bit strangely, in that a "newer"
    // write is generally regarded as always better. For this reason,
    // sPendingWriteData can be overwritten multiple times before anyone
    // gets around to actually using it, minimizing writes. However,
    // once we've acquired sPendingWriteData we've reached a
    // "point of no return" and have to complete the write.
    //
    // Unfortunately, this design allows the following behaviour:
    //
    // 1. write1 is queued up
    // 2. thread1 acquires write1
    // 3. write2 is queued up
    // 4. thread2 acquires write2
    // 5. thread1 and thread2 concurrently clobber each other
    //
    // To avoid this, we use this lock to ensure that only one thread
    // at a time is trying to acquire the write, and when it does,
    // all other threads are prevented from acquiring writes until it
    // completes the write. New writes are still allowed to be queued
    // up in this time.
    //
    // Although it's atomic, the acquire needs to be guarded by the mutex
    // to avoid reordering of writes -- we don't want an older write to
    // run after a newer one. To avoid this causing too much waiting, we check
    // if sPendingWriteData is already null before acquiring the mutex. If it
    // is, then there's definitely no work to be done (or someone is in the
    // middle of doing it for us).
    //
    // Note that every time a new write is queued up, a new write task is
    // is also queued up, so there will always be a task that can see the newest
    // write.
    //
    // Ideally this lock wouldn't be necessary, and the PreferencesWriter
    // would be used more carefully, but it's hard to untangle all that.
    nsresult rv = NS_OK;
    if (PreferencesWriter::sPendingWriteData) {
      StaticMutexAutoLock lock(PreferencesWriter::sWritingToFile);
      // If we get a nullptr on the exchange, it means that somebody
      // else has already processed the request, and we can just return.
      UniquePtr<PrefSaveData> prefs(
          PreferencesWriter::sPendingWriteData.exchange(nullptr));
      if (prefs) {
        rv = PreferencesWriter::Write(mFile, *prefs);
        // Make a copy of these so we can have them in runnable lambda.
        // nsIFile is only there so that we would never release the
        // ref counted pointer off main thread.
        nsresult rvCopy = rv;
        nsCOMPtr<nsIFile> fileCopy(mFile);
        SchedulerGroup::Dispatch(NS_NewRunnableFunction(
            "Preferences::WriterRunnable",
            [fileCopy, rvCopy, promiseHolder = std::move(mPromiseHolder)] {
              MOZ_RELEASE_ASSERT(NS_IsMainThread());
              if (NS_FAILED(rvCopy)) {
                Preferences::HandleDirty();
              }
              if (promiseHolder) {
                promiseHolder->ResolveIfExists(true, __func__);
              }
            }));
      }
    }
    // We've completed the write to the best of our abilities, whether
    // we had prefs to write or another runnable got to them first. If
    // PreferencesWriter::Write failed, this is still correct as the
    // write is no longer outstanding, and the above HandleDirty call
    // will just start the cycle again.
    PreferencesWriter::sPendingWriteCount--;
    return rv;
  }

 private:
  ~PWRunnable() {
    if (mPromiseHolder) {
      mPromiseHolder->RejectIfExists(NS_ERROR_ABORT, __func__);
    }
  }

 protected:
  nsCOMPtr<nsIFile> mFile;
  UniquePtr<MozPromiseHolder<Preferences::WritePrefFilePromise>> mPromiseHolder;
};

// Although this is a member of Preferences, it measures sPreferences and
// several other global structures.
/* static */
void Preferences::AddSizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                                         PrefsSizes& aSizes) {
  if (!sPreferences) {
    return;
  }

  aSizes.mMisc += aMallocSizeOf(sPreferences.get());

  aSizes.mRootBranches +=
      static_cast<nsPrefBranch*>(sPreferences->mRootBranch.get())
          ->SizeOfIncludingThis(aMallocSizeOf) +
      static_cast<nsPrefBranch*>(sPreferences->mDefaultRootBranch.get())
          ->SizeOfIncludingThis(aMallocSizeOf);
}

/* static */
uint32_t Preferences::GetCallbackCount() {
  uint32_t count = 0;
  for (CallbackNode* node = gFirstCallback; node; node = node->Next()) {
    count++;
  }
  return count;
}

class PreferenceServiceReporter final : public nsIMemoryReporter {
  ~PreferenceServiceReporter() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER

 protected:
  static const uint32_t kSuspectReferentCount = 1000;
};

NS_IMPL_ISUPPORTS(PreferenceServiceReporter, nsIMemoryReporter)

MOZ_DEFINE_MALLOC_SIZE_OF(PreferenceServiceMallocSizeOf)

NS_IMETHODIMP
PreferenceServiceReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  MOZ_ASSERT(NS_IsMainThread());

  MallocSizeOf mallocSizeOf = PreferenceServiceMallocSizeOf;
  PrefsSizes sizes;

  Preferences::AddSizeOfIncludingThis(mallocSizeOf, sizes);

  if (HashTable()) {
    sizes.mHashTable += HashTable()->shallowSizeOfIncludingThis(mallocSizeOf);
    for (auto iter = HashTable()->iter(); !iter.done(); iter.next()) {
      iter.get()->AddSizeOfIncludingThis(mallocSizeOf, sizes);
    }
  }

  sizes.mPrefNameArena += PrefNameArena().SizeOfExcludingThis(mallocSizeOf);

  for (CallbackNode* node = gFirstCallback; node; node = node->Next()) {
    node->AddSizeOfIncludingThis(mallocSizeOf, sizes);
  }

  if (gSharedMap) {
    sizes.mMisc += mallocSizeOf(gSharedMap);
  }

#ifdef ACCESS_COUNTS
  if (gAccessCounts) {
    sizes.mMisc += gAccessCounts->ShallowSizeOfIncludingThis(mallocSizeOf);
  }
#endif

  MOZ_COLLECT_REPORT("explicit/preferences/hash-table", KIND_HEAP, UNITS_BYTES,
                     sizes.mHashTable, "Memory used by libpref's hash table.");

  MOZ_COLLECT_REPORT("explicit/preferences/pref-values", KIND_HEAP, UNITS_BYTES,
                     sizes.mPrefValues,
                     "Memory used by PrefValues hanging off the hash table.");

  MOZ_COLLECT_REPORT("explicit/preferences/string-values", KIND_HEAP,
                     UNITS_BYTES, sizes.mStringValues,
                     "Memory used by libpref's string pref values.");

  MOZ_COLLECT_REPORT("explicit/preferences/root-branches", KIND_HEAP,
                     UNITS_BYTES, sizes.mRootBranches,
                     "Memory used by libpref's root branches.");

  MOZ_COLLECT_REPORT("explicit/preferences/pref-name-arena", KIND_HEAP,
                     UNITS_BYTES, sizes.mPrefNameArena,
                     "Memory used by libpref's arena for pref names.");

  MOZ_COLLECT_REPORT("explicit/preferences/callbacks/objects", KIND_HEAP,
                     UNITS_BYTES, sizes.mCallbacksObjects,
                     "Memory used by pref callback objects.");

  MOZ_COLLECT_REPORT("explicit/preferences/callbacks/domains", KIND_HEAP,
                     UNITS_BYTES, sizes.mCallbacksDomains,
                     "Memory used by pref callback domains (pref names and "
                     "prefixes).");

  MOZ_COLLECT_REPORT("explicit/preferences/misc", KIND_HEAP, UNITS_BYTES,
                     sizes.mMisc, "Miscellaneous memory used by libpref.");

  if (gSharedMap) {
    if (XRE_IsParentProcess()) {
      MOZ_COLLECT_REPORT("explicit/preferences/shared-memory-map", KIND_NONHEAP,
                         UNITS_BYTES, gSharedMap->MapSize(),
                         "The shared memory mapping used to share a "
                         "snapshot of preference values across processes.");
    }
  }

  nsPrefBranch* rootBranch =
      static_cast<nsPrefBranch*>(Preferences::GetRootBranch());
  if (!rootBranch) {
    return NS_OK;
  }

  size_t numStrong = 0;
  size_t numWeakAlive = 0;
  size_t numWeakDead = 0;
  nsTArray<nsCString> suspectPreferences;
  // Count of the number of referents for each preference.
  nsTHashMap<nsCStringHashKey, uint32_t> prefCounter;

  for (const auto& entry : rootBranch->mObservers) {
    auto* callback = entry.GetWeak();

    if (callback->IsWeak()) {
      nsCOMPtr<nsIObserver> callbackRef = do_QueryReferent(callback->mWeakRef);
      if (callbackRef) {
        numWeakAlive++;
      } else {
        numWeakDead++;
      }
    } else {
      numStrong++;
    }

    const uint32_t currentCount = prefCounter.Get(callback->GetDomain()) + 1;
    prefCounter.InsertOrUpdate(callback->GetDomain(), currentCount);

    // Keep track of preferences that have a suspiciously large number of
    // referents (a symptom of a leak).
    if (currentCount == kSuspectReferentCount) {
      suspectPreferences.AppendElement(callback->GetDomain());
    }
  }

  for (uint32_t i = 0; i < suspectPreferences.Length(); i++) {
    nsCString& suspect = suspectPreferences[i];
    const uint32_t totalReferentCount = prefCounter.Get(suspect);

    nsPrintfCString suspectPath(
        "preference-service-suspect/"
        "referent(pref=%s)",
        suspect.get());

    aHandleReport->Callback(
        /* process = */ ""_ns, suspectPath, KIND_OTHER, UNITS_COUNT,
        totalReferentCount,
        "A preference with a suspiciously large number "
        "referents (symptom of a leak)."_ns,
        aData);
  }

  MOZ_COLLECT_REPORT(
      "preference-service/referent/strong", KIND_OTHER, UNITS_COUNT, numStrong,
      "The number of strong referents held by the preference service.");

  MOZ_COLLECT_REPORT(
      "preference-service/referent/weak/alive", KIND_OTHER, UNITS_COUNT,
      numWeakAlive,
      "The number of weak referents held by the preference service that are "
      "still alive.");

  MOZ_COLLECT_REPORT(
      "preference-service/referent/weak/dead", KIND_OTHER, UNITS_COUNT,
      numWeakDead,
      "The number of weak referents held by the preference service that are "
      "dead.");

  return NS_OK;
}

namespace {

class AddPreferencesMemoryReporterRunnable : public Runnable {
 public:
  AddPreferencesMemoryReporterRunnable()
      : Runnable("AddPreferencesMemoryReporterRunnable") {}

  NS_IMETHOD Run() override {
    return RegisterStrongMemoryReporter(
        MakeAndAddRef<PreferenceServiceReporter>());
  }
};

}  // namespace

// A list of changed prefs sent from the parent via shared memory.
static StaticAutoPtr<nsTArray<dom::Pref>> gChangedDomPrefs;

static const char kTelemetryPref[] = "toolkit.telemetry.enabled";
#if !(defined(MOZ_WIDGET_ANDROID) && defined(MOZ_TELEMETRY_ON_BY_DEFAULT))
static const char kChannelPref[] = "app.update.channel";
#endif

#ifdef MOZ_WIDGET_ANDROID

static Maybe<bool> TelemetryPrefValue() {
  // Leave it unchanged if it's already set.
  // XXX: how could it already be set?
  if (Preferences::GetType(kTelemetryPref) != nsIPrefBranch::PREF_INVALID) {
    return Nothing();
  }

  // Determine the correct default for toolkit.telemetry.enabled. If this
  // build has MOZ_TELEMETRY_ON_BY_DEFAULT *or* we're on the beta channel,
  // telemetry is on by default, otherwise not. This is necessary so that
  // beta users who are testing final release builds don't flipflop defaults.
#  ifdef MOZ_TELEMETRY_ON_BY_DEFAULT
  return Some(true);
#  else
  nsAutoCString channelPrefValue;
  (void)Preferences::GetCString(kChannelPref, channelPrefValue,
                                PrefValueKind::Default);
  return Some(channelPrefValue.EqualsLiteral("beta"));
#  endif
}

/* static */
void Preferences::SetupTelemetryPref() {
  MOZ_ASSERT(XRE_IsParentProcess());

  Maybe<bool> telemetryPrefValue = TelemetryPrefValue();
  if (telemetryPrefValue.isSome()) {
    Preferences::SetBool(kTelemetryPref, *telemetryPrefValue,
                         PrefValueKind::Default);
  }
}

#else  // !MOZ_WIDGET_ANDROID

static bool TelemetryPrefValue() {
  // For platforms with Unified Telemetry (here meaning not-Android),
  // toolkit.telemetry.enabled determines whether we send "extended" data.
  // We only want extended data from pre-release channels due to size.

  constexpr auto channel = MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL) ""_ns;

  // Easy cases: Nightly, Aurora, Beta.
  if (channel.EqualsLiteral("nightly") || channel.EqualsLiteral("aurora") ||
      channel.EqualsLiteral("beta")) {
    return true;
  }

#  ifndef MOZILLA_OFFICIAL
  // Local developer builds: non-official builds on the "default" channel.
  if (channel.EqualsLiteral("default")) {
    return true;
  }
#  endif

  // Release Candidate builds: builds that think they are release builds, but
  // are shipped to beta users.
  if (channel.EqualsLiteral("release")) {
    nsAutoCString channelPrefValue;
    (void)Preferences::GetCString(kChannelPref, channelPrefValue,
                                  PrefValueKind::Default);
    if (channelPrefValue.EqualsLiteral("beta")) {
      return true;
    }
  }

  return false;
}

/* static */
void Preferences::SetupTelemetryPref() {
  MOZ_ASSERT(XRE_IsParentProcess());

  Preferences::SetBool(kTelemetryPref, TelemetryPrefValue(),
                       PrefValueKind::Default);
  Preferences::Lock(kTelemetryPref);
}

#endif  // MOZ_WIDGET_ANDROID

/* static */
already_AddRefed<Preferences> Preferences::GetInstanceForService() {
  if (sPreferences) {
    return do_AddRef(sPreferences);
  }

  if (sShutdown) {
    return nullptr;
  }

  sPreferences = new Preferences();

  MOZ_ASSERT(!HashTable());
  HashTable() = new PrefsHashTable(XRE_IsParentProcess()
                                       ? kHashTableInitialLengthParent
                                       : kHashTableInitialLengthContent);

#ifdef DEBUG
  gOnceStaticPrefsAntiFootgun = new AntiFootgunMap();
#endif

#ifdef ACCESS_COUNTS
  MOZ_ASSERT(!gAccessCounts);
  gAccessCounts = new AccessCountsHashTable();
#endif

  nsresult rv = InitInitialObjects(/* isStartup */ true);
  if (NS_FAILED(rv)) {
    sPreferences = nullptr;
    return nullptr;
  }

  if (!XRE_IsParentProcess()) {
    MOZ_ASSERT(gChangedDomPrefs);
    for (unsigned int i = 0; i < gChangedDomPrefs->Length(); i++) {
      Preferences::SetPreference(gChangedDomPrefs->ElementAt(i));
    }
    gChangedDomPrefs = nullptr;
  } else {
    // Check if there is a deployment configuration file. If so, set up the
    // pref config machinery, which will actually read the file.
    nsAutoCString lockFileName;
    nsresult rv = Preferences::GetCString("general.config.filename",
                                          lockFileName, PrefValueKind::User);
    if (NS_SUCCEEDED(rv)) {
      NS_CreateServicesFromCategory(
          "pref-config-startup",
          static_cast<nsISupports*>(static_cast<void*>(sPreferences)),
          "pref-config-startup");
    }

    nsCOMPtr<nsIObserverService> observerService =
        services::GetObserverService();
    if (!observerService) {
      sPreferences = nullptr;
      return nullptr;
    }

    observerService->AddObserver(sPreferences,
                                 "profile-before-change-telemetry", true);
    rv = observerService->AddObserver(sPreferences, "profile-before-change",
                                      true);

    observerService->AddObserver(sPreferences, "suspend_process_notification",
                                 true);

    if (NS_FAILED(rv)) {
      sPreferences = nullptr;
      return nullptr;
    }
  }

  const char* defaultPrefs = getenv("MOZ_DEFAULT_PREFS");
  if (defaultPrefs) {
    parsePrefData(nsCString(defaultPrefs), PrefValueKind::Default);
  }

  // Preferences::GetInstanceForService() can be called from GetService(), and
  // RegisterStrongMemoryReporter calls GetService(nsIMemoryReporter).  To
  // avoid a potential recursive GetService() call, we can't register the
  // memory reporter here; instead, do it off a runnable.
  RefPtr<AddPreferencesMemoryReporterRunnable> runnable =
      new AddPreferencesMemoryReporterRunnable();
  NS_DispatchToMainThread(runnable);

  return do_AddRef(sPreferences);
}

/* static */
bool Preferences::IsServiceAvailable() {
  MOZ_ASSERT(NS_IsMainThread());
  return !!sPreferences;
}

/* static */
bool Preferences::InitStaticMembers() {
  MOZ_ASSERT(NS_IsMainThread() || ServoStyleSet::IsInServoTraversal());

  if (MOZ_LIKELY(sPreferences)) {
    return true;
  }

  if (!sShutdown) {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIPrefService> prefService =
        do_GetService(NS_PREFSERVICE_CONTRACTID);
  }

  return sPreferences != nullptr;
}

/* static */
void Preferences::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!sShutdown) {
    sShutdown = true;  // Don't create the singleton instance after here.
    if (sSweepRunner) {
      sSweepRunner->Cancel();
      sSweepRunner = nullptr;
    }
    sPreferences = nullptr;
    StaticPrefs::ShutdownAlwaysPrefs();
  }
}

Preferences::Preferences()
    : mRootBranch(new nsPrefBranch("", PrefValueKind::User)),
      mDefaultRootBranch(new nsPrefBranch("", PrefValueKind::Default)) {}

Preferences::~Preferences() {
  MOZ_ASSERT(!sPreferences);

  MOZ_ASSERT(!gCallbacksInProgress);

  CallbackNode* node = gFirstCallback;
  while (node) {
    CallbackNode* next_node = node->Next();
    delete node;
    node = next_node;
  }
  gLastPriorityNode = gFirstCallback = nullptr;

  delete HashTable();
  HashTable() = nullptr;

#ifdef DEBUG
  gOnceStaticPrefsAntiFootgun = nullptr;
#endif

#ifdef ACCESS_COUNTS
  gAccessCounts = nullptr;
#endif

  gSharedMap = nullptr;

  PrefNameArena().Clear();
}

NS_IMPL_ISUPPORTS(Preferences, nsIPrefService, nsIObserver, nsIPrefBranch,
                  nsISupportsWeakReference)

/* static */
void Preferences::SerializePreferences(nsCString& aStr,
                                       bool aIsDestinationWebContentProcess) {
  MOZ_RELEASE_ASSERT(InitStaticMembers());

  aStr.Truncate();

  for (auto iter = HashTable()->iter(); !iter.done(); iter.next()) {
    Pref* pref = iter.get().get();
    if (!pref->IsTypeNone() && pref->HasAdvisablySizedValues()) {
      pref->SerializeAndAppend(aStr, aIsDestinationWebContentProcess &&
                                         ShouldSanitizePreference(pref));
    }
  }

  aStr.Append('\0');
}

/* static */
void Preferences::DeserializePreferences(const char* aStr, size_t aPrefsLen) {
  MOZ_ASSERT(!XRE_IsParentProcess());

  MOZ_ASSERT(!gChangedDomPrefs);
  gChangedDomPrefs = new nsTArray<dom::Pref>();

  const char* p = aStr;
  while (*p != '\0') {
    dom::Pref pref;
    p = Pref::Deserialize(p, &pref);
    gChangedDomPrefs->AppendElement(pref);
  }

  // We finished parsing on a '\0'. That should be the last char in the shared
  // memory. (aPrefsLen includes the '\0'.)
  MOZ_ASSERT(p == aStr + aPrefsLen - 1);

  MOZ_ASSERT(!gContentProcessPrefsAreInited);
  gContentProcessPrefsAreInited = true;
}

/* static */
mozilla::ipc::ReadOnlySharedMemoryHandle Preferences::EnsureSnapshot() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (!gSharedMap) {
    SharedPrefMapBuilder builder;

    nsTArray<Pref*> toRepopulate;
    NameArena* newPrefNameArena = new NameArena();
    for (auto iter = HashTable()->modIter(); !iter.done(); iter.next()) {
      if (!ShouldSanitizePreference(iter.get().get())) {
        iter.get()->AddToMap(builder);
      } else {
        Pref* pref = iter.getMutable().release();
        pref->RelocateName(newPrefNameArena);
        toRepopulate.AppendElement(pref);
      }
    }

    // Store the current value of `once`-mirrored prefs. After this point they
    // will be immutable.
    StaticPrefs::RegisterOncePrefs(builder);

    gSharedMap = new SharedPrefMap(std::move(builder));

    // Once we've built a snapshot of the database, there's no need to continue
    // storing dynamic copies of the preferences it contains. Once we reset the
    // hashtable, preference lookups will fall back to the snapshot for any
    // preferences not in the dynamic hashtable.
    //
    // And since the majority of the database is now contained in the snapshot,
    // we can initialize the hashtable with the expected number of per-session
    // changed preferences, rather than the expected total number of
    // preferences.
    HashTable()->clearAndCompact();
    (void)HashTable()->reserve(kHashTableInitialLengthContent);

    delete sPrefNameArena;
    sPrefNameArena = newPrefNameArena;
    gCallbackPref = nullptr;

    for (uint32_t i = 0; i < toRepopulate.Length(); i++) {
      auto pref = toRepopulate[i];
      auto p = HashTable()->lookupForAdd(pref->Name());
      MOZ_ASSERT(!p.found());
      (void)HashTable()->add(p, pref);
    }
  }

  return gSharedMap->CloneHandle();
}

/* static */
void Preferences::InitSnapshot(
    const mozilla::ipc::ReadOnlySharedMemoryHandle& aHandle) {
  MOZ_ASSERT(!XRE_IsParentProcess());
  MOZ_ASSERT(!gSharedMap);

  gSharedMap = new SharedPrefMap(aHandle);

  StaticPrefs::InitStaticPrefsFromShared();
}

/* static */
void Preferences::InitializeUserPrefs() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(!sPreferences->mCurrentFile, "Should only initialize prefs once");

  // Prefs which are set before we initialize the profile are silently
  // discarded. This is stupid, but there are various tests which depend on
  // this behavior.
  sPreferences->ResetUserPrefs();

  nsCOMPtr<nsIFile> prefsFile = sPreferences->ReadSavedPrefs();
  sPreferences->ReadUserOverridePrefs();

  sPreferences->mDirty = false;

  // Don't set mCurrentFile until we're done so that dirty flags work properly.
  sPreferences->mCurrentFile = std::move(prefsFile);
}

/* static */
void Preferences::FinishInitializingUserPrefs() {
  MOZ_ASSERT(NS_IsMainThread());
  sPreferences->NotifyServiceObservers(NS_PREFSERVICE_READ_TOPIC_ID);
}

NS_IMETHODIMP
Preferences::Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* someData) {
  if (MOZ_UNLIKELY(!XRE_IsParentProcess())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = NS_OK;

  if (!nsCRT::strcmp(aTopic, "profile-before-change")) {
    // Normally prefs aren't written after this point, and so we kick off
    // an asynchronous pref save so that I/O can be done in parallel with
    // other shutdown.
    if (AllowOffMainThreadSave()) {
      SavePrefFile(nullptr);
    }

  } else if (!nsCRT::strcmp(aTopic, "profile-before-change-telemetry")) {
    // It's possible that a profile-before-change observer after ours
    // set a pref. A blocking save here re-saves if necessary and also waits
    // for any pending saves to complete.
    SavePrefFileBlocking();
    MOZ_ASSERT(!mDirty, "Preferences should not be dirty");
    mProfileShutdown = true;

  } else if (!nsCRT::strcmp(aTopic, "suspend_process_notification")) {
    // Our process is being suspended. The OS may wake our process later,
    // or it may kill the process. In case our process is going to be killed
    // from the suspended state, we save preferences before suspending.
    rv = SavePrefFileBlocking();
  }

  return rv;
}

NS_IMETHODIMP
Preferences::ReadDefaultPrefsFromFile(nsIFile* aFile) {
  ENSURE_PARENT_PROCESS("Preferences::ReadDefaultPrefsFromFile", "all prefs");

  if (!aFile) {
    NS_ERROR("ReadDefaultPrefsFromFile requires a parameter");
    return NS_ERROR_INVALID_ARG;
  }

  return openPrefFile(aFile, PrefValueKind::Default);
}

NS_IMETHODIMP
Preferences::ReadUserPrefsFromFile(nsIFile* aFile) {
  ENSURE_PARENT_PROCESS("Preferences::ReadUserPrefsFromFile", "all prefs");

  if (!aFile) {
    NS_ERROR("ReadUserPrefsFromFile requires a parameter");
    return NS_ERROR_INVALID_ARG;
  }

  return openPrefFile(aFile, PrefValueKind::User);
}

NS_IMETHODIMP
Preferences::ResetPrefs() {
  ENSURE_PARENT_PROCESS("Preferences::ResetPrefs", "all prefs");

  if (gSharedMap) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  HashTable()->clearAndCompact();
  (void)HashTable()->reserve(kHashTableInitialLengthParent);

  PrefNameArena().Clear();

  return InitInitialObjects(/* isStartup */ false);
}

nsresult Preferences::ResetUserPrefs() {
  ENSURE_PARENT_PROCESS("Preferences::ResetUserPrefs", "all prefs");
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);
  MOZ_ASSERT(NS_IsMainThread());

  Vector<const char*> prefNames;
  for (auto iter = HashTable()->modIter(); !iter.done(); iter.next()) {
    Pref* pref = iter.get().get();

    if (pref->HasUserValue()) {
      if (!prefNames.append(pref->Name())) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      pref->ClearUserValue();
      if (!pref->HasDefaultValue()) {
        iter.remove();
      }
    }
  }

  for (const char* prefName : prefNames) {
    NotifyCallbacks(nsDependentCString{prefName});
  }

  Preferences::HandleDirty();
  return NS_OK;
}

bool Preferences::AllowOffMainThreadSave() {
  // Put in a preference that allows us to disable off main thread preference
  // file save.
  if (sAllowOMTPrefWrite < 0) {
    bool value = false;
    Preferences::GetBool("preferences.allow.omt-write", &value);
    sAllowOMTPrefWrite = value ? 1 : 0;
  }

  return !!sAllowOMTPrefWrite;
}

nsresult Preferences::SavePrefFileBlocking() {
  if (mDirty) {
    return SavePrefFileInternal(nullptr, SaveMethod::Blocking);
  }

  // If we weren't dirty to start, SavePrefFileInternal will early exit so
  // there is no guarantee that we don't have oustanding async saves in the
  // pipe. Since the contract of SavePrefFileOnMainThread is that the file on
  // disk matches the preferences, we have to make sure those requests are
  // completed.

  if (AllowOffMainThreadSave()) {
    PreferencesWriter::Flush();
  }

  return NS_OK;
}

nsresult Preferences::SavePrefFileAsynchronous() {
  return SavePrefFileInternal(nullptr, SaveMethod::Asynchronous);
}

NS_IMETHODIMP
Preferences::SavePrefFile(nsIFile* aFile) {
  // This is the method accessible from service API. Make it off main thread.
  return SavePrefFileInternal(aFile, SaveMethod::Asynchronous);
}

NS_IMETHODIMP
Preferences::BackupPrefFile(nsIFile* aFile, nsIPrefOverrideMap* aJSOverrideMap,
                            JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aFile) {
    return NS_ERROR_INVALID_ARG;
  }

  if (mCurrentFile) {
    bool equalsCurrent = false;
    nsresult rv = aFile->Equals(mCurrentFile, &equalsCurrent);

    if (NS_FAILED(rv)) {
      return rv;
    }

    if (equalsCurrent) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);

  if (MOZ_UNLIKELY(result.Failed())) {
    return result.StealNSResult();
  }

  nsMainThreadPtrHandle<Promise> domPromiseHolder(
      new nsMainThreadPtrHolder<Promise>("Preferences::BackupPrefFile promise",
                                         promise));

  auto mozPromiseHolder = MakeUnique<MozPromiseHolder<WritePrefFilePromise>>();
  RefPtr<WritePrefFilePromise> writePrefPromise =
      mozPromiseHolder->Ensure(__func__);

  // Write prefs, minus ExperimentAPI prefs and overrides.
  nsresult rv = WritePrefFile(aFile, SaveMethod::Asynchronous,
                              std::move(mozPromiseHolder), aJSOverrideMap);
  if (NS_FAILED(rv)) {
    // WritePrefFile is responsible for rejecting the underlying MozPromise in
    // the event that it the method failed somewhere.
    return rv;
  }

  writePrefPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [domPromiseHolder](bool) {
        MOZ_ASSERT(NS_IsMainThread());
        domPromiseHolder.get()->MaybeResolveWithUndefined();
      },
      [domPromiseHolder](nsresult rv) {
        MOZ_ASSERT(NS_IsMainThread());
        domPromiseHolder.get()->MaybeReject(rv);
      });

  promise.forget(aPromise);
  return NS_OK;
}

/* static */
void Preferences::SetPreference(const dom::Pref& aDomPref) {
  MOZ_ASSERT(!XRE_IsParentProcess());
  NS_ENSURE_TRUE(InitStaticMembers(), (void)0);

  const nsCString& prefName = aDomPref.name();

  Pref* pref;
  auto p = HashTable()->lookupForAdd(prefName.get());
  if (!p) {
    pref = new Pref(prefName);
    if (!HashTable()->add(p, pref)) {
      delete pref;
      return;
    }
  } else {
    pref = p->get();
  }

  bool valueChanged = false;
  pref->FromDomPref(aDomPref, &valueChanged);

  // When the parent process clears a pref's user value or deletes a pref
  // we get a DomPref here with no default value and no user value.
  // There are two possibilities.
  //
  // - There was an existing pref with only a user value. FromDomPref() will
  //   have just cleared that user value, so the pref can be removed.
  //
  // - There was no existing pref. FromDomPref() will have done nothing, and
  //   `pref` will be valueless. We will end up adding and removing the value
  //   needlessly, but that's ok because this case is rare.
  //
  if (!pref->HasDefaultValue() && !pref->HasUserValue() &&
      !pref->IsSanitized()) {
    // If the preference exists in the shared map, we need to keep the dynamic
    // entry around to mask it.
    if (gSharedMap->Has(pref->Name())) {
      pref->SetType(PrefType::None);
    } else {
      HashTable()->remove(prefName.get());
    }
    pref = nullptr;
  }

  // Note: we don't have to worry about HandleDirty() because we are setting
  // prefs in the content process that have come from the parent process.

  if (valueChanged) {
    if (pref) {
      NotifyCallbacks(prefName, PrefWrapper(pref));
    } else {
      NotifyCallbacks(prefName);
    }
  }
}

/* static */
void Preferences::GetPreference(dom::Pref* aDomPref,
                                const GeckoProcessType aDestinationProcessType,
                                const nsACString& aDestinationRemoteType) {
  MOZ_ASSERT(XRE_IsParentProcess());
  bool destIsWebContent =
      aDestinationProcessType == GeckoProcessType_Content &&
      (StringBeginsWith(aDestinationRemoteType, WEB_REMOTE_TYPE) ||
       StringBeginsWith(aDestinationRemoteType, PREALLOC_REMOTE_TYPE) ||
       StringBeginsWith(aDestinationRemoteType, PRIVILEGEDMOZILLA_REMOTE_TYPE));

  Pref* pref = pref_HashTableLookup(aDomPref->name().get());
  if (pref && pref->HasAdvisablySizedValues()) {
    pref->ToDomPref(aDomPref, destIsWebContent);
  }
}

#ifdef DEBUG
bool Preferences::ArePrefsInitedInContentProcess() {
  MOZ_ASSERT(!XRE_IsParentProcess());
  return gContentProcessPrefsAreInited;
}
#endif

NS_IMETHODIMP
Preferences::GetBranch(const char* aPrefRoot, nsIPrefBranch** aRetVal) {
  if ((nullptr != aPrefRoot) && (*aPrefRoot != '\0')) {
    // TODO: Cache this stuff and allow consumers to share branches (hold weak
    // references, I think).
    RefPtr<nsPrefBranch> prefBranch =
        new nsPrefBranch(aPrefRoot, PrefValueKind::User);
    prefBranch.forget(aRetVal);
  } else {
    // Special case: caching the default root.
    nsCOMPtr<nsIPrefBranch> root(sPreferences->mRootBranch);
    root.forget(aRetVal);
  }

  return NS_OK;
}

NS_IMETHODIMP
Preferences::GetDefaultBranch(const char* aPrefRoot, nsIPrefBranch** aRetVal) {
  if (!aPrefRoot || !aPrefRoot[0]) {
    nsCOMPtr<nsIPrefBranch> root(sPreferences->mDefaultRootBranch);
    root.forget(aRetVal);
    return NS_OK;
  }

  // TODO: Cache this stuff and allow consumers to share branches (hold weak
  // references, I think).
  RefPtr<nsPrefBranch> prefBranch =
      new nsPrefBranch(aPrefRoot, PrefValueKind::Default);
  if (!prefBranch) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  prefBranch.forget(aRetVal);
  return NS_OK;
}

NS_IMETHODIMP
Preferences::ReadStats(nsIPrefStatsCallback* aCallback) {
#ifdef ACCESS_COUNTS
  for (const auto& entry : *gAccessCounts) {
    aCallback->Visit(entry.GetKey(), entry.GetData());
  }

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
Preferences::ResetStats() {
#ifdef ACCESS_COUNTS
  gAccessCounts->Clear();
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

// We would much prefer to use C++ lambdas, but we cannot convert
// lambdas that capture (here, the underlying observer) to C pointer
// to functions.  So, here we are, with icky C callbacks.  Be aware
// that nothing is thread-safe here because there's a single global
// `nsIPrefObserver` instance.  Use this from the main thread only.
nsIPrefObserver* PrefObserver = nullptr;

void HandlePref(const char* aPrefName, PrefType aType, PrefValueKind aKind,
                PrefValue aValue, bool aIsSticky, bool aIsLocked) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!PrefObserver) {
    return;
  }

  const char* kind = aKind == PrefValueKind::Default ? "Default" : "User";

  switch (aType) {
    case PrefType::String:
      PrefObserver->OnStringPref(kind, aPrefName, aValue.mStringVal, aIsSticky,
                                 aIsLocked);
      break;
    case PrefType::Int:
      PrefObserver->OnIntPref(kind, aPrefName, aValue.mIntVal, aIsSticky,
                              aIsLocked);
      break;
    case PrefType::Bool:
      PrefObserver->OnBoolPref(kind, aPrefName, aValue.mBoolVal, aIsSticky,
                               aIsLocked);
      break;
    default:
      PrefObserver->OnError("Unexpected pref type.");
  }
}

void HandleError(const char* aFullMsg, uint64_t aStaticMsgOffset) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!PrefObserver) {
    return;
  }

  // No need to use the static message here
  PrefObserver->OnError(aFullMsg);
}

NS_IMETHODIMP
Preferences::ParsePrefsFromBuffer(const nsTArray<uint8_t>& aBytes,
                                  nsIPrefObserver* aObserver,
                                  const char* aPathLabel) {
  MOZ_ASSERT(NS_IsMainThread());

  // We need a null-terminated buffer.
  nsTArray<uint8_t> data = aBytes.Clone();
  data.AppendElement(0);

  // Parsing as default handles both `pref` and `user_pref`.
  PrefObserver = aObserver;
  prefs_parser_parse(aPathLabel ? aPathLabel : "<ParsePrefsFromBuffer data>",
                     PrefValueKind::Default, (const char*)data.Elements(),
                     data.Length() - 1, HandlePref, HandleError);
  PrefObserver = nullptr;

  return NS_OK;
}

NS_IMETHODIMP
Preferences::GetUserPrefsFileLastModifiedAtStartup(PRTime* aLastModified) {
  *aLastModified = mUserPrefsFileLastModifiedAtStartup;
  return NS_OK;
}

NS_IMETHODIMP
Preferences::GetDirty(bool* aRetVal) {
  *aRetVal = mDirty;
  return NS_OK;
}

NS_IMETHODIMP
Preferences::GetPrefsJsPreamble(nsACString& aPreamble) {
  ::GetPrefsJsPreamble(aPreamble);
  return NS_OK;
}

nsresult Preferences::NotifyServiceObservers(const char* aTopic) {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (!observerService) {
    return NS_ERROR_FAILURE;
  }

  auto subject = static_cast<nsIPrefService*>(this);
  observerService->NotifyObservers(subject, aTopic, nullptr);

  return NS_OK;
}

already_AddRefed<nsIFile> Preferences::ReadSavedPrefs() {
  nsCOMPtr<nsIFile> file;
  nsresult rv =
      NS_GetSpecialDirectory(NS_APP_PREFS_50_FILE, getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  rv = openPrefFile(file, PrefValueKind::User);
  if (rv == NS_ERROR_FILE_NOT_FOUND) {
    // This is a normal case for new users.
    rv = NS_OK;
  } else {
    // Store the last modified time of the file while we've got it.
    // We don't really care if this fails.
    (void)file->GetLastModifiedTime(&mUserPrefsFileLastModifiedAtStartup);

    if (NS_FAILED(rv)) {
      // Save a backup copy of the current (invalid) prefs file, since all prefs
      // from the error line to the end of the file will be lost (bug 361102).
      // TODO we should notify the user about it (bug 523725).
      glean::preferences::prefs_file_was_invalid.Set(true);
      MakeBackupPrefFile(file);
    }
  }

  return file.forget();
}

void Preferences::ReadUserOverridePrefs() {
  nsCOMPtr<nsIFile> aFile;
  nsresult rv =
      NS_GetSpecialDirectory(NS_APP_PREFS_50_DIR, getter_AddRefs(aFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  aFile->AppendNative("user.js"_ns);
  rv = openPrefFile(aFile, PrefValueKind::User);
}

nsresult Preferences::MakeBackupPrefFile(nsIFile* aFile) {
  // Example: this copies "prefs.js" to "Invalidprefs.js" in the same directory.
  // "Invalidprefs.js" is removed if it exists, prior to making the copy.
  nsAutoString newFilename;
  nsresult rv = aFile->GetLeafName(newFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  newFilename.InsertLiteral(u"Invalid", 0);
  nsCOMPtr<nsIFile> newFile;
  rv = aFile->GetParent(getter_AddRefs(newFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = newFile->Append(newFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists = false;
  newFile->Exists(&exists);
  if (exists) {
    rv = newFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = aFile->CopyTo(nullptr, newFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  return rv;
}

nsresult Preferences::SavePrefFileInternal(nsIFile* aFile,
                                           SaveMethod aSaveMethod) {
  ENSURE_PARENT_PROCESS("Preferences::SavePrefFileInternal", "all prefs");

  // We allow different behavior here when aFile argument is not null, but it
  // happens to be the same as the current file.  It is not clear that we
  // should, but it does give us a "force" save on the unmodified pref file
  // (see the original bug 160377 when we added this.)

  if (nullptr == aFile) {
    mSavePending = false;

    // Off main thread writing only if allowed.
    if (!AllowOffMainThreadSave()) {
      aSaveMethod = SaveMethod::Blocking;
    }

    // The mDirty flag tells us if we should write to mCurrentFile. We only
    // check this flag when the caller wants to write to the default.
    if (!mDirty) {
      return NS_OK;
    }

    // Check for profile shutdown after mDirty because the runnables from
    // HandleDirty() can still be pending.
    if (mProfileShutdown) {
      NS_WARNING("Cannot save pref file after profile shutdown.");
      return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
    }

    // It's possible that we never got a prefs file.
    nsresult rv = NS_OK;
    if (mCurrentFile) {
      rv = WritePrefFile(mCurrentFile, aSaveMethod);
    }

    // If we succeeded writing to mCurrentFile, reset the dirty flag.
    if (NS_SUCCEEDED(rv)) {
      mDirty = false;
    }
    return rv;

  } else {
    // We only allow off main thread writes on mCurrentFile using this method.
    // If you want to write asynchronously, use BackupPrefFile instead.
    return WritePrefFile(aFile, SaveMethod::Blocking);
  }
}

nsresult Preferences::WritePrefFile(
    nsIFile* aFile, SaveMethod aSaveMethod,
    UniquePtr<MozPromiseHolder<WritePrefFilePromise>>
        aPromiseHolder /* = nullptr */,
    const nsIPrefOverrideMap* aPrefOverrideMap /* = nullptr */) {
  MOZ_ASSERT(XRE_IsParentProcess());

#define REJECT_IF_PROMISE_HOLDER_EXISTS(rv)       \
  if (aPromiseHolder) {                           \
    aPromiseHolder->RejectIfExists(rv, __func__); \
  }                                               \
  return rv;

  if (!HashTable()) {
    REJECT_IF_PROMISE_HOLDER_EXISTS(NS_ERROR_NOT_INITIALIZED);
  }

  AUTO_PROFILER_LABEL("Preferences::WritePrefFile", OTHER);

  if (AllowOffMainThreadSave()) {
    UniquePtr<PrefSaveData> prefs =
        MakeUnique<PrefSaveData>(pref_savePrefs(aPrefOverrideMap));

    nsresult rv = NS_OK;
    bool writingToCurrent = false;

    // The TaskQueue mAsyncTarget ensures that we'll only process one write at
    // a time, so we won't run into a deadlock if a previous write blocks.
    if (!mAsyncTarget) {
      rv = NS_CreateBackgroundTaskQueue("WritePrefFile",
                                        getter_AddRefs(mAsyncTarget));
      if (NS_FAILED(rv)) {
        REJECT_IF_PROMISE_HOLDER_EXISTS(rv);
      }
    }

    if (mCurrentFile) {
      rv = mCurrentFile->Equals(aFile, &writingToCurrent);
      if (NS_FAILED(rv)) {
        REJECT_IF_PROMISE_HOLDER_EXISTS(rv);
      }
    }

    // Put the newly constructed preference data into sPendingWriteData
    // for the next request to pick up
    prefs.reset(PreferencesWriter::sPendingWriteData.exchange(prefs.release()));
    if (prefs && !writingToCurrent) {
      MOZ_ASSERT(!aPromiseHolder,
                 "Shouldn't be able to enter here if aPromiseHolder is set");
      // There was a previous request writing to the default location that
      // hasn't been processed. It will do the work of eventually writing this
      // latest batch of data to disk.
      return NS_OK;
    }

    // There were no previous requests. Dispatch one since sPendingWriteData has
    // the up to date information.
    bool async = aSaveMethod == SaveMethod::Asynchronous;

    // Increment sPendingWriteCount, even though it's redundant to track this
    // in the case of a sync runnable; it just makes it easier to simply
    // decrement this inside PWRunnable. We cannot use the constructor /
    // destructor for increment/decrement, as on dispatch failure we might
    // leak the runnable in order to not destroy it on the wrong thread, which
    // would make us get stuck in an infinite SpinEventLoopUntil inside
    // PreferencesWriter::Flush. Better that in future code we miss an
    // increment of sPendingWriteCount and cause a simple crash due to it
    // ending up negative.
    PreferencesWriter::sPendingWriteCount++;

    // If aPromiseHolder is not null, ownership is transferred to PWRunnable.
    // The PWRunnable will automatically reject the MozPromise if it is
    // destroyed before being resolved or rejected by the Run method.
    if (async) {
      rv = mAsyncTarget->Dispatch(
          new PWRunnable(aFile, std::move(aPromiseHolder)),
          nsIEventTarget::DISPATCH_EVENT_MAY_BLOCK);
    } else {
      rv = SyncRunnable::DispatchToThread(
          mAsyncTarget, new PWRunnable(aFile, std::move(aPromiseHolder)), true);
    }
    if (NS_FAILED(rv)) {
      // If our dispatch failed, we should correct our bookkeeping to
      // avoid shutdown hangs.
      PreferencesWriter::sPendingWriteCount--;
      // No need to reject the aPromiseHolder here, as the PWRunnable will
      // have already done so.
      return rv;
    }
    return NS_OK;
  }

  // This will do a main thread write. It is safe to do it this way because
  // AllowOffMainThreadSave() returns a consistent value for the lifetime of
  // the parent process.
  PrefSaveData prefsData = pref_savePrefs(aPrefOverrideMap);

  // If we were given a MozPromiseHolder, this means the caller is attempting
  // to write prefs asynchronously to the disk - but if we get here, it means
  // that AllowOffMainThreadSave() return false, and that we will be forced
  // to write on the main thread instead. We still have to resolve or reject
  // that MozPromise regardless.
  nsresult rv = PreferencesWriter::Write(aFile, prefsData);
  if (aPromiseHolder) {
    NS_WARNING(
        "Cannot write to prefs asynchronously, as AllowOffMainThreadSave() "
        "returned false.");
    if (NS_SUCCEEDED(rv)) {
      aPromiseHolder->ResolveIfExists(true, __func__);
    } else {
      aPromiseHolder->RejectIfExists(rv, __func__);
    }
  }
  return rv;

#undef REJECT_IF_PROMISE_HOLDER_EXISTS
}

static nsresult openPrefFile(nsIFile* aFile, PrefValueKind aKind) {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCString data = MOZ_TRY(URLPreloader::ReadFile(aFile));

  nsAutoString path;
  aFile->GetPath(path);

  return parsePrefFileData(aKind, NS_ConvertUTF16toUTF8(path).get(), data,
                           Parser::HandlePref, Parser::HandleError);
}

static nsresult parsePrefData(const nsCString& aData, PrefValueKind aKind) {
  const nsCString path = "$MOZ_DEFAULT_PREFS"_ns;

  Parser parser;
  if (!parser.Parse(aKind, path.get(), aData)) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  return NS_OK;
}

static int pref_CompareFileNames(nsIFile* aFile1, nsIFile* aFile2) {
  nsAutoCString filename1, filename2;
  aFile1->GetNativeLeafName(filename1);
  aFile2->GetNativeLeafName(filename2);

  return Compare(filename2, filename1);
}

// Load default pref files from a directory. The files in the directory are
// sorted reverse-alphabetically.
static nsresult pref_LoadPrefsInDir(nsIFile* aDir) {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsresult rv, rv2;

  nsCOMPtr<nsIDirectoryEnumerator> dirIterator;

  // This may fail in some normal cases, such as embedders who do not use a
  // GRE.
  rv = aDir->GetDirectoryEntries(getter_AddRefs(dirIterator));
  if (NS_FAILED(rv)) {
    // If the directory doesn't exist, then we have no reason to complain. We
    // loaded everything (and nothing) successfully.
    if (rv == NS_ERROR_FILE_NOT_FOUND) {
      rv = NS_OK;
    }
    return rv;
  }

  nsCOMArray<nsIFile> prefFiles(INITIAL_PREF_FILES);
  nsCOMPtr<nsIFile> prefFile;

  while (NS_SUCCEEDED(dirIterator->GetNextFile(getter_AddRefs(prefFile))) &&
         prefFile) {
    nsAutoCString leafName;
    prefFile->GetNativeLeafName(leafName);
    MOZ_ASSERT(
        !leafName.IsEmpty(),
        "Failure in default prefs: directory enumerator returned empty file?");

    // Skip non-js files.
    if (StringEndsWith(leafName, ".js"_ns,
                       nsCaseInsensitiveCStringComparator)) {
      prefFiles.AppendObject(prefFile);
    }
  }

  if (prefFiles.Count() == 0) {
    NS_WARNING("No default pref files found.");
    if (NS_SUCCEEDED(rv)) {
      rv = NS_SUCCESS_FILE_DIRECTORY_EMPTY;
    }
    return rv;
  }

  prefFiles.Sort(pref_CompareFileNames);

  uint32_t arrayCount = prefFiles.Count();
  uint32_t i;
  for (i = 0; i < arrayCount; ++i) {
    rv2 = openPrefFile(prefFiles[i], PrefValueKind::Default);
    if (NS_FAILED(rv2)) {
      NS_ERROR("Default pref file not parsed successfully.");
      rv = rv2;
    }
  }

  return rv;
}

static nsresult pref_ReadPrefFromJar(nsZipArchive* aJarReader,
                                     const char* aName) {
  nsCString manifest =
      MOZ_TRY(URLPreloader::ReadZip(aJarReader, nsDependentCString(aName)));

  Parser parser;
  if (!parser.Parse(PrefValueKind::Default, aName, manifest)) {
    return NS_ERROR_FILE_CORRUPTED;
  }

  return NS_OK;
}

static nsresult pref_ReadDefaultPrefs(const RefPtr<nsZipArchive> jarReader,
                                      const char* path) {
  UniquePtr<nsZipFind> find;
  nsTArray<nsCString> prefEntries;
  const char* entryName;
  uint16_t entryNameLen;

  nsresult rv = jarReader->FindInit(path, getter_Transfers(find));
  NS_ENSURE_SUCCESS(rv, rv);

  while (NS_SUCCEEDED(find->FindNext(&entryName, &entryNameLen))) {
    prefEntries.AppendElement(Substring(entryName, entryNameLen));
  }

  prefEntries.Sort();
  for (uint32_t i = prefEntries.Length(); i--;) {
    rv = pref_ReadPrefFromJar(jarReader, prefEntries[i].get());
    if (NS_FAILED(rv)) {
      NS_WARNING("Error parsing preferences.");
    }
  }

  return NS_OK;
}

static nsCString PrefValueToString(const bool* b) {
  return nsCString(*b ? "true" : "false");
}
static nsCString PrefValueToString(const int* i) {
  return nsPrintfCString("%d", *i);
}
static nsCString PrefValueToString(const uint32_t* u) {
  return nsPrintfCString("%d", *u);
}
static nsCString PrefValueToString(const float* f) {
  return nsPrintfCString("%f", *f);
}
static nsCString PrefValueToString(const nsACString* s) {
  return nsCString(*s);
}
static nsCString PrefValueToString(const nsACString& s) { return nsCString(s); }

// These preference getter wrappers allow us to look up the value for static
// preferences based on their native types, rather than manually mapping them to
// the appropriate Preferences::Get* functions.
// We define these methods in a struct which is made friend of Preferences in
// order to access private members.
struct Internals {
  template <typename T>
  static nsresult GetPrefValue(const char* aPrefName, T&& aResult,
                               PrefValueKind aKind) {
    nsresult rv = NS_ERROR_UNEXPECTED;
    NS_ENSURE_TRUE(Preferences::InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

    if (Maybe<PrefWrapper> pref = pref_Lookup(aPrefName)) {
      rv = pref->GetValue(aKind, aResult);

      if (profiler_thread_is_being_profiled_for_markers()) {
        auto str = PrefValueToString(aResult);
        profiler_add_marker(
            "Preference Read", baseprofiler::category::OTHER_PreferenceRead, {},
            PreferenceMarker{},
            ProfilerString8View::WrapNullTerminatedString(aPrefName),
            Some(aKind), pref->Type(), std::move(str));
      }
    }

    return rv;
  }

  template <typename T>
  static nsresult GetSharedPrefValue(const char* aName, T* aResult) {
    nsresult rv = NS_ERROR_UNEXPECTED;

    if (Maybe<PrefWrapper> pref = pref_SharedLookup(aName)) {
      rv = pref->GetValue(PrefValueKind::User, aResult);

      if (profiler_thread_is_being_profiled_for_markers()) {
        auto str = PrefValueToString(aResult);
        profiler_add_marker(
            "Preference Read", baseprofiler::category::OTHER_PreferenceRead, {},
            PreferenceMarker{},
            ProfilerString8View::WrapNullTerminatedString(aName),
            Nothing() /* indicates Shared */, pref->Type(), std::move(str));
      }
    }

    return rv;
  }

  template <typename T>
  static T GetPref(const char* aPrefName, T aFallback,
                   PrefValueKind aKind = PrefValueKind::User) {
    T result = aFallback;
    GetPrefValue(aPrefName, &result, aKind);
    return result;
  }

  template <typename T, typename V>
  static void MOZ_NEVER_INLINE AssignMirror(T& aMirror, V aValue) {
    aMirror = aValue;
  }

  static void MOZ_NEVER_INLINE AssignMirror(DataMutexString& aMirror,
                                            nsCString&& aValue) {
    auto lock = aMirror.Lock();
    lock->Assign(std::move(aValue));
  }

  static void MOZ_NEVER_INLINE AssignMirror(DataMutexString& aMirror,
                                            const nsLiteralCString& aValue) {
    auto lock = aMirror.Lock();
    lock->Assign(aValue);
  }

  static void ClearMirror(DataMutexString& aMirror) {
    auto lock = aMirror.Lock();
    lock->Assign(nsCString());
  }

  template <typename T>
  static void UpdateMirror(const char* aPref, void* aMirror) {
    StripAtomic<T> value;

    nsresult rv = GetPrefValue(aPref, &value, PrefValueKind::User);
    if (NS_SUCCEEDED(rv)) {
      AssignMirror(*static_cast<T*>(aMirror),
                   std::forward<StripAtomic<T>>(value));
    } else {
      // GetPrefValue() can fail if the update is caused by the pref being
      // deleted or if it fails to make a cast. This assertion is the only place
      // where we safeguard these. In this case the mirror variable will be
      // untouched, thus keeping the value it had prior to the change.
      // (Note that this case won't happen for a deletion via DeleteBranch()
      // unless bug 343600 is fixed, but it will happen for a deletion via
      // ClearUserPref().)
      NS_WARNING(nsPrintfCString("Pref changed failure: %s", aPref).get());
      MOZ_ASSERT(false);
    }
  }

  template <typename T>
  static nsresult RegisterCallback(void* aMirror, const nsACString& aPref) {
    return Preferences::RegisterCallback(UpdateMirror<T>, aPref, aMirror,
                                         Preferences::ExactMatch,
                                         /* isPriority */ true);
  }
};

// Initialize default preference JavaScript buffers from appropriate TEXT
// resources.
/* static */
nsresult Preferences::InitInitialObjects(bool aIsStartup) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!XRE_IsParentProcess()) {
    MOZ_DIAGNOSTIC_ASSERT(gSharedMap);
    if (aIsStartup) {
      StaticPrefs::StartObservingAlwaysPrefs();
    }
    return NS_OK;
  }

  // Initialize static prefs before prefs from data files so that the latter
  // will override the former.
  StaticPrefs::InitAll();

  // In the omni.jar case, we load the following prefs:
  // - jar:$gre/omni.jar!/greprefs.js
  // - jar:$gre/omni.jar!/defaults/pref/*.js
  //
  // In the non-omni.jar case, we load:
  // - $gre/greprefs.js
  //
  // In both cases, we also load:
  // - $gre/defaults/pref/*.js
  //
  // This is kept for bug 591866 (channel-prefs.js should not be in omni.jar)
  // in the `$app == $gre` case; we load all files instead of channel-prefs.js
  // only to have the same behaviour as `$app != $gre`, where this is required
  // as a supported location for GRE preferences.
  //
  // When `$app != $gre`, we additionally load, in the omni.jar case:
  // - jar:$app/omni.jar!/defaults/preferences/*.js
  // - $app/defaults/preferences/*.js
  //
  // and in the non-omni.jar case:
  // - $app/defaults/preferences/*.js
  //
  // When `$app == $gre`, we additionally load, in the omni.jar case:
  // - jar:$gre/omni.jar!/defaults/preferences/*.js
  //
  // Thus, in the omni.jar case, we always load app-specific default
  // preferences from omni.jar, whether or not `$app == $gre`.

  nsresult rv = NS_ERROR_FAILURE;
  UniquePtr<nsZipFind> find;
  nsTArray<nsCString> prefEntries;
  const char* entryName;
  uint16_t entryNameLen;

  RefPtr<nsZipArchive> jarReader = Omnijar::GetReader(Omnijar::GRE);
  if (jarReader) {
#ifdef MOZ_WIDGET_ANDROID
    // Try to load an architecture-specific greprefs.js first. This will be
    // present in FAT AAR builds of GeckoView on Android.
    const char* abi = getenv("MOZ_ANDROID_CPU_ABI");
    if (abi) {
      nsAutoCString path;
      path.AppendPrintf("%s/greprefs.js", abi);
      rv = pref_ReadPrefFromJar(jarReader, path.get());
    }

    if (NS_FAILED(rv)) {
      // Fallback to toplevel greprefs.js if arch-specific load fails.
      rv = pref_ReadPrefFromJar(jarReader, "greprefs.js");
    }
#else
    // Load jar:$gre/omni.jar!/greprefs.js.
    rv = pref_ReadPrefFromJar(jarReader, "greprefs.js");
#endif
    NS_ENSURE_SUCCESS(rv, rv);

    // Load jar:$gre/omni.jar!/defaults/pref/*.js.
    rv = pref_ReadDefaultPrefs(jarReader, "defaults/pref/*.js$");
    NS_ENSURE_SUCCESS(rv, rv);

#ifdef MOZ_BACKGROUNDTASKS
    if (BackgroundTasks::IsBackgroundTaskMode()) {
      rv = pref_ReadDefaultPrefs(jarReader, "defaults/backgroundtasks/*.js$");
      NS_ENSURE_SUCCESS(rv, rv);
    }
#endif

#ifdef MOZ_WIDGET_ANDROID
    // Load jar:$gre/omni.jar!/defaults/pref/$MOZ_ANDROID_CPU_ABI/*.js.
    nsAutoCString path;
    path.AppendPrintf("jar:$gre/omni.jar!/defaults/pref/%s/*.js$", abi);
    pref_ReadDefaultPrefs(jarReader, path.get());
    NS_ENSURE_SUCCESS(rv, rv);
#endif
  } else {
    // Load $gre/greprefs.js.
    nsCOMPtr<nsIFile> greprefsFile;
    rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(greprefsFile));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = greprefsFile->AppendNative("greprefs.js"_ns);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = openPrefFile(greprefsFile, PrefValueKind::Default);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "Error parsing GRE default preferences. Is this an old-style "
          "embedding app?");
    }
  }

  // Load $gre/defaults/pref/*.js.
  nsCOMPtr<nsIFile> defaultPrefDir;
  rv = NS_GetSpecialDirectory(NS_APP_PREF_DEFAULTS_50_DIR,
                              getter_AddRefs(defaultPrefDir));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = pref_LoadPrefsInDir(defaultPrefDir);
  if (NS_FAILED(rv)) {
    NS_WARNING("Error parsing application default preferences.");
  }

#ifdef MOZ_WIDGET_COCOA
  // On macOS, channel-prefs.js is no longer bundled with the application and
  // the "app.update.channel" pref is now read from a Framework instead.
  // Previously, channel-prefs.js was read as one of the files in
  // NS_APP_PREF_DEFAULTS_50_DIR (see just above). See bug 1799332 for more
  // info.
  nsAutoCString appUpdatePrefKey;
  appUpdatePrefKey.Assign(kChannelPref);
  nsAutoCString appUpdatePrefValue;
  PrefValue channelPrefValue;
  channelPrefValue.mStringVal = MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL);
  if (ChannelPrefsUtil::GetChannelPrefValue(appUpdatePrefValue)) {
    channelPrefValue.mStringVal = appUpdatePrefValue.get();
  }
  pref_SetPref(appUpdatePrefKey, PrefType::String, PrefValueKind::Default,
               channelPrefValue,
               /* isSticky */ false,
               /* isLocked */ true,
               /* fromInit */ true);
#endif

  // Load jar:$app/omni.jar!/defaults/preferences/*.js
  // or jar:$gre/omni.jar!/defaults/preferences/*.js.
  RefPtr<nsZipArchive> appJarReader = Omnijar::GetReader(Omnijar::APP);

  // GetReader(Omnijar::APP) returns null when `$app == $gre`, in
  // which case we look for app-specific default preferences in $gre.
  if (!appJarReader) {
    appJarReader = Omnijar::GetReader(Omnijar::GRE);
  }

  if (appJarReader) {
    rv = appJarReader->FindInit("defaults/preferences/*.js$",
                                getter_Transfers(find));
    NS_ENSURE_SUCCESS(rv, rv);
    prefEntries.Clear();
    while (NS_SUCCEEDED(find->FindNext(&entryName, &entryNameLen))) {
      prefEntries.AppendElement(Substring(entryName, entryNameLen));
    }
    prefEntries.Sort();
    for (uint32_t i = prefEntries.Length(); i--;) {
      rv = pref_ReadPrefFromJar(appJarReader, prefEntries[i].get());
      if (NS_FAILED(rv)) {
        NS_WARNING("Error parsing preferences.");
      }
    }

#ifdef MOZ_BACKGROUNDTASKS
    if (BackgroundTasks::IsBackgroundTaskMode()) {
      rv = appJarReader->FindInit("defaults/backgroundtasks/*.js$",
                                  getter_Transfers(find));
      NS_ENSURE_SUCCESS(rv, rv);
      prefEntries.Clear();
      while (NS_SUCCEEDED(find->FindNext(&entryName, &entryNameLen))) {
        prefEntries.AppendElement(Substring(entryName, entryNameLen));
      }
      prefEntries.Sort();
      for (uint32_t i = prefEntries.Length(); i--;) {
        rv = pref_ReadPrefFromJar(appJarReader, prefEntries[i].get());
        if (NS_FAILED(rv)) {
          NS_WARNING("Error parsing preferences.");
        }
      }
    }
#endif
  }

  nsCOMPtr<nsIProperties> dirSvc(
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISimpleEnumerator> list;
  dirSvc->Get(NS_APP_PREFS_DEFAULTS_DIR_LIST, NS_GET_IID(nsISimpleEnumerator),
              getter_AddRefs(list));
  if (list) {
    bool hasMore;
    while (NS_SUCCEEDED(list->HasMoreElements(&hasMore)) && hasMore) {
      nsCOMPtr<nsISupports> elem;
      list->GetNext(getter_AddRefs(elem));
      if (!elem) {
        continue;
      }

      nsCOMPtr<nsIFile> path = do_QueryInterface(elem);
      if (!path) {
        continue;
      }

      // Do we care if a file provided by this process fails to load?
      pref_LoadPrefsInDir(path);
    }
  }

#if defined(MOZ_WIDGET_GTK) && defined(MOZ_SYSTEM_PREFERENCES)
  // To ensure the system-wide preferences are not overwritten by
  // firefox/browser/defauts/preferences/*.js we need to load
  // the /etc/firefox/defaults/pref/*.js settings as last.
  // Under Flatpak, the NS_OS_SYSTEM_CONFIG_DIR points to /app/etc/firefox
  nsCOMPtr<nsIFile> defaultSystemPrefDir;
  rv = NS_GetSpecialDirectory(NS_OS_SYSTEM_CONFIG_DIR,
                              getter_AddRefs(defaultSystemPrefDir));
  NS_ENSURE_SUCCESS(rv, rv);
  defaultSystemPrefDir->AppendNative("defaults"_ns);
  defaultSystemPrefDir->AppendNative("pref"_ns);

  rv = pref_LoadPrefsInDir(defaultSystemPrefDir);
  if (NS_FAILED(rv)) {
    NS_WARNING("Error parsing application default preferences.");
  }
#endif

  if (XRE_IsParentProcess()) {
    SetupTelemetryPref();
  }

  if (aIsStartup) {
    // Now that all prefs have their initial values, install the callbacks for
    // `always`-mirrored static prefs. We do this now rather than in
    // StaticPrefs::InitAll() so that the callbacks don't need to be traversed
    // while we load prefs from data files.
    StaticPrefs::StartObservingAlwaysPrefs();
  }

  NS_CreateServicesFromCategory(NS_PREFSERVICE_APPDEFAULTS_TOPIC_ID, nullptr,
                                NS_PREFSERVICE_APPDEFAULTS_TOPIC_ID);

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (NS_WARN_IF(!observerService)) {
    return NS_ERROR_FAILURE;
  }

  observerService->NotifyObservers(nullptr, NS_PREFSERVICE_APPDEFAULTS_TOPIC_ID,
                                   nullptr);

  return NS_OK;
}

/* static */
nsresult Preferences::GetBool(const char* aPrefName, bool* aResult,
                              PrefValueKind aKind) {
  MOZ_ASSERT(aResult);
  return Internals::GetPrefValue(aPrefName, aResult, aKind);
}

/* static */
nsresult Preferences::GetInt(const char* aPrefName, int32_t* aResult,
                             PrefValueKind aKind) {
  MOZ_ASSERT(aResult);
  return Internals::GetPrefValue(aPrefName, aResult, aKind);
}

/* static */
nsresult Preferences::GetFloat(const char* aPrefName, float* aResult,
                               PrefValueKind aKind) {
  MOZ_ASSERT(aResult);
  return Internals::GetPrefValue(aPrefName, aResult, aKind);
}

/* static */
nsresult Preferences::GetCString(const char* aPrefName, nsACString& aResult,
                                 PrefValueKind aKind) {
  aResult.SetIsVoid(true);
  return Internals::GetPrefValue(aPrefName, aResult, aKind);
}

/* static */
nsresult Preferences::GetString(const char* aPrefName, nsAString& aResult,
                                PrefValueKind aKind) {
  nsAutoCString result;
  nsresult rv = Preferences::GetCString(aPrefName, result, aKind);
  if (NS_SUCCEEDED(rv)) {
    CopyUTF8toUTF16(result, aResult);
  }
  return rv;
}

/* static */
nsresult Preferences::GetLocalizedCString(const char* aPrefName,
                                          nsACString& aResult,
                                          PrefValueKind aKind) {
  nsAutoString result;
  nsresult rv = GetLocalizedString(aPrefName, result, aKind);
  if (NS_SUCCEEDED(rv)) {
    CopyUTF16toUTF8(result, aResult);
  }
  return rv;
}

/* static */
nsresult Preferences::GetLocalizedString(const char* aPrefName,
                                         nsAString& aResult,
                                         PrefValueKind aKind) {
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);
  nsCOMPtr<nsIPrefLocalizedString> prefLocalString;
  nsresult rv = GetRootBranch(aKind)->GetComplexValue(
      aPrefName, NS_GET_IID(nsIPrefLocalizedString),
      getter_AddRefs(prefLocalString));
  if (NS_SUCCEEDED(rv)) {
    MOZ_ASSERT(prefLocalString, "Succeeded but the result is NULL");
    prefLocalString->GetData(aResult);
  }
  return rv;
}

/* static */
nsresult Preferences::GetComplex(const char* aPrefName, const nsIID& aType,
                                 void** aResult, PrefValueKind aKind) {
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);
  return GetRootBranch(aKind)->GetComplexValue(aPrefName, aType, aResult);
}

/* static */
bool Preferences::GetBool(const char* aPrefName, bool aFallback,
                          PrefValueKind aKind) {
  return Internals::GetPref(aPrefName, aFallback, aKind);
}

/* static */
int32_t Preferences::GetInt(const char* aPrefName, int32_t aFallback,
                            PrefValueKind aKind) {
  return Internals::GetPref(aPrefName, aFallback, aKind);
}

/* static */
uint32_t Preferences::GetUint(const char* aPrefName, uint32_t aFallback,
                              PrefValueKind aKind) {
  return Internals::GetPref(aPrefName, aFallback, aKind);
}

/* static */
float Preferences::GetFloat(const char* aPrefName, float aFallback,
                            PrefValueKind aKind) {
  return Internals::GetPref(aPrefName, aFallback, aKind);
}

/* static */
nsresult Preferences::SetCString(const char* aPrefName,
                                 const nsACString& aValue,
                                 PrefValueKind aKind) {
  ENSURE_PARENT_PROCESS("SetCString", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  if (aValue.Length() > MAX_PREF_LENGTH) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  // It's ok to stash a pointer to the temporary PromiseFlatCString's chars in
  // pref because pref_SetPref() duplicates those chars.
  PrefValue prefValue;
  const nsCString& flat = PromiseFlatCString(aValue);
  prefValue.mStringVal = flat.get();
  return pref_SetPref(nsDependentCString(aPrefName), PrefType::String, aKind,
                      prefValue,
                      /* isSticky */ false,
                      /* isLocked */ false,
                      /* fromInit */ false);
}

/* static */
nsresult Preferences::SetBool(const char* aPrefName, bool aValue,
                              PrefValueKind aKind) {
  ENSURE_PARENT_PROCESS("SetBool", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  PrefValue prefValue;
  prefValue.mBoolVal = aValue;
  return pref_SetPref(nsDependentCString(aPrefName), PrefType::Bool, aKind,
                      prefValue,
                      /* isSticky */ false,
                      /* isLocked */ false,
                      /* fromInit */ false);
}

/* static */
nsresult Preferences::SetInt(const char* aPrefName, int32_t aValue,
                             PrefValueKind aKind) {
  ENSURE_PARENT_PROCESS("SetInt", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  PrefValue prefValue;
  prefValue.mIntVal = aValue;
  return pref_SetPref(nsDependentCString(aPrefName), PrefType::Int, aKind,
                      prefValue,
                      /* isSticky */ false,
                      /* isLocked */ false,
                      /* fromInit */ false);
}

/* static */
nsresult Preferences::SetComplex(const char* aPrefName, const nsIID& aType,
                                 nsISupports* aValue, PrefValueKind aKind) {
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);
  return GetRootBranch(aKind)->SetComplexValue(aPrefName, aType, aValue);
}

/* static */
nsresult Preferences::Lock(const char* aPrefName) {
  ENSURE_PARENT_PROCESS("Lock", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  Pref* pref = MOZ_TRY(pref_LookupForModify(
      aPrefName, [](const PrefWrapper& aPref) { return !aPref.IsLocked(); }));

  if (pref) {
    pref->SetIsLocked(true);
    NotifyCallbacks(nsDependentCString{aPrefName}, PrefWrapper(pref));
  }

  return NS_OK;
}

/* static */
nsresult Preferences::Unlock(const char* aPrefName) {
  ENSURE_PARENT_PROCESS("Unlock", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  Pref* pref = MOZ_TRY(pref_LookupForModify(
      aPrefName, [](const PrefWrapper& aPref) { return aPref.IsLocked(); }));

  if (pref) {
    pref->SetIsLocked(false);
    NotifyCallbacks(nsDependentCString{aPrefName}, PrefWrapper(pref));
  }

  return NS_OK;
}

/* static */
bool Preferences::IsLocked(const char* aPrefName) {
  NS_ENSURE_TRUE(InitStaticMembers(), false);

  Maybe<PrefWrapper> pref = pref_Lookup(aPrefName);
  return pref.isSome() && pref->IsLocked();
}

/* static */
bool Preferences::IsSanitized(const char* aPrefName) {
  NS_ENSURE_TRUE(InitStaticMembers(), false);

  Maybe<PrefWrapper> pref = pref_Lookup(aPrefName);
  return pref.isSome() && pref->IsSanitized();
}

/* static */
nsresult Preferences::ClearUser(const char* aPrefName) {
  ENSURE_PARENT_PROCESS("ClearUser", aPrefName);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  auto result = pref_LookupForModify(
      aPrefName, [](const PrefWrapper& aPref) { return aPref.HasUserValue(); });
  if (result.isErr()) {
    return NS_OK;
  }

  if (Pref* pref = result.unwrap()) {
    pref->ClearUserValue();

    if (!pref->HasDefaultValue()) {
      MOZ_ASSERT(
          !gSharedMap || !pref->IsSanitized() || !gSharedMap->Has(pref->Name()),
          "A sanitized pref should never be in the shared pref map.");
      if (!pref->IsSanitized() &&
          (!gSharedMap || !gSharedMap->Has(pref->Name()))) {
        HashTable()->remove(aPrefName);
      } else {
        pref->SetType(PrefType::None);
      }

      NotifyCallbacks(nsDependentCString{aPrefName});
    } else {
      NotifyCallbacks(nsDependentCString{aPrefName}, PrefWrapper(pref));
    }

    Preferences::HandleDirty();
  }
  return NS_OK;
}

/* static */
bool Preferences::HasUserValue(const char* aPrefName) {
  NS_ENSURE_TRUE(InitStaticMembers(), false);

  Maybe<PrefWrapper> pref = pref_Lookup(aPrefName);
  return pref.isSome() && pref->HasUserValue();
}

/* static */
bool Preferences::HasDefaultValue(const char* aPrefName) {
  NS_ENSURE_TRUE(InitStaticMembers(), false);

  Maybe<PrefWrapper> pref = pref_Lookup(aPrefName);
  return pref.isSome() && pref->HasDefaultValue();
}

/* static */
nsIPrefBranch::PreferenceType Preferences::GetType(const char* aPrefName) {
  NS_ENSURE_TRUE(InitStaticMembers(), nsIPrefBranch::PREF_INVALID);

  if (!HashTable()) {
    return PREF_INVALID;
  }

  Maybe<PrefWrapper> pref = pref_Lookup(aPrefName);
  if (!pref.isSome()) {
    return PREF_INVALID;
  }

  switch (pref->Type()) {
    case PrefType::String:
      return PREF_STRING;

    case PrefType::Int:
      return PREF_INT;

    case PrefType::Bool:
      return PREF_BOOL;

    case PrefType::None:
      if (IsPreferenceSanitized(aPrefName)) {
        glean::security::pref_usage_content_process.Record(Some(
            glean::security::PrefUsageContentProcessExtra{Some(aPrefName)}));

        if (sCrashOnBlocklistedPref) {
          MOZ_CRASH_UNSAFE_PRINTF(
              "Should not access the preference '%s' in the Content Processes",
              aPrefName);
        } else {
          return PREF_INVALID;
        }
      }
      [[fallthrough]];

    default:
      MOZ_CRASH();
  }
}

/* static */
nsresult Preferences::AddStrongObserver(nsIObserver* aObserver,
                                        const nsACString& aPref) {
  MOZ_ASSERT(aObserver);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);
  return sPreferences->mRootBranch->AddObserver(aPref, aObserver, false);
}

/* static */
nsresult Preferences::AddWeakObserver(nsIObserver* aObserver,
                                      const nsACString& aPref) {
  MOZ_ASSERT(aObserver);
  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  // Periodically schedule a sweep of expired weak observers, in case
  // observers expire without triggering a notification.
  static uint32_t sWeakRegistrationsSinceSweep = 0;
  static constexpr uint32_t kSweepInterval = 512;
  if (!sSweepRunner && ++sWeakRegistrationsSinceSweep >= kSweepInterval) {
    sWeakRegistrationsSinceSweep = 0;
    MaybeScheduleExpiredWeakObserverSweep();
  }

  return sPreferences->mRootBranch->AddObserver(aPref, aObserver, true);
}

/* static */
nsresult Preferences::RemoveObserver(nsIObserver* aObserver,
                                     const nsACString& aPref) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aObserver);
  if (sShutdown) {
    MOZ_ASSERT(!sPreferences);
    return NS_OK;  // Observers have been released automatically.
  }
  NS_ENSURE_TRUE(sPreferences, NS_ERROR_NOT_AVAILABLE);
  return sPreferences->mRootBranch->RemoveObserver(aPref, aObserver);
}

template <typename T>
static void AssertNotMallocAllocated(T* aPtr) {
#if defined(DEBUG) && defined(MOZ_MEMORY)
  jemalloc_ptr_info_t info;
  jemalloc_ptr_info((void*)aPtr, &info);
  MOZ_ASSERT(info.tag == TagUnknown);
#endif
}

/* static */
nsresult Preferences::AddStrongObservers(nsIObserver* aObserver,
                                         const char* const* aPrefs) {
  MOZ_ASSERT(aObserver);
  for (uint32_t i = 0; aPrefs[i]; i++) {
    AssertNotMallocAllocated(aPrefs[i]);

    nsCString pref;
    pref.AssignLiteral(aPrefs[i], strlen(aPrefs[i]));
    nsresult rv = AddStrongObserver(aObserver, pref);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

/* static */
nsresult Preferences::AddWeakObservers(nsIObserver* aObserver,
                                       const char* const* aPrefs) {
  MOZ_ASSERT(aObserver);
  for (uint32_t i = 0; aPrefs[i]; i++) {
    AssertNotMallocAllocated(aPrefs[i]);

    nsCString pref;
    pref.AssignLiteral(aPrefs[i], strlen(aPrefs[i]));
    nsresult rv = AddWeakObserver(aObserver, pref);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

/* static */
nsresult Preferences::RemoveObservers(nsIObserver* aObserver,
                                      const char* const* aPrefs) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aObserver);
  if (sShutdown) {
    MOZ_ASSERT(!sPreferences);
    return NS_OK;  // Observers have been released automatically.
  }
  NS_ENSURE_TRUE(sPreferences, NS_ERROR_NOT_AVAILABLE);

  for (uint32_t i = 0; aPrefs[i]; i++) {
    nsresult rv = RemoveObserver(aObserver, nsDependentCString(aPrefs[i]));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

template <typename T>
/* static */
nsresult Preferences::RegisterCallbackImpl(PrefChangedFunc aCallback,
                                           T& aPrefNode, void* aData,
                                           MatchKind aMatchKind,
                                           bool aIsPriority) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aCallback);

  NS_ENSURE_TRUE(InitStaticMembers(), NS_ERROR_NOT_AVAILABLE);

  auto node = new CallbackNode(aPrefNode, aCallback, aData, aMatchKind);

  if (aIsPriority) {
    // Add to the start of the list.
    node->SetNext(gFirstCallback);
    gFirstCallback = node;
    if (!gLastPriorityNode) {
      gLastPriorityNode = node;
    }
  } else {
    // Add to the start of the non-priority part of the list.
    if (gLastPriorityNode) {
      node->SetNext(gLastPriorityNode->Next());
      gLastPriorityNode->SetNext(node);
    } else {
      node->SetNext(gFirstCallback);
      gFirstCallback = node;
    }
  }

  return NS_OK;
}

/* static */
nsresult Preferences::RegisterCallback(PrefChangedFunc aCallback,
                                       const nsACString& aPrefNode, void* aData,
                                       MatchKind aMatchKind, bool aIsPriority) {
  return RegisterCallbackImpl(aCallback, aPrefNode, aData, aMatchKind,
                              aIsPriority);
}

/* static */
nsresult Preferences::RegisterCallbacks(PrefChangedFunc aCallback,
                                        const char* const* aPrefs, void* aData,
                                        MatchKind aMatchKind) {
  return RegisterCallbackImpl(aCallback, aPrefs, aData, aMatchKind);
}

/* static */
nsresult Preferences::RegisterCallbackAndCall(PrefChangedFunc aCallback,
                                              const nsACString& aPref,
                                              void* aClosure,
                                              MatchKind aMatchKind) {
  MOZ_ASSERT(aCallback);
  nsresult rv = RegisterCallback(aCallback, aPref, aClosure, aMatchKind);
  if (NS_SUCCEEDED(rv)) {
    (*aCallback)(PromiseFlatCString(aPref).get(), aClosure);
  }
  return rv;
}

/* static */
nsresult Preferences::RegisterCallbacksAndCall(PrefChangedFunc aCallback,
                                               const char* const* aPrefs,
                                               void* aClosure) {
  MOZ_ASSERT(aCallback);

  nsresult rv =
      RegisterCallbacks(aCallback, aPrefs, aClosure, MatchKind::ExactMatch);
  if (NS_SUCCEEDED(rv)) {
    for (const char* const* ptr = aPrefs; *ptr; ptr++) {
      (*aCallback)(*ptr, aClosure);
    }
  }
  return rv;
}

template <typename T>
/* static */
nsresult Preferences::UnregisterCallbackImpl(PrefChangedFunc aCallback,
                                             T& aPrefNode, void* aData,
                                             MatchKind aMatchKind) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCallback);
  if (sShutdown) {
    MOZ_ASSERT(!sPreferences);
    return NS_OK;  // Observers have been released automatically.
  }
  NS_ENSURE_TRUE(sPreferences, NS_ERROR_NOT_AVAILABLE);

  nsresult rv = NS_ERROR_FAILURE;
  CallbackNode* node = gFirstCallback;
  CallbackNode* prev_node = nullptr;

  while (node) {
    if (node->Func() == aCallback && node->Data() == aData &&
        node->MatchKind() == aMatchKind && node->DomainIs(aPrefNode)) {
      if (gCallbacksInProgress) {
        // Postpone the node removal until after callbacks enumeration is
        // finished.
        node->ClearFunc();
        gShouldCleanupDeadNodes = true;
        prev_node = node;
        node = node->Next();
      } else {
        node = pref_RemoveCallbackNode(node, prev_node);
      }
      rv = NS_OK;
    } else {
      prev_node = node;
      node = node->Next();
    }
  }
  return rv;
}

/* static */
nsresult Preferences::UnregisterCallback(PrefChangedFunc aCallback,
                                         const nsACString& aPrefNode,
                                         void* aData, MatchKind aMatchKind) {
  return UnregisterCallbackImpl<const nsACString&>(aCallback, aPrefNode, aData,
                                                   aMatchKind);
}

/* static */
nsresult Preferences::UnregisterCallbacks(PrefChangedFunc aCallback,
                                          const char* const* aPrefs,
                                          void* aData, MatchKind aMatchKind) {
  return UnregisterCallbackImpl(aCallback, aPrefs, aData, aMatchKind);
}

/* static */
uint32_t Preferences::UnregisterCallbacksForBranch(nsPrefBranch* aBranch) {
  MOZ_ASSERT(NS_IsMainThread());
  if (sShutdown || !sPreferences) {
    return 0;
  }

  uint32_t removedCount = 0;
  CallbackNode* node = gFirstCallback;
  CallbackNode* prev_node = nullptr;

  while (node) {
    if (node->Func() == nsPrefBranch::NotifyObserver &&
        static_cast<PrefCallback*>(node->Data())->GetPrefBranch() == aBranch) {
      ++removedCount;
      if (gCallbacksInProgress) {
        node->ClearFunc();
        gShouldCleanupDeadNodes = true;
        prev_node = node;
        node = node->Next();
      } else {
        node = pref_RemoveCallbackNode(node, prev_node);
      }
    } else {
      prev_node = node;
      node = node->Next();
    }
  }
  return removedCount;
}

template <typename T>
static void AddMirrorCallback(T* aMirror, const nsACString& aPref) {
  MOZ_ASSERT(NS_IsMainThread());

  Internals::RegisterCallback<T>(aMirror, aPref);
}

// Don't inline because it explodes compile times.
template <typename T>
static MOZ_NEVER_INLINE void AddMirror(T* aMirror, const nsACString& aPref,
                                       StripAtomic<T> aDefault) {
  *aMirror = Internals::GetPref(PromiseFlatCString(aPref).get(), aDefault);
  AddMirrorCallback(aMirror, aPref);
}

static MOZ_NEVER_INLINE void AddMirror(DataMutexString& aMirror,
                                       const nsACString& aPref) {
  auto lock = aMirror.Lock();
  nsCString result(*lock);
  Internals::GetPrefValue(PromiseFlatCString(aPref).get(), result,
                          PrefValueKind::User);
  lock->Assign(std::move(result));
  AddMirrorCallback(&aMirror, aPref);
}

// The InitPref_*() functions below end in a `_<type>` suffix because they are
// used by the PREF macro definition in InitAll() below.

static void InitPref_bool(const nsCString& aName, bool aDefaultValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  PrefValue value;
  value.mBoolVal = aDefaultValue;
  pref_SetPref(aName, PrefType::Bool, PrefValueKind::Default, value,
               /* isSticky */ false,
               /* isLocked */ false,
               /* fromInit */ true);
}

static void InitPref_int32_t(const nsCString& aName, int32_t aDefaultValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  PrefValue value;
  value.mIntVal = aDefaultValue;
  pref_SetPref(aName, PrefType::Int, PrefValueKind::Default, value,
               /* isSticky */ false,
               /* isLocked */ false,
               /* fromInit */ true);
}

static void InitPref_uint32_t(const nsCString& aName, uint32_t aDefaultValue) {
  InitPref_int32_t(aName, int32_t(aDefaultValue));
}

static void InitPref_float(const nsCString& aName, float aDefaultValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  PrefValue value;
  // Convert the value in a locale-independent way, including a trailing ".0"
  // if necessary to distinguish floating-point from integer prefs when viewing
  // them in about:config.
  nsAutoCString defaultValue;
  defaultValue.AppendFloat(aDefaultValue);
  if (!defaultValue.Contains('.') && !defaultValue.Contains('e')) {
    defaultValue.AppendLiteral(".0");
  }
  value.mStringVal = defaultValue.get();
  pref_SetPref(aName, PrefType::String, PrefValueKind::Default, value,
               /* isSticky */ false,
               /* isLocked */ false,
               /* fromInit */ true);
}

static void InitPref_String(const nsCString& aName, const char* aDefaultValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  PrefValue value;
  value.mStringVal = aDefaultValue;
  pref_SetPref(aName, PrefType::String, PrefValueKind::Default, value,
               /* isSticky */ false,
               /* isLocked */ false,
               /* fromInit */ true);
}

static void InitPref(const nsCString& aName, bool aDefaultValue) {
  InitPref_bool(aName, aDefaultValue);
}
static void InitPref(const nsCString& aName, int32_t aDefaultValue) {
  InitPref_int32_t(aName, aDefaultValue);
}
static void InitPref(const nsCString& aName, uint32_t aDefaultValue) {
  InitPref_uint32_t(aName, aDefaultValue);
}
static void InitPref(const nsCString& aName, float aDefaultValue) {
  InitPref_float(aName, aDefaultValue);
}

template <typename T>
static void InitAlwaysPref(const nsCString& aName, T* aCache,
                           StripAtomic<T> aDefaultValue) {
  // Only called in the parent process. Set/reset the pref value and the
  // `always` mirror to the default value.
  // `once` mirrors will be initialized lazily in InitOncePrefs().
  InitPref(aName, aDefaultValue);
  *aCache = aDefaultValue;
}

static void InitAlwaysPref(const nsCString& aName, DataMutexString& aCache,
                           const nsLiteralCString& aDefaultValue) {
  // Only called in the parent process. Set/reset the pref value and the
  // `always` mirror to the default value.
  // `once` mirrors will be initialized lazily in InitOncePrefs().
  InitPref_String(aName, aDefaultValue.get());
  Internals::AssignMirror(aCache, aDefaultValue);
}

static Atomic<bool> sOncePrefRead(false);

namespace StaticPrefs {

void MaybeInitOncePrefs() {
  if (MOZ_LIKELY(sOncePrefRead)) {
    // `once`-mirrored prefs have already been initialized to their default
    // value.
    return;
  }

  if (NS_IsMainThread()) {
    // sOncePrefRead could not have changed since the last check since it is
    // only set by the main thread.
    InitOncePrefs();
    sOncePrefRead = true;
  } else {
    RefPtr<Runnable> runnable = NS_NewRunnableFunction(
        "Preferences::MaybeInitOncePrefs", [&]() { MaybeInitOncePrefs(); });
    // This logic needs to run on the main thread
    SyncRunnable::DispatchToThread(GetMainThreadSerialEventTarget(), runnable);
  }
}

// For mirrored prefs we generate a variable definition.
#define NEVER_PREF(name, cpp_type, value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, default_value) \
  cpp_type sMirror_##full_id(default_value);
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, default_value) \
  MOZ_RUNINIT cpp_type sMirror_##full_id("DataMutexString");
#define ONCE_PREF(name, base_id, full_id, cpp_type, default_value) \
  cpp_type sMirror_##full_id(default_value);
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF

static void InitAll() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  // For all prefs we generate some initialization code.
  //
  // The InitPref_*() functions have a type suffix to avoid ambiguity between
  // prefs having int32_t and float default values. That suffix is not needed
  // for the InitAlwaysPref() functions because they take a pointer parameter,
  // which prevents automatic int-to-float coercion.
#define NEVER_PREF(name, cpp_type, value) \
  InitPref_##cpp_type(name ""_ns, value);
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, value) \
  InitAlwaysPref(name ""_ns, &sMirror_##full_id, value);
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, value) \
  InitAlwaysPref(name ""_ns, sMirror_##full_id, value);
#define ONCE_PREF(name, base_id, full_id, cpp_type, value) \
  InitPref_##cpp_type(name ""_ns, value);
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
}

static void StartObservingAlwaysPrefs() {
  MOZ_ASSERT(NS_IsMainThread());

  // Call AddMirror so that our mirrors for `always` prefs will stay updated.
  // The call to AddMirror re-reads the current pref value into the mirror, so
  // our mirror will now be up-to-date even if some of the prefs have changed
  // since the call to InitAll().
#define NEVER_PREF(name, cpp_type, value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, value) \
  AddMirror(&sMirror_##full_id, name ""_ns, sMirror_##full_id);
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, value) \
  AddMirror(sMirror_##full_id, name ""_ns);
#define ONCE_PREF(name, base_id, full_id, cpp_type, value)
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
}

static void InitOncePrefs() MOZ_REQUIRES(sMainThreadCapability) {
  // For `once`-mirrored prefs we generate some initialization code. This is
  // done in case the pref value was updated when reading pref data files. It's
  // necessary because we don't have callbacks registered for `once`-mirrored
  // prefs.
  //
  // In debug builds, we also install a mechanism that can check if the
  // preference value is modified after `once`-mirrored prefs are initialized.
  // In tests this would indicate a likely misuse of a `once`-mirrored pref and
  // suggest that it should instead be `always`-mirrored.
#define NEVER_PREF(name, cpp_type, value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, value)
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, value)
#ifdef DEBUG
#  define ONCE_PREF(name, base_id, full_id, cpp_type, value)                   \
    {                                                                          \
      MOZ_ASSERT(gOnceStaticPrefsAntiFootgun);                                 \
      sMirror_##full_id = Internals::GetPref(name, cpp_type(value));           \
      auto checkPref = [&]() {                                                 \
        MOZ_ASSERT(sOncePrefRead);                                             \
        cpp_type staticPrefValue = full_id();                                  \
        cpp_type preferenceValue =                                             \
            Internals::GetPref(GetPrefName_##base_id(), cpp_type(value));      \
        MOZ_ASSERT(staticPrefValue == preferenceValue,                         \
                   "Preference '" name                                         \
                   "' got modified since StaticPrefs::" #full_id               \
                   " was initialized. Consider using an `always` mirror kind " \
                   "instead");                                                 \
      };                                                                       \
      gOnceStaticPrefsAntiFootgun->insert(                                     \
          std::pair<const char*, AntiFootgunCallback>(GetPrefName_##base_id(), \
                                                      std::move(checkPref)));  \
    }
#else
#  define ONCE_PREF(name, base_id, full_id, cpp_type, value) \
    sMirror_##full_id = Internals::GetPref(name, cpp_type(value));
#endif

#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
}

static void ShutdownAlwaysPrefs() {
  MOZ_ASSERT(NS_IsMainThread());

  // We may need to do clean up for leak detection for some StaticPrefs.
#define NEVER_PREF(name, cpp_type, value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, value)
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, value) \
  Internals::ClearMirror(sMirror_##full_id);
#define ONCE_PREF(name, base_id, full_id, cpp_type, value)
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
}

}  // namespace StaticPrefs

[[maybe_unused]] static void SaveOncePrefToSharedMap(
    SharedPrefMapBuilder& aBuilder, const nsACString& aName, bool aValue) {
  auto oncePref = MakeUnique<Pref>(aName);
  oncePref->SetType(PrefType::Bool);
  oncePref->SetIsSkippedByIteration(true);
  bool valueChanged = false;
  MOZ_ALWAYS_SUCCEEDS(
      oncePref->SetDefaultValue(PrefType::Bool, PrefValue(aValue),
                                /* isSticky */ true,
                                /* isLocked */ true, &valueChanged));
  oncePref->AddToMap(aBuilder);
}

[[maybe_unused]] static void SaveOncePrefToSharedMap(
    SharedPrefMapBuilder& aBuilder, const nsACString& aName, int32_t aValue) {
  auto oncePref = MakeUnique<Pref>(aName);
  oncePref->SetType(PrefType::Int);
  oncePref->SetIsSkippedByIteration(true);
  bool valueChanged = false;
  MOZ_ALWAYS_SUCCEEDS(
      oncePref->SetDefaultValue(PrefType::Int, PrefValue(aValue),
                                /* isSticky */ true,
                                /* isLocked */ true, &valueChanged));
  oncePref->AddToMap(aBuilder);
}

[[maybe_unused]] static void SaveOncePrefToSharedMap(
    SharedPrefMapBuilder& aBuilder, const nsACString& aName, uint32_t aValue) {
  SaveOncePrefToSharedMap(aBuilder, aName, int32_t(aValue));
}

[[maybe_unused]] static void SaveOncePrefToSharedMap(
    SharedPrefMapBuilder& aBuilder, const nsACString& aName, float aValue) {
  auto oncePref = MakeUnique<Pref>(aName);
  oncePref->SetType(PrefType::String);
  oncePref->SetIsSkippedByIteration(true);
  nsAutoCString value;
  value.AppendFloat(aValue);
  bool valueChanged = false;
  // It's ok to stash a pointer to the temporary PromiseFlatCString's chars in
  // pref because pref_SetPref() duplicates those chars.
  const nsCString& flat = PromiseFlatCString(value);
  MOZ_ALWAYS_SUCCEEDS(
      oncePref->SetDefaultValue(PrefType::String, PrefValue(flat.get()),
                                /* isSticky */ true,
                                /* isLocked */ true, &valueChanged));
  oncePref->AddToMap(aBuilder);
}

#define ONCE_PREF_NAME(name) "$$$" name "$$$"

namespace StaticPrefs {

static void RegisterOncePrefs(SharedPrefMapBuilder& aBuilder) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_DIAGNOSTIC_ASSERT(!gSharedMap,
                        "Must be called before gSharedMap has been created");
  MaybeInitOncePrefs();

  // For `once`-mirrored prefs we generate a save call, which saves the value
  // as it was at parent startup. It is stored in a special (hidden and locked)
  // entry in the global SharedPreferenceMap. In order for the entry to be
  // hidden and not appear in about:config nor ever be stored to disk, we set
  // its IsSkippedByIteration flag to true. We also distinguish it by adding a
  // "$$$" prefix and suffix to the preference name.
#define NEVER_PREF(name, cpp_type, value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, value)
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, value)
#define ONCE_PREF(name, base_id, full_id, cpp_type, value)      \
  SaveOncePrefToSharedMap(aBuilder, ONCE_PREF_NAME(name) ""_ns, \
                          cpp_type(sMirror_##full_id));
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
}

// Disable thread safety analysis on this function, because it explodes build
// times and memory usage.
MOZ_NO_THREAD_SAFETY_ANALYSIS
static void InitStaticPrefsFromShared() {
  MOZ_ASSERT(!XRE_IsParentProcess());
  MOZ_DIAGNOSTIC_ASSERT(gSharedMap,
                        "Must be called once gSharedMap has been created");

#ifdef DEBUG
#  define ASSERT_PREF_NOT_SANITIZED(name, cpp_type)                          \
    if (IsString<cpp_type>::value && IsPreferenceSanitized(name)) {          \
      MOZ_CRASH("Unexpected sanitized string preference '" name              \
                "'. "                                                        \
                "Static Preferences cannot be sanitized currently, because " \
                "they expect to be initialized from the Static Map, and "    \
                "sanitized preferences are not present there.");             \
    }
#else
#  define ASSERT_PREF_NOT_SANITIZED(name, cpp_type)
#endif

  // For mirrored static prefs we generate some initialization code. Each
  // mirror variable is already initialized in the binary with the default
  // value. If the pref value hasn't changed from the default in the main
  // process (the common case) then the overwriting here won't change the
  // mirror variable's value.
  //
  // Note that the MOZ_ASSERT calls below can fail in one obscure case: when a
  // Firefox update occurs and we get a main process from the old binary (with
  // static prefs {A,B,C,D}) plus a new content process from the new binary
  // (with static prefs {A,B,C,D,E}). The content process' call to
  // GetSharedPrefValue() for pref E will fail because the shared pref map was
  // created by the main process, which doesn't have pref E.
  //
  // This silent failure is safe. The mirror variable for pref E is already
  // initialized to the default value in the content process, and the main
  // process cannot have changed pref E because it doesn't know about it!
  //
  // Nonetheless, it's useful to have the MOZ_ASSERT here for testing of debug
  // builds, where this scenario involving inconsistent binaries should not
  // occur.
#define NEVER_PREF(name, cpp_type, default_value)
#define ALWAYS_PREF(name, base_id, full_id, cpp_type, default_value)    \
  {                                                                     \
    StripAtomic<cpp_type> val;                                          \
    ASSERT_PREF_NOT_SANITIZED(name, cpp_type);                          \
    DebugOnly<nsresult> rv = Internals::GetSharedPrefValue(name, &val); \
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Failed accessing " name);             \
    StaticPrefs::sMirror_##full_id = val;                               \
  }
#define ALWAYS_DATAMUTEX_PREF(name, base_id, full_id, cpp_type, default_value) \
  {                                                                            \
    StripAtomic<cpp_type> val;                                                 \
    ASSERT_PREF_NOT_SANITIZED(name, cpp_type);                                 \
    DebugOnly<nsresult> rv = Internals::GetSharedPrefValue(name, &val);        \
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Failed accessing " name);                    \
    Internals::AssignMirror(StaticPrefs::sMirror_##full_id,                    \
                            std::forward<StripAtomic<cpp_type>>(val));         \
  }
#define ONCE_PREF(name, base_id, full_id, cpp_type, default_value) \
  {                                                                \
    cpp_type val;                                                  \
    ASSERT_PREF_NOT_SANITIZED(name, cpp_type);                     \
    DebugOnly<nsresult> rv =                                       \
        Internals::GetSharedPrefValue(ONCE_PREF_NAME(name), &val); \
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Failed accessing " name);        \
    StaticPrefs::sMirror_##full_id = val;                          \
  }
#include "mozilla/StaticPrefListAll.h"
#undef NEVER_PREF
#undef ALWAYS_PREF
#undef ALWAYS_DATAMUTEX_PREF
#undef ONCE_PREF
#undef ASSERT_PREF_NOT_SANITIZED

  // `once`-mirrored prefs have been set to their value in the step above and
  // outside the parent process they are immutable. We set sOncePrefRead so
  // that we can directly skip any lazy initializations.
  sOncePrefRead = true;
}

}  // namespace StaticPrefs

}  // namespace mozilla

#undef ENSURE_PARENT_PROCESS

//===========================================================================
// Module and factory stuff
//===========================================================================

NS_IMPL_COMPONENT_FACTORY(nsPrefLocalizedString) {
  auto str = MakeRefPtr<nsPrefLocalizedString>();
  if (NS_SUCCEEDED(str->Init())) {
    return str.forget().downcast<nsISupports>();
  }
  return nullptr;
}

NS_IMPL_COMPONENT_FACTORY(nsPrefOverrideMap) {
  auto comp = MakeRefPtr<nsPrefOverrideMap>();
  return comp.forget().downcast<nsISupports>();
}

namespace mozilla {

void UnloadPrefsModule() { Preferences::Shutdown(); }

}  // namespace mozilla

// Preference Sanitization Related Code ---------------------------------------

#define PREF_LIST_ENTRY(s) {s, (sizeof(s) / sizeof(char)) - 1}
struct PrefListEntry {
  const char* mPrefBranch;
  size_t mLen;
};

// A preference is 'sanitized' (i.e. not sent to web content processes) if
// one of two criteria are met:
//   1. The pref name matches one of the prefixes in the following list
//   2. The pref is dynamically named (i.e. not specified in all.js or
//      StaticPrefList.yml), a string pref, and it is NOT exempted in
//      sDynamicPrefOverrideList
//
// This behavior is codified in ShouldSanitizePreference() below.
// Exclusions of preferences can be defined in sOverrideRestrictionsList[].
static const PrefListEntry sRestrictFromWebContentProcesses[] = {
    // Remove prefs with user data
    PREF_LIST_ENTRY("datareporting.policy."),
    PREF_LIST_ENTRY("browser.download.lastDir"),
    PREF_LIST_ENTRY("browser.newtabpage.pinned"),
    PREF_LIST_ENTRY("browser.uiCustomization.state"),
    PREF_LIST_ENTRY("browser.urlbar"),
    PREF_LIST_ENTRY("devtools.debugger.pending-selected-location"),
    PREF_LIST_ENTRY("identity.fxaccounts.account.device.name"),
    PREF_LIST_ENTRY("identity.fxaccounts.account.telemetry.sanitized_uid"),
    PREF_LIST_ENTRY("identity.fxaccounts.lastSignedInUserHash"),
    PREF_LIST_ENTRY("print_printer"),
    PREF_LIST_ENTRY("services."),
    PREF_LIST_ENTRY("termsofuse."),

    // Remove UUIDs
    PREF_LIST_ENTRY("app.normandy.user_id"),
    PREF_LIST_ENTRY("browser.newtabpage.activity-stream.impressionId"),
    PREF_LIST_ENTRY("browser.pageActions.persistedActions"),
    PREF_LIST_ENTRY("browser.startup.lastColdStartupCheck"),
    PREF_LIST_ENTRY("dom.push.userAgentID"),
    PREF_LIST_ENTRY("extensions.webextensions.uuids"),
    PREF_LIST_ENTRY("privacy.userContext.extension"),
    PREF_LIST_ENTRY("toolkit.telemetry.cachedClientID"),
    PREF_LIST_ENTRY("toolkit.telemetry.cachedProfileGroupID"),

    // Remove IDs that could be used to correlate across origins
    PREF_LIST_ENTRY("app.update.lastUpdateTime."),
    PREF_LIST_ENTRY(
        "browser.contentblocking.cfr-milestone.milestone-shown-time"),
    PREF_LIST_ENTRY("browser.contextual-services.contextId"),
    PREF_LIST_ENTRY("browser.laterrun.bookkeeping.profileCreationTime"),
    PREF_LIST_ENTRY("browser.newtabpage.activity-stream.discoverystream."),
    PREF_LIST_ENTRY("browser.sessionstore.upgradeBackup.latestBuildID"),
    PREF_LIST_ENTRY("browser.shell.mostRecentDateSetAsDefault"),
    PREF_LIST_ENTRY("idle.lastDailyNotification"),
    PREF_LIST_ENTRY("media.gmp-gmpopenh264.lastUpdate"),
    PREF_LIST_ENTRY("media.gmp-manager.lastCheck"),
    PREF_LIST_ENTRY("places.database.lastMaintenance"),
    PREF_LIST_ENTRY("privacy.purge_trackers.last_purge"),
    PREF_LIST_ENTRY("storage.vacuum.last.places.sqlite"),
    PREF_LIST_ENTRY("toolkit.startup.last_success"),

    // Remove fingerprintable things
    PREF_LIST_ENTRY("browser.startup.homepage_override.buildID"),
    PREF_LIST_ENTRY("extensions.lastAppBuildId"),
    PREF_LIST_ENTRY("media.gmp-manager.buildID"),
    PREF_LIST_ENTRY("toolkit.telemetry.previousBuildID"),
};

// Allowlist for prefs and branches blocklisted in
// sRestrictFromWebContentProcesses[], including prefs from
// StaticPrefList.yaml and *.js, to let them pass.
static const PrefListEntry sOverrideRestrictionsList[]{
    PREF_LIST_ENTRY("services.settings.clock_skew_seconds"),
    PREF_LIST_ENTRY("services.settings.last_update_seconds"),
    PREF_LIST_ENTRY("services.settings.loglevel"),
    // This is really a boolean dynamic pref, but one Nightly user
    // has it set as a string...
    PREF_LIST_ENTRY("services.settings.preview_enabled"),
    PREF_LIST_ENTRY("services.settings.server"),
};

// These prefs are dynamically-named (i.e. not specified in prefs.js or
// StaticPrefList) and would normally by blocklisted but we allow them through
// anyway, so this override list acts as an allowlist
static const PrefListEntry sDynamicPrefOverrideList[]{
    PREF_LIST_ENTRY("accessibility.tabfocus"),
    PREF_LIST_ENTRY("app.update.channel"),
    PREF_LIST_ENTRY("apz.subtest"),
    PREF_LIST_ENTRY("browser.contentblocking.category"),
    PREF_LIST_ENTRY("browser.dom.window.dump.file"),
    PREF_LIST_ENTRY("browser.search.region"),
    PREF_LIST_ENTRY(
        "browser.tabs.remote.testOnly.failPBrowserCreation.browsingContext"),
    PREF_LIST_ENTRY("browser.uitour.testingOrigins"),
    PREF_LIST_ENTRY("browser.urlbar.loglevel"),
    PREF_LIST_ENTRY("browser.urlbar.opencompanionsearch.enabled"),
    PREF_LIST_ENTRY("capability.policy"),
    PREF_LIST_ENTRY("dom.securecontext.allowlist"),
    PREF_LIST_ENTRY("extensions.foobaz"),
    PREF_LIST_ENTRY(
        "extensions.formautofill.creditCards.heuristics.testConfidence"),
    PREF_LIST_ENTRY("general.appversion.override"),
    PREF_LIST_ENTRY("general.buildID.override"),
    PREF_LIST_ENTRY("general.oscpu.override"),
    PREF_LIST_ENTRY("general.useragent.override"),
    PREF_LIST_ENTRY("general.platform.override"),
    PREF_LIST_ENTRY("gfx.blacklist."),
    PREF_LIST_ENTRY("font.system.whitelist"),
    PREF_LIST_ENTRY("font.name."),
    PREF_LIST_ENTRY("intl.date_time.pattern_override."),
    PREF_LIST_ENTRY("intl.hyphenation-alias."),
    PREF_LIST_ENTRY("logging.config.LOG_FILE"),
    PREF_LIST_ENTRY("media.audio_loopback_dev"),
    PREF_LIST_ENTRY("media.decoder-doctor."),
    PREF_LIST_ENTRY("media.cubeb.backend"),
    PREF_LIST_ENTRY("media.cubeb.output_device"),
    PREF_LIST_ENTRY("media.getusermedia.fake-camera-name"),
    PREF_LIST_ENTRY("media.hls.server.url"),
    PREF_LIST_ENTRY("media.peerconnection.nat_simulator.filtering_type"),
    PREF_LIST_ENTRY("media.peerconnection.nat_simulator.mapping_type"),
    PREF_LIST_ENTRY("media.peerconnection.nat_simulator.redirect_address"),
    PREF_LIST_ENTRY("media.peerconnection.nat_simulator.redirect_targets"),
    PREF_LIST_ENTRY("media.peerconnection.nat_simulator.network_delay_ms"),
    PREF_LIST_ENTRY("media.video_loopback_dev"),
    PREF_LIST_ENTRY("media.webspeech.service.endpoint"),
    PREF_LIST_ENTRY("network.protocol-handler.external."),
    PREF_LIST_ENTRY("network.security.ports.banned"),
    PREF_LIST_ENTRY("nimbus.syncdatastore."),
    PREF_LIST_ENTRY("pdfjs."),
    PREF_LIST_ENTRY("plugins.force.wmode"),
    PREF_LIST_ENTRY("print.printer_"),
    PREF_LIST_ENTRY("print_printer"),
    PREF_LIST_ENTRY("places.interactions.customBlocklist"),
    PREF_LIST_ENTRY("remote.log.level"),
    // services.* preferences should be added in sOverrideRestrictionsList[] -
    // the whole preference branch gets sanitized by default.
    PREF_LIST_ENTRY("spellchecker.dictionary"),
    PREF_LIST_ENTRY("test.char"),
    PREF_LIST_ENTRY("Test.IPC."),
    PREF_LIST_ENTRY("exists.thenDoesNot"),
    PREF_LIST_ENTRY("type.String."),
    PREF_LIST_ENTRY("toolkit.mozprotocol.url"),
    PREF_LIST_ENTRY("toolkit.telemetry.log.level"),
    PREF_LIST_ENTRY("ui."),
};

#undef PREF_LIST_ENTRY

static bool ShouldSanitizePreference(const Pref* const aPref) {
  // In the parent process, we use a heuristic to decide if a pref
  // value should be sanitized before sending to subprocesses.
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  const char* prefName = aPref->Name();

  // If a pref starts with this magic string, it is a Once-Initialized pref
  // from Static Prefs. It should* not be in the above list and while it looks
  // like a dnyamically named pref, it is not.
  // * nothing enforces this
  if (strncmp(prefName, "$$$", 3) == 0) {
    return false;
  }

  // First check against the denylist.
  // The services pref is an annoying one - it's much easier to blocklist
  // the whole branch and then add this one check to let this one annoying
  // pref through.
  for (const auto& entry : sRestrictFromWebContentProcesses) {
    if (strncmp(entry.mPrefBranch, prefName, entry.mLen) == 0) {
      for (const auto& pasEnt : sOverrideRestrictionsList) {
        if (strncmp(pasEnt.mPrefBranch, prefName, pasEnt.mLen) == 0) {
          return false;
        }
      }
      return true;
    }
  }

  // Then check if it's a dynamically named string preference and not
  // in the override list
  if (aPref->Type() == PrefType::String && !aPref->HasDefaultValue()) {
    for (const auto& entry : sDynamicPrefOverrideList) {
      if (strncmp(entry.mPrefBranch, prefName, entry.mLen) == 0) {
        return false;
      }
    }
    return true;
  }

  return false;
}

// Forward Declaration - it's not defined in the .h, because we don't need to;
// it's only used here.
template <class T>
static bool IsPreferenceSanitized_Impl(const T& aPref);

static bool IsPreferenceSanitized(const Pref* const aPref) {
  return IsPreferenceSanitized_Impl(*aPref);
}

static bool IsPreferenceSanitized(const PrefWrapper& aPref) {
  return IsPreferenceSanitized_Impl(aPref);
}

template <class T>
static bool IsPreferenceSanitized_Impl(const T& aPref) {
  if (aPref.IsSanitized()) {
    MOZ_DIAGNOSTIC_ASSERT(!XRE_IsParentProcess());
    MOZ_DIAGNOSTIC_ASSERT(XRE_IsContentProcess());
    return true;
  }
  return false;
}

namespace mozilla {

// This is the only Check Sanitization function exposed outside of
// Preferences.cpp, because this is the only one ever called from
// outside this file.
bool IsPreferenceSanitized(const char* aPrefName) {
  // Perform this comparison (see notes above) early to avoid a lookup
  // if we can avoid it.
  if (strncmp(aPrefName, "$$$", 3) == 0) {
    return false;
  }

  if (!gContentProcessPrefsAreInited) {
    return false;
  }

  if (Maybe<PrefWrapper> pref = pref_Lookup(aPrefName)) {
    if (pref.isNothing()) {
      return true;
    }
    return IsPreferenceSanitized(pref.value());
  }

  return true;
}

Atomic<bool, Relaxed> sOmitBlocklistedPrefValues(false);
Atomic<bool, Relaxed> sCrashOnBlocklistedPref(false);

void OnFissionBlocklistPrefChange(const char* aPref, void* aData) {
  if (strcmp(aPref, kFissionEnforceBlockList) == 0) {
    sCrashOnBlocklistedPref =
        StaticPrefs::fission_enforceBlocklistedPrefsInSubprocesses();
  } else if (strcmp(aPref, kFissionOmitBlockListValues) == 0) {
    sOmitBlocklistedPrefValues =
        StaticPrefs::fission_omitBlocklistedPrefsInSubprocesses();
  } else {
    MOZ_CRASH("Unknown pref passed to callback");
  }
}

}  // namespace mozilla

// This file contains the C wrappers for the C++ static pref getters, as used
// by Rust code.
#include "init/StaticPrefsCGetters.cpp"
