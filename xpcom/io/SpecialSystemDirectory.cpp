/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpecialSystemDirectory.h"

#include "mozilla/Try.h"
#include "nsComponentManagerUtils.h"
#include "nsDependentString.h"
#include "nsIXULAppInfo.h"
#include "nsString.h"

#if defined(XP_WIN)

#  include <direct.h>
#  include <guiddef.h>
#  include <knownfolders.h>
#  include <shlobj.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <windows.h>

#  include "mozilla/WinHeaderOnlyUtils.h"
#  include "nsIWindowsRegKey.h"

#elif defined(XP_UNIX)

#  include <limits.h>
#  include <stdlib.h>
#  include <sys/param.h>
#  include <unistd.h>

#  include "prenv.h"
#  include "xpcpublic.h"
#  if defined(XP_DARWIN)
#    include "DarwinFileUtils.h"
#    if defined(MOZ_WIDGET_COCOA)
#      include "CFTypeRefPtr.h"
#    elif defined(MOZ_WIDGET_UIKIT)
#      include "mozilla/UIKitDirProvider.h"
#    endif
#  endif
#  if defined(MOZ_WIDGET_GTK)
#    include "mozilla/WidgetUtilsGtk.h"
#  endif

#endif

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  elif defined(MAX_PATH)
#    define MAXPATHLEN MAX_PATH
#  elif defined(_MAX_PATH)
#    define MAXPATHLEN _MAX_PATH
#  elif defined(CCHMAXPATH)
#    define MAXPATHLEN CCHMAXPATH
#  else
#    define MAXPATHLEN 1024
#  endif
#endif

#if defined(XP_WIN)
// OneDrive For Business folders are named "Business1", "Business2", ...
// "Business10".
static const uint32_t kOneDriveBusinessFolderStartIdx = 1;
static const uint32_t kOneDriveBusinessFolderEndIdx = 10;

static nsresult GetKnownFolder(REFKNOWNFOLDERID aFolderId, nsIFile** aFile) {
  mozilla::UniquePtr<WCHAR, mozilla::CoTaskMemFreeDeleter> path;
  SHGetKnownFolderPath(aFolderId, 0, nullptr, getter_Transfers(path));

  if (!path) {
    return NS_ERROR_FAILURE;
  }

  return NS_NewLocalFile(nsDependentString(path.get()), aFile);
}

/**
 * Provides a fallback for getting the path to APPDATA or LOCALAPPDATA by
 * querying the registry when the call to SHGetSpecialFolderPathW is unable to
 * provide these paths (Bug 513958).
 */
static nsresult GetRegWindowsAppDataFolder(bool aLocal, nsIFile** aFile) {
  HKEY key;
  LPCWSTR keyName =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders";
  DWORD res = ::RegOpenKeyExW(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &key);
  if (res != ERROR_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  WCHAR path[MAX_PATH + 2];
  DWORD type, size;
  res = RegQueryValueExW(key, (aLocal ? L"Local AppData" : L"AppData"), nullptr,
                         &type, (LPBYTE)&path, &size);
  ::RegCloseKey(key);
  // The call to RegQueryValueExW must succeed, the type must be REG_SZ, the
  // buffer size must not equal 0, and the buffer size be a multiple of 2.
  if (res != ERROR_SUCCESS || type != REG_SZ || size == 0 || size % 2 != 0) {
    return NS_ERROR_FAILURE;
  }

  // Append the trailing slash
  int len = wcslen(path);
  if (len > 1 && path[len - 1] != L'\\') {
    path[len] = L'\\';
    path[++len] = L'\0';
  }

  return NS_NewLocalFile(nsDependentString(path, len), aFile);
}

static const auto kOneDrivePersonalSubkey{u"Personal"_ns};

static nsresult GetOneDriveSyncRoot(const nsAString& aSubkey, nsIFile** aFolder,
                                    nsIWindowsRegKey* aRegistrySvc = nullptr) {
  nsresult rv = NS_OK;
  nsCOMPtr<nsIWindowsRegKey> registrySvc = aRegistrySvc;
  if (!registrySvc) {
    registrySvc = do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  const nsAutoString path =
      u"Software\\Microsoft\\OneDrive\\Accounts\\"_ns + aSubkey;
  rv = registrySvc->Open(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER, path,
                         nsIWindowsRegKey::ACCESS_READ);
  NS_ENSURE_SUCCESS(rv, rv);
  if (aSubkey.Equals(kOneDrivePersonalSubkey)) {
    auto isUserLoggedIn{false};
    rv = registrySvc->HasValue(u"cid"_ns, &isUserLoggedIn);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!isUserLoggedIn) {
      return NS_ERROR_FILE_NOT_FOUND;
    }
  }
  bool hasUserFolder = false;
  rv = registrySvc->HasValue(u"UserFolder"_ns, &hasUserFolder);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasUserFolder) {
    return NS_ERROR_FILE_NOT_FOUND;
  }
  nsAutoString folderPath;
  rv = registrySvc->ReadStringValue(u"UserFolder"_ns, folderPath);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_NewLocalFile(folderPath, aFolder);
}
#endif  // XP_WIN

