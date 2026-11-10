/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_
#define SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_

#include "SandboxTestingChild.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "nsXULAppAPI.h"

#ifdef XP_UNIX
#  include <fcntl.h>
#  include <netdb.h>
#  ifdef XP_LINUX
#    include <arpa/inet.h>
#    include <linux/memfd.h>
#    include <linux/mempolicy.h>
#    include <linux/mman.h>
#    include <sched.h>
#    include <sys/ioctl.h>
#    include <sys/mman.h>
#    include <sys/prctl.h>
#    include <sys/resource.h>
#    include <sys/socket.h>
#    include <sys/statfs.h>
#    include <sys/syscall.h>
#    include <sys/sysmacros.h>
#    include <sys/time.h>
#    include <sys/un.h>
#    include <sys/utsname.h>
#    include <termios.h>

#    include "mozilla/ProcInfo_linux.h"
#    ifdef MOZ_X11
#      include "X11/Xlib.h"
#      include "X11UndefineNone.h"
#    endif  // MOZ_X11
#  endif    // XP_LINUX
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>
#endif

#ifdef XP_MACOSX
#  include "mozilla/Sandbox.h"
#  include "mozilla/StaticPrefs_webgl.h"
#  include "mozilla/gfx/gfxVars.h"
#  if defined(__SSE2__) || defined(_M_X64) || \
      (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    include "emmintrin.h"
#  endif
#  include <AudioToolbox/AudioToolbox.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <CoreGraphics/CoreGraphics.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <limits.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/sysctl.h>
#  include <unistd.h>
namespace ApplicationServices {
#  include <ApplicationServices/ApplicationServices.h>
}
extern "C" int sandbox_check(pid_t pid, const char* operation, int type, ...);
#endif

#ifdef XP_WIN
#  include <stdio.h>
#  include <winternl.h>

#  include "mozilla/DynamicallyLinkedFunctionPtr.h"
#  include "mozilla/WindowsProcessMitigations.h"
#  include "nsAppDirectoryServiceDefs.h"
#endif

#ifdef XP_LINUX
// Defined in <linux/watch_queue.h> which was added in 5.8
#  ifndef O_NOTIFICATION_PIPE
#    define O_NOTIFICATION_PIPE O_EXCL
#  endif
// Added in 5.7.
#  ifndef MREMAP_DONTUNMAP
#    define MREMAP_DONTUNMAP 4
#  endif
// Added in 4.14.
#  ifndef MFD_HUGETLB
#    define MFD_HUGETLB 4U
#    define MFD_HUGE_2MB (21U << 26)
#  endif
// (MAP_HUGE_* is from 3.8.  MAP_HUGETLB is 2.6.32.)
//
// This constant is ancient, but the kernel header for it conflicts
// with glibc's fcntl.h:
#  ifndef F_LINUX_SPECIFIC_BASE
#    define F_LINUX_SPECIFIC_BASE 1024
#  endif
// Added in 6.10:
#  ifndef F_DUPFD_QUERY
#    define F_DUPFD_QUERY (F_LINUX_SPECIFIC_BASE + 3)
#  endif
#endif

constexpr bool kIsDebug =
#ifdef DEBUG
    true;
#else
    false;
#endif

namespace mozilla {

#ifdef XP_LINUX
static void RunTestsSched(SandboxTestingChild* child) {
  struct sched_param param_pid_0 = {};
  child->ErrnoTest("sched_getparam(0)"_ns, true,
                   [&] { return sched_getparam(0, &param_pid_0); });

  struct sched_param param_pid_tid = {};
  child->ErrnoTest("sched_getparam(tid)"_ns, true, [&] {
    return sched_getparam((pid_t)syscall(__NR_gettid), &param_pid_tid);
  });

  struct sched_param param_pid_Ntid = {};
  child->ErrnoValueTest("sched_getparam(Ntid)"_ns, EPERM, [&] {
    return sched_getparam((pid_t)(syscall(__NR_gettid) - 1), &param_pid_Ntid);
  });
}
#endif

// Tests that apply to every process type (more or less)
static void RunGenericTests(SandboxTestingChild* child, bool aIsGMP = false) {
#ifdef XP_LINUX
  // Check ABI issues with 32-bit arguments on 64-bit platforms.
  if (sizeof(void*) == 8) {
    static constexpr uint64_t kHighBits = 0xDEADBEEF00000000;

    struct timespec ts0, ts1;
    child->ErrnoTest("high_bits_gettime"_ns, true, [&] {
      return syscall(__NR_clock_gettime, kHighBits | CLOCK_MONOTONIC, &ts0);
    });
    // Try to make sure we got the correct clock by reading it again and
    // comparing to see if the times are vaguely similar.
    int rv = clock_gettime(CLOCK_MONOTONIC, &ts1);
    MOZ_RELEASE_ASSERT(rv == 0);
    MOZ_RELEASE_ASSERT(ts0.tv_sec <= ts1.tv_sec + 1);
    MOZ_RELEASE_ASSERT(ts1.tv_sec <= ts0.tv_sec + 60);

    // Check some non-zeroth arguments.  (fcntl is convenient for
    // this, but GMP has a stricter policy, so skip it there.)
    if (!aIsGMP) {
      int flags;
      child->ErrnoTest("high_bits_fcntl_getfl"_ns, true, [&] {
        flags = syscall(__NR_fcntl, 0, kHighBits | F_GETFL);
        return flags;
      });
      MOZ_RELEASE_ASSERT(flags == fcntl(0, F_GETFL));

      int fds[2];
      rv = pipe(fds);
      MOZ_RELEASE_ASSERT(rv >= 0);
      child->ErrnoTest("high_bits_fcntl_setfl"_ns, true, [&] {
        return syscall(__NR_fcntl, fds[0], kHighBits | F_SETFL,
                       kHighBits | O_NONBLOCK);
      });
      flags = fcntl(fds[0], F_GETFL);
      MOZ_RELEASE_ASSERT(flags >= 0);
      MOZ_RELEASE_ASSERT(flags & O_NONBLOCK);
    }
  }

  if (!aIsGMP) {
    constexpr auto name = "fcntl_dupfd_query"_ns;
    int rv = fcntl(0, F_DUPFD_QUERY, 0);
    // Expected:
    // * success with rv == 1 (new kernel)
    // * failure with EINVAL (old kernel)
    // Rejected:
    // * failure with ENOSYS or any other error
    // * success with rv == 0 (shouldn't be possible)
    MOZ_RELEASE_ASSERT(rv != 0);
    if (rv > 0) {
      child->PosixTest(name, true, 0);
    } else {  // (rv < 0), errno unchanged since fcntl
      child->PosixTest(name, false, errno, Some(EINVAL));
    }
  }
#endif  // XP_LINUX
}

#ifdef XP_WIN
/**
 * Uses NtCreateFile directly to test file system brokering.
 *
 */
static void FileTest(const nsCString& aName, const char* aSpecialDirName,
                     const nsString& aRelativeFilePath, ACCESS_MASK aAccess,
                     bool aExpectSuccess, SandboxTestingChild* aChild) {
  static const StaticDynamicallyLinkedFunctionPtr<decltype(&NtCreateFile)>
      pNtCreateFile(L"ntdll.dll", "NtCreateFile");
  static const StaticDynamicallyLinkedFunctionPtr<decltype(&NtClose)> pNtClose(
      L"ntdll.dll", "NtClose");

  // Start the filename with the NT namespace
  nsString testFilename(u"\\??\\"_ns);
  nsString dirPath;
  aChild->SendGetSpecialDirectory(nsDependentCString(aSpecialDirName),
                                  &dirPath);
  testFilename.Append(dirPath);
  testFilename.AppendLiteral("\\");
  testFilename.Append(aRelativeFilePath);

  UNICODE_STRING uniFileName;
  ::RtlInitUnicodeString(&uniFileName, testFilename.get());

  OBJECT_ATTRIBUTES objectAttributes;
  InitializeObjectAttributes(&objectAttributes, &uniFileName,
                             OBJ_CASE_INSENSITIVE, nullptr, nullptr);

  HANDLE fileHandle = INVALID_HANDLE_VALUE;
  IO_STATUS_BLOCK ioStatusBlock = {};

  ULONG createOptions = StringEndsWith(testFilename, u"\\"_ns) ||
                                StringEndsWith(testFilename, u"/"_ns)
                            ? FILE_DIRECTORY_FILE
                            : FILE_NON_DIRECTORY_FILE;
  NTSTATUS status = pNtCreateFile(
      &fileHandle, aAccess, &objectAttributes, &ioStatusBlock, nullptr, 0, 0,
      FILE_OPEN_IF, createOptions | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);

  if (fileHandle != INVALID_HANDLE_VALUE) {
    pNtClose(fileHandle);
  }

  nsCString accessString;
  if ((aAccess & FILE_GENERIC_READ) == FILE_GENERIC_READ) {
    accessString.AppendLiteral("r");
  }
  if ((aAccess & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE) {
    accessString.AppendLiteral("w");
  }
  if ((aAccess & FILE_GENERIC_EXECUTE) == FILE_GENERIC_EXECUTE) {
    accessString.AppendLiteral("e");
  }

  nsCString msgRelPath = NS_ConvertUTF16toUTF8(aRelativeFilePath);
  for (size_t i = 0, j = 0; i < aRelativeFilePath.Length(); ++i, ++j) {
    if (aRelativeFilePath[i] == u'\\') {
      msgRelPath.Insert('\\', j++);
    }
  }

  nsCString message;
  message.AppendPrintf(
      "Special dir: %s, file: %s, access: %s , returned status: %lx",
      aSpecialDirName, msgRelPath.get(), accessString.get(), status);

  aChild->SendReportTestResults(aName, aExpectSuccess == NT_SUCCESS(status),
                                message);
}
#endif

#ifdef XP_MACOSX
/*
 * Test if this process can launch another process with posix_spawnp,
 * exec, and LSOpenCFURLRef. All launches are expected to fail. Only the
 * failure itself is checked, not the error that is returned: the error
 * depends on which sandbox rule stops the launch first, and that differs
 * between process types, macOS versions and CPU architectures.
 */
void RunMacTestLaunchProcess(SandboxTestingChild* child) {
  // Test that posix_spawnp fails
  char* argv[2];
  argv[0] = const_cast<char*>("bash");
  argv[1] = NULL;
  int rv = posix_spawnp(NULL, "/bin/bash", NULL, NULL, argv, NULL);
  nsPrintfCString posixSpawnMessage(
      "posix_spawnp returned %d, expected a non-zero error", rv);
  child->SendReportTestResults("posix_spawnp test"_ns, rv != 0,
                               posixSpawnMessage);

  // Test that exec fails
  child->ErrnoTest("execv /bin/bash test"_ns, false, [&] {
    char* argvp = NULL;
    return execv("/bin/bash", &argvp);
  });

  // Test that launching an application using LSOpenCFURLRef fails
  char* uri = const_cast<char*>("/System/Applications/Utilities/Console.app");
  CFStringRef filePath = ::CFStringCreateWithCString(kCFAllocatorDefault, uri,
                                                     kCFStringEncodingUTF8);
  CFURLRef urlRef = ::CFURLCreateWithFileSystemPath(
      kCFAllocatorDefault, filePath, kCFURLPOSIXPathStyle, false);
  ::CFRelease(filePath);
  if (!urlRef) {
    child->SendReportTestResults("LSOpenCFURLRef"_ns, false,
                                 "CFURLCreateWithFileSystemPath failed"_ns);
    return;
  }

  OSStatus status = ApplicationServices::LSOpenCFURLRef(urlRef, NULL);
  ::CFRelease(urlRef);
  nsPrintfCString lsMessage(
      "LSOpenCFURLRef returned %d, expected a non-zero error", status);
  child->SendReportTestResults("LSOpenCFURLRef"_ns, status != noErr, lsMessage);
}

/*
 * Test if this process can connect to the macOS window server.
 * When |aShouldHaveAccess| is true, the test passes if access is __permitted__.
 * When |aShouldHaveAccess| is false, the test passes if access is __blocked__.
 */
void RunMacTestWindowServer(SandboxTestingChild* child,
                            bool aShouldHaveAccess = false) {
  // CGSessionCopyCurrentDictionary() returns NULL when a
  // connection to the window server is not available.
  CFDictionaryRef windowServerDict = CGSessionCopyCurrentDictionary();
  bool gotWindowServerDetails = (windowServerDict != nullptr);
  bool testPassed = (gotWindowServerDetails == aShouldHaveAccess);
  child->SendReportTestResults(
      "CGSessionCopyCurrentDictionary"_ns, testPassed,
      gotWindowServerDetails
          ? "dictionary returned, access is permitted"_ns
          : "no dictionary returned, access appears blocked"_ns);
  if (windowServerDict != nullptr) {
    CFRelease(windowServerDict);
  }
}

/*
 * Test if this process can get access to audio components on macOS.
 * When |aShouldHaveAccess| is true, the test passes if access is __permitted__.
 * When |aShouldHaveAccess| is false, the test passes if access is __blocked__.
 */
void RunMacTestAudioAPI(SandboxTestingChild* child,
                        bool aShouldHaveAccess = false) {
  AudioStreamBasicDescription inputFormat;
  inputFormat.mFormatID = kAudioFormatMPEG4AAC;
  inputFormat.mSampleRate = 48000.0;
  inputFormat.mChannelsPerFrame = 2;
  inputFormat.mBitsPerChannel = 0;
  inputFormat.mFormatFlags = 0;
  inputFormat.mFramesPerPacket = 1024;
  inputFormat.mBytesPerPacket = 0;

  UInt32 inputFormatSize = sizeof(inputFormat);
  OSStatus status = AudioFormatGetProperty(
      kAudioFormatProperty_FormatInfo, 0, NULL, &inputFormatSize, &inputFormat);

  bool gotAudioFormat = (status == 0);
  bool testPassed = (gotAudioFormat == aShouldHaveAccess);
  child->SendReportTestResults(
      "AudioFormatGetProperty"_ns, testPassed,
      gotAudioFormat ? "got audio format, access is permitted"_ns
                     : "no audio format, access appears blocked"_ns);
}
#endif /* XP_MACOSX */

#ifdef XP_WIN
void RunWinTestWin32k(SandboxTestingChild* child,
                      bool aShouldHaveAccess = true) {
  bool isLockedDown = (IsWin32kLockedDown() == true);
  bool testPassed = (isLockedDown == aShouldHaveAccess);
  child->SendReportTestResults(
      "Win32kLockdown"_ns, testPassed,
      isLockedDown ? "got lockdown, access is blocked"_ns
                   : "no lockdown, access appears permitted"_ns);
}
#endif  // XP_WIN

void RunTestsContent(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

#ifdef XP_UNIX
  struct stat st;
  static const char kAllowedPath[] = "/usr/lib";

  child->ErrnoTest("fstatat_as_stat"_ns, true,
                   [&] { return fstatat(AT_FDCWD, kAllowedPath, &st, 0); });
  child->ErrnoTest("fstatat_as_lstat"_ns, true, [&] {
    return fstatat(AT_FDCWD, kAllowedPath, &st, AT_SYMLINK_NOFOLLOW);
  });

#  ifdef XP_LINUX
  child->ErrnoTest("fstatat_as_fstat"_ns, true,
                   [&] { return fstatat(0, "", &st, AT_EMPTY_PATH); });

  const struct timespec usec = {0, 1000};
  child->ErrnoTest("nanosleep"_ns, true,
                   [&] { return nanosleep(&usec, nullptr); });

  struct timespec res = {0, 0};
  child->ErrnoTest("clock_getres"_ns, true,
                   [&] { return clock_getres(CLOCK_REALTIME, &res); });

  // same process is allowed
  struct timespec tproc = {0, 0};
  clockid_t same_process = MAKE_PROCESS_CPUCLOCK(getpid(), CPUCLOCK_SCHED);
  child->ErrnoTest("clock_gettime_same_process"_ns, true,
                   [&] { return clock_gettime(same_process, &tproc); });

  // different process is blocked by sandbox (SIGSYS, kernel would return
  // EINVAL)
  struct timespec tprocd = {0, 0};
  clockid_t diff_process = MAKE_PROCESS_CPUCLOCK(1, CPUCLOCK_SCHED);
  child->ErrnoValueTest("clock_gettime_diff_process"_ns, ENOSYS,
                        [&] { return clock_gettime(diff_process, &tprocd); });

  // thread is allowed
  struct timespec tthread = {0, 0};
  clockid_t thread =
      MAKE_THREAD_CPUCLOCK((pid_t)syscall(__NR_gettid), CPUCLOCK_SCHED);
  child->ErrnoTest("clock_gettime_thread"_ns, true,
                   [&] { return clock_gettime(thread, &tthread); });

  // getcpu is allowed
  // We're using syscall directly because:
  // - sched_getcpu uses vdso and as a result doesn't go through the sandbox.
  // - getcpu isn't defined in the header files we're using yet.
  int c;
  child->ErrnoTest("getcpu"_ns, true,
                   [&] { return syscall(SYS_getcpu, &c, NULL, NULL); });

  // An abstract socket that does not starts with '/', so we don't want it to
  // work.
  // Checking ENETUNREACH should be thrown by SandboxBrokerClient::Connect()
  // when it detects it does not starts with a '/'
  child->ErrnoValueTest("connect_abstract_blocked"_ns, ENETUNREACH, [&] {
    int sockfd;
    struct sockaddr_un addr;
    char str[] = "\0xyz";  // Abstract socket requires first byte to be NULL
    size_t str_size = 4;

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    memcpy(&addr.sun_path, str, str_size);

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
      return -1;
    }

    int con_st = connect(sockfd, (struct sockaddr*)&addr,
                         sizeof(sa_family_t) + str_size);
    return con_st;
  });

  // An abstract socket that does starts with /, so we do want it to work.
  //
  // Normally, this will be passed to the broker (unlike the previous
  // test) and rejected, failing with EACCES.
  //
  // With content sandbox level <5, the expected error is ECONNREFUSED
  // (the broker tries to connect but it fails at the OS level);
  // however, these tests don't handle non-default pref settings.
  child->ErrnoValueTest("connect_abstract_permit"_ns, EACCES, [&] {
    int sockfd;
    struct sockaddr_un addr;
    // we re-use actual X path, because this is what is allowed within
    // SandboxBrokerPolicyFactory::InitContentPolicy()
    // We can't just use any random path allowed, but one with CONNECT allowed.

    // (Note that the real X11 sockets have names like `X0` for
    // display `:0`; there shouldn't be anything named just `X`.)

    // Abstract socket requires first byte to be NULL
    char str[] = "\0/tmp/.X11-unix/X";
    size_t str_size = 17;

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    memcpy(&addr.sun_path, str, str_size);

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
      return -1;
    }

    int con_st = connect(sockfd, (struct sockaddr*)&addr,
                         sizeof(sa_family_t) + str_size);
    return con_st;
  });

  // Testing FIPS-relevant files, which need to be accessible
  std::vector<std::pair<const char*, bool>> open_tests = {
      {"/dev/random", true}};
  // Not all systems have that file, so we only test access, if it exists
  // in the first place
  if (stat("/proc/sys/crypto/fips_enabled", &st) == 0) {
    open_tests.push_back({"/proc/sys/crypto/fips_enabled", true});
  }

  for (const std::pair<const char*, bool>& to_open : open_tests) {
    child->ErrnoTest("open("_ns + nsCString(to_open.first) + ")"_ns,
                     to_open.second, [&] {
                       int fd = open(to_open.first, O_RDONLY);
                       if (to_open.second && fd > 0) {
                         close(fd);
                       }
                       return fd;
                     });
  }

  child->ErrnoTest("statfs"_ns, true, [] {
    struct statfs sf;
    return statfs("/usr/share", &sf);
  });

  child->ErrnoTest("pipe2"_ns, true, [] {
    int fds[2];
    int rv = pipe2(fds, O_CLOEXEC);
    int savedErrno = errno;
    if (rv == 0) {
      close(fds[0]);
      close(fds[1]);
    }
    errno = savedErrno;
    return rv;
  });

  child->ErrnoValueTest("chroot"_ns, ENOSYS, [] { return chroot("/"); });

  child->ErrnoValueTest("pipe2_notif"_ns, ENOSYS, [] {
    int fds[2];
    return pipe2(fds, O_NOTIFICATION_PIPE);
  });

#    ifdef MOZ_X11
  // Check that X11 access is blocked (bug 1129492).
  // This will fail if security.sandbox.content.level is less than 5.
  if (PR_GetEnv("DISPLAY")) {
    Display* disp = XOpenDisplay(nullptr);

    child->SendReportTestResults(
        "x11_access"_ns, !disp,
        disp ? "XOpenDisplay succeeded"_ns : "XOpenDisplay failed"_ns);
    if (disp) {
      XCloseDisplay(disp);
    }
  }
#    endif  // MOZ_X11

  child->ErrnoTest("realpath localtime"_ns, true, [] {
    char buf[PATH_MAX];
    return realpath("/etc/localtime", buf) ? 0 : -1;
  });

  // Check that readlink truncates results longer than the buffer
  // (rather than failing) and returns the total number of bytes
  // actually written (not the size of the link or anything else).
  {
    char buf;
    ssize_t rv = readlink("/etc/localtime", &buf, 1);
    int err = errno;
    if (rv == 1) {
      child->SendReportTestResults("readlink truncate"_ns, true,
                                   "expected 1, got 1"_ns);
    } else if (rv < 0) {
      nsPrintfCString msg("expected 1, got error: %s", strerror(err));
      child->SendReportTestResults("readlink truncate"_ns, false, msg);
    } else {
      nsPrintfCString msg("expected 1, got %zd", rv);
      child->SendReportTestResults("readlink truncate"_ns, false, msg);
    }
  }

  {
    static constexpr size_t kMapSize = 65536;
    void* mapping = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    MOZ_ASSERT(mapping != MAP_FAILED);
    child->ErrnoTest("mremap-zero"_ns, true, [&] {
      void* rv = mremap(mapping, kMapSize, kMapSize, 0);
      if (rv == MAP_FAILED) {
        return -1;
      }
      MOZ_ASSERT(rv == mapping);
      return 0;
    });

    child->ErrnoValueTest("mremap-forbidden"_ns, ENOSYS, [&] {
      void* rv = mremap(mapping, kMapSize, kMapSize, MREMAP_DONTUNMAP);
      // This is an invalid flag combination (DONTUNMAP requires
      // MAYMOVE) so it will always fail with *something*.
      MOZ_ASSERT(rv == MAP_FAILED);
      return -1;
    });

    munmap(mapping, kMapSize);
  }

  child->ErrnoValueTest("ioctl_dma_buf"_ns, ENOSYS, [] {
    // Attempt an arbitrary non-tty ioctl, on the wrong type of fd; if
    // allowed it would fail with ENOTTY (see the RDD tests) but in
    // this sandbox it should be blocked (ENOSYS).
    return ioctl(0, _IOW('b', 0, uint64_t), nullptr);
  });

  child->ErrnoValueTest("send_with_flag"_ns, ENOSYS, [] {
    char c = 0;
    return send(0, &c, 1, MSG_CONFIRM);
  });

  child->ErrnoValueTest("mmap_huge"_ns, ENOSYS, [] {
    void* ptr =
        mmap(nullptr, 1 << 21, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
    return ptr == MAP_FAILED ? -1 : 0;
  });

  child->ErrnoValueTest("memfd_huge"_ns, ENOSYS, [] {
    int fd = syscall(__NR_memfd_create, "hugemem",
                     MFD_CLOEXEC | MFD_HUGETLB | MFD_HUGE_2MB);
    if (fd >= 0) {
      close(fd);
    }
    // Returning a closed fd is fine; it's just going to be tested for >= 0.
    return fd;
  });

  child->ErrnoValueTest("symlink"_ns, EPERM,
                        [] { return symlink("something", "/tmp/testlink"); });
  child->ErrnoValueTest("symlinkat"_ns, EPERM, [] {
    return symlinkat("something", AT_FDCWD, "/tmp/testlink");
  });

#  endif  // XP_LINUX

#  ifdef XP_MACOSX
  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child, true);
#  endif

#elif XP_WIN
  FileTest("read from chrome"_ns, NS_APP_USER_CHROME_DIR, u"sandboxTest.txt"_ns,
           FILE_GENERIC_READ, true, child);
  FileTest("read from profile via relative path"_ns, NS_APP_USER_CHROME_DIR,
           u"..\\sandboxTest.txt"_ns, FILE_GENERIC_READ, false, child);
  // The profile dir is the parent of the chrome dir.
  FileTest("read from chrome using forward slash"_ns,
           NS_APP_USER_PROFILE_50_DIR, u"chrome/sandboxTest.txt"_ns,
           FILE_GENERIC_READ, false, child);

  // Note: these only pass in DEBUG builds because we allow write access to the
  // temp dir for certain test logs and that is where the profile is created.
  FileTest("read from profile"_ns, NS_APP_USER_PROFILE_50_DIR,
           u"sandboxTest.txt"_ns, FILE_GENERIC_READ, kIsDebug, child);
  FileTest("read/write from chrome"_ns, NS_APP_USER_CHROME_DIR,
           u"sandboxTest.txt"_ns, FILE_GENERIC_READ | FILE_GENERIC_WRITE,
           kIsDebug, child);
#else
    child->ReportNoTests();
#endif
}

void RunTestsRDD(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

#ifdef XP_UNIX
#  ifdef XP_LINUX
  child->ErrnoValueTest("ioctl_tiocsti"_ns, ENOSYS, [&] {
    int rv = ioctl(1, TIOCSTI, "x");
    return rv;
  });

  struct rusage res = {};
  child->ErrnoTest("getrusage"_ns, true, [&] {
    int rv = getrusage(RUSAGE_SELF, &res);
    return rv;
  });

  child->ErrnoValueTest("unlink"_ns, ENOENT, [&] {
    int rv = unlink("");
    return rv;
  });

  child->ErrnoValueTest("unlinkat"_ns, ENOENT, [&] {
    int rv = unlinkat(AT_FDCWD, "", 0);
    return rv;
  });

  RunTestsSched(child);

#    ifdef MOZ_ENABLE_VULKAN_VIDEO
  // Vulkan video decode (bug 2021722) routes socket() through
  // FakeSocketTrap, letting RDD create a real AF_UNIX socket for EGL's
  // Wayland/X11 probing (hence the "0" below, meaning success) while
  // still rejecting AF_INET, now with EAFNOSUPPORT instead of EACCES.
  child->ErrnoValueTest("socket_inet"_ns, EAFNOSUPPORT,
                        [] { return socket(AF_INET, SOCK_STREAM, 0); });
  child->ErrnoValueTest("socket_unix"_ns, 0,
                        [] { return socket(AF_UNIX, SOCK_STREAM, 0); });
#    else
  child->ErrnoValueTest("socket_inet"_ns, EACCES,
                        [] { return socket(AF_INET, SOCK_STREAM, 0); });

  child->ErrnoValueTest("socket_unix"_ns, EACCES,
                        [] { return socket(AF_UNIX, SOCK_STREAM, 0); });
#    endif

  child->ErrnoTest("uname"_ns, true, [] {
    struct utsname uts;
    return uname(&uts);
  });

  child->ErrnoValueTest("ioctl_dma_buf"_ns, ENOTTY, [] {
    // Apply the ioctl to the wrong kind of fd; it should fail with
    // ENOTTY (rather than ENOSYS if it were blocked).
    return ioctl(0, _IOW('b', 0, uint64_t), nullptr);
  });

  // getcpu is allowed
  // We're using syscall directly because:
  // - sched_getcpu uses vdso and as a result doesn't go through the sandbox.
  // - getcpu isn't defined in the header files we're using yet.
  int c;
  child->ErrnoTest("getcpu"_ns, true,
                   [&] { return syscall(SYS_getcpu, &c, NULL, NULL); });

  // The nvidia proprietary drivers will, in some cases, try to
  // mknod their device files; we reject this politely.
  child->ErrnoValueTest("mknod"_ns, EPERM, [] {
    return mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3));
  });

  // Rust panics call getcwd to try to print relative paths in
  // backtraces.
  child->ErrnoValueTest("getcwd"_ns, ENOENT, [] {
    char buf[4096];
    return (getcwd(buf, sizeof(buf)) == nullptr) ? -1 : 0;
  });

  // nvidia defines some ioctls with the type 0x46 ('F', otherwise
  // used by fbdev) and numbers starting from 200 (0xc8).
  child->ErrnoValueTest("ioctl_nvidia"_ns, ENOTTY,
                        [] { return ioctl(0, 0x46c8, nullptr); });

  child->ErrnoTest("statfs"_ns, true, [] {
    struct statfs sf;
    return statfs("/usr/share", &sf);
  });

  child->ErrnoValueTest("fork"_ns, EPERM, [] {
    pid_t pid = fork();
    if (pid == 0) {
      // Success: shouldn't happen, and parent will report a test
      // failure.
      _exit(0);
    }
    return pid;
  });

