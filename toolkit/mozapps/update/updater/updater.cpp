/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 *  Manifest Format
 *  ---------------
 *
 *  contents = 1*( line )
 *  line     = method LWS *( param LWS ) CRLF
 *  CRLF     = "\r\n"
 *  LWS      = 1*( " " | "\t" )
 *
 *  Available methods for the manifest file:
 *
 *  updatev3.manifest
 *  -----------------
 *  method   = "add" | "add-if" | "add-if-not" | "patch" | "patch-if" |
 *             "remove" | "rmdir" | "rmrfdir" | type
 *
 *  'add-if-not' adds a file if it doesn't exist.
 *
 *  'type' is the update type (e.g. complete or partial) and when present MUST
 *  be the first entry in the update manifest. The type is used to support
 *  removing files that no longer exist when when applying a complete update by
 *  causing the actions defined in the precomplete file to be performed.
 *
 *  precomplete
 *  -----------
 *  method   = "remove" | "rmdir"
 */

#if defined(MOZ_BSPATCH)
#  include "bspatch.h"
#  include "crctable.h"
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
#  include "mozilla/moz_zucchini.h"
#endif  // defined(MOZ_ZUCCHINI)

#if !defined(MOZ_BSPATCH) && !defined(MOZ_ZUCCHINI)
#  error Updater enabled, but all supported patch formats are turned off.
#endif  // !defined(MOZ_BSPATCH) && !defined(MOZ_ZUCCHINI)

#include "progressui.h"
#include "archivereader.h"
#include "readstrings.h"
#include "updatererrors.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include "updatecommon.h"
#ifdef XP_MACOSX
#  include "UpdateSettingsUtil.h"
#  include "updaterfileutils_osx.h"
#endif  // XP_MACOSX

#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#ifdef XP_WIN
#  include "mozilla/Maybe.h"
#  include "mozilla/WinHeaderOnlyUtils.h"
#  include "mozilla/WinTokenUtils.h"
#  include <climits>
#endif  // XP_WIN

// Amount of the progress bar to use in each of the 4 update phases,
// should total 100.0. Estimates are for non-staged updates, because
// only they show the progress UI.
#define PROGRESS_PREPARE_SIZE 20.0f
#define PROGRESS_DRAFT_SIZE 65.0f
#define PROGRESS_EXECUTE_SIZE 10.0f
#define PROGRESS_FINISH_SIZE 5.0f

// Maximum amount of time in ms to wait for the parent process to close. The 30
// seconds is rather long but there have been bug reports where the parent
// process has exited after 10 seconds and it is better to give it a chance.
#define PARENT_WAIT 30000

#if defined(XP_MACOSX)
// These functions are defined in launchchild_osx.mm
void CleanupElevatedMacUpdate(bool aFailureOccurred);
bool IsOwnedByGroupAdmin(const char* aAppBundle);
bool IsRecursivelyWritable(const char* aPath);
void LaunchMacApp(int argc, const char** argv);
void LaunchMacPostProcess(const char* aAppBundle);
bool ObtainUpdaterArguments(int* aArgc, char*** aArgv,
                            MARChannelStringTable* aMARStrings);
bool ServeElevatedUpdate(int aArgc, const char** aArgv,
                         const char* aMARChannelID);
void SetGroupOwnershipAndPermissions(const char* aAppBundle);
bool PerformInstallationFromDMG(int argc, char** argv);
struct UpdateServerThreadArgs {
  int argc;
  const NS_tchar** argv;
  const char* marChannelID = "";
};
#endif

#ifndef _O_BINARY
#  define _O_BINARY 0
#endif

#ifndef NULL
#  define NULL (0)
#endif

#ifndef SSIZE_MAX
#  define SSIZE_MAX LONG_MAX
#endif

// We want to use execv to invoke the callback executable on platforms where
// we were launched using execv.  See nsUpdateDriver.cpp.
#if defined(XP_UNIX) && !defined(XP_MACOSX)
#  define USE_EXECV
#endif

#if defined(XP_OPENBSD)
#  define stat64 stat
#endif

#if defined(MOZ_VERIFY_MAR_SIGNATURE) && defined(MAR_NSS)
#  include "nss.h"
#  include "prerror.h"
#endif

#ifdef XP_WIN
#  ifdef MOZ_MAINTENANCE_SERVICE
#    include "registrycertificates.h"
#  endif
BOOL PathAppendSafe(LPWSTR base, LPCWSTR extra);
BOOL PathGetSiblingFilePath(LPWSTR destinationBuffer, LPCWSTR siblingFilePath,
                            LPCWSTR newFileName);
#  include "updatehelper.h"

// Closes the handle if valid and if this is the second updater instance,
// return with the return code specified. As `gIsSecondInvocation` alludes to,
// this is used to guard things like launching the callback application, since
// only the first updater invocation should do that.
// The passed handle is meant to be the handle to the "update in progress" lock
// so that we close it when we are done updating.
#  define EXIT_IF_SECOND_UPDATER_INSTANCE(handle, retCode)                    \
    {                                                                         \
      if (handle != INVALID_HANDLE_VALUE) {                                   \
        CloseHandle(handle);                                                  \
      }                                                                       \
      if (gInvocation == UpdaterInvocation::Second) {                         \
        LOG(("%s:%d - Returning early. This is the second updater instance.", \
             __func__, __LINE__));                                            \
        return retCode;                                                       \
      }                                                                       \
    }
#endif

//-----------------------------------------------------------------------------

/**
 * This enum and its related functions are intended for interpreting the passed
 * parameter and using it to determine whether this is the first or second
 * invocation of the updater.
 */
enum class UpdaterInvocation {
  // The initial invocation of the updater. This may apply the update, or it may
  // start the second invocation of the updater to update depending on whether
  // elevation is required.
  // This invocation always does all modifications of the update directory and
  // calls the callback application, even if another updater is launched.
  First,
  // The second invocation of the updater. This basically applies the update to
  // the installation directory, calls PostUpdate (on Windows) and exits.
  Second,
  // It cannot be determined that we are doing either of the above invocations.
  // This generally represents an uninitialized value or an error.
  Unknown,
};

/**
 * This enum is used to indicate on Windows why the post-updater is running.
 *
 * If the updater elevates, the post-updater runs twice: once as the system
 * user, and once as the current user. There's some logic that we don't want to
 * do in the second case; for an example, see bug 2004959.
 *
 * Notice that the CurrentUser case only runs in installations that elevate
 * (via UAC or the Maintenance Service) to perform the update; therefore,
 * nothing should run _only_ when the postupdate target is 'CurrentUser'.
 * Ideally it would do nothing, see bug 2030813.
 */
enum class PostUpdateTarget {
  // The post-updater is being run for the entire installation.
  Installation,
  // The post-updater has just finished the elevated update and is running as
  // the user who prompted the update.
  CurrentUser,
};

/**
 * Returns a human-readable representation of an `UpdaterInvocation`.
 */
const char* getUpdaterInvocationString(UpdaterInvocation value) {
  switch (value) {
    case UpdaterInvocation::First:
      return "UpdaterInvocation::First";
    case UpdaterInvocation::Second:
      return "UpdaterInvocation::Second";
    case UpdaterInvocation::Unknown:
      return "UpdaterInvocation::Unknown";
  }
  MOZ_CRASH("impossible value for UpdaterInvocation");
}

const NS_tchar* firstUpdateInvocationArg = NS_T("first");
const NS_tchar* secondUpdateInvocationArg = NS_T("second");

/**
 * Gets which updater invocation this is based on the value passed to this
 * function by the caller.
 */
static UpdaterInvocation getUpdaterInvocationFromArg(const NS_tchar* argument) {
  if (NS_tstrcmp(argument, firstUpdateInvocationArg) == 0) {
    return UpdaterInvocation::First;
  }
  if (NS_tstrcmp(argument, secondUpdateInvocationArg) == 0) {
    return UpdaterInvocation::Second;
  }
  return UpdaterInvocation::Unknown;
}

//-----------------------------------------------------------------------------

// A simple stack based container for a FILE struct that closes the
// file descriptor from its destructor.
class AutoFile {
 public:
  explicit AutoFile(FILE* file = nullptr) : mFile(file) {}

  ~AutoFile() { close(); }

  AutoFile& operator=(FILE* file) {
    close();
    mFile = file;
    return *this;
  }

  operator FILE*() { return mFile; }

  FILE* get() { return mFile; }

 private:
  FILE* mFile;

  void close() {
    if (mFile != nullptr) {
      int rv = fclose(mFile);
      if (rv != 0) {
        LOG(("File close did not execute successfully"));
      }
      mFile = nullptr;
    }
  }
};

//-----------------------------------------------------------------------------

#ifdef XP_MACOSX

// Just a simple class that sets a umask value in its constructor and resets
// it in its destructor.
class UmaskContext {
 public:
  explicit UmaskContext(mode_t umaskToSet);
  ~UmaskContext();

 private:
  mode_t mPreviousUmask;
};

UmaskContext::UmaskContext(mode_t umaskToSet) {
  mPreviousUmask = umask(umaskToSet);
}

UmaskContext::~UmaskContext() { umask(mPreviousUmask); }

#endif

//-----------------------------------------------------------------------------

typedef void (*ThreadFunc)(void* param);

#ifdef XP_WIN
#  include <process.h>

class Thread {
 public:
  int Run(ThreadFunc func, void* param) {
    mThreadFunc = func;
    mThreadParam = param;

    unsigned int threadID;

    mThread =
        (HANDLE)_beginthreadex(nullptr, 0, ThreadMain, this, 0, &threadID);

    return mThread ? 0 : -1;
  }
  int Join() {
    WaitForSingleObject(mThread, INFINITE);
    CloseHandle(mThread);
    return 0;
  }

 private:
  static unsigned __stdcall ThreadMain(void* p) {
    Thread* self = (Thread*)p;
    self->mThreadFunc(self->mThreadParam);
    return 0;
  }
  HANDLE mThread;
  ThreadFunc mThreadFunc;
  void* mThreadParam;
};

#elif defined(XP_UNIX)
#  include <pthread.h>

class Thread {
 public:
  int Run(ThreadFunc func, void* param) {
    return pthread_create(&thr, nullptr, (void* (*)(void*))func, param);
  }
  int Join() {
    void* result;
    return pthread_join(thr, &result);
  }

 private:
  pthread_t thr;
};

#else
#  error "Unsupported platform"
#endif

//-----------------------------------------------------------------------------

static NS_tchar gPatchDirPath[MAXPATHLEN];
static NS_tchar gInstallDirPath[MAXPATHLEN];
static NS_tchar gWorkingDirPath[MAXPATHLEN];
constinit static ArchiveReader gArchiveReader;
static bool gSucceeded = false;
static bool sStagedUpdate = false;
static bool sReplaceRequest = false;
static bool sUsingService = false;
// When the updater needs to elevate, we generally run the updater again with
// elevation. These two invocations differ in many important ways. The elevated
// updater doesn't touch any files that don't require that elevation, it
// basically just changes the installation directory, runs PostUpdate
// (on Windows), and exits to let the first updater invocation finalize the
// update (write its own logs, conditionally move the elevated updater
// logs/status to the update directory, call the callback application, etc).
static UpdaterInvocation gInvocation = UpdaterInvocation::Unknown;

// `argv` indices for standard invocation. Does not apply to other methods of
// invocation including when `--openAppBundle`, or `-dmgInstall` are used.
// Note that `argv[1]` is the argument version, which is needed by the MMS, but
// not by the updater since it is guaranteed to be the same version as the
// application launching it.
static const int kPatchDirIndex = 2;
static const int kInstallDirIndex = 3;
static const int kApplyToDirIndex = 4;
static const int kWhichInvocationIndex = 5;
// Note that this is the first optional argument.
static const int kWaitPidIndex = 6;
static const int kCallbackWorkingDirIndex = 7;
// This indicates the entry in `argv` that is the callback binary path. All
// arguments after this one are treated as arguments to the callback.
static const int kCallbackIndex = 8;

// This string contains the MAR channel IDs that are later extracted by one of
// the `ReadMARChannelIDsFrom` variants.
constinit static MARChannelStringTable gMARStrings;

// Normally, we run updates as a result of user action (the user started Firefox
// or clicked a "Restart to Update" button). But there are some cases when
// we are not:
// a) The callback app is a background task. If true then the updater is
//    likely being run as part of a background task.
//    The updater could be run with no callback, but this only happens
//    when performing a staged update (see calls to ProcessUpdates), and there
//    are already checks for sStagedUpdate when showing UI or elevating.
// b) The environment variable MOZ_APP_SILENT_START is set and not empty. This
//    is set, for instance, on macOS when Firefox had no windows open for a
//    while and restarted to apply updates.
//
// In these cases, the update should be installed silently, so we shouldn't:
// a) show progress UI
// b) prompt for elevation
static bool sUpdateSilently = false;

#ifdef XP_WIN
static NS_tchar gCallbackRelPath[MAXPATHLEN];
static NS_tchar gCallbackBackupPath[MAXPATHLEN];
static NS_tchar gDeleteDirPath[MAXPATHLEN];

// Whether to copy the update-elevated.log and update.status file to the update
// patch directory from a secure directory.
static bool gCopyOutputFiles = false;
#endif

static const NS_tchar kWhitespace[] = NS_T(" \t");
static const NS_tchar kNL[] = NS_T("\r\n");
static const NS_tchar kQuote[] = NS_T("\"");

static inline size_t mmin(size_t a, size_t b) { return (a > b) ? b : a; }

static NS_tchar* mstrtok(const NS_tchar* delims, NS_tchar** str) {
  if (!*str || !**str) {
    *str = nullptr;
    return nullptr;
  }

  // skip leading "whitespace"
  NS_tchar* ret = *str;
  const NS_tchar* d;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*ret == *d) {
        ++ret;
        break;
      }
    }
  } while (*d);

  if (!*ret) {
    *str = ret;
    return nullptr;
  }

  NS_tchar* i = ret;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*i == *d) {
        *i = NS_T('\0');
        *str = ++i;
        return ret;
      }
    }
    ++i;
  } while (*i);

  *str = nullptr;
  return ret;
}

#if defined(TEST_UPDATER) || defined(XP_WIN) || defined(XP_MACOSX)
static bool EnvHasValue(const char* name) {
  const char* val = getenv(name);
  return (val && *val);
}
#endif

static const NS_tchar* UpdateLogFilename() {
  if (gInvocation == UpdaterInvocation::Second) {
    return NS_T("update-elevated.log");
  }
  return NS_T("update.log");
}

#ifdef XP_WIN
/**
 * Obtains the update ID from the secure id file located in secure output
 * directory.
 *
 * @param  outBuf
 *         A buffer of size UUID_LEN (e.g. 37) to store the result. The uuid is
 *         36 characters in length and 1 more for null termination.
 * @return true if successful
 */
bool GetSecureID(char* outBuf) {
  NS_tchar idFilePath[MAX_PATH + 1] = {L'\0'};
  if (!GetSecureOutputFilePath(gPatchDirPath, L".id", idFilePath)) {
    return false;
  }

  AutoFile idFile(NS_tfopen(idFilePath, NS_T("rb")));
  if (idFile == nullptr) {
    return false;
  }

  size_t read = fread(outBuf, UUID_LEN - 1, 1, idFile);
  if (read != 1) {
    return false;
  }

  outBuf[UUID_LEN - 1] = '\0';
  return true;
}
#endif

/**
 * Calls LogFinish for the update log. On Windows, the unelevated updater copies
 * the update status file and the update log file that were written by the
 * elevated updater from the secure directory to the update patch directory.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because this function copies the update status file for the elevated
 *       updater and writing the status file after calling output_finish will
 *       overwrite it.
 */
static void output_finish() {
  LogFinish();
#ifdef XP_WIN
  if (gCopyOutputFiles) {
    NS_tchar srcStatusPath[MAXPATHLEN + 1] = {NS_T('\0')};
    if (GetSecureOutputFilePath(gPatchDirPath, L".status", srcStatusPath)) {
      NS_tchar dstStatusPath[MAXPATHLEN + 1] = {NS_T('\0')};
      NS_tsnprintf(dstStatusPath,
                   sizeof(dstStatusPath) / sizeof(dstStatusPath[0]),
                   NS_T("%s\\update.status"), gPatchDirPath);
      CopyFileW(srcStatusPath, dstStatusPath, false);
    }

    NS_tchar srcLogPath[MAXPATHLEN + 1] = {NS_T('\0')};
    if (GetSecureOutputFilePath(gPatchDirPath, L".log", srcLogPath)) {
      NS_tchar dstLogPath[MAXPATHLEN + 1] = {NS_T('\0')};
      // Unconditionally use "update-elevated.log" here rather than
      // `UpdateLogFilename` since (a) secure output files are only created by
      // elevated instances and (b) the copying of the secure output file is
      // done by the unelevated instance, so `UpdateLogFilename` will return
      // the wrong thing for this.
      NS_tsnprintf(dstLogPath, sizeof(dstLogPath) / sizeof(dstLogPath[0]),
                   NS_T("%s\\update-elevated.log"), gPatchDirPath);
      CopyFileW(srcLogPath, dstLogPath, false);
    }
  }
#endif
}

/**
 * Coverts a relative update path to a full path.
 *
 * @param  relpath
 *         The relative path to convert to a full path.
 * @return valid filesystem full path or nullptr if memory allocation fails.
 */
static NS_tchar* get_full_path(const NS_tchar* relpath) {
  NS_tchar* destpath = sStagedUpdate ? gWorkingDirPath : gInstallDirPath;
  size_t lendestpath = NS_tstrlen(destpath);
  size_t lenrelpath = NS_tstrlen(relpath);
  NS_tchar* s = new NS_tchar[lendestpath + lenrelpath + 2];

  NS_tchar* c = s;

  NS_tstrcpy(c, destpath);
  c += lendestpath;
  NS_tstrcat(c, NS_T("/"));
  c++;

  NS_tstrcat(c, relpath);
  c += lenrelpath;
  *c = NS_T('\0');
  return s;
}

/**
 * Converts a full update path into a relative path; reverses get_full_path.
 *
 * @param  fullpath
 *         The absolute path to convert into a relative path.
 * return pointer to the location within fullpath where the relative path starts
 *        or fullpath itself if it already looks relative.
 */
#ifndef XP_WIN
static const NS_tchar* get_relative_path(const NS_tchar* fullpath) {
  if (fullpath[0] != '/') {
    return fullpath;
  }

  NS_tchar* prefix = sStagedUpdate ? gWorkingDirPath : gInstallDirPath;

  // If the path isn't long enough to be absolute, return it as-is.
  if (NS_tstrlen(fullpath) <= NS_tstrlen(prefix)) {
    return fullpath;
  }

  return fullpath + NS_tstrlen(prefix) + 1;
}
#endif

/**
 * Gets the platform specific path and performs simple checks to the path. If
 * the path checks don't pass nullptr will be returned.
 *
 * @param  line
 *         The line from the manifest that contains the path.
 * @param  isdir
 *         Whether the path is a directory path. Defaults to false.
 * @return valid filesystem path or nullptr if the path checks fail.
 */
static NS_tchar* get_valid_path(NS_tchar** line, bool isdir = false) {
  NS_tchar* path = mstrtok(kQuote, line);
  if (!path) {
    LOG(("get_valid_path: unable to determine path: " LOG_S, *line));
    return nullptr;
  }

  // All paths must be relative from the current working directory
  if (path[0] == NS_T('/')) {
    LOG(("get_valid_path: path must be relative: " LOG_S, path));
    return nullptr;
  }

#ifdef XP_WIN
  // All paths must be relative from the current working directory
  if (path[0] == NS_T('\\') || path[1] == NS_T(':')) {
    LOG(("get_valid_path: path must be relative: " LOG_S, path));
    return nullptr;
  }
#endif

  if (isdir) {
    // Directory paths must have a trailing forward slash.
    if (path[NS_tstrlen(path) - 1] != NS_T('/')) {
      LOG(
          ("get_valid_path: directory paths must have a trailing forward "
           "slash: " LOG_S,
           path));
      return nullptr;
    }

    // Remove the trailing forward slash because stat on Windows will return
    // ENOENT if the path has a trailing slash.
    path[NS_tstrlen(path) - 1] = NS_T('\0');
  }

  // Don't allow relative paths that resolve to a parent directory.
  if (NS_tstrstr(path, NS_T("..")) != nullptr) {
    LOG(("get_valid_path: paths must not contain '..': " LOG_S, path));
    return nullptr;
  }

  return path;
}

/*
 * Gets a quoted path. The return value is malloc'd and it is the responsibility
 * of the caller to free it.
 *
 * @param  path
 *         The path to quote.
 * @return On success the quoted path and nullptr otherwise.
 */
static NS_tchar* get_quoted_path(const NS_tchar* path) {
  size_t lenQuote = NS_tstrlen(kQuote);
  size_t lenPath = NS_tstrlen(path);
  size_t len = lenQuote + lenPath + lenQuote + 1;

  NS_tchar* s = (NS_tchar*)malloc(len * sizeof(NS_tchar));
  if (!s) {
    return nullptr;
  }

  NS_tchar* c = s;
  NS_tstrcpy(c, kQuote);
  c += lenQuote;
  NS_tstrcat(c, path);
  c += lenPath;
  NS_tstrcat(c, kQuote);
  c += lenQuote;
  *c = NS_T('\0');
  return s;
}

static void ensure_write_permissions(const NS_tchar* path) {
#ifdef XP_WIN
  (void)_wchmod(path, _S_IREAD | _S_IWRITE);
#else
  struct stat fs;
  if (!stat(path, &fs) && !(fs.st_mode & S_IWUSR)) {
    (void)chmod(path, fs.st_mode | S_IWUSR);
  }
#endif
}

static int ensure_remove(const NS_tchar* path) {
#if defined(TEST_UPDATER) && defined(XP_WIN)
  // Matching is on the leaf name only, and exact: a test that makes the removal
  // of a file fail must not also make the removal of, say, its backup or its
  // draft fail.
  const wchar_t* failName = _wgetenv(L"MOZ_TEST_ENSURE_REMOVE_FAIL");
  if (failName && *failName) {
    const wchar_t* leaf = path;
    for (const wchar_t* c = path; *c; ++c) {
      if (*c == L'\\' || *c == L'/') {
        leaf = c + 1;
      }
    }
    if (!wcscmp(leaf, failName)) {
      LOG(
          ("ensure_remove: TEST fault injection, pretending removal "
           "failed: " LOG_S,
           path));
      return -1;
    }
  }
#endif
  ensure_write_permissions(path);
  int rv = NS_tremove(path);
  if (rv) {
    LOG(("ensure_remove: failed to remove file: " LOG_S ", rv: %d, err: %d",
         path, rv, errno));
  }
  return rv;
}

