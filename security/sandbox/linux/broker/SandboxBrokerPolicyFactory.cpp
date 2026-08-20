/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxBrokerPolicyFactory.h"

#ifdef MOZ_PROFILE_GENERATE
#  include <string>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#ifndef ANDROID
#  include <glob.h>
#endif
#ifdef MOZ_ENABLE_V4L2
#  include <fcntl.h>
#  include <linux/videodev2.h>
#  include <sys/ioctl.h>
#endif  // MOZ_ENABLE_V4L2
#ifdef MOZ_ENABLE_VULKAN_VIDEO
#  include "mozilla/Components.h"
#  include "mozilla/gfx/gfxVars.h"
// gfxVars.h logging collides with sandbox policy one so undef it.
#  undef CHECK
#  undef DCHECK
#endif  // MOZ_ENABLE_VULKAN_VIDEO
#ifdef MOZ_WIDGET_GTK
#  include <glib.h>
#endif

#include "SandboxInfo.h"
#include "SandboxLogging.h"
#include "SpecialSystemDirectory.h"
#include "mozilla/Array.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Omnijar.h"
#include "mozilla/Preferences.h"
#include "mozilla/SandboxLaunch.h"
#include "mozilla/SandboxSettings.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsILineInputStream.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "prenv.h"
#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#endif
#ifdef ANDROID
#  include "cutils/properties.h"
#endif