#  elif XP_MACOSX
  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child, true);
#  endif
#else  // XP_UNIX
#  ifdef XP_WIN
  RunWinTestWin32k(child, false);
#  endif  // XP_WIN
  child->ReportNoTests();
#endif
}

void RunTestsGMPlugin(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child, /* aIsGMP = */ true);

#ifdef XP_UNIX
#  ifdef XP_LINUX
  struct utsname utsname_res = {};
  child->ErrnoTest("uname_restricted"_ns, true, [&] {
    int rv = uname(&utsname_res);

    nsCString expectedSysname("Linux"_ns);
    nsCString sysname(utsname_res.sysname);
    nsCString expectedVersion("3"_ns);
    nsCString version(utsname_res.version);
    if ((sysname != expectedSysname) || (version != expectedVersion)) {
      return -1;
    }

    return rv;
  });

  child->ErrnoTest("getuid"_ns, true, [&] { return getuid(); });
  child->ErrnoTest("getgid"_ns, true, [&] { return getgid(); });
  child->ErrnoTest("geteuid"_ns, true, [&] { return geteuid(); });
  child->ErrnoTest("getegid"_ns, true, [&] { return getegid(); });

  RunTestsSched(child);

  std::vector<std::pair<const char*, bool>> open_tests = {
      {"/etc/ld.so.cache", true},
      {"/proc/cpuinfo", true},
      {"/etc/hostname", false}};

  for (const std::pair<const char*, bool>& to_open : open_tests) {
    child->ErrnoTest("open("_ns + nsCString(to_open.first) + ")"_ns,
                     to_open.second, [&] {
                       int fd = open(to_open.first, O_RDONLY);
                       if (to_open.second && fd > 0) {
                         close(fd);
                       }
                       return fd;
                     });
  }

  child->ErrnoValueTest("readlink_exe"_ns, EINVAL, [] {
    char pathBuf[PATH_MAX];
    return readlink("/proc/self/exe", pathBuf, sizeof(pathBuf));
  });

  child->ErrnoTest("memfd_sizing"_ns, true, [] {
    int fd = syscall(__NR_memfd_create, "sandbox-test", 0);
    if (fd < 0) {
      if (errno == ENOSYS) {
        // Don't fail the test if the kernel is old.
        return 0;
      }
      return -1;
    }

    int rv = ftruncate(fd, 4096);
    int savedErrno = errno;
    close(fd);
    errno = savedErrno;
    return rv;
  });

  {
    static constexpr size_t kMapSize = 65536;
    void* mapping = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    MOZ_ASSERT(mapping != MAP_FAILED);

#    ifndef MOZ_MEMORY
    child->ErrnoTest("mremap-move"_ns, true, [&] {
      void* rv = mremap(mapping, kMapSize, kMapSize, MREMAP_MAYMOVE);
      if (rv == MAP_FAILED) {
        return -1;
      }
      // It *may* move the mapping, but when the size doesn't change
      // it's not expected to:
      MOZ_ASSERT(rv == mapping);
      return 0;
    });
#    endif

    child->ErrnoValueTest("mremap-forbidden"_ns, ENOSYS, [&] {
      void* rv = mremap(mapping, kMapSize, kMapSize, MREMAP_DONTUNMAP);
      // This is an invalid flag combination (DONTUNMAP requires
      // MAYMOVE) so it will always fail with *something*.
      MOZ_ASSERT(rv == MAP_FAILED);
      return -1;
    });

    munmap(mapping, kMapSize);
  }