// Remove the directory pointed to by path and all of its files and
// sub-directories.
static int ensure_remove_recursive(const NS_tchar* path,
                                   bool continueEnumOnFailure = false) {
  // We use lstat rather than stat here so that we can successfully remove
  // symlinks.
  struct NS_tstat_t sInfo;
  int rv = NS_tlstat(path, &sInfo);
  if (rv) {
    // This error is benign
    return rv;
  }
  if (!S_ISDIR(sInfo.st_mode)) {
    return ensure_remove(path);
  }

  NS_tDIR* dir;
  NS_tdirent* entry;

  dir = NS_topendir(path);
  if (!dir) {
    LOG(("ensure_remove_recursive: unable to open directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return rv;
  }

  while ((entry = NS_treaddir(dir)) != nullptr) {
    if (NS_tstrcmp(entry->d_name, NS_T(".")) &&
        NS_tstrcmp(entry->d_name, NS_T(".."))) {
      NS_tchar childPath[MAXPATHLEN];
      NS_tsnprintf(childPath, sizeof(childPath) / sizeof(childPath[0]),
                   NS_T("%s/%s"), path, entry->d_name);
      rv = ensure_remove_recursive(childPath);
      if (rv && !continueEnumOnFailure) {
        break;
      }
    }
  }

  NS_tclosedir(dir);

  if (rv == OK) {
    ensure_write_permissions(path);
    rv = NS_trmdir(path);
    if (rv) {
      LOG(("ensure_remove_recursive: unable to remove directory: " LOG_S
           ", rv: %d, err: %d",
           path, rv, errno));
    }
  }
  return rv;
}

static bool is_read_only(const NS_tchar* flags) {
  size_t length = NS_tstrlen(flags);
  if (length == 0) {
    return false;
  }

  // Make sure the string begins with "r"
  if (flags[0] != NS_T('r')) {
    return false;
  }

  // Look for "r+" or "r+b"
  if (length > 1 && flags[1] == NS_T('+')) {
    return false;
  }

  // Look for "rb+"
  if (NS_tstrcmp(flags, NS_T("rb+")) == 0) {
    return false;
  }

  return true;
}

static FILE* ensure_open(const NS_tchar* path, const NS_tchar* flags,
                         unsigned int options) {
  ensure_write_permissions(path);
  FILE* f = NS_tfopen(path, flags);
  if (is_read_only(flags)) {
    // Don't attempt to modify the file permissions if the file is being opened
    // in read-only mode.
    return f;
  }
  if (NS_tchmod(path, options) != 0) {
    if (f != nullptr) {
      fclose(f);
    }
    return nullptr;
  }
  struct NS_tstat_t ss;
  if (NS_tstat(path, &ss) != 0 || ss.st_mode != options) {
    if (f != nullptr) {
      fclose(f);
    }
    return nullptr;
  }
  return f;
}

// Ensure that the directory containing this file exists.
static int ensure_parent_dir(const NS_tchar* path) {
  int rv = OK;

  NS_tchar* slash = (NS_tchar*)NS_tstrrchr(path, NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = ensure_parent_dir(path);
    // Only attempt to create the directory if we're not at the root
    if (rv == OK && *path) {
      rv = NS_tmkdir(path, 0755);
      // If the directory already exists, then ignore the error.
      if (rv < 0 && errno != EEXIST) {
        LOG(("ensure_parent_dir: failed to create directory: " LOG_S ", "
             "err: %d",
             path, errno));
        rv = WRITE_ERROR;
      } else {
        rv = OK;
      }
    }
    *slash = NS_T('/');
  }
  return rv;
}

#ifdef XP_UNIX
static int ensure_copy_symlink(const NS_tchar* path, const NS_tchar* dest) {
  // Copy symlinks by creating a new symlink to the same target
  NS_tchar target[MAXPATHLEN + 1] = {NS_T('\0')};
  int rv = readlink(path, target, MAXPATHLEN);
  if (rv == -1) {
    LOG(("ensure_copy_symlink: failed to read the link: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }
  rv = symlink(target, dest);
  if (rv == -1) {
    LOG(("ensure_copy_symlink: failed to create the new link: " LOG_S
         ", target: " LOG_S " err: %d",
         dest, target, errno));
    return READ_ERROR;
  }
  return 0;
}
#endif

// Copy the file named path onto a new file named dest.
static int ensure_copy(const NS_tchar* path, const NS_tchar* dest) {
#ifdef XP_WIN
  // Fast path for Windows
  bool result = CopyFileW(path, dest, false);
  if (!result) {
    LOG(("ensure_copy: failed to copy the file " LOG_S " over to " LOG_S
         ", lasterr: %lx",
         path, dest, GetLastError()));
    return WRITE_ERROR_FILE_COPY;
  }
  return OK;
#else
  struct NS_tstat_t ss;
  int rv = NS_tlstat(path, &ss);
  if (rv) {
    LOG(("ensure_copy: failed to read file status info: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }

#  ifdef XP_UNIX
  if (S_ISLNK(ss.st_mode)) {
    return ensure_copy_symlink(path, dest);
  }
#  endif

  AutoFile infile(ensure_open(path, NS_T("rb"), ss.st_mode));
  if (!infile) {
    LOG(("ensure_copy: failed to open the file for reading: " LOG_S ", err: %d",
         path, errno));
    return READ_ERROR;
  }
  AutoFile outfile(ensure_open(dest, NS_T("wb"), ss.st_mode));
  if (!outfile) {
    LOG(("ensure_copy: failed to open the file for writing: " LOG_S ", err: %d",
         dest, errno));
    return WRITE_ERROR;
  }

  // This block size was chosen pretty arbitrarily but seems like a reasonable
  // compromise. For example, the optimal block size on a modern OS X machine
  // is 100k */
  const int blockSize = 32 * 1024;
  void* buffer = malloc(blockSize);
  if (!buffer) {
    return UPDATER_MEM_ERROR;
  }

  while (!feof(infile.get())) {
    size_t read = fread(buffer, 1, blockSize, infile);
    if (ferror(infile.get())) {
      LOG(("ensure_copy: failed to read the file: " LOG_S ", err: %d", path,
           errno));
      free(buffer);
      return READ_ERROR;
    }

    size_t written = 0;

    while (written < read) {
      size_t chunkWritten = fwrite(buffer, 1, read - written, outfile);
      if (chunkWritten <= 0) {
        LOG(("ensure_copy: failed to write the file: " LOG_S ", err: %d", dest,
             errno));
        free(buffer);
        return WRITE_ERROR_FILE_COPY;
      }

      written += chunkWritten;
    }
  }

  rv = NS_tchmod(dest, ss.st_mode);

  free(buffer);
  return rv;
#endif
}

// Returns true if the path is that of a draft file, that is, a file that the
// Draft phase of a non-staged update writes new contents to.
static bool is_draft_path(const NS_tchar* path) {
  size_t pathLen = NS_tstrlen(path);
  size_t extLen = NS_tstrlen(DRAFT_EXT);
  return pathLen > extLen && !NS_tstricmp(path + pathLen - extLen, DRAFT_EXT);
}

template <unsigned N>
struct copy_recursive_skiplist {
  NS_tchar paths[N][MAXPATHLEN];

  void append(unsigned index, const NS_tchar* path, const NS_tchar* suffix) {
    NS_tsnprintf(paths[index], MAXPATHLEN, NS_T("%s/%s"), path, suffix);
  }

  bool find(const NS_tchar* path) {
    for (int i = 0; i < static_cast<int>(N); ++i) {
      if (!NS_tstricmp(paths[i], path)) {
        return true;
      }
    }
    return false;
  }
};

// Copy all of the files and subdirectories under path to a new directory named
// dest. The path names in the skiplist will be skipped and will not be copied.
// Draft files are always skipped: a draft that a crashed updater instance left
// behind is not part of the installation, so it must not be copied into an
// updated one, where nothing would ever get rid of it.
template <unsigned N>
static int ensure_copy_recursive(const NS_tchar* path, const NS_tchar* dest,
                                 copy_recursive_skiplist<N>& skiplist) {
  struct NS_tstat_t sInfo;
  int rv = NS_tlstat(path, &sInfo);
  if (rv) {
    LOG(("ensure_copy_recursive: path doesn't exist: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return READ_ERROR;
  }

#ifdef XP_UNIX
  if (S_ISLNK(sInfo.st_mode)) {
    return ensure_copy_symlink(path, dest);
  }
#endif

  if (!S_ISDIR(sInfo.st_mode)) {
    return ensure_copy(path, dest);
  }

  rv = NS_tmkdir(dest, sInfo.st_mode);
  if (rv < 0 && errno != EEXIST) {
    LOG(("ensure_copy_recursive: could not create destination directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return WRITE_ERROR;
  }

  NS_tDIR* dir;
  NS_tdirent* entry;

  dir = NS_topendir(path);
  if (!dir) {
    LOG(("ensure_copy_recursive: path is not a directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return READ_ERROR;
  }

  while ((entry = NS_treaddir(dir)) != nullptr) {
    if (NS_tstrcmp(entry->d_name, NS_T(".")) &&
        NS_tstrcmp(entry->d_name, NS_T(".."))) {
      NS_tchar childPath[MAXPATHLEN];
      NS_tsnprintf(childPath, sizeof(childPath) / sizeof(childPath[0]),
                   NS_T("%s/%s"), path, entry->d_name);
      if (skiplist.find(childPath) || is_draft_path(entry->d_name)) {
        continue;
      }
      NS_tchar childPathDest[MAXPATHLEN];
      NS_tsnprintf(childPathDest,
                   sizeof(childPathDest) / sizeof(childPathDest[0]),
                   NS_T("%s/%s"), dest, entry->d_name);
      rv = ensure_copy_recursive(childPath, childPathDest, skiplist);
      if (rv) {
        break;
      }
    }
  }
  NS_tclosedir(dir);
  return rv;
}

#ifdef XP_WIN
// Moves a file that could not be removed into the deletion directory, and
// schedules it for removal on the next OS reboot.
static int remove_on_reboot(const NS_tchar* path) {
  if (sStagedUpdate || sReplaceRequest) {
    return WRITE_ERROR_DELETE_FILE;
  }

  NS_tchar deletePath[MAXPATHLEN + 1];
  if (!GetUUIDTempFilePath(gDeleteDirPath, L"moz", deletePath)) {
    LOG(("remove_on_reboot: failed to generate a temporary file path"));
    return WRITE_ERROR_DELETE_FILE;
  }
  if (NS_trename(path, deletePath) != 0) {
    LOG(("remove_on_reboot: failed to move file out of the way: " LOG_S
         ", err: %d",
         path, errno));
    return WRITE_ERROR_DELETE_FILE;
  }

  // The MoveFileEx call to remove the file on OS reboot will fail if the
  // process doesn't have write access to the HKEY_LOCAL_MACHINE registry key
  // but this is ok since the installer / uninstaller will delete the
  // directory containing the file along with its contents after an update is
  // applied, on reinstall, and on uninstall.
  if (MoveFileEx(deletePath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
    LOG(("remove_on_reboot: file will be removed on OS reboot: " LOG_S, path));
  } else {
    LOG(
        ("remove_on_reboot: failed to schedule OS reboot removal of "
         "file: " LOG_S,
         path));
  }
  return OK;
}
#endif

// Renames the specified file to the new file specified. If the destination file
// exists it is removed.
static int rename_file(const NS_tchar* spath, const NS_tchar* dpath,
                       bool allowDirs = false) {
  int rv = ensure_parent_dir(dpath);
  if (rv) {
    return rv;
  }

  struct NS_tstat_t spathInfo;
  rv = NS_tstat(spath, &spathInfo);
  if (rv) {
    LOG(("rename_file: failed to read file status info: " LOG_S ", "
         "err: %d",
         spath, errno));
    return READ_ERROR;
  }

  if (!S_ISREG(spathInfo.st_mode)) {
    if (allowDirs && !S_ISDIR(spathInfo.st_mode)) {
      LOG(("rename_file: path present, but not a file: " LOG_S ", err: %d",
           spath, errno));
      return RENAME_ERROR_EXPECTED_FILE;
    }
    LOG(("rename_file: proceeding to rename the directory"));
  }

  if (!NS_taccess(dpath, F_OK)) {
    rv = ensure_remove(dpath);
    if (rv) {
      LOG(
          ("rename_file: destination file exists and could not be "
           "removed: " LOG_S,
           dpath));
#ifdef XP_WIN
      rv = remove_on_reboot(dpath);
    }
    if (rv) {
#endif
      return WRITE_ERROR_DELETE_FILE;
    }
  }

  if (NS_trename(spath, dpath) != 0) {
    LOG(("rename_file: failed to rename file - src: " LOG_S ", "
         "dst:" LOG_S ", err: %d",
         spath, dpath, errno));
    return WRITE_ERROR;
  }

  return OK;
}

#ifdef XP_WIN
// Remove the directory pointed to by path and all of its files and
// sub-directories. If a file is in use move it to the tobedeleted directory
// and attempt to schedule removal of the file on reboot
static int remove_recursive_on_reboot(const NS_tchar* path,
                                      const NS_tchar* deleteDir) {
  struct NS_tstat_t sInfo;
  int rv = NS_tlstat(path, &sInfo);
  if (rv) {
    // This error is benign
    return rv;
  }

  if (!S_ISDIR(sInfo.st_mode)) {
    NS_tchar tmpDeleteFile[MAXPATHLEN + 1];
    if (!GetUUIDTempFilePath(deleteDir, L"rep", tmpDeleteFile)) {
      LOG(
          ("remove_recursive_on_reboot: failed to generate a temporary file "
           "path"));
      return WRITE_ERROR_DELETE_FILE;
    }
    if (NS_tremove(tmpDeleteFile) && errno != ENOENT) {
      LOG(("remove_recursive_on_reboot: failed to remove temporary file: " LOG_S
           ", err: %d",
           tmpDeleteFile, errno));
    }
    rv = rename_file(path, tmpDeleteFile, false);
    if (MoveFileEx(rv ? path : tmpDeleteFile, nullptr,
                   MOVEFILE_DELAY_UNTIL_REBOOT)) {
      LOG(
          ("remove_recursive_on_reboot: file will be removed on OS "
           "reboot: " LOG_S,
           rv ? path : tmpDeleteFile));
    } else {
      LOG((
          "remove_recursive_on_reboot: failed to schedule OS reboot removal of "
          "file: " LOG_S,
          rv ? path : tmpDeleteFile));
    }
    return rv;
  }

  NS_tDIR* dir;
  NS_tdirent* entry;

  dir = NS_topendir(path);
  if (!dir) {
    LOG(("remove_recursive_on_reboot: unable to open directory: " LOG_S
         ", rv: %d, err: %d",
         path, rv, errno));
    return rv;
  }

  while ((entry = NS_treaddir(dir)) != nullptr) {
    if (NS_tstrcmp(entry->d_name, NS_T(".")) &&
        NS_tstrcmp(entry->d_name, NS_T(".."))) {
      NS_tchar childPath[MAXPATHLEN];
      NS_tsnprintf(childPath, sizeof(childPath) / sizeof(childPath[0]),
                   NS_T("%s/%s"), path, entry->d_name);
      // There is no need to check the return value of this call since this
      // function is only called after an update is successful and there is not
      // much that can be done to recover if it isn't successful. There is also
      // no need to log the value since it will have already been logged.
      remove_recursive_on_reboot(childPath, deleteDir);
    }
  }

  NS_tclosedir(dir);

  if (rv == OK) {
    ensure_write_permissions(path);
    rv = NS_trmdir(path);
    if (rv) {
      LOG(("remove_recursive_on_reboot: unable to remove directory: " LOG_S
           ", rv: %d, err: %d",
           path, rv, errno));
    }
  }
  return rv;
}
#endif

//-----------------------------------------------------------------------------

// Create a backup of the specified file by renaming it.
static int backup_create(const NS_tchar* path) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  return rename_file(path, backup);
}

// Rename the backup of the specified file that was created by renaming it back
// to the original file.
static int backup_restore(const NS_tchar* path, const NS_tchar* relPath) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  NS_tchar relBackup[MAXPATHLEN];
  NS_tsnprintf(relBackup, sizeof(relBackup) / sizeof(relBackup[0]),
               NS_T("%s") BACKUP_EXT, relPath);

  if (NS_taccess(backup, F_OK)) {
    LOG(("backup_restore: backup file doesn't exist: " LOG_S, relBackup));
    return OK;
  }

  return rename_file(backup, path);
}

// Discard the backup of the specified file that was created by renaming it.
static int backup_discard(const NS_tchar* path, const NS_tchar* relPath) {
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup) / sizeof(backup[0]),
               NS_T("%s") BACKUP_EXT, path);

  NS_tchar relBackup[MAXPATHLEN];
  NS_tsnprintf(relBackup, sizeof(relBackup) / sizeof(relBackup[0]),
               NS_T("%s") BACKUP_EXT, relPath);

  // Nothing to discard
  if (NS_taccess(backup, F_OK)) {
    return OK;
  }

  int rv = ensure_remove(backup);
  if (rv) {
    LOG(("backup_discard: unable to remove: " LOG_S, relBackup));
#ifdef XP_WIN
    rv = remove_on_reboot(backup);
  }
  if (rv) {
#endif
    return WRITE_ERROR_DELETE_BACKUP;
  }

  return OK;
}

[[nodiscard]] static bool draft_path(NS_tchar (&draft)[MAXPATHLEN],
                                     const NS_tchar* path) {
  return NS_tvsnprintf(draft, MAXPATHLEN, NS_T("%s") DRAFT_EXT, path);
}

// Discard the file that holds the draft contents of the specified file. This is
// also used to get rid of files that a previous updater instance left behind
// when it crashed, so it only ever discards drafts for paths that the update
// being applied produces new contents for.
static int draft_discard(const NS_tchar* path, const NS_tchar* relPath) {
  NS_tchar draft[MAXPATHLEN];
  NS_tchar relDraft[MAXPATHLEN];
  if (!draft_path(draft, path) || !draft_path(relDraft, relPath)) {
    LOG(("draft_discard: draft path too long for: " LOG_S, relPath));
    return USAGE_ERROR;
  }

  // Nothing to discard.
  if (NS_taccess(draft, F_OK)) {
    return OK;
  }

  LOG(("draft_discard: discarding draft file: " LOG_S, relDraft));

  int rv = ensure_remove(draft);
  if (rv) {
    LOG(("draft_discard: unable to remove: " LOG_S, relDraft));
#ifdef XP_WIN
    rv = remove_on_reboot(draft);
  }
  if (rv) {
#endif
    return WRITE_ERROR_DELETE_FILE;
  }

  return OK;
}

// Move the file that holds the draft contents of the specified file into
// place. The specified file must have been moved out of the way already.
static int draft_commit(const NS_tchar* path) {
  NS_tchar draft[MAXPATHLEN];
  if (!draft_path(draft, path)) {
    LOG(("draft_commit: draft path too long"));
    return USAGE_ERROR;
  }

  return rename_file(draft, path);
}

// Helper function for post-processing a temporary backup.
static void backup_finish(const NS_tchar* path, const NS_tchar* relPath,
                          int status) {
  if (status == OK) {
    backup_discard(path, relPath);
  } else {
    backup_restore(path, relPath);
  }
}

static int extract_file(const NS_tchar* itemPath, const NS_tchar* dstPath) {
#ifdef XP_WIN
  char mbItemPath[MAXPATHLEN];
  if (!WideCharToMultiByte(CP_UTF8, 0, itemPath, -1, mbItemPath, MAXPATHLEN,
                           nullptr, nullptr)) {
    LOG(("error converting wchar to utf8: %lu", GetLastError()));
    return STRING_CONVERSION_ERROR;
  }

  return gArchiveReader.ExtractFile(mbItemPath, dstPath);
#else
  return gArchiveReader.ExtractFile(itemPath, dstPath);
#endif
}

static int extract_file_to_stream(const NS_tchar* itemPath, FILE* dstStream) {
#ifdef XP_WIN
  char mbItemPath[MAXPATHLEN];
  if (!WideCharToMultiByte(CP_UTF8, 0, itemPath, -1, mbItemPath, MAXPATHLEN,
                           nullptr, nullptr)) {
    LOG(("error converting wchar to utf8: %lu", GetLastError()));
    return STRING_CONVERSION_ERROR;
  }

  return gArchiveReader.ExtractFileToStream(mbItemPath, dstStream);
#else
  return gArchiveReader.ExtractFileToStream(itemPath, dstStream);
#endif
}

//-----------------------------------------------------------------------------

static int DoUpdate();

class Action {
 public:
  Action() : mProgressCost(1), mNext(nullptr) {}
  virtual ~Action() = default;

  virtual int Parse(NS_tchar* line) = 0;

  // Do any preprocessing to ensure that the action can be performed.  Draft
  // will be called if this Action and all others return OK from this method.
  virtual int Prepare() = 0;

  // In non-staged updates, write the new contents of the file that this action
  // produces to a draft file next to the target file.  Execute will be called
  // if this Action and all others return OK from this method.
  virtual int Draft() = 0;

  // Perform the operation.  Return OK to indicate success.  After all actions
  // have been executed, Finish will be called.  A requirement of Execute is
  // that its operation be reversable from Finish.
  virtual int Execute() = 0;

  // Finish is called after execution of all actions.  If status is OK, then
  // all actions were successfully executed.  Otherwise, some action failed.
  virtual void Finish(int status) = 0;

  int mProgressCost;

 private:
  Action* mNext;

  friend class ActionList;
};

class RemoveFile : public Action {
 public:
  RemoveFile() : mSkip(0) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override { return OK; }
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  int mSkip;
};

int RemoveFile::Parse(NS_tchar* line) {
  // format "<deadfile>"

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int RemoveFile::Prepare() {
  // Skip the file if it already doesn't exist.
  int rv = NS_taccess(mFile.get(), F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEFILE " LOG_S, mRelPath.get()));

  // Make sure that we're actually a file...
  struct NS_tstat_t fileInfo;
  rv = NS_tstat(mFile.get(), &fileInfo);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d", mFile.get(),
         errno));
    return READ_ERROR;
  }

  if (!S_ISREG(fileInfo.st_mode)) {
    LOG(("path present, but not a file: " LOG_S, mFile.get()));
    return DELETE_ERROR_EXPECTED_FILE;
  }

  NS_tchar* slash = (NS_tchar*)NS_tstrrchr(mFile.get(), NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = NS_taccess(mFile.get(), W_OK);
    *slash = NS_T('/');
  } else {
    rv = NS_taccess(NS_T("."), W_OK);
  }

  if (rv) {
    LOG(("access failed: %d", errno));
    return WRITE_ERROR_FILE_ACCESS_DENIED;
  }

  return OK;
}

int RemoveFile::Execute() {
  if (mSkip) {
    return OK;
  }

  LOG(("EXECUTE REMOVEFILE " LOG_S, mRelPath.get()));

  // The file is checked for existence here and in Prepare since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mFile.get(), F_OK);
  if (rv) {
    LOG(("file cannot be removed because it does not exist; skipping"));
    mSkip = 1;
    return OK;
  }

  if (sStagedUpdate) {
    // Staged updates don't need backup files so just remove it.
    rv = ensure_remove(mFile.get());
    if (rv) {
      return WRITE_ERROR_DELETE_FILE;
    }
  } else {
    // Rename the old file. It will be removed in Finish.
    rv = backup_create(mFile.get());
    if (rv) {
      LOG(("backup_create failed: %d", rv));
      return rv;
    }
  }

  return OK;
}

void RemoveFile::Finish(int status) {
  if (mSkip) {
    return;
  }

  LOG(("FINISH REMOVEFILE " LOG_S, mRelPath.get()));

  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    backup_finish(mFile.get(), mRelPath.get(), status);
  }
}

class RemoveDir : public Action {
 public:
  RemoveDir() : mSkip(0) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;  // check that the source dir exists
  int Draft() override { return OK; }
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mDir;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  int mSkip;
};

int RemoveDir::Parse(NS_tchar* line) {
  // format "<deaddir>/"

  NS_tchar* validPath = get_valid_path(&line, true);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mDir.reset(get_full_path(validPath));
  if (!mDir) {
    return PARSE_ERROR;
  }

  return OK;
}

int RemoveDir::Prepare() {
  // We expect the directory to exist if we are to remove it.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEDIR " LOG_S "/", mRelPath.get()));

  // Make sure that we're actually a dir.
  struct NS_tstat_t dirInfo;
  rv = NS_tstat(mDir.get(), &dirInfo);
  if (rv) {
    LOG(("failed to read directory status info: " LOG_S ", err: %d",
         mRelPath.get(), errno));
    return READ_ERROR;
  }

  if (!S_ISDIR(dirInfo.st_mode)) {
    LOG(("path present, but not a directory: " LOG_S, mRelPath.get()));
    return DELETE_ERROR_EXPECTED_DIR;
  }

  rv = NS_taccess(mDir.get(), W_OK);
  if (rv) {
    LOG(("access failed: %d, %d", rv, errno));
    return WRITE_ERROR_DIR_ACCESS_DENIED;
  }

  return OK;
}

int RemoveDir::Execute() {
  if (mSkip) {
    return OK;
  }

  LOG(("EXECUTE REMOVEDIR " LOG_S "/", mRelPath.get()));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping"));
    mSkip = 1;
  }

  return OK;
}

void RemoveDir::Finish(int status) {
  if (mSkip || status != OK) {
    return;
  }

  LOG(("FINISH REMOVEDIR " LOG_S "/", mRelPath.get()));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir.get(), F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping"));
    return;
  }

  if (status == OK) {
    if (NS_trmdir(mDir.get())) {
      LOG(("non-fatal error removing directory: " LOG_S "/, rv: %d, err: %d",
           mRelPath.get(), rv, errno));
    }
  }
}

class AddFile : public Action {
 public:
  AddFile() : mAdded(false) {}

  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mRelPath;
  bool mAdded;
};

int AddFile::Parse(NS_tchar* line) {
  // format "<newfile>"

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int AddFile::Prepare() {
  LOG(("PREPARE ADD " LOG_S, mRelPath.get()));

  return OK;
}

int AddFile::Draft() {
  // Logged before the staged update check on purpose: staged and non-staged
  // updater tests compare against the same expected update logs.
  LOG(("DRAFT ADD " LOG_S, mRelPath.get()));

  if (sStagedUpdate) {
    return OK;
  }

  NS_tchar draft[MAXPATHLEN];
  if (!draft_path(draft, mFile.get())) {
    LOG(("draft path too long for: " LOG_S, mRelPath.get()));
    return USAGE_ERROR;
  }

  int rv = ensure_parent_dir(mFile.get());
  if (rv) {
    return rv;
  }

  // Get rid of a draft that a previous updater instance left behind, so that
  // the new one is created with the mode that the archive asks for.
  rv = draft_discard(mFile.get(), mRelPath.get());
  if (rv) {
    return rv;
  }

  return extract_file(mRelPath.get(), draft);
}

int AddFile::Execute() {
  LOG(("EXECUTE ADD " LOG_S, mRelPath.get()));

  int rv;

  // First make sure that we can actually get rid of any existing file.
  rv = NS_taccess(mFile.get(), F_OK);
  if (rv == 0) {
    if (sStagedUpdate) {
      // Staged updates don't need backup files so just remove it.
      if (ensure_remove(mFile.get())) {
        return WRITE_ERROR_DELETE_FILE;
      }
    } else {
      rv = backup_create(mFile.get());
      if (rv) {
        return rv;
      }
    }
  } else {
    rv = ensure_parent_dir(mFile.get());
    if (rv) {
      return rv;
    }
  }

  if (!sStagedUpdate) {
    // The new contents have already been written during the Draft phase.
    rv = draft_commit(mFile.get());
    if (!rv) {
      mAdded = true;
    }
    return rv;
  }

  rv = extract_file(mRelPath.get(), mFile.get());
  if (!rv) {
    mAdded = true;
  }
  return rv;
}

void AddFile::Finish(int status) {
  LOG(("FINISH ADD " LOG_S, mRelPath.get()));
  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    // Get rid of the new contents if they were never moved into place, which
    // happens when the update failed before or during the Execute phase.
    draft_discard(mFile.get(), mRelPath.get());

    // When there is an update failure and a file has been added it is removed
    // here since there might not be a backup to replace it.
    if (status && mAdded) {
      if (NS_tremove(mFile.get()) && errno != ENOENT) {
        LOG(("non-fatal error after update failure removing added file: " LOG_S
             ", err: %d",
             mFile.get(), errno));
#ifdef XP_WIN
        (void)remove_on_reboot(mFile.get());
#endif
      }
    }
    backup_finish(mFile.get(), mRelPath.get(), status);
  }
}

class PatchFileDecoder {
 public:
  // This method is the only valid way to create a PatchFileDecoder object,
  // which ensures that any such object has successfully called the Load()
  // method. Child classes must hide their constructors and declare the parent
  // class as a friend.
  template <typename ChildClass>
  static mozilla::UniquePtr<PatchFileDecoder> TryLoadAs(FILE* aPatchFile,
                                                        int* aReturnValue) {
    // We cannot use MakeUnique because child classes hide their constructors.
    mozilla::UniquePtr<ChildClass> ptr{new ChildClass()};
    fseek(aPatchFile, 0, SEEK_SET);
    int rv = ptr->Load(aPatchFile);
    if (rv != OK) {
      ptr.reset();
    }
    if (aReturnValue) {
      *aReturnValue = rv;
    }
    return ptr;
  }

  virtual ~PatchFileDecoder() = default;

  virtual unsigned int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize) = 0;

  virtual off_t SourceSize() = 0;
  virtual off_t DestinationSize() = 0;
  virtual unsigned int SourceCrc32() = 0;

  // Applies the loaded patch to aCheckedSrcBuf, and writes the result to
  // aDstFile. aDstFile is never deleted, cleanup is up to the caller.
  // Assumes that the crc32 and size of aCheckedSrcBuf have been
  // checked by the caller.
  virtual int Apply(const uint8_t* aCheckedSrcBuf, size_t aCheckedSrcBufSize,
                    FILE* aDstFile) = 0;

 protected:
  virtual int Load(FILE* aPatchFile) = 0;
};

#if defined(MOZ_BSPATCH)
class BSPatchFileDecoder : public PatchFileDecoder {
 public:
  ~BSPatchFileDecoder() override = default;

  unsigned int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize) override;

  off_t SourceSize() override;

  off_t DestinationSize() override;

  unsigned int SourceCrc32() override;

  int Apply(const uint8_t* aSrcBuf, size_t aSrcBufSize,
            FILE* aDstFile) override;

 protected:  // Comply with PatchFileDecoder::TryLoadAs requirements
  BSPatchFileDecoder() = default;
  int Load(FILE* aPatchFile) override;
  friend class PatchFileDecoder;

 private:
  FILE* mPatchFile{};
  MBSPatchHeader mHeader{};
};

// This BZ2_crc32Table variable lives in libbz2. We just took the
// data structure from bz2 and created crctables.h
unsigned int BSPatchFileDecoder::ComputeCrc32(const uint8_t* aBuf,
                                              size_t aBufSize) {
  unsigned int crc = 0xffffffffL;

  const uint8_t* end = aBuf + aBufSize;
  for (; aBuf != end; ++aBuf)
    crc = (crc << 8) ^ BZ2_crc32Table[(crc >> 24) ^ *aBuf];

  crc = ~crc;
  return crc;
}

int BSPatchFileDecoder::Load(FILE* aPatchFile) {
  mPatchFile = aPatchFile;
  return MBS_ReadHeader(aPatchFile, &mHeader);
}

off_t BSPatchFileDecoder::SourceSize() {
  return static_cast<off_t>(mHeader.slen);
}

off_t BSPatchFileDecoder::DestinationSize() {
  return static_cast<off_t>(mHeader.dlen);
}

unsigned int BSPatchFileDecoder::SourceCrc32() { return mHeader.scrc32; }

int BSPatchFileDecoder::Apply(const uint8_t* aCheckedSrcBuf,
                              size_t aCheckedSrcBufSize, FILE* aDstFile) {
  return MBS_ApplyPatch(&mHeader, mPatchFile, aCheckedSrcBuf, aDstFile);
}
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
void LogZucchiniMessage(const char* aMessage) { LOG(("%s", aMessage)); }

// Best-effort conversion from Zucchini status codes to updater status codes
int FromZucchiniStatus(zucchini::status::Code code) {
  int result = OK;
  switch (code) {
    case zucchini::status::kStatusSuccess:
      break;
    case zucchini::status::kStatusFileReadError:
    case zucchini::status::kStatusPatchReadError:
      result = READ_ERROR;
      break;
    case zucchini::status::kStatusFileWriteError:
      result = WRITE_ERROR;
      break;
    case zucchini::status::kStatusPatchWriteError:
      result = WRITE_ERROR_PATCH_FILE;
      break;
    case zucchini::status::kStatusInvalidOldImage:
    case zucchini::status::kStatusInvalidNewImage:
      result = CRC_ERROR;
      break;
    case zucchini::status::kStatusOutOfMemory:
      result = BSPATCH_MEM_ERROR;
      break;
    case zucchini::status::kStatusInvalidParam:
    case zucchini::status::kStatusDiskFull:
    case zucchini::status::kStatusIoError:
    case zucchini::status::kStatusFatal:
    default:
      result = UNEXPECTED_BSPATCH_ERROR;
      break;
  }
  if (result != OK) {
    LOG(
        ("FromZucchiniStatus: encountered zucchini error %d, converting to "
         "updater error %d",
         code, result));
  }
  return result;
}

class ZucchiniPatchFileDecoder : public PatchFileDecoder {
 public:
  ~ZucchiniPatchFileDecoder() override = default;

  unsigned int ComputeCrc32(const uint8_t* aBuf, size_t aBufSize) override;

  off_t SourceSize() override;

  off_t DestinationSize() override;

  unsigned int SourceCrc32() override;

  int Apply(const uint8_t* aCheckedSrcBuf, size_t aCheckedSrcBufSize,
            FILE* aDstFile) override;

 protected:  // Comply with PatchFileDecoder::TryLoadAs requirements
  ZucchiniPatchFileDecoder() = default;
  int Load(FILE* aPatchFile) override;
  friend class PatchFileDecoder;

 private:
  zucchini::mozilla::MappedPatch mMappedPatch;
  uint32_t mSourceSize{};
  uint32_t mDestinationSize{};
  uint32_t mSourceCrc32{};
};

unsigned int ZucchiniPatchFileDecoder::ComputeCrc32(const uint8_t* aBuf,
                                                    size_t aBufSize) {
  return zucchini::mozilla::ComputeCrc32(aBuf, aBufSize);
}

int ZucchiniPatchFileDecoder::Load(FILE* aPatchFile) {
  return FromZucchiniStatus(mMappedPatch.Load(
      aPatchFile, &mSourceSize, &mDestinationSize, &mSourceCrc32));
}

off_t ZucchiniPatchFileDecoder::SourceSize() {
  return static_cast<off_t>(mSourceSize);
}

off_t ZucchiniPatchFileDecoder::DestinationSize() {
  return static_cast<off_t>(mDestinationSize);
}

unsigned int ZucchiniPatchFileDecoder::SourceCrc32() {
  return static_cast<int>(mSourceCrc32);
}

int ZucchiniPatchFileDecoder::Apply(const uint8_t* aCheckedSrcBuf,
                                    size_t aCheckedSrcBufSize, FILE* aDstFile) {
  // SAFETY: The caller has already checked that the crc32 and size of
  // aCheckedSrcBuf match with the contents of the patch file as a requirement
  // of PatchFileDecoder::Apply.
  return FromZucchiniStatus(
      mMappedPatch.ApplyUnsafe(aCheckedSrcBuf, aCheckedSrcBufSize, aDstFile));
}
#endif  // defined(MOZ_ZUCCHINI)

class PatchFile : public Action {
 public:
  PatchFile() : mPatchFile(nullptr), mPatchIndex(-1), mBufSize(0) {}

  ~PatchFile() override;

  int Parse(NS_tchar* line) override;
  int Prepare() override;  // should check for patch file and for checksum here
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  enum class PatchDest {
    InPlace,
    Draft,
  };

  int LoadSourceFile(FILE* ofile);

  // This consumes the patch, so it may only be called once.
  int ApplyPatchTo(PatchDest aDest);

  static int sPatchIndex;

  const NS_tchar* mPatchFile;
  mozilla::UniquePtr<NS_tchar[]> mFile;
  mozilla::UniquePtr<NS_tchar[]> mFileRelPath;
  int mPatchIndex;
  mozilla::UniquePtr<PatchFileDecoder> mPatchFileDecoder;
  mozilla::UniquePtr<uint8_t[]> mBuf;
  size_t mBufSize;
  NS_tchar mPatchPath[MAXPATHLEN];
  AutoFile mPatchStream;
};

int PatchFile::sPatchIndex = 0;

PatchFile::~PatchFile() {
  // Make sure mPatchStream gets unlocked on Windows; the system will do that,
  // but not until some indeterminate future time, and we want determinism.
  // Normally this happens at the end of Execute, when we close the stream;
  // this call is here in case Execute errors out.
#ifdef XP_WIN
  if (mPatchStream) {
    UnlockFile((HANDLE)_get_osfhandle(fileno(mPatchStream)), 0, 0, -1, -1);
  }
#endif
  // Patch files are written to the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete patch files here.
}

int PatchFile::LoadSourceFile(FILE* ofile) {
  struct stat os;
  int rv = fstat(fileno((FILE*)ofile), &os);
  if (rv) {
    LOG(("LoadSourceFile: unable to stat destination file: " LOG_S ", "
         "err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  off_t expectedSize = mPatchFileDecoder->SourceSize();
  if (os.st_size != expectedSize) {
    LOG(
        ("LoadSourceFile: destination file size %jd does not match expected "
         "size %jd",
         static_cast<intmax_t>(os.st_size),
         static_cast<intmax_t>(expectedSize)));
    return LOADSOURCE_ERROR_WRONG_SIZE;
  }

  mBufSize = os.st_size;
  mBuf = mozilla::MakeUniqueFallible<uint8_t[]>(mBufSize);
  if (!mBuf) {
    return UPDATER_MEM_ERROR;
  }

  size_t r = mBufSize;
  uint8_t* rb = mBuf.get();
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, ofile);
    if (c != count) {
      LOG(("LoadSourceFile: error reading destination file: " LOG_S,
           mFileRelPath.get()));
      return READ_ERROR;
    }

    r -= c;
    rb += c;
  }

  // Verify that the contents of the source file correspond to what we expect.

  unsigned int crc = mPatchFileDecoder->ComputeCrc32(mBuf.get(), mBufSize);
  unsigned int expectedCrc = mPatchFileDecoder->SourceCrc32();

  if (crc != expectedCrc) {
    LOG(
        ("LoadSourceFile: destination file crc %u does not match expected "
         "crc %u",
         crc, expectedCrc));
    return CRC_ERROR;
  }

  return OK;
}

int PatchFile::Parse(NS_tchar* line) {
  // format "<patchfile>" "<filetopatch>"

  // Get the path to the patch file inside of the mar
  mPatchFile = mstrtok(kQuote, &line);
  if (!mPatchFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  NS_tchar* validPath = get_valid_path(&line);
  if (!validPath) {
    return PARSE_ERROR;
  }

  mFileRelPath = mozilla::MakeUnique<NS_tchar[]>(MAXPATHLEN);
  NS_tstrcpy(mFileRelPath.get(), validPath);

  mFile.reset(get_full_path(validPath));
  if (!mFile) {
    return PARSE_ERROR;
  }

  return OK;
}

int PatchFile::Prepare() {
  LOG(("PREPARE PATCH " LOG_S, mFileRelPath.get()));

  // extract the patch to a temporary file
  mPatchIndex = sPatchIndex++;

  NS_tsnprintf(mPatchPath, sizeof(mPatchPath) / sizeof(mPatchPath[0]),
               NS_T("%s/updating/%d.patch"), gWorkingDirPath, mPatchIndex);

  // The removal of pre-existing patch files here is in case a previous update
  // crashed and left these files behind.
  if (NS_tremove(mPatchPath) && errno != ENOENT) {
    LOG(("failure removing pre-existing patch file: " LOG_S ", err: %d",
         mPatchPath, errno));
    return WRITE_ERROR;
  }

  mPatchStream = NS_tfopen(mPatchPath, NS_T("wb+"));
  if (!mPatchStream) {
    return WRITE_ERROR;
  }

#ifdef XP_WIN
  // Lock the patch file, so it can't be messed with between
  // when we're done creating it and when we go to apply it.
  if (!LockFile((HANDLE)_get_osfhandle(fileno(mPatchStream)), 0, 0, -1, -1)) {
    LOG(("Couldn't lock patch file: %lu", GetLastError()));
    return LOCK_ERROR_PATCH_FILE;
  }
#endif

  return extract_file_to_stream(mPatchFile, mPatchStream);
}

int PatchFile::Draft() {
  // Logged before the staged update check on purpose: staged and non-staged
  // updater tests compare against the same expected update logs.
  LOG(("DRAFT PATCH " LOG_S, mFileRelPath.get()));

  if (sStagedUpdate) {
    return OK;
  }

  // Get rid of a draft that a previous updater instance left behind. Its mode
  // doesn't matter, because ApplyPatchTo sets the mode of the draft it writes,
  // but on Windows ApplyPatchTo opens the draft exclusively, so an undeletable
  // one has to be moved out of the way first, which is what draft_discard does.
  int rv = draft_discard(mFile.get(), mFileRelPath.get());
  if (rv) {
    return rv;
  }

  return ApplyPatchTo(PatchDest::Draft);
}

int PatchFile::Execute() {
  LOG(("EXECUTE PATCH " LOG_S, mFileRelPath.get()));

  if (!sStagedUpdate) {
    // The new contents have already been written during the Draft phase. Rename
    // the destination file so that it can be used to restore the file to its
    // original state if there is an error, then move the new contents in place.
    int rv = backup_create(mFile.get());
    if (rv) {
      return rv;
    }

    return draft_commit(mFile.get());
  }

  return ApplyPatchTo(PatchDest::InPlace);
}

int PatchFile::ApplyPatchTo(PatchDest aDest) {
  NS_tchar draft[MAXPATHLEN];
  if (aDest == PatchDest::Draft && !draft_path(draft, mFile.get())) {
    LOG(("draft path too long for: " LOG_S, mFileRelPath.get()));
    return USAGE_ERROR;
  }
  const NS_tchar* destPath = aDest == PatchDest::Draft ? draft : mFile.get();

  int rv = UNEXPECTED_BSPATCH_ERROR;

  // zucchini patch files start with "Zucc" bytes, while bspatch patch files
  // start with "MBDIFF10" bytes. Since these bytes are checked, there is no
  // risk of a loader accepting a patch in the wrong format and we can safely
  // iterate over the formats.

#if defined(MOZ_BSPATCH)
  mPatchFileDecoder =
      PatchFileDecoder::TryLoadAs<BSPatchFileDecoder>(mPatchStream, &rv);
#endif  // defined(MOZ_BSPATCH)

#if defined(MOZ_ZUCCHINI)
  if (!mPatchFileDecoder) {
    mPatchFileDecoder = PatchFileDecoder::TryLoadAs<ZucchiniPatchFileDecoder>(
        mPatchStream, &rv);
  }
#endif  // defined(MOZ_ZUCCHINI)

  if (!mPatchFileDecoder) {
    return rv;
  }

  FILE* origfile = nullptr;
#ifdef XP_WIN
  if (NS_tstrcmp(mFileRelPath.get(), gCallbackRelPath) == 0) {
    // Read from the copy of the callback when patching since the callback can't
    // be opened for reading to prevent the application from being launched.
    origfile = NS_tfopen(gCallbackBackupPath, NS_T("rb"));
  } else {
    origfile = NS_tfopen(mFile.get(), NS_T("rb"));
  }
#else
  origfile = NS_tfopen(mFile.get(), NS_T("rb"));
#endif

  if (!origfile) {
    LOG(("unable to open destination file: " LOG_S ", err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  rv = LoadSourceFile(origfile);
  fclose(origfile);
  if (rv) {
    LOG(("LoadSourceFile failed"));
    return rv;
  }

  // The new file inherits the mode of the file that we are patching.
  struct NS_tstat_t ss;
  rv = NS_tstat(mFile.get(), &ss);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d",
         mFileRelPath.get(), errno));
    return READ_ERROR;
  }

  unsigned int destMode = ss.st_mode;
#ifdef XP_WIN
  if (aDest == PatchDest::Draft) {
    // _wstat derives the execute bits from the extension, which the draft file
    // doesn't have. They come back on their own once it is renamed into place.
    destMode &= ~(unsigned int)(_S_IEXEC | (_S_IEXEC >> 3) | (_S_IEXEC >> 6));
  }
#endif

  off_t dlen = mPatchFileDecoder->DestinationSize();

#if defined(HAVE_POSIX_FALLOCATE)
  AutoFile ofile(ensure_open(destPath, NS_T("wb+"), destMode));
  if (ofile != nullptr) {
    // Preallocation is only an anti-fragmentation optimization; a failure here
    // is not fatal because writing the patched contents will fail below if
    // there really is no space. posix_fallocate returns the error number
    // instead of setting errno.
    int fallocateRv = posix_fallocate(fileno((FILE*)ofile), 0, dlen);
    if (fallocateRv != 0) {
      LOG(("failed to preallocate space for new file: " LOG_S ", err: %d",
           mFileRelPath.get(), fallocateRv));
    }
  }
#elif defined(XP_WIN)
  bool shouldTruncate = true;

  // Creating the file, setting the size, and then closing the file handle
  // lessens fragmentation more than any other method tested. Other methods that
  // have been tested are:
  // 1. _chsize / _chsize_s reduced fragmentation though not completely.
  // 2. _get_osfhandle and then setting the size reduced fragmentation though
  //    not completely. There are also reports of _get_osfhandle failing on
  //    mingw.
  HANDLE hfile = CreateFileW(destPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hfile != INVALID_HANDLE_VALUE) {
    if (SetFilePointer(hfile, dlen, nullptr, FILE_BEGIN) !=
            INVALID_SET_FILE_POINTER &&
        SetEndOfFile(hfile) != 0) {
      shouldTruncate = false;
    }
    CloseHandle(hfile);
  }

  AutoFile ofile(ensure_open(
      destPath, shouldTruncate ? NS_T("wb+") : NS_T("rb+"), destMode));
#elif defined(XP_MACOSX)
  AutoFile ofile(ensure_open(destPath, NS_T("wb+"), destMode));
  if (ofile != nullptr) {
    // Modified code from FileUtils.cpp
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, dlen};
    // Try to get a continous chunk of disk space
    rv = fcntl(fileno((FILE*)ofile), F_PREALLOCATE, &store);
    if (rv == -1) {
      // OK, perhaps we are too fragmented, allocate non-continuous
      store.fst_flags = F_ALLOCATEALL;
      rv = fcntl(fileno((FILE*)ofile), F_PREALLOCATE, &store);
    }

    if (rv != -1 && ftruncate(fileno((FILE*)ofile), dlen) != 0) {
      LOG(("failed to set the size of the new file: " LOG_S ", err: %d",
           mFileRelPath.get(), errno));
    }
  }
#else
  AutoFile ofile(ensure_open(destPath, NS_T("wb+"), destMode));
#endif

  if (ofile == nullptr) {
    LOG(("unable to create new file: " LOG_S ", err: %d", mFileRelPath.get(),
         errno));
    return WRITE_ERROR_OPEN_PATCH_FILE;
  }

#ifdef XP_WIN
  if (!shouldTruncate) {
    fseek(ofile, 0, SEEK_SET);
  }
#endif

  // SAFETY: We have manually checked that the size and crc32 of mBuf match with
  // the patch in PatchFile::LoadSourceFile.
  rv = mPatchFileDecoder->Apply(mBuf.get(), mBufSize, ofile);

  // Go ahead and do a bit of cleanup now to minimize runtime overhead.
  // Release the patch decoder and any resources it holds (such as
  // memory-mapped patch files in zucchini) so they don't accumulate
  // across sequential patch actions.
  mPatchFileDecoder.reset();
  // Make sure mPatchStream gets unlocked on Windows; the system will do that,
  // but not until some indeterminate future time, and we want determinism.
#ifdef XP_WIN
  UnlockFile((HANDLE)_get_osfhandle(fileno(mPatchStream)), 0, 0, -1, -1);
#endif
  // Set mPatchStream to nullptr to make AutoFile close the file,
  // so it can be deleted on Windows.
  mPatchStream = nullptr;
  // Patch files are written to the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete patch files here.
  mPatchPath[0] = NS_T('\0');
  mBuf.reset();
  mBufSize = 0;

  return rv;
}

void PatchFile::Finish(int status) {
  LOG(("FINISH PATCH " LOG_S, mFileRelPath.get()));

  // Staged updates don't create backup files.
  if (!sStagedUpdate) {
    // Get rid of the new contents if they were never moved into place, which
    // happens when the update failed before or during the Execute phase.
    draft_discard(mFile.get(), mFileRelPath.get());

    backup_finish(mFile.get(), mFileRelPath.get(), status);
  }
}

class AddIfFile : public AddFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 protected:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int AddIfFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<newfile>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return AddFile::Parse(line);
}

int AddIfFile::Prepare() {
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return AddFile::Prepare();
}

int AddIfFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Draft();
}

int AddIfFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Execute();
}

void AddIfFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  AddFile::Finish(status);
}

class AddIfNotFile : public AddFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 protected:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int AddIfNotFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<newfile>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return AddFile::Parse(line);
}

int AddIfNotFile::Prepare() {
  // If the test file exists, then skip this action.
  if (!NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return AddFile::Prepare();
}

int AddIfNotFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Draft();
}

int AddIfNotFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return AddFile::Execute();
}

void AddIfNotFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  AddFile::Finish(status);
}

class PatchIfFile : public PatchFile {
 public:
  int Parse(NS_tchar* line) override;
  int Prepare() override;  // should check for patch file and for checksum here
  int Draft() override;
  int Execute() override;
  void Finish(int status) override;

 private:
  mozilla::UniquePtr<NS_tchar[]> mTestFile;
};

int PatchIfFile::Parse(NS_tchar* line) {
  // format "<testfile>" "<patchfile>" "<filetopatch>"

  mTestFile.reset(get_full_path(get_valid_path(&line)));
  if (!mTestFile) {
    return PARSE_ERROR;
  }

  // consume whitespace between args
  NS_tchar* q = mstrtok(kQuote, &line);
  if (!q) {
    return PARSE_ERROR;
  }

  return PatchFile::Parse(line);
}

int PatchIfFile::Prepare() {
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile.get(), F_OK)) {
    mTestFile = nullptr;
    return OK;
  }

  return PatchFile::Prepare();
}

int PatchIfFile::Draft() {
  if (!mTestFile) {
    return OK;
  }

  return PatchFile::Draft();
}

int PatchIfFile::Execute() {
  if (!mTestFile) {
    return OK;
  }

  return PatchFile::Execute();
}

void PatchIfFile::Finish(int status) {
  if (!mTestFile) {
    return;
  }

  PatchFile::Finish(status);
}

