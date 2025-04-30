/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_GeckoArgs_h
#define mozilla_GeckoArgs_h

#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/ipc/SharedMemoryHandle.h"

#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <string>
#include <string_view>
#include <vector>

namespace mozilla {

namespace geckoargs {

// Type used for passing arguments to a content process, including OS files.
struct ChildProcessArgs {
  std::vector<std::string> mArgs;
  std::vector<UniqueFileHandle> mFiles;
#ifdef XP_DARWIN
  std::vector<UniqueMachSendRight> mSendRights;
#endif
};

#ifdef XP_UNIX
// On some unix platforms, file handles are passed down without using a fixed
// file descriptor. This method can be used to override the default mapping.
void SetPassedFileHandles(Span<int> aFiles);
void SetPassedFileHandles(std::vector<UniqueFileHandle>&& aFiles);

// Add the file handles from a ChildProcessArgs to a fdsToRemap table.
void AddToFdsToRemap(const ChildProcessArgs& aArgs,
                     std::vector<std::pair<int, int>>& aFdsToRemap);
#endif

#ifdef XP_DARWIN
// Size of the internal static array of mach send rights. This acts as a limit
// to the number of mach send rights which can be passed on the command line.
constexpr size_t kMaxPassedMachSendRights = 10;

// Fill the internal static array with the mach send rights which were passed
// from the parent process.
void SetPassedMachSendRights(std::vector<UniqueMachSendRight>&& aSendRights);
#endif

template <typename T>
struct CommandLineArg {
  bool IsPresent(int& aArgc, char** aArgv) const {
    return ARG_FOUND ==
           CheckArg(aArgc, aArgv, sMatch, nullptr, CheckArgFlag::None);
  }

  Maybe<T> Get(int& aArgc, char** aArgv,
               const CheckArgFlag aFlags = CheckArgFlag::RemoveArg) {
    return GetCommon(sMatch, aArgc, aArgv, aFlags);
  }
  static Maybe<T> GetCommon(const char* aMatch, int& aArgc, char** aArgv,
                            const CheckArgFlag aFlags);

  const char* Name() { return sName; };

  void Put(T aValue, ChildProcessArgs& aArgs) {
    return PutCommon(sName, std::move(aValue), aArgs);
  }
  static void PutCommon(const char* aName, T aValue, ChildProcessArgs& aArgs);

  const char* sName;
  const char* sMatch;
};

/// Get()

inline Maybe<uint64_t> ParseIntArgument(std::string_view aStr) {
  uint64_t conv = 0;
  const char* end = aStr.data() + aStr.size();
  auto [ptr, ec] = std::from_chars(aStr.data(), end, conv);
  if (ec == std::errc() && ptr == end) {
    return Some(conv);
  }
  return Nothing();
}

template <>
inline Maybe<const char*> CommandLineArg<const char*>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags) {
  MOZ_ASSERT(aArgv, "aArgv must be initialized before CheckArg()");
  const char* rv = nullptr;
  if (ARG_FOUND == CheckArg(aArgc, aArgv, aMatch, &rv, aFlags)) {
    return Some(rv);
  }
  return Nothing();
}

template <>
inline Maybe<bool> CommandLineArg<bool>::GetCommon(const char* aMatch,
                                                   int& aArgc, char** aArgv,
                                                   const CheckArgFlag aFlags) {
  MOZ_ASSERT(aArgv, "aArgv must be initialized before CheckArg()");
  if (ARG_FOUND ==
      CheckArg(aArgc, aArgv, aMatch, (const char**)nullptr, aFlags)) {
    return Some(true);
  }
  return Nothing();
}

template <>
inline Maybe<uint64_t> CommandLineArg<uint64_t>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags) {
  if (Maybe<const char*> arg = CommandLineArg<const char*>::GetCommon(
          aMatch, aArgc, aArgv, aFlags)) {
    return ParseIntArgument(*arg);
  }
  return Nothing();
}

template <>
inline Maybe<uint32_t> CommandLineArg<uint32_t>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags) {
  return CommandLineArg<uint64_t>::GetCommon(aMatch, aArgc, aArgv, aFlags);
}

template <>
Maybe<UniqueFileHandle> CommandLineArg<UniqueFileHandle>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags);

#ifdef XP_DARWIN
template <>
Maybe<UniqueMachSendRight> CommandLineArg<UniqueMachSendRight>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags);
#endif

template <>
Maybe<mozilla::ipc::ReadOnlySharedMemoryHandle>
CommandLineArg<mozilla::ipc::ReadOnlySharedMemoryHandle>::GetCommon(
    const char* aMatch, int& aArgc, char** aArgv, const CheckArgFlag aFlags);

/// Put()

template <>
inline void CommandLineArg<const char*>::PutCommon(const char* aName,
                                                   const char* aValue,
                                                   ChildProcessArgs& aArgs) {
  aArgs.mArgs.push_back(aName);
  aArgs.mArgs.push_back(aValue);
}

