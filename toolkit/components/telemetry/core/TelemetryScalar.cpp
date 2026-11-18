/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TelemetryScalar.h"

#include "ETWTools.h"
#include "ipc/TelemetryIPCAccumulator.h"
#include "js/Array.h"               // JS::GetArrayLength, JS::IsArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_DefineUCProperty, JS_Enumerate, JS_GetElement, JS_GetProperty, JS_GetPropertyById, JS_HasProperty
#include "mozilla/dom/ContentParent.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TelemetryComms.h"
#include "nsBaseHashtable.h"
#include "nsClassHashtable.h"
#include "nsContentUtils.h"
#include "nsHashKeys.h"
#include "nsITelemetry.h"
#include "nsIVariant.h"
#include "nsIXPConnect.h"
#include "nsJSUtils.h"
#include "nsPrintfCString.h"
#include "nsVariant.h"
#include "TelemetryCommon.h"
#include "TelemetryScalarData.h"

using mozilla::Nothing;
using mozilla::Preferences;
using mozilla::Some;
using mozilla::StaticAutoPtr;
using mozilla::StaticMutex;
using mozilla::StaticMutexAutoLock;
using mozilla::UniquePtr;
using mozilla::Telemetry::DynamicScalarDefinition;
using mozilla::Telemetry::KeyedScalarAction;
using mozilla::Telemetry::ProcessID;
using mozilla::Telemetry::ScalarAction;
using mozilla::Telemetry::ScalarActionType;
using mozilla::Telemetry::ScalarID;
using mozilla::Telemetry::ScalarVariant;
using mozilla::Telemetry::Common::AutoHashtable;
using mozilla::Telemetry::Common::CanRecordDataset;
using mozilla::Telemetry::Common::CanRecordProduct;
using mozilla::Telemetry::Common::GetCurrentProduct;
using mozilla::Telemetry::Common::GetIDForProcessName;
using mozilla::Telemetry::Common::GetNameForProcessID;
using mozilla::Telemetry::Common::IsExpiredVersion;
using mozilla::Telemetry::Common::IsInDataset;
using mozilla::Telemetry::Common::IsValidIdentifierString;
using mozilla::Telemetry::Common::LogToBrowserConsole;
using mozilla::Telemetry::Common::RecordedProcessType;
using mozilla::Telemetry::Common::StringHashSet;
using mozilla::Telemetry::Common::SupportedProduct;

namespace TelemetryIPCAccumulator = mozilla::TelemetryIPCAccumulator;

namespace geckoprofiler::markers {

struct ScalarMarker : public mozilla::BaseMarkerType<ScalarMarker> {
  static constexpr const char* Name = "Scalar";
  // Only the name tells "Scalar::Set" from "Scalar::Add", and either of them
  // from their "ChildScalar::" counterparts.
  static constexpr bool ETWStoreName = true;
  using MS = mozilla::MarkerSchema;
  static constexpr MS::Location Locations[] = {
      MS::Location::MarkerChart,
      MS::Location::MarkerTable,
  };
  static constexpr MS::PayloadField PayloadFields[] = {
      {"id", MS::InputType::CString, "Scalar Name", MS::Format::UniqueString},
      {"key", MS::InputType::CString, "Key", MS::Format::String},
      {"scalarType", MS::InputType::CString, "Type", MS::Format::UniqueString},
      // "val" is a uint, a string or a bool depending on the scalar's type, so
      // this InputType should be CString for ETW to carry all of them. That
      // isn't possible yet, see bug 2072225; in the meantime use Uint64, which
      // is only meaningful for uint and bool scalars.
      {"val", MS::InputType::Uint64, "Value", MS::Format::String},
  };
  static constexpr const char* TooltipLabel =
      "{marker.data.id}[{marker.data.key}] {marker.data.val}";
  static constexpr const char* TableLabel =
      "{marker.data.id}[{marker.data.key}]: "
      "{marker.data.val}";
  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const mozilla::ProfilerString8View& aName, const uint32_t& aKind,
      const nsCString& aKey, const ScalarVariant& aValue) {
    aWriter.UniqueStringProperty("id", aName);
    if (!aKey.IsEmpty()) {
      aWriter.StringProperty("key", mozilla::MakeStringSpan(aKey.get()));
    }
    aWriter.UniqueStringProperty("scalarType", ScalarTypeToString(aKind));
    if (aKind == nsITelemetry::SCALAR_TYPE_COUNT) {
      aWriter.IntProperty("val", aValue.as<uint32_t>());
    } else if (aKind == nsITelemetry::SCALAR_TYPE_STRING) {
      aWriter.StringProperty(
          "val", mozilla::MakeStringSpan(
                     NS_ConvertUTF16toUTF8(aValue.as<nsString>()).get()));
    } else {
      aWriter.BoolProperty("val", aValue.as<bool>());
    }
  }

  static void TranslateMarkerInputToSchema(
      void* aContext, const mozilla::ProfilerString8View& aName,
      const uint32_t& aKind, const nsCString& aKey,
      const ScalarVariant& aValue) {
    ETW::OutputMarkerSchema(
        aContext, ScalarMarker{}, aName, mozilla::ProfilerString8View(aKey),
        ScalarTypeToString(aKind), ScalarValueToUint64(aKind, aValue));
  }

 private:
  static mozilla::ProfilerString8View ScalarTypeToString(uint32_t aKind) {
    if (aKind == nsITelemetry::SCALAR_TYPE_COUNT) {
      return "uint";
    }
    if (aKind == nsITelemetry::SCALAR_TYPE_STRING) {
      return "string";
    }
    return "bool";
  }

  static uint64_t ScalarValueToUint64(uint32_t aKind,
                                      const ScalarVariant& aValue) {
    if (aKind == nsITelemetry::SCALAR_TYPE_COUNT) {
      return aValue.as<uint32_t>();
    }
    if (aKind == nsITelemetry::SCALAR_TYPE_STRING) {
      return 0;
    }
    return aValue.as<bool>() ? 1 : 0;
  }
};

}  // namespace geckoprofiler::markers

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// Naming: there are two kinds of functions in this file:
//
// * Functions named internal_*: these can only be reached via an
//   interface function (TelemetryScalar::*). If they access shared
//   state, they require the interface function to have acquired
//   |gTelemetryScalarMutex| to ensure thread safety.
//
// * Functions named TelemetryScalar::*. This is the external interface.
//   Entries and exits to these functions are serialised using
//   |gTelemetryScalarsMutex|.
//
// Avoiding races and deadlocks:
//
// All functions in the external interface (TelemetryScalar::*) are
// serialised using the mutex |gTelemetryScalarsMutex|. This means
// that the external interface is thread-safe. But it also brings
// a danger of deadlock if any function in the external interface can
// get back to that interface. That is, we will deadlock on any call
// chain like this
//
// TelemetryScalar::* -> .. any functions .. -> TelemetryScalar::*
//
// To reduce the danger of that happening, observe the following rules:
//
// * No function in TelemetryScalar::* may directly call, nor take the
//   address of, any other function in TelemetryScalar::*.
//
// * No internal function internal_* may call, nor take the address
//   of, any function in TelemetryScalar::*.

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE TYPES

namespace {

const uint32_t kMaximumNumberOfKeys = 100;
const uint32_t kMaximumKeyStringLength = 72;
const uint32_t kMaximumStringValueLength = 50;
// The category and scalar name maximum lengths are used by the dynamic
// scalar registration function and must match the constants used by
// the 'parse_scalars.py' script for static scalars.
const uint32_t kMaximumCategoryNameLength = 40;
const uint32_t kMaximumScalarNameLength = 40;
const uint32_t kScalarCount =
    static_cast<uint32_t>(mozilla::Telemetry::ScalarID::ScalarCount);

const char* TEST_SCALAR_PREFIX = "telemetry.test.";

// The max offset supported by gScalarStoresTable for static scalars' stores.
// Also the sentinel value (with store_count == 0) for just the sole "main"
// store.
const uint32_t kMaxStaticStoreOffset = UINT16_MAX;

enum class ScalarResult : uint8_t {
  // Nothing went wrong.
  Ok,
  // General Scalar Errors
  NotInitialized,
  CannotUnpackVariant,
  CannotRecordInProcess,
  CannotRecordDataset,
  KeyedTypeMismatch,
  UnknownScalar,
  OperationNotSupported,
  InvalidType,
  InvalidValue,
  // Keyed Scalar Errors
  KeyIsEmpty,
  KeyTooLong,
  TooManyKeys,
  KeyNotAllowed,
  // String Scalar Errors
  StringTooLong,
  // Unsigned Scalar Errors
  UnsignedNegativeValue,
  UnsignedTruncatedValue,
};

// A common identifier for both built-in and dynamic scalars.
struct ScalarKey {
  uint32_t id;
  bool dynamic;
};

// Dynamic scalar store names.
StaticAutoPtr<nsTArray<RefPtr<nsAtom>>> gDynamicStoreNames;

/**
 * Scalar information for dynamic definitions.
 */
struct DynamicScalarInfo : BaseScalarInfo {
  nsCString mDynamicName;
  bool mDynamicExpiration;
  uint32_t store_count;
  uint32_t store_offset;

  DynamicScalarInfo(uint32_t aKind, bool aRecordOnRelease, bool aExpired,
                    const nsACString& aName, bool aKeyed,
                    const nsTArray<nsCString>& aStores)
      : BaseScalarInfo(
            aKind,
            aRecordOnRelease ? nsITelemetry::DATASET_ALL_CHANNELS
                             : nsITelemetry::DATASET_PRERELEASE_CHANNELS,
            RecordedProcessType::All, aKeyed, 0, 0, GetCurrentProduct()),
        mDynamicName(aName),
        mDynamicExpiration(aExpired) {
    store_count = aStores.Length();
    if (store_count == 0) {
      store_count = 1;
      store_offset = kMaxStaticStoreOffset;
    } else {
      store_offset = kMaxStaticStoreOffset + 1 + gDynamicStoreNames->Length();
      for (const auto& storeName : aStores) {
        gDynamicStoreNames->AppendElement(NS_Atomize(storeName));
      }
      MOZ_ASSERT(
          gDynamicStoreNames->Length() < UINT32_MAX - kMaxStaticStoreOffset - 1,
          "Too many dynamic scalar store names. Overflow.");
    }
  };

  // The following functions will read the stored text
  // instead of looking it up in the statically generated
  // tables.
  const char* name() const override;
  const char* expiration() const override;