#if defined(XP_UNIX)
static nsresult GetUnixHomeDir(nsIFile** aFile) {
#  if defined(ANDROID)
  // XXX no home dir on android; maybe we should return the sdcard if present?
  return NS_ERROR_FAILURE;
#  else
  return NS_NewNativeLocalFile(nsDependentCString(PR_GetEnv("HOME")), aFile);
#  endif
}

static nsresult GetUnixSystemConfigDir(nsIFile** aFile) {
#  if defined(ANDROID)
  return NS_ERROR_FAILURE;
#  else
  nsAutoCString appName;
  if (nsCOMPtr<nsIXULAppInfo> appInfo =
          do_GetService("@mozilla.org/xre/app-info;1")) {
    MOZ_TRY(appInfo->GetName(appName));
  } else {
    appName.AssignLiteral(MOZ_APP_BASENAME);
  }

  ToLowerCase(appName);

  nsDependentCString sysConfigDir;
  if (xpc::IsInAutomation()) {
    const char* mozSystemConfigDir = PR_GetEnv("MOZ_SYSTEM_CONFIG_DIR");
    if (mozSystemConfigDir) {
      sysConfigDir.Assign(nsDependentCString(mozSystemConfigDir));
    }
  }
#    if defined(MOZ_WIDGET_GTK)
  if (sysConfigDir.IsEmpty() && mozilla::widget::IsRunningUnderFlatpak()) {
    sysConfigDir.Assign(nsLiteralCString("/app/etc"));
  }
#    endif
  if (sysConfigDir.IsEmpty()) {
    sysConfigDir.Assign(nsLiteralCString("/etc"));
  }
  MOZ_TRY(NS_NewNativeLocalFile(sysConfigDir, aFile));
  MOZ_TRY((*aFile)->AppendNative(appName));
  return NS_OK;
#  endif
}