//-----------------------------------------------------------------------------

#ifdef XP_WIN
#  include "EnterprisePolicies.h"
#  include "EnterprisePoliciesFlagFile.h"
#  include "nsWindowsRestart.cpp"
#  include "nsWindowsHelpers.h"
#  include "uachelper.h"
#  ifdef MOZ_MAINTENANCE_SERVICE
#    include "pathhash.h"
#  endif

/**
 * Launch the post update application (helper.exe). It takes in the path of the
 * callback application to calculate the path of helper.exe. For service updates
 * this is called from both the system account and the current user account.
 *
 * @param  installationDir The path to the callback application binary.
 * @param  updateInfoDir   The directory where update info is stored.
 * @param  target          Whether this post-update should update user-specific
 *                         data or installation-specific data.
 * @return true if there was no error starting the process.
 */
bool LaunchWinPostProcess(const WCHAR* installationDir,
                          const WCHAR* updateInfoDir, PostUpdateTarget target) {
  WCHAR workingDirectory[MAX_PATH + 1] = {L'\0'};
  wcsncpy(workingDirectory, installationDir, MAX_PATH);

  // Launch helper.exe to perform post processing (e.g. registry and log file
  // modifications) for the update.
  WCHAR inifile[MAX_PATH + 1] = {L'\0'};
  wcsncpy(inifile, installationDir, MAX_PATH);
  if (!PathAppendSafe(inifile, L"updater.ini")) {
    LOG(
        ("LaunchWinPostProcess failed because PathAppendSafe failed when "
         "getting INI path"));
    return false;
  }

  WCHAR exefile[MAX_PATH + 1];
  WCHAR exearg[MAX_PATH + 1];
  if (!GetPrivateProfileStringW(L"PostUpdateWin", L"ExeRelPath", nullptr,
                                exefile, MAX_PATH + 1, inifile)) {
    LOG(("LaunchWinPostProcess failed due to failure to retrieve ExeRelPath"));
    return false;
  }

  if (!GetPrivateProfileStringW(L"PostUpdateWin", L"ExeArg", nullptr, exearg,
                                MAX_PATH + 1, inifile)) {
    LOG(("LaunchWinPostProcess failed due to failure to retrieve ExeArg"));
    return false;
  }

  // The relative path must not contain directory traversals, current directory,
  // or colons.
  if (wcsstr(exefile, L"..") != nullptr || wcsstr(exefile, L"./") != nullptr ||
      wcsstr(exefile, L".\\") != nullptr || wcsstr(exefile, L":") != nullptr) {
    LOG(
        ("LaunchWinPostProcess failed because executable path contains "
         "disallowed characters"));
    return false;
  }

  // The relative path must not start with a decimal point, backslash, or
  // forward slash.
  if (exefile[0] == L'.' || exefile[0] == L'\\' || exefile[0] == L'/') {
    LOG(("LaunchWinPostProcess failed because first character is invalid"));
    return false;
  }

  WCHAR exefullpath[MAX_PATH + 1] = {L'\0'};
  wcsncpy(exefullpath, installationDir, MAX_PATH);
  if (!PathAppendSafe(exefullpath, exefile)) {
    LOG(
        ("LaunchWinPostProcess failed because PathAppendSafe failed when "
         "getting full executable path"));
    return false;
  }

  if (!IsValidFullPath(exefullpath)) {
    LOG(
        ("LaunchWinPostProcess failed because executable path is not a valid, "
         "full path"));
    return false;
  }

#  if !defined(TEST_UPDATER) && defined(MOZ_MAINTENANCE_SERVICE)
  if (sUsingService &&
      !DoesBinaryMatchAllowedCertificates(installationDir, exefullpath)) {
    LOG(
        ("LaunchWinPostProcess failed because the binary doesn't match the "
         "allowed certificates"));
    return false;
  }
#  endif

  WCHAR dlogFile[MAX_PATH + 1];
  if (!PathGetSiblingFilePath(dlogFile, exefullpath, L"uninstall.update")) {
    LOG(("LaunchWinPostProcess failed because dlogFile path is unavailable"));
    return false;
  }

  WCHAR slogFile[MAX_PATH + 1] = {L'\0'};
  if (gCopyOutputFiles) {
    if (!GetSecureOutputFilePath(gPatchDirPath, L".log", slogFile)) {
      LOG(
          ("LaunchWinPostProcess failed because a secure slogFile path is "
           "unavailable"));
      return false;
    }
  } else {
    wcsncpy(slogFile, updateInfoDir, MAX_PATH);
    if (!PathAppendSafe(slogFile, UpdateLogFilename())) {
      LOG(("LaunchWinPostProcess failed because slogFile path is unavailable"));
      return false;
    }
  }

  const bool addDesktopLauncher{
      !EnterprisePoliciesFlagFile::Exists(gPatchDirPath)};
  if (addDesktopLauncher) {
    LOG(("Add /DesktopLauncher argument to helper.exe"));
  }

  LPCWSTR args[] = {
      L"argv0ignored ",
      exearg,
      addDesktopLauncher ? L" /DesktopLauncher" : L"",
      target == PostUpdateTarget::Installation
          ? L" /PostUpdateTarget:Installation"
          : L" /PostUpdateTarget:CurrentUser",
  };

  size_t len = 0;
  for (LPCWSTR arg : args) {
    len += wcslen(arg);
  }

  WCHAR* cmdline = (WCHAR*)calloc(len + 1, sizeof(WCHAR));
  if (!cmdline) {
    LOG(
        ("LaunchWinPostProcess failed due to failure to allocate %zu wchars "
         "for cmdline",
         len + 1));
    return false;
  }

  for (LPCWSTR arg : args) {
    wcscat(cmdline, arg);
  }

  // We want to launch the post update helper app to update the Windows
  // registry even if there is a failure with removing the uninstall.update
  // file or copying the update.log file.
  CopyFileW(slogFile, dlogFile, false);

  STARTUPINFOW si = {sizeof(si), 0};
  si.lpDesktop = const_cast<LPWSTR>(L"");  // -Wwritable-strings
  PROCESS_INFORMATION pi = {0};

  // Invoke post-update with a minimal environment to avoid environment
  // variables intended to relaunch Firefox impacting post-update operations, in
  // particular background tasks.  The updater will invoke the callback
  // application with the current (non-minimal) environment.
  //
  // N.b.: two null terminating characters!  The first terminates a non-existent
  // key-value pair, the second (automatically added) terminates the block of
  // key-value pairs.
  const WCHAR* emptyEnvironment = L"\0";

  bool ok =
      CreateProcessW(exefullpath, cmdline,
                     nullptr,  // no special security attributes
                     nullptr,  // no special thread attributes
                     false,    // don't inherit filehandles
                     0,        // No special process creation flags
                     (LPVOID)emptyEnvironment, workingDirectory, &si, &pi);
  free(cmdline);
  if (ok) {
    LOG(("LaunchWinPostProcess - Waiting for process to complete"));
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    LOG(("LaunchWinPostProcess - Process completed"));
  } else {
    LOG(("LaunchWinPostProcess - CreateProcessW failed: %lu", GetLastError()));
  }
  return ok;
}

#endif

static void LaunchCallbackApp(const NS_tchar* workingDir, int argc,
                              NS_tchar** argv, bool usingService) {
  putenv(const_cast<char*>("MOZ_LAUNCHED_CHILD=1"));

  // Run from the specified working directory (see bug 312360).
  if (NS_tchdir(workingDir) != 0) {
    LOG(("Warning: chdir failed"));
  }

#if defined(USE_EXECV)
  execv(argv[0], argv);
#elif defined(XP_MACOSX)
  LaunchMacApp(argc, (const char**)argv);
#elif defined(XP_WIN)
  // Do not allow the callback to run when running an update through the
  // service as session 0.  The unelevated updater.exe will do the launching.
  if (!usingService) {
    HANDLE hProcess;
    if (WinLaunchChild(argv[0], argc, argv, nullptr, &hProcess)) {
      // Keep the current process around until the callback process has created
      // its message queue, to avoid the launched process's windows being forced
      // into the background.
      mozilla::WaitForInputIdle(hProcess);
      CloseHandle(hProcess);
    }
  }
#else
#  warning "Need implementaton of LaunchCallbackApp"
#endif
}

static bool WriteToFile(const NS_tchar* aFilename, const char* aStatus) {
  LOG(("Writing status to file: %s", aStatus));

  NS_tchar statusFilePath[MAXPATHLEN + 1] = {NS_T('\0')};
#if defined(XP_WIN)
  if (gInvocation == UpdaterInvocation::Second) {
    if (!GetSecureOutputFilePath(gPatchDirPath, L".status", statusFilePath)) {
      LOG(("WriteToFile failed to get secure output path"));
      return false;
    }
  } else {
    NS_tsnprintf(statusFilePath,
                 sizeof(statusFilePath) / sizeof(statusFilePath[0]),
                 NS_T("%s\\%s"), gPatchDirPath, aFilename);
  }
#else
  NS_tsnprintf(statusFilePath,
               sizeof(statusFilePath) / sizeof(statusFilePath[0]),
               NS_T("%s/%s"), gPatchDirPath, aFilename);
  // Make sure that the directory for the update status file exists
  if (ensure_parent_dir(statusFilePath)) {
    LOG(("WriteToFile failed to ensure parent directory's existence"));
    return false;
  }
#endif

  AutoFile statusFile(CreateAndOpenFile(statusFilePath, true));
  if (statusFile == nullptr) {
    LOG(("WriteToFile failed to open status file: %d", errno));
    return false;
  }

  if (fwrite(aStatus, strlen(aStatus), 1, statusFile) != 1) {
    LOG(("WriteToFile failed to write to status file: %d", errno));
    return false;
  }

#if defined(XP_WIN)
  if (gInvocation == UpdaterInvocation::Second) {
    // This is done after the update status file has been written so if the
    // write to the update status file fails an existing update status file
    // won't be used.
    if (!WriteSecureIDFile(gPatchDirPath)) {
      LOG(("WriteToFile failed to write secure ID file"));
      return false;
    }
  }
#endif

  return true;
}

/**
 * Writes a string to the update.status file.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because the output_finish function copies the update status file for
 *       the elevated updater and writing the status file after calling
 *       output_finish will overwrite it.
 *
 * @param  aStatus
 *         The string to write to the update.status file.
 * @return true on success.
 */
static bool WriteStatusFile(const char* aStatus) {
  return WriteToFile(NS_T("update.status"), aStatus);
}

/**
 * Writes a string to the update.status file based on the status param.
 *
 * NOTE: All calls to WriteStatusFile MUST happen before calling output_finish
 *       because the output_finish function copies the update status file for
 *       the elevated updater and writing the status file after calling
 *       output_finish will overwrite it.
 *
 * @param  status
 *         A status code used to determine what string to write to the
 *         update.status file (see code).
 */
static void WriteStatusFile(int status) {
  const char* text;

  char buf[32];
  if (status == OK) {
    if (sStagedUpdate) {
      text = "applied\n";
    } else {
      text = "succeeded\n";
    }
  } else {
    snprintf(buf, sizeof(buf) / sizeof(buf[0]), "failed: %d\n", status);
    text = buf;
  }

  WriteStatusFile(text);
}

#if defined(XP_WIN)
/*
 * Parses the passed contents of an update status file and checks if the
 * contained status matches the expected status.
 *
 * @param  statusString The status file contents.
 * @param  expectedStatus The status to compare the update status file's
 *                        contents against.
 * @param  errorCode Optional out parameter. If a pointer is passed and the
 *                   update status file contains an error code, the code
 *                   will be returned via the out parameter. If a pointer is
 *                   passed and the update status file does not contain an error
 *                   code, or any error code after the status could not be
 *                   parsed, mozilla::Nothing will be returned via this
 *                   parameter.
 * @return true if the status is set to the value indicated by expectedStatus.
 */
static bool UpdateStatusIs(const char* statusString, const char* expectedStatus,
                           mozilla::Maybe<int>* errorCode = nullptr) {
  if (errorCode) {
    *errorCode = mozilla::Nothing();
  }

  // Parse the update status file. Expected format is:
  //   Update status string
  //   Optionally followed by:
  //     Colon character (':')
  //     Space character (' ')
  //     Integer error code
  //   Newline character
  const char* statusEnd = strchr(statusString, ':');
  if (statusEnd == nullptr) {
    statusEnd = strchr(statusString, '\n');
  }
  if (statusEnd == nullptr) {
    statusEnd = strchr(statusString, '\0');
  }
  size_t statusLen = statusEnd - statusString;
  size_t expectedStatusLen = strlen(expectedStatus);

  bool statusMatch =
      statusLen == expectedStatusLen &&
      strncmp(statusString, expectedStatus, expectedStatusLen) == 0;

  // We only need to continue parsing if (a) there is a place to store the error
  // code if we parse it, and (b) there is a status code to parse. If the status
  // string didn't end with a ':', there won't be an error code after it.
  if (!errorCode || *statusEnd != ':') {
    return statusMatch;
  }

  const char* errorCodeStart = statusEnd + 1;
  char* errorCodeEnd = nullptr;
  // strtol skips an arbitrary number of leading whitespace characters. This
  // technically allows us to successfully consume slightly misformatted status
  // files, since the expected format is for there to be a single space only.
  long longErrorCode = strtol(errorCodeStart, &errorCodeEnd, 10);
  if (errorCodeEnd != errorCodeStart && longErrorCode < INT_MAX &&
      longErrorCode > INT_MIN) {
    // We don't allow equality with INT_MAX/INT_MIN for two reasons. It could
    // be that, on this platform, INT_MAX/INT_MIN equal LONG_MAX/LONG_MIN, which
    // is what strtol gives us if the parsed value was out of bounds. And those
    // values are already way, way outside the set of valid update error codes
    // anyways.
    errorCode->emplace(static_cast<int>(longErrorCode));
  }
  return statusMatch;
}

/*
 * Reads the secure update status file and sets statusMatch to true if the
 * status matches the expected status that was passed.
 *
 * @param  expectedStatus The status to compare the update status file's
 *                        contents against.
 * @param  statusMatch Out parameter for specifying if the status is set to
 *                     the value indicated by expectedStatus
 * @param  errorCode Optional out parameter. If a pointer is passed and the
 *                   update status file contains an error code, the code
 *                   will be returned via the out parameter. If a pointer is
 *                   passed and the update status file does not contain an error
 *                   code, or any error code after the status could not be
 *                   parsed, mozilla::Nothing will be returned via this
 *                   parameter.
 * @return true if the information was retrieved successfully.
 */
static bool CompareSecureUpdateStatus(
    const char* expectedStatus, bool& statusMatch,
    mozilla::Maybe<int>* errorCode = nullptr) {
  NS_tchar statusFilePath[MAX_PATH + 1] = {L'\0'};
  if (!GetSecureOutputFilePath(gPatchDirPath, L".status", statusFilePath)) {
    LOG(
        ("CompareSecureUpdateStatus failed due to GetSecureOutputFilePath "
         "failure"));
    return false;
  }

  AutoFile file(NS_tfopen(statusFilePath, NS_T("rb")));
  if (file == nullptr) {
    LOG(("CompareSecureUpdateStatus failed to open the secure status file: %d",
         errno));
    return false;
  }

  const size_t bufferLength = 32;
  char buf[bufferLength] = {0};
  size_t charsRead = fread(buf, sizeof(buf[0]), bufferLength - 1, file);
  if (ferror(file)) {
    LOG(("CompareSecureUpdateStatus failed to read status file"));
    return false;
  }
  buf[charsRead] = '\0';

  statusMatch = UpdateStatusIs(buf, expectedStatus, errorCode);
  LOG(("CompareSecureUpdateStatus %s %s %s", buf,
       statusMatch ? "matches" : "does not match", expectedStatus));
  return true;
}

/*
 * Reads the secure update status file and sets isSucceeded to true if the
 * status is set to succeeded.
 *
 * @param  isSucceeded Out parameter for specifying if the status
 *                     is set to succeeded or not.
 * @return true if the information was retrieved successfully.
 */
static bool IsSecureUpdateStatusSucceeded(bool& isSucceeded) {
  return CompareSecureUpdateStatus("succeeded", isSucceeded);
}
#endif

#ifdef MOZ_MAINTENANCE_SERVICE
/*
 * Read the update.status file and sets isPendingService to true if
 * the status is set to pending-service.
 *
 * @param  isPendingService Out parameter for specifying if the status
 *         is set to pending-service or not.
 * @return true if the information was retrieved and it is pending
 *         or pending-service.
 */
static bool IsUpdateStatusPendingService() {
  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename) / sizeof(filename[0]),
               NS_T("%s/update.status"), gPatchDirPath);

  AutoFile file(NS_tfopen(filename, NS_T("rb")));
  if (file == nullptr) {
    return false;
  }

  const size_t bufferLength = 32;
  char buf[bufferLength] = {0};
  size_t charsRead = fread(buf, sizeof(buf[0]), bufferLength - 1, file);
  if (ferror(file)) {
    return false;
  }
  buf[charsRead] = '\0';

  return UpdateStatusIs(buf, "pending-service") ||
         UpdateStatusIs(buf, "applied-service");
}

/*
 * Reads the secure update status file and sets isFailed to true if the
 * status is set to failed.
 *
 * @param  isFailed Out parameter for specifying if the status
 *                  is set to failed or not.
 * @param  errorCode Optional out parameter. If a pointer is passed and the
 *                   update status file contains an error code, the code
 *                   will be returned via the out parameter. If a pointer is
 *                   passed and the update status file does not contain an error
 *                   code, or any error code after the status could not be
 *                   parsed, mozilla::Nothing will be returned via this
 *                   parameter.
 * @return true if the information was retrieved successfully.
 */
static bool IsSecureUpdateStatusFailed(
    bool& isFailed, mozilla::Maybe<int>* errorCode = nullptr) {
  return CompareSecureUpdateStatus("failed", isFailed, errorCode);
}

/**
 * This function determines whether the error represented by the passed error
 * code could potentially be recovered from or bypassed by updating without
 * using the Maintenance Service (i.e. by showing a UAC prompt).
 * We don't really want to show a UAC prompt, but it's preferable over the
 * manual update doorhanger
 *
 * @param   errorCode An integer error code from the update.status file. Should
 *                    be one of the codes enumerated in updatererrors.h.
 * @returns true if the code represents a Maintenance Service specific error.
 *          Otherwise, false.
 */