  uint32_t storeCount() const override;
  uint32_t storeOffset() const override;
};

const char* DynamicScalarInfo::name() const { return mDynamicName.get(); }

const char* DynamicScalarInfo::expiration() const {
  // Dynamic scalars can either be expired or not (boolean flag).
  // Return an appropriate version string to leverage the scalar expiration
  // logic.
  return mDynamicExpiration ? "1.0" : "never";
}

uint32_t DynamicScalarInfo::storeOffset() const { return store_offset; }
uint32_t DynamicScalarInfo::storeCount() const { return store_count; }

typedef nsBaseHashtableET<nsDepCharHashKey, ScalarKey> CharPtrEntryType;
typedef AutoHashtable<CharPtrEntryType> ScalarMapType;

// Dynamic scalar definitions.
StaticAutoPtr<nsTArray<DynamicScalarInfo>> gDynamicScalarInfo;

const BaseScalarInfo& internal_GetScalarInfo(const StaticMutexAutoLock& lock,
                                             const ScalarKey& aId) {
  if (!aId.dynamic) {
    return gScalars[aId.id];
  }

  return (*gDynamicScalarInfo)[aId.id];
}

bool IsValidEnumId(mozilla::Telemetry::ScalarID aID) {
  return aID < mozilla::Telemetry::ScalarID::ScalarCount;
}

bool internal_IsValidId(const StaticMutexAutoLock& lock, const ScalarKey& aId) {
  // Please note that this function needs to be called with the scalar
  // mutex being acquired: other functions might be messing with
  // |gDynamicScalarInfo|.
  return aId.dynamic
             ? (aId.id < gDynamicScalarInfo->Length())
             : IsValidEnumId(static_cast<mozilla::Telemetry::ScalarID>(aId.id));
}

// Implements the methods for ScalarInfo.
const char* ScalarInfo::name() const {
  return &gScalarsStringTable[this->name_offset];
}

const char* ScalarInfo::expiration() const {
  return &gScalarsStringTable[this->expiration_offset];
}

/**
 * The base scalar object, that serves as a common ancestor for storage
 * purposes.
 */
class ScalarBase {
 public:
  explicit ScalarBase(const BaseScalarInfo& aInfo)
      : mStoreCount(aInfo.storeCount()),
        mStoreOffset(aInfo.storeOffset()),
        mStoreHasValue(mStoreCount),
        mName(aInfo.name()) {
    mStoreHasValue.SetLength(mStoreCount);
    for (auto& val : mStoreHasValue) {
      val = false;
    }
  };
  virtual ~ScalarBase() = default;

  // Convenience methods used by the C++ API.
  virtual void SetValue(uint32_t aValue) { (void)HandleUnsupported(); }
  virtual ScalarResult SetValue(const nsAString& aValue) {
    return HandleUnsupported();
  }
  virtual void SetValue(bool aValue) { (void)HandleUnsupported(); }
  virtual void AddValue(uint32_t aValue) { (void)HandleUnsupported(); }

  // GetValue is used to get the value of the scalar when persisting it to JS.
  virtual nsresult GetValue(const nsACString& aStoreName, bool aClearStore,
                            nsCOMPtr<nsIVariant>& aResult) = 0;

  // To measure the memory stats.
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  virtual size_t SizeOfIncludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const = 0;

 protected:
  bool HasValueInStore(size_t aStoreIndex) const;
  void ClearValueInStore(size_t aStoreIndex);
  void SetValueInStores();
  nsresult StoreIndex(const nsACString& aStoreName, size_t* aStoreIndex) const;

 private:
  ScalarResult HandleUnsupported() const;

  const uint32_t mStoreCount;
  const uint32_t mStoreOffset;
  nsTArray<bool> mStoreHasValue;

 protected:
  const nsCString mName;
};

ScalarResult ScalarBase::HandleUnsupported() const {
  MOZ_ASSERT(false, "This operation is not support for this scalar type.");
  return ScalarResult::OperationNotSupported;
}

bool ScalarBase::HasValueInStore(size_t aStoreIndex) const {
  MOZ_ASSERT(aStoreIndex < mStoreHasValue.Length(),
             "Invalid scalar store index.");
  return mStoreHasValue[aStoreIndex];
}

void ScalarBase::ClearValueInStore(size_t aStoreIndex) {
  MOZ_ASSERT(aStoreIndex < mStoreHasValue.Length(),
             "Invalid scalar store index to clear.");
  mStoreHasValue[aStoreIndex] = false;
}

void ScalarBase::SetValueInStores() {
  for (auto& val : mStoreHasValue) {
    val = true;
  }
}

nsresult ScalarBase::StoreIndex(const nsACString& aStoreName,
                                size_t* aStoreIndex) const {
  if (mStoreCount == 1 && mStoreOffset == kMaxStaticStoreOffset) {
    // This Scalar is only in the "main" store.
    if (aStoreName.EqualsLiteral("main")) {
      *aStoreIndex = 0;
      return NS_OK;
    }
    return NS_ERROR_NO_CONTENT;
  }

  // Multiple stores. Linear scan to find one that matches aStoreName.
  // Dynamic Scalars start at kMaxStaticStoreOffset + 1
  if (mStoreOffset > kMaxStaticStoreOffset) {
    auto dynamicOffset = mStoreOffset - kMaxStaticStoreOffset - 1;
    for (uint32_t i = 0; i < mStoreCount; ++i) {
      auto scalarStore = (*gDynamicStoreNames)[dynamicOffset + i];
      if (nsAtomCString(scalarStore).Equals(aStoreName)) {
        *aStoreIndex = i;
        return NS_OK;
      }
    }
    return NS_ERROR_NO_CONTENT;
  }

  // Static Scalars are similar.
  for (uint32_t i = 0; i < mStoreCount; ++i) {
    uint32_t stringIndex = gScalarStoresTable[mStoreOffset + i];
    if (aStoreName.EqualsASCII(&gScalarsStringTable[stringIndex])) {
      *aStoreIndex = i;
      return NS_OK;
    }
  }
  return NS_ERROR_NO_CONTENT;
}

size_t ScalarBase::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return mStoreHasValue.ShallowSizeOfExcludingThis(aMallocSizeOf);
}

/**
 * The implementation for the unsigned int scalar type.
 */
class ScalarUnsigned : public ScalarBase {
 public:
  using ScalarBase::SetValue;

  explicit ScalarUnsigned(const BaseScalarInfo& aInfo)
      : ScalarBase(aInfo), mStorage(aInfo.storeCount()) {
    mStorage.SetLength(aInfo.storeCount());
    for (auto& val : mStorage) {
      val = 0;
    }
  };

  ~ScalarUnsigned() override = default;

  void SetValue(uint32_t aValue) final;
  void AddValue(uint32_t aValue) final;
  nsresult GetValue(const nsACString& aStoreName, bool aClearStore,
                    nsCOMPtr<nsIVariant>& aResult) final;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const final;

  // Prevent copying.
  ScalarUnsigned(const ScalarUnsigned& aOther) = delete;
  void operator=(const ScalarUnsigned& aOther) = delete;

 private:
  nsTArray<uint32_t> mStorage;
};

void ScalarUnsigned::SetValue(uint32_t aValue) {
  for (auto& val : mStorage) {
    val = aValue;
  }
  SetValueInStores();
}

void ScalarUnsigned::AddValue(uint32_t aValue) {
  for (auto& val : mStorage) {
    val += aValue;
  }
  SetValueInStores();
}

nsresult ScalarUnsigned::GetValue(const nsACString& aStoreName,
                                  bool aClearStore,
                                  nsCOMPtr<nsIVariant>& aResult) {
  size_t storeIndex = 0;
  nsresult rv = StoreIndex(aStoreName, &storeIndex);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!HasValueInStore(storeIndex)) {
    return NS_ERROR_NO_CONTENT;
  }
  nsCOMPtr<nsIWritableVariant> outVar(new nsVariant());
  rv = outVar->SetAsUint32(mStorage[storeIndex]);
  if (NS_FAILED(rv)) {
    return rv;
  }
  aResult = std::move(outVar);
  if (aClearStore) {
    mStorage[storeIndex] = 0;
    ClearValueInStore(storeIndex);
  }
  return NS_OK;
}

size_t ScalarUnsigned::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += ScalarBase::SizeOfExcludingThis(aMallocSizeOf);
  n += mStorage.ShallowSizeOfExcludingThis(aMallocSizeOf);
  return n;
}

/**
 * The implementation for the string scalar type.
 */
class ScalarString : public ScalarBase {
 public:
  using ScalarBase::SetValue;

  explicit ScalarString(const BaseScalarInfo& aInfo)
      : ScalarBase(aInfo), mStorage(aInfo.storeCount()) {
    mStorage.SetLength(aInfo.storeCount());
  };

  ~ScalarString() override = default;

  ScalarResult SetValue(const nsAString& aValue) final;
  nsresult GetValue(const nsACString& aStoreName, bool aClearStore,
                    nsCOMPtr<nsIVariant>& aResult) final;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const final;

  // Prevent copying.
  ScalarString(const ScalarString& aOther) = delete;
  void operator=(const ScalarString& aOther) = delete;

 private:
  nsTArray<nsString> mStorage;
};

ScalarResult ScalarString::SetValue(const nsAString& aValue) {
  auto str = Substring(aValue, 0, kMaximumStringValueLength);
  for (auto& val : mStorage) {
    val.Assign(str);
  }
  SetValueInStores();
  if (aValue.Length() > kMaximumStringValueLength) {
    return ScalarResult::StringTooLong;
  }
  return ScalarResult::Ok;
}

nsresult ScalarString::GetValue(const nsACString& aStoreName, bool aClearStore,
                                nsCOMPtr<nsIVariant>& aResult) {
  nsCOMPtr<nsIWritableVariant> outVar(new nsVariant());
  size_t storeIndex = 0;
  nsresult rv = StoreIndex(aStoreName, &storeIndex);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!HasValueInStore(storeIndex)) {
    return NS_ERROR_NO_CONTENT;
  }
  rv = outVar->SetAsAString(mStorage[storeIndex]);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (aClearStore) {
    ClearValueInStore(storeIndex);
  }
  aResult = std::move(outVar);
  return NS_OK;
}

size_t ScalarString::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += ScalarBase::SizeOfExcludingThis(aMallocSizeOf);
  n += mStorage.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (auto& val : mStorage) {
    n += val.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }
  return n;
}

/**
 * The implementation for the boolean scalar type.
 */
class ScalarBoolean : public ScalarBase {
 public:
  using ScalarBase::SetValue;

  explicit ScalarBoolean(const BaseScalarInfo& aInfo)
      : ScalarBase(aInfo), mStorage(aInfo.storeCount()) {
    mStorage.SetLength(aInfo.storeCount());
    for (auto& val : mStorage) {
      val = false;
    }
  };

  ~ScalarBoolean() override = default;

  void SetValue(bool aValue) final;
  nsresult GetValue(const nsACString& aStoreName, bool aClearStore,
                    nsCOMPtr<nsIVariant>& aResult) final;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const final;

  // Prevent copying.
  ScalarBoolean(const ScalarBoolean& aOther) = delete;
  void operator=(const ScalarBoolean& aOther) = delete;

 private:
  nsTArray<bool> mStorage;
};

void ScalarBoolean::SetValue(bool aValue) {
  for (auto& val : mStorage) {
    val = aValue;
  }
  SetValueInStores();
}

nsresult ScalarBoolean::GetValue(const nsACString& aStoreName, bool aClearStore,
                                 nsCOMPtr<nsIVariant>& aResult) {
  nsCOMPtr<nsIWritableVariant> outVar(new nsVariant());
  size_t storeIndex = 0;
  nsresult rv = StoreIndex(aStoreName, &storeIndex);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!HasValueInStore(storeIndex)) {
    return NS_ERROR_NO_CONTENT;
  }
  if (aClearStore) {
    ClearValueInStore(storeIndex);
  }
  rv = outVar->SetAsBool(mStorage[storeIndex]);
  if (NS_FAILED(rv)) {
    return rv;
  }
  aResult = std::move(outVar);
  return NS_OK;
}

size_t ScalarBoolean::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += ScalarBase::SizeOfExcludingThis(aMallocSizeOf);
  n += mStorage.ShallowSizeOfExcludingThis(aMallocSizeOf);
  return n;
}

/**
 * Allocate a scalar class given the scalar info.
 *
 * @param aInfo The informations for the scalar coming from the definition file.
 * @return nullptr if the scalar type is unknown, otherwise a valid pointer to
 * the scalar type.
 */
