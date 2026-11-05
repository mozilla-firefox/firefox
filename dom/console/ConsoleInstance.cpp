/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ConsoleInstance.h"

#include "Console.h"
#include "ConsoleCommon.h"
#include "ConsoleUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/ConsoleBinding.h"
#include "nsContentUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ConsoleInstance, mConsole)

NS_IMPL_CYCLE_COLLECTING_ADDREF(ConsoleInstance)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ConsoleInstance)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ConsoleInstance)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_END

namespace {

ConsoleUtils::Level WebIDLevelToConsoleUtilsLevel(ConsoleLevel aLevel) {
  switch (aLevel) {
    case ConsoleLevel::Log:
      return ConsoleUtils::eLog;
    case ConsoleLevel::Warning:
      return ConsoleUtils::eWarning;
    case ConsoleLevel::Error:
      return ConsoleUtils::eError;
    default:
      break;
  }

  return ConsoleUtils::eLog;
}

}  // namespace

ConsoleInstance::ConsoleInstance(JSContext* aCx,
                                 const ConsoleInstanceOptions& aOptions)
    : mMaxLogLevel(ConsoleLogLevel::All),
      mConsole(new Console(aCx, nullptr, 0, 0, aOptions.mLogModule,
                           aOptions.mPrefix)) {
  mConsole->mConsoleID = aOptions.mConsoleID;
  mConsole->mPassedInnerID = aOptions.mInnerID;

  if (aOptions.mDump.WasPassed()) {
    mConsole->mDumpFunction = &aOptions.mDump.Value();
  }

  // Let's inform that this is a custom instance.
  mConsole->mChromeInstance = true;

  if (aOptions.mMaxLogLevel.WasPassed()) {
    mMaxLogLevel = aOptions.mMaxLogLevel.Value();
  }

  if (!aOptions.mMaxLogLevelPref.IsEmpty()) {
    if (!NS_IsMainThread()) {
      // Set the log level based on what we have.
      SetLogLevel();

      // Flag an error to the console.
      JS::Rooted<JS::Value> msg(aCx);
      if (!ToJSValue(
              aCx,
              nsLiteralCString(
                  "Console.maxLogLevelPref is not supported within workers!"),
              &msg)) {
        JS_ClearPendingException(aCx);
        return;
      }

      AutoTArray<JS::Value, 1> sequence;
      SequenceRooter rootedSequence(aCx, &sequence);
      sequence.AppendElement(std::move(msg));
      this->Error(aCx, std::move(sequence));
      return;
    }

    mMaxLogLevelPref = aOptions.mMaxLogLevelPref;

    Preferences::RegisterCallback(MaxLogLevelPrefChangedCallback,
                                  mMaxLogLevelPref, this);
  }
  SetLogLevel();
}

ConsoleInstance::~ConsoleInstance() {
  // We should only ever have set `mMaxLogLevelPref` when on the main thread,
  // but check it here to be safe.
  if (!mMaxLogLevelPref.IsEmpty() && NS_IsMainThread()) {
    Preferences::UnregisterCallback(MaxLogLevelPrefChangedCallback,
                                    mMaxLogLevelPref, this);
  }
};

ConsoleLogLevel PrefToValue(const nsACString& aPref,
                            const ConsoleLogLevel aLevel) {
  if (aPref.IsEmpty()) {
    return aLevel;
  }

  nsAutoCString value;
  nsresult rv = Preferences::GetCString(PromiseFlatCString(aPref).get(), value);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsString message;
    message.AssignLiteral(
        "Console.maxLogLevelPref used with a non-existing pref: ");
    message.Append(NS_ConvertUTF8toUTF16(aPref));

    nsContentUtils::LogSimpleConsoleError(message, "chrome"_ns, false,
                                          true /* from chrome context*/);
    return aLevel;
  }

  Maybe<ConsoleLogLevel> level =
      CaseInsensitiveStringToEnum<ConsoleLogLevel>(value);
  if (NS_WARN_IF(level.isNothing())) {
    nsString message;
    message.AssignLiteral("Invalid Console.maxLogLevelPref value: ");
    message.Append(NS_ConvertUTF8toUTF16(value));

    nsContentUtils::LogSimpleConsoleError(message, "chrome"_ns, false,
                                          true /* from chrome context*/);
    return aLevel;
  }

  return level.value();
}

void ConsoleInstance::SetLogLevel() {
  mConsole->mCurrentLogLevel = mConsole->WebIDLLogLevelToInteger(
      PrefToValue(mMaxLogLevelPref, mMaxLogLevel));
}

// static
void ConsoleInstance::MaxLogLevelPrefChangedCallback(
    const char* /* aPrefName */, void* aSelf) {
  auto* instance = static_cast<ConsoleInstance*>(aSelf);
  if (MOZ_UNLIKELY(!instance->mConsole)) {
    // We've been unlinked already but not destroyed yet. Bail.
    return;
  }
  RefPtr pin{instance};
  pin->SetLogLevel();
}

JSObject* ConsoleInstance::WrapObject(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return ConsoleInstance_Binding::Wrap(aCx, this, aGivenProto);
}