static bool IsServiceSpecificErrorCode(int errorCode) {
  return ((errorCode >= 24 && errorCode <= 33) ||
          (errorCode >= 49 && errorCode <= 58));
}
#endif

/*
 * Copy the entire contents of the application installation directory to the
 * destination directory for the update process.
 *
 * @return 0 if successful, an error code otherwise.
 */
static int CopyInstallDirToDestDir() {
  // These files should not be copied over to the updated app
#ifdef XP_WIN
#  define SKIPLIST_COUNT 3
#elif XP_MACOSX
#  define SKIPLIST_COUNT 0
#else
#  define SKIPLIST_COUNT 2
#endif
  copy_recursive_skiplist<SKIPLIST_COUNT> skiplist;
#ifndef XP_MACOSX
  skiplist.append(0, gInstallDirPath, NS_T("updated"));
  skiplist.append(1, gInstallDirPath, NS_T("updates/0"));
#  ifdef XP_WIN
  skiplist.append(2, gInstallDirPath, NS_T("updated.update_in_progress.lock"));
#  endif
#endif

  return ensure_copy_recursive(gInstallDirPath, gWorkingDirPath, skiplist);
}

/*
 * Replace the application installation directory with the destination
 * directory in order to finish a staged update task
 *
 * @return 0 if successful, an error code otherwise.
 */
static int ProcessReplaceRequest() {
  // The replacement algorithm is like this:
  // 1. Move destDir to tmpDir.  In case of failure, abort.
  // 2. Move newDir to destDir.  In case of failure, revert step 1 and abort.
  // 3. Delete tmpDir (or defer it to the next reboot).

#ifdef XP_MACOSX
  NS_tchar destDir[MAXPATHLEN];
  NS_tsnprintf(destDir, sizeof(destDir) / sizeof(destDir[0]),
               NS_T("%s/Contents"), gInstallDirPath);
#elif XP_WIN
  // Windows preserves the case of the file/directory names.  We use the
  // GetLongPathName API in order to get the correct case for the directory
  // name, so that if the user has used a different case when launching the
  // application, the installation directory's name does not change.
  NS_tchar destDir[MAXPATHLEN];
  if (!GetLongPathNameW(gInstallDirPath, destDir,
                        sizeof(destDir) / sizeof(destDir[0]))) {
    return NO_INSTALLDIR_ERROR;
  }
#else
  NS_tchar* destDir = gInstallDirPath;
#endif

  NS_tchar tmpDir[MAXPATHLEN];
  NS_tsnprintf(tmpDir, sizeof(tmpDir) / sizeof(tmpDir[0]), NS_T("%s.bak"),
               destDir);

  NS_tchar newDir[MAXPATHLEN];
  NS_tsnprintf(newDir, sizeof(newDir) / sizeof(newDir[0]),
#ifdef XP_MACOSX
               NS_T("%s/Contents"), gWorkingDirPath);
#else
               NS_T("%s.bak/updated"), gInstallDirPath);
#endif

  // First try to remove the possibly existing temp directory, because if this
  // directory exists, we will fail to rename destDir.
  // No need to error check here because if this fails, we will fail in the
  // next step anyways.
  ensure_remove_recursive(tmpDir);

  LOG(("Begin moving destDir (" LOG_S ") to tmpDir (" LOG_S ")", destDir,
       tmpDir));
  int rv = rename_file(destDir, tmpDir, true);
#ifdef XP_WIN
  // On Windows, if Firefox is launched using the shortcut, it will hold a
  // handle to its installation directory open, which might not get released in
  // time. Therefore we wait a little bit here to see if the handle is released.
  // If it's not released, we just fail to perform the replace request.
  const int max_retries = 10;
  int retries = 0;
  while (rv == WRITE_ERROR && (retries++ < max_retries)) {
    LOG(
        ("PerformReplaceRequest: destDir rename attempt %d failed. "
         "File: " LOG_S ". Last error: %lu, err: %d",
         retries, destDir, GetLastError(), rv));

    Sleep(100);

    rv = rename_file(destDir, tmpDir, true);
  }
#endif
  if (rv) {
    // The status file will have 'pending' written to it so there is no value in
    // returning an error specific for this failure.
    LOG(("Moving destDir to tmpDir failed, err: %d", rv));
    return rv;
  }

  LOG(("Begin moving newDir (" LOG_S ") to destDir (" LOG_S ")", newDir,
       destDir));
  rv = rename_file(newDir, destDir, true);
#ifdef XP_MACOSX
  if (rv) {
    LOG(("Moving failed. Begin copying newDir (" LOG_S ") to destDir (" LOG_S
         ")",
         newDir, destDir));
    copy_recursive_skiplist<0> skiplist;
    rv = ensure_copy_recursive(newDir, destDir, skiplist);
  }
#endif
  if (rv) {
    LOG(("Moving newDir to destDir failed, err: %d", rv));
    LOG(("Now, try to move tmpDir back to destDir"));
    ensure_remove_recursive(destDir);
    int rv2 = rename_file(tmpDir, destDir, true);
    if (rv2) {
      LOG(("Moving tmpDir back to destDir failed, err: %d", rv2));
    }
    // The status file will be have 'pending' written to it so there is no value
    // in returning an error specific for this failure.
    return rv;
  }

#if !defined(XP_WIN) && !defined(XP_MACOSX)
  // Platforms that have their updates directory in the installation directory
  // need to have the last-update.log and backup-update.log files moved from the
  // old installation directory to the new installation directory.
  NS_tchar tmpLog[MAXPATHLEN];
  NS_tsnprintf(tmpLog, sizeof(tmpLog) / sizeof(tmpLog[0]),
               NS_T("%s/updates/last-update.log"), tmpDir);
  if (!NS_taccess(tmpLog, F_OK)) {
    NS_tchar destLog[MAXPATHLEN];
    NS_tsnprintf(destLog, sizeof(destLog) / sizeof(destLog[0]),
                 NS_T("%s/updates/last-update.log"), destDir);
    if (NS_tremove(destLog) && errno != ENOENT) {
      LOG(("non-fatal error removing log file: " LOG_S ", err: %d", destLog,
           errno));
    }
    NS_trename(tmpLog, destLog);
  }
#endif

  LOG(("Now, remove the tmpDir"));
  rv = ensure_remove_recursive(tmpDir, true);
  if (rv) {
    LOG(("Removing tmpDir failed, err: %d", rv));
#ifdef XP_WIN
    NS_tchar deleteDir[MAXPATHLEN];
    NS_tsnprintf(deleteDir, sizeof(deleteDir) / sizeof(deleteDir[0]),
                 NS_T("%s\\%s"), destDir, DELETE_DIR);
    // Attempt to remove the tobedeleted directory and then recreate it if it
    // was successfully removed.
    _wrmdir(deleteDir);
    if (NS_taccess(deleteDir, F_OK)) {
      NS_tmkdir(deleteDir, 0755);
    }
    remove_recursive_on_reboot(tmpDir, deleteDir);
#endif
  }

#ifdef XP_MACOSX
  // On OS X, we we need to remove the staging directory after its Contents
  // directory has been moved.
  NS_tchar updatedAppDir[MAXPATHLEN];
  NS_tsnprintf(updatedAppDir, sizeof(updatedAppDir) / sizeof(updatedAppDir[0]),
               NS_T("%s/Updated.app"), gPatchDirPath);
  ensure_remove_recursive(updatedAppDir);
#endif

  gSucceeded = true;

  return 0;
}

#if defined(XP_WIN) && defined(MOZ_MAINTENANCE_SERVICE)
static void WaitForServiceFinishThread(void* param) {
  // We wait at most 10 minutes, we already waited 5 seconds previously
  // before deciding to show this UI.
  WaitForServiceStop(SVC_NAME, 595);
  QuitProgressUI();
}
#endif

#ifdef MOZ_VERIFY_MAR_SIGNATURE
#  ifndef XP_MACOSX
/**
 * This function reads in the ACCEPTED_MAR_CHANNEL_IDS from update-settings.ini
 *
 * @param aPath    The path to the ini file that is to be read
 * @param aResults A pointer to the location to store the read strings
 * @return OK on success
 */
static int ReadMARChannelIDsFromPath(const NS_tchar* aPath,
                                     MARChannelStringTable* aResults) {
  const unsigned int kNumStrings = 1;
  const char* kUpdaterKeys = "ACCEPTED_MAR_CHANNEL_IDS\0";
  return ReadStrings(aPath, kUpdaterKeys, kNumStrings, &aResults->MARChannelID,
                     "Settings");
}
#  else   // XP_MACOSX
/**
 * This function reads in the ACCEPTED_MAR_CHANNEL_IDS from a string buffer.
 *
 * @param aChannels   A string buffer containing the MAR channel(s).
 * @param aResults    A pointer to the location to store the read strings.
 * @return OK on success
 */
static int ReadMARChannelIDsFromBuffer(char* aChannels,
                                       MARChannelStringTable* aResults) {
  const unsigned int kNumStrings = 1;
  const char* kUpdaterKeys = "ACCEPTED_MAR_CHANNEL_IDS\0";
  return ReadStringsFromBuffer(aChannels, kUpdaterKeys, kNumStrings,
                               &aResults->MARChannelID, "Settings");
}
#  endif  // XP_MACOSX

/**
 * This function reads in the `ACCEPTED_MAR_CHANNEL_IDS` from the appropriate
 * (platform-dependent) source and populates `gMARStrings`.
 *
 * @return
 *        `OK` on success, `UPDATE_SETTINGS_FILE_CHANNEL` on failure.
 */
static int PopulategMARStrings() {
  if (gMARStrings.MARChannelID && gMARStrings.MARChannelID[0] != '\0') {
    return OK;
  }

  int rv = UPDATE_SETTINGS_FILE_CHANNEL;
#  ifdef XP_MACOSX
  if (gInvocation != UpdaterInvocation::Second) {
    if (std::optional<std::string> marChannels =
            UpdateSettingsUtil::GetAcceptedMARChannelsValue()) {
      rv = ReadMARChannelIDsFromBuffer(marChannels->data(), &gMARStrings);
    }
  }
#  else
  NS_tchar updateSettingsPath[MAXPATHLEN];
  NS_tsnprintf(updateSettingsPath,
               sizeof(updateSettingsPath) / sizeof(updateSettingsPath[0]),
               NS_T("%s/update-settings.ini"), gInstallDirPath);
  rv = ReadMARChannelIDsFromPath(updateSettingsPath, &gMARStrings);
#  endif
  return rv == OK ? OK : UPDATE_SETTINGS_FILE_CHANNEL;
}
#endif  // MOZ_VERIFY_MAR_SIGNATURE

static int GetUpdateFileName(NS_tchar* fileName, int maxChars) {
  NS_tsnprintf(fileName, maxChars, NS_T("%s/update.mar"), gPatchDirPath);
  return OK;
}

static void UpdateThreadFunc(void* param) {
  // open ZIP archive and process...
  int rv;
  if (sReplaceRequest) {
    rv = ProcessReplaceRequest();
  } else {
    NS_tchar dataFile[MAXPATHLEN];
    rv = GetUpdateFileName(dataFile, sizeof(dataFile) / sizeof(dataFile[0]));
    if (rv == OK) {
      rv = gArchiveReader.Open(dataFile);
    }

#ifdef MOZ_VERIFY_MAR_SIGNATURE
    if (rv == OK) {
      rv = gArchiveReader.VerifySignature();
    }

    if (rv == OK) {
      rv = PopulategMARStrings();
      if (rv == OK) {
        rv = gArchiveReader.VerifyProductInformation(
            gMARStrings.MARChannelID.get(), MOZ_APP_VERSION);
      }
    }
#endif

    if (rv == OK && sStagedUpdate) {
#ifdef TEST_UPDATER
      // The MOZ_TEST_SKIP_UPDATE_STAGE environment variable prevents copying
      // the files in dist/bin in the test updater when staging an update since
      // this can cause tests to timeout.
      if (EnvHasValue("MOZ_TEST_SKIP_UPDATE_STAGE")) {
        rv = OK;
      } else if (EnvHasValue("MOZ_TEST_SLOW_SKIP_UPDATE_STAGE")) {
        // The following is to simulate staging so the UI tests have time to
        // show that the update is being staged.
        NS_tchar continueFilePath[MAXPATHLEN] = {NS_T('\0')};
        NS_tsnprintf(continueFilePath,
                     sizeof(continueFilePath) / sizeof(continueFilePath[0]),
                     NS_T("%s/continueStaging"), gInstallDirPath);
        // Use 300 retries for staging requests to lessen the likelihood of
        // tests intermittently failing on verify tasks due to launching the
        // updater. The total time to wait with the default interval of 100 ms
        // is approximately 30 seconds. The total time for tests is longer to
        // account for the extra time it takes for the updater to launch.
        const int max_retries = 300;
        int retries = 0;
        while (retries++ < max_retries) {
#  ifdef XP_WIN
          Sleep(100);
#  else
          usleep(100000);
#  endif
          // Continue after the continue file exists and is removed.
          if (!NS_tremove(continueFilePath)) {
            break;
          }
        }
        rv = OK;
      } else {
        rv = CopyInstallDirToDestDir();
      }
#else
      rv = CopyInstallDirToDestDir();
#endif
    }

    if (rv == OK) {
      rv = DoUpdate();
      gArchiveReader.Close();
      NS_tchar updatingDir[MAXPATHLEN];
      NS_tsnprintf(updatingDir, sizeof(updatingDir) / sizeof(updatingDir[0]),
                   NS_T("%s/updating"), gWorkingDirPath);
      ensure_remove_recursive(updatingDir);
    }
  }

  if (rv && (sReplaceRequest || sStagedUpdate)) {
    ensure_remove_recursive(gWorkingDirPath);
    // When attempting to replace the application, we should fall back
    // to non-staged updates in case of a failure.  We do this by
    // setting the status to pending, exiting the updater, and
    // launching the callback application.  The callback application's
    // startup path will see the pending status, and will start the
    // updater application again in order to apply the update without
    // staging.
    if (sReplaceRequest) {
      WriteStatusFile(sUsingService ? "pending-service" : "pending");
    } else {
      WriteStatusFile(rv);
    }
    LOG(("failed: %d", rv));
#ifdef TEST_UPDATER
    // Some tests need to use --test-process-updates again.
    putenv(const_cast<char*>("MOZ_TEST_PROCESS_UPDATES="));
#endif
  } else {
#ifdef TEST_UPDATER
    const char* forceErrorCodeString = getenv("MOZ_FORCE_ERROR_CODE");
    if (forceErrorCodeString && *forceErrorCodeString) {
      rv = atoi(forceErrorCodeString);
    }
#endif
    if (rv) {
      LOG(("failed: %d", rv));
    } else {
#ifdef XP_MACOSX
      // If the update was successful we need to update the timestamp on the
      // top-level Mac OS X bundle directory so that Mac OS X's Launch Services
      // picks up any major changes when the bundle is updated.
      if (!sStagedUpdate && utimes(gInstallDirPath, nullptr) != 0) {
        LOG(("Couldn't set access/modification time on application bundle."));
      }
#endif
      LOG(("succeeded"));
    }
    WriteStatusFile(rv);
  }

  LOG(("calling QuitProgressUI"));
  QuitProgressUI();
}

#ifdef XP_MACOSX
static void ServeElevatedUpdateThreadFunc(void* param) {
  UpdateServerThreadArgs* threadArgs = (UpdateServerThreadArgs*)param;
  gSucceeded = ServeElevatedUpdate(threadArgs->argc, threadArgs->argv,
                                   threadArgs->marChannelID);
  if (!gSucceeded) {
    WriteStatusFile(ELEVATION_CANCELED);
  }
  QuitProgressUI();
}

void freeArguments(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    free(argv[i]);
  }
  free(argv);
}
#endif

int LaunchCallbackAndPostProcessApps(int argc, NS_tchar** argv
#ifdef XP_WIN
                                     ,
                                     HANDLE updateLockFileHandle
#elif XP_MACOSX
                                     ,
                                     mozilla::UniquePtr<UmaskContext>
                                         umaskContext
#endif
) {
  // We want to make sure to call `output_finish` before we leave this function
  // and, if we end up launching the callback app, we want to call it before
  // we do that (so that the callback app can operate on the output).
  // But we want to do this as late as possible to make the log as detailed as
  // possible.
  class RaiiOutputFinish {
   public:
    RaiiOutputFinish() : mCalled(false) {}
    ~RaiiOutputFinish() { call(); }
    void call() {
      if (!mCalled) {
        mCalled = true;
        output_finish();
      }
    }

   private:
    bool mCalled;
  } raii_output_finish;

#ifdef XP_MACOSX
  umaskContext.reset();
#endif

  if (argc > kCallbackIndex) {
#if defined(XP_WIN)
    if (gSucceeded) {
      LOG(("Launching Windows post update process"));
      if (!LaunchWinPostProcess(gInstallDirPath, gPatchDirPath,
                                PostUpdateTarget::Installation)) {
        LOG(("The post update process was not launched successfully"));
      }

#  ifdef MOZ_MAINTENANCE_SERVICE
      // The service update will only be executed if it is already installed.
      // For first time installs of the service, the install will happen from
      // the PostUpdate process. We do the service update process here
      // because it's possible we are updating with updater.exe without the
      // service if the service failed to apply the update. We want to update
      // the service to a newer version in that case. If we are not running
      // through the service, then MOZ_USING_SERVICE will not exist.
      if (!sUsingService) {
        LOG(("Starting Service Update before launching callback app"));
        StartServiceUpdate(gInstallDirPath);
      } else {
        LOG(("Not starting service update. MMS will handle it."));
      }
#  endif
    } else {
      LOG(("Not launching Windows post update process because !gSucceeded"));
    }

    EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 0);

    // Flag removed by the unelevated process during the single-process update
    EnterprisePoliciesFlagFile::Remove(gPatchDirPath);

#elif XP_MACOSX
    if (gInvocation == UpdaterInvocation::First) {
      if (gSucceeded) {
        LOG(("Launching macOS post update process"));
        LaunchMacPostProcess(gInstallDirPath);
      } else {
        LOG(("Not launching macOS post update process because !gSucceeded"));
      }
#endif

    raii_output_finish.call();
    LaunchCallbackApp(argv[kCallbackWorkingDirIndex], argc - kCallbackIndex,
                      argv + kCallbackIndex, sUsingService);
#ifdef XP_MACOSX
  } else {  // isElevated
    LOG(
        ("This is the second instance. Skipping LaunchMacPostProcess and "
         "LaunchCallbackApp"));
  }
#endif /* XP_MACOSX */
}
else {
  LOG(
      ("No callback arg. Skipping LaunchWinPostProcess and "
       "LaunchCallbackApp"));
}
return 0;
}

bool ShouldRunSilently(int argc, NS_tchar** argv) {
#ifdef MOZ_BACKGROUNDTASKS
  // If the callback has a --backgroundtask switch, consider it a background
  // task. The CheckArg semantics aren't reproduced in full here,
  // there's e.g. no check for a parameter and no case-insensitive comparison.
  for (int i = 1; i < argc; ++i) {
    if (const auto option = mozilla::internal::ReadAsOption(argv[i])) {
      const NS_tchar* arg = option.value();
      if (NS_tstrcmp(arg, NS_T("backgroundtask")) == 0) {
        return true;
      }
    }
  }
#endif  // MOZ_BACKGROUNDTASKS

#if defined(XP_WIN) || defined(XP_MACOSX)
  if (EnvHasValue("MOZ_APP_SILENT_START")) {
    return true;
  }
#endif

  return false;
}