ScalarBase* internal_ScalarAllocate(const BaseScalarInfo& aInfo) {
  ScalarBase* scalar = nullptr;
  switch (aInfo.kind) {
    case nsITelemetry::SCALAR_TYPE_COUNT:
      scalar = new ScalarUnsigned(aInfo);
      break;
    case nsITelemetry::SCALAR_TYPE_STRING:
      scalar = new ScalarString(aInfo);
      break;
    case nsITelemetry::SCALAR_TYPE_BOOLEAN:
      scalar = new ScalarBoolean(aInfo);
      break;
    default:
      MOZ_ASSERT(false, "Invalid scalar type");
  }
  return scalar;
}

/**
 * The implementation for the keyed scalar type.
 */
class KeyedScalar {
 public:
  typedef std::pair<nsCString, nsCOMPtr<nsIVariant>> KeyValuePair;

  // We store the name instead of a reference to the BaseScalarInfo because
  // the BaseScalarInfo can move if it's from a dynamic scalar.
  explicit KeyedScalar(const BaseScalarInfo& info)
      : mScalarName(info.name()),
        mScalarKeyCount(info.key_count),
        mScalarKeyOffset(info.key_offset),
        mMaximumNumberOfKeys(kMaximumNumberOfKeys) {};
  ~KeyedScalar() = default;

  // Convenience methods used by the C++ API.
  void SetValue(const StaticMutexAutoLock& locker, const nsAString& aKey,
                uint32_t aValue);
  void SetValue(const StaticMutexAutoLock& locker, const nsAString& aKey,
                bool aValue);
  void AddValue(const StaticMutexAutoLock& locker, const nsAString& aKey,
                uint32_t aValue);

  // GetValue is used to get the key-value pairs stored in the keyed scalar
  // when persisting it to JS.
  nsresult GetValue(const nsACString& aStoreName, bool aClearStorage,
                    nsTArray<KeyValuePair>& aValues);

  // To measure the memory stats.
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

  // To permit more keys than normal.
  void SetMaximumNumberOfKeys(uint32_t aMaximumNumberOfKeys) {
    mMaximumNumberOfKeys = aMaximumNumberOfKeys;
  };

 private:
  typedef nsClassHashtable<nsCStringHashKey, ScalarBase> ScalarKeysMapType;

  const nsCString mScalarName;
  ScalarKeysMapType mScalarKeys;
  uint32_t mScalarKeyCount;
  uint32_t mScalarKeyOffset;
  uint32_t mMaximumNumberOfKeys;

  ScalarResult GetScalarForKey(const StaticMutexAutoLock& locker,
                               const nsAString& aKey, ScalarBase** aRet);

  bool AllowsKey(const nsAString& aKey) const;
};

void KeyedScalar::SetValue(const StaticMutexAutoLock& locker,
                           const nsAString& aKey, uint32_t aValue) {
  ScalarBase* scalar = nullptr;
  ScalarResult sr = GetScalarForKey(locker, aKey, &scalar);

  if (sr != ScalarResult::Ok) {
    // Bug 1451813 - We now report which scalars exceed the key limit in
    // telemetry.keyed_scalars_exceed_limit.
    return;
  }

  return scalar->SetValue(aValue);
}

void KeyedScalar::SetValue(const StaticMutexAutoLock& locker,
                           const nsAString& aKey, bool aValue) {
  ScalarBase* scalar = nullptr;
  ScalarResult sr = GetScalarForKey(locker, aKey, &scalar);

  if (sr != ScalarResult::Ok) {
    // Bug 1451813 - We now report which scalars exceed the key limit in
    // telemetry.keyed_scalars_exceed_limit.
    return;
  }

  return scalar->SetValue(aValue);
}

void KeyedScalar::AddValue(const StaticMutexAutoLock& locker,
                           const nsAString& aKey, uint32_t aValue) {
  ScalarBase* scalar = nullptr;
  ScalarResult sr = GetScalarForKey(locker, aKey, &scalar);

  if (sr != ScalarResult::Ok) {
    // Bug 1451813 - We now report which scalars exceed the key limit in
    // telemetry.keyed_scalars_exceed_limit.
    return;
  }

  return scalar->AddValue(aValue);
}

/**
 * Get a key-value array with the values for the Keyed Scalar.
 * @param aValue The array that will hold the key-value pairs.
 * @return {nsresult} NS_OK or an error value as reported by the
 *         the specific scalar objects implementations (e.g.
 *         ScalarUnsigned).
 */
nsresult KeyedScalar::GetValue(const nsACString& aStoreName, bool aClearStorage,
                               nsTArray<KeyValuePair>& aValues) {
  for (const auto& entry : mScalarKeys) {
    ScalarBase* scalar = entry.GetRawPtr();

    // Get the scalar value.
    nsCOMPtr<nsIVariant> scalarValue;
    nsresult rv = scalar->GetValue(aStoreName, aClearStorage, scalarValue);
    if (rv == NS_ERROR_NO_CONTENT) {
      // No value for this store.
      continue;
    }
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Append it to value list.
    aValues.AppendElement(
        std::make_pair(nsCString(entry.GetKey()), scalarValue));
  }

  return NS_OK;
}

// Forward declaration
nsresult internal_GetKeyedScalarByEnum(const StaticMutexAutoLock& lock,
                                       const ScalarKey& aId,
                                       ProcessID aProcessStorage,
                                       KeyedScalar** aRet);

// Forward declaration
nsresult internal_GetEnumByScalarName(const StaticMutexAutoLock& lock,
                                      const nsACString& aName, ScalarKey* aId);

/**
 * Get the scalar for the referenced key.
 * If there's no such key, instantiate a new Scalar object with the
 * same type of the Keyed scalar and create the key.
 */
ScalarResult KeyedScalar::GetScalarForKey(const StaticMutexAutoLock& locker,
                                          const nsAString& aKey,
                                          ScalarBase** aRet) {
  if (aKey.IsEmpty()) {
    return ScalarResult::KeyIsEmpty;
  }

  if (!AllowsKey(aKey)) {
    KeyedScalar* scalarUnknown = nullptr;
    ScalarKey scalarUnknownUniqueId{
        static_cast<uint32_t>(
            mozilla::Telemetry::ScalarID::TELEMETRY_KEYED_SCALARS_UNKNOWN_KEYS),
        false};
    ProcessID process = ProcessID::Parent;
    nsresult rv = internal_GetKeyedScalarByEnum(locker, scalarUnknownUniqueId,
                                                process, &scalarUnknown);
    if (NS_FAILED(rv)) {
      return ScalarResult::TooManyKeys;
    }
    scalarUnknown->AddValue(locker, NS_ConvertUTF8toUTF16(mScalarName), 1);

    return ScalarResult::KeyNotAllowed;
  }

  if (aKey.Length() > kMaximumKeyStringLength) {
    return ScalarResult::KeyTooLong;
  }

  NS_ConvertUTF16toUTF8 utf8Key(aKey);

  ScalarBase* scalar = nullptr;
  if (mScalarKeys.Get(utf8Key, &scalar)) {
    *aRet = scalar;
    return ScalarResult::Ok;
  }

  ScalarKey uniqueId;
  nsresult rv = internal_GetEnumByScalarName(locker, mScalarName, &uniqueId);
  if (NS_FAILED(rv)) {
    return (rv == NS_ERROR_FAILURE) ? ScalarResult::NotInitialized
                                    : ScalarResult::UnknownScalar;
  }

  const BaseScalarInfo& info = internal_GetScalarInfo(locker, uniqueId);
  if (mScalarKeys.Count() >= mMaximumNumberOfKeys) {
    if (aKey.EqualsLiteral("telemetry.keyed_scalars_exceed_limit")) {
      return ScalarResult::TooManyKeys;
    }

    KeyedScalar* scalarExceed = nullptr;

    ScalarKey uniqueId{
        static_cast<uint32_t>(
            mozilla::Telemetry::ScalarID::TELEMETRY_KEYED_SCALARS_EXCEED_LIMIT),
        false};

    ProcessID process = ProcessID::Parent;
    nsresult rv =
        internal_GetKeyedScalarByEnum(locker, uniqueId, process, &scalarExceed);

    if (NS_FAILED(rv)) {
      return ScalarResult::TooManyKeys;
    }

    scalarExceed->AddValue(locker, NS_ConvertUTF8toUTF16(info.name()), 1);

    return ScalarResult::TooManyKeys;
  }

  scalar = internal_ScalarAllocate(info);
  if (!scalar) {
    return ScalarResult::InvalidType;
  }

  mScalarKeys.InsertOrUpdate(utf8Key, UniquePtr<ScalarBase>(scalar));

  *aRet = scalar;
  return ScalarResult::Ok;
}

size_t KeyedScalar::SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
  size_t n = aMallocSizeOf(this);
  for (const auto& scalar : mScalarKeys.Values()) {
    n += scalar->SizeOfIncludingThis(aMallocSizeOf);
  }
  return n;
}

bool KeyedScalar::AllowsKey(const nsAString& aKey) const {
  // If we didn't specify a list of allowed keys, just return true.
  if (mScalarKeyCount == 0) {
    return true;
  }

  for (uint32_t i = 0; i < mScalarKeyCount; ++i) {
    uint32_t stringIndex = gScalarKeysTable[mScalarKeyOffset + i];
    if (aKey.EqualsASCII(&gScalarsStringTable[stringIndex])) {
      return true;
    }
  }

  return false;
}

typedef nsUint32HashKey ScalarIDHashKey;
typedef nsUint32HashKey ProcessIDHashKey;
typedef nsClassHashtable<ScalarIDHashKey, ScalarBase> ScalarStorageMapType;
typedef nsClassHashtable<ScalarIDHashKey, KeyedScalar>
    KeyedScalarStorageMapType;
typedef nsClassHashtable<ProcessIDHashKey, ScalarStorageMapType>
    ProcessesScalarsMapType;
typedef nsClassHashtable<ProcessIDHashKey, KeyedScalarStorageMapType>
    ProcessesKeyedScalarsMapType;

typedef std::tuple<const char*, nsCOMPtr<nsIVariant>, uint32_t> ScalarDataTuple;
typedef nsTArray<ScalarDataTuple> ScalarTupleArray;
typedef nsTHashMap<ProcessIDHashKey, ScalarTupleArray> ScalarSnapshotTable;

typedef std::tuple<const char*, nsTArray<KeyedScalar::KeyValuePair>, uint32_t>
    KeyedScalarDataTuple;
typedef nsTArray<KeyedScalarDataTuple> KeyedScalarTupleArray;
typedef nsTHashMap<ProcessIDHashKey, KeyedScalarTupleArray>
    KeyedScalarSnapshotTable;

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE STATE, SHARED BY ALL THREADS

namespace {

// Set to true once this global state has been initialized.
bool gTelemetryScalarInitDone = false;

bool gTelemetryScalarCanRecordBase;
bool gTelemetryScalarCanRecordExtended;

// The Name -> ID cache map.
MOZ_RUNINIT ScalarMapType gScalarNameIDMap(kScalarCount);

// The (Process Id -> (Scalar ID -> Scalar Object)) map. This is a
// nsClassHashtable, it owns the scalar instances and takes care of deallocating
// them when they are removed from the map.
constinit ProcessesScalarsMapType gScalarStorageMap;
// As above, for the keyed scalars.
constinit ProcessesKeyedScalarsMapType gKeyedScalarStorageMap;
// Provide separate storage for "dynamic builtin" plain and keyed scalars,
// needed to support "build faster" in local developer builds.
constinit ProcessesScalarsMapType gDynamicBuiltinScalarStorageMap;
constinit ProcessesKeyedScalarsMapType gDynamicBuiltinKeyedScalarStorageMap;
}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: helpers for the external interface