namespace mozilla {

namespace {
static const int rdonly = SandboxBroker::MAY_READ;
static const int wronly = SandboxBroker::MAY_WRITE;
static const int rdwr = rdonly | wronly;
static const int rdwrcr = rdwr | SandboxBroker::MAY_CREATE;
static const int access = SandboxBroker::MAY_ACCESS;
static const int deny = SandboxBroker::FORCE_DENY;
}  // namespace

using CacheE = std::pair<nsCString, int>;
using FileCacheT = nsTArray<CacheE>;

static void AddDriPaths(SandboxBroker::Policy* aPolicy) {
  // Bug 1401666: Mesa driver loader part 2: Mesa <= 12 using libudev
  // Used by libdrm, which is used by Mesa, and
  // Intel(R) Media Driver for VAAPI.
  if (auto dir = opendir("/dev/dri")) {
    while (auto entry = readdir(dir)) {
      if (entry->d_name[0] != '.') {
        nsPrintfCString devPath("/dev/dri/%s", entry->d_name);
        struct stat sb;
        if (stat(devPath.get(), &sb) == 0 && S_ISCHR(sb.st_mode)) {
          // For both the DRI node and its parent (the physical
          // device), allow reading the "uevent" file.
          static const Array<nsCString, 2> kSuffixes = {""_ns, "/device"_ns};
          nsPrintfCString prefix("/sys/dev/char/%u:%u", major(sb.st_rdev),
                                 minor(sb.st_rdev));
          for (const auto& suffix : kSuffixes) {
            nsCString sysPath(prefix + suffix);

            // libudev will expand the symlink but not do full
            // canonicalization, so it will leave in ".." path
            // components that will be realpath()ed in the
            // broker.  To match this, allow the canonical paths.
            UniqueFreePtr<char[]> realSysPath(realpath(sysPath.get(), nullptr));
            if (realSysPath) {
              // https://gitlab.freedesktop.org/mesa/drm/-/commit/3988580e4c0f4b3647a0c6af138a3825453fe6e0
              // > term = strrchr(real_path, '/');
              // > if (term && strncmp(term, "/virtio", 7) == 0)
              // >     *term = 0;
              char* term = strrchr(realSysPath.get(), '/');
              if (term && strncmp(term, "/virtio", 7) == 0) {
                *term = 0;
              }

              aPolicy->AddFilePrefix(rdonly, realSysPath.get(), "");
              // Allowing stat-ing and readlink-ing the parent dirs
              nsPrintfCString basePath("%s/", realSysPath.get());
              aPolicy->AddAncestors(basePath.get(), rdonly);
            }
          }

          // https://gitlab.freedesktop.org/mesa/drm/-/commit/a02900133b32dd4a7d6da4966f455ab337e80dfc
          // > strncpy(path, device_path, PATH_MAX);
          // > strncat(path, "/subsystem", PATH_MAX);
          // >
          // > if (readlink(path, link, PATH_MAX) < 0)
          // >     return -errno;
          nsCString subsystemPath(prefix + "/device/subsystem"_ns);
          aPolicy->AddPath(rdonly, subsystemPath.get());
          aPolicy->AddAncestors(subsystemPath.get(), rdonly);
        }
      }
    }
    closedir(dir);
  }

  // https://gitlab.freedesktop.org/mesa/mesa/-/commit/04bdbbcab3c4862bf3f54ce60fcc1d2007776f80
  aPolicy->AddPath(rdonly, "/usr/share/drirc.d");

  // https://dri.freedesktop.org/wiki/ConfigurationInfrastructure/
  aPolicy->AddPath(rdonly, "/etc/drirc");

  nsCOMPtr<nsIFile> drirc;
  nsresult rv =
      GetSpecialSystemDirectory(Unix_HomeDirectory, getter_AddRefs(drirc));
  if (NS_SUCCEEDED(rv)) {
    rv = drirc->AppendNative(".drirc"_ns);
    if (NS_SUCCEEDED(rv)) {
      nsAutoCString tmpPath;
      rv = drirc->GetNativePath(tmpPath);
      if (NS_SUCCEEDED(rv)) {
        aPolicy->AddPath(rdonly, tmpPath.get());
      }
    }
  }
}

static void JoinPathIfRelative(const nsACString& aCwd, const nsACString& inPath,
                               nsACString& outPath) {
  if (inPath.Length() < 1) {
    outPath.Assign(aCwd);
    SANDBOX_LOG("Unjoinable path: %s", PromiseFlatCString(aCwd).get());
    return;
  }
  const char* startChar = inPath.BeginReading();
  if (*startChar != '/') {
    // Relative path, copy basepath in front
    outPath.Assign(aCwd);
    outPath.Append("/");
    outPath.Append(inPath);
  } else {
    // Absolute path, it's ok like this
    outPath.Assign(inPath);
  }
}

static void CachePathsFromFile(FileCacheT& aCache, const nsACString& aPath);

static void CachePathsFromFileInternal(FileCacheT& aCache,
                                       const nsACString& aCwd,
                                       const nsACString& aPath) {
  nsresult rv;
  nsCOMPtr<nsIFile> ldconfig;
  rv = NS_NewNativeLocalFile(aPath, getter_AddRefs(ldconfig));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  nsCOMPtr<nsIFileInputStream> fileStream(
      do_CreateInstance(NS_LOCALFILEINPUTSTREAM_CONTRACTID, &rv));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  rv = fileStream->Init(ldconfig, -1, -1, 0);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  nsCOMPtr<nsILineInputStream> lineStream(do_QueryInterface(fileStream, &rv));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsAutoCString line;
  bool more = true;
  do {
    rv = lineStream->ReadLine(line, &more);
    if (NS_FAILED(rv)) {
      break;
    }
    // Cut off any comments at the end of the line, also catches lines
    // that are entirely a comment
    int32_t hash = line.FindChar('#');
    if (hash >= 0) {
      line = Substring(line, 0, hash);
    }
    // Simplify our following parsing by trimming whitespace
    line.CompressWhitespace(true, true);
    if (line.IsEmpty()) {
      // Skip comment lines
      continue;
    }
    // Check for any included files and recursively process
    nsACString::const_iterator start, end, token_end;

    line.BeginReading(start);
    line.EndReading(end);
    token_end = end;

    if (FindInReadable("include "_ns, start, token_end)) {
      nsAutoCString includes(Substring(token_end, end));
      for (const nsACString& includeGlob : includes.Split(' ')) {
        // Glob path might be relative, so add cwd if so.
        nsAutoCString includeFile;
        JoinPathIfRelative(aCwd, includeGlob, includeFile);
        glob_t globbuf;
        if (!glob(PromiseFlatCString(includeFile).get(), GLOB_NOSORT, nullptr,
                  &globbuf)) {
          for (size_t fileIdx = 0; fileIdx < globbuf.gl_pathc; fileIdx++) {
            nsAutoCString filePath(globbuf.gl_pathv[fileIdx]);
            CachePathsFromFile(aCache, filePath);
          }
          globfree(&globbuf);
        }
      }
    }

    // Cut off anything behind an = sign, used by dirname=TYPE directives
    int32_t equals = line.FindChar('=');
    if (equals >= 0) {
      line = Substring(line, 0, equals);
    }
    char* resolvedPath = realpath(line.get(), nullptr);
    if (resolvedPath) {
      aCache.AppendElement(std::make_pair(nsCString(resolvedPath), rdonly));
      free(resolvedPath);
    }
  } while (more);
}

static void CachePathsFromFile(FileCacheT& aCache, const nsACString& aPath) {
  // Find the new base path where that file sits in.
  nsresult rv;
  nsCOMPtr<nsIFile> includeFile;
  rv = NS_NewNativeLocalFile(aPath, getter_AddRefs(includeFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  if (SandboxInfo::Get().Test(SandboxInfo::kVerbose)) {
    SANDBOX_LOG("Adding paths from %s to policy.",
                PromiseFlatCString(aPath).get());
  }

  // Find the parent dir where this file sits in.
  nsCOMPtr<nsIFile> parentDir;
  rv = includeFile->GetParent(getter_AddRefs(parentDir));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  nsAutoCString parentPath;
  rv = parentDir->GetNativePath(parentPath);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  if (SandboxInfo::Get().Test(SandboxInfo::kVerbose)) {
    SANDBOX_LOG("Parent path is %s", PromiseFlatCString(parentPath).get());
  }
  CachePathsFromFileInternal(aCache, parentPath, aPath);
}

static void AddLdconfigPaths(SandboxBroker::Policy* aPolicy) {
  static StaticMutex sMutex;
  StaticMutexAutoLock lock(sMutex);

  static FileCacheT ldConfigCache{};
  static bool ldConfigCachePopulated = false;
  if (!ldConfigCachePopulated) {
    CachePathsFromFile(ldConfigCache, "/etc/ld.so.conf"_ns);
    ldConfigCachePopulated = true;
    RunOnShutdown([&] {
      ldConfigCache.Clear();
      MOZ_ASSERT(ldConfigCache.IsEmpty(), "ldconfig cache should be empty");
    });
  }
  for (const CacheE& e : ldConfigCache) {
    aPolicy->AddTree(e.second, e.first.get());
  }
}

static void AddLdLibraryEnvPaths(SandboxBroker::Policy* aPolicy) {
  nsAutoCString LdLibraryEnv(PR_GetEnv("LD_LIBRARY_PATH"));
  // The items in LD_LIBRARY_PATH can be separated by either colons or
  // semicolons, according to the ld.so(8) man page, and empirically it
  // seems to be allowed to mix them (i.e., a:b;c is a list with 3 elements).
  // There is no support for escaping the delimiters, fortunately (for us).
  LdLibraryEnv.ReplaceChar(';', ':');
  for (const nsACString& libPath : LdLibraryEnv.Split(':')) {
    char* resolvedPath = realpath(PromiseFlatCString(libPath).get(), nullptr);
    if (resolvedPath) {
      aPolicy->AddTree(rdonly, resolvedPath);
      free(resolvedPath);
    }
  }
}

static void AddSharedMemoryPaths(SandboxBroker::Policy* aPolicy, pid_t aPid) {
  std::string shmPath("/dev/shm");
  if (ipc::shared_memory::AppendPosixShmPrefix(&shmPath, aPid)) {
    aPolicy->AddPrefix(rdwrcr, shmPath.c_str());
  }
}

static void AddMemoryReporting(SandboxBroker::Policy* aPolicy, pid_t aPid) {
  // Bug 1198552: memory reporting.
  // Bug 1647957: memory reporting.
  aPolicy->AddPath(rdonly, nsPrintfCString("/proc/%d/statm", aPid).get());
  aPolicy->AddPath(rdonly, nsPrintfCString("/proc/%d/smaps", aPid).get());
}

static void AddDynamicPathList(SandboxBroker::Policy* policy,
                               const char* aPathListPref, int perms) {
  nsAutoCString pathList;
  nsresult rv = Preferences::GetCString(aPathListPref, pathList);
  if (NS_SUCCEEDED(rv)) {
    for (const nsACString& path : pathList.Split(',')) {
      nsCString trimPath(path);
      trimPath.Trim(" ", true, true);
      policy->AddDynamic(perms, trimPath.get());
    }
  }
}

static void AddX11Dependencies(SandboxBroker::Policy* policy) {
  // Allow Primus to contact the Bumblebee daemon to manage GPU
  // switching on NVIDIA Optimus systems.
  const char* bumblebeeSocket = PR_GetEnv("BUMBLEBEE_SOCKET");
  if (bumblebeeSocket == nullptr) {
    bumblebeeSocket = "/var/run/bumblebee.socket";
  }
  policy->AddPath(SandboxBroker::MAY_CONNECT, bumblebeeSocket);

#if defined(MOZ_WIDGET_GTK) && defined(MOZ_X11)
  // Allow local X11 connections, if:
  //
  // * Primus or VirtualGL is used, to contact the secondary X server
  static const bool kIsX11 =
      !mozilla::widget::GdkIsWaylandDisplay() && PR_GetEnv("DISPLAY");
  if (kIsX11) {
    policy->AddPrefix(SandboxBroker::MAY_CONNECT, "/tmp/.X11-unix/X");
    if (auto* const xauth = PR_GetEnv("XAUTHORITY")) {
      policy->AddPath(rdonly, xauth);
    } else if (auto* const home = PR_GetEnv("HOME")) {
      // This follows the logic in libXau: append "/.Xauthority",
      // even if $HOME ends in a slash, except in the special case
      // where HOME=/ because POSIX allows implementations to treat
      // an initial double slash specially.
      nsAutoCString xauth(home);
      if (xauth != "/"_ns) {
        xauth.Append('/');
      }
      xauth.AppendLiteral(".Xauthority");
      policy->AddPath(rdonly, xauth.get());
    }
  }
#endif
}

#if defined(MOZ_WIDGET_GTK)
static void AddWaylandDependencies(SandboxBroker::Policy* policy) {
  static const bool kIsWayland =
      mozilla::widget::GdkIsWaylandDisplay() && PR_GetEnv("WAYLAND_DISPLAY");
  static const bool kIsXWayland = mozilla::widget::IsXWaylandProtocol();
  if (kIsWayland || kIsXWayland) {
    nsAutoCString waylandDisplayName(PR_GetEnv("WAYLAND_DISPLAY"));
    nsAutoCString socketPath;
    nsAutoCString xdgRuntimeDir(PR_GetEnv("XDG_RUNTIME_DIR"));
    if (waylandDisplayName[0] == '/') {
      socketPath = waylandDisplayName;
    } else if (!xdgRuntimeDir.IsEmpty()) {
      socketPath = nsPrintfCString("%s/%s", xdgRuntimeDir.get(),
                                   waylandDisplayName.get());
    }
    // If WAYLAND_DISPLAY is relative and XDG_RUNTIME_DIR is unset, socketPath
    // will be empty and EGL connect() will fail silently at runtime.
    if (!socketPath.IsEmpty()) {
      policy->AddPath(SandboxBroker::MAY_CONNECT, socketPath.get());
    }
  }
}
#endif

static void AddGLDependencies(SandboxBroker::Policy* policy) {
  // Devices
  policy->AddTree(rdwr, "/dev/dri");
  policy->AddFilePrefix(rdwr, "/dev", "nvidia");

  // Hardware info
  AddDriPaths(policy);

  // /etc and /usr/share (glvnd, libdrm, drirc, ...?)
  policy->AddTree(rdonly, "/etc");
  policy->AddTree(rdonly, "/usr/share");
  policy->AddTree(rdonly, "/usr/local/share");

  // Snap puts the usual /usr/share things in a different place, and
  // we'll fail to load the library if we don't have (at least) the
  // glvnd config:
  if (const char* snapDesktopDir = PR_GetEnv("SNAP_DESKTOP_RUNTIME")) {
    nsAutoCString snapDesktopShare(snapDesktopDir);
    snapDesktopShare.AppendLiteral("/usr/share");
    policy->AddTree(rdonly, snapDesktopShare.get());
  }

  // Introduced by Snap's core24 changes there is a gpu-2404 dependency and
  // it is recommended to allow access to all of it?
  if (const char* snapRoot = PR_GetEnv("SNAP")) {
    nsAutoCString snapRootString(snapRoot);
    snapRootString.AppendLiteral("/gpu-2404");
    policy->AddTree(rdonly, snapRootString.get());
  }

  // Note: This function doesn't do anything about Mesa's shader
  // cache, because the details can vary by process type, including
  // whether caching is enabled.

  // This also doesn't include permissions for connecting to a display
  // server, because headless GL (e.g., Mesa GBM) may not need it.
}

// Assums this is an absolute path, SandboxBroker does not like relative paths:
// RealPath() will try to get the absolute path of the llvm profile path to open
// for writing but this will return errno=2 because the file does not exists, so
// sandbox will not allow for its creation.
//
// Forcing expecting an absolute path will be enough to make sure it can be
// allowed.
//
// It should only be allowed on instrumented builds, never on production
// builds.
#if defined(MOZ_PROFILE_GENERATE)
static void AddLLVMProfilePathDirectory(SandboxBroker::Policy* aPolicy) {
  std::string parentPath;
  if (GetLlvmProfileDir(parentPath)) {
    aPolicy->AddFutureDir(rdwrcr, parentPath.c_str());
  }
}
#endif  // defined(MOZ_PROFILE_GENERATE)

// Make sure the path read from environment is correct (absolute).
const char* PathFromEnvIfAbsolute(const char* aPath) {
  const char* envValue = PR_GetEnv(aPath);
  if (envValue && envValue[0] == '/') {
    return envValue;
  }
  return nullptr;
}

void SandboxBrokerPolicyFactory::InitContentPolicy() {
  const int level = GetEffectiveContentSandboxLevel();
  const bool headless = level >= 5;

  // Policy entries that are the same in every process go here, and
  // are cached over the lifetime of the factory.
  SandboxBroker::Policy* policy = new SandboxBroker::Policy;
  // Write permssions

  // Bug 1575985: WASM library sandbox needs RW access to /dev/null
  policy->AddPath(rdwr, "/dev/null");

  if (!headless) {
    AddGLDependencies(policy);
    AddX11Dependencies(policy);
  }

  // Read permissions
  policy->AddPath(rdonly, "/dev/urandom");
  policy->AddPath(rdonly, "/dev/random");
  policy->AddPath(rdonly, "/proc/sys/crypto/fips_enabled");
  policy->AddPath(rdonly, "/proc/cpuinfo");
  policy->AddPath(rdonly, "/proc/meminfo");
  policy->AddTree(rdonly, "/sys/devices/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/cpu");
  policy->AddTree(rdonly, "/lib");
  policy->AddTree(rdonly, "/lib64");
  policy->AddTree(rdonly, "/usr/lib");
  policy->AddTree(rdonly, "/usr/lib32");
  policy->AddTree(rdonly, "/usr/lib64");
  policy->AddTree(rdonly, "/etc");
  policy->AddTree(rdonly, "/usr/share");
  policy->AddTree(rdonly, "/usr/local/share");
  // Various places where fonts reside
  policy->AddTree(rdonly, "/usr/X11R6/lib/X11/fonts");
  policy->AddTree(rdonly, "/nix/store");
  // https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/e434e680d22260f277f4a30ec4660ed32b591d16/files/fontconfig-flatpak.conf
  policy->AddTree(rdonly, "/run/host/fonts");
  policy->AddTree(rdonly, "/run/host/user-fonts");
  policy->AddTree(rdonly, "/run/host/local-fonts");
  policy->AddTree(rdonly, "/var/cache/fontconfig");

  // Bug 1848615
  policy->AddPath(rdonly, "/usr");
  policy->AddPath(rdonly, "/nix");

  AddLdconfigPaths(policy);
  AddLdLibraryEnvPaths(policy);

  if (!headless) {
    // Bug 1385715: NVIDIA PRIME support
    policy->AddPath(rdonly, "/proc/modules");
  }

  // XDG directories might be non existent according to specs:
  // https://specifications.freedesktop.org/basedir-spec/0.8/ar01s04.html
  //
  // > If, when attempting to write a file, the destination directory is
  // > non-existent an attempt should be made to create it with permission 0700.
  //
  // For that we use AddPath(, SandboxBroker::Policy::AddCondition::AddAlways).
  //
  // Allow access to XDG_CONFIG_HOME and XDG_CONFIG_DIRS
  nsAutoCString xdgConfigHome(PathFromEnvIfAbsolute("XDG_CONFIG_HOME"));
  if (!xdgConfigHome.IsEmpty()) {  // AddPath will fail on empty strings
    policy->AddFutureDir(rdonly, xdgConfigHome.get());
  }

  nsAutoCString xdgConfigDirs(PR_GetEnv("XDG_CONFIG_DIRS"));
  for (const auto& path : xdgConfigDirs.Split(':')) {
    if (path.IsEmpty() || path[0] != '/') {
      continue;
    }

    policy->AddFutureDir(rdonly, PromiseFlatCString(path).get());
  }

  // Allow fonts subdir in XDG_DATA_HOME
  nsAutoCString xdgDataHome(PathFromEnvIfAbsolute("XDG_DATA_HOME"));
  if (!xdgDataHome.IsEmpty()) {
    nsAutoCString fontPath(xdgDataHome);
    fontPath.Append("/fonts");
    policy->AddFutureDir(rdonly, PromiseFlatCString(fontPath).get());
  }

  // Any font subdirs in XDG_DATA_DIRS
  nsAutoCString xdgDataDirs(PR_GetEnv("XDG_DATA_DIRS"));
  for (const auto& path : xdgDataDirs.Split(':')) {
    if (path.IsEmpty() || path[0] != '/') {
      continue;
    }

    nsAutoCString fontPath(path);
    fontPath.Append("/fonts");
    policy->AddFutureDir(rdonly, PromiseFlatCString(fontPath).get());
  }

  // Extra configuration/cache dirs in the homedir that we want to allow read
  // access to.
  std::vector<const char*> extraConfDirsAllow = {
      ".themes",
      ".fonts",
      ".cache/fontconfig",
  };

  // Fallback if XDG_CONFIG_HOME isn't set
  if (xdgConfigHome.IsEmpty()) {
    extraConfDirsAllow.emplace_back(".config");
  }

  nsCOMPtr<nsIFile> homeDir;
  nsresult rv =
      GetSpecialSystemDirectory(Unix_HomeDirectory, getter_AddRefs(homeDir));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIFile> confDir;

    for (const auto& dir : extraConfDirsAllow) {
      rv = homeDir->Clone(getter_AddRefs(confDir));
      if (NS_SUCCEEDED(rv)) {
        rv = confDir->AppendRelativeNativePath(nsDependentCString(dir));
        if (NS_SUCCEEDED(rv)) {
          nsAutoCString tmpPath;
          rv = confDir->GetNativePath(tmpPath);
          if (NS_SUCCEEDED(rv)) {
            policy->AddTree(rdonly, tmpPath.get());
          }
        }
      }
    }

    // ~/.config/mozilla/ needs to be manually blocked, because the previous
    // loop will allow for ~/.config/ access.
    {
      // If $XDG_CONFIG_HOME is set, we need to account for it.
      // FIXME: Bug 1722272: Maybe this should just be handled with
      // GetSpecialSystemDirectory(Unix_XDG_ConfigHome) ?
      nsCOMPtr<nsIFile> confDirOrXDGConfigHomeDir;
      if (!xdgConfigHome.IsEmpty()) {
        rv = NS_NewNativeLocalFile(xdgConfigHome,
                                   getter_AddRefs(confDirOrXDGConfigHomeDir));
        // confDirOrXDGConfigHomeDir = nsIFile($XDG_CONFIG_HOME)
      } else {
        rv = homeDir->Clone(getter_AddRefs(confDirOrXDGConfigHomeDir));
        if (NS_SUCCEEDED(rv)) {
          // since we will use that later, we dont need to care about trailing
          // slash
          rv = confDirOrXDGConfigHomeDir->AppendNative(".config"_ns);
          // confDirOrXDGConfigHomeDir = nsIFile($HOME/.config/)
        }
      }

      if (NS_SUCCEEDED(rv)) {
        rv = confDirOrXDGConfigHomeDir->AppendNative("mozilla"_ns);
        if (NS_SUCCEEDED(rv)) {
          nsAutoCString tmpPath;
          rv = confDirOrXDGConfigHomeDir->GetNativePath(tmpPath);
          if (NS_SUCCEEDED(rv)) {
            policy->AddFutureDir(deny, tmpPath.get());
          }
        }
      }
    }

    // ~/.local/share (for themes)
    rv = homeDir->Clone(getter_AddRefs(confDir));
    if (NS_SUCCEEDED(rv)) {
      rv = confDir->AppendNative(".local"_ns);
      if (NS_SUCCEEDED(rv)) {
        rv = confDir->AppendNative("share"_ns);
      }
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString tmpPath;
        rv = confDir->GetNativePath(tmpPath);
        if (NS_SUCCEEDED(rv)) {
          policy->AddTree(rdonly, tmpPath.get());
        }
      }
    }

    // ~/.fonts.conf (Fontconfig)
    rv = homeDir->Clone(getter_AddRefs(confDir));
    if (NS_SUCCEEDED(rv)) {
      rv = confDir->AppendNative(".fonts.conf"_ns);
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString tmpPath;
        rv = confDir->GetNativePath(tmpPath);
        if (NS_SUCCEEDED(rv)) {
          policy->AddPath(rdonly, tmpPath.get());
        }
      }
    }

    // .pangorc
    rv = homeDir->Clone(getter_AddRefs(confDir));
    if (NS_SUCCEEDED(rv)) {
      rv = confDir->AppendNative(".pangorc"_ns);
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString tmpPath;
        rv = confDir->GetNativePath(tmpPath);
        if (NS_SUCCEEDED(rv)) {
          policy->AddPath(rdonly, tmpPath.get());
        }
      }
    }
  }

  // Firefox binary dir.
  // Note that unlike the previous cases, we use NS_GetSpecialDirectory
  // instead of GetSpecialSystemDirectory. The former requires a working XPCOM
  // system, which may not be the case for some tests. For querying for the
  // location of XPCOM things, we can use it anyway.
  nsCOMPtr<nsIFile> ffDir;
  rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(ffDir));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString tmpPath;
    rv = ffDir->GetNativePath(tmpPath);
    if (NS_SUCCEEDED(rv)) {
      policy->AddTree(rdonly, tmpPath.get());
    }
  }

  if (!mozilla::IsPackagedBuild()) {
    // If this is not a packaged build the resources are likely symlinks to
    // outside the binary dir. Therefore in non-release builds we allow reads
    // from the whole repository. MOZ_DEVELOPER_REPO_DIR is set by mach run.
    const char* developer_repo_dir = PR_GetEnv("MOZ_DEVELOPER_REPO_DIR");
    if (developer_repo_dir) {
      policy->AddTree(rdonly, developer_repo_dir);
    }
  }