int NS_main(int argc, NS_tchar** argv) {
  LogToOS(NS_T("Updater started"));

  // We may need to tweak our argument list when we launch the Second Updater
  // Invocation (SUI), so we are going to make a copy of our arguments to
  // modify.
  int suiArgc = argc;
  mozilla::UniquePtr<const NS_tchar*[]> suiArgv =
      mozilla::MakeUnique<const NS_tchar*[]>(suiArgc);
  for (int argIndex = 0; argIndex < suiArgc; argIndex++) {
    suiArgv.get()[argIndex] = argv[argIndex];
  }

#ifdef MOZ_MAINTENANCE_SERVICE
  sUsingService = EnvHasValue("MOZ_USING_SERVICE");
  putenv(const_cast<char*>("MOZ_USING_SERVICE="));
#endif

  if (argc == 2 && NS_tstrcmp(argv[1], NS_T("--channels-allowed")) == 0) {
#ifdef MOZ_VERIFY_MAR_SIGNATURE
    int rv = PopulategMARStrings();
    if (rv == OK) {
      printf("Channels Allowed: '%s'\n", gMARStrings.MARChannelID.get());
      return 0;
    }
    printf("Error: %d\n", rv);
    return 1;
#else
      printf("Not Applicable: No support for signature verification\n");
      return 0;
#endif
  }

  // `isDMGInstall` is only ever true for macOS, but we are declaring it here
  // to avoid a ton of extra #ifdef's.
  bool isDMGInstall = false;

#ifdef XP_MACOSX
  if (argc > 2 && NS_tstrcmp(argv[1], NS_T("--openAppBundle")) == 0) {
    LogToOS(NS_T("Opening App Bundle"));
    // We have been asked to open a .app bundle. The path to the .app bundle and
    // any command line arguments have been passed to us as arguments after
    // "--openAppBundle", so remove the first two arguments and launch the .app
    // bundle.
    LaunchMacApp(argc - 2, (const char**)argv + 2);
    return 0;
  }

  // We want to control file permissions explicitly, or else we could end up
  // corrupting installs for other users on the system. Accordingly, set the
  // umask to 0 for all file creations below and reset it on exit. See Bug
  // 1337007
  mozilla::UniquePtr<UmaskContext> umaskContext(new UmaskContext(0));
#endif

#ifdef XP_WIN
  auto isAdmin = mozilla::UserHasAdminPrivileges();
  if (isAdmin.isErr()) {
    fprintf(stderr,
            "Failed to query if the current process has admin privileges.\n");
    return 1;
  }
  auto isLocalSystem = mozilla::UserIsLocalSystem();
  if (isLocalSystem.isErr()) {
    fprintf(
        stderr,
        "Failed to query if the current process has LocalSystem privileges.\n");
    return 1;
  }
#endif

  // Indicates that we are running with elevated privileges.
  // This is only ever true on macOS and Windows. We don't currently have a
  // way of elevating on other platforms.
  // Note that this should not be used to determine whether this is the first or
  // second invocation of the updater, even though the first invocation will
  // _usually_ be unelevated and the second invocation should always be
  // elevated. `gInvocation` can be used for that purpose.
  bool isElevated =
#ifdef XP_WIN
      // While is it technically redundant to check LocalSystem in addition to
      // Admin given the former contains privileges of the latter, we have opt
      // to verify both. A few reasons for this decision include the off chance
      // that the Windows security model changes in the future and weird system
      // setups where someone has modified the group lists in surprising ways.
      //
      // We use this to detect if we were launched from the Maintenance Service
      // under LocalSystem or UAC under the user's account, and therefore can
      // proceed with an install to `Program Files` or `Program Files(x86)`.
      isAdmin.unwrap() || isLocalSystem.unwrap();
#elif defined(XP_MACOSX)
        strstr(argv[0], "/Library/PrivilegedHelperTools/org.mozilla.updater") !=
        nullptr;
#else
      false;
#endif

#ifdef XP_MACOSX
  if (isElevated) {
    LogToOS(NS_T("Updater is elevated"));
    if (!ObtainUpdaterArguments(&argc, &argv, &gMARStrings)) {
      // Won't actually get here because ObtainUpdaterArguments will terminate
      // the current process on failure.
      return 1;
    }
  }

  if (argc == 4 && (strstr(argv[1], "-dmgInstall") != nullptr)) {
    isDMGInstall = true;
    if (isElevated) {
      freeArguments(argc, argv);
      CleanupElevatedMacUpdate(true);
      return 0;
    }
  }
#endif

  if (!isDMGInstall) {
    // Skip update-related code path for DMG installs.

#if defined(MOZ_VERIFY_MAR_SIGNATURE) && defined(MAR_NSS)
    // If using NSS for signature verification, initialize NSS but minimize
    // the portion we depend on by avoiding all of the NSS databases.
    if (NSS_NoDB_Init(nullptr) != SECSuccess) {
      PRErrorCode error = PR_GetError();
      fprintf(stderr, "Could not initialize NSS: %s (%d)",
              PR_ErrorToName(error), (int)error);
      _exit(1);
    }
#endif

    // To process an update the updater command line must at a minimum have the
    // argument version as the first argument, the directory path containing the
    // updater.mar file to process as the second argument, the install directory
    // as the third argument, the directory to apply the update to as the fourth
    // argument, and which updater invocation this is as the fifth argument.
    // When the updater is launched by another process, the PID of the parent
    // process should be provided in the optional sixth argument and the updater
    // will wait on the parent process to exit if the value is non-zero and the
    // process is present. This is necessary due to not being able to update
    // files that are in use on Windows. The optional seventh argument is the
    // callback's working directory and the optional eighth argument is the
    // callback path. The callback is the application to launch after updating
    // and it will be launched when these arguments are provided whether the
    // update was successful or not. All remaining arguments are optional and
    // are passed to the callback when it is launched.
    if (argc < kWaitPidIndex) {
      fprintf(stderr,
              "Usage: updater arg-version patch-dir install-dir apply-to-dir "
              "which-invocation [wait-pid [callback-working-dir callback-path "
              "args...]]\n");
#ifdef XP_MACOSX
      if (isElevated) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
#endif
      return 1;
    }

#if defined(TEST_UPDATER) && defined(XP_WIN)
    // The tests use nsIProcess to launch the updater and it is simpler for the
    // tests to just set an environment variable and have the test updater set
    // the current working directory than it is to set the current working
    // directory in the test itself.
    if (EnvHasValue("CURWORKDIRPATH")) {
      const WCHAR* val = _wgetenv(L"CURWORKDIRPATH");
      NS_tchdir(val);
    }
#endif

    gInvocation = getUpdaterInvocationFromArg(argv[kWhichInvocationIndex]);
    switch (gInvocation) {
      case UpdaterInvocation::Unknown:
        fprintf(stderr, "Invalid which-invocation value: " LOG_S "\n",
                argv[kWhichInvocationIndex]);
        return 1;
      case UpdaterInvocation::First:
        suiArgv.get()[kWhichInvocationIndex] = secondUpdateInvocationArg;
        break;
      default:
        // There is no good reason we should be launching a third updater, but
        // assign something recognizable and unlikely to be used in the future
        // to make any bugs here a bit easier to understand.
        suiArgv.get()[kWhichInvocationIndex] = NS_T("third???");
        break;
    }
  } else { /* else if (isDMGInstall) */
    // We already exited in the other case.
    gInvocation = UpdaterInvocation::First;
  }

  // The directory containing the update information.
  NS_tstrncpy(gPatchDirPath, argv[kPatchDirIndex], MAXPATHLEN);
  gPatchDirPath[MAXPATHLEN - 1] = NS_T('\0');

  if (!isDMGInstall) {
    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kPatchDirIndex])) {
      // Since the status file is written to the patch directory and the patch
      // directory is invalid don't write the status file.
      fprintf(stderr,
              "The patch directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kPatchDirIndex]);
#ifdef XP_MACOSX
      if (isElevated) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
#endif
      return 1;
    }

    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kInstallDirIndex])) {
      WriteStatusFile(INVALID_INSTALL_DIR_PATH_ERROR);
      fprintf(stderr,
              "The install directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kInstallDirIndex]);
#ifdef XP_MACOSX
      if (isElevated) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
#endif
      return 1;
    }

  }  // if (!isDMGInstall)

  // The directory we're going to update to.
  // We copy this string because we need to remove trailing slashes.  The C++
  // standard says that it's always safe to write to strings pointed to by argv
  // elements, but I don't necessarily believe it.
  NS_tstrncpy(gInstallDirPath, argv[kInstallDirIndex], MAXPATHLEN);
  gInstallDirPath[MAXPATHLEN - 1] = NS_T('\0');
  NS_tchar* slash = NS_tstrrchr(gInstallDirPath, NS_SLASH);
  if (slash && !slash[1]) {
    *slash = NS_T('\0');
  }

#ifdef XP_WIN
  bool useService = false;
  bool testOnlyFallbackKeyExists = false;
  // Prevent the updater from falling back from updating with the Maintenance
  // Service to updating without the Service. Used for Service tests.
  // This is set below via the MOZ_NO_SERVICE_FALLBACK environment variable.
  bool noServiceFallback = false;
  // Force the updater to use the Maintenance Service incorrectly, causing it
  // to fail. Used to test the mechanism that allows the updater to fall back
  // from using the Maintenance Service to updating without it.
  // This is set below via the MOZ_FORCE_SERVICE_FALLBACK environment variable.
  bool forceServiceFallback = false;
#endif

  if (!isDMGInstall) {
#ifdef XP_WIN
    // We never want the service to be used unless we build with
    // the maintenance service.
#  ifdef MOZ_MAINTENANCE_SERVICE
    useService = IsUpdateStatusPendingService();
#    ifdef TEST_UPDATER
    noServiceFallback = EnvHasValue("MOZ_NO_SERVICE_FALLBACK");
    putenv(const_cast<char*>("MOZ_NO_SERVICE_FALLBACK="));
    forceServiceFallback = EnvHasValue("MOZ_FORCE_SERVICE_FALLBACK");
    putenv(const_cast<char*>("MOZ_FORCE_SERVICE_FALLBACK="));
    // Our tests run with a different apply directory for each test.
    // We use this registry key on our test machines to store the
    // allowed name/issuers.
    testOnlyFallbackKeyExists = DoesFallbackKeyExist();
#    endif
#  endif

    // Remove everything except close window from the context menu
    {
      HKEY hkApp = nullptr;
      RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications", 0,
                      nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                      &hkApp, nullptr);
      RegCloseKey(hkApp);
      if (RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Classes\\Applications\\updater.exe", 0,
                          nullptr, REG_OPTION_VOLATILE, KEY_SET_VALUE, nullptr,
                          &hkApp, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hkApp, L"IsHostApp", 0, REG_NONE, 0, 0);
        RegSetValueExW(hkApp, L"NoOpenWith", 0, REG_NONE, 0, 0);
        RegSetValueExW(hkApp, L"NoStartPage", 0, REG_NONE, 0, 0);
        RegCloseKey(hkApp);
      }
    }
#endif

  }  // if (!isDMGInstall)

  // If there is a PID specified and it is not '0' then wait for the process to
  // exit.
  NS_tpid pid = 0;
  if (argc > kWaitPidIndex) {
    pid = NS_tatoi(argv[kWaitPidIndex]);
    if (pid == -1) {
      // This is a signal from the parent process that the updater should stage
      // the update.
      sStagedUpdate = true;
    } else if (NS_tstrstr(argv[kWaitPidIndex], NS_T("/replace"))) {
      // We're processing a request to replace the application with a staged
      // update.
      sReplaceRequest = true;
    }
  }

  if (!isDMGInstall) {
    // This check is also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (!IsValidFullPath(argv[kApplyToDirIndex])) {
      WriteStatusFile(INVALID_WORKING_DIR_PATH_ERROR);
      fprintf(stderr,
              "The working directory path is not valid for this "
              "application (" LOG_S ")\n",
              argv[kApplyToDirIndex]);
#ifdef XP_MACOSX
      if (isElevated) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
#endif
      return 1;
    }
    // The directory we're going to update to.
    // We copy this string because we need to remove trailing slashes.  The C++
    // standard says that it's always safe to write to strings pointed to by
    // argv elements, but I don't necessarily believe it.
    NS_tstrncpy(gWorkingDirPath, argv[kApplyToDirIndex], MAXPATHLEN);
    gWorkingDirPath[MAXPATHLEN - 1] = NS_T('\0');
    slash = NS_tstrrchr(gWorkingDirPath, NS_SLASH);
    if (slash && !slash[1]) {
      *slash = NS_T('\0');
    }

    if (argc > kCallbackIndex) {
      if (!IsValidFullPath(argv[kCallbackIndex])) {
        WriteStatusFile(INVALID_CALLBACK_PATH_ERROR);
        fprintf(stderr,
                "The callback file path is not valid for this "
                "application (" LOG_S ")\n",
                argv[kCallbackIndex]);
#ifdef XP_MACOSX
        if (isElevated) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
#endif
        return 1;
      }

      size_t len = NS_tstrlen(gInstallDirPath);
      NS_tchar callbackInstallDir[MAXPATHLEN] = {NS_T('\0')};
      NS_tstrncpy(callbackInstallDir, argv[kCallbackIndex], len);
      if (NS_tstrcmp(gInstallDirPath, callbackInstallDir) != 0) {
        WriteStatusFile(INVALID_CALLBACK_DIR_ERROR);
        fprintf(stderr,
                "The callback file must be located in the "
                "installation directory (" LOG_S ")\n",
                argv[kCallbackIndex]);
#ifdef XP_MACOSX
        if (isElevated) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
#endif
        return 1;
      }

      sUpdateSilently =
          ShouldRunSilently(argc - kCallbackIndex, argv + kCallbackIndex);
    }

  }  // if (!isDMGInstall)

  if (!sUpdateSilently && !isDMGInstall
#ifdef XP_MACOSX
      && !isElevated
#endif
  ) {
    InitProgressUI(&argc, &argv);
  }

#ifdef XP_MACOSX
  if (!isElevated &&
      (!IsRecursivelyWritable(argv[kInstallDirIndex]) || isDMGInstall)) {
    // If the app directory isn't recursively writeable or if this is a DMG
    // install, an elevated helper process is required.
    if (sUpdateSilently) {
      // An elevated update always requires an elevation dialog, so if we are
      // updating silently, don't do an elevated update.
      // This means that we cannot successfully perform silent updates from
      // non-admin accounts on a Mac.
      // It also means that we cannot silently perform the first update by an
      // admin who was not the installing user. Once the first update has been
      // installed, the permissions of the installation directory should be
      // changed such that we don't need to elevate in the future.
      // Firefox shouldn't actually launch the updater at all in this case. This
      // is defense in depth.
      WriteStatusFile(SILENT_UPDATE_NEEDED_ELEVATION_ERROR);
      fprintf(stderr,
              "Skipping update to avoid elevation prompt from silent update.");
    } else {
      UpdateServerThreadArgs threadArgs;
      threadArgs.argc = suiArgc;
      threadArgs.argv = suiArgv.get();
      threadArgs.marChannelID = "";

#  ifdef MOZ_VERIFY_MAR_SIGNATURE
      // Try to populate gMARStrings so that we can pass the resulting MAR
      // channel ID to the elevated updater via IPC. If this fails (observed on
      // some macOS standard-profile elevated updates where the unelevated
      // updater cannot resolve the weak UpdateSettingsGetAcceptedMARChannels
      // symbol from UpdateSettings.framework), proceed with an empty channel
      // ID rather than aborting the elevated update.
      // ArchiveReader::VerifyProductInformation skips the channel-match check
      // when the channel ID is empty; the MAR's cryptographic signature is
      // still verified, preserving the security posture that existed prior to
      // bug 2028575.
      if (PopulategMARStrings() == OK) {
        threadArgs.marChannelID = gMARStrings.MARChannelID.get();
      } else {
        fprintf(stderr,
                "Unable to retrieve MAR channels in unelevated updater; "
                "proceeding with elevation using an empty channel ID.\n");
      }
#  endif  // MOZ_VERIFY_MAR_SIGNATURE

      Thread t1;
      if (t1.Run(ServeElevatedUpdateThreadFunc, &threadArgs) == 0) {
        // Show an indeterminate progress bar while an elevated update is in
        // progress.
        if (!isDMGInstall) {
          ShowProgressUI(true);
        }
      }
      t1.Join();
    }

    LaunchCallbackAndPostProcessApps(argc, argv, std::move(umaskContext));
    return gSucceeded ? 0 : 1;
  }
#endif

#ifdef XP_WIN
  HANDLE updateLockFileHandle = INVALID_HANDLE_VALUE;