namespace {

bool internal_CanRecordBase(const StaticMutexAutoLock& lock) {
  return gTelemetryScalarCanRecordBase;
}

bool internal_CanRecordExtended(const StaticMutexAutoLock& lock) {
  return gTelemetryScalarCanRecordExtended;
}

/**
 * Check if the given scalar is a keyed scalar.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aId The scalar identifier.
 * @return true if aId refers to a keyed scalar, false otherwise.
 */
bool internal_IsKeyedScalar(const StaticMutexAutoLock& lock,
                            const ScalarKey& aId) {
  return internal_GetScalarInfo(lock, aId).keyed;
}

/**
 * Check if we're allowed to record the given scalar in the current
 * process.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aId The scalar identifier.
 * @return true if the scalar is allowed to be recorded in the current process,
 * false otherwise.
 */
bool internal_CanRecordProcess(const StaticMutexAutoLock& lock,
                               const ScalarKey& aId) {
  const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);
  return CanRecordInProcess(info.record_in_processes, XRE_GetProcessType());
}

bool internal_CanRecordProduct(const StaticMutexAutoLock& lock,
                               const ScalarKey& aId) {
  const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);
  return CanRecordProduct(info.products);
}

bool internal_CanRecordForScalarID(const StaticMutexAutoLock& lock,
                                   const ScalarKey& aId) {
  // Get the scalar info from the id.
  const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);

  // Can we record at all?
  bool canRecordBase = internal_CanRecordBase(lock);
  if (!canRecordBase) {
    return false;
  }

  bool canRecordDataset = CanRecordDataset(info.dataset, canRecordBase,
                                           internal_CanRecordExtended(lock));
  if (!canRecordDataset) {
    return false;
  }

  return true;
}

/**
 * Check if we are allowed to record the provided scalar.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aId The scalar identifier.
 * @param aKeyed Are we attempting to write a keyed scalar?
 * @param aForce Whether to allow recording even if the probe is not allowed on
 *        the current process.
 * @return ScalarResult::Ok if we can record, an error code otherwise.
 */
ScalarResult internal_CanRecordScalar(const StaticMutexAutoLock& lock,
                                      const ScalarKey& aId, bool aKeyed,
                                      bool aForce = false) {
  // Make sure that we have a keyed scalar if we are trying to change one.
  if (internal_IsKeyedScalar(lock, aId) != aKeyed) {
    const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);
    PROFILER_MARKER_TEXT(
        "ScalarError", TELEMETRY, mozilla::MarkerStack::Capture(),
        nsPrintfCString("KeyedTypeMismatch for %s", info.name()));
    return ScalarResult::KeyedTypeMismatch;
  }

  // Are we allowed to record this scalar based on the current Telemetry
  // settings?
  if (!internal_CanRecordForScalarID(lock, aId)) {
    return ScalarResult::CannotRecordDataset;
  }

  // Can we record in this process?
  if (!aForce && !internal_CanRecordProcess(lock, aId)) {
    const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);
    PROFILER_MARKER_TEXT(
        "ScalarError", TELEMETRY, mozilla::MarkerStack::Capture(),
        nsPrintfCString("CannotRecordInProcess for %s", info.name()));
    return ScalarResult::CannotRecordInProcess;
  }

  // Can we record on this product?
  if (!internal_CanRecordProduct(lock, aId)) {
    return ScalarResult::CannotRecordDataset;
  }

  return ScalarResult::Ok;
}

/**
 * Get the scalar enum id from the scalar name.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aName The scalar name.
 * @param aId The output variable to contain the enum.
 * @return
 *   NS_ERROR_FAILURE if this was called before init is completed.
 *   NS_ERROR_INVALID_ARG if the name can't be found in the scalar definitions.
 *   NS_OK if the scalar was found and aId contains a valid enum id.
 */
nsresult internal_GetEnumByScalarName(const StaticMutexAutoLock& lock,
                                      const nsACString& aName, ScalarKey* aId) {
  if (!gTelemetryScalarInitDone) {
    return NS_ERROR_FAILURE;
  }

  CharPtrEntryType* entry =
      gScalarNameIDMap.GetEntry(PromiseFlatCString(aName).get());
  if (!entry) {
    return NS_ERROR_INVALID_ARG;
  }
  *aId = entry->GetData();
  return NS_OK;
}

/**
 * Get a scalar object by its enum id. This implicitly allocates the scalar
 * object in the storage if it wasn't previously allocated.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aId The scalar identifier.
 * @param aProcessStorage This drives the selection of the map to use to store
 *        the scalar data coming from child processes. This is only meaningful
 *        when this function is called in parent process. If that's the case,
 *        if this is not |GeckoProcessType_Default|, the process id is used to
 *        allocate and store the scalars.
 * @param aRes The output variable that stores scalar object.
 * @return
 *   NS_ERROR_INVALID_ARG if the scalar id is unknown.
 *   NS_ERROR_NOT_AVAILABLE if the scalar is expired.
 *   NS_OK if the scalar was found. If that's the case, aResult contains a
 *   valid pointer to a scalar type.
 */
