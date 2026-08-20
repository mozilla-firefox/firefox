/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The Mac sandbox module is a static library (a Library in moz.build terms)
// that can be linked into any binary (for example plugin-container or XUL).
// It must not have dependencies on any other Mozilla module.  This is why,
// for example, it has its own OS X version detection code, rather than
// linking to nsCocoaFeatures.mm in XUL.

#include "Sandbox.h"

#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <sstream>
#include <vector>

#include "SandboxPolicyContent.h"
#include "SandboxPolicyGMP.h"
#include "SandboxPolicyGPU.h"
#include "SandboxPolicyHWInference.h"
#include "SandboxPolicyRDD.h"
#include "SandboxPolicySocket.h"
#include "SandboxPolicyUtility.h"
#if defined(MOZ_PROFILE_GENERATE)
#  include "SandboxPolicyPGO.h"
#endif  // defined(MOZ_PROFILE_GENERATE)

#include "mozilla/Assertions.h"

#include "mozilla/GeckoArgs.h"
#include "mozilla/SandboxSettings.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"

// Undocumented sandbox setup routines.
extern "C" int sandbox_init_with_parameters(const char* profile, uint64_t flags,
                                            const char* const parameters[],
                                            char** errorbuf);
extern "C" void sandbox_free_error(char* errorbuf);
extern "C" int sandbox_check(pid_t pid, const char* operation, int type, ...);

// Note about "major", "minor" and "bugfix" in the following code:
//
// The code decomposes an OS X version number into these components, and in
// doing so follows Apple's terminology in Gestalt.h.  But this is very
// misleading, because in other contexts Apple uses the "minor" component of
// an OS X version number to indicate a "major" release (for example the "9"
// in OS X 10.9.5), and the "bugfix" component to indicate a "minor" release
// (for example the "5" in OS X 10.9.5).
class OSXVersion {
 public:
  static void Get(int32_t& aMajor, int32_t& aMinor);

 private:
  static void GetSystemVersion(int32_t& aMajor, int32_t& aMinor,
                               int32_t& aBugFix);
  static bool mCached;
  static int32_t mOSXVersionMajor;
  static int32_t mOSXVersionMinor;
};

bool OSXVersion::mCached = false;
int32_t OSXVersion::mOSXVersionMajor;
int32_t OSXVersion::mOSXVersionMinor;

void OSXVersion::Get(int32_t& aMajor, int32_t& aMinor) {
  if (!mCached) {
    int32_t major, minor, bugfix;
    GetSystemVersion(major, minor, bugfix);
    mOSXVersionMajor = major;
    mOSXVersionMinor = minor;
    mCached = true;
  }
  aMajor = mOSXVersionMajor;
  aMinor = mOSXVersionMinor;
}

void OSXVersion::GetSystemVersion(int32_t& aMajor, int32_t& aMinor,
                                  int32_t& aBugFix) {
  SInt32 major = 0, minor = 0, bugfix = 0;

  CFURLRef url = CFURLCreateWithString(
      kCFAllocatorDefault,
      CFSTR("file:///System/Library/CoreServices/SystemVersion.plist"),
      nullptr);
  CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
  CFReadStreamOpen(stream);
  CFDictionaryRef sysVersionPlist =
      (CFDictionaryRef)CFPropertyListCreateWithStream(
          kCFAllocatorDefault, stream, 0, kCFPropertyListImmutable, nullptr,
          nullptr);
  CFReadStreamClose(stream);
  CFRelease(stream);
  CFRelease(url);

  CFStringRef versionString = (CFStringRef)CFDictionaryGetValue(
      sysVersionPlist, CFSTR("ProductVersion"));
  CFArrayRef versions = CFStringCreateArrayBySeparatingStrings(
      kCFAllocatorDefault, versionString, CFSTR("."));
  CFIndex count = CFArrayGetCount(versions);
  if (count > 0) {
    CFStringRef component = (CFStringRef)CFArrayGetValueAtIndex(versions, 0);
    major = CFStringGetIntValue(component);
    if (count > 1) {
      component = (CFStringRef)CFArrayGetValueAtIndex(versions, 1);
      minor = CFStringGetIntValue(component);
      if (count > 2) {
        component = (CFStringRef)CFArrayGetValueAtIndex(versions, 2);
        bugfix = CFStringGetIntValue(component);
      }
    }
  }
  CFRelease(sysVersionPlist);
  CFRelease(versions);

  if (major < 10) {
    // If 'major' isn't what we expect, assume 10.6.
    aMajor = 10;
    aMinor = 6;
    aBugFix = 0;
  } else if ((major == 10) && (minor >= 16)) {
    // Account for SystemVersionCompat.plist being used which is
    // automatically used for builds using older SDK versions and
    // results in 11.0 being reported as 10.16. Assume the compat
    // version will increase in step with the correct version.
    aMajor = 11;
    aMinor = minor - 16;
    aBugFix = bugfix;
  } else {
    aMajor = major;
    aMinor = minor;
    aBugFix = bugfix;
  }
}