#endif

  if (!isDMGInstall) {
    NS_tchar logFilePath[MAXPATHLEN + 1] = {L'\0'};
#ifdef XP_WIN
    if (gInvocation == UpdaterInvocation::Second) {
      // Remove the secure output files so it is easier to determine when new
      // files are created in the unelevated updater.
      RemoveSecureOutputFiles(gPatchDirPath);

      (void)GetSecureOutputFilePath(gPatchDirPath, L".log", logFilePath);
    } else {
      NS_tsnprintf(logFilePath, sizeof(logFilePath) / sizeof(logFilePath[0]),
                   NS_T("%s\\%s"), gPatchDirPath, UpdateLogFilename());
    }
#else
      NS_tsnprintf(logFilePath, sizeof(logFilePath) / sizeof(logFilePath[0]),
                   NS_T("%s/%s"), gPatchDirPath, UpdateLogFilename());
#endif
    LogInit(logFilePath);

    LOG(("sUsingService=%s", sUsingService ? "true" : "false"));
    LOG(("sUpdateSilently=%s", sUpdateSilently ? "true" : "false"));
#ifdef XP_WIN
    // Note that this is not the final value of useService
    LOG(("useService=%s", useService ? "true" : "false"));
#endif
    LOG(("isElevated=%s", isElevated ? "true" : "false"));
    LOG(("gInvocation=%s", getUpdaterInvocationString(gInvocation)));

    if (!WriteStatusFile("applying")) {
      LOG(("failed setting status to 'applying'"));
#ifdef XP_MACOSX
      if (isElevated) {
        freeArguments(argc, argv);
        CleanupElevatedMacUpdate(true);
      }
#endif
      output_finish();
      return 1;
    }

    if (sStagedUpdate) {
      LOG(("Performing a staged update"));
    } else if (sReplaceRequest) {
      LOG(("Performing a replace request"));
    }

    LOG(("PATCH DIRECTORY " LOG_S, gPatchDirPath));
    LOG(("INSTALLATION DIRECTORY " LOG_S, gInstallDirPath));
    LOG(("WORKING DIRECTORY " LOG_S, gWorkingDirPath));

#if defined(XP_WIN)
    // These checks are also performed in workmonitor.cpp since the maintenance
    // service can be called directly.
    if (_wcsnicmp(gWorkingDirPath, gInstallDirPath, MAX_PATH) != 0) {
      if (!sStagedUpdate && !sReplaceRequest) {
        WriteStatusFile(INVALID_APPLYTO_DIR_ERROR);
        LOG(
            ("Installation directory and working directory must be the same "
             "for non-staged updates. Exiting."));
        output_finish();
        return 1;
      }

      NS_tchar workingDirParent[MAX_PATH];
      NS_tsnprintf(workingDirParent,
                   sizeof(workingDirParent) / sizeof(workingDirParent[0]),
                   NS_T("%s"), gWorkingDirPath);
      if (!PathRemoveFileSpecW(workingDirParent)) {
        WriteStatusFile(REMOVE_FILE_SPEC_ERROR);
        LOG(("Error calling PathRemoveFileSpecW: %lu", GetLastError()));
        output_finish();
        return 1;
      }

      if (_wcsnicmp(workingDirParent, gInstallDirPath, MAX_PATH) != 0) {
        WriteStatusFile(INVALID_APPLYTO_DIR_STAGED_ERROR);
        LOG(
            ("The apply-to directory must be the same as or "
             "a child of the installation directory! Exiting."));
        output_finish();
        return 1;
      }
    }
#endif

#ifdef XP_WIN
    if (pid > 0) {
      HANDLE parent = OpenProcess(SYNCHRONIZE, false, (DWORD)pid);
      // May return nullptr if the parent process has already gone away.
      // Otherwise, wait for the parent process to exit before starting the
      // update.
      if (parent) {
        DWORD waitTime = PARENT_WAIT;
#  ifdef TEST_UPDATER
        if (EnvHasValue("MOZ_TEST_SHORTER_WAIT_PID")) {
          // Use a shorter time to wait for the PID to exit for the test.
          waitTime = 100;
        }
#  endif
        DWORD result = WaitForSingleObject(parent, waitTime);
        CloseHandle(parent);
        if (result != WAIT_OBJECT_0) {
          // Continue to update since the parent application sometimes doesn't
          // exit (see bug 1375242) so any fixes to the parent application will
          // be applied instead of leaving the client in a broken state.
          LOG(("The parent process didn't exit! Continuing with update."));
        }
      }
    }
#endif

#ifdef XP_WIN
    if (sReplaceRequest || sStagedUpdate) {
      // On Windows, when performing a stage or replace request the current
      // working directory for the process must be changed so it isn't locked.
      NS_tchar sysDir[MAX_PATH + 1] = {L'\0'};
      if (GetSystemDirectoryW(sysDir, MAX_PATH + 1)) {
        NS_tchdir(sysDir);
      }
    }

    // lastFallbackError keeps track of the last error for the service not being
    // used, in case of an error when fallback is not enabled we write the
    // error to the update.status file.
    // When fallback is disabled (MOZ_NO_SERVICE_FALLBACK does not exist) then
    // we will instead fallback to not using the service and display a UAC
    // prompt.
    int lastFallbackError = FALLBACKKEY_UNKNOWN_ERROR;

    // Check whether a second instance of the updater should be launched by the
    // maintenance service or with the 'runas' verb when write access is denied
    // to the installation directory.
    if (!sUsingService &&
        (argc > kCallbackIndex || sStagedUpdate || sReplaceRequest)) {
      LOG(("Checking whether elevation is needed"));

      NS_tchar updateLockFilePath[MAXPATHLEN];
      if (sStagedUpdate) {
        // When staging an update, the lock file is:
        // <install_dir>\updated.update_in_progress.lock
        NS_tsnprintf(updateLockFilePath,
                     sizeof(updateLockFilePath) / sizeof(updateLockFilePath[0]),
                     NS_T("%s/updated.update_in_progress.lock"),
                     gInstallDirPath);
      } else if (sReplaceRequest) {
        // When processing a replace request, the lock file is:
        // <install_dir>\..\moz_update_in_progress.lock
        NS_tchar installDir[MAXPATHLEN];
        NS_tstrcpy(installDir, gInstallDirPath);
        NS_tchar* slash = (NS_tchar*)NS_tstrrchr(installDir, NS_SLASH);
        *slash = NS_T('\0');
        NS_tsnprintf(updateLockFilePath,
                     sizeof(updateLockFilePath) / sizeof(updateLockFilePath[0]),
                     NS_T("%s\\moz_update_in_progress.lock"), installDir);
      } else {
        // In the non-staging update case, the lock file is:
        // <install_dir>\<app_name>.exe.update_in_progress.lock
        NS_tsnprintf(updateLockFilePath,
                     sizeof(updateLockFilePath) / sizeof(updateLockFilePath[0]),
                     NS_T("%s.update_in_progress.lock"), argv[kCallbackIndex]);
      }

      // The update_in_progress.lock file should only exist during an update. In
      // case it exists attempt to remove it and exit if that fails to prevent
      // simultaneous updates occurring.
      if (NS_tremove(updateLockFilePath) && errno != ENOENT) {
        // Try to fall back to the old way of doing updates if a staged
        // update fails.
        if (sReplaceRequest) {
          // Note that this could fail, but if it does, there isn't too much we
          // can do in order to recover anyways.
          WriteStatusFile("pending");
        } else if (sStagedUpdate) {
          WriteStatusFile(DELETE_ERROR_STAGING_LOCK_FILE);
        }
        LOG(("Update already in progress! Exiting"));
        output_finish();
        return 1;
      }

      // If we're running from the service, then we were started with the same
      // token as the service so the permissions are already dropped.  If we're
      // running from an elevated updater that was started from an unelevated
      // updater, then we drop the permissions here. We do not drop the
      // permissions on the originally called updater because we use its token
      // to start the callback application.
      if (isElevated) {
        // Disable every privilege we don't need. Processes started using
        // CreateProcess will use the same token as this process.
        UACHelper::DisablePrivileges(nullptr);
      }

      updateLockFileHandle =
          CreateFileW(updateLockFilePath, GENERIC_READ | GENERIC_WRITE, 0,
                      nullptr, OPEN_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);

      if (updateLockFileHandle == INVALID_HANDLE_VALUE) {
        LOG(("Failed to open update lock file: %lu", GetLastError()));
      } else {
        LOG(("Successfully opened lock file"));
      }

      bool isEnterprise = EnterprisePolicies::InDistribution(gInstallDirPath) ||
                          EnterprisePolicies::InRegistry(L"" MOZ_APP_BASENAME);
#  ifdef TEST_UPDATER
      const wchar_t* forceEnvVar = _wgetenv(L"MOZ_TEST_FORCE_ENTERPRISE");
      if (forceEnvVar) {
        isEnterprise = wcscmp(forceEnvVar, L"1") == 0;
      }
#  endif

      if (isEnterprise) {
        LOG(("Enterprise policies detected"));
        EnterprisePoliciesFlagFile::Add(gPatchDirPath);
      } else {
        LOG(("No enterprise policies detected"));
      }

      if (updateLockFileHandle == INVALID_HANDLE_VALUE ||
          (useService && testOnlyFallbackKeyExists &&
           (noServiceFallback || forceServiceFallback))) {
        LOG(("Can't open lock file - seems like we need elevation"));

#  ifdef MOZ_MAINTENANCE_SERVICE
// Only invoke the service for installations in Program Files.
// This check is duplicated in workmonitor.cpp because the service can
// be invoked directly without going through the updater.
#    ifndef TEST_UPDATER
        if (useService) {
          useService = IsProgramFilesPath(gInstallDirPath);
          LOG(("After checking IsProgramFilesPath, useService=%s",
               useService ? "true" : "false"));
        }
#    endif

        // Make sure the path to the updater to use for the update is on local.
        // We do this check to make sure that file locking is available for
        // race condition security checks.
        if (useService) {
          BOOL isLocal = FALSE;
          useService = IsLocalFile(argv[0], isLocal) && isLocal;
          LOG(("After checking IsLocalFile, useService=%s",
               useService ? "true" : "false"));
        }

        // If we have unprompted elevation we should NOT use the service
        // for the update. Service updates happen with the SYSTEM account
        // which has more privs than we need to update with.
        // Windows 8 provides a user interface so users can configure this
        // behavior and it can be configured in the registry in all Windows
        // versions that support UAC.
        if (useService) {
          BOOL unpromptedElevation;
          if (IsUnpromptedElevation(unpromptedElevation)) {
            useService = !unpromptedElevation;
            LOG(("After checking IsUnpromptedElevation, useService=%s",
                 useService ? "true" : "false"));
          }
        }

        // Make sure the service registry entries for the installation path
        // are available.  If not don't use the service.
        if (useService) {
          WCHAR maintenanceServiceKey[MAX_PATH + 1];
          if (CalculateRegistryPathFromFilePath(gInstallDirPath,
                                                maintenanceServiceKey)) {
            HKEY baseKey = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, maintenanceServiceKey, 0,
                              KEY_READ | KEY_WOW64_64KEY,
                              &baseKey) == ERROR_SUCCESS) {
              RegCloseKey(baseKey);
            } else {
#    ifdef TEST_UPDATER
              useService = testOnlyFallbackKeyExists;
              LOG(("After failing to open maintenanceServiceKey, useService=%s",
                   useService ? "true" : "false"));
#    endif
              if (!useService) {
                lastFallbackError = FALLBACKKEY_NOKEY_ERROR;
              }
            }
          } else {
            useService = false;
            lastFallbackError = FALLBACKKEY_REGPATH_ERROR;
            LOG(("Can't get registry certificate location. useService=false"));
          }
        }

        // Originally we used to write "pending" to update.status before
        // launching the service command.  This is no longer needed now
        // since the service command is launched from updater.exe.  If anything
        // fails in between, we can fall back to using the normal update process
        // on our own.

        // If we still want to use the service try to launch the service
        // command for the update.
        if (useService) {
          // Get the secure ID before trying to update so it is possible to
          // determine if the updater or the maintenance service has created a
          // new one.
          char uuidStringBefore[UUID_LEN] = {'\0'};
          bool checkID = GetSecureID(uuidStringBefore);
          // Write a catchall service failure status in case it fails without
          // changing the status.
          WriteStatusFile(SERVICE_UPDATE_STATUS_UNCHANGED);

          int serviceArgc = argc;
          if (forceServiceFallback && serviceArgc > kPatchDirIndex) {
            // To force the service to fail, we can just pass it too few
            // arguments. However, we don't want to pass it no arguments,
            // because then it won't have enough information to write out the
            // update status file telling us that it failed.
            serviceArgc = kPatchDirIndex + 1;
          }

          // If the update couldn't be started, then set useService to false so
          // we do the update the old way.
          DWORD launchResult = LaunchServiceSoftwareUpdateCommand(
              serviceArgc, (LPCWSTR*)suiArgv.get());
          useService = (launchResult == ERROR_SUCCESS);
          // If the command was launched then wait for the service to be done.
          if (useService) {
            LOG(("Launched service successfully"));
            bool showProgressUI = false;
            // Never show the progress UI when staging updates or in a
            // background task.
            if (!sStagedUpdate && !sUpdateSilently) {
              // We need to call this separately instead of allowing
              // ShowProgressUI to initialize the strings because the service
              // will move the ini file out of the way when running updater.
              showProgressUI = !InitProgressUIStrings();
            }

            // Wait for the service to stop for 5 seconds.  If the service
            // has still not stopped then show an indeterminate progress bar.
            DWORD lastState = WaitForServiceStop(SVC_NAME, 5);
            if (lastState != SERVICE_STOPPED) {
              Thread t1;
              if (t1.Run(WaitForServiceFinishThread, nullptr) == 0 &&
                  showProgressUI) {
                ShowProgressUI(true, false);
              }
              t1.Join();
            }

            lastState = WaitForServiceStop(SVC_NAME, 1);
            if (lastState != SERVICE_STOPPED) {
              // If the service doesn't stop after 10 minutes there is
              // something seriously wrong.
              lastFallbackError = FALLBACKKEY_SERVICE_NO_STOP_ERROR;
              useService = false;
              LOG(("Service didn't stop after 10 minutes. useService=false"));
            } else {
              LOG(("Service stop detected."));
              // Copy the secure output files if the secure ID has changed.
              gCopyOutputFiles = true;
              char uuidStringAfter[UUID_LEN] = {'\0'};
              if (checkID && GetSecureID(uuidStringAfter) &&
                  strncmp(uuidStringBefore, uuidStringAfter,
                          sizeof(uuidStringBefore)) == 0) {
                LOG(
                    ("The secure ID hasn't changed after launching the updater "
                     "using the service"));
                gCopyOutputFiles = false;
              }
              if (gCopyOutputFiles && !sStagedUpdate && !noServiceFallback) {
                // If the Maintenance Service fails for a Service-specific
                // reason, we ought to fall back to attempting to update
                // without the Service.
                // However, we need the secure output files to be able to be
                // check the error code, and we can't fall back when we are
                // staging, because we will need to show a UAC.
                bool updateFailed;
                mozilla::Maybe<int> maybeErrorCode;
                bool success =
                    IsSecureUpdateStatusFailed(updateFailed, &maybeErrorCode);
                if (success && updateFailed && maybeErrorCode.isSome() &&
                    IsServiceSpecificErrorCode(maybeErrorCode.value())) {
                  useService = false;
                  LOG(("Service-specific failure detected. useService=false"));
                }
              }
            }
          } else {
            LOG(("Launching service failed. useService=false, launchResult=%lu",
                 launchResult));
            lastFallbackError = FALLBACKKEY_LAUNCH_ERROR;
          }
        }
#  endif

        // If the service can't be used when staging an update, make sure that
        // the UAC prompt is not shown!
        if (!useService && sStagedUpdate) {
          if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(updateLockFileHandle);
          }
          // Set an error so the failure is reported. This will be reset
          // to pending so the update can be applied during the next startup,
          // see bug 1552853.
          WriteStatusFile(UNEXPECTED_STAGING_ERROR);
          LOG(
              ("Non-critical update staging error! Falling back to non-staged "
               "updates and exiting"));
          output_finish();
          // We don't have a callback when staging so we can just exit.
          return 0;
        }

        // If the service can't be used when in a background task, make sure
        // that the UAC prompt is not shown!
        if (!useService && sUpdateSilently) {
          if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(updateLockFileHandle);
          }
          // Set an error so we don't get into an update loop when the callback
          // runs. This will be reset to pending by handleUpdateFailure in
          // UpdateService.sys.mjs.
          WriteStatusFile(SILENT_UPDATE_NEEDED_ELEVATION_ERROR);
          LOG(("Skipping update to avoid UAC prompt from background task."));
          output_finish();

          LaunchCallbackApp(argv[kCallbackWorkingDirIndex],
                            argc - kCallbackIndex, argv + kCallbackIndex,
                            sUsingService);
          return 0;
        }

        // If we didn't want to use the service at all, or if an update was
        // already happening, or launching the service command failed, then
        // launch the elevated updater.exe as we do without the service.
        // We don't launch the elevated updater in the case that we did have
        // write access all along because in that case the only reason we're
        // using the service is because we are testing.
        if (!useService && !noServiceFallback &&
            (updateLockFileHandle == INVALID_HANDLE_VALUE ||
             forceServiceFallback)) {
          LOG(("Elevating via a UAC prompt"));
          // Get the secure ID before trying to update so it is possible to
          // determine if the updater has created a new one.
          char uuidStringBefore[UUID_LEN] = {'\0'};
          bool checkID = GetSecureID(uuidStringBefore);
          // Write a catchall failure status in case it fails without changing
          // the status.
          WriteStatusFile(UPDATE_STATUS_UNCHANGED);

          SHELLEXECUTEINFO sinfo;
          memset(&sinfo, 0, sizeof(SHELLEXECUTEINFO));
          sinfo.cbSize = sizeof(SHELLEXECUTEINFO);
          sinfo.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_FLAG_DDEWAIT |
                        SEE_MASK_NOCLOSEPROCESS;
          sinfo.hwnd = nullptr;
          sinfo.lpFile = argv[0];
          if (forceServiceFallback) {
            // In testing, we don't actually want a UAC prompt. We should
            // already have the permissions such that we shouldn't need it.
            // And we don't have a good way of accepting the prompt in
            // automation.
            sinfo.lpVerb = L"open";
            // This argument is what lets the updater that we spawn below know
            // that it's the second updater invocation. We are going to change
            // it so that it doesn't know that it runs as the first updater
            // invocation would. Doing this makes this an imperfect test of
            // the service fallback functionality because it changes how the
            // second updater invocation runs. One of the effects of this is
            // that the secure output files will not be used. So that
            // functionality won't really be covered by testing. But writing to
            // those files would require that the updater run with actual
            // elevation, which we have no way to do with in automation.
            suiArgv.get()[kWhichInvocationIndex] = firstUpdateInvocationArg;
            // We need to let go of the update lock to let the un-elevated
            // updater we are about to spawn update.
            if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
              CloseHandle(updateLockFileHandle);
            }
          } else {
            sinfo.lpVerb = L"runas";
          }
          sinfo.nShow = SW_SHOWNORMAL;

          auto cmdLine =
              mozilla::MakeCommandLine(suiArgc - 1, suiArgv.get() + 1);
          if (!cmdLine) {
            LOG(("Failed to make command line! Exiting"));
            output_finish();
            return 1;
          }
          sinfo.lpParameters = cmdLine.get();
          LOG(("Using UAC to launch \"%S\"", sinfo.lpParameters));

          bool result = ShellExecuteEx(&sinfo);

          if (result) {
            LOG(("Elevation successful. Waiting for elevated updater to run."));
            WaitForSingleObject(sinfo.hProcess, INFINITE);
            LOG(("Elevated updater has finished running."));
            CloseHandle(sinfo.hProcess);

            // Copy the secure output files if the secure ID has changed.
            gCopyOutputFiles = true;
            char uuidStringAfter[UUID_LEN] = {'\0'};
            if (checkID && GetSecureID(uuidStringAfter) &&
                strncmp(uuidStringBefore, uuidStringAfter,
                        sizeof(uuidStringBefore)) == 0) {
              LOG(
                  ("The secure ID hasn't changed after launching the updater "
                   "using runas"));
              gCopyOutputFiles = false;
            }
          } else {
            // Don't copy the secure output files if the elevation request was
            // canceled since the status file written below is in the patch
            // directory. At this point it should already be set to false and
            // this is set here to make it clear that it should be false at this
            // point and to prevent future changes from regressing this code.
            gCopyOutputFiles = false;
            WriteStatusFile(ELEVATION_CANCELED);
            LOG(("Elevation canceled."));
          }
        } else {
          LOG(("Not showing a UAC prompt."));
          LOG(("useService=%s", useService ? "true" : "false"));
          LOG(("noServiceFallback=%s", noServiceFallback ? "true" : "false"));
          LOG(("updateLockFileHandle%sINVALID_HANDLE_VALUE",
               updateLockFileHandle == INVALID_HANDLE_VALUE ? "==" : "!="));
          LOG(("forceServiceFallback=%s",
               forceServiceFallback ? "true" : "false"));
        }

        // If we started the elevated updater, and it finished, check the secure
        // update status file to make sure that it succeeded, and if it did we
        // need to launch the PostUpdate process in the unelevated updater which
        // is running in the current user's session. Note that we don't need to
        // do this when staging an update since the PostUpdate step runs during
        // the replace request.
        if (!sStagedUpdate) {
          bool updateStatusSucceeded = false;
          if (IsSecureUpdateStatusSucceeded(updateStatusSucceeded) &&
              updateStatusSucceeded) {
            LOG(("Running LaunchWinPostProcess"));
            if (!LaunchWinPostProcess(gInstallDirPath, gPatchDirPath,
                                      PostUpdateTarget::CurrentUser)) {
              LOG(("Failed to run LaunchWinPostProcess"));
            }
          } else {
            LOG(
                ("Not running LaunchWinPostProcess because update status is not"
                 "'succeeded'."));
          }
        }

        // Flag removed by the unelevated process during the two-process update
        EnterprisePoliciesFlagFile::Remove(gPatchDirPath);

        if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
          CloseHandle(updateLockFileHandle);
        }

        if (!useService && noServiceFallback) {
          // When the service command was not launched at all.
          // We should only reach this code path because we had write access
          // all along to the directory and a fallback key existed, and we
          // have fallback disabled (MOZ_NO_SERVICE_FALLBACK env var exists).
          // We only currently use this env var from XPCShell tests.
          gCopyOutputFiles = false;
          WriteStatusFile(lastFallbackError);
        }

        // The logging output needs to be finished before launching the callback
        // application so the update status file contains the value from the
        // secure directory used by the maintenance service and the elevated
        // updater.
        LOG(("Update complete"));
        output_finish();
        if (argc > kCallbackIndex) {
          LaunchCallbackApp(argv[kCallbackWorkingDirIndex],
                            argc - kCallbackIndex, argv + kCallbackIndex,
                            sUsingService);
        }
        return 0;

        // This is the end of the code block for launching another instance of
        // the updater using either the maintenance service or with the 'runas'
        // verb when the updater doesn't have write access to the installation
        // directory.
      }
      // This is the end of the code block when the updater was not launched by
      // the service that checks whether the updater has write access to the
      // installation directory.
    }
    // If we made it this far this is the updater instance that will perform the
    // actual update and gCopyOutputFiles will be false (e.g. the default
    // value).
    LOG(("Going to update via this updater instance."));
#endif

    if (sStagedUpdate) {
#ifdef TEST_UPDATER
      // This allows testing that the correct UI after an update staging failure
      // that falls back to applying the update on startup. It is simulated due
      // to the difficulty of creating the conditions for this type of staging
      // failure.
      if (EnvHasValue("MOZ_TEST_STAGING_ERROR")) {
#  ifdef XP_WIN
        if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
          CloseHandle(updateLockFileHandle);
        }
#  endif
        // WRITE_ERROR is one of the cases where the staging failure falls back
        // to applying the update on startup.
        WriteStatusFile(WRITE_ERROR);
        output_finish();
        return 0;
      }
#endif
      // When staging updates, blow away the old installation directory and
      // create it from scratch.
      ensure_remove_recursive(gWorkingDirPath);
    }
    if (!sReplaceRequest) {
      // Try to create the destination directory if it doesn't exist
      int rv = NS_tmkdir(gWorkingDirPath, 0755);
      if (rv != OK && errno != EEXIST) {
#ifdef XP_MACOSX
        if (isElevated) {
          freeArguments(argc, argv);
          CleanupElevatedMacUpdate(true);
        }
#endif
        output_finish();
        return 1;
      }
    }

#ifdef XP_WIN
    NS_tchar applyDirLongPath[MAXPATHLEN];
    if (!GetLongPathNameW(
            gWorkingDirPath, applyDirLongPath,
            sizeof(applyDirLongPath) / sizeof(applyDirLongPath[0]))) {
      WriteStatusFile(WRITE_ERROR_APPLY_DIR_PATH);
      LOG(("NS_main: unable to find apply to dir: " LOG_S, gWorkingDirPath));
      output_finish();
      EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 1);
      if (argc > kCallbackIndex) {
        LaunchCallbackApp(argv[kCallbackWorkingDirIndex], argc - kCallbackIndex,
                          argv + kCallbackIndex, sUsingService);
      }
      return 1;
    }

    HANDLE callbackFile = INVALID_HANDLE_VALUE;
    if (argc > kCallbackIndex) {
      // If the callback executable is specified it must exist for a successful
      // update.  It is important we null out the whole buffer here because
      // later we make the assumption that the callback application is inside
      // the apply-to dir.  If we don't have a fully null'ed out buffer it can
      // lead to stack corruption which causes crashes and other problems.
      NS_tchar callbackLongPath[MAXPATHLEN];
      ZeroMemory(callbackLongPath, sizeof(callbackLongPath));
      NS_tchar* targetPath = argv[kCallbackIndex];
      NS_tchar buffer[MAXPATHLEN * 2] = {NS_T('\0')};
      size_t bufferLeft = MAXPATHLEN * 2;
      if (sReplaceRequest) {
        // In case of replace requests, we should look for the callback file in
        // the destination directory.
        size_t commonPrefixLength =
            PathCommonPrefixW(argv[kCallbackIndex], gInstallDirPath, nullptr);
        NS_tchar* p = buffer;
        NS_tstrncpy(p, argv[kCallbackIndex], commonPrefixLength);
        p += commonPrefixLength;
        bufferLeft -= commonPrefixLength;
        NS_tstrncpy(p, gInstallDirPath + commonPrefixLength, bufferLeft);

        size_t len = NS_tstrlen(gInstallDirPath + commonPrefixLength);
        p += len;
        bufferLeft -= len;
        *p = NS_T('\\');
        ++p;
        bufferLeft--;
        *p = NS_T('\0');
        NS_tchar installDir[MAXPATHLEN];
        NS_tstrcpy(installDir, gInstallDirPath);
        size_t callbackPrefixLength =
            PathCommonPrefixW(argv[kCallbackIndex], installDir, nullptr);
        NS_tstrncpy(p,
                    argv[kCallbackIndex] +
                        std::max(callbackPrefixLength, commonPrefixLength),
                    bufferLeft);
        targetPath = buffer;
      }
      if (!GetLongPathNameW(
              targetPath, callbackLongPath,
              sizeof(callbackLongPath) / sizeof(callbackLongPath[0]))) {
        WriteStatusFile(WRITE_ERROR_CALLBACK_PATH);
        LOG(("NS_main: unable to find callback file: " LOG_S, targetPath));
        output_finish();
        EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 1);
        if (argc > kCallbackIndex) {
          LaunchCallbackApp(argv[kCallbackWorkingDirIndex],
                            argc - kCallbackIndex, argv + kCallbackIndex,
                            sUsingService);
        }
        return 1;
      }

      // Doing this is only necessary when we're actually applying a patch.
      if (!sReplaceRequest) {
        int len = NS_tstrlen(applyDirLongPath);
        NS_tchar* s = callbackLongPath;
        NS_tchar* d = gCallbackRelPath;
        // advance to the apply to directory and advance past the trailing
        // backslash if present.
        s += len;
        if (*s == NS_T('\\')) {
          ++s;
        }

        // Copy the string and replace backslashes with forward slashes along
        // the way.
        do {
          if (*s == NS_T('\\')) {
            *d = NS_T('/');
          } else {
            *d = *s;
          }
          ++s;
          ++d;
        } while (*s);
        *d = NS_T('\0');
        ++d;

        const size_t callbackBackupPathBufSize =
            sizeof(gCallbackBackupPath) / sizeof(gCallbackBackupPath[0]);
        const int callbackBackupPathLen =
            NS_tsnprintf(gCallbackBackupPath, callbackBackupPathBufSize,
                         NS_T("%s" CALLBACK_BACKUP_EXT), argv[kCallbackIndex]);

        if (callbackBackupPathLen < 0 ||
            callbackBackupPathLen >=
                static_cast<int>(callbackBackupPathBufSize)) {
          WriteStatusFile(USAGE_ERROR);
          LOG(("NS_main: callback backup path truncated"));
          output_finish();

          // Don't attempt to launch the callback when the callback path is
          // longer than expected.
          EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 1);
          return 1;
        }

        // Make a copy of the callback executable so it can be read when
        // patching.
        if (!CopyFileW(argv[kCallbackIndex], gCallbackBackupPath, false)) {
          DWORD copyFileError = GetLastError();
          if (copyFileError == ERROR_ACCESS_DENIED) {
            WriteStatusFile(WRITE_ERROR_ACCESS_DENIED);
          } else {
            WriteStatusFile(WRITE_ERROR_CALLBACK_APP);
          }
          LOG(("NS_main: failed to copy callback file " LOG_S
               " into place at " LOG_S,
               argv[kCallbackIndex], gCallbackBackupPath));
          output_finish();
          EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 1);
          LaunchCallbackApp(argv[kCallbackWorkingDirIndex],
                            argc - kCallbackIndex, argv + kCallbackIndex,
                            sUsingService);
          return 1;
        }

        // Since the process may be signaled as exited by WaitForSingleObject
        // before the release of the executable image try to lock the main
        // executable file multiple times before giving up.  If we end up giving
        // up, we won't fail the update.
        const int max_retries = 10;
        int retries = 1;
        DWORD lastWriteError = 0;
        do {
          // By opening a file handle wihout FILE_SHARE_READ to the callback
          // executable, the OS will prevent launching the process while it is
          // being updated.
          callbackFile = CreateFileW(targetPath, DELETE | GENERIC_WRITE,
                                     // allow delete, rename, and write
                                     FILE_SHARE_DELETE | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
          if (callbackFile != INVALID_HANDLE_VALUE) {
            break;
          }

          lastWriteError = GetLastError();
          LOG(
              ("NS_main: callback app file open attempt %d failed. "
               "File: " LOG_S ". Last error: %lu",
               retries, targetPath, lastWriteError));

          Sleep(100);
        } while (++retries <= max_retries);

        // CreateFileW will fail if the callback executable is already in use.
        if (callbackFile == INVALID_HANDLE_VALUE) {
          bool proceedWithoutExclusive = true;

          // Fail the update if the last error was not a sharing violation.
          if (lastWriteError != ERROR_SHARING_VIOLATION) {
            LOG((
                "NS_main: callback app file in use, failed to exclusively open "
                "executable file: " LOG_S,
                argv[kCallbackIndex]));
            if (lastWriteError == ERROR_ACCESS_DENIED) {
              WriteStatusFile(WRITE_ERROR_ACCESS_DENIED);
            } else {
              WriteStatusFile(WRITE_ERROR_CALLBACK_APP);
            }

            proceedWithoutExclusive = false;
          }

          // Fail even on sharing violation from a background task, since a
          // background task has a higher risk of interfering with a running
          // app. Note that this does not apply when staging (when an exclusive
          // lock isn't necessary), as there is no callback.
          if (lastWriteError == ERROR_SHARING_VIOLATION && sUpdateSilently) {
            LOG((
                "NS_main: callback app file in use, failed to exclusively open "
                "executable file from background task: " LOG_S,
                argv[kCallbackIndex]));
            WriteStatusFile(BACKGROUND_TASK_SHARING_VIOLATION);

            proceedWithoutExclusive = false;
          }

          if (!proceedWithoutExclusive) {
            if (NS_tremove(gCallbackBackupPath) && errno != ENOENT) {
              LOG(
                  ("NS_main: unable to remove backup of callback app file, "
                   "path: " LOG_S,
                   gCallbackBackupPath));
            }
            output_finish();
            EXIT_IF_SECOND_UPDATER_INSTANCE(updateLockFileHandle, 1);
            LaunchCallbackApp(argv[kCallbackWorkingDirIndex],
                              argc - kCallbackIndex, argv + kCallbackIndex,
                              sUsingService);
            return 1;
          }

          LOG(
              ("NS_main: callback app file in use, continuing without "
               "exclusive access for executable file: " LOG_S,
               argv[kCallbackIndex]));
        }
      }
    }

    // DELETE_DIR is not required when performing a staged update or replace
    // request; it can be used during a replace request but then it doesn't
    // use gDeleteDirPath.
    if (!sStagedUpdate && !sReplaceRequest) {
      // The directory to move files that are in use to on Windows. This
      // directory will be deleted after the update is finished, on OS reboot
      // using MoveFileEx if it contains files that are in use, or by the post
      // update process after the update finishes. On Windows when performing a
      // normal update (e.g. the update is not a staged update and is not a
      // replace request) gWorkingDirPath is the same as gInstallDirPath and
      // gWorkingDirPath is used because it is the destination directory.
      NS_tsnprintf(gDeleteDirPath,
                   sizeof(gDeleteDirPath) / sizeof(gDeleteDirPath[0]),
                   NS_T("%s/%s"), gWorkingDirPath, DELETE_DIR);

      if (NS_taccess(gDeleteDirPath, F_OK)) {
        NS_tmkdir(gDeleteDirPath, 0755);
      }
    }