nsresult internal_GetScalarByEnum(const StaticMutexAutoLock& lock,
                                  const ScalarKey& aId,
                                  ProcessID aProcessStorage,
                                  ScalarBase** aRet) {
  if (!internal_IsValidId(lock, aId)) {
    MOZ_ASSERT(false, "Requested a scalar with an invalid id.");
    return NS_ERROR_INVALID_ARG;
  }

  const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);

  ScalarBase* scalar = nullptr;
  // Initialize the scalar storage to the parent storage. This will get
  // set to the child storage if needed.
  uint32_t storageId = static_cast<uint32_t>(aProcessStorage);

  // Put dynamic-builtin scalars (used to support "build faster") in a
  // separate storage.
  ProcessesScalarsMapType& processStorage =
      aId.dynamic ? gDynamicBuiltinScalarStorageMap : gScalarStorageMap;

  // Get the process-specific storage or create one if it's not
  // available.
  ScalarStorageMapType* const scalarStorage =
      processStorage.GetOrInsertNew(storageId);

  // Check if the scalar is already allocated in the parent or in the child
  // storage.
  if (scalarStorage->Get(aId.id, &scalar)) {
    // Dynamic scalars can expire at any time during the session (e.g. an
    // add-on was updated). Check if it expired.
    if (aId.dynamic) {
      const DynamicScalarInfo& dynInfo =
          static_cast<const DynamicScalarInfo&>(info);
      if (dynInfo.mDynamicExpiration) {
        // The Dynamic scalar is expired.
        PROFILER_MARKER_TEXT(
            "ScalarError", TELEMETRY, mozilla::MarkerStack::Capture(),
            nsPrintfCString("ExpiredDynamicScalar: %s", info.name()));
        return NS_ERROR_NOT_AVAILABLE;
      }
    }
    // This was not a dynamic scalar or was not expired.
    *aRet = scalar;
    return NS_OK;
  }

  // The scalar storage wasn't already allocated. Check if the scalar is expired
  // and then allocate the storage, if needed.
  if (IsExpiredVersion(info.expiration())) {
    PROFILER_MARKER_TEXT("ScalarError", TELEMETRY,
                         mozilla::MarkerStack::Capture(),
                         nsPrintfCString("ExpiredScalar: %s", info.name()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  scalar = internal_ScalarAllocate(info);
  if (!scalar) {
    return NS_ERROR_INVALID_ARG;
  }

  scalarStorage->InsertOrUpdate(aId.id, UniquePtr<ScalarBase>(scalar));
  *aRet = scalar;
  return NS_OK;
}

// For C++ consummers.
static void internal_profilerMarker_impl(
    const StaticMutexAutoLock& lock, ScalarActionType aType,
    const ScalarVariant& aValue, ScalarKey uniqueId,
    const nsAString& aKey = EmptyString()) {
  const BaseScalarInfo& info = internal_GetScalarInfo(lock, uniqueId);
  PROFILER_MARKER(
      aType == ScalarActionType::eSet
          ? mozilla::ProfilerString8View("Scalar::Set")
          : mozilla::ProfilerString8View("Scalar::Add"),
      TELEMETRY, {}, ScalarMarker,
      mozilla::ProfilerString8View::WrapNullTerminatedString(info.name()),
      info.kind, NS_ConvertUTF16toUTF8(aKey), aValue);
}

// For child process data coming from IPCs
static void internal_profilerMarker_impl(
    const StaticMutexAutoLock& lock, const ScalarAction& aAction,
    const nsCString& aKey = EmptyCString()) {
  ScalarKey uniqueId{aAction.mId, aAction.mDynamic};
  const BaseScalarInfo& info = internal_GetScalarInfo(lock, uniqueId);

  PROFILER_MARKER(
      aAction.mActionType == ScalarActionType::eSet
          ? mozilla::ProfilerString8View("ChildScalar::Set")
          : mozilla::ProfilerString8View("ChildScalar::Add"),
      TELEMETRY, {}, ScalarMarker,
      mozilla::ProfilerString8View::WrapNullTerminatedString(info.name()),
      info.kind, aKey, *aAction.mData);
}

#define internal_profilerMarker(...)                       \
  do {                                                     \
    if (profiler_thread_is_being_profiled_for_markers()) { \
      internal_profilerMarker_impl(__VA_ARGS__);           \
    }                                                      \
  } while (false)

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: thread-unsafe helpers for the keyed scalars

namespace {

/**
 * Get a keyed scalar object by its enum id. This implicitly allocates the keyed
 * scalar object in the storage if it wasn't previously allocated.
 *
 * @param lock Instance of a lock locking gTelemetryHistogramMutex
 * @param aId The scalar identifier.
 * @param aProcessStorage This drives the selection of the map to use to store
 *        the scalar data coming from child processes. This is only meaningful
 *        when this function is called in parent process. If that's the case,
 *        if this is not |GeckoProcessType_Default|, the process id is used to
 *        allocate and store the scalars.
 * @param aRet The output variable that stores scalar object.
 * @return
 *   NS_ERROR_INVALID_ARG if the scalar id is unknown or a this is a keyed
 *                        string scalar.
 *   NS_ERROR_NOT_AVAILABLE if the scalar is expired.
 *   NS_OK if the scalar was found. If that's the case, aResult contains a
 *   valid pointer to a scalar type.
 */
nsresult internal_GetKeyedScalarByEnum(const StaticMutexAutoLock& lock,
                                       const ScalarKey& aId,
                                       ProcessID aProcessStorage,
                                       KeyedScalar** aRet) {
  if (!internal_IsValidId(lock, aId)) {
    MOZ_ASSERT(false, "Requested a keyed scalar with an invalid id.");
    return NS_ERROR_INVALID_ARG;
  }

  const BaseScalarInfo& info = internal_GetScalarInfo(lock, aId);

  KeyedScalar* scalar = nullptr;
  // Initialize the scalar storage to the parent storage. This will get
  // set to the child storage if needed.
  uint32_t storageId = static_cast<uint32_t>(aProcessStorage);

  // Put dynamic-builtin scalars (used to support "build faster") in a
  // separate storage.
  ProcessesKeyedScalarsMapType& processStorage =
      aId.dynamic ? gDynamicBuiltinKeyedScalarStorageMap
                  : gKeyedScalarStorageMap;

  // Get the process-specific storage or create one if it's not
  // available.
  KeyedScalarStorageMapType* const scalarStorage =
      processStorage.GetOrInsertNew(storageId);

  if (scalarStorage->Get(aId.id, &scalar)) {
    *aRet = scalar;
    return NS_OK;
  }

  if (IsExpiredVersion(info.expiration())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // We don't currently support keyed string scalars. Disable them.
  if (info.kind == nsITelemetry::SCALAR_TYPE_STRING) {
    MOZ_ASSERT(false, "Keyed string scalars are not currently supported.");
    return NS_ERROR_INVALID_ARG;
  }

  scalar = new KeyedScalar(info);

  scalarStorage->InsertOrUpdate(aId.id, UniquePtr<KeyedScalar>(scalar));
  *aRet = scalar;
  return NS_OK;
}

/**
 * Helper function to convert an array of |DynamicScalarInfo|
 * to |DynamicScalarDefinition| used by the IPC calls.
 */
void internal_DynamicScalarToIPC(
    const StaticMutexAutoLock& lock,
    const nsTArray<DynamicScalarInfo>& aDynamicScalarInfos,
    nsTArray<DynamicScalarDefinition>& aIPCDefs) {
  for (auto& info : aDynamicScalarInfos) {
    DynamicScalarDefinition stubDefinition;
    stubDefinition.type = info.kind;
    stubDefinition.dataset = info.dataset;
    stubDefinition.expired = info.mDynamicExpiration;
    stubDefinition.keyed = info.keyed;
    stubDefinition.name = info.mDynamicName;
    aIPCDefs.AppendElement(stubDefinition);
  }
}

/**
 * Broadcasts the dynamic scalar definitions to all the other
 * content processes.
 */
void internal_BroadcastDefinitions(
    const nsTArray<DynamicScalarDefinition>& scalarDefs) {
  nsTArray<mozilla::dom::ContentParent*> parents;
  mozilla::dom::ContentParent::GetAll(parents);
  if (!parents.Length()) {
    return;
  }

  // Broadcast the definitions to the other content processes.
  for (auto parent : parents) {
    (void)parent->SendAddDynamicScalars(scalarDefs);
  }
}

void internal_RegisterScalars(const StaticMutexAutoLock& lock,
                              const nsTArray<DynamicScalarInfo>& scalarInfos) {
  // Register the new scalars.
  if (!gDynamicScalarInfo) {
    gDynamicScalarInfo = new nsTArray<DynamicScalarInfo>();
  }
  if (!gDynamicStoreNames) {
    gDynamicStoreNames = new nsTArray<RefPtr<nsAtom>>();
  }

  for (auto& scalarInfo : scalarInfos) {
    // Allow expiring scalars that were already registered.
    CharPtrEntryType* existingKey =
        gScalarNameIDMap.GetEntry(scalarInfo.name());
    if (existingKey) {
      continue;
    }

    gDynamicScalarInfo->AppendElement(scalarInfo);
    uint32_t scalarId = gDynamicScalarInfo->Length() - 1;
    CharPtrEntryType* entry = gScalarNameIDMap.PutEntry(scalarInfo.name());
    entry->SetData(ScalarKey{scalarId, true});
  }
}

/**
 * Creates a snapshot of the desired scalar storage.
 * @param {aLock} The proof of lock to access scalar data.
 * @param {aScalarsToReflect} The table that will contain the snapshot.
 * @param {aDataset} The dataset we're asking the snapshot for.
 * @param {aProcessStorage} The scalar storage to take a snapshot of.
 * @param {aIsBuiltinDynamic} Whether or not the storage is for dynamic builtin
 *                            scalars.
 * @return NS_OK or the error code describing the failure reason.
 */
nsresult internal_ScalarSnapshotter(const StaticMutexAutoLock& aLock,
                                    ScalarSnapshotTable& aScalarsToReflect,
                                    unsigned int aDataset,
                                    ProcessesScalarsMapType& aProcessStorage,
                                    bool aIsBuiltinDynamic, bool aClearScalars,
                                    const nsACString& aStoreName) {
  // Iterate the scalars in aProcessStorage. The storage may contain empty or
  // yet to be initialized scalars from all the supported processes.
  for (const auto& entry : aProcessStorage) {
    ScalarStorageMapType* scalarStorage = entry.GetRawPtr();
    ScalarTupleArray& processScalars =
        aScalarsToReflect.LookupOrInsert(entry.GetKey());

    // Are we in the "Dynamic" process?
    bool isDynamicProcess =
        ProcessID::Dynamic == static_cast<ProcessID>(entry.GetKey());

    // Iterate each available child storage.
    for (const auto& childEntry : *scalarStorage) {
      ScalarBase* scalar = childEntry.GetRawPtr();
      const ScalarKey key{childEntry.GetKey(),
                          aIsBuiltinDynamic || isDynamicProcess};
      // Get the informations for this scalar.
      const BaseScalarInfo& info = internal_GetScalarInfo(aLock, key);
      // Serialize the scalar if it's in the desired dataset.
      if (IsInDataset(info.dataset, aDataset)) {
        // Get the scalar value.
        nsCOMPtr<nsIVariant> scalarValue;
        nsresult rv = scalar->GetValue(aStoreName, aClearScalars, scalarValue);
        if (rv == NS_ERROR_NO_CONTENT) {
          // No value for this store. Proceed.
          continue;
        }
        if (NS_FAILED(rv)) {
          return rv;
        }
        // Append it to our list.
        processScalars.AppendElement(
            std::make_tuple(info.name(), scalarValue, info.kind));
      }
    }
    if (processScalars.Length() == 0) {
      aScalarsToReflect.Remove(entry.GetKey());
    }
  }
  return NS_OK;
}

/**
 * Creates a snapshot of the desired keyed scalar storage.
 * @param {aLock} The proof of lock to access scalar data.
 * @param {aScalarsToReflect} The table that will contain the snapshot.
 * @param {aDataset} The dataset we're asking the snapshot for.
 * @param {aProcessStorage} The scalar storage to take a snapshot of.
 * @param {aIsBuiltinDynamic} Whether or not the storage is for dynamic builtin
 *                            scalars.
 * @return NS_OK or the error code describing the failure reason.
 */
nsresult internal_KeyedScalarSnapshotter(
    const StaticMutexAutoLock& aLock,
    KeyedScalarSnapshotTable& aScalarsToReflect, unsigned int aDataset,
    ProcessesKeyedScalarsMapType& aProcessStorage, bool aIsBuiltinDynamic,
    bool aClearScalars, const nsACString& aStoreName) {
  // Iterate the scalars in aProcessStorage. The storage may contain empty or
  // yet to be initialized scalars from all the supported processes.
  for (const auto& entry : aProcessStorage) {
    KeyedScalarStorageMapType* scalarStorage = entry.GetRawPtr();
    KeyedScalarTupleArray& processScalars =
        aScalarsToReflect.LookupOrInsert(entry.GetKey());

    // Are we in the "Dynamic" process?
    bool isDynamicProcess =
        ProcessID::Dynamic == static_cast<ProcessID>(entry.GetKey());

    for (const auto& childEntry : *scalarStorage) {
      KeyedScalar* scalar = childEntry.GetRawPtr();
      const ScalarKey key{childEntry.GetKey(),
                          aIsBuiltinDynamic || isDynamicProcess};
      // Get the informations for this scalar.
      const BaseScalarInfo& info = internal_GetScalarInfo(aLock, key);

      // Serialize the scalar if it's in the desired dataset.
      if (IsInDataset(info.dataset, aDataset)) {
        // Get the keys for this scalar.
        nsTArray<KeyedScalar::KeyValuePair> scalarKeyedData;
        nsresult rv =
            scalar->GetValue(aStoreName, aClearScalars, scalarKeyedData);
        if (NS_FAILED(rv)) {
          return rv;
        }
        if (scalarKeyedData.Length() == 0) {
          // Don't bother with empty keyed scalars.
          continue;
        }
        // Append it to our list.
        processScalars.AppendElement(std::make_tuple(
            info.name(), std::move(scalarKeyedData), info.kind));
      }
    }
    if (processScalars.Length() == 0) {
      aScalarsToReflect.Remove(entry.GetKey());
    }
  }
  return NS_OK;
}

/**
 * Helper function to get a snapshot of the scalars.
 *
 * @param {aLock} The proof of lock to access scalar data.
 * @param {aScalarsToReflect} The table that will contain the snapshot.
 * @param {aDataset} The dataset we're asking the snapshot for.
 * @param {aClearScalars} Whether or not to clear the scalar storage.
 * @param {aStoreName} The name of the store to snapshot.
 * @return NS_OK or the error code describing the failure reason.
 */
nsresult internal_GetScalarSnapshot(const StaticMutexAutoLock& aLock,
                                    ScalarSnapshotTable& aScalarsToReflect,
                                    unsigned int aDataset, bool aClearScalars,
                                    const nsACString& aStoreName) {
  // Take a snapshot of the scalars.
  nsresult rv =
      internal_ScalarSnapshotter(aLock, aScalarsToReflect, aDataset,
                                 gScalarStorageMap, false, /*aIsBuiltinDynamic*/
                                 aClearScalars, aStoreName);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // And a snapshot of the dynamic builtin ones.
  rv = internal_ScalarSnapshotter(aLock, aScalarsToReflect, aDataset,
                                  gDynamicBuiltinScalarStorageMap,
                                  true, /*aIsBuiltinDynamic*/
                                  aClearScalars, aStoreName);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

/**
 * Helper function to get a snapshot of the keyed scalars.
 *
 * @param {aLock} The proof of lock to access scalar data.
 * @param {aScalarsToReflect} The table that will contain the snapshot.
 * @param {aDataset} The dataset we're asking the snapshot for.
 * @param {aClearScalars} Whether or not to clear the scalar storage.
 * @param {aStoreName} The name of the store to snapshot.
 * @return NS_OK or the error code describing the failure reason.
 */
nsresult internal_GetKeyedScalarSnapshot(
    const StaticMutexAutoLock& aLock,
    KeyedScalarSnapshotTable& aScalarsToReflect, unsigned int aDataset,
    bool aClearScalars, const nsACString& aStoreName) {
  // Take a snapshot of the scalars.
  nsresult rv = internal_KeyedScalarSnapshotter(
      aLock, aScalarsToReflect, aDataset, gKeyedScalarStorageMap,
      false, /*aIsBuiltinDynamic*/
      aClearScalars, aStoreName);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // And a snapshot of the dynamic builtin ones.
  rv = internal_KeyedScalarSnapshotter(aLock, aScalarsToReflect, aDataset,
                                       gDynamicBuiltinKeyedScalarStorageMap,
                                       true, /*aIsBuiltinDynamic*/
                                       aClearScalars, aStoreName);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

}  // namespace

// helpers for recording/applying scalar operations
namespace {

void internal_ApplyScalarActions(
    const StaticMutexAutoLock& lock,
    const nsTArray<mozilla::Telemetry::ScalarAction>& aScalarActions,
    const mozilla::Maybe<ProcessID>& aProcessType = Nothing()) {
  if (!internal_CanRecordBase(lock)) {
    return;
  }

  for (auto& upd : aScalarActions) {
    ScalarKey uniqueId{upd.mId, upd.mDynamic};
    if (NS_WARN_IF(!internal_IsValidId(lock, uniqueId))) {
      MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
      continue;
    }

    if (internal_IsKeyedScalar(lock, uniqueId)) {
      continue;
    }

    // Are we allowed to record this scalar? We don't need to check for
    // allowed processes here, that's taken care of when recording
    // in child processes.
    if (!internal_CanRecordForScalarID(lock, uniqueId)) {
      continue;
    }

    // Either we got passed a process type or it was explicitely set on the
    // recorded action. It should never happen that it is set to an invalid
    // value (such as ProcessID::Count)
    ProcessID processType = aProcessType.valueOr(upd.mProcessType);
    MOZ_ASSERT(processType != ProcessID::Count);

    // Refresh the data in the parent process with the data coming from the
    // child processes.
    ScalarBase* scalar = nullptr;
    nsresult rv =
        internal_GetScalarByEnum(lock, uniqueId, processType, &scalar);
    if (NS_FAILED(rv)) {
      // Bug 1513496 - We no longer log a warning if the scalar is expired.
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        NS_WARNING("NS_FAILED internal_GetScalarByEnum for CHILD");
      }
      continue;
    }

    if (upd.mData.isNothing()) {
      MOZ_ASSERT(false, "There is no data in the ScalarActionType.");
      continue;
    }

    internal_profilerMarker(lock, upd);

    // Get the type of this scalar from the scalar ID. We already checked
    // for its validity a few lines above.
    const uint32_t scalarType = internal_GetScalarInfo(lock, uniqueId).kind;

    // Extract the data from the mozilla::Variant.
    switch (upd.mActionType) {
      case ScalarActionType::eSet: {
        switch (scalarType) {
          case nsITelemetry::SCALAR_TYPE_COUNT:
            if (!upd.mData->is<uint32_t>()) {
              NS_WARNING("Attempting to set a count scalar to a non-integer.");
              continue;
            }
            scalar->SetValue(upd.mData->as<uint32_t>());
            break;
          case nsITelemetry::SCALAR_TYPE_BOOLEAN:
            if (!upd.mData->is<bool>()) {
              NS_WARNING(
                  "Attempting to set a boolean scalar to a non-boolean.");
              continue;
            }
            scalar->SetValue(upd.mData->as<bool>());
            break;
          case nsITelemetry::SCALAR_TYPE_STRING:
            if (!upd.mData->is<nsString>()) {
              NS_WARNING("Attempting to set a string scalar to a non-string.");
              continue;
            }
            scalar->SetValue(upd.mData->as<nsString>());
            break;
        }
        break;
      }
      case ScalarActionType::eAdd: {
        if (scalarType != nsITelemetry::SCALAR_TYPE_COUNT) {
          NS_WARNING("Attempting to add on a non count scalar.");
          continue;
        }
        // We only support adding uint32_t.
        if (!upd.mData->is<uint32_t>()) {
          NS_WARNING("Attempting to add to a count scalar with a non-integer.");
          continue;
        }
        scalar->AddValue(upd.mData->as<uint32_t>());
        break;
      }
      default:
        NS_WARNING("Unsupported action coming from scalar child updates.");
    }
  }
}

void internal_ApplyKeyedScalarActions(
    const StaticMutexAutoLock& lock,
    const nsTArray<mozilla::Telemetry::KeyedScalarAction>& aScalarActions,
    const mozilla::Maybe<ProcessID>& aProcessType = Nothing()) {
  if (!internal_CanRecordBase(lock)) {
    return;
  }

  for (auto& upd : aScalarActions) {
    ScalarKey uniqueId{upd.mId, upd.mDynamic};
    if (NS_WARN_IF(!internal_IsValidId(lock, uniqueId))) {
      MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
      continue;
    }

    if (!internal_IsKeyedScalar(lock, uniqueId)) {
      continue;
    }

    // Are we allowed to record this scalar? We don't need to check for
    // allowed processes here, that's taken care of when recording
    // in child processes.
    if (!internal_CanRecordForScalarID(lock, uniqueId)) {
      continue;
    }

    // Either we got passed a process type or it was explicitely set on the
    // recorded action. It should never happen that it is set to an invalid
    // value (such as ProcessID::Count)
    ProcessID processType = aProcessType.valueOr(upd.mProcessType);
    MOZ_ASSERT(processType != ProcessID::Count);

    // Refresh the data in the parent process with the data coming from the
    // child processes.
    KeyedScalar* scalar = nullptr;
    nsresult rv =
        internal_GetKeyedScalarByEnum(lock, uniqueId, processType, &scalar);
    if (NS_FAILED(rv)) {
      // Bug 1513496 - We no longer log a warning if the scalar is expired.
      if (rv != NS_ERROR_NOT_AVAILABLE) {
        NS_WARNING("NS_FAILED internal_GetKeyedScalarByEnum for CHILD");
      }
      continue;
    }

    if (upd.mData.isNothing()) {
      MOZ_ASSERT(false, "There is no data in the KeyedScalarAction.");
      continue;
    }

    // Get the type of this scalar from the scalar ID. We already checked
    // for its validity a few lines above.
    const uint32_t scalarType = internal_GetScalarInfo(lock, uniqueId).kind;

    internal_profilerMarker(lock, upd, upd.mKey);

    // Extract the data from the mozilla::Variant.
    switch (upd.mActionType) {
      case ScalarActionType::eSet: {
        switch (scalarType) {
          case nsITelemetry::SCALAR_TYPE_COUNT:
            if (!upd.mData->is<uint32_t>()) {
              NS_WARNING("Attempting to set a count scalar to a non-integer.");
              continue;
            }
            scalar->SetValue(lock, NS_ConvertUTF8toUTF16(upd.mKey),
                             upd.mData->as<uint32_t>());
            break;
          case nsITelemetry::SCALAR_TYPE_BOOLEAN:
            if (!upd.mData->is<bool>()) {
              NS_WARNING(
                  "Attempting to set a boolean scalar to a non-boolean.");
              continue;
            }
            scalar->SetValue(lock, NS_ConvertUTF8toUTF16(upd.mKey),
                             upd.mData->as<bool>());
            break;
          default:
            NS_WARNING("Unsupported type coming from scalar child updates.");
        }
        break;
      }
      case ScalarActionType::eAdd: {
        if (scalarType != nsITelemetry::SCALAR_TYPE_COUNT) {
          NS_WARNING("Attempting to add on a non count scalar.");
          continue;
        }
        // We only support adding on uint32_t.
        if (!upd.mData->is<uint32_t>()) {
          NS_WARNING("Attempting to add to a count scalar with a non-integer.");
          continue;
        }
        scalar->AddValue(lock, NS_ConvertUTF8toUTF16(upd.mKey),
                         upd.mData->as<uint32_t>());
        break;
      }
      default:
        NS_WARNING(
            "Unsupported action coming from keyed scalar child updates.");
    }
  }
}

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in namespace TelemetryScalars::

// This is a StaticMutex rather than a plain Mutex (1) so that
// it gets initialised in a thread-safe manner the first time
// it is used, and (2) because it is never de-initialised, and
// a normal Mutex would show up as a leak in BloatView.  StaticMutex
// also has the "OffTheBooks" property, so it won't show as a leak
// in BloatView.
// Another reason to use a StaticMutex instead of a plain Mutex is
// that, due to the nature of Telemetry, we cannot rely on having a
// mutex initialized in InitializeGlobalState. Unfortunately, we
// cannot make sure that no other function is called before this point.
static StaticMutex gTelemetryScalarsMutex MOZ_UNANNOTATED;

void TelemetryScalar::InitializeGlobalState(bool aCanRecordBase,
                                            bool aCanRecordExtended) {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  MOZ_ASSERT(!gTelemetryScalarInitDone,
             "TelemetryScalar::InitializeGlobalState "
             "may only be called once");

  gTelemetryScalarCanRecordBase = aCanRecordBase;
  gTelemetryScalarCanRecordExtended = aCanRecordExtended;

  // Populate the static scalar name->id cache. Note that the scalar names are
  // statically allocated and come from the automatically generated
  // TelemetryScalarData.h.
  uint32_t scalarCount =
      static_cast<uint32_t>(mozilla::Telemetry::ScalarID::ScalarCount);
  for (uint32_t i = 0; i < scalarCount; i++) {
    CharPtrEntryType* entry = gScalarNameIDMap.PutEntry(gScalars[i].name());
    entry->SetData(ScalarKey{i, false});
  }

  // Initializes gDynamicScalarInfo and gDynamicStoreNames.
  internal_RegisterScalars(locker, {});

  gTelemetryScalarInitDone = true;
}

void TelemetryScalar::DeInitializeGlobalState() {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  gTelemetryScalarCanRecordBase = false;
  gTelemetryScalarCanRecordExtended = false;
  gScalarNameIDMap.Clear();
  gScalarStorageMap.Clear();
  gKeyedScalarStorageMap.Clear();
  gDynamicBuiltinScalarStorageMap.Clear();
  gDynamicBuiltinKeyedScalarStorageMap.Clear();
  gDynamicScalarInfo = nullptr;
  gDynamicStoreNames = nullptr;
  gTelemetryScalarInitDone = false;
}

void TelemetryScalar::SetCanRecordBase(bool b) {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  gTelemetryScalarCanRecordBase = b;
}

void TelemetryScalar::SetCanRecordExtended(bool b) {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  gTelemetryScalarCanRecordExtended = b;
}

/**
 * Adds the value to the given scalar.
 *
 * @param aId The scalar enum id.
 * @param aVal The numeric value to add to the scalar.
 */
void TelemetryScalar::Add(mozilla::Telemetry::ScalarID aId, uint32_t aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, false) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eAdd, ScalarVariant(aValue),
                          uniqueId);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildScalarAction(
        uniqueId.id, uniqueId.dynamic, ScalarActionType::eAdd,
        ScalarVariant(aValue));
    return;
  }

  ScalarBase* scalar = nullptr;
  nsresult rv =
      internal_GetScalarByEnum(locker, uniqueId, ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->AddValue(aValue);
}

/**
 * Adds the value to the given keyed scalar.
 *
 * @param aId The scalar enum id.
 * @param aKey The key name.
 * @param aVal The numeric value to add to the scalar.
 */
void TelemetryScalar::Add(mozilla::Telemetry::ScalarID aId,
                          const nsAString& aKey, uint32_t aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, true) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eAdd, ScalarVariant(aValue),
                          uniqueId, aKey);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildKeyedScalarAction(
        uniqueId.id, uniqueId.dynamic, aKey, ScalarActionType::eAdd,
        ScalarVariant(aValue));
    return;
  }

  KeyedScalar* scalar = nullptr;
  nsresult rv = internal_GetKeyedScalarByEnum(locker, uniqueId,
                                              ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->AddValue(locker, aKey, aValue);
}

/**
 * Sets the scalar to the given numeric value.
 *
 * @param aId The scalar enum id.
 * @param aValue The numeric, unsigned value to set the scalar to.
 */
void TelemetryScalar::Set(mozilla::Telemetry::ScalarID aId, uint32_t aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, false) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eSet, ScalarVariant(aValue),
                          uniqueId);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildScalarAction(
        uniqueId.id, uniqueId.dynamic, ScalarActionType::eSet,
        ScalarVariant(aValue));
    return;
  }

  ScalarBase* scalar = nullptr;
  nsresult rv =
      internal_GetScalarByEnum(locker, uniqueId, ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->SetValue(aValue);
}