#ifdef DEBUG
  char* bloatLog = PR_GetEnv("XPCOM_MEM_BLOAT_LOG");
  // XPCOM_MEM_BLOAT_LOG has the format
  // /tmp/tmpd0YzFZ.mozrunner/runtests_leaks.log
  // but stores into /tmp/tmpd0YzFZ.mozrunner/runtests_leaks_tab_pid3411.log
  // So cut the .log part and whitelist the prefix.
  if (bloatLog != nullptr) {
    size_t bloatLen = strlen(bloatLog);
    if (bloatLen >= 4) {
      nsAutoCString bloatStr(bloatLog);
      bloatStr.Truncate(bloatLen - 4);
      policy->AddPrefix(rdwrcr, bloatStr.get());
    }
  }
#endif

  if (!headless) {
    AddX11Dependencies(policy);
  }

  // Bug 1732580: when packaged as a strictly confined snap, may need
  // read-access to configuration files under $SNAP/.
  const char* snap = PR_GetEnv("SNAP");
  if (snap) {
    // When running as a snap, the directory pointed to by $SNAP is guaranteed
    // to exist before the app is launched, but unit tests need to create it
    // dynamically, hence the use of AddFutureDir().
    policy->AddTree(rdonly, snap);
  }

  // Read any extra paths that will get write permissions,
  // configured by the user or distro
  AddDynamicPathList(policy, "security.sandbox.content.write_path_whitelist",
                     rdwr);

  // Whitelisted for reading by the user/distro
  AddDynamicPathList(policy, "security.sandbox.content.read_path_whitelist",
                     rdonly);

  // userContent.css and the extensions dir sit in the profile, which is
  // normally blocked.
  nsCOMPtr<nsIFile> profileDir;
  rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                              getter_AddRefs(profileDir));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIFile> workDir;
    rv = profileDir->Clone(getter_AddRefs(workDir));
    if (NS_SUCCEEDED(rv)) {
      rv = workDir->AppendNative("chrome"_ns);
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString tmpPath;
        rv = workDir->GetNativePath(tmpPath);
        if (NS_SUCCEEDED(rv)) {
          policy->AddTree(rdonly, tmpPath.get());
        }
      }
    }
    rv = profileDir->Clone(getter_AddRefs(workDir));
    if (NS_SUCCEEDED(rv)) {
      rv = workDir->AppendNative("extensions"_ns);
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString tmpPath;
        rv = workDir->GetNativePath(tmpPath);
        if (NS_SUCCEEDED(rv)) {
          policy->AddFutureDir(rdonly, tmpPath.get());
        }
      }
    }
  }

  bool allowPulse = false;
  bool allowAlsa = false;
  if (level < 4) {
#ifdef MOZ_PULSEAUDIO
    allowPulse = true;
#endif
#ifdef MOZ_ALSA
    allowAlsa = true;
#endif
  }

  if (allowAlsa) {
    // Bug 1309098: ALSA support
    policy->AddTree(rdwr, "/dev/snd");
  }

  if (allowPulse) {
    policy->AddTree(rdwrcr, "/dev/shm");
  }