#endif /* XP_WIN */

    // Run update process on a background thread. ShowProgressUI may return
    // before QuitProgressUI has been called, so wait for UpdateThreadFunc to
    // terminate. Avoid showing the progress UI when staging an update, or if
    // this is an elevated process on macOS.
    Thread t;
    if (t.Run(UpdateThreadFunc, nullptr) == 0) {
      if (!sStagedUpdate && !sReplaceRequest && !sUpdateSilently
#ifdef XP_MACOSX
          && !isElevated
#endif
      ) {
        ShowProgressUI();
      }
    }
    t.Join();

#ifdef XP_WIN
    if (argc > kCallbackIndex && !sReplaceRequest) {
      if (callbackFile != INVALID_HANDLE_VALUE) {
        CloseHandle(callbackFile);
      }
      // Remove the copy of the callback executable.
      if (NS_tremove(gCallbackBackupPath) && errno != ENOENT) {
        LOG(
            ("NS_main: non-fatal error removing backup of callback app file, "
             "path: " LOG_S,
             gCallbackBackupPath));
      }
    }

    if (!sStagedUpdate && !sReplaceRequest && _wrmdir(gDeleteDirPath)) {
      LOG(("NS_main: unable to remove directory: " LOG_S ", err: %d",
           DELETE_DIR, errno));
      // The directory probably couldn't be removed due to it containing files
      // that are in use and will be removed on OS reboot. The call to remove
      // the directory on OS reboot is done after the calls to remove the files
      // so the files are removed first on OS reboot since the directory must be
      // empty for the directory removal to be successful. The MoveFileEx call
      // to remove the directory on OS reboot will fail if the process doesn't
      // have write access to the HKEY_LOCAL_MACHINE registry key but this is ok
      // since the installer / uninstaller will delete the directory along with
      // its contents after an update is applied, on reinstall, and on
      // uninstall.
      if (MoveFileEx(gDeleteDirPath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        LOG(("NS_main: directory will be removed on OS reboot: " LOG_S,
             DELETE_DIR));
      } else {
        LOG(
            ("NS_main: failed to schedule OS reboot removal of "
             "directory: " LOG_S,
             DELETE_DIR));
      }
    }
#endif /* XP_WIN */

  }  // if (!isDMGInstall)

#ifdef XP_MACOSX
  if (isElevated) {
    SetGroupOwnershipAndPermissions(gInstallDirPath);
    freeArguments(argc, argv);
    CleanupElevatedMacUpdate(false);
  } else if (IsOwnedByGroupAdmin(gInstallDirPath)) {
    // If the group ownership of the Firefox .app bundle was set to the "admin"
    // group during a previous elevated update, we need to ensure that all files
    // in the bundle have group ownership of "admin" as well as write permission
    // for the group to not break updates in the future.
    SetGroupOwnershipAndPermissions(gInstallDirPath);
  }
#endif /* XP_MACOSX */

  LOG(("Running LaunchCallbackAndPostProcessApps"));

  int retVal = LaunchCallbackAndPostProcessApps(argc, argv
#ifdef XP_WIN
                                                ,
                                                updateLockFileHandle
#elif XP_MACOSX
                                                  ,
                                                  std::move(umaskContext)
#endif
  );

  return retVal ? retVal : (gSucceeded ? 0 : 1);
}

class ActionList {
 public:
  ActionList() : mFirst(nullptr), mLast(nullptr), mCount(0) {}
  ~ActionList();

  void Append(Action* action);
  int Prepare();
  int Draft();
  int Execute();
  void Finish(int status);

 private:
  Action* mFirst;
  Action* mLast;
  int mCount;
};

ActionList::~ActionList() {
  Action* a = mFirst;
  while (a) {
    Action* b = a;
    a = a->mNext;
    delete b;
  }
}

void ActionList::Append(Action* action) {
  if (mLast) {
    mLast->mNext = action;
  } else {
    mFirst = action;
  }

  mLast = action;
  mCount++;
}

int ActionList::Prepare() {
  // If the action list is empty then we should fail in order to signal that
  // something has gone wrong. Otherwise we report success when nothing is
  // actually done. See bug 327140.
  if (mCount == 0) {
    LOG(("empty action list"));
    return MAR_ERROR_EMPTY_ACTION_LIST;
  }

  Action* a = mFirst;
  int i = 0;
  while (a) {
    int rv = a->Prepare();
    if (rv) {
      return rv;
    }

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

int ActionList::Draft() {
  int currentProgress = 0, maxProgress = 0;
  Action* a = mFirst;
  while (a) {
    maxProgress += a->mProgressCost;
    a = a->mNext;
  }

  a = mFirst;
  while (a) {
    int rv = a->Draft();
    if (rv) {
      LOG(("### draft failed"));
      return rv;
    }

    currentProgress += a->mProgressCost;
    float percent = float(currentProgress) / float(maxProgress);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

int ActionList::Execute() {
  int currentProgress = 0, maxProgress = 0;
  Action* a = mFirst;
  while (a) {
    maxProgress += a->mProgressCost;
    a = a->mNext;
  }

  a = mFirst;
  while (a) {
    int rv = a->Execute();
    if (rv) {
      LOG(("### execution failed"));
      return rv;
    }

    currentProgress += a->mProgressCost;
    float percent = float(currentProgress) / float(maxProgress);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE +
                     PROGRESS_EXECUTE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

void ActionList::Finish(int status) {
  Action* a = mFirst;
  int i = 0;
  while (a) {
    a->Finish(status);

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE + PROGRESS_DRAFT_SIZE +
                     PROGRESS_EXECUTE_SIZE + PROGRESS_FINISH_SIZE * percent);

    a = a->mNext;
  }

  if (status == OK) {
    gSucceeded = true;
  }
}

#ifdef XP_WIN
int add_dir_entries(const NS_tchar* dirpath, ActionList* list) {
  int rv = OK;
  WIN32_FIND_DATAW finddata;
  HANDLE hFindFile;
  NS_tchar searchspec[MAXPATHLEN];
  NS_tchar foundpath[MAXPATHLEN];

  NS_tsnprintf(searchspec, sizeof(searchspec) / sizeof(searchspec[0]),
               NS_T("%s*"), dirpath);
  mozilla::UniquePtr<const NS_tchar> pszSpec(get_full_path(searchspec));

  hFindFile = FindFirstFileW(pszSpec.get(), &finddata);
  if (hFindFile != INVALID_HANDLE_VALUE) {
    do {
      // Don't process the current or parent directory.
      if (NS_tstrcmp(finddata.cFileName, NS_T(".")) == 0 ||
          NS_tstrcmp(finddata.cFileName, NS_T("..")) == 0) {
        continue;
      }

      NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                   NS_T("%s%s"), dirpath, finddata.cFileName);
      if (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                     NS_T("%s/"), foundpath);
        // Recurse into the directory.
        rv = add_dir_entries(foundpath, list);
        if (rv) {
          LOG(("add_dir_entries error: " LOG_S ", err: %d", foundpath, rv));
          FindClose(hFindFile);
          return rv;
        }
      } else {
        // Add the file to be removed to the ActionList.
        NS_tchar* quotedpath = get_quoted_path(foundpath);
        if (!quotedpath) {
          FindClose(hFindFile);
          return PARSE_ERROR;
        }

        mozilla::UniquePtr<Action> action(new RemoveFile());
        rv = action->Parse(quotedpath);
        if (rv) {
          LOG(("add_dir_entries Parse error on recurse: " LOG_S ", err: %d",
               quotedpath, rv));
          free(quotedpath);
          FindClose(hFindFile);
          return rv;
        }
        free(quotedpath);

        list->Append(action.release());
      }
    } while (FindNextFileW(hFindFile, &finddata) != 0);

    FindClose(hFindFile);
    {
      // Add the directory to be removed to the ActionList.
      NS_tchar* quotedpath = get_quoted_path(dirpath);
      if (!quotedpath) {
        return PARSE_ERROR;
      }

      mozilla::UniquePtr<Action> action(new RemoveDir());
      rv = action->Parse(quotedpath);
      if (rv) {
        LOG(("add_dir_entries Parse error on close: " LOG_S ", err: %d",
             quotedpath, rv));
      } else {
        list->Append(action.release());
      }
      free(quotedpath);
    }
  }

  return rv;
}

#elif defined(HAVE_FTS_H)
  int add_dir_entries(const NS_tchar* dirpath, ActionList* list) {
    int rv = OK;
    FTS* ftsdir;
    FTSENT* ftsdirEntry;
    mozilla::UniquePtr<NS_tchar[]> searchpath(get_full_path(dirpath));

    // Remove the trailing slash so the paths don't contain double slashes. The
    // existence of the slash has already been checked in DoUpdate.
    searchpath[NS_tstrlen(searchpath.get()) - 1] = NS_T('\0');
    char* const pathargv[] = {searchpath.get(), nullptr};

    // FTS_NOCHDIR is used so relative paths from the destination directory are
    // returned.
    if (!(ftsdir = fts_open(pathargv,
                            FTS_PHYSICAL | FTS_NOSTAT | FTS_XDEV | FTS_NOCHDIR,
                            nullptr))) {
      return UNEXPECTED_FILE_OPERATION_ERROR;
    }

    while ((ftsdirEntry = fts_read(ftsdir)) != nullptr) {
      NS_tchar foundpath[MAXPATHLEN];
      NS_tchar* quotedpath = nullptr;
      mozilla::UniquePtr<Action> action;

      switch (ftsdirEntry->fts_info) {
        // Filesystem objects that shouldn't be in the application's directories
        case FTS_SL:
        case FTS_SLNONE:
        case FTS_DEFAULT:
          LOG(("add_dir_entries: found a non-standard file: " LOG_S,
               ftsdirEntry->fts_path));
          // Fall through and try to remove as a file
          [[fallthrough]];

        // Files
        case FTS_F:
        case FTS_NSOK:
          // Add the file to be removed to the ActionList.
          NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                       NS_T("%s"), ftsdirEntry->fts_accpath);
          quotedpath = get_quoted_path(get_relative_path(foundpath));
          if (!quotedpath) {
            rv = UPDATER_QUOTED_PATH_MEM_ERROR;
            break;
          }
          action.reset(new RemoveFile());
          rv = action->Parse(quotedpath);
          free(quotedpath);
          if (!rv) {
            list->Append(action.release());
          }
          break;

        // Directories
        case FTS_DP:
          // Add the directory to be removed to the ActionList.
          NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                       NS_T("%s/"), ftsdirEntry->fts_accpath);
          quotedpath = get_quoted_path(get_relative_path(foundpath));
          if (!quotedpath) {
            rv = UPDATER_QUOTED_PATH_MEM_ERROR;
            break;
          }

          action.reset(new RemoveDir());
          rv = action->Parse(quotedpath);
          free(quotedpath);
          if (!rv) {
            list->Append(action.release());
          }
          break;

        // Errors
        case FTS_DNR:
        case FTS_NS:
          // ENOENT is an acceptable error for FTS_DNR and FTS_NS and means that
          // we're racing with ourselves. Though strange, the entry will be
          // removed anyway.
          if (ENOENT == ftsdirEntry->fts_errno) {
            rv = OK;
            break;
          }
          [[fallthrough]];

        case FTS_ERR:
          rv = UNEXPECTED_FILE_OPERATION_ERROR;
          LOG(("add_dir_entries: fts_read() error: " LOG_S ", err: %d",
               ftsdirEntry->fts_path, ftsdirEntry->fts_errno));
          break;

        case FTS_DC:
          rv = UNEXPECTED_FILE_OPERATION_ERROR;
          LOG(("add_dir_entries: fts_read() returned FT_DC: " LOG_S,
               ftsdirEntry->fts_path));
          break;

        default:
          // FTS_D is ignored and FTS_DP is used instead (post-order).
          rv = OK;
          break;
      }

      if (rv != OK) {
        break;
      }
    }

    fts_close(ftsdir);

    return rv;
  }

#else

int add_dir_entries(const NS_tchar* dirpath, ActionList* list) {
  int rv = OK;
  NS_tchar foundpath[PATH_MAX];
  struct {
    dirent dent_buffer;
    char chars[NAME_MAX];
  } ent_buf;
  struct dirent* ent;
  mozilla::UniquePtr<NS_tchar[]> searchpath(get_full_path(dirpath));

  DIR* dir = opendir(searchpath.get());
  if (!dir) {
    LOG(("add_dir_entries error on opendir: " LOG_S ", err: %d",
         searchpath.get(), errno));
    return UNEXPECTED_FILE_OPERATION_ERROR;
  }

  while (readdir_r(dir, (dirent*)&ent_buf, &ent) == 0 && ent) {
    if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
      continue;
    }

    NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                 NS_T("%s%s"), searchpath.get(), ent->d_name);
    struct stat64 st_buf;
    int test = stat64(foundpath, &st_buf);
    if (test) {
      closedir(dir);
      return UNEXPECTED_FILE_OPERATION_ERROR;
    }
    if (S_ISDIR(st_buf.st_mode)) {
      NS_tsnprintf(foundpath, sizeof(foundpath) / sizeof(foundpath[0]),
                   NS_T("%s%s/"), dirpath, ent->d_name);
      // Recurse into the directory.
      rv = add_dir_entries(foundpath, list);
      if (rv) {
        LOG(("add_dir_entries error: " LOG_S ", err: %d", foundpath, rv));
        closedir(dir);
        return rv;
      }
    } else {
      // Add the file to be removed to the ActionList.
      NS_tchar* quotedpath = get_quoted_path(get_relative_path(foundpath));
      if (!quotedpath) {
        closedir(dir);
        return PARSE_ERROR;
      }

      mozilla::UniquePtr<Action> action(new RemoveFile());
      rv = action->Parse(quotedpath);
      if (rv) {
        LOG(("add_dir_entries Parse error on recurse: " LOG_S ", err: %d",
             quotedpath, rv));
        free(quotedpath);
        closedir(dir);
        return rv;
      }
      free(quotedpath);

      list->Append(action.release());
    }
  }
  closedir(dir);

  // Add the directory to be removed to the ActionList.
  NS_tchar* quotedpath = get_quoted_path(get_relative_path(dirpath));
  if (!quotedpath) {
    return PARSE_ERROR;
  }

  mozilla::UniquePtr<Action> action(new RemoveDir());
  rv = action->Parse(quotedpath);
  if (rv) {
    LOG(("add_dir_entries Parse error on close: " LOG_S ", err: %d", quotedpath,
         rv));
  } else {
    list->Append(action.release());
  }
  free(quotedpath);

  return rv;
}

#endif

/*
 * Gets the contents of an update manifest file. The return value is malloc'd
 * and it is the responsibility of the caller to free it.
 *
 * @param  manifest
 *         The full path to the manifest file.
 * @return On success the contents of the manifest and nullptr otherwise.
 */
static NS_tchar* GetManifestContents(const NS_tchar* manifest) {
  AutoFile mfile(NS_tfopen(manifest, NS_T("rb")));
  if (mfile == nullptr) {
    LOG(("GetManifestContents: error opening manifest file: " LOG_S, manifest));
    return nullptr;
  }

  struct stat ms;
  int rv = fstat(fileno((FILE*)mfile), &ms);
  if (rv) {
    LOG(("GetManifestContents: error stating manifest file: " LOG_S, manifest));
    return nullptr;
  }

  char* mbuf = (char*)malloc(ms.st_size + 1);
  if (!mbuf) {
    return nullptr;
  }

  size_t r = ms.st_size;
  char* rb = mbuf;
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, mfile);
    if (c != count) {
      LOG(("GetManifestContents: error reading manifest file: " LOG_S,
           manifest));
      free(mbuf);
      return nullptr;
    }

    r -= c;
    rb += c;
  }
  *rb = '\0';

#ifndef XP_WIN
  return mbuf;
#else
    NS_tchar* wrb = (NS_tchar*)malloc((ms.st_size + 1) * sizeof(NS_tchar));
    if (!wrb) {
      free(mbuf);
      return nullptr;
    }

    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mbuf, -1, wrb,
                             ms.st_size + 1)) {
      LOG(("GetManifestContents: error converting utf8 to utf16le: %lu",
           GetLastError()));
      free(mbuf);
      free(wrb);
      return nullptr;
    }
    free(mbuf);

    return wrb;
#endif
}

int AddPreCompleteActions(ActionList* list) {
#ifdef XP_MACOSX
  mozilla::UniquePtr<NS_tchar[]> manifestPath(
      get_full_path(NS_T("Contents/Resources/precomplete")));
#else
    mozilla::UniquePtr<NS_tchar[]> manifestPath(
        get_full_path(NS_T("precomplete")));
#endif

  NS_tchar* buf = GetManifestContents(manifestPath.get());
  if (!buf) {
    LOG(
        ("AddPreCompleteActions: error getting contents of precomplete "
         "manifest"));
    // Applications aren't required to have a precomplete manifest. The mar
    // generation scripts enforce the presence of a precomplete manifest.
    return OK;
  }
  NS_tchar* rb = buf;

  int rv;
  NS_tchar* line;
  while ((line = mstrtok(kNL, &rb)) != nullptr) {
    // skip comments
    if (*line == NS_T('#')) {
      continue;
    }

    NS_tchar* token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("AddPreCompleteActions: token not found in manifest"));
      free(buf);
      return PARSE_ERROR;
    }

    Action* action = nullptr;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) {  // rm file
      action = new RemoveFile();
    } else if (NS_tstrcmp(token, NS_T("remove-cc")) ==
               0) {  // no longer supported
      continue;
    } else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) {  // rmdir if  empty
      action = new RemoveDir();
    } else {
      LOG(("AddPreCompleteActions: unknown token: " LOG_S, token));
      free(buf);
      return PARSE_ERROR;
    }

    if (!action) {
      free(buf);
      return BAD_ACTION_ERROR;
    }

    rv = action->Parse(line);
    if (rv) {
      delete action;
      free(buf);
      return rv;
    }

    list->Append(action);
  }

  free(buf);
  return OK;
}

int DoUpdate() {
  NS_tchar manifest[MAXPATHLEN];
  NS_tsnprintf(manifest, sizeof(manifest) / sizeof(manifest[0]),
               NS_T("%s/updating/update.manifest"), gWorkingDirPath);
  ensure_parent_dir(manifest);

  // extract the manifest
  int rv = gArchiveReader.ExtractFile("updatev3.manifest", manifest);
  if (rv) {
    LOG(("DoUpdate: error extracting manifest file"));
    return rv;
  }

  NS_tchar* buf = GetManifestContents(manifest);
  // The manifest is located in the <working_dir>/updating directory which is
  // removed after the update has finished so don't delete it here.
  if (!buf) {
    LOG(("DoUpdate: error opening manifest file: " LOG_S, manifest));
    return READ_ERROR;
  }
  NS_tchar* rb = buf;

#if defined(MOZ_ZUCCHINI)
#  if defined(TEST_UPDATER) && defined(XP_WIN)
  // Crash recovery is only supported (and hence tested) on Windows for now.
  // POSIX support is planned, see bug 2043122 for more information.
  zucchini::mozilla::TestOptions options;
  options.logDestructorMarker = EnvHasValue("MOZ_TEST_ZUCCHINI_DTOR_MARKER");
  options.triggerBadAlloc = EnvHasValue("MOZ_TEST_ZUCCHINI_BAD_ALLOC");
  options.triggerCheckFailure = EnvHasValue("MOZ_TEST_ZUCCHINI_CHECK_FAILURE");
  zucchini::mozilla::SetTestOptions(options);
#  endif  // TEST_UPDATER && XP_WIN

  zucchini::mozilla::SetLogFunction(LogZucchiniMessage);
#endif  // defined(MOZ_ZUCCHINI)

  ActionList list;
  NS_tchar* line;
  bool isFirstAction = true;
  while ((line = mstrtok(kNL, &rb)) != nullptr) {
    // skip comments
    if (*line == NS_T('#')) {
      continue;
    }

    NS_tchar* token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("DoUpdate: token not found in manifest"));
      free(buf);
      return PARSE_ERROR;
    }

    if (isFirstAction) {
      isFirstAction = false;
      // The update manifest isn't required to have a type declaration. The mar
      // generation scripts enforce the presence of the type declaration.
      if (NS_tstrcmp(token, NS_T("type")) == 0) {
        const NS_tchar* type = mstrtok(kQuote, &line);
        LOG(("UPDATE TYPE " LOG_S, type));
        if (NS_tstrcmp(type, NS_T("complete")) == 0) {
          rv = AddPreCompleteActions(&list);
          if (rv) {
            free(buf);
            return rv;
          }
        }
        continue;
      }
    }

    Action* action = nullptr;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) {  // rm file
      action = new RemoveFile();
    } else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) {  // rmdir if empty
      action = new RemoveDir();
    } else if (NS_tstrcmp(token, NS_T("rmrfdir")) == 0) {  // rmdir recursive
      const NS_tchar* reldirpath = mstrtok(kQuote, &line);
      if (!reldirpath) {
        free(buf);
        return PARSE_ERROR;
      }

      if (reldirpath[NS_tstrlen(reldirpath) - 1] != NS_T('/')) {
        free(buf);
        return PARSE_ERROR;
      }

      rv = add_dir_entries(reldirpath, &list);
      if (rv) {
        free(buf);
        return rv;
      }

      continue;
    } else if (NS_tstrcmp(token, NS_T("add")) == 0) {
      action = new AddFile();
    } else if (NS_tstrcmp(token, NS_T("patch")) == 0) {
      action = new PatchFile();
    } else if (NS_tstrcmp(token, NS_T("add-if")) == 0) {  // Add if exists
      action = new AddIfFile();
    } else if (NS_tstrcmp(token, NS_T("add-if-not")) ==
               0) {  // Add if not exists
      action = new AddIfNotFile();
    } else if (NS_tstrcmp(token, NS_T("patch-if")) == 0) {  // Patch if exists
      action = new PatchIfFile();
    } else {
      LOG(("DoUpdate: unknown token: " LOG_S, token));
      free(buf);
      return PARSE_ERROR;
    }

    if (!action) {
      free(buf);
      return BAD_ACTION_ERROR;
    }

    rv = action->Parse(line);
    if (rv) {
      delete action;
      free(buf);
      return rv;
    }

    list.Append(action);
  }

  rv = list.Prepare();
  if (rv) {
    free(buf);
    return rv;
  }

  rv = list.Draft();
  if (rv == OK) {
    rv = list.Execute();
  }

  list.Finish(rv);
  free(buf);
  return rv;
}