#  elif XP_MACOSX  // XP_LINUX
  RunMacTestLaunchProcess(child);
  /* The Mac GMP process requires access to the window server */
  RunMacTestWindowServer(child, true /* aShouldHaveAccess */);
  RunMacTestAudioAPI(child);
#  endif           // XP_MACOSX
#else              // XP_UNIX
  child->ReportNoTests();
#endif
}

void RunTestsGenericUtility(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

#ifdef XP_UNIX
#  ifdef XP_LINUX
  child->ErrnoValueTest("ioctl_tiocsti"_ns, ENOSYS, [&] {
    int rv = ioctl(1, TIOCSTI, "x");
    return rv;
  });

  child->ErrnoTest("getrusage"_ns, true, [&] {
    struct rusage res;
    return getrusage(RUSAGE_SELF, &res);
  });

  child->ErrnoTest("uname"_ns, true, [&] {
    struct utsname uts;
    return uname(&uts);
  });

#  elif XP_MACOSX  // XP_LINUX
  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(child);
#  endif           // XP_MACOSX
#elif XP_WIN       // XP_UNIX
  child->ErrnoValueTest("write_only"_ns, EACCES, [&] {
    FILE* rv = fopen("test_sandbox.txt", "w");
    if (rv != nullptr) {
      fclose(rv);
      return 0;
    }
    return -1;
  });
  RunWinTestWin32k(child);
#else              // XP_UNIX
    child->ReportNoTests();
#endif             // XP_MACOSX
}

