/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "updatedefines.h"
#if defined(XP_WIN)
#  include <windows.h>
#  include <winioctl.h>  // for FSCTL_GET_REPARSE_POINT
#  include <shlobj.h>
#  include <pathcch.h>
#  ifndef RRF_SUBKEY_WOW6464KEY
#    define RRF_SUBKEY_WOW6464KEY 0x00010000
#  endif
#endif

#if defined(XP_MACOSX)
#  include <os/log.h>
#endif

#include <stdio.h>
#include <stdarg.h>

#include "updatecommon.h"
#ifdef XP_WIN
#  include "updatehelper.h"
#  include "nsWindowsHelpers.h"
#  include "mozilla/UniquePtr.h"
#  include "mozilla/WinHeaderOnlyUtils.h"

// This struct isn't in any SDK header, so this definition was copied from:
// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntifs/ns-ntifs-_reparse_data_buffer
typedef struct _REPARSE_DATA_BUFFER {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

void UpdateLog::Init(NS_tchar* logFilePath) {
  if (logFP) {
    return;
  }

  // When the path is over the length limit disable logging by not opening the
  // file and not setting logFP.
  int dstFilePathLen = NS_tstrlen(logFilePath);
  if (dstFilePathLen > 0 && dstFilePathLen < MAXPATHLEN - 1) {
    NS_tstrncpy(mDstFilePath, logFilePath, MAXPATHLEN);
#if defined(XP_WIN) || defined(XP_MACOSX)
    logFP = CreateAndOpenFile(mDstFilePath, false);
    if (logFP == nullptr) {
      LogToOS(NS_T("Failed to create FILE*"));
    }
#else
    // On platforms that have an updates directory in the installation directory
    // (e.g. platforms other than Windows and Mac) the update log is written to
    // a temporary file and then to the update log file. This is needed since
    // the installation directory is moved during a replace request. This can be
    // removed when the platform's updates directory is located outside of the
    // installation directory.
    logFP = tmpfile();
#endif
  }
}

void UpdateLog::Finish() {
  if (!logFP) {
    return;
  }

#if !defined(XP_WIN) && !defined(XP_MACOSX)
  const int blockSize = 1024;
  char buffer[blockSize];
  fflush(logFP);
  rewind(logFP);

  FILE* updateLogFP = CreateAndOpenFile(mDstFilePath, true);
  if (updateLogFP == nullptr) {
    return;
  }

  while (!feof(logFP)) {
    size_t read = fread(buffer, 1, blockSize, logFP);
    if (ferror(logFP)) {
      fclose(logFP);
      logFP = nullptr;
      fclose(updateLogFP);
      updateLogFP = nullptr;
      return;
    }

    size_t written = 0;

    while (written < read) {
      size_t chunkWritten = fwrite(buffer, 1, read - written, updateLogFP);
      if (chunkWritten <= 0) {
        fclose(logFP);
        logFP = nullptr;
        fclose(updateLogFP);
        updateLogFP = nullptr;
        return;
      }

      written += chunkWritten;
    }
  }
  fclose(updateLogFP);
  updateLogFP = nullptr;
#endif

  fclose(logFP);
  logFP = nullptr;
}

void UpdateLog::Flush() {
  if (!logFP) {
    return;
  }

  fflush(logFP);
}

void UpdateLog::PrintTimestampPrefix() {
  if (!logFP) {
    return;
  }

  time_t rawtime = time(nullptr);
  struct tm* timeinfo = localtime(&rawtime);

  if (nullptr != timeinfo) {
    // attempt to format the time similar to rfc-3339 so that it works with
    // sort(1). xxxx-xx-xx xx:xx:xx+xxxx -> 24 chars + 1 NUL
    const size_t buffer_size = 25;
    char buffer[buffer_size] = {0};

    if (0 == strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S%z", timeinfo)) {
      buffer[0] = '\0';  // reset buffer into a defined state and try posix ts
      if (0 > snprintf(buffer, buffer_size, "%d", (int)mktime(timeinfo))) {
        buffer[0] = '\0';  // reset and give up
      }
    }

    fprintf(logFP, "%s: ", buffer);
  }
}

void UpdateLog::Printf(const char* fmt, ...) {
  if (!logFP) {
    return;
  }

  PrintTimestampPrefix();

  va_list ap;
  va_start(ap, fmt);
  vfprintf(logFP, fmt, ap);
  va_end(ap);

  fprintf(logFP, "\n");
  // When the updater crashes on Windows the log file won't be flushed and this
  // can make it easier to debug what is going on.
  fflush(logFP);
}

void UpdateLog::WarnPrintf(const char* fmt, ...) {
  if (!logFP) {
    return;
  }

  PrintTimestampPrefix();

  va_list ap;
  va_start(ap, fmt);
  fprintf(logFP, "*** Warning: ");
  vfprintf(logFP, fmt, ap);
  fprintf(logFP, "***\n");
  va_end(ap);
  // When the updater crashes on Windows the log file won't be flushed and this
  // can make it easier to debug what is going on.
  fflush(logFP);
}

/**
 * Creates and opens a file for logging in read/write mode.
 *
 * @param filePath  The path of the log file.
 * @param binary  If the file should be opened as binary (ignored on
 * macOS/Linux).
 * @return a pointer to the FILE struct or nullptr on error.
 */
FILE* CreateAndOpenFile(NS_tchar* filePath, [[maybe_unused]] bool binary) {
#ifdef XP_WIN
  return NS_tfopen(filePath, binary ? NS_T("wb+") : NS_T("w+"));
#else
  LogToOS(NS_T("Opening logfile"));
  NS_tchar* lastSeperator = NS_tstrrchr(filePath, '/');
  if (lastSeperator == nullptr) {
    // No separator, disable logging.
    return nullptr;
  }

  const NS_tchar templateSuffix[] = NS_T("/temp.XXXXXX");
  size_t templateLen = NS_tstrlen(templateSuffix);

  long dirLength = lastSeperator - filePath;
  // +1 to account for the \0 terminator
  if (dirLength < 0 ||
      (unsigned long)dirLength >= MAXPATHLEN - (templateLen + 1)) {
    // Too short, or too long. Disable logging.
    return nullptr;
  }

  NS_tchar tmpFilePath[MAXPATHLEN] = {L'\0'};
  // Use memcpy here to only copy the directory portion of filePath
  // and using strncpy triggered GCC's stringop-truncation error because
  // filePath's \0 terminator wasn't being carried over. This isn't a problem
  // here because the next strncpy adds it.
  memcpy(tmpFilePath, filePath, dirLength);
  NS_tstrncpy(tmpFilePath + dirLength, templateSuffix, MAXPATHLEN - dirLength);

  int fd = mkstemp(tmpFilePath);
  if (fd == -1) {
    LogToOS(NS_T("Failed to open tmp"));
    return nullptr;
  }

  if (rename(tmpFilePath, filePath) == -1) {
    LogToOS(NS_T("Failed to rename"));
    close(fd);
    return nullptr;
  }

  LogToOS(NS_T("Opening file*"));
  return fdopen(fd, "w+");
#endif
}

#ifdef XP_WIN
/**
 * Determine if a path contains symlinks or junctions to disallowed locations
 *
 * @param fullPath  The full path to check.
 * @return true if the path contains invalid links or on errors,
 *         false if the check passes and the path can be used
 */
bool PathContainsInvalidLinks(wchar_t* const fullPath) {
  wchar_t pathCopy[MAXPATHLEN + 1] = L"";
  wcsncpy(pathCopy, fullPath, MAXPATHLEN);
  wchar_t* remainingPath = nullptr;
  wchar_t* nextToken = wcstok_s(pathCopy, L"\\", &remainingPath);
  wchar_t* partialPath = nextToken;

  while (nextToken) {
    if ((GetFileAttributesW(partialPath) & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      nsAutoHandle h(CreateFileW(
          partialPath, 0,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
          // The path can't be an invalid link if it doesn't exist.
          return false;
        } else {
          return true;
        }
      }

      auto byteBuffer =
          std::make_unique<UINT8[]>(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
      ZeroMemory(byteBuffer.get(), MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
      REPARSE_DATA_BUFFER* buffer = (REPARSE_DATA_BUFFER*)byteBuffer.get();
      DWORD bytes = 0;
      if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer,
                           MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &bytes, nullptr)) {
        return true;
      }

      wchar_t* reparseTarget = nullptr;
      switch (buffer->ReparseTag) {
        case IO_REPARSE_TAG_MOUNT_POINT:
          reparseTarget =
              buffer->MountPointReparseBuffer.PathBuffer +
              (buffer->MountPointReparseBuffer.SubstituteNameOffset /
               sizeof(wchar_t));
          if (buffer->MountPointReparseBuffer.SubstituteNameLength <
              ARRAYSIZE(L"\\??\\")) {
            return false;
          }
          break;
        case IO_REPARSE_TAG_SYMLINK:
          reparseTarget =
              buffer->SymbolicLinkReparseBuffer.PathBuffer +
              (buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset /
               sizeof(wchar_t));
          if (buffer->SymbolicLinkReparseBuffer.SubstituteNameLength <
              ARRAYSIZE(L"\\??\\")) {
            return false;
          }
          break;
        default:
          return true;
          break;
      }

      if (!reparseTarget) {
        return false;
      }
      if (wcsncmp(reparseTarget, L"\\??\\", ARRAYSIZE(L"\\??\\") - 1) != 0) {
        return true;
      }
    }

    nextToken = wcstok_s(nullptr, L"\\", &remainingPath);
    PathAppendW(partialPath, nextToken);
  }

  return false;
}

/**
 * Determine if a path is located within Program Files, either native or x86
 *
 * @param fullPath  The full path to check.
 * @return true if fullPath begins with either Program Files directory,
 *         false if it does not or if an error is encountered
 */
bool IsProgramFilesPath(NS_tchar* fullPath) {
  // Make sure we don't try to compare against a short path.
  DWORD longInstallPathChars = GetLongPathNameW(fullPath, nullptr, 0);
  if (longInstallPathChars == 0) {
    return false;
  }
  auto longInstallPath = std::make_unique<wchar_t[]>(longInstallPathChars);
  if (!GetLongPathNameW(fullPath, longInstallPath.get(),
                        longInstallPathChars)) {
    return false;
  }

  // First check for Program Files (x86).
  {
    PWSTR programFiles32PathRaw = nullptr;
    // FOLDERID_ProgramFilesX86 gets native Program Files directory on a 32-bit
    // OS or the (x86) directory on a 64-bit OS regardless of this binary's
    // bitness.
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, nullptr,
                                    &programFiles32PathRaw))) {
      // That call should never fail on any supported OS version.
      return false;
    }
    mozilla::UniquePtr<wchar_t, mozilla::CoTaskMemFreeDeleter>
        programFiles32Path(programFiles32PathRaw);
    // We need this path to have a trailing slash so our prefix test doesn't
    // match on a different folder which happens to have a name beginning with
    // the prefix we're looking for but then also more characters after that.
    size_t length = wcslen(programFiles32Path.get());
    if (length == 0) {
      return false;
    }
    if (programFiles32Path.get()[length - 1] == L'\\') {
      if (wcsnicmp(longInstallPath.get(), programFiles32Path.get(), length) ==
          0) {
        return true;
      }
    } else {
      // Allocate space for a copy of the string along with a terminator and one
      // extra character for the trailing backslash.
      length += 1;
      auto programFiles32PathWithSlash =
          std::make_unique<wchar_t[]>(length + 1);

      NS_tsnprintf(programFiles32PathWithSlash.get(), length + 1, NS_T("%s\\"),
                   programFiles32Path.get());

      if (wcsnicmp(longInstallPath.get(), programFiles32PathWithSlash.get(),
                   length) == 0) {
        return true;
      }
    }
  }

  // If we didn't find (x86), check for the native Program Files.
  {
    // In case we're a 32-bit binary on 64-bit Windows, we now have a problem
    // getting the right "native" Program Files path, which is that there is no
    // FOLDERID_* value that returns that path. So we always read that one out
    // of its canonical registry location instead. If we're on a 32-bit OS, this
    // will be the same path that we just checked. First get the buffer size to
    // allocate for the path.
    DWORD length = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"Software\\Microsoft\\Windows\\CurrentVersion",
                     L"ProgramFilesDir", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, nullptr, &length) != ERROR_SUCCESS) {
      return false;
    }
    // RegGetValue returns the length including the terminator, but it's in
    // bytes, so convert that to characters.
    DWORD lengthChars = (length / sizeof(wchar_t));
    if (lengthChars <= 1) {
      return false;
    }
    auto programFilesNativePath = std::make_unique<wchar_t[]>(lengthChars);

    // Now actually read the value.
    if (RegGetValueW(
            HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion",
            L"ProgramFilesDir", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr,
            programFilesNativePath.get(), &length) != ERROR_SUCCESS) {
      return false;
    }
    size_t nativePathStrLen =
        wcsnlen_s(programFilesNativePath.get(), lengthChars);
    if (nativePathStrLen == 0) {
      return false;
    }

    // As before, append a backslash if there isn't one already.
    if (programFilesNativePath.get()[nativePathStrLen - 1] == L'\\') {
      if (wcsnicmp(longInstallPath.get(), programFilesNativePath.get(),
                   nativePathStrLen) == 0) {
        return true;
      }
    } else {
      // Allocate space for a copy of the string along with a terminator and one
      // extra character for the trailing backslash.
      nativePathStrLen += 1;
      auto programFilesNativePathWithSlash =
          std::make_unique<wchar_t[]>(nativePathStrLen + 1);

      NS_tsnprintf(programFilesNativePathWithSlash.get(), nativePathStrLen + 1,
                   NS_T("%s\\"), programFilesNativePath.get());

      if (wcsnicmp(longInstallPath.get(), programFilesNativePathWithSlash.get(),
                   nativePathStrLen) == 0) {
        return true;
      }
    }
  }

  return false;
}
#endif