bool GetRealPath(std::string& aOutputPath, const char* aInputPath) {
  char* resolvedPath = realpath(aInputPath, nullptr);
  if (resolvedPath == nullptr) {
    return false;
  }

  aOutputPath = resolvedPath;
  free(resolvedPath);

  return !aOutputPath.empty();
}

/*
 * Returns true if the process is running under Rosetta translation. Returns
 * false if running natively or if an error was encountered. To be called
 * before enabling the sandbox therefore not requiring the sysctl be allowed
 * by the sandbox policy. We use the `sysctl.proc_translated` sysctl which is
 * documented by Apple to be used for this purpose.
 */
bool ProcessIsRosettaTranslated() {
  int ret = 0;
  size_t size = sizeof(ret);
  if (sysctlbyname("sysctl.proc_translated", &ret, &size, nullptr, 0) == -1) {
    if (errno != ENOENT) {
      fprintf(stderr, "Failed to check for translation environment\n");
    }
    return false;
  }
  return (ret == 1);
}

void MacSandboxInfo::AppendAsParams(std::vector<std::string>& aParams) const {
  this->AppendStartupParam(aParams);
  this->AppendLoggingParam(aParams);
  this->AppendAppPathParam(aParams);

  switch (this->type) {
    case MacSandboxType_Content:
      this->AppendLevelParam(aParams);
      this->AppendAudioParam(aParams);
      this->AppendWindowServerParam(aParams);
      this->AppendReadPathParams(aParams);
#ifdef DEBUG
      this->AppendDebugWriteDirParam(aParams);
#endif
      break;
    case MacSandboxType_RDD:
    case MacSandboxType_Socket:
    case MacSandboxType_Utility:
      break;
    case MacSandboxType_GMP:
      this->AppendPluginPathParam(aParams);
      this->AppendWindowServerParam(aParams);
      this->AppendReadPathParams(aParams);
      break;
    case MacSandboxType_GPU:
      break;
    default:
      // Before supporting a new process type, add a case statement
      // here to append any neccesary process-type-specific params.
      MOZ_RELEASE_ASSERT(false);
      break;
  }
}

void MacSandboxInfo::AppendStartupParam(
    std::vector<std::string>& aParams) const {
  aParams.push_back("-sbStartup");
}

void MacSandboxInfo::AppendLoggingParam(
    std::vector<std::string>& aParams) const {
  if (this->shouldLog) {
    aParams.push_back("-sbLogging");
  }
}

void MacSandboxInfo::AppendAppPathParam(
    std::vector<std::string>& aParams) const {
  aParams.push_back("-sbAppPath");
  aParams.push_back(this->appPath);
}

void MacSandboxInfo::AppendPluginPathParam(
    std::vector<std::string>& aParams) const {
  aParams.push_back("-sbPluginPath");
  aParams.push_back(this->pluginPath);
}

/* static */
void MacSandboxInfo::AppendFileAccessParam(std::vector<std::string>& aParams,
                                           bool aHasFilePrivileges) {
  if (aHasFilePrivileges) {
    aParams.push_back("-sbAllowFileAccess");
  }
}