#ifdef MOZ_WIDGET_GTK
  if (const auto userDir = g_get_user_runtime_dir()) {
    // Bug 1321134: DConf's single bit of shared memory
    // The leaf filename is "user" by default, but is configurable.
    nsPrintfCString shmPath("%s/dconf/", userDir);
    policy->AddFutureDir(rdwrcr, shmPath.get());
    policy->AddAncestors(shmPath.get());
    if (allowPulse) {
      // PulseAudio, if it can't get server info from X11, will break
      // unless it can open this directory (or create it, but in our use
      // case we know it already exists).  See bug 1335329.
      nsPrintfCString pulsePath("%s/pulse", userDir);
      policy->AddPath(rdonly, pulsePath.get());
    }
  }
#endif  // MOZ_WIDGET_GTK

  if (allowPulse) {
    // PulseAudio also needs access to read the $XAUTHORITY file (see
    // bug 1384986 comment #1), but that's already allowed for hybrid
    // GPU drivers (see above).
    policy->AddPath(rdonly, "/var/lib/dbus/machine-id");
  }

  // Bug 1434711 - AMDGPU-PRO crashes if it can't read it's marketing ids
  // and various other things
  if (!headless && HasAtiDrivers()) {
    policy->AddTree(rdonly, "/opt/amdgpu/share");
    policy->AddPath(rdonly, "/sys/module/amdgpu");
  }

