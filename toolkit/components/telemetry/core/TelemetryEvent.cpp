/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Telemetry.h"
#include "TelemetryEvent.h"
#include <limits>
#include "ipc/TelemetryIPCAccumulator.h"
#include "jsapi.h"
#include "js/Array.h"  // JS::GetArrayLength, JS::IsArrayObject, JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_DefineProperty, JS_Enumerate, JS_GetElement, JS_GetProperty, JS_GetPropertyById, JS_HasProperty
#include "mozilla/glean/TelemetryMetrics.h"
#include "mozilla/Maybe.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/Services.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsIObserverService.h"
#include "nsITelemetry.h"
#include "nsJSUtils.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"
#include "nsUTF8Utils.h"
#include "nsXULAppAPI.h"
#include "TelemetryCommon.h"
#include "TelemetryEventData.h"
#include "TelemetryScalar.h"

using mozilla::Maybe;
using mozilla::StaticAutoPtr;
using mozilla::StaticMutex;
using mozilla::StaticMutexAutoLock;
using mozilla::TimeStamp;
using mozilla::UniquePtr;
using mozilla::Telemetry::ChildEventData;
using mozilla::Telemetry::EventExtraEntry;
using mozilla::Telemetry::ProcessID;
using mozilla::Telemetry::Common::CanRecordDataset;
using mozilla::Telemetry::Common::CanRecordInProcess;
using mozilla::Telemetry::Common::CanRecordProduct;
using mozilla::Telemetry::Common::GetNameForProcessID;
using mozilla::Telemetry::Common::IsExpiredVersion;
using mozilla::Telemetry::Common::IsInDataset;
using mozilla::Telemetry::Common::IsValidIdentifierString;
using mozilla::Telemetry::Common::LogToBrowserConsole;
using mozilla::Telemetry::Common::MsSinceProcessStart;
using mozilla::Telemetry::Common::ToJSString;

namespace TelemetryIPCAccumulator = mozilla::TelemetryIPCAccumulator;

namespace geckoprofiler::markers {

struct EventMarker : public mozilla::BaseMarkerType<EventMarker> {
  static constexpr const char* Name = "TEvent";
  // "Event" and "ChildEvent" only differ by their name.
  static constexpr bool ETWStoreName = true;
  using MS = mozilla::MarkerSchema;
  static constexpr MS::Location Locations[] = {
      MS::Location::MarkerChart,
      MS::Location::MarkerTable,
  };
  static constexpr MS::PayloadField PayloadFields[] = {
      {"cat", MS::InputType::CString, "Category", MS::Format::UniqueString},
      {"met", MS::InputType::CString, "Method", MS::Format::UniqueString},
      {"obj", MS::InputType::CString, "Object", MS::Format::UniqueString},
      {"val", MS::InputType::CString, "Value", MS::Format::String},
  };
  static constexpr const char* TooltipLabel =
      "{marker.data.cat}.{marker.data.met}#{marker.data.obj} "
      "{marker.data.val}";
  static constexpr const char* TableLabel =
      "{marker.data.cat}.{marker.data.met}#"
      "{marker.data.obj} {marker.data.val}";
  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const mozilla::ProfilerString8View& aCategory,
      const mozilla::ProfilerString8View& aMethod,
      const mozilla::ProfilerString8View& aObject,
      const Maybe<nsCString>& aValue) {
    StreamJSONMarkerDataImpl(aWriter, aCategory, aMethod, aObject);
    if (aValue.isSome()) {
      aWriter.StringProperty("val", aValue.value());
    }
  }
};

}  // namespace geckoprofiler::markers

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// Naming: there are two kinds of functions in this file:
//
// * Functions taking a StaticMutexAutoLock: these can only be reached via
//   an interface function (TelemetryEvent::*). They expect the interface
//   function to have acquired |gTelemetryEventsMutex|, so they do not
//   have to be thread-safe.
//
// * Functions named TelemetryEvent::*. This is the external interface.
//   Entries and exits to these functions are serialised using
//   |gTelemetryEventsMutex|.
//
// Avoiding races and deadlocks:
//
// All functions in the external interface (TelemetryEvent::*) are
// serialised using the mutex |gTelemetryEventsMutex|. This means
// that the external interface is thread-safe, and the internal
// functions can ignore thread safety. But it also brings a danger
// of deadlock if any function in the external interface can get back
// to that interface. That is, we will deadlock on any call chain like
// this:
//
// TelemetryEvent::* -> .. any functions .. -> TelemetryEvent::*
//
// To reduce the danger of that happening, observe the following rules:
//
// * No function in TelemetryEvent::* may directly call, nor take the
//   address of, any other function in TelemetryEvent::*.
//
// * No internal function may call, nor take the address
//   of, any function in TelemetryEvent::*.

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE TYPES