/*
  The following license applies to the xdg_user_dir_lookup function:

  Copyright (c) 2007 Red Hat, Inc.

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

static char* xdg_user_dir_lookup(const char* aType) {
  FILE* file;
  char* home_dir;
  char* config_home;
  char* config_file;
  char buffer[512];
  char* user_dir;
  char* p;
  char* d;
  int len;
  int relative;

  home_dir = getenv("HOME");

  if (!home_dir) {
    goto error;
  }

  config_home = getenv("XDG_CONFIG_HOME");
  if (!config_home || config_home[0] == 0) {
    config_file =
        (char*)malloc(strlen(home_dir) + strlen("/.config/user-dirs.dirs") + 1);
    if (!config_file) {
      goto error;
    }

    strcpy(config_file, home_dir);
    strcat(config_file, "/.config/user-dirs.dirs");
  } else {
    config_file =
        (char*)malloc(strlen(config_home) + strlen("/user-dirs.dirs") + 1);
    if (!config_file) {
      goto error;
    }

    strcpy(config_file, config_home);
    strcat(config_file, "/user-dirs.dirs");
  }

  file = fopen(config_file, "r");
  free(config_file);
  if (!file) {
    goto error;
  }

  user_dir = nullptr;
  while (fgets(buffer, sizeof(buffer), file)) {
    /* Remove newline at end */
    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = 0;
    }

    p = buffer;
    while (*p == ' ' || *p == '\t') {
      p++;
    }

    if (strncmp(p, "XDG_", 4) != 0) {
      continue;
    }
    p += 4;
    if (strncmp(p, aType, strlen(aType)) != 0) {
      continue;
    }
    p += strlen(aType);
    if (strncmp(p, "_DIR", 4) != 0) {
      continue;
    }
    p += 4;

    while (*p == ' ' || *p == '\t') {
      p++;
    }

    if (*p != '=') {
      continue;
    }
    p++;

    while (*p == ' ' || *p == '\t') {
      p++;
    }

    if (*p != '"') {
      continue;
    }
    p++;

    relative = 0;
    if (strncmp(p, "$HOME/", 6) == 0) {
      p += 6;
      relative = 1;
    } else if (*p != '/') {
      continue;
    }

    if (relative) {
      free(user_dir);
      user_dir = (char*)malloc(strlen(home_dir) + 1 + strlen(p) + 1);
      if (!user_dir) {
        goto error2;
      }

      strcpy(user_dir, home_dir);
      strcat(user_dir, "/");
    } else {
      free(user_dir);
      user_dir = (char*)malloc(strlen(p) + 1);
      if (!user_dir) {
        goto error2;
      }

      *user_dir = 0;
    }

    d = user_dir + strlen(user_dir);
    while (*p && *p != '"') {
      if ((*p == '\\') && (*(p + 1) != 0)) {
        p++;
      }
      *d++ = *p++;
    }
    *d = 0;
  }
error2:
  fclose(file);

  if (user_dir) {
    return user_dir;
  }

error:
  return nullptr;
}

static const char xdg_user_dirs[] =
    "DESKTOP\0"
    "DOCUMENTS\0"
    "DOWNLOAD\0"
    "MUSIC\0"
    "PICTURES\0"
    "PUBLICSHARE\0"
    "TEMPLATES\0"
    "VIDEOS";

static const uint8_t xdg_user_dir_offsets[] = {0, 8, 18, 27, 33, 42, 54, 64};