#if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectory(policy);
#endif

  mCommonContentPolicy.reset(policy);
}

UniquePtr<SandboxBroker::Policy> SandboxBrokerPolicyFactory::GetContentPolicy(
    int aPid, bool aFileProcess) {
  // Policy entries that vary per-process (because they depend on the
  // pid or content subtype) are added here.

  MOZ_ASSERT(NS_IsMainThread());

  const int level = GetEffectiveContentSandboxLevel();
  // The file broker is used at level 2 and up.
  if (level <= 1) {
    // Level 1 has been removed.
    MOZ_ASSERT(level == 0);
    return nullptr;
  }

  std::call_once(mContentInited, [this] { InitContentPolicy(); });
  MOZ_ASSERT(mCommonContentPolicy);
  UniquePtr<SandboxBroker::Policy> policy(
      new SandboxBroker::Policy(*mCommonContentPolicy));

  // No read blocking at level 2 and below.
  // file:// processes also get global read permissions
  if (level <= 2 || aFileProcess) {
    policy->RemoveAllDenyRules();
    policy->AddTree(rdonly, "/");
    // Any other read-only rules will be removed as redundant by
    // Policy::FixRecursivePermissions, so there's no need to
    // early-return here.
  }

  // Access to /dev/shm is restricted to a per-process prefix to
  // prevent interfering with other processes or with services outside
  // the browser (e.g., PulseAudio).
  AddSharedMemoryPaths(policy.get(), aPid);

  // Bug 1198550: the profiler's replacement for dl_iterate_phdr
  policy->AddPath(rdonly, nsPrintfCString("/proc/%d/maps", aPid).get());

  // Bug 1736040: CPU use telemetry
  policy->AddPath(rdonly, nsPrintfCString("/proc/%d/stat", aPid).get());

  // Bug 1198552: memory reporting.
  AddMemoryReporting(policy.get(), aPid);

  // Bug 1384804, notably comment 15
  // Used by libnuma, included by x265/ffmpeg, who falls back
  // to get_mempolicy if this fails
  policy->AddPath(rdonly, nsPrintfCString("/proc/%d/status", aPid).get());

  // Finalize the policy.
  policy->FixRecursivePermissions();
  return policy;
}