void MacSandboxInfo::AppendLevelParam(std::vector<std::string>& aParams) const {
  std::ostringstream os;
  os << this->level;
  std::string levelString = os.str();
  aParams.push_back("-sbLevel");
  aParams.push_back(levelString);
}

void MacSandboxInfo::AppendAudioParam(std::vector<std::string>& aParams) const {
  if (this->hasAudio) {
    aParams.push_back("-sbAllowAudio");
  }
}

void MacSandboxInfo::AppendWindowServerParam(
    std::vector<std::string>& aParams) const {
  if (this->hasWindowServer) {
    aParams.push_back("-sbAllowWindowServer");
  }
}

void MacSandboxInfo::AppendReadPathParams(
    std::vector<std::string>& aParams) const {
  if (!this->testingReadPath1.empty()) {
    aParams.push_back("-sbTestingReadPath");
    aParams.push_back(this->testingReadPath1.c_str());
  }
  if (!this->testingReadPath2.empty()) {
    aParams.push_back("-sbTestingReadPath");
    aParams.push_back(this->testingReadPath2.c_str());
  }
  if (!this->testingReadPath3.empty()) {
    aParams.push_back("-sbTestingReadPath");
    aParams.push_back(this->testingReadPath3.c_str());
  }
  if (!this->testingReadPath4.empty()) {
    aParams.push_back("-sbTestingReadPath");
    aParams.push_back(this->testingReadPath4.c_str());
  }
}

#ifdef DEBUG
void MacSandboxInfo::AppendDebugWriteDirParam(
    std::vector<std::string>& aParams) const {
  if (!this->debugWriteDir.empty()) {
    aParams.push_back("-sbDebugWriteDir");
    aParams.push_back(this->debugWriteDir.c_str());
  }
}
#endif