static nsresult GetUnixXDGUserDirectory(SystemDirectories aSystemDirectory,
                                        nsIFile** aFile) {
  char* dir = xdg_user_dir_lookup(
      xdg_user_dirs +
      xdg_user_dir_offsets[aSystemDirectory - Unix_XDG_Desktop]);

  nsresult rv;
  nsCOMPtr<nsIFile> file;
  bool exists;
  if (dir) {
    rv = NS_NewNativeLocalFile(nsDependentCString(dir), getter_AddRefs(file));
    free(dir);

    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = file->Exists(&exists);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (!exists) {
      rv = file->Create(nsIFile::DIRECTORY_TYPE, 0755);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  } else if (Unix_XDG_Desktop == aSystemDirectory) {
    // for the XDG desktop dir, fall back to HOME/Desktop
    // (for historical compatibility)
    nsCOMPtr<nsIFile> home;
    rv = GetUnixHomeDir(getter_AddRefs(home));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = home->Clone(getter_AddRefs(file));
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = file->AppendNative("Desktop"_ns);
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = file->Exists(&exists);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // fallback to HOME only if HOME/Desktop doesn't exist
    if (!exists) {
      file = std::move(home);
    }
  } else {
    // no fallback for the other XDG dirs
    return NS_ERROR_FAILURE;
  }

  *aFile = nullptr;
  file.swap(*aFile);

  return NS_OK;
}
#endif

nsresult GetSpecialSystemDirectory(SystemDirectories aSystemSystemDirectory,
                                   nsIFile** aFile) {
#if defined(XP_WIN)
  WCHAR path[MAX_PATH];
#else
  char path[MAXPATHLEN];
#endif

  switch (aSystemSystemDirectory) {
    case OS_CurrentWorkingDirectory:
#if defined(XP_WIN)
      if (!_wgetcwd(path, MAX_PATH)) {
        return NS_ERROR_FAILURE;
      }
      return NS_NewLocalFile(nsDependentString(path), aFile);
#else
      if (!getcwd(path, MAXPATHLEN)) {
        return NS_ERROR_FAILURE;
      }
#endif

#if !defined(XP_WIN)
      return NS_NewNativeLocalFile(nsDependentCString(path), aFile);
#endif

    case OS_TemporaryDirectory:
#if defined(XP_WIN)
    {
      DWORD len = ::GetTempPathW(MAX_PATH, path);
      if (len == 0) {
        break;
      }
      return NS_NewLocalFile(nsDependentString(path, len), aFile);
    }
#elif defined(XP_DARWIN)
    {
      nsAutoCString tempDir;
      DarwinFileUtils::GetTemporaryDirectory(tempDir);
      return NS_NewNativeLocalFile(tempDir, aFile);
    }

#elif defined(XP_UNIX)
    {
      static const char* tPath = nullptr;
      if (!tPath) {
        tPath = PR_GetEnv("TMPDIR");
        if (!tPath || !*tPath) {
          tPath = PR_GetEnv("TMP");
          if (!tPath || !*tPath) {
            tPath = PR_GetEnv("TEMP");
            if (!tPath || !*tPath) {
              tPath = "/tmp/";
            }
          }
        }
      }
      return NS_NewNativeLocalFile(nsDependentCString(tPath), aFile);
    }
#else
      break;
#endif
#if defined(MOZ_WIDGET_COCOA)
    case Mac_SystemDirectory: {
      return GetOSXFolderType(kClassicDomain, kSystemFolderType, aFile);
    }
    case Mac_UserLibDirectory: {
      return GetOSXFolderType(kUserDomain, kDomainLibraryFolderType, aFile);
    }
    case Mac_HomeDirectory: {
      return GetOSXFolderType(kUserDomain, kDomainTopLevelFolderType, aFile);
    }
    case Mac_DefaultDownloadDirectory: {
      nsresult rv = GetOSXFolderType(kUserDomain, kDownloadsFolderType, aFile);
      if (NS_FAILED(rv)) {
        return GetOSXFolderType(kUserDomain, kDesktopFolderType, aFile);
      }
      return NS_OK;
    }
    case Mac_UserDesktopDirectory: {
      return GetOSXFolderType(kUserDomain, kDesktopFolderType, aFile);
    }
    case Mac_UserDocumentsDirectory: {
      return GetOSXFolderType(kUserDomain, kDocumentsFolderType, aFile);
    }
    case Mac_LocalApplicationsDirectory: {
      return GetOSXFolderType(kLocalDomain, kApplicationsFolderType, aFile);
    }
    case Mac_UserPreferencesDirectory: {
      return GetOSXFolderType(kUserDomain, kPreferencesFolderType, aFile);
    }
    case Mac_PictureDocumentsDirectory: {
      return GetOSXFolderType(kUserDomain, kPictureDocumentsFolderType, aFile);
    }
    case Mac_DefaultScreenshotDirectory: {
      auto prefValue = CFTypeRefPtr<CFPropertyListRef>::WrapUnderCreateRule(
          CFPreferencesCopyAppValue(CFSTR("location"),
                                    CFSTR("com.apple.screencapture")));

      if (!prefValue || CFGetTypeID(prefValue.get()) != CFStringGetTypeID()) {
        return GetOSXFolderType(kUserDomain, kPictureDocumentsFolderType,
                                aFile);
      }

      nsAutoString path;
      mozilla::Span<char16_t> data =
          path.GetMutableData(CFStringGetLength((CFStringRef)prefValue.get()));
      CFStringGetCharacters((CFStringRef)prefValue.get(),
                            CFRangeMake(0, data.Length()),
                            reinterpret_cast<UniChar*>(data.Elements()));

      return NS_NewLocalFile(path, aFile);
    }
#elif defined(XP_WIN)
    case Win_SystemDirectory: {
      return GetKnownFolder(FOLDERID_System, aFile);
    }

    case Win_WindowsDirectory: {
      return GetKnownFolder(FOLDERID_Windows, aFile);
    }

    case Win_ProgramFiles: {
      return GetKnownFolder(FOLDERID_ProgramFiles, aFile);
    }

    case Win_HomeDirectory: {
      return GetKnownFolder(FOLDERID_Profile, aFile);
    }
    case Win_Programs: {
      return GetKnownFolder(FOLDERID_Programs, aFile);
    }

    case Win_Downloads: {
      return GetKnownFolder(FOLDERID_Downloads, aFile);
    }

    case Win_Favorites: {
      return GetKnownFolder(FOLDERID_Favorites, aFile);
    }
    case Win_Desktopdirectory: {
      return GetKnownFolder(FOLDERID_Desktop, aFile);
    }
    case Win_Cookies: {
      return GetKnownFolder(FOLDERID_Cookies, aFile);
    }
    case Win_Appdata: {
      nsresult rv = GetKnownFolder(FOLDERID_RoamingAppData, aFile);
      if (NS_FAILED(rv)) {
        rv = GetRegWindowsAppDataFolder(false, aFile);
      }
      return rv;
    }
    case Win_LocalAppdata: {
      nsresult rv = GetKnownFolder(FOLDERID_LocalAppData, aFile);
      if (NS_FAILED(rv)) {
        rv = GetRegWindowsAppDataFolder(true, aFile);
      }
      return rv;
    }
    case Win_Documents: {
      return GetKnownFolder(FOLDERID_Documents, aFile);
    }
    case Win_OneDrivePersonal: {
      return GetOneDriveSyncRoot(kOneDrivePersonalSubkey, aFile);
    }
#endif  // XP_WIN

#if defined(XP_UNIX)
    case Unix_HomeDirectory:
      return GetUnixHomeDir(aFile);

    case Unix_XDG_Desktop:
    case Unix_XDG_Documents:
    case Unix_XDG_Download:
      return GetUnixXDGUserDirectory(aSystemSystemDirectory, aFile);

    case Unix_SystemConfigDirectory:
      return GetUnixSystemConfigDir(aFile);
#endif

    default:
      break;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

nsresult GetSpecialSystemDirectoryList(
    SystemDirectoryLists aSystemDirectoryLists,
    nsCOMArray<nsIFile>& aDirectories) {
  switch (aSystemDirectoryLists) {
#ifdef XP_WIN
    case Win_OneDriveBusiness: {
      nsresult rv;
      nsCOMPtr<nsIWindowsRegKey> registrySvc =
          do_GetService("@mozilla.org/windows-registry-key;1", &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      for (uint32_t idx = kOneDriveBusinessFolderStartIdx;
           idx <= kOneDriveBusinessFolderEndIdx; ++idx) {
        nsAutoString businessValue;
        businessValue.AppendPrintf("Business%d", idx);
        nsCOMPtr<nsIFile> folder;
        rv = GetOneDriveSyncRoot(businessValue, getter_AddRefs(folder),
                                 registrySvc);
        // Skip folder on error.  Report error only if not
        // NS_ERROR_FILE_NOT_FOUND, which indicates an unused business folder.
        if (rv == NS_ERROR_FILE_NOT_FOUND || NS_WARN_IF(NS_FAILED(rv))) {
          continue;
        }
        aDirectories.AppendElement(folder);
      }
      return NS_OK;
    }
#endif  // XP_WIN
    default:
      break;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

#if defined(MOZ_WIDGET_COCOA)
nsresult GetOSXFolderType(short aDomain, OSType aFolderType,
                          nsIFile** aLocalFile) {
  nsresult rv = NS_ERROR_FAILURE;

  OSErr err;
  FSRef fsRef;
  err = ::FSFindFolder(aDomain, aFolderType, kCreateFolder, &fsRef);
  if (err == noErr) {
    nsCOMPtr<nsILocalFileMac> localMacFile;
    rv = NS_NewLocalFileWithFSRef(&fsRef, getter_AddRefs(localMacFile));
    localMacFile.forget(aLocalFile);
  }
  return rv;
}
#endif