#ifdef MOZ_ENABLE_V4L2
static void AddV4l2Dependencies(SandboxBroker::Policy* policy) {
  // For V4L2 hardware-accelerated video decode, RDD needs access to certain
  // /dev/video* devices but don't want to allow it access to webcams etc.
  // So we only allow it access to M2M video devices (encoders and decoders).
  DIR* dir = opendir("/dev");
  if (!dir) {
    SANDBOX_LOG("Couldn't list /dev");
    return;
  }

  struct dirent* dir_entry;
  while ((dir_entry = readdir(dir))) {
    if (strncmp(dir_entry->d_name, "video", 5)) {
      // Not a /dev/video* device, so ignore it
      continue;
    }

    nsCString path = "/dev/"_ns;
    path += nsDependentCString(dir_entry->d_name);

    int fd = open(path.get(), O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
      // Couldn't open this device, so ignore it.
      SANDBOX_LOG("Couldn't open video device %s", path.get());
      continue;
    }

    // Query device capabilities
    struct v4l2_capability cap;
    int result = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (result < 0) {
      // Couldn't query capabilities of this device, so ignore it
      SANDBOX_LOG("Couldn't query capabilities of video device %s", path.get());
      close(fd);
      continue;
    }

    if ((cap.device_caps & V4L2_CAP_VIDEO_M2M) ||
        (cap.device_caps & V4L2_CAP_VIDEO_M2M_MPLANE)) {
      // This is an M2M device (i.e. not a webcam), so allow access
      policy->AddPath(rdwr, path.get());
    }

    close(fd);
  }
  closedir(dir);

  // FFmpeg V4L2 needs to list /dev to find V4L2 devices.
  policy->AddPath(rdonly, "/dev");
}
#endif  // MOZ_ENABLE_V4L2

#ifdef MOZ_ENABLE_VULKAN_VIDEO

static void AddVulkanDependencies(SandboxBroker::Policy* policy) {
  // RDD Vulkan Video decode: ICD manifests (paths beyond AddGLDependencies).
  policy->AddTree(rdonly, "/usr/share/vulkan/icd.d");
  policy->AddTree(rdonly, "/usr/local/share/vulkan/icd.d");
  policy->AddTree(rdonly, "/etc/vulkan/icd.d");

  // Allow user-provided optional extra ICD JSON manifests for the Vulkan loader
  // (colon-separated list, used as the input for the Vulkan loader).
  const char* vkDriverFiles = PR_GetEnv("VK_DRIVER_FILES");
  if (vkDriverFiles && vkDriverFiles[0] != '\0') {
    nsAutoCString paths(vkDriverFiles);
    for (const nsACString& path : paths.Split(':')) {
      if (!path.IsEmpty()) {
        char* resolvedPath = realpath(PromiseFlatCString(path).get(), nullptr);
        if (resolvedPath) {
          policy->AddTree(rdonly, resolvedPath);
          free(resolvedPath);
        }
      }
    }
  }

  // Custom Mesa prefix to allow user-provided Intel ANV Mesa build dir (binary
  // files location)
  const char* mesaBuildDir = PR_GetEnv("MESA_BUILD_DIR");
  if (mesaBuildDir && mesaBuildDir[0] != '\0') {
    policy->AddTree(rdonly, nsPrintfCString("%s", mesaBuildDir).get());
  }

  // Custom Vulkan SDK prefix to allow user-provided Vulkan SDK dir (binary and
  // source files location)
  const char* vulkanSdkDir = PR_GetEnv("VULKAN_SDK");
  if (vulkanSdkDir && vulkanSdkDir[0] != '\0') {
    policy->AddTree(rdonly, nsPrintfCString("%s", vulkanSdkDir).get());
  }

  policy->AddPath(rdwr, "/dev/nvidiactl", SandboxBroker::Policy::AddAlways);
  policy->AddPath(rdwr, "/dev/nvidia-uvm", SandboxBroker::Policy::AddAlways);
  policy->AddPath(rdwr, "/dev/nvidia-modeset",
                  SandboxBroker::Policy::AddAlways);
  policy->AddTree(rdonly, "/dev/nvidia-caps");
  for (int i = 0; i < 8; i++) {
    policy->AddPath(rdwr, nsPrintfCString("/dev/nvidia%d", i).get(),
                    SandboxBroker::Policy::AddIfExistsNow);
  }
  // NVIDIA Wayland DMA-BUF export for Vulkan video (EGL / copy-ring NV12).
  // AddAlways because the udmabuf misc device only exists while its module is
  // loaded, which may happen after this policy is built.
  policy->AddPath(rdwr, "/dev/udmabuf", SandboxBroker::Policy::AddAlways);
}
#endif  // MOZ_ENABLE_VULKAN_VIDEO