#define METHOD(name, string)                                     \
  void ConsoleInstance::name(JSContext* aCx,                     \
                             const Sequence<JS::Value>& aData) { \
    RefPtr<Console> console(mConsole);                           \
    if (MOZ_UNLIKELY(!console)) {                                \
      return;                                                    \
    }                                                            \
    console->MethodInternal(aCx, Console::Method##name,          \
                            nsLiteralString(string), aData);     \
  }

METHOD(Log, u"log")
METHOD(Info, u"info")
METHOD(Warn, u"warn")
METHOD(Error, u"error")
METHOD(Exception, u"exception")
METHOD(Debug, u"debug")
METHOD(Table, u"table")
METHOD(Trace, u"trace")
METHOD(Dir, u"dir");
METHOD(Dirxml, u"dirxml");
METHOD(Group, u"group")
METHOD(GroupCollapsed, u"groupCollapsed")

#undef METHOD

void ConsoleInstance::GroupEnd(JSContext* aCx) {
  RefPtr<Console> console(mConsole);
  const Sequence<JS::Value> data;
  console->MethodInternal(aCx, Console::MethodGroupEnd, u"groupEnd"_ns, data);
}

void ConsoleInstance::Time(JSContext* aCx, const nsAString& aLabel) {
  RefPtr<Console> console(mConsole);
  console->StringMethodInternal(aCx, aLabel, Sequence<JS::Value>(),
                                Console::MethodTime, u"time"_ns);
}

void ConsoleInstance::TimeLog(JSContext* aCx, const nsAString& aLabel,
                              const Sequence<JS::Value>& aData) {
  RefPtr<Console> console(mConsole);
  console->StringMethodInternal(aCx, aLabel, aData, Console::MethodTimeLog,
                                u"timeLog"_ns);
}

void ConsoleInstance::TimeEnd(JSContext* aCx, const nsAString& aLabel) {
  RefPtr<Console> console(mConsole);
  console->StringMethodInternal(aCx, aLabel, Sequence<JS::Value>(),
                                Console::MethodTimeEnd, u"timeEnd"_ns);
}

void ConsoleInstance::TimeStamp(JSContext* aCx,
                                const JS::Handle<JS::Value> aData) {
  ConsoleCommon::ClearException ce(aCx);

  Sequence<JS::Value> data;
  SequenceRooter<JS::Value> rooter(aCx, &data);

  if (aData.isString() && !data.AppendElement(aData, fallible)) {
    return;
  }

  RefPtr<Console> console(mConsole);
  console->MethodInternal(aCx, Console::MethodTimeStamp, u"timeStamp"_ns, data);
}

void ConsoleInstance::Profile(JSContext* aCx,
                              const Sequence<JS::Value>& aData) {
  RefPtr<Console> console(mConsole);
  console->ProfileMethodInternal(aCx, Console::MethodProfile, u"profile"_ns,
                                 aData);
}

void ConsoleInstance::ProfileEnd(JSContext* aCx,
                                 const Sequence<JS::Value>& aData) {
  RefPtr<Console> console(mConsole);
  console->ProfileMethodInternal(aCx, Console::MethodProfileEnd,
                                 u"profileEnd"_ns, aData);
}

void ConsoleInstance::Assert(JSContext* aCx, bool aCondition,
                             const Sequence<JS::Value>& aData) {
  if (!aCondition) {
    RefPtr<Console> console(mConsole);
    console->MethodInternal(aCx, Console::MethodAssert, u"assert"_ns, aData);
  }
}

void ConsoleInstance::Count(JSContext* aCx, const nsAString& aLabel) {
  RefPtr<Console> console(mConsole);
  console->StringMethodInternal(aCx, aLabel, Sequence<JS::Value>(),
                                Console::MethodCount, u"count"_ns);
}

void ConsoleInstance::CountReset(JSContext* aCx, const nsAString& aLabel) {
  RefPtr<Console> console(mConsole);
  console->StringMethodInternal(aCx, aLabel, Sequence<JS::Value>(),
                                Console::MethodCountReset, u"countReset"_ns);
}

void ConsoleInstance::Clear(JSContext* aCx) {
  RefPtr<Console> console(mConsole);
  const Sequence<JS::Value> data;
  console->MethodInternal(aCx, Console::MethodClear, u"clear"_ns, data);
}

bool ConsoleInstance::ShouldLog(ConsoleLogLevel aLevel) {
  return mConsole->mCurrentLogLevel <=
             mConsole->WebIDLLogLevelToInteger(aLevel) ||
         mConsole->ShouldLogToMozLog(aLevel);
}

void ConsoleInstance::ReportForServiceWorkerScope(const nsAString& aScope,
                                                  const nsAString& aMessage,
                                                  const nsACString& aFilename,
                                                  uint32_t aLineNumber,
                                                  uint32_t aColumnNumber,
                                                  ConsoleLevel aLevel) {
  if (!NS_IsMainThread()) {
    return;
  }

  ConsoleUtils::ReportForServiceWorkerScope(
      aScope, aMessage, aFilename, aLineNumber, aColumnNumber,
      WebIDLevelToConsoleUtilsLevel(aLevel));
}

}  // namespace mozilla::dom
