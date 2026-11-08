/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Documentation for libpref is in modules/libpref/docs/index.rst.

#ifndef mozilla_Preferences_h
#define mozilla_Preferences_h

#ifndef MOZILLA_INTERNAL_API
#  error "This header is only usable from within libxul (MOZILLA_INTERNAL_API)."
#endif

#include "mozilla/Atomics.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MozPromise.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "nsCOMPtr.h"
#include "nsIObserver.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWeakReference.h"
#include "nsXULAppAPI.h"

class nsIFile;
class nsIPrefOverrideMap;

// The callback function will get passed the pref name which triggered the call
// and the void* data which was passed to the registered callback function.
typedef void (*PrefChangedFunc)(const char* aPref, void* aData);

class nsPrefBranch;

namespace mozilla {

void UnloadPrefsModule();

class PreferenceServiceReporter;

namespace dom {
class Pref;
class PrefValue;
}  // namespace dom

namespace ipc {
class FileDescriptor;
}  // namespace ipc

struct PrefsSizes;

// Xlib.h defines Bool as a macro constant. Don't try to define this enum if
// it's already been included.
#ifndef Bool

// Keep this in sync with PrefType in parser/src/lib.rs.
enum class PrefType : uint8_t {
  None = 0,  // only used when neither the default nor user value is set
  String = 1,
  Int = 2,
  Bool = 3,
};

#endif

#ifdef XP_UNIX
// We need to send two shared memory descriptors to every child process:
//
// 1) A read-only/write-protected snapshot of the initial state of the
//    preference database. This memory is shared between all processes, and
//    therefore cannot be modified once it has been created.
//
// 2) A set of changes on top of the snapshot, containing the current values of
//    all preferences which have changed since it was created.
//
// Since the second set will be different for every process, and the first set
// cannot be modified, it is unfortunately not possible to combine them into a
// single file descriptor.
//
// XXX: bug 1440207 is about improving how fixed fds such as this are used.
static const int kPrefsFileDescriptor = 8;
static const int kPrefMapFileDescriptor = 9;
#endif

// Keep this in sync with PrefType in parser/src/lib.rs.
enum class PrefValueKind : uint8_t { Default, User };

class Preferences final : public nsIPrefService,
                          public nsIObserver,
                          public nsIPrefBranch,
                          public nsSupportsWeakReference {
  friend class ::nsPrefBranch;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIPREFSERVICE
  NS_DECL_NSIPREFBRANCH
  NS_DECL_NSIOBSERVER

  Preferences();

  // Returns true if the Preferences service is available, false otherwise.
  static bool IsServiceAvailable();

  // Initialize user prefs from prefs.js/user.js
  static void InitializeUserPrefs();
  static void FinishInitializingUserPrefs();

  // Returns the singleton instance which is addreffed.
  static already_AddRefed<Preferences> GetInstanceForService();

  // Finallizes global members.
  static void Shutdown();

  // Returns shared pref service instance NOTE: not addreffed.
  static nsIPrefService* GetService() {
    NS_ENSURE_TRUE(InitStaticMembers(), nullptr);
    return sPreferences;
  }

  // Returns shared pref branch instance. NOTE: not addreffed.
  static nsIPrefBranch* GetRootBranch(
      PrefValueKind aKind = PrefValueKind::User);

  // Gets the type of the pref.
  static nsIPrefBranch::PreferenceType GetType(const char* aPrefName);

  // Fallible value getters. When `aKind` is `User` they will get the user
  // value if possible, and fall back to the default value otherwise.
  static nsresult GetBool(const char* aPrefName, bool* aResult,
                          PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetInt(const char* aPrefName, int32_t* aResult,
                         PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetUint(const char* aPrefName, uint32_t* aResult,
                          PrefValueKind aKind = PrefValueKind::User) {
    return GetInt(aPrefName, reinterpret_cast<int32_t*>(aResult), aKind);
  }
  static nsresult GetFloat(const char* aPrefName, float* aResult,
                           PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetCString(const char* aPrefName, nsACString& aResult,
                             PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetString(const char* aPrefName, nsAString& aResult,
                            PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetLocalizedCString(
      const char* aPrefName, nsACString& aResult,
      PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetLocalizedString(const char* aPrefName, nsAString& aResult,
                                     PrefValueKind aKind = PrefValueKind::User);
  static nsresult GetComplex(const char* aPrefName, const nsIID& aType,
                             void** aResult,
                             PrefValueKind aKind = PrefValueKind::User);

  // Infallible getters of user or default values, with fallback results on
  // failure. When `aKind` is `User` they will get the user value if possible,
  // and fall back to the default value otherwise.
  static bool GetBool(const char* aPrefName, bool aFallback = false,
                      PrefValueKind aKind = PrefValueKind::User);
  static int32_t GetInt(const char* aPrefName, int32_t aFallback = 0,
                        PrefValueKind aKind = PrefValueKind::User);
  static uint32_t GetUint(const char* aPrefName, uint32_t aFallback = 0,
                          PrefValueKind aKind = PrefValueKind::User);
  static float GetFloat(const char* aPrefName, float aFallback = 0.0f,
                        PrefValueKind aKind = PrefValueKind::User);

  // Value setters. These fail if run outside the parent process.

  static nsresult SetBool(const char* aPrefName, bool aValue,
                          PrefValueKind aKind = PrefValueKind::User);
  static nsresult SetInt(const char* aPrefName, int32_t aValue,
                         PrefValueKind aKind = PrefValueKind::User);
  static nsresult SetCString(const char* aPrefName, const nsACString& aValue,
                             PrefValueKind aKind = PrefValueKind::User);

  static nsresult SetUint(const char* aPrefName, uint32_t aValue,
                          PrefValueKind aKind = PrefValueKind::User) {
    return SetInt(aPrefName, static_cast<int32_t>(aValue), aKind);
  }

  static nsresult SetFloat(const char* aPrefName, float aValue,
                           PrefValueKind aKind = PrefValueKind::User) {
    nsAutoCString value;
    value.AppendFloat(aValue);
    return SetCString(aPrefName, value, aKind);
  }

  static nsresult SetCString(const char* aPrefName, const char* aValue,
                             PrefValueKind aKind = PrefValueKind::User) {
    return Preferences::SetCString(aPrefName, nsDependentCString(aValue),
                                   aKind);
  }

  static nsresult SetString(const char* aPrefName, const char16ptr_t aValue,
                            PrefValueKind aKind = PrefValueKind::User) {
    return Preferences::SetCString(aPrefName, NS_ConvertUTF16toUTF8(aValue),
                                   aKind);
  }

  static nsresult SetString(const char* aPrefName, const nsAString& aValue,
                            PrefValueKind aKind = PrefValueKind::User) {
    return Preferences::SetCString(aPrefName, NS_ConvertUTF16toUTF8(aValue),
                                   aKind);
  }

  static nsresult SetComplex(const char* aPrefName, const nsIID& aType,
                             nsISupports* aValue,
                             PrefValueKind aKind = PrefValueKind::User);

  static nsresult Lock(const char* aPrefName);
  static nsresult Unlock(const char* aPrefName);
  static bool IsLocked(const char* aPrefName);
  static bool IsSanitized(const char* aPrefName);

  // Clears user set pref. Fails if run outside the parent process.
  static nsresult ClearUser(const char* aPrefName);

  // Whether the pref has a user value or not.
  static bool HasUserValue(const char* aPref);

  // Whether the pref has a user value or not.
  static bool HasDefaultValue(const char* aPref);

  // Adds/Removes the observer for the root pref branch. See nsIPrefBranch.idl
  // for details.
  static nsresult AddStrongObserver(nsIObserver* aObserver,
                                    const nsACString& aPref);
  static nsresult AddWeakObserver(nsIObserver* aObserver,
                                  const nsACString& aPref);
  static nsresult RemoveObserver(nsIObserver* aObserver,
                                 const nsACString& aPref);

  template <int N>
  static nsresult AddStrongObserver(nsIObserver* aObserver,
                                    const char (&aPref)[N]) {
    return AddStrongObserver(aObserver, nsLiteralCString(aPref));
  }
  template <int N>
  static nsresult AddWeakObserver(nsIObserver* aObserver,
                                  const char (&aPref)[N]) {
    return AddWeakObserver(aObserver, nsLiteralCString(aPref));
  }
  template <int N>
  static nsresult RemoveObserver(nsIObserver* aObserver,
                                 const char (&aPref)[N]) {
    return RemoveObserver(aObserver, nsLiteralCString(aPref));
  }

  // Adds/Removes two or more observers for the root pref branch. Pass to
  // aPrefs an array of const char* whose last item is nullptr.
  // Note: All preference strings *must* be statically-allocated string
  // literals.
  static nsresult AddStrongObservers(nsIObserver* aObserver,
                                     const char* const* aPrefs);
  static nsresult AddWeakObservers(nsIObserver* aObserver,
                                   const char* const* aPrefs);
  static nsresult RemoveObservers(nsIObserver* aObserver,
                                  const char* const* aPrefs);

  // Registers/Unregisters the callback function for the aPref.
  template <typename T = void>
  static nsresult RegisterCallback(PrefChangedFunc aCallback,
                                   const nsACString& aPref,
                                   T* aClosure = nullptr) {
    return RegisterCallback(aCallback, aPref, static_cast<void*>(aClosure),
                            false);
  }

  template <typename T = void>
  static nsresult UnregisterCallback(PrefChangedFunc aCallback,
                                     const nsACString& aPref,
                                     T* aClosure = nullptr) {
    return UnregisterCallback(aCallback, aPref, static_cast<void*>(aClosure),
                              false);
  }

  // Like RegisterCallback, but also calls the callback immediately for
  // initialization.
  template <typename T = void>
  static nsresult RegisterCallbackAndCall(PrefChangedFunc aCallback,
                                          const nsACString& aPref,
                                          T* aClosure = nullptr) {
    nsresult rv = RegisterCallback(aCallback, aPref, aClosure, false);
    if (NS_SUCCEEDED(rv)) {
      (*aCallback)(PromiseFlatCString(aPref).get(),
                   static_cast<void*>(aClosure));
    }
    return rv;
  }

  // Like RegisterCallback, but registers a callback for a prefix of multiple
  // pref names, not a single pref name. Matching is done on dot-segment
  // boundaries: the callback fires for aPref itself and for any pref that
  // extends it by one or more whole '.'-delimited segments, but not for a pref
  // that merely shares a leading substring (e.g. a "foo.bar" prefix does not
  // match "foo.barbaz"). A trailing '.' is optional and is normalized away;
  // a domain ending in more than one '.' is rejected with
  // NS_ERROR_INVALID_ARG.
  template <typename T = void>
  static nsresult RegisterPrefixCallback(PrefChangedFunc aCallback,
                                         const nsACString& aPref,
                                         T* aClosure = nullptr) {
    return RegisterCallback(aCallback, aPref, static_cast<void*>(aClosure),
                            true);
  }

  // Like RegisterPrefixCallback, but also calls the callback immediately for
  // initialization.
  template <typename T = void>
  static nsresult RegisterPrefixCallbackAndCall(PrefChangedFunc aCallback,
                                                const nsACString& aPref,
                                                T* aClosure = nullptr) {
    nsresult rv = RegisterCallback(aCallback, aPref, aClosure, true);
    if (NS_SUCCEEDED(rv)) {
      (*aCallback)(PromiseFlatCString(aPref).get(),
                   static_cast<void*>(aClosure));
    }
    return rv;
  }

  // Unregister a callback registered with RegisterPrefixCallback or
  // RegisterPrefixCallbackAndCall.
  template <typename T = void>
  static nsresult UnregisterPrefixCallback(PrefChangedFunc aCallback,
                                           const nsACString& aPref,
                                           T* aClosure = nullptr) {
    return UnregisterCallback(aCallback, aPref, static_cast<void*>(aClosure),
                              true);
  }

  // Variants of the above which register a single callback to handle multiple
  // preferences.
  //
  // The array of preference names must be null terminated. It may be
  // dynamically allocated, but the caller is responsible for keeping it alive
  // until the callback is unregistered.
  //
  // Also note that the exact same aPrefs pointer must be passed to the
  // Unregister call as was passed to the Register call.
  template <typename T = void>
  static nsresult RegisterCallbacks(PrefChangedFunc aCallback,
                                    const char* const* aPrefs,
                                    T* aClosure = nullptr) {
    return RegisterCallbacks(aCallback, aPrefs, static_cast<void*>(aClosure),
                             false);
  }
  static nsresult RegisterCallbacksAndCall(PrefChangedFunc aCallback,
                                           const char* const* aPrefs,
                                           void* aClosure = nullptr);
  template <typename T = void>
  static nsresult UnregisterCallbacks(PrefChangedFunc aCallback,
                                      const char* const* aPrefs,
                                      T* aClosure = nullptr) {
    return UnregisterCallbacks(aCallback, aPrefs, static_cast<void*>(aClosure),
                               false);
  }
  template <typename T = void>
  static nsresult RegisterPrefixCallbacks(PrefChangedFunc aCallback,
                                          const char* const* aPrefs,
                                          T* aClosure = nullptr) {
    return RegisterCallbacks(aCallback, aPrefs, static_cast<void*>(aClosure),
                             true);
  }
  template <typename T = void>
  static nsresult UnregisterPrefixCallbacks(PrefChangedFunc aCallback,
                                            const char* const* aPrefs,
                                            T* aClosure = nullptr) {
    return UnregisterCallbacks(aCallback, aPrefs, static_cast<void*>(aClosure),
                               true);
  }

  template <int N, typename T = void>
  static nsresult RegisterCallback(PrefChangedFunc aCallback,
                                   const char (&aPref)[N],
                                   T* aClosure = nullptr) {
    return RegisterCallback(aCallback, nsLiteralCString(aPref),
                            static_cast<void*>(aClosure), false);
  }

  template <int N, typename T = void>
  static nsresult UnregisterCallback(PrefChangedFunc aCallback,
                                     const char (&aPref)[N],
                                     T* aClosure = nullptr) {
    return UnregisterCallback(aCallback, nsLiteralCString(aPref),
                              static_cast<void*>(aClosure), false);
  }

  template <int N, typename T = void>
  static nsresult RegisterCallbackAndCall(PrefChangedFunc aCallback,
                                          const char (&aPref)[N],
                                          T* aClosure = nullptr) {
    nsresult rv = RegisterCallback(aCallback, nsLiteralCString(aPref),
                                   static_cast<void*>(aClosure), false);
    if (NS_SUCCEEDED(rv)) {
      (*aCallback)(aPref, static_cast<void*>(aClosure));
    }
    return rv;
  }

  template <int N, typename T = void>
  static nsresult RegisterPrefixCallback(PrefChangedFunc aCallback,
                                         const char (&aPref)[N],
                                         T* aClosure = nullptr) {
    return RegisterCallback(aCallback, nsLiteralCString(aPref),
                            static_cast<void*>(aClosure), true);
  }

  template <int N, typename T = void>
  static nsresult RegisterPrefixCallbackAndCall(PrefChangedFunc aCallback,
                                                const char (&aPref)[N],
                                                T* aClosure = nullptr) {
    nsresult rv = RegisterCallback(aCallback, nsLiteralCString(aPref),
                                   static_cast<void*>(aClosure), true);
    if (NS_SUCCEEDED(rv)) {
      (*aCallback)(aPref, static_cast<void*>(aClosure));
    }
    return rv;
  }

  template <int N, typename T = void>
  static nsresult UnregisterPrefixCallback(PrefChangedFunc aCallback,
                                           const char (&aPref)[N],
                                           T* aClosure = nullptr) {
    return UnregisterCallback(aCallback, nsLiteralCString(aPref),
                              static_cast<void*>(aClosure), true);
  }

  // When a content process is created these methods are used to pass changed
  // prefs in bulk from the parent process, via shared memory.
  static void SerializePreferences(nsCString& aStr,
                                   bool aIsDestinationWebContentProcess);
  static void DeserializePreferences(const char* aStr, size_t aPrefsLen);

  static mozilla::ipc::ReadOnlySharedMemoryHandle EnsureSnapshot();
  static void InitSnapshot(const mozilla::ipc::ReadOnlySharedMemoryHandle&);

  // When a single pref is changed in the parent process, these methods are
  // used to pass the update to content processes.
  static void GetPreference(dom::Pref* aPref,
                            const GeckoProcessType aDestinationProcessType,
                            const dom::RemoteType& aDestinationRemoteType);
  static void SetPreference(const dom::Pref& aPref);

#ifdef DEBUG
  static bool ArePrefsInitedInContentProcess();
#endif

  static void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                     PrefsSizes& aSizes);

  static uint32_t GetCallbackCount();

  // Breakdown of the memory used by the pref callback tries. Exposed for the
  // CallbackTrie memory microbenchmark (gtest); not for production use.
  struct CallbackTrieStats {
    size_t mTotalBytes = 0;       // objects + domains + trie scaffolding
    size_t mObjectBytes = 0;      // CallbackNode objects
    size_t mDomainBytes = 0;      // per-callback domain strings
    size_t mTrieBytes = 0;        // nodes + child arrays + segment strings
    size_t mSegmentBytes = 0;     // segment strings only (subset of mTrieBytes)
    uint32_t mNodeCount = 0;      // CallbackTrieNodes (excluding the two roots)
    uint32_t mCallbackCount = 0;  // distinct live+dead CallbackNode objects
  };
  static CallbackTrieStats GetCallbackTrieStatsForTesting();

  // Test-only: synchronously reap expired weak observers and compact dead
  // callback nodes, which normally happens lazily on an idle task. Lets tests
  // deterministically run compaction that would otherwise race a notification.
  static void ReapCallbacksForTesting();

  static void HandleDirty();

  // Explicitly choosing synchronous or asynchronous (if allowed) preferences
  // file write. Only for the default file.  The guarantee for the "blocking"
  // is that when it returns, the file on disk reflect the current state of
  // preferences.
  nsresult SavePrefFileBlocking();
  nsresult SavePrefFileAsynchronous();

  // If this is false, only blocking writes, on main thread are allowed.
  bool AllowOffMainThreadSave();

 private:
  friend class PreferencesImpl;

  ~Preferences();

  static nsresult RegisterCallback(PrefChangedFunc aCallback,
                                   const nsACString& aPref, void* aClosure,
                                   bool aPrefixMatch);
  static nsresult UnregisterCallback(PrefChangedFunc aCallback,
                                     const nsACString& aPref, void* aClosure,
                                     bool aPrefixMatch);
  static nsresult RegisterCallbacks(PrefChangedFunc aCallback,
                                    const char* const* aPrefs, void* aClosure,
                                    bool aPrefixMatch);
  static nsresult UnregisterCallbacks(PrefChangedFunc aCallback,
                                      const char* const* aPrefs, void* aClosure,
                                      bool aPrefixMatch);

  static uint32_t UnregisterCallbacksForBranch(nsPrefBranch* aBranch);

  static StaticRefPtr<Preferences> sPreferences;
  static bool sShutdown;

  // Init static members. Returns true on success.
  static bool InitStaticMembers();
};

extern Atomic<bool, Relaxed> sOmitBlocklistedPrefValues;
extern Atomic<bool, Relaxed> sCrashOnBlocklistedPref;

bool IsPreferenceSanitized(const char* aPref);

const char kFissionEnforceBlockList[] =
    "fission.enforceBlocklistedPrefsInSubprocesses";
const char kFissionOmitBlockListValues[] =
    "fission.omitBlocklistedPrefsInSubprocesses";

void OnFissionBlocklistPrefChange(const char* aPref, void* aData);

}  // namespace mozilla

#endif  // mozilla_Preferences_h