/* static */ UniquePtr<SandboxBroker::Policy>
SandboxBrokerPolicyFactory::GetRDDPolicy(int aPid) {
  auto policy = MakeUnique<SandboxBroker::Policy>();

  AddSharedMemoryPaths(policy.get(), aPid);

  policy->AddPath(rdonly, "/dev/urandom");
  // FIXME (bug 1662321): we should fix nsSystemInfo so that every
  // child process doesn't need to re-read these files to get the info
  // the parent process already has.
  policy->AddPath(rdonly, "/proc/cpuinfo");
  policy->AddPath(rdonly,
                  "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
  policy->AddPath(rdonly, "/sys/devices/system/cpu/cpu0/cache/index2/size");
  policy->AddPath(rdonly, "/sys/devices/system/cpu/cpu0/cache/index3/size");
  policy->AddTree(rdonly, "/sys/devices/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/node");
  policy->AddTree(rdonly, "/lib");
  policy->AddTree(rdonly, "/lib64");
  policy->AddTree(rdonly, "/usr/lib");
  policy->AddTree(rdonly, "/usr/lib32");
  policy->AddTree(rdonly, "/usr/lib64");
  policy->AddTree(rdonly, "/run/opengl-driver/lib");
  policy->AddTree(rdonly, "/nix/store");

  // Bug 1647957: memory reporting.
  AddMemoryReporting(policy.get(), aPid);

  // Firefox binary dir.
  // Note that unlike the previous cases, we use NS_GetSpecialDirectory
  // instead of GetSpecialSystemDirectory. The former requires a working XPCOM
  // system, which may not be the case for some tests. For querying for the
  // location of XPCOM things, we can use it anyway.
  nsCOMPtr<nsIFile> ffDir;
  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(ffDir));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString tmpPath;
    rv = ffDir->GetNativePath(tmpPath);
    if (NS_SUCCEEDED(rv)) {
      policy->AddTree(rdonly, tmpPath.get());
    }
  }

  if (!mozilla::IsPackagedBuild()) {
    // If this is not a packaged build the resources are likely symlinks to
    // outside the binary dir. Therefore in non-release builds we allow reads
    // from the whole repository. MOZ_DEVELOPER_REPO_DIR is set by mach run.
    const char* developer_repo_dir = PR_GetEnv("MOZ_DEVELOPER_REPO_DIR");
    if (developer_repo_dir) {
      policy->AddTree(rdonly, developer_repo_dir);
    }
  }

  // VA-API needs GPU access and GL context creation (but not display
  // server access, as of bug 1769499).
  AddGLDependencies(policy.get());

  // FFmpeg and GPU drivers may need general-case library loading
  AddLdconfigPaths(policy.get());
  AddLdLibraryEnvPaths(policy.get());

#ifdef MOZ_ENABLE_VULKAN_VIDEO
  // Only open Vulkan-specific sandbox paths if Vulkan Video is actually
  // enabled and supported on this GPU, to avoid granting display-server
  // access when the feature is blocked or disabled.
  if (gfx::gfxVars::CanUseVulkanHardwareVideoDecoding()) {
    AddVulkanDependencies(policy.get());
#  if defined(MOZ_WIDGET_GTK)
    // EGL needs display server sockets for EGL_MESA_image_dma_buf_export
    // (bug 2021722).
    AddWaylandDependencies(policy.get());
#  endif
    // Vulkan on X11/XWayland needs display server socket access.
    AddX11Dependencies(policy.get());
#  if defined(MOZ_WIDGET_GTK) && defined(MOZ_X11)
    // AddX11Dependencies() skips the X socket on a Wayland UI, but the EGL
    // bring-up in RDD still connects to XWayland (bug 2057724). Grant the
    // same paths it would have granted on X11: the socket, and the cookie
    // ($XAUTHORITY — on GNOME this is $XDG_RUNTIME_DIR/.mutter-Xwaylandauth.*).
    if (mozilla::widget::GdkIsWaylandDisplay() && PR_GetEnv("DISPLAY")) {
      policy->AddPrefix(SandboxBroker::MAY_CONNECT, "/tmp/.X11-unix/X");
      if (auto* const xauth = PR_GetEnv("XAUTHORITY")) {
        policy->AddPath(rdonly, xauth);
      }
    }
#  endif
  }
#endif  // MOZ_ENABLE_VULKAN_VIDEO

#ifdef MOZ_ENABLE_V4L2
  AddV4l2Dependencies(policy.get());
#endif  // MOZ_ENABLE_V4L2

  // Bug 1903688: NVIDIA Tegra hardware decoding from Linux4Tegra
  // Only built on ARM64 since Tegra is ARM64 SoC with different drivers, so the
  // path are not needed on e.g. x86-64
#if defined(__aarch64__)
  policy->AddTree(rdonly, "/sys/devices/system/present");
  policy->AddTree(rdonly, "/sys/module/tegra_fuse");
  policy->AddPath(rdwr, "/dev/nvmap");
  policy->AddPath(rdwr, "/dev/nvhost-ctrl");
  policy->AddPath(rdwr, "/dev/nvhost-ctrl-gpu");
  policy->AddPath(rdwr, "/dev/nvhost-nvdec");
  policy->AddPath(rdwr, "/dev/nvhost-nvdec1");
  policy->AddPath(rdwr, "/dev/nvhost-vic");
#endif  // defined(__aarch64__)

#if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectory(policy.get());
#endif

  if (policy->IsEmpty()) {
    policy = nullptr;
  }
  return policy;
}

/* static */ UniquePtr<SandboxBroker::Policy>
SandboxBrokerPolicyFactory::GetSocketProcessPolicy(int aPid) {
  auto policy = MakeUnique<SandboxBroker::Policy>();

  policy->AddPath(rdonly, "/dev/urandom");
  policy->AddPath(rdonly, "/dev/random");
  policy->AddPath(rdonly, "/proc/sys/crypto/fips_enabled");
  policy->AddPath(rdonly, "/proc/cpuinfo");
  policy->AddPath(rdonly, "/proc/meminfo");
  policy->AddTree(rdonly, "/sys/devices/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/cpu");
  policy->AddTree(rdonly, "/lib");
  policy->AddTree(rdonly, "/lib64");
  policy->AddTree(rdonly, "/usr/lib");
  policy->AddTree(rdonly, "/usr/lib32");
  policy->AddTree(rdonly, "/usr/lib64");
  policy->AddTree(rdonly, "/usr/share");
  policy->AddTree(rdonly, "/usr/local/share");
  policy->AddTree(rdonly, "/etc");

  // glibc will try to stat64("/") while populating nsswitch database
  // https://sourceware.org/git/?p=glibc.git;a=blob;f=nss/nss_database.c;h=cf0306adc47f12d9bc761ab1b013629f4482b7e6;hb=9826b03b747b841f5fc6de2054bf1ef3f5c4bdf3#l396
  // denying will make getaddrinfo() return ENONAME
  policy->AddPath(access, "/");

  AddLdconfigPaths(policy.get());

  // Socket process sandbox needs to allow shmem in order to support
  // profiling.  See Bug 1626385.
  AddSharedMemoryPaths(policy.get(), aPid);

  // Bug 1647957: memory reporting.
  AddMemoryReporting(policy.get(), aPid);

  // Firefox binary dir.
  // Note that unlike the previous cases, we use NS_GetSpecialDirectory
  // instead of GetSpecialSystemDirectory. The former requires a working XPCOM
  // system, which may not be the case for some tests. For querying for the
  // location of XPCOM things, we can use it anyway.
  nsCOMPtr<nsIFile> ffDir;
  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(ffDir));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString tmpPath;
    rv = ffDir->GetNativePath(tmpPath);
    if (NS_SUCCEEDED(rv)) {
      policy->AddTree(rdonly, tmpPath.get());
    }
  }