void RunTestsUtilityMediaService(SandboxTestingChild* child,
                                 ipc::SandboxingKind aSandbox) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

#ifdef XP_UNIX
#  ifdef XP_LINUX
  // getrusage is allowed in Generic Utility and on AudioDecoder
  struct rusage res;
  child->ErrnoTest("getrusage"_ns, true, [&] {
    int rv = getrusage(RUSAGE_SELF, &res);
    return rv;
  });

  // get_mempolicy is not allowed in Generic Utility but is on AudioDecoder
  child->ErrnoTest("get_mempolicy"_ns, true, [&] {
    int numa_node;
    int test_val = 0;
    // <numaif.h> not installed by default, let's call directly the syscall
    long rv = syscall(SYS_get_mempolicy, &numa_node, NULL, 0, (void*)&test_val,
                      MPOL_F_NODE | MPOL_F_ADDR);
    return rv;
  });
  // set_mempolicy is not allowed in Generic Utility but is on AudioDecoder
  child->ErrnoValueTest("set_mempolicy"_ns, ENOSYS, [&] {
    // <numaif.h> not installed by default, let's call directly the syscall
    long rv = syscall(SYS_set_mempolicy, 0, NULL, 0);
    return rv;
  });

  child->ErrnoValueTest("prctl_capbtest_read_blocked"_ns, EINVAL, [] {
    int rv = prctl(PR_CAPBSET_READ, 0, 0, 0, 0);
    return rv;
  });