namespace mozilla {

bool StartMacSandbox(MacSandboxInfo const& aInfo, std::string& aErrorMessage) {
  std::vector<const char*> params;
  std::string profile;

  // Use a combined version number to simplify version check logic
  // in sandbox policies. For example, 10.14 becomes "1014".
  int32_t major = 0, minor = 0;
  OSXVersion::Get(major, minor);
  MOZ_ASSERT(minor >= 0 && minor < 100);
  std::string combinedVersion = std::to_string((major * 100) + minor);

  params.push_back("IS_ROSETTA_TRANSLATED");
  params.push_back(ProcessIsRosettaTranslated() ? "TRUE" : "FALSE");

  // Used for the content process to access to parts of the cache dir.
  std::string userCacheDir;
  std::string bundleIDCacheDir;

  if (aInfo.type == MacSandboxType_Utility &&
      aInfo.utilityKind != ipc::SandboxingKind::HW_INFERENCE) {
    profile = const_cast<char*>(SandboxPolicyUtility);

    switch (aInfo.utilityKind) {
      case ipc::SandboxingKind::GENERIC_UTILITY:
        // Nothing to do here specifically
        break;

#ifdef MOZ_APPLEMEDIA
      case ipc::SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA: {
        profile.append(SandboxPolicyUtilityMediaServiceAppleMediaAddend);
        params.push_back("MAC_OS_VERSION");
        params.push_back(combinedVersion.c_str());
      } break;
#endif

      default:
        MOZ_ASSERT(false, "Invalid SandboxingKind");
        break;
    }
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    params.push_back("APP_BINARY_PATH");
    params.push_back(aInfo.appBinaryPath.c_str());
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }
  } else if (aInfo.type == MacSandboxType_RDD) {
    profile = const_cast<char*>(SandboxPolicyRDD);
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("MAC_OS_VERSION");
    params.push_back(combinedVersion.c_str());
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    params.push_back("HOME_PATH");
    params.push_back(getenv("HOME"));
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }
  } else if (aInfo.type == MacSandboxType_Socket) {
    profile = const_cast<char*>(SandboxPolicySocket);
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }
    params.push_back("HOME_PATH");
    params.push_back(getenv("HOME"));
  } else if (aInfo.type == MacSandboxType_GMP) {
    profile = const_cast<char*>(SandboxPolicyGMP);
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    params.push_back("PLUGIN_PATH");
    params.push_back(aInfo.pluginPath.c_str());
    if (!aInfo.pluginBinaryPath.empty()) {
      params.push_back("PLUGIN_BINARY_PATH");
      params.push_back(aInfo.pluginBinaryPath.c_str());
    }
    params.push_back("HAS_WINDOW_SERVER");
    params.push_back(aInfo.hasWindowServer ? "TRUE" : "FALSE");
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }
    if (!aInfo.testingReadPath1.empty()) {
      params.push_back("TESTING_READ_PATH1");
      params.push_back(aInfo.testingReadPath1.c_str());
    }
    if (!aInfo.testingReadPath2.empty()) {
      params.push_back("TESTING_READ_PATH2");
      params.push_back(aInfo.testingReadPath2.c_str());
    }
  } else if (aInfo.type == MacSandboxType_Content) {
    MOZ_ASSERT(aInfo.level >= 1);
    if (aInfo.level >= 1) {
      profile = SandboxPolicyContent;
      params.push_back("SHOULD_LOG");
      params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
      params.push_back("SANDBOX_LEVEL_1");
      params.push_back(aInfo.level == 1 ? "TRUE" : "FALSE");
      params.push_back("SANDBOX_LEVEL_2");
      params.push_back(aInfo.level == 2 ? "TRUE" : "FALSE");
      params.push_back("SANDBOX_LEVEL_3");
      params.push_back(aInfo.level == 3 ? "TRUE" : "FALSE");
      params.push_back("MAC_OS_VERSION");
      params.push_back(combinedVersion.c_str());
      params.push_back("APP_PATH");
      params.push_back(aInfo.appPath.c_str());
      params.push_back("PROFILE_DIR");
      params.push_back(aInfo.profileDir.c_str());
      params.push_back("HOME_PATH");
      params.push_back(getenv("HOME"));
      params.push_back("HAS_SANDBOXED_PROFILE");
      params.push_back(aInfo.hasSandboxedProfile ? "TRUE" : "FALSE");
      params.push_back("HAS_WINDOW_SERVER");
      params.push_back(aInfo.hasWindowServer ? "TRUE" : "FALSE");
      if (!aInfo.crashServerPort.empty()) {
        params.push_back("CRASH_PORT");
        params.push_back(aInfo.crashServerPort.c_str());
      }

      params.push_back("DARWIN_USER_CACHE_DIR");
      char confStrBuf[PATH_MAX];
      if (!confstr(_CS_DARWIN_USER_CACHE_DIR, confStrBuf, sizeof(confStrBuf))) {
        return false;
      }
      if (!GetRealPath(userCacheDir, confStrBuf)) {
        return false;
      }
      params.push_back(userCacheDir.c_str());

      if (!aInfo.testingReadPath1.empty()) {
        params.push_back("TESTING_READ_PATH1");
        params.push_back(aInfo.testingReadPath1.c_str());
      }
      if (!aInfo.testingReadPath2.empty()) {
        params.push_back("TESTING_READ_PATH2");
        params.push_back(aInfo.testingReadPath2.c_str());
      }
      if (!aInfo.testingReadPath3.empty()) {
        params.push_back("TESTING_READ_PATH3");
        params.push_back(aInfo.testingReadPath3.c_str());
      }
      if (!aInfo.testingReadPath4.empty()) {
        params.push_back("TESTING_READ_PATH4");
        params.push_back(aInfo.testingReadPath4.c_str());
      }
#ifdef DEBUG
      if (!aInfo.debugWriteDir.empty()) {
        params.push_back("DEBUG_WRITE_DIR");
        params.push_back(aInfo.debugWriteDir.c_str());
      }
#endif  // DEBUG

      if (aInfo.hasFilePrivileges) {
        profile.append(SandboxPolicyContentFileAddend);
      }
      if (aInfo.hasAudio) {
        profile.append(SandboxPolicyContentAudioAddend);
      }
    } else {
      fprintf(stderr,
              "Content sandbox disabled due to sandbox level setting\n");
      return false;
    }
  } else if (aInfo.type == MacSandboxType_GPU) {
    profile = const_cast<char*>(SandboxPolicyGPU);
    if (ProcessIsX86_64()) {
      profile.append(SandboxPolicyGPUx86_64Addend);
    }
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("MAC_OS_VERSION");
    params.push_back(combinedVersion.c_str());
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    params.push_back("HOME_PATH");
    params.push_back(getenv("HOME"));
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }

    params.push_back("DARWIN_USER_CACHE_DIR");
    char confStrBuf[PATH_MAX];
    if (!confstr(_CS_DARWIN_USER_CACHE_DIR, confStrBuf, sizeof(confStrBuf))) {
      return false;
    }
    if (!GetRealPath(userCacheDir, confStrBuf)) {
      return false;
    }
    params.push_back(userCacheDir.c_str());

    // For accessing shader cache paths in the cache dir that
    // are derived from the applications bundle ID.
    bundleIDCacheDir = userCacheDir;
    bundleIDCacheDir.append("/" MOZ_GPU_PROCESS_BUNDLEID);
    params.push_back("BUNDLE_ID_CACHE_DIR");
    params.push_back(bundleIDCacheDir.c_str());
    params.push_back("ALLOW_REMOTE_APPLE_IMAGEIO");
    params.push_back(getenv("MOZ_BLOCK_REMOTE_APPLE_IMAGEIO") ? "FALSE"
                                                              : "TRUE");
  } else if (aInfo.type == MacSandboxType_Utility &&
             aInfo.utilityKind == ipc::SandboxingKind::HW_INFERENCE) {
    profile = const_cast<char*>(SandboxPolicyHWInference);
    params.push_back("SHOULD_LOG");
    params.push_back(aInfo.shouldLog ? "TRUE" : "FALSE");
    params.push_back("MAC_OS_VERSION");
    params.push_back(combinedVersion.c_str());
    params.push_back("APP_PATH");
    params.push_back(aInfo.appPath.c_str());
    params.push_back("HOME_PATH");
    params.push_back(getenv("HOME"));
    if (!aInfo.crashServerPort.empty()) {
      params.push_back("CRASH_PORT");
      params.push_back(aInfo.crashServerPort.c_str());
    }

    params.push_back("DARWIN_USER_CACHE_DIR");
    char confStrBuf[PATH_MAX];
    if (!confstr(_CS_DARWIN_USER_CACHE_DIR, confStrBuf, sizeof(confStrBuf))) {
      return false;
    }
    if (!GetRealPath(userCacheDir, confStrBuf)) {
      return false;
    }
    params.push_back(userCacheDir.c_str());

    // For accessing shader cache paths in the cache dir that
    // are derived from the applications bundle ID.
    bundleIDCacheDir = userCacheDir;
    bundleIDCacheDir.append("/" MOZ_CHILD_PROCESS_BUNDLEID);
    params.push_back("BUNDLE_ID_CACHE_DIR");
    params.push_back(bundleIDCacheDir.c_str());
  } else {
    char* msg = nullptr;
    asprintf(&msg, "Unexpected sandbox type %u", aInfo.type);
    if (msg) {
      aErrorMessage.assign(msg);
      free(msg);
    }
    return false;
  }

  if (profile.empty()) {
    fprintf(stderr, "Out of memory in StartMacSandbox()!\n");
    return false;
  }