namespace {

const uint32_t kEventCount =
    static_cast<uint32_t>(mozilla::Telemetry::EventID::EventCount);
// This is a special event id used to mark expired events, to make expiry checks
// cheap at runtime.
const uint32_t kExpiredEventId = std::numeric_limits<uint32_t>::max();
static_assert(kExpiredEventId > kEventCount,
              "Built-in event count should be less than the expired event id.");

// Maximum length of any passed value string, in UTF8 byte sequence length.
const uint32_t kMaxValueByteLength = 80;
// Maximum length of any string value in the extra dictionary, in UTF8 byte
// sequence length.
const uint32_t kMaxExtraValueByteLength = 80;
// Maximum length of dynamic method names, in UTF8 byte sequence length.
const uint32_t kMaxMethodNameByteLength = 20;
// Maximum length of dynamic object names, in UTF8 byte sequence length.
const uint32_t kMaxObjectNameByteLength = 20;
// Maximum length of extra key names, in UTF8 byte sequence length.
const uint32_t kMaxExtraKeyNameByteLength = 15;
// The maximum number of valid extra keys for an event.
const uint32_t kMaxExtraKeyCount = 10;
// The number of event records allowed in an event ping.
const uint32_t kEventPingLimit = 1000;

struct EventKey {
  uint32_t id;
  bool dynamic;

  EventKey() : id(kExpiredEventId), dynamic(false) {}
  EventKey(uint32_t id_, bool dynamic_) : id(id_), dynamic(dynamic_) {}
};

struct DynamicEventInfo {
  DynamicEventInfo(const nsACString& category, const nsACString& method,
                   const nsACString& object, nsTArray<nsCString>& extra_keys,
                   bool recordOnRelease)
      : category(category),
        method(method),
        object(object),
        extra_keys(extra_keys.Clone()),
        recordOnRelease(recordOnRelease) {}

  DynamicEventInfo(const DynamicEventInfo&) = default;
  DynamicEventInfo& operator=(const DynamicEventInfo&) = delete;

  const nsCString category;
  const nsCString method;
  const nsCString object;
  const CopyableTArray<nsCString> extra_keys;
  const bool recordOnRelease;

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
    size_t n = 0;

    n += category.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += method.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += object.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += extra_keys.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto& key : extra_keys) {
      n += key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }

    return n;
  }
};

enum class RecordEventResult {
  Ok,
  UnknownEvent,
  InvalidExtraKey,
  StorageLimitReached,
  ExpiredEvent,
  WrongProcess,
  CannotRecord,
};

typedef CopyableTArray<EventExtraEntry> ExtraArray;

class EventRecord {
 public:
  EventRecord(double timestamp, const EventKey& key,
              const Maybe<nsCString>& value, const ExtraArray& extra)
      : mTimestamp(timestamp),
        mEventKey(key),
        mValue(value),
        mExtra(extra.Clone()) {}

  EventRecord(const EventRecord& other) = default;

  EventRecord& operator=(const EventRecord& other) = delete;

  double Timestamp() const { return mTimestamp; }
  const EventKey& GetEventKey() const { return mEventKey; }
  const Maybe<nsCString>& Value() const { return mValue; }
  const ExtraArray& Extra() const { return mExtra; }

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 private:
  const double mTimestamp;
  const EventKey mEventKey;
  const Maybe<nsCString> mValue;
  const ExtraArray mExtra;
};

// Implements the methods for EventInfo.
const nsDependentCString EventInfo::method() const {
  return nsDependentCString(&gEventsStringTable[this->method_offset]);
}

const nsDependentCString EventInfo::object() const {
  return nsDependentCString(&gEventsStringTable[this->object_offset]);
}

// Implements the methods for CommonEventInfo.
const nsDependentCString CommonEventInfo::category() const {
  return nsDependentCString(&gEventsStringTable[this->category_offset]);
}

const nsDependentCString CommonEventInfo::expiration_version() const {
  return nsDependentCString(
      &gEventsStringTable[this->expiration_version_offset]);
}

const nsDependentCString CommonEventInfo::extra_key(uint32_t index) const {
  MOZ_ASSERT(index < this->extra_count);
  uint32_t key_index = gExtraKeysTable[this->extra_index + index];
  return nsDependentCString(&gEventsStringTable[key_index]);
}

// Implementation for the EventRecord class.
size_t EventRecord::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;

  if (mValue) {
    n += mValue.value().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }

  n += mExtra.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (uint32_t i = 0; i < mExtra.Length(); ++i) {
    n += mExtra[i].key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += mExtra[i].value.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }

  return n;
}

nsCString UniqueEventName(const nsACString& category, const nsACString& method,
                          const nsACString& object) {
  nsCString name;
  name.Append(category);
  name.AppendLiteral("#");
  name.Append(method);
  name.AppendLiteral("#");
  name.Append(object);
  return name;
}

nsCString UniqueEventName(const EventInfo& info) {
  return UniqueEventName(info.common_info.category(), info.method(),
                         info.object());
}

nsCString UniqueEventName(const DynamicEventInfo& info) {
  return UniqueEventName(info.category, info.method, info.object);
}

void TruncateToByteLength(nsCString& str, uint32_t length) {
  // last will be the index of the first byte of the current multi-byte
  // sequence.
  uint32_t last = RewindToPriorUTF8Codepoint(str.get(), length);
  str.Truncate(last);
}

}  // anonymous namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE STATE, SHARED BY ALL THREADS