#  elif XP_MACOSX  // XP_LINUX
  RunMacTestLaunchProcess(child);
  RunMacTestWindowServer(child);
  RunMacTestAudioAPI(
      child,
      aSandbox == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA);
#  endif           // XP_MACOSX
#else              // XP_UNIX
#  ifdef XP_WIN
  RunWinTestWin32k(child);
#  endif  // XP_WIN
  child->ReportNoTests();
#endif    // XP_UNIX
}

void RunTestsGPU(SandboxTestingChild* child) {
  MOZ_ASSERT(child, "No SandboxTestingChild*?");

  RunGenericTests(child);

#if defined(XP_WIN)

  FileTest("R/W access to shader-cache dir"_ns, NS_APP_USER_PROFILE_50_DIR,
           u"shader-cache\\"_ns, FILE_GENERIC_READ | FILE_GENERIC_WRITE, true,
           child);

  FileTest("R/W access to shader-cache files"_ns, NS_APP_USER_PROFILE_50_DIR,
           u"shader-cache\\sandboxTest.txt"_ns,
           FILE_GENERIC_READ | FILE_GENERIC_WRITE, true, child);

#elif defined(XP_MACOSX)

  // Check if the GPU process sandbox has been started
  bool isSandboxStarted = sandbox_check(getpid(), NULL, 0) == 1;
  nsCString gpuSandboxCheckMessage;
  if (isSandboxStarted) {
    gpuSandboxCheckMessage.AppendLiteral(
        "sandbox_check() indicates GPU process sandbox is running");
  } else {
    gpuSandboxCheckMessage.AppendLiteral(
        "sandbox_check() indicates GPU process sandbox is not running");
  }
  child->SendReportTestResults("sandbox_check()"_ns, isSandboxStarted,
                               gpuSandboxCheckMessage);

  // Home directory tests
  const char* home = getenv("HOME");
  if (home) {
    // Test write to home directory
    nsCString testFile(home);
    testFile.Append("/gpu_sbox_writetest.tmp");

    child->ErrnoTest("write denied ($HOME)"_ns, false,
                     [p = std::string(testFile.get())] {
                       int fd = open(p.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                       }
                       return fd;
                     });

    // Test reading from home directory - file already created by parent process
    nsCString testReadFile(home);
    testReadFile.Append("/.mozilla_gpu_sandbox_read_test");
    std::string path = std::string(testReadFile.get());
    if (access(path.c_str(), F_OK) == 0) {
      child->ErrnoTest("read denied (home test file)"_ns, false, [path] {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
          close(fd);
        }
        return fd;
      });
    } else {
      child->SendReportTestResults(
          "read denied (home test file)"_ns, false,
          "Test file does not exist, test setup failure"_ns);
    }
  } else {
    child->SendReportTestResults("HOME check"_ns, false,
                                 "HOME environment variable not set"_ns);
  }

  // System directory
  child->ErrnoTest("write denied (/tmp)"_ns, false, [] {
    int fd = open("/tmp/gpu_sandbox_test_file.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/private/tmp)"_ns, false, [] {
    int fd = open("/private/tmp/sandboxTest.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  // Shader cache tests
  char buf[PATH_MAX];
  if (confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof(buf)) > 0) {
    nsCString cache(buf);

    // Can't create directories at the cache root.
    nsCString subdir = cache + "/mozilla-gpu-sbox-test"_ns;
    child->ErrnoTest("mkdir denied (cache root subdir)"_ns, false,
                     [s = std::string(subdir.get())] {
                       int rv = mkdir(s.c_str(), 0700);
                       if (rv == 0) {
                         rmdir(s.c_str());  // cleanup if somehow created
                       }
                       return rv;
                     });

    // Can't write files at the cache root.
    nsCString file = cache + "/gpu_test_file.txt"_ns;
    child->ErrnoTest("write denied (cache root)"_ns, false,
                     [f = std::string(file.get())] {
                       int fd = open(f.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                         unlink(f.c_str());
                       }
                       return fd;
                     });

    // Can't create a fake GPU bundle cache directory.
    nsCString fakeGpuDir = cache + "/org.mozilla.firefox-fake-gpu"_ns;
    child->ErrnoTest("mkdir denied (fake GPU cache dir)"_ns, false,
                     [dir = std::string(fakeGpuDir.get())] {
                       int rv = mkdir(dir.c_str(), 0700);
                       if (rv == 0) {
                         rmdir(dir.c_str());  // cleanup if somehow created
                       }
                       return rv;
                     });

    // Actual GPU bundle cache directory
    nsCString actualGpuCacheDir =
        cache + "/"_ns + nsCString(MOZ_GPU_PROCESS_BUNDLEID);

    // Allowed to write a regular file in the GPU bundle cache.
    nsCString legitGpuFile = actualGpuCacheDir + "/gpu_test.cache"_ns;
    child->ErrnoTest("write allowed (GPU bundle cache)"_ns, true,
                     [f = std::string(legitGpuFile.get())] {
                       int fd = open(f.c_str(), O_CREAT | O_WRONLY, 0600);
                       if (fd >= 0) {
                         close(fd);
                         unlink(f.c_str());  // cleanup
                         return 0;
                       }
                       return fd;
                     });

    // Symlink test inside the GPU bundle cache.
    nsCString symlinkInGpuCache = actualGpuCacheDir + "/bad_symlink"_ns;
    child->ErrnoTest("symlink denied (GPU bundle cache)"_ns, false,
                     [s = std::string(symlinkInGpuCache.get())] {
                       int rv = symlink("/etc/passwd", s.c_str());
                       if (rv == 0) {  // cleanup if somehow created
                         int fd = open(s.c_str(), O_RDONLY);
                         if (fd >= 0) {
                           close(fd);
                         }
                         unlink(s.c_str());
                       }
                       return rv;
                     });

  } else {
    child->SendReportTestResults("cache dir check"_ns, false,
                                 "Could not get user cache directory"_ns);
  }

  // Test read permissions
  child->ErrnoTest(
      "read allowed (/System/.../SystemVersion.plist)"_ns, true, [] {
        int fd =
            open("/System/Library/CoreServices/SystemVersion.plist", O_RDONLY);
        if (fd >= 0) {
          close(fd);
          return 0;
        }
        return fd;
      });

  // Test directory listing permissions
  child->ErrnoTest("list allowed (/Library/ColorSync/Profiles)"_ns, true, [] {
    DIR* d = opendir("/Library/ColorSync/Profiles");
    if (!d) return -1;
    struct dirent* entry = readdir(d);
    closedir(d);
    return entry ? 0 : -1;
  });

  child->ErrnoTest("list allowed (/private/var/db/CVMS)"_ns, true, [] {
    DIR* d = opendir("/private/var/db/CVMS");
    if (!d) return -1;
    struct dirent* entry = readdir(d);
    closedir(d);
    return entry ? 0 : -1;
  });

  // Test sysctl permissions
  child->ErrnoTest("sysctl allowed (kern.ostype)"_ns, true, [] {
    char buf[256];
    size_t sz = sizeof(buf);
    int rv = sysctlbyname("kern.ostype", buf, &sz, nullptr, 0);
    return rv;
  });

  child->ErrnoTest("sysctl allowed (hw.memsize)"_ns, true, [] {
    uint64_t mem = 0;
    size_t sz = sizeof(mem);
    int rv = sysctlbyname("hw.memsize", &mem, &sz, nullptr, 0);
    return rv;
  });

  // System directory write
  child->ErrnoTest("write denied (root)"_ns, false, [] {
    int fd = open("/gpu_root_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/usr/local)"_ns, false, [] {
    int fd = open("/usr/local/gpu_bad_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  child->ErrnoTest("write denied (/etc)"_ns, false, [] {
    int fd = open("/etc/gpu_bad_test.txt", O_CREAT | O_WRONLY, 0600);
    if (fd >= 0) {
      close(fd);
    }
    return fd;
  });

  RunMacTestLaunchProcess(child);
  RunMacTestAudioAPI(child);
  RunMacTestWindowServer(
      child,
      ProcessIsX86_64() && (!gfx::gfxVars::AllowMetalAngleWebGL() ||
                            StaticPrefs::webgl_disable_angle_AtStartup()));

#else   // defined(XP_WIN)
    child->ReportNoTests();
#endif  // defined(XP_WIN)
}

}  // namespace mozilla

#endif  // SECURITY_SANDBOX_COMMON_TEST_SANDBOXTESTINGCHILDTESTS_H_