#if defined(MOZ_PROFILE_GENERATE)
  // It should only be allowed on instrumented builds, never on production
  // builds.
  std::string parentPath;
  if (GetLlvmProfileDir(parentPath)) {
    params.push_back("PGO_DATA_DIR");
    params.push_back(parentPath.c_str());
    profile.append(SandboxPolicyPGO);
  }
#endif

// In order to avoid relying on any other Mozilla modules (as described at the
// top of this file), we use our own #define instead of the existing MOZ_LOG
// infrastructure. This can be used by developers to debug the macOS sandbox
// policy.
#define MAC_SANDBOX_PRINT_POLICY 0
#if MAC_SANDBOX_PRINT_POLICY
  printf("Sandbox params for PID %d:\n", getpid());
  for (size_t i = 0; i < params.size() / 2; i++) {
    printf("  %s = %s\n", params[i * 2], params[(i * 2) + 1]);
  }
  printf("Sandbox profile:\n%s\n", profile.c_str());
#endif

  // The parameters array is null terminated.
  params.push_back(nullptr);

  char* errorbuf = nullptr;
  int rv = sandbox_init_with_parameters(profile.c_str(), 0, params.data(),
                                        &errorbuf);
  if (rv) {
    if (errorbuf) {
      char* msg = nullptr;
      asprintf(&msg, "sandbox_init() failed with error \"%s\"", errorbuf);
      if (msg) {
        aErrorMessage.assign(msg);
        free(msg);
      }
      fprintf(stderr, "profile: %s\n", profile.c_str());
      sandbox_free_error(errorbuf);
    }
  }
  if (rv) {
    return false;
  }

  return true;
}