namespace {

// Set to true once this global state has been initialized.
bool gTelemetryEventInitDone = false;

bool gTelemetryEventCanRecordBase;
bool gTelemetryEventCanRecordExtended;

// The EventName -> EventKey cache map.
MOZ_RUNINIT nsTHashMap<nsCStringHashKey, EventKey> gEventNameIDMap(kEventCount);

// The CategoryName set.
constinit nsTHashSet<nsCString> gCategoryNames;

// The main event storage. Events are inserted here, keyed by process id and
// in recording order.
typedef nsUint32HashKey ProcessIDHashKey;
typedef nsTArray<EventRecord> EventRecordArray;
typedef nsClassHashtable<ProcessIDHashKey, EventRecordArray>
    EventRecordsMapType;

constinit EventRecordsMapType gEventRecords;

// The details on dynamic events that are recorded from addons are registered
// here.
StaticAutoPtr<nsTArray<DynamicEventInfo>> gDynamicEventInfo;

}  // namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: thread-safe helpers for event recording.

namespace {

unsigned int GetDataset(const StaticMutexAutoLock& lock,
                        const EventKey& eventKey) {
  if (!eventKey.dynamic) {
    return gEventInfo[eventKey.id].common_info.dataset;
  }

  if (!gDynamicEventInfo) {
    return nsITelemetry::DATASET_PRERELEASE_CHANNELS;
  }

  return (*gDynamicEventInfo)[eventKey.id].recordOnRelease
             ? nsITelemetry::DATASET_ALL_CHANNELS
             : nsITelemetry::DATASET_PRERELEASE_CHANNELS;
}

bool CanRecordEvent(const StaticMutexAutoLock& lock, const EventKey& eventKey,
                    ProcessID process) {
  if (!gTelemetryEventCanRecordBase) {
    return false;
  }

  if (!CanRecordDataset(GetDataset(lock, eventKey),
                        gTelemetryEventCanRecordBase,
                        gTelemetryEventCanRecordExtended)) {
    return false;
  }

  // We don't allow specifying a process to record in for dynamic events.
  if (!eventKey.dynamic) {
    const CommonEventInfo& info = gEventInfo[eventKey.id].common_info;

    if (!CanRecordProduct(info.products)) {
      return false;
    }

    if (!CanRecordInProcess(info.record_in_processes, process)) {
      return false;
    }
  }

  return true;
}

bool IsExpired(const EventKey& key) { return key.id == kExpiredEventId; }

EventRecordArray* GetEventRecordsForProcess(const StaticMutexAutoLock& lock,
                                            ProcessID processType) {
  return gEventRecords.GetOrInsertNew(uint32_t(processType));
}

bool GetEventKey(const StaticMutexAutoLock& lock, const nsACString& category,
                 const nsACString& method, const nsACString& object,
                 EventKey* aEventKey) {
  const nsCString& name = UniqueEventName(category, method, object);
  return gEventNameIDMap.Get(name, aEventKey);
}

static bool CheckExtraKeysValid(const EventKey& eventKey,
                                const ExtraArray& extra) {
  nsTHashSet<nsCString> validExtraKeys;
  if (!eventKey.dynamic) {
    const CommonEventInfo& common = gEventInfo[eventKey.id].common_info;
    for (uint32_t i = 0; i < common.extra_count; ++i) {
      validExtraKeys.Insert(common.extra_key(i));
    }
  } else if (gDynamicEventInfo) {
    const DynamicEventInfo& info = (*gDynamicEventInfo)[eventKey.id];
    for (uint32_t i = 0, len = info.extra_keys.Length(); i < len; ++i) {
      validExtraKeys.Insert(info.extra_keys[i]);
    }
  }

  for (uint32_t i = 0; i < extra.Length(); ++i) {
    if (!validExtraKeys.Contains(extra[i].key)) {
      return false;
    }
  }

  return true;
}

RecordEventResult RecordEvent(const StaticMutexAutoLock& lock,
                              ProcessID processType, double timestamp,
                              const nsACString& category,
                              const nsACString& method,
                              const nsACString& object,
                              const Maybe<nsCString>& value,
                              const ExtraArray& extra) {
  // Look up the event id.
  EventKey eventKey;
  if (!GetEventKey(lock, category, method, object, &eventKey)) {
    mozilla::glean::telemetry::event_recording_error
        .EnumGet(
            mozilla::glean::telemetry::EventRecordingErrorLabel::eUnknownevent)
        .Add();
    return RecordEventResult::UnknownEvent;
  }

  // If the event is expired or not enabled for this process, we silently drop
  // this call. We don't want recording for expired probes to be an error so
  // code doesn't have to be removed at a specific time or version. Even logging
  // warnings would become very noisy.
  if (IsExpired(eventKey)) {
    mozilla::glean::telemetry::event_recording_error
        .EnumGet(mozilla::glean::telemetry::EventRecordingErrorLabel::eExpired)
        .Add();
    return RecordEventResult::ExpiredEvent;
  }

  // Check whether the extra keys passed are valid.
  if (!CheckExtraKeysValid(eventKey, extra)) {
    mozilla::glean::telemetry::event_recording_error
        .EnumGet(mozilla::glean::telemetry::EventRecordingErrorLabel::eExtrakey)
        .Add();
    return RecordEventResult::InvalidExtraKey;
  }

  // Check whether we can record this event.
  if (!CanRecordEvent(lock, eventKey, processType)) {
    return RecordEventResult::CannotRecord;
  }

  EventRecordArray* eventRecords = GetEventRecordsForProcess(lock, processType);
  eventRecords->AppendElement(EventRecord(timestamp, eventKey, value, extra));

  // Notify observers when we hit the "event" ping event record limit.
  if (eventRecords->Length() == kEventPingLimit) {
    return RecordEventResult::StorageLimitReached;
  }

  return RecordEventResult::Ok;
}

RecordEventResult ShouldRecordChildEvent(const StaticMutexAutoLock& lock,
                                         const nsACString& category,
                                         const nsACString& method,
                                         const nsACString& object) {
  EventKey eventKey;
  if (!GetEventKey(lock, category, method, object, &eventKey)) {
    // This event is unknown in this process, but it might be a dynamic event
    // that was registered in the parent process.
    return RecordEventResult::Ok;
  }

  if (IsExpired(eventKey)) {
    return RecordEventResult::ExpiredEvent;
  }

  const auto processes =
      gEventInfo[eventKey.id].common_info.record_in_processes;
  if (!CanRecordInProcess(processes, XRE_GetProcessType())) {
    return RecordEventResult::WrongProcess;
  }

  return RecordEventResult::Ok;
}

void RegisterBuiltinEvents(const StaticMutexAutoLock& lock,
                           const nsACString& category,
                           const nsTArray<DynamicEventInfo>& eventInfos,
                           const nsTArray<bool>& eventExpired) {
  MOZ_ASSERT(eventInfos.Length() == eventExpired.Length(),
             "Event data array sizes should match.");

  // Register the new events.
  if (!gDynamicEventInfo) {
    gDynamicEventInfo = new nsTArray<DynamicEventInfo>();
  }

  for (uint32_t i = 0, len = eventInfos.Length(); i < len; ++i) {
    const nsCString& eventName = UniqueEventName(eventInfos[i]);
    gDynamicEventInfo->AppendElement(eventInfos[i]);
    uint32_t eventId =
        eventExpired[i] ? kExpiredEventId : gDynamicEventInfo->Length() - 1;
    gEventNameIDMap.InsertOrUpdate(eventName, EventKey{eventId, true});
  }

  // Add the category name in order to enable it later.
  gCategoryNames.Insert(category);
}

}  // anonymous namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// PRIVATE: thread-unsafe helpers for event handling.