/**
 * Sets the scalar to the given string value.
 *
 * @param aId The scalar enum id.
 * @param aValue The string value to set the scalar to.
 */
void TelemetryScalar::Set(mozilla::Telemetry::ScalarID aId,
                          const nsAString& aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, false) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eSet,
                          ScalarVariant(nsString(aValue)), uniqueId);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildScalarAction(
        uniqueId.id, uniqueId.dynamic, ScalarActionType::eSet,
        ScalarVariant(nsString(aValue)));
    return;
  }

  ScalarBase* scalar = nullptr;
  nsresult rv =
      internal_GetScalarByEnum(locker, uniqueId, ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->SetValue(aValue);
}

/**
 * Sets the scalar to the given boolean value.
 *
 * @param aId The scalar enum id.
 * @param aValue The boolean value to set the scalar to.
 */
void TelemetryScalar::Set(mozilla::Telemetry::ScalarID aId, bool aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, false) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eSet, ScalarVariant(aValue),
                          uniqueId);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildScalarAction(
        uniqueId.id, uniqueId.dynamic, ScalarActionType::eSet,
        ScalarVariant(aValue));
    return;
  }

  ScalarBase* scalar = nullptr;
  nsresult rv =
      internal_GetScalarByEnum(locker, uniqueId, ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->SetValue(aValue);
}