template <>
inline void CommandLineArg<bool>::PutCommon(const char* aName, bool aValue,
                                            ChildProcessArgs& aArgs) {
  if (aValue) {
    aArgs.mArgs.push_back(aName);
  }
}

template <>
inline void CommandLineArg<uint64_t>::PutCommon(const char* aName,
                                                uint64_t aValue,
                                                ChildProcessArgs& aArgs) {
  aArgs.mArgs.push_back(aName);
  aArgs.mArgs.push_back(std::to_string(aValue));
}

template <>
inline void CommandLineArg<uint32_t>::PutCommon(const char* aName,
                                                uint32_t aValue,
                                                ChildProcessArgs& aArgs) {
  CommandLineArg<uint64_t>::PutCommon(aName, aValue, aArgs);
}

template <>
void CommandLineArg<UniqueFileHandle>::PutCommon(const char* aName,
                                                 UniqueFileHandle aValue,
                                                 ChildProcessArgs& aArgs);

#ifdef XP_DARWIN
template <>
void CommandLineArg<UniqueMachSendRight>::PutCommon(const char* aName,
                                                    UniqueMachSendRight aValue,
                                                    ChildProcessArgs& aArgs);
#endif

template <>
void CommandLineArg<mozilla::ipc::ReadOnlySharedMemoryHandle>::PutCommon(
    const char* aName, mozilla::ipc::ReadOnlySharedMemoryHandle aValue,
    ChildProcessArgs& aArgs);

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-variable"
#endif

static CommandLineArg<uint64_t> sParentPid{"-parentPid", "parentpid"};
static CommandLineArg<const char*> sInitialChannelID{"-initialChannelId",
                                                     "initialchannelid"};
static CommandLineArg<const char*> sParentBuildID{"-parentBuildID",
                                                  "parentbuildid"};
static CommandLineArg<const char*> sAppDir{"-appDir", "appdir"};
static CommandLineArg<const char*> sGREOmni{"-greomni", "greomni"};
static CommandLineArg<const char*> sAppOmni{"-appomni", "appomni"};
static CommandLineArg<const char*> sProfile{"-profile", "profile"};

static CommandLineArg<UniqueFileHandle> sIPCHandle{"-ipcHandle", "ipchandle"};

static CommandLineArg<mozilla::ipc::ReadOnlySharedMemoryHandle> sJsInitHandle{
    "-jsInitHandle", "jsinithandle"};
static CommandLineArg<mozilla::ipc::ReadOnlySharedMemoryHandle> sPrefsHandle{
    "-prefsHandle", "prefshandle"};
static CommandLineArg<mozilla::ipc::ReadOnlySharedMemoryHandle> sPrefMapHandle{
    "-prefMapHandle", "prefmaphandle"};

static CommandLineArg<uint64_t> sSandboxingKind{"-sandboxingKind",
                                                "sandboxingkind"};

static CommandLineArg<bool> sSafeMode{"-safeMode", "safemode"};

static CommandLineArg<bool> sIsForBrowser{"-isForBrowser", "isforbrowser"};
static CommandLineArg<bool> sNotForBrowser{"-notForBrowser", "notforbrowser"};

static CommandLineArg<const char*> sPluginPath{"-pluginPath", "pluginpath"};
static CommandLineArg<bool> sPluginNativeEvent{"-pluginNativeEvent",
                                               "pluginnativeevent"};
#if defined(XP_LINUX)
static CommandLineArg<UniqueFileHandle> sCrashReporter{"-crashReporter",
                                                       "crashreporter"};
#  if !defined(MOZ_WIDGET_ANDROID)
static CommandLineArg<uint64_t> sCrashHelperPid{"-crashHelperPid",
                                                "crashhelperpid"};
#  endif  // !defined(MOZ_WIDGET_ANDROID)
#else
static CommandLineArg<const char*> sCrashReporter{"-crashReporter",
                                                  "crashreporter"};
#endif

#if defined(XP_WIN)
#  if defined(MOZ_SANDBOX)
static CommandLineArg<bool> sWin32kLockedDown{"-win32kLockedDown",
                                              "win32klockeddown"};
#  endif  // defined(MOZ_SANDBOX)
static CommandLineArg<bool> sDisableDynamicDllBlocklist{
    "-disableDynamicBlocklist", "disabledynamicblocklist"};
#endif  // defined(XP_WIN)

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
static CommandLineArg<UniqueFileHandle> sSandboxReporter{"-sandboxReporter",
                                                         "sandboxreporter"};
static CommandLineArg<UniqueFileHandle> sChrootClient{"-chrootClient",
                                                      "chrootclient"};
#endif

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

}  // namespace geckoargs

}  // namespace mozilla

#endif  // mozilla_GeckoArgs_h