namespace {

nsresult SerializeEventsArray(const EventRecordArray& events, JSContext* cx,
                              JS::MutableHandle<JSObject*> result,
                              unsigned int dataset) {
  // We serialize the events to a JS array.
  JS::Rooted<JSObject*> eventsArray(cx,
                                    JS::NewArrayObject(cx, events.Length()));
  if (!eventsArray) {
    return NS_ERROR_FAILURE;
  }

  for (uint32_t i = 0; i < events.Length(); ++i) {
    const EventRecord& record = events[i];

    // Each entry is an array of one of the forms:
    // [timestamp, category, method, object, value]
    // [timestamp, category, method, object, null, extra]
    // [timestamp, category, method, object, value, extra]
    JS::RootedVector<JS::Value> items(cx);

    // Add timestamp.
    JS::Rooted<JS::Value> val(cx);
    if (!items.append(JS::NumberValue(floor(record.Timestamp())))) {
      return NS_ERROR_FAILURE;
    }

    // Add category, method, object.
    auto addCategoryMethodObjectValues = [&](const nsACString& category,
                                             const nsACString& method,
                                             const nsACString& object) -> bool {
      return items.append(JS::StringValue(ToJSString(cx, category))) &&
             items.append(JS::StringValue(ToJSString(cx, method))) &&
             items.append(JS::StringValue(ToJSString(cx, object)));
    };

    const EventKey& eventKey = record.GetEventKey();
    if (!eventKey.dynamic) {
      const EventInfo& info = gEventInfo[eventKey.id];
      if (!addCategoryMethodObjectValues(info.common_info.category(),
                                         info.method(), info.object())) {
        return NS_ERROR_FAILURE;
      }
    } else if (gDynamicEventInfo) {
      const DynamicEventInfo& info = (*gDynamicEventInfo)[eventKey.id];
      if (!addCategoryMethodObjectValues(info.category, info.method,
                                         info.object)) {
        return NS_ERROR_FAILURE;
      }
    }

    // Add the optional string value only when needed.
    // When the value field is empty and extra is not set, we can save a little
    // space that way. We still need to submit a null value if extra is set, to
    // match the form: [ts, category, method, object, null, extra]
    if (record.Value()) {
      if (!items.append(
              JS::StringValue(ToJSString(cx, record.Value().value())))) {
        return NS_ERROR_FAILURE;
      }
    } else if (!record.Extra().IsEmpty()) {
      if (!items.append(JS::NullValue())) {
        return NS_ERROR_FAILURE;
      }
    }

    // Add the optional extra dictionary.
    // To save a little space, only add it when it is not empty.
    if (!record.Extra().IsEmpty()) {
      JS::Rooted<JSObject*> obj(cx, JS_NewPlainObject(cx));
      if (!obj) {
        return NS_ERROR_FAILURE;
      }

      // Add extra key & value entries.
      const ExtraArray& extra = record.Extra();
      for (uint32_t i = 0; i < extra.Length(); ++i) {
        JS::Rooted<JS::Value> value(cx);
        value.setString(ToJSString(cx, extra[i].value));

        if (!JS_DefineProperty(cx, obj, extra[i].key.get(), value,
                               JSPROP_ENUMERATE)) {
          return NS_ERROR_FAILURE;
        }
      }
      val.setObject(*obj);

      if (!items.append(val)) {
        return NS_ERROR_FAILURE;
      }
    }

    // Add the record to the events array.
    JS::Rooted<JSObject*> itemsArray(cx, JS::NewArrayObject(cx, items));
    if (!JS_DefineElement(cx, eventsArray, i, itemsArray, JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }

  result.set(eventsArray);
  return NS_OK;
}

}  // anonymous namespace

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
//
// EXTERNALLY VISIBLE FUNCTIONS in namespace TelemetryEvents::

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
static StaticMutex gTelemetryEventsMutex MOZ_UNANNOTATED;

void TelemetryEvent::InitializeGlobalState(bool aCanRecordBase,
                                           bool aCanRecordExtended) {
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  MOZ_ASSERT(!gTelemetryEventInitDone,
             "TelemetryEvent::InitializeGlobalState "
             "may only be called once");

  gTelemetryEventCanRecordBase = aCanRecordBase;
  gTelemetryEventCanRecordExtended = aCanRecordExtended;

  // Populate the static event name->id cache. Note that the event names are
  // statically allocated and come from the automatically generated
  // TelemetryEventData.h.
  const uint32_t eventCount =
      static_cast<uint32_t>(mozilla::Telemetry::EventID::EventCount);
  for (uint32_t i = 0; i < eventCount; ++i) {
    const EventInfo& info = gEventInfo[i];
    uint32_t eventId = i;

    // If this event is expired or not recorded in this process, mark it with
    // a special event id.
    // This avoids doing repeated checks at runtime.
    if (IsExpiredVersion(info.common_info.expiration_version().get())) {
      eventId = kExpiredEventId;
    }

    gEventNameIDMap.InsertOrUpdate(UniqueEventName(info),
                                   EventKey{eventId, false});
    gCategoryNames.Insert(info.common_info.category());
  }

  gTelemetryEventInitDone = true;
}

void TelemetryEvent::DeInitializeGlobalState() {
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  MOZ_ASSERT(gTelemetryEventInitDone);

  gTelemetryEventCanRecordBase = false;
  gTelemetryEventCanRecordExtended = false;

  gEventNameIDMap.Clear();
  gCategoryNames.Clear();
  gEventRecords.Clear();

  gDynamicEventInfo = nullptr;

  gTelemetryEventInitDone = false;
}

void TelemetryEvent::SetCanRecordBase(bool b) {
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  gTelemetryEventCanRecordBase = b;
}

void TelemetryEvent::SetCanRecordExtended(bool b) {
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  gTelemetryEventCanRecordExtended = b;
}

nsresult TelemetryEvent::RecordChildEvents(
    ProcessID aProcessType,
    const nsTArray<mozilla::Telemetry::ChildEventData>& aEvents) {
  MOZ_ASSERT(XRE_IsParentProcess());
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  for (uint32_t i = 0; i < aEvents.Length(); ++i) {
    const mozilla::Telemetry::ChildEventData& e = aEvents[i];

    // Timestamps from child processes are absolute. We fix them up here to be
    // relative to the main process start time.
    // This allows us to put events from all processes on the same timeline.
    double relativeTimestamp =
        (e.timestamp - TimeStamp::ProcessCreation()).ToMilliseconds();

    PROFILER_MARKER("ChildEvent", TELEMETRY, {}, EventMarker, e.category,
                    e.method, e.object, e.value);

    ::RecordEvent(locker, aProcessType, relativeTimestamp, e.category, e.method,
                  e.object, e.value, e.extra);
  }
  return NS_OK;
}

void TelemetryEvent::RecordEventNative(
    mozilla::Telemetry::EventID aId, const mozilla::Maybe<nsCString>& aValue,
    const mozilla::Maybe<ExtraArray>& aExtra) {
  // Truncate aValue if present and necessary.
  mozilla::Maybe<nsCString> value;
  if (aValue) {
    nsCString valueStr = aValue.ref();
    if (valueStr.Length() > kMaxValueByteLength) {
      TruncateToByteLength(valueStr, kMaxValueByteLength);
    }
    value = mozilla::Some(valueStr);
  }

  // Truncate any over-long extra values.
  ExtraArray extra;
  if (aExtra) {
    extra = aExtra.value();
    for (auto& item : extra) {
      if (item.value.Length() > kMaxExtraValueByteLength) {
        TruncateToByteLength(item.value, kMaxExtraValueByteLength);
      }
    }
  }

  const EventInfo& info = gEventInfo[static_cast<uint32_t>(aId)];
  const nsCString category(info.common_info.category());
  const nsCString method(info.method());
  const nsCString object(info.object());

  PROFILER_MARKER("Event", TELEMETRY, {}, EventMarker, category, method, object,
                  value);

  RecordEventResult res;
  if (!XRE_IsParentProcess()) {
    {
      StaticMutexAutoLock lock(gTelemetryEventsMutex);
      res = ::ShouldRecordChildEvent(lock, category, method, object);
    }

    if (res == RecordEventResult::Ok) {
      TelemetryIPCAccumulator::RecordChildEvent(TimeStamp::NowLoRes(), category,
                                                method, object, value, extra);
    }
  } else {
    StaticMutexAutoLock lock(gTelemetryEventsMutex);

    if (!gTelemetryEventInitDone) {
      return;
    }

    // Get the current time.
    double timestamp = -1;
    if (NS_WARN_IF(NS_FAILED(MsSinceProcessStart(&timestamp)))) {
      return;
    }

    res = ::RecordEvent(lock, ProcessID::Parent, timestamp, category, method,
                        object, value, extra);
  }

  // Trigger warnings or errors where needed.
  switch (res) {
    case RecordEventResult::UnknownEvent: {
      nsPrintfCString msg(R"(Unknown event: ["%s", "%s", "%s"])",
                          category.get(), method.get(), object.get());
      LogToBrowserConsole(nsIScriptError::errorFlag,
                          NS_ConvertUTF8toUTF16(msg));
      PROFILER_MARKER_TEXT("EventError", TELEMETRY,
                           mozilla::MarkerStack::Capture(), msg);
      return;
    }
    case RecordEventResult::InvalidExtraKey: {
      nsPrintfCString msg(R"(Invalid extra key for event ["%s", "%s", "%s"].)",
                          category.get(), method.get(), object.get());
      LogToBrowserConsole(nsIScriptError::warningFlag,
                          NS_ConvertUTF8toUTF16(msg));
      PROFILER_MARKER_TEXT("EventError", TELEMETRY,
                           mozilla::MarkerStack::Capture(), msg);
      return;
    }
    case RecordEventResult::StorageLimitReached: {
      LogToBrowserConsole(nsIScriptError::warningFlag,
                          u"Event storage limit reached."_ns);
      if (NS_IsMainThread()) {
        nsCOMPtr<nsIObserverService> serv =
            mozilla::services::GetObserverService();
        if (serv) {
          serv->NotifyObservers(
              nullptr, "event-telemetry-storage-limit-reached", nullptr);
        }
      }
      return;
    }
    default:
      return;
  }
}

static bool GetArrayPropertyValues(JSContext* cx, JS::Handle<JSObject*> obj,
                                   const char* property,
                                   nsTArray<nsCString>* results) {
  JS::Rooted<JS::Value> value(cx);
  if (!JS_GetProperty(cx, obj, property, &value)) {
    JS_ReportErrorASCII(cx, R"(Missing required property "%s" for event)",
                        property);
    return false;
  }

  bool isArray = false;
  if (!JS::IsArrayObject(cx, value, &isArray) || !isArray) {
    JS_ReportErrorASCII(cx, R"(Property "%s" for event should be an array)",
                        property);
    return false;
  }

  JS::Rooted<JSObject*> arrayObj(cx, &value.toObject());
  uint32_t arrayLength;
  if (!JS::GetArrayLength(cx, arrayObj, &arrayLength)) {
    return false;
  }

  for (uint32_t arrayIdx = 0; arrayIdx < arrayLength; ++arrayIdx) {
    JS::Rooted<JS::Value> element(cx);
    if (!JS_GetElement(cx, arrayObj, arrayIdx, &element)) {
      return false;
    }

    if (!element.isString()) {
      JS_ReportErrorASCII(
          cx, R"(Array entries for event property "%s" should be strings)",
          property);
      return false;
    }

    nsAutoJSString jsStr;
    if (!jsStr.init(cx, element)) {
      return false;
    }

    results->AppendElement(NS_ConvertUTF16toUTF8(jsStr));
  }

  return true;
}

nsresult TelemetryEvent::RegisterBuiltinEvents(const nsACString& aCategory,
                                               JS::Handle<JS::Value> aEventData,
                                               JSContext* cx) {
  MOZ_ASSERT(XRE_IsParentProcess(),
             "Events can only be registered in the parent process");

  if (!IsValidIdentifierString(aCategory, 30, true, true)) {
    JS_ReportErrorASCII(
        cx, "Category parameter should match the identifier pattern.");
    mozilla::glean::telemetry::event_registration_error
        .EnumGet(
            mozilla::glean::telemetry::EventRegistrationErrorLabel::eCategory)
        .Add();
    return NS_ERROR_INVALID_ARG;
  }

  if (!aEventData.isObject()) {
    JS_ReportErrorASCII(cx, "Event data parameter should be an object");
    mozilla::glean::telemetry::event_registration_error
        .EnumGet(mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
        .Add();
    return NS_ERROR_INVALID_ARG;
  }

  JS::Rooted<JSObject*> obj(cx, &aEventData.toObject());
  JS::Rooted<JS::IdVector> eventPropertyIds(cx, JS::IdVector(cx));
  if (!JS_Enumerate(cx, obj, &eventPropertyIds)) {
    mozilla::glean::telemetry::event_registration_error
        .EnumGet(mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
        .Add();
    return NS_ERROR_FAILURE;
  }

  // Collect the event data into local storage first.
  // Only after successfully validating all contained events will we register
  // them into global storage.
  nsTArray<DynamicEventInfo> newEventInfos;
  nsTArray<bool> newEventExpired;

  for (size_t i = 0, n = eventPropertyIds.length(); i < n; i++) {
    nsAutoJSString eventName;
    if (!eventName.init(cx, eventPropertyIds[i])) {
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(
              mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
          .Add();
      return NS_ERROR_FAILURE;
    }

    if (!IsValidIdentifierString(NS_ConvertUTF16toUTF8(eventName),
                                 kMaxMethodNameByteLength, false, true)) {
      JS_ReportErrorASCII(cx,
                          "Event names should match the identifier pattern.");
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(
              mozilla::glean::telemetry::EventRegistrationErrorLabel::eName)
          .Add();
      return NS_ERROR_INVALID_ARG;
    }

    JS::Rooted<JS::Value> value(cx);
    if (!JS_GetPropertyById(cx, obj, eventPropertyIds[i], &value) ||
        !value.isObject()) {
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(
              mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
          .Add();
      return NS_ERROR_FAILURE;
    }
    JS::Rooted<JSObject*> eventObj(cx, &value.toObject());

    // Extract the event registration data.
    nsTArray<nsCString> methods;
    nsTArray<nsCString> objects;
    nsTArray<nsCString> extra_keys;
    bool expired = false;
    bool recordOnRelease = false;

    // The methods & objects properties are required.
    if (!GetArrayPropertyValues(cx, eventObj, "methods", &methods)) {
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(
              mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
          .Add();
      return NS_ERROR_FAILURE;
    }

    if (!GetArrayPropertyValues(cx, eventObj, "objects", &objects)) {
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(
              mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
          .Add();
      return NS_ERROR_FAILURE;
    }

    // extra_keys is optional.
    bool hasProperty = false;
    if (JS_HasProperty(cx, eventObj, "extra_keys", &hasProperty) &&
        hasProperty) {
      if (!GetArrayPropertyValues(cx, eventObj, "extra_keys", &extra_keys)) {
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(
                mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
            .Add();
        return NS_ERROR_FAILURE;
      }
    }

    // expired is optional.
    if (JS_HasProperty(cx, eventObj, "expired", &hasProperty) && hasProperty) {
      JS::Rooted<JS::Value> temp(cx);
      if (!JS_GetProperty(cx, eventObj, "expired", &temp) ||
          !temp.isBoolean()) {
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(
                mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
            .Add();
        return NS_ERROR_FAILURE;
      }

      expired = temp.toBoolean();
    }

    // record_on_release is optional.
    if (JS_HasProperty(cx, eventObj, "record_on_release", &hasProperty) &&
        hasProperty) {
      JS::Rooted<JS::Value> temp(cx);
      if (!JS_GetProperty(cx, eventObj, "record_on_release", &temp) ||
          !temp.isBoolean()) {
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(
                mozilla::glean::telemetry::EventRegistrationErrorLabel::eOther)
            .Add();
        return NS_ERROR_FAILURE;
      }

      recordOnRelease = temp.toBoolean();
    }

    // Validate methods.
    for (auto& method : methods) {
      if (!IsValidIdentifierString(method, kMaxMethodNameByteLength, false,
                                   true)) {
        JS_ReportErrorASCII(
            cx, "Method names should match the identifier pattern.");
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(
                mozilla::glean::telemetry::EventRegistrationErrorLabel::eMethod)
            .Add();
        return NS_ERROR_INVALID_ARG;
      }
    }

    // Validate objects.
    for (auto& object : objects) {
      if (!IsValidIdentifierString(object, kMaxObjectNameByteLength, false,
                                   true)) {
        JS_ReportErrorASCII(
            cx, "Object names should match the identifier pattern.");
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(
                mozilla::glean::telemetry::EventRegistrationErrorLabel::eObject)
            .Add();
        return NS_ERROR_INVALID_ARG;
      }
    }

    // Validate extra keys.
    if (extra_keys.Length() > kMaxExtraKeyCount) {
      JS_ReportErrorASCII(cx, "No more than 10 extra keys can be registered.");
      mozilla::glean::telemetry::event_registration_error
          .EnumGet(mozilla::glean::telemetry::EventRegistrationErrorLabel::
                       eExtrakeys)
          .Add();
      return NS_ERROR_INVALID_ARG;
    }
    for (auto& key : extra_keys) {
      if (!IsValidIdentifierString(key, kMaxExtraKeyNameByteLength, false,
                                   true)) {
        JS_ReportErrorASCII(
            cx, "Extra key names should match the identifier pattern.");
        mozilla::glean::telemetry::event_registration_error
            .EnumGet(mozilla::glean::telemetry::EventRegistrationErrorLabel::
                         eExtrakeys)
            .Add();
        return NS_ERROR_INVALID_ARG;
      }
    }

    // Append event infos to be registered.
    for (auto& method : methods) {
      for (auto& object : objects) {
        // We defer the actual registration here in case any other event
        // description is invalid. In that case we don't need to roll back any
        // partial registration.
        DynamicEventInfo info{aCategory, method, object, extra_keys,
                              recordOnRelease};
        newEventInfos.AppendElement(info);
        newEventExpired.AppendElement(expired);
      }
    }
  }

  {
    StaticMutexAutoLock locker(gTelemetryEventsMutex);
    RegisterBuiltinEvents(locker, aCategory, newEventInfos, newEventExpired);
  }

  return NS_OK;
}

nsresult TelemetryEvent::CreateSnapshots(uint32_t aDataset, bool aClear,
                                         uint32_t aEventLimit, JSContext* cx,
                                         uint8_t optional_argc,
                                         JS::MutableHandle<JS::Value> aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  // Creating a JS snapshot of the events is a two-step process:
  // (1) Lock the storage and copy the events into function-local storage.
  // (2) Serialize the events into JS.
  // We can't hold a lock for (2) because we will run into deadlocks otherwise
  // from JS recording Telemetry.

  // (1) Extract the events from storage with a lock held.
  nsTArray<std::pair<const char*, EventRecordArray>> processEvents;
  nsTArray<std::pair<uint32_t, EventRecordArray>> leftovers;
  {
    StaticMutexAutoLock locker(gTelemetryEventsMutex);

    if (!gTelemetryEventInitDone) {
      return NS_ERROR_FAILURE;
    }

    // The snapshotting function is the same for both static and dynamic builtin
    // events. We can use the same function and store the events in the same
    // output storage.
    auto snapshotter = [aDataset, &locker, &processEvents, &leftovers, aClear,
                        optional_argc,
                        aEventLimit](EventRecordsMapType& aProcessStorage) {
      for (const auto& entry : aProcessStorage) {
        const EventRecordArray* eventStorage = entry.GetRawPtr();
        EventRecordArray events;
        EventRecordArray leftoverEvents;

        const uint32_t len = eventStorage->Length();
        for (uint32_t i = 0; i < len; ++i) {
          const EventRecord& record = (*eventStorage)[i];
          if (IsInDataset(GetDataset(locker, record.GetEventKey()), aDataset)) {
            // If we have a limit, adhere to it. If we have a limit and are
            // going to clear, save the leftovers for later.
            if (optional_argc < 2 || events.Length() < aEventLimit) {
              events.AppendElement(record);
            } else if (aClear) {
              leftoverEvents.AppendElement(record);
            }
          }
        }

        if (events.Length()) {
          const char* processName =
              GetNameForProcessID(ProcessID(entry.GetKey()));
          processEvents.EmplaceBack(processName, std::move(events));
          if (leftoverEvents.Length()) {
            leftovers.EmplaceBack(entry.GetKey(), std::move(leftoverEvents));
          }
        }
      }
    };

    // Take a snapshot of the plain and dynamic builtin events.
    snapshotter(gEventRecords);
    if (aClear) {
      gEventRecords.Clear();
      for (auto& pair : leftovers) {
        gEventRecords.InsertOrUpdate(
            pair.first,
            std::make_unique<EventRecordArray>(std::move(pair.second)));
      }
      leftovers.Clear();
    }
  }

  // (2) Serialize the events to a JS object.
  JS::Rooted<JSObject*> rootObj(cx, JS_NewPlainObject(cx));
  if (!rootObj) {
    return NS_ERROR_FAILURE;
  }

  const uint32_t processLength = processEvents.Length();
  for (uint32_t i = 0; i < processLength; ++i) {
    JS::Rooted<JSObject*> eventsArray(cx);
    if (NS_FAILED(SerializeEventsArray(processEvents[i].second, cx,
                                       &eventsArray, aDataset))) {
      return NS_ERROR_FAILURE;
    }

    if (!JS_DefineProperty(cx, rootObj, processEvents[i].first, eventsArray,
                           JSPROP_ENUMERATE)) {
      return NS_ERROR_FAILURE;
    }
  }

  aResult.setObject(*rootObj);
  return NS_OK;
}

/**
 * Resets all the stored events. This is intended to be only used in tests.
 */
void TelemetryEvent::ClearEvents() {
  StaticMutexAutoLock lock(gTelemetryEventsMutex);

  if (!gTelemetryEventInitDone) {
    return;
  }

  gEventRecords.Clear();
}

size_t TelemetryEvent::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  StaticMutexAutoLock locker(gTelemetryEventsMutex);
  size_t n = 0;

  auto getSizeOfRecords = [aMallocSizeOf](auto& storageMap) {
    size_t partial = storageMap.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (const auto& eventRecords : storageMap.Values()) {
      partial += eventRecords->ShallowSizeOfIncludingThis(aMallocSizeOf);

      const uint32_t len = eventRecords->Length();
      for (uint32_t i = 0; i < len; ++i) {
        partial += (*eventRecords)[i].SizeOfExcludingThis(aMallocSizeOf);
      }
    }
    return partial;
  };

  n += getSizeOfRecords(gEventRecords);

  n += gEventNameIDMap.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (auto iter = gEventNameIDMap.ConstIter(); !iter.Done(); iter.Next()) {
    n += iter.Key().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }

  n += gCategoryNames.ShallowSizeOfExcludingThis(aMallocSizeOf);

  if (gDynamicEventInfo) {
    n += gDynamicEventInfo->ShallowSizeOfIncludingThis(aMallocSizeOf);
    for (auto& info : *gDynamicEventInfo) {
      n += info.SizeOfExcludingThis(aMallocSizeOf);
    }
  }

  return n;
}