/*
 * Fill |aInfo| with content sandbox params parsed from the provided
 * command line arguments. Return false if any sandbox parameters needed
 * for early startup of the sandbox are not present in the arguments.
 */
bool GetContentSandboxParamsFromArgs(int aArgc, char** aArgv,
                                     MacSandboxInfo& aInfo) {
  // Ensure we find these paramaters in the command
  // line arguments. Return false if any are missing.
  bool foundSandboxLevel = false;
  bool foundValidSandboxLevel = false;
  bool foundAppPath = false;

  // Read access directories used in testing
  int nTestingReadPaths = 0;
  std::string testingReadPaths[MAX_CONTENT_TESTING_READ_PATHS] = {};

  // Collect sandbox params from CLI arguments
  for (int i = 0; i < aArgc; i++) {
    if ((strcmp(aArgv[i], "-sbLevel") == 0) && (i + 1 < aArgc)) {
      std::stringstream ss(aArgv[i + 1]);
      int level = 0;
      ss >> level;
      foundSandboxLevel = true;
      aInfo.level = level;
      foundValidSandboxLevel = level > 0 && level <= 3 ? true : false;
      if (!foundValidSandboxLevel) {
        break;
      }
      i++;
      continue;
    }

    if (strcmp(aArgv[i], "-sbLogging") == 0) {
      aInfo.shouldLog = true;
      continue;
    }

    if (strcmp(aArgv[i], "-sbAllowFileAccess") == 0) {
      aInfo.hasFilePrivileges = true;
      continue;
    }

    if (strcmp(aArgv[i], "-sbAllowAudio") == 0) {
      aInfo.hasAudio = true;
      continue;
    }

    if (strcmp(aArgv[i], "-sbAllowWindowServer") == 0) {
      aInfo.hasWindowServer = true;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbAppPath") == 0) && (i + 1 < aArgc)) {
      foundAppPath = true;
      aInfo.appPath.assign(aArgv[i + 1]);
      i++;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbTestingReadPath") == 0) && (i + 1 < aArgc)) {
      if (nTestingReadPaths >= MAX_CONTENT_TESTING_READ_PATHS) {
        MOZ_CRASH("Too many content process -sbTestingReadPath arguments");
      }
      testingReadPaths[nTestingReadPaths] = aArgv[i + 1];
      nTestingReadPaths++;
      i++;
      continue;
    }

    if ((strcmp(aArgv[i], "-profile") == 0) && (i + 1 < aArgc)) {
      aInfo.hasSandboxedProfile = true;
      aInfo.profileDir.assign(aArgv[i + 1]);
      i++;
      continue;
    }

#ifdef DEBUG
    if ((strcmp(aArgv[i], "-sbDebugWriteDir") == 0) && (i + 1 < aArgc)) {
      aInfo.debugWriteDir.assign(aArgv[i + 1]);
      i++;
      continue;
    }
#endif  // DEBUG

    // Handle crash server positional argument
    if (strstr(aArgv[i], "gecko-crash-server-pipe") != nullptr) {
      aInfo.crashServerPort.assign(aArgv[i]);
      continue;
    }
  }

  if (!foundSandboxLevel) {
    fprintf(stderr, "Content sandbox disabled due to "
                    "missing sandbox CLI level parameter.\n");
    return false;
  }

  if (!foundValidSandboxLevel) {
    fprintf(stderr,
            "Content sandbox disabled due to invalid"
            "sandbox level (%d)\n",
            aInfo.level);
    return false;
  }

  if (!foundAppPath) {
    fprintf(stderr, "Content sandbox disabled due to "
                    "missing sandbox CLI app path parameter.\n");
    return false;
  }

  aInfo.testingReadPath1 = testingReadPaths[0];
  aInfo.testingReadPath2 = testingReadPaths[1];
  aInfo.testingReadPath3 = testingReadPaths[2];
  aInfo.testingReadPath4 = testingReadPaths[3];

  return true;
}