#if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectory(policy.get());
#endif

  if (policy->IsEmpty()) {
    policy = nullptr;
  }
  return policy;
}

/* static */ UniquePtr<SandboxBroker::Policy>
SandboxBrokerPolicyFactory::GetUtilityProcessPolicy(int aPid) {
  auto policy = MakeUnique<SandboxBroker::Policy>();

  policy->AddPath(rdonly, "/dev/urandom");
  policy->AddPath(rdonly, "/proc/cpuinfo");
  policy->AddPath(rdonly, "/proc/meminfo");
  policy->AddPath(rdonly, nsPrintfCString("/proc/%d/exe", aPid).get());
  policy->AddTree(rdonly, "/sys/devices/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/cpu");
  policy->AddTree(rdonly, "/lib");
  policy->AddTree(rdonly, "/lib64");
  policy->AddTree(rdonly, "/usr/lib");
  policy->AddTree(rdonly, "/usr/lib32");
  policy->AddTree(rdonly, "/usr/lib64");
  policy->AddTree(rdonly, "/usr/share");
  policy->AddTree(rdonly, "/usr/local/share");
  policy->AddTree(rdonly, "/etc");
  // Required to make sure ffmpeg loads properly, this is already existing on
  // Content and RDD
  policy->AddTree(rdonly, "/nix/store");

  // glibc will try to stat64("/") while populating nsswitch database
  // https://sourceware.org/git/?p=glibc.git;a=blob;f=nss/nss_database.c;h=cf0306adc47f12d9bc761ab1b013629f4482b7e6;hb=9826b03b747b841f5fc6de2054bf1ef3f5c4bdf3#l396
  // denying will make getaddrinfo() return ENONAME
  policy->AddPath(access, "/");

  AddLdconfigPaths(policy.get());
  AddLdLibraryEnvPaths(policy.get());

  // Utility process sandbox needs to allow shmem in order to support
  // profiling.  See Bug 1626385.
  AddSharedMemoryPaths(policy.get(), aPid);

  // Bug 1647957: memory reporting.
  AddMemoryReporting(policy.get(), aPid);

  // Firefox binary dir.
  // Note that unlike the previous cases, we use NS_GetSpecialDirectory
  // instead of GetSpecialSystemDirectory. The former requires a working XPCOM
  // system, which may not be the case for some tests. For querying for the
  // location of XPCOM things, we can use it anyway.
  nsCOMPtr<nsIFile> ffDir;
  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(ffDir));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString tmpPath;
    rv = ffDir->GetNativePath(tmpPath);
    if (NS_SUCCEEDED(rv)) {
      policy->AddTree(rdonly, tmpPath.get());
    }
  }

#if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectory(policy.get());
#endif

  if (policy->IsEmpty()) {
    policy = nullptr;
  }
  return policy;
}

#ifndef ANDROID
/* static */
UniquePtr<SandboxBroker::Policy>
SandboxBrokerPolicyFactory::GetHWInferencePolicy(int aPid) {
  auto policy = MakeUnique<SandboxBroker::Policy>();

  AddSharedMemoryPaths(policy.get(), aPid);

  policy->AddPath(rdonly, "/dev/urandom");
  // FIXME (bug 1662321): we should fix nsSystemInfo so that every
  // child process doesn't need to re-read these files to get the info
  // the parent process already has.
  policy->AddPath(rdonly, "/proc/cpuinfo");
  policy->AddPath(rdonly,
                  "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
  policy->AddPath(rdonly, "/sys/devices/system/cpu/cpu0/cache/index2/size");
  policy->AddPath(rdonly, "/sys/devices/system/cpu/cpu0/cache/index3/size");
  policy->AddTree(rdonly, "/sys/devices/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/cpu");
  policy->AddTree(rdonly, "/sys/devices/system/node");
  policy->AddTree(rdonly, "/lib");
  policy->AddTree(rdonly, "/lib64");
  policy->AddTree(rdonly, "/usr/lib");
  policy->AddTree(rdonly, "/usr/lib32");
  policy->AddTree(rdonly, "/usr/lib64");
  policy->AddTree(rdonly, "/run/opengl-driver/lib");
  policy->AddTree(rdonly, "/nix/store");

  // Bug 1647957: memory reporting.
  AddMemoryReporting(policy.get(), aPid);

  // Firefox binary dir.
  // Note that unlike the previous cases, we use NS_GetSpecialDirectory
  // instead of GetSpecialSystemDirectory. The former requires a working XPCOM
  // system, which may not be the case for some tests. For querying for the
  // location of XPCOM things, we can use it anyway.
  nsCOMPtr<nsIFile> ffDir;
  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(ffDir));
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString tmpPath;
    rv = ffDir->GetNativePath(tmpPath);
    if (NS_SUCCEEDED(rv)) {
      policy->AddTree(rdonly, tmpPath.get());
    }
  }

  if (!mozilla::IsPackagedBuild()) {
    // If this is not a packaged build the resources are likely symlinks to
    // outside the binary dir. Therefore in non-release builds we allow reads
    // from the whole repository. MOZ_DEVELOPER_REPO_DIR is set by mach run.
    const char* developer_repo_dir = PR_GetEnv("MOZ_DEVELOPER_REPO_DIR");
    if (developer_repo_dir) {
      policy->AddTree(rdonly, developer_repo_dir);
    }
  }

  // GPU compute backends need GPU access and GL context creation (but not
  // display server access, as of bug 1769499).
  AddGLDependencies(policy.get());

  // GPU drivers may need general-case library loading.
  AddLdconfigPaths(policy.get());
  AddLdLibraryEnvPaths(policy.get());

#  if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectory(policy.get());
#  endif

  return policy;
}
#endif  // !ANDROID

}  // namespace mozilla