/**
 * Sets the keyed scalar to the given numeric value.
 *
 * @param aId The scalar enum id.
 * @param aKey The scalar key.
 * @param aValue The numeric, unsigned value to set the scalar to.
 */
void TelemetryScalar::Set(mozilla::Telemetry::ScalarID aId,
                          const nsAString& aKey, uint32_t aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, true) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eSet, ScalarVariant(aValue),
                          uniqueId, aKey);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildKeyedScalarAction(
        uniqueId.id, uniqueId.dynamic, aKey, ScalarActionType::eSet,
        ScalarVariant(aValue));
    return;
  }

  KeyedScalar* scalar = nullptr;
  nsresult rv = internal_GetKeyedScalarByEnum(locker, uniqueId,
                                              ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->SetValue(locker, aKey, aValue);
}

/**
 * Sets the scalar to the given boolean value.
 *
 * @param aId The scalar enum id.
 * @param aKey The scalar key.
 * @param aValue The boolean value to set the scalar to.
 */
void TelemetryScalar::Set(mozilla::Telemetry::ScalarID aId,
                          const nsAString& aKey, bool aValue) {
  if (NS_WARN_IF(!IsValidEnumId(aId))) {
    MOZ_ASSERT_UNREACHABLE("Scalar usage requires valid ids.");
    return;
  }

  ScalarKey uniqueId{static_cast<uint32_t>(aId), false};
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  if (internal_CanRecordScalar(locker, uniqueId, true) != ScalarResult::Ok) {
    // We can't record this scalar. Bail out.
    return;
  }

  internal_profilerMarker(locker, ScalarActionType::eSet, ScalarVariant(aValue),
                          uniqueId, aKey);

  // Accumulate in the child process if needed.
  if (!XRE_IsParentProcess()) {
    TelemetryIPCAccumulator::RecordChildKeyedScalarAction(
        uniqueId.id, uniqueId.dynamic, aKey, ScalarActionType::eSet,
        ScalarVariant(aValue));
    return;
  }

  KeyedScalar* scalar = nullptr;
  nsresult rv = internal_GetKeyedScalarByEnum(locker, uniqueId,
                                              ProcessID::Parent, &scalar);
  if (NS_FAILED(rv)) {
    return;
  }

  scalar->SetValue(locker, aKey, aValue);
}