bool GetAppPathForExecutable(const char* aAppName, const char* aExecutablePath,
                             std::string& aAppPath) {
  std::string execPath(aExecutablePath);
  std::string appName(aAppName);
  size_t pos = execPath.rfind(appName + '/');
  if (pos == std::string::npos) {
    return false;
  }
  aAppPath = execPath.substr(0, pos + appName.size());
  return true;
}

bool GetUtilitySandboxParamsFromArgs(int aArgc, char** aArgv,
                                     MacSandboxInfo& aInfo,
                                     bool aSandboxingKindRequired = true) {
  // Ensure we find these paramaters in the command
  // line arguments. Return false if any are missing.
  bool foundAppPath = false;

  GetAppPathForExecutable(MOZ_CHILD_PROCESS_BUNDLENAME, aArgv[0],
                          aInfo.appBinaryPath);

  // Collect sandbox params from CLI arguments
  for (int i = 0; i < aArgc; i++) {
    if (strcmp(aArgv[i], "-sbLogging") == 0) {
      aInfo.shouldLog = true;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbAppPath") == 0) && (i + 1 < aArgc)) {
      foundAppPath = true;
      aInfo.appPath.assign(aArgv[i + 1]);
      i++;
      continue;
    }

    // Handle crash server positional argument
    if (strstr(aArgv[i], "gecko-crash-server-pipe") != nullptr) {
      aInfo.crashServerPort.assign(aArgv[i]);
      continue;
    }
  }

  if (aSandboxingKindRequired) {
    Maybe<uint64_t> sandboxingKind =
        geckoargs::sSandboxingKind.Get(aArgc, aArgv, CheckArgFlag::None);
    if (sandboxingKind.isNothing()) {
      fprintf(stderr, "Utility sandbox requires a sandboxingKind");
      return false;
    }
    aInfo.utilityKind = (ipc::SandboxingKind)*sandboxingKind;
  }

  if (!foundAppPath) {
    fprintf(stderr, "Utility sandbox disabled due to "
                    "missing sandbox CLI app path parameter.\n");
    return false;
  }

  return true;
}

bool GetSocketSandboxParamsFromArgs(int aArgc, char** aArgv,
                                    MacSandboxInfo& aInfo) {
  return GetUtilitySandboxParamsFromArgs(aArgc, aArgv, aInfo, false);
}