/**
 * Performs checks of a full path for validity for this application.
 *
 * @param  origFullPath
 *         The full path to check.
 * @return true if the path is valid for this application and false otherwise.
 */
bool IsValidFullPath(NS_tchar* origFullPath) {
  // Subtract 1 from MAXPATHLEN for null termination.
  if (NS_tstrlen(origFullPath) > MAXPATHLEN - 1) {
    // The path is longer than acceptable for this application.
    return false;
  }

#ifdef XP_WIN
  NS_tchar testPath[MAXPATHLEN] = {NS_T('\0')};
  // GetFullPathNameW will replace / with \ which PathCchCanonicalize requires.
  if (GetFullPathNameW(origFullPath, MAXPATHLEN, testPath, nullptr) == 0) {
    // Unable to get the full name for the path (e.g. invalid path).
    return false;
  }

  NS_tchar canonicalPath[MAXPATHLEN] = {NS_T('\0')};
  if (FAILED(PathCchCanonicalize(canonicalPath, MAXPATHLEN, testPath))) {
    // Path could not be canonicalized (e.g. invalid path).
    return false;
  }

  // Check if the path passed in resolves to a differerent path.
  if (NS_tstricmp(origFullPath, canonicalPath) != 0) {
    // Case insensitive string comparison between the supplied path and the
    // canonical path are not equal. This will prevent directory traversal and
    // the use of / in paths since they are converted to \.
    return false;
  }

  NS_tstrncpy(testPath, origFullPath, MAXPATHLEN);
  if (!PathStripToRootW(testPath)) {
    // It should always be possible to strip a valid path to its root.
    return false;
  }

  if (origFullPath[0] == NS_T('\\')) {
    // Only allow UNC server share paths.
    if (!PathIsUNCServerShareW(testPath)) {
      return false;
    }
  }

  if (PathContainsInvalidLinks(canonicalPath)) {
    return false;
  }
#else
  // Only allow full paths.
  if (origFullPath[0] != NS_T('/')) {
    return false;
  }

  // The path must not traverse directories
  if (NS_tstrstr(origFullPath, NS_T("/../")) != nullptr) {
    return false;
  }

  // The path shall not have a path traversal suffix
  const NS_tchar invalidSuffix[] = NS_T("/..");
  size_t pathLen = NS_tstrlen(origFullPath);
  size_t invalidSuffixLen = NS_tstrlen(invalidSuffix);
  if (invalidSuffixLen <= pathLen &&
      NS_tstrncmp(origFullPath + pathLen - invalidSuffixLen, invalidSuffix,
                  invalidSuffixLen) == 0) {
    return false;
  }
#endif
  return true;
}

#if defined(XP_MACOSX)
// This is never deallocated by the system
MOZ_RUNINIT static os_log_t updaterLogger =
    os_log_create("org.mozilla.updater", "Updater");
#endif

/**
 * Logs a message to the system log for debugging purposes before our log
 * file has been set up.
 *
 * @param  message
 *         The message to log.
 */
void LogToOS(const NS_tchar* message) {
#if defined(XP_MACOSX)
  os_log(updaterLogger, "%{public}s", message);
#endif
}

bool EnvHasValue(const char* name) {
  const char* val = getenv(name);
  return (val && *val);
}