nsresult TelemetryScalar::CreateSnapshots(unsigned int aDataset,
                                          bool aClearScalars, JSContext* aCx,
                                          uint8_t optional_argc,
                                          JS::MutableHandle<JS::Value> aResult,
                                          bool aFilterTest,
                                          const nsACString& aStoreName) {
  MOZ_ASSERT(
      XRE_IsParentProcess(),
      "Snapshotting scalars should only happen in the parent processes.");
  // If no arguments were passed in, apply the default value.
  if (!optional_argc) {
    aClearScalars = false;
  }

  JS::Rooted<JSObject*> root_obj(aCx, JS_NewPlainObject(aCx));
  if (!root_obj) {
    return NS_ERROR_FAILURE;
  }
  aResult.setObject(*root_obj);

  // Return `{}` in child processes.
  if (!XRE_IsParentProcess()) {
    return NS_OK;
  }

  // Only lock the mutex while accessing our data, without locking any JS
  // related code.
  ScalarSnapshotTable scalarsToReflect;
  {
    StaticMutexAutoLock locker(gTelemetryScalarsMutex);

    nsresult rv = internal_GetScalarSnapshot(locker, scalarsToReflect, aDataset,
                                             aClearScalars, aStoreName);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Reflect it to JS.
  for (const auto& entry : scalarsToReflect) {
    const ScalarTupleArray& processScalars = entry.GetData();
    const char* processName = GetNameForProcessID(ProcessID(entry.GetKey()));

    // Create the object that will hold the scalars for this process and add it
    // to the returned root object.
    JS::Rooted<JSObject*> processObj(aCx, JS_NewPlainObject(aCx));
    if (!processObj || !JS_DefineProperty(aCx, root_obj, processName,
                                          processObj, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    for (ScalarTupleArray::size_type i = 0; i < processScalars.Length(); i++) {
      const ScalarDataTuple& scalar = processScalars[i];

      const char* scalarName = std::get<0>(scalar);
      if (aFilterTest && strncmp(TEST_SCALAR_PREFIX, scalarName,
                                 strlen(TEST_SCALAR_PREFIX)) == 0) {
        continue;
      }

      // Convert it to a JS Val.
      JS::Rooted<JS::Value> scalarJsValue(aCx);
      nsresult rv = nsContentUtils::XPConnect()->VariantToJS(
          aCx, processObj, std::get<1>(scalar), &scalarJsValue);
      if (NS_FAILED(rv)) {
        return rv;
      }

      // Add it to the scalar object.
      if (!JS_DefineProperty(aCx, processObj, scalarName, scalarJsValue,
                             JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  return NS_OK;
}

nsresult TelemetryScalar::CreateKeyedSnapshots(
    unsigned int aDataset, bool aClearScalars, JSContext* aCx,
    uint8_t optional_argc, JS::MutableHandle<JS::Value> aResult,
    bool aFilterTest, const nsACString& aStoreName) {
  MOZ_ASSERT(
      XRE_IsParentProcess(),
      "Snapshotting scalars should only happen in the parent processes.");
  // If no arguments were passed in, apply the default value.
  if (!optional_argc) {
    aClearScalars = false;
  }

  JS::Rooted<JSObject*> root_obj(aCx, JS_NewPlainObject(aCx));
  if (!root_obj) {
    return NS_ERROR_FAILURE;
  }
  aResult.setObject(*root_obj);

  // Return `{}` in child processes.
  if (!XRE_IsParentProcess()) {
    return NS_OK;
  }

  // Only lock the mutex while accessing our data, without locking any JS
  // related code.
  KeyedScalarSnapshotTable scalarsToReflect;
  {
    StaticMutexAutoLock locker(gTelemetryScalarsMutex);

    nsresult rv = internal_GetKeyedScalarSnapshot(
        locker, scalarsToReflect, aDataset, aClearScalars, aStoreName);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Reflect it to JS.
  for (const auto& entry : scalarsToReflect) {
    const KeyedScalarTupleArray& processScalars = entry.GetData();
    const char* processName = GetNameForProcessID(ProcessID(entry.GetKey()));

    // Create the object that will hold the scalars for this process and add it
    // to the returned root object.
    JS::Rooted<JSObject*> processObj(aCx, JS_NewPlainObject(aCx));
    if (!processObj || !JS_DefineProperty(aCx, root_obj, processName,
                                          processObj, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }

    for (KeyedScalarTupleArray::size_type i = 0; i < processScalars.Length();
         i++) {
      const KeyedScalarDataTuple& keyedScalarData = processScalars[i];

      const char* scalarName = std::get<0>(keyedScalarData);
      if (aFilterTest && strncmp(TEST_SCALAR_PREFIX, scalarName,
                                 strlen(TEST_SCALAR_PREFIX)) == 0) {
        continue;
      }

      // Go through each keyed scalar and create a keyed scalar object.
      // This object will hold the values for all the keyed scalar keys.
      JS::Rooted<JSObject*> keyedScalarObj(aCx, JS_NewPlainObject(aCx));

      // Define a property for each scalar key, then add it to the keyed scalar
      // object.
      const nsTArray<KeyedScalar::KeyValuePair>& keyProps =
          std::get<1>(keyedScalarData);
      for (uint32_t i = 0; i < keyProps.Length(); i++) {
        const KeyedScalar::KeyValuePair& keyData = keyProps[i];

        // Convert the value for the key to a JSValue.
        JS::Rooted<JS::Value> keyJsValue(aCx);
        nsresult rv = nsContentUtils::XPConnect()->VariantToJS(
            aCx, keyedScalarObj, keyData.second, &keyJsValue);
        if (NS_FAILED(rv)) {
          return rv;
        }

        // Add the key to the scalar representation.
        const NS_ConvertUTF8toUTF16 key(keyData.first);
        if (!JS_DefineUCProperty(aCx, keyedScalarObj, key.Data(), key.Length(),
                                 keyJsValue, JSPROP_ENUMERATE)) {
          return NS_ERROR_FAILURE;
        }
      }

      // Add the scalar to the root object.
      if (!JS_DefineProperty(aCx, processObj, scalarName, keyedScalarObj,
                             JSPROP_ENUMERATE)) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  return NS_OK;
}

nsresult TelemetryScalar::RegisterScalars(const nsACString& aCategoryName,
                                          JS::Handle<JS::Value> aScalarData,
                                          JSContext* cx) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Dynamic scalars should only be created in the parent process.");

  if (!IsValidIdentifierString(aCategoryName, kMaximumCategoryNameLength, true,
                               false)) {
    JS_ReportErrorASCII(cx, "Invalid category name %s.",
                        PromiseFlatCString(aCategoryName).get());
    return NS_ERROR_INVALID_ARG;
  }

  if (!aScalarData.isObject()) {
    JS_ReportErrorASCII(cx, "Scalar data parameter should be an object");
    return NS_ERROR_INVALID_ARG;
  }

  JS::Rooted<JSObject*> obj(cx, &aScalarData.toObject());
  JS::Rooted<JS::IdVector> scalarPropertyIds(cx, JS::IdVector(cx));
  if (!JS_Enumerate(cx, obj, &scalarPropertyIds)) {
    return NS_ERROR_FAILURE;
  }

  // Collect the scalar data into local storage first.
  // Only after successfully validating all contained scalars will we register
  // them into global storage.
  nsTArray<DynamicScalarInfo> newScalarInfos;

  for (size_t i = 0, n = scalarPropertyIds.length(); i < n; i++) {
    nsAutoJSString scalarName;
    if (!scalarName.init(cx, scalarPropertyIds[i])) {
      return NS_ERROR_FAILURE;
    }

    if (!IsValidIdentifierString(NS_ConvertUTF16toUTF8(scalarName),
                                 kMaximumScalarNameLength, false, true)) {
      JS_ReportErrorASCII(
          cx, "Invalid scalar name %s.",
          PromiseFlatCString(NS_ConvertUTF16toUTF8(scalarName)).get());
      return NS_ERROR_INVALID_ARG;
    }

    // Join the category and the probe names.
    nsPrintfCString fullName("%s.%s", PromiseFlatCString(aCategoryName).get(),
                             NS_ConvertUTF16toUTF8(scalarName).get());

    JS::Rooted<JS::Value> value(cx);
    if (!JS_GetPropertyById(cx, obj, scalarPropertyIds[i], &value) ||
        !value.isObject()) {
      return NS_ERROR_FAILURE;
    }
    JS::Rooted<JSObject*> scalarDef(cx, &value.toObject());

    // Get the scalar's kind.
    if (!JS_GetProperty(cx, scalarDef, "kind", &value) || !value.isInt32()) {
      JS_ReportErrorASCII(cx, "Invalid or missing 'kind' for scalar %s.",
                          PromiseFlatCString(fullName).get());
      return NS_ERROR_FAILURE;
    }
    uint32_t kind = static_cast<uint32_t>(value.toInt32());

    // Get the optional scalar's recording policy (default to false).
    bool hasProperty = false;
    bool recordOnRelease = false;
    if (JS_HasProperty(cx, scalarDef, "record_on_release", &hasProperty) &&
        hasProperty) {
      if (!JS_GetProperty(cx, scalarDef, "record_on_release", &value) ||
          !value.isBoolean()) {
        JS_ReportErrorASCII(cx, "Invalid 'record_on_release' for scalar %s.",
                            PromiseFlatCString(fullName).get());
        return NS_ERROR_FAILURE;
      }
      recordOnRelease = static_cast<bool>(value.toBoolean());
    }

    // Get the optional scalar's keyed (default to false).
    bool keyed = false;
    if (JS_HasProperty(cx, scalarDef, "keyed", &hasProperty) && hasProperty) {
      if (!JS_GetProperty(cx, scalarDef, "keyed", &value) ||
          !value.isBoolean()) {
        JS_ReportErrorASCII(cx, "Invalid 'keyed' for scalar %s.",
                            PromiseFlatCString(fullName).get());
        return NS_ERROR_FAILURE;
      }
      keyed = static_cast<bool>(value.toBoolean());
    }

    // Get the optional scalar's expired state (default to false).
    bool expired = false;
    if (JS_HasProperty(cx, scalarDef, "expired", &hasProperty) && hasProperty) {
      if (!JS_GetProperty(cx, scalarDef, "expired", &value) ||
          !value.isBoolean()) {
        JS_ReportErrorASCII(cx, "Invalid 'expired' for scalar %s.",
                            PromiseFlatCString(fullName).get());
        return NS_ERROR_FAILURE;
      }
      expired = static_cast<bool>(value.toBoolean());
    }

    // Get the scalar's optional stores list (default to ["main"]).
    nsTArray<nsCString> stores;
    if (JS_HasProperty(cx, scalarDef, "stores", &hasProperty) && hasProperty) {
      bool isArray = false;
      if (!JS_GetProperty(cx, scalarDef, "stores", &value) ||
          !JS::IsArrayObject(cx, value, &isArray) || !isArray) {
        JS_ReportErrorASCII(cx, "Invalid 'stores' for scalar %s.",
                            PromiseFlatCString(fullName).get());
        return NS_ERROR_FAILURE;
      }

      JS::Rooted<JSObject*> arrayObj(cx, &value.toObject());
      uint32_t storesLength = 0;
      if (!JS::GetArrayLength(cx, arrayObj, &storesLength)) {
        JS_ReportErrorASCII(cx,
                            "Can't get 'stores' array length for scalar %s.",
                            PromiseFlatCString(fullName).get());
        return NS_ERROR_FAILURE;
      }

      for (uint32_t i = 0; i < storesLength; ++i) {
        JS::Rooted<JS::Value> elt(cx);
        if (!JS_GetElement(cx, arrayObj, i, &elt)) {
          JS_ReportErrorASCII(
              cx, "Can't get element from scalar %s 'stores' array.",
              PromiseFlatCString(fullName).get());
          return NS_ERROR_FAILURE;
        }
        if (!elt.isString()) {
          JS_ReportErrorASCII(cx,
                              "Element in scalar %s 'stores' array isn't a "
                              "string.",
                              PromiseFlatCString(fullName).get());
          return NS_ERROR_FAILURE;
        }

        nsAutoJSString jsStr;
        if (!jsStr.init(cx, elt)) {
          return NS_ERROR_FAILURE;
        }

        stores.AppendElement(NS_ConvertUTF16toUTF8(jsStr));
      }
      // In the event of the usual case (just "main"), save the storage.
      if (stores.Length() == 1 && stores[0].EqualsLiteral("main")) {
        stores.ClearAndRetainStorage();
      }
    }

    // We defer the actual registration here in case any other event description
    // is invalid. In that case we don't need to roll back any partial
    // registration.
    newScalarInfos.AppendElement(DynamicScalarInfo{
        kind, recordOnRelease, expired, fullName, keyed, std::move(stores)});
  }

  // Register the dynamic definition on the parent process.
  nsTArray<DynamicScalarDefinition> ipcDefinitions;
  {
    StaticMutexAutoLock locker(gTelemetryScalarsMutex);
    ::internal_RegisterScalars(locker, newScalarInfos);

    // Convert the internal scalar representation to a stripped down IPC one.
    ::internal_DynamicScalarToIPC(locker, newScalarInfos, ipcDefinitions);
  }

  // Propagate the registration to all the content-processes.
  // Do not hold the mutex while calling IPC.
  ::internal_BroadcastDefinitions(ipcDefinitions);

  return NS_OK;
}

/**
 * Resets all the stored scalars. This is intended to be only used in tests.
 */
void TelemetryScalar::ClearScalars() {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Scalars should only be cleared in the parent process.");
  if (!XRE_IsParentProcess()) {
    return;
  }

  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  gScalarStorageMap.Clear();
  gKeyedScalarStorageMap.Clear();
  gDynamicBuiltinScalarStorageMap.Clear();
  gDynamicBuiltinKeyedScalarStorageMap.Clear();
}

size_t TelemetryScalar::GetMapShallowSizesOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  return gScalarNameIDMap.ShallowSizeOfExcludingThis(aMallocSizeOf);
}

size_t TelemetryScalar::GetScalarSizesOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  size_t n = 0;

  auto getSizeOf = [aMallocSizeOf](auto& storageMap) {
    size_t partial = 0;
    for (const auto& scalarStorage : storageMap.Values()) {
      for (const auto& scalar : scalarStorage->Values()) {
        partial += scalar->SizeOfIncludingThis(aMallocSizeOf);
      }
    }
    return partial;
  };

  // Account for all the storage used for the different scalar types.
  n += getSizeOf(gScalarStorageMap);
  n += getSizeOf(gKeyedScalarStorageMap);
  n += getSizeOf(gDynamicBuiltinScalarStorageMap);
  n += getSizeOf(gDynamicBuiltinKeyedScalarStorageMap);

  return n;
}

void TelemetryScalar::UpdateChildData(
    ProcessID aProcessType,
    const nsTArray<mozilla::Telemetry::ScalarAction>& aScalarActions) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "The stored child processes scalar data must be updated from the "
             "parent process.");
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  internal_ApplyScalarActions(locker, aScalarActions, Some(aProcessType));
}

void TelemetryScalar::UpdateChildKeyedData(
    ProcessID aProcessType,
    const nsTArray<mozilla::Telemetry::KeyedScalarAction>& aScalarActions) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "The stored child processes keyed scalar data must be updated "
             "from the parent process.");
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);

  internal_ApplyKeyedScalarActions(locker, aScalarActions, Some(aProcessType));
}

void TelemetryScalar::RecordDiscardedData(
    ProcessID aProcessType,
    const mozilla::Telemetry::DiscardedData& aDiscardedData) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Discarded Data must be updated from the parent process.");
  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  if (!internal_CanRecordBase(locker)) {
    return;
  }

  ScalarBase* scalar = nullptr;
  mozilla::DebugOnly<nsresult> rv;

#define REPORT_DISCARDED(id, member)                                         \
  if (aDiscardedData.member) {                                               \
    ScalarKey uniqueId{static_cast<uint32_t>(ScalarID::id), false};          \
    rv = internal_GetScalarByEnum(locker, uniqueId, aProcessType, &scalar);  \
    MOZ_ASSERT(NS_SUCCEEDED(rv));                                            \
    internal_profilerMarker(locker, ScalarActionType::eAdd,                  \
                            ScalarVariant(aDiscardedData.member), uniqueId); \
    scalar->AddValue(aDiscardedData.member);                                 \
  }

  REPORT_DISCARDED(TELEMETRY_DISCARDED_ACCUMULATIONS,
                   mDiscardedHistogramAccumulations)
  REPORT_DISCARDED(TELEMETRY_DISCARDED_KEYED_ACCUMULATIONS,
                   mDiscardedKeyedHistogramAccumulations)
  REPORT_DISCARDED(TELEMETRY_DISCARDED_SCALAR_ACTIONS, mDiscardedScalarActions)
  REPORT_DISCARDED(TELEMETRY_DISCARDED_KEYED_SCALAR_ACTIONS,
                   mDiscardedKeyedScalarActions)
  REPORT_DISCARDED(TELEMETRY_DISCARDED_CHILD_EVENTS, mDiscardedChildEvents)
}

/**
 * Get the dynamic scalar definitions in an IPC-friendly
 * structure.
 */
void TelemetryScalar::GetDynamicScalarDefinitions(
    nsTArray<DynamicScalarDefinition>& aDefArray) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!gDynamicScalarInfo) {
    // Don't have dynamic scalar definitions. Bail out!
    return;
  }

  StaticMutexAutoLock locker(gTelemetryScalarsMutex);
  internal_DynamicScalarToIPC(locker, *gDynamicScalarInfo, aDefArray);
}

/**
 * This adds the dynamic scalar definitions coming from
 * the parent process to this child process. If a dynamic
 * scalar definition is already defined, check if the new definition
 * makes the scalar expired and eventually update the expiration
 * state.
 */
void TelemetryScalar::AddDynamicScalarDefinitions(
    const nsTArray<DynamicScalarDefinition>& aDefs) {
  MOZ_ASSERT(!XRE_IsParentProcess());

  nsTArray<DynamicScalarInfo> dynamicStubs;

  // Populate the definitions array before acquiring the lock.
  for (auto& def : aDefs) {
    bool recordOnRelease = def.dataset == nsITelemetry::DATASET_ALL_CHANNELS;
    dynamicStubs.AppendElement(DynamicScalarInfo{def.type,
                                                 recordOnRelease,
                                                 def.expired,
                                                 def.name,
                                                 def.keyed,
                                                 {} /* stores */});
  }

  {
    StaticMutexAutoLock locker(gTelemetryScalarsMutex);
    internal_RegisterScalars(locker, dynamicStubs);
  }
}

nsresult TelemetryScalar::GetAllStores(StringHashSet& set) {
  // Static stores
  for (uint32_t storeIdx : gScalarStoresTable) {
    const char* name = &gScalarsStringTable[storeIdx];
    nsAutoCString store;
    store.AssignASCII(name);
    if (!set.Insert(store, mozilla::fallible)) {
      return NS_ERROR_FAILURE;
    }
  }

  // Dynamic stores
  for (auto& ptr : *gDynamicStoreNames) {
    nsAutoCString store;
    ptr->ToUTF8String(store);
    if (!set.Insert(store, mozilla::fallible)) {
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}