bool GetPluginSandboxParamsFromArgs(int aArgc, char** aArgv,
                                    MacSandboxInfo& aInfo) {
  // Ensure we find these paramaters in the command
  // line arguments. Return false if any are missing.
  bool foundAppPath = false;
  bool foundPluginPath = false;

  // Read access directories used in testing
  int nTestingReadPaths = 0;
  std::string testingReadPaths[MAX_GMP_TESTING_READ_PATHS] = {};

  // Collect sandbox params from CLI arguments
  for (int i = 0; i < aArgc; i++) {
    if (strcmp(aArgv[i], "-sbLogging") == 0) {
      aInfo.shouldLog = true;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbAppPath") == 0) && (i + 1 < aArgc)) {
      foundAppPath = true;
      aInfo.appPath.assign(aArgv[i + 1]);
      i++;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbPluginPath") == 0) && (i + 1 < aArgc)) {
      foundPluginPath = true;
      aInfo.pluginPath.assign(aArgv[i + 1]);
      i++;
      continue;
    }

    if (strcmp(aArgv[i], "-sbAllowWindowServer") == 0) {
      aInfo.hasWindowServer = true;
      continue;
    }

    if ((strcmp(aArgv[i], "-sbTestingReadPath") == 0) && (i + 1 < aArgc)) {
      if (nTestingReadPaths >= MAX_GMP_TESTING_READ_PATHS) {
        MOZ_CRASH("Too many GMP process -sbTestingReadPath arguments");
      }
      testingReadPaths[nTestingReadPaths] = aArgv[i + 1];
      nTestingReadPaths++;
      i++;
      continue;
    }

    // Handle crash server positional argument
    if (strstr(aArgv[i], "gecko-crash-server-pipe") != nullptr) {
      aInfo.crashServerPort.assign(aArgv[i]);
      continue;
    }
  }

  if (!foundPluginPath) {
    fprintf(stderr, "GMP sandbox disabled due to "
                    "missing sandbox CLI plugin path parameter.\n");
    return false;
  }

  if (!foundAppPath) {
    fprintf(stderr, "GMP sandbox disabled due to "
                    "missing sandbox CLI app path parameter.\n");
    return false;
  }

  aInfo.testingReadPath1 = testingReadPaths[0];
  aInfo.testingReadPath2 = testingReadPaths[1];

  return true;
}

bool GetRDDSandboxParamsFromArgs(int aArgc, char** aArgv,
                                 MacSandboxInfo& aInfo) {
  return GetUtilitySandboxParamsFromArgs(aArgc, aArgv, aInfo, false);
}

bool GetGPUSandboxParamsFromArgs(int aArgc, char** aArgv,
                                 MacSandboxInfo& aInfo) {
  return GetUtilitySandboxParamsFromArgs(aArgc, aArgv, aInfo, false);
}

/*
 * Returns true if no errors were encountered or if early sandbox startup is
 * not enabled for this process. Returns false if an error was encountered.
 */
bool StartMacSandboxIfEnabled(const MacSandboxType aSandboxType, int aArgc,
                              char** aArgv, std::string& aErrorMessage) {
  bool earlyStartupEnabled = false;

  // Check for the -sbStartup CLI parameter which
  // indicates we should start the sandbox now.
  for (int i = 0; i < aArgc; i++) {
    if (strcmp(aArgv[i], "-sbStartup") == 0) {
      earlyStartupEnabled = true;
      break;
    }
  }

  // The sandbox will be started later when/if parent
  // sends the sandbox startup message. Return true
  // indicating no errors occurred.
  if (!earlyStartupEnabled) {
    return true;
  }

  MacSandboxInfo info;
  info.type = aSandboxType;

  // For now, early start is only implemented
  // for content and utility sandbox types.
  switch (aSandboxType) {
    case MacSandboxType_Content:
      if (!GetContentSandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    case MacSandboxType_GMP:
      if (!GetPluginSandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    case MacSandboxType_GPU:
      if (!GetGPUSandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    case MacSandboxType_RDD:
      if (!GetRDDSandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    case MacSandboxType_Socket:
      if (!GetSocketSandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    case MacSandboxType_Utility:
      if (!GetUtilitySandboxParamsFromArgs(aArgc, aArgv, info)) {
        return false;
      }
      break;
    default:
      MOZ_RELEASE_ASSERT(false);
      break;
  }

  return StartMacSandbox(info, aErrorMessage);
}

bool IsMacSandboxStarted() { return sandbox_check(getpid(), nullptr, 0) == 1; }

#ifdef DEBUG
// sandbox_check returns 1 if the specified process is sandboxed
void AssertMacSandboxEnabled() {
  MOZ_ASSERT(sandbox_check(getpid(), nullptr, 0) == 1);
}
#endif /* DEBUG */

}  // namespace mozilla
