/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SandboxFilter.h"

#include <asm/ioctls.h>    // For TCGETS2
#include <asm/termbits.h>  // For termios2
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/ipc.h>
#include <linux/memfd.h>
#include <linux/mman.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/sockios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
// This has to go after <sys/socket.h> for annoying reasons
#include <linux/wireless.h>

#include <algorithm>
#include <utility>

#include "Sandbox.h"  // for ContentProcessSandboxParams
#include "SandboxBrokerClient.h"
#include "SandboxFilterUtil.h"
#include "SandboxInfo.h"
#include "SandboxInternal.h"
#include "SandboxLogging.h"
#include "SandboxOpenedFiles.h"
#include "mozilla/PodOperations.h"
#include "mozilla/ProcInfo_linux.h"
#include "mozilla/ProfilerPlatformMacros.h"
#include "mozilla/UniquePtr.h"
#include "prenv.h"
#include "sandbox/linux/bpf_dsl/bpf_dsl.h"
#include "sandbox/linux/system_headers/linux_seccomp.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"

#if defined(GP_PLAT_amd64_linux) && defined(GP_ARCH_amd64) && \
    defined(MOZ_USING_WASM_SANDBOXING)
#  include <asm/prctl.h>  // For ARCH_SET_GS
#endif

using namespace sandbox::bpf_dsl;

// Fill in defines in case of old headers.
// (Warning: these are wrong on PA-RISC.)
#ifndef MADV_HUGEPAGE
#  define MADV_HUGEPAGE 14
#endif
#ifndef MADV_NOHUGEPAGE
#  define MADV_NOHUGEPAGE 15
#endif
#ifndef MADV_DONTDUMP
#  define MADV_DONTDUMP 16
#endif

// Added in Linux 4.5; see bug 1303813.
#ifndef MADV_FREE
#  define MADV_FREE 8
#endif

#ifndef PR_SET_PTRACER
#  define PR_SET_PTRACER 0x59616d61
#endif

// Linux 5.17+
#ifndef PR_SET_VMA
#  define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#  define PR_SET_VMA_ANON_NAME 0
#endif

// The GNU libc headers define O_LARGEFILE as 0 on x86_64, but we need the
// actual value because it shows up in file flags.
#if !defined(O_LARGEFILE) || O_LARGEFILE == 0
#  define O_LARGEFILE_REAL 00100000
#else
#  define O_LARGEFILE_REAL O_LARGEFILE
#endif

// Not part of UAPI, but userspace sees it in F_GETFL; see bug 1650751.
#define FMODE_NONOTIFY 0x4000000

#ifndef F_LINUX_SPECIFIC_BASE
#  define F_LINUX_SPECIFIC_BASE 1024
#else
static_assert(F_LINUX_SPECIFIC_BASE == 1024);
#endif

#ifndef F_ADD_SEALS
#  define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)
#  define F_GET_SEALS (F_LINUX_SPECIFIC_BASE + 10)
#  define F_SEAL_SEAL 0x0001
#  define F_SEAL_SHRINK 0x0002
#  define F_SEAL_GROW 0x0004
#  define F_SEAL_WRITE 0x0008
#else
static_assert(F_ADD_SEALS == (F_LINUX_SPECIFIC_BASE + 9));
static_assert(F_GET_SEALS == (F_LINUX_SPECIFIC_BASE + 10));
#endif

// Added in 6.13
#ifndef MADV_GUARD_INSTALL
#  define MADV_GUARD_INSTALL 102
#  define MADV_GUARD_REMOVE 103
#else
static_assert(MADV_GUARD_INSTALL == 102);
static_assert(MADV_GUARD_REMOVE == 103);
#endif

// Added in 4.14
#ifndef MFD_HUGETLB
#  define MFD_HUGETLB 4U
#  define MFD_HUGE_MASK MAP_HUGE_MASK
#  define MFD_HUGE_SHIFT MAP_HUGE_SHIFT
#else
static_assert(MFD_HUGE_MASK == MAP_HUGE_MASK);
static_assert(MFD_HUGE_SHIFT == MAP_HUGE_SHIFT);
#endif

// Added in 6.10
#ifndef F_DUPFD_QUERY
#  define F_DUPFD_QUERY (F_LINUX_SPECIFIC_BASE + 3)
#else
static_assert(F_DUPFD_QUERY == (F_LINUX_SPECIFIC_BASE + 3));
#endif

// To avoid visual confusion between "ifdef ANDROID" and "ifndef ANDROID":
#ifndef ANDROID
#  define DESKTOP
#endif

namespace {
static const unsigned long kIoctlTypeMask = _IOC_TYPEMASK << _IOC_TYPESHIFT;
static const unsigned long kTtyIoctls = TIOCSTI & kIoctlTypeMask;
// On some older architectures (but not x86 or ARM), ioctls are
// assigned type fields differently, and the TIOC/TC/FIO group
// isn't all the same type.  If/when we support those archs,
// this would need to be revised (but really this should be a
// default-deny policy; see below).
static_assert(kTtyIoctls == (TCSETA & kIoctlTypeMask) &&
                  kTtyIoctls == (FIOASYNC & kIoctlTypeMask),
              "tty-related ioctls use the same type");
};  // namespace

// This file defines the seccomp-bpf system call filter policies.
// See also SandboxFilterUtil.h, for the CASES_FOR_* macros and
// SandboxFilterBase::Evaluate{Socket,Ipc}Call.
//
// One important difference from how Chromium bpf_dsl filters are
// normally interpreted: returning -ENOSYS from a Trap() handler
// indicates an unexpected system call; SigSysHandler() in Sandbox.cpp
// will detect this, request a crash dump, and terminate the process.
// This does not apply to using Error(ENOSYS) in the policy, so that
// can be used if returning an actual ENOSYS is needed.

namespace mozilla {

// This class allows everything used by the sandbox itself, by the
// core IPC code, by the crash reporter, or other core code.  It also
// contains support for brokering file operations, but file access is
// denied if no broker client is provided by the concrete class.
class SandboxPolicyCommon : public SandboxPolicyBase {
 protected:
  // Subclasses can assign these in their constructors to loosen the
  // default settings.
  SandboxBrokerClient* mBroker = nullptr;
  bool mMayCreateShmem = false;
  bool mAllowUnsafeSocketPair = false;
  bool mBrokeredConnect = false;  // Can connect() be brokered?

  SandboxPolicyCommon() = default;

  typedef const arch_seccomp_data& ArgsRef;

  static intptr_t BlockedSyscallTrap(ArgsRef aArgs, void* aux) {
    MOZ_ASSERT(!aux);
    return -ENOSYS;
  }

  // Convert Unix-style "return -1 and set errno" APIs back into the
  // Linux ABI "return -err" style.
  static intptr_t ConvertError(long rv) { return rv < 0 ? -errno : rv; }

  template <typename... Args>
  static intptr_t DoSyscall(long nr, Args... args) {
    static_assert(std::conjunction_v<
                      std::conditional_t<(sizeof(Args) <= sizeof(void*)),
                                         std::true_type, std::false_type>...>,
                  "each syscall arg is at most one word");
    return ConvertError(syscall(nr, args...));
  }

  // Mesa's amdgpu driver uses kcmp with KCMP_FILE; see also bug
  // 1624743.  This policy restricts it to the process's own pid,
  // which should be sufficient on its own if we need to remove the
  // `type` restriction in the future.
  //
  // (Note: if we end up with more Mesa-specific hooks needed in
  // several process types, we could put them into this class's
  // EvaluateSyscall guarded by a boolean member variable, or
  // introduce another layer of subclassing.)
  ResultExpr KcmpPolicyForMesa() const {
    // The real KCMP_FILE is part of an anonymous enum in
    // <linux/kcmp.h>, but we can't depend on having that header,
    // and it's not a #define so the usual #ifndef approach
    // doesn't work.
    static const int kKcmpFile = 0;
    const pid_t myPid = getpid();
    Arg<pid_t> pid1(0), pid2(1);
    Arg<int> type(2);
    return If(AllOf(pid1 == myPid, pid2 == myPid, type == kKcmpFile), Allow())
        .Else(InvalidSyscall());
  }

  static intptr_t SchedTrap(ArgsRef aArgs, void* aux) {
    const pid_t tid = syscall(__NR_gettid);
    if (aArgs.args[0] == static_cast<uint64_t>(tid)) {
      return DoSyscall(aArgs.nr, 0, static_cast<uintptr_t>(aArgs.args[1]),
                       static_cast<uintptr_t>(aArgs.args[2]),
                       static_cast<uintptr_t>(aArgs.args[3]),
                       static_cast<uintptr_t>(aArgs.args[4]),
                       static_cast<uintptr_t>(aArgs.args[5]));
    }
    return -EPERM;
  }

 private:
  // Bug 1093893: Translate tkill to tgkill for pthread_kill; fixed in
  // bionic commit 10c8ce59a (in JB and up; API level 16 = Android 4.1).
  // Bug 1376653: musl also needs this, and security-wise it's harmless.
  static intptr_t TKillCompatTrap(ArgsRef aArgs, void* aux) {
    auto tid = static_cast<pid_t>(aArgs.args[0]);
    auto sig = static_cast<int>(aArgs.args[1]);
    return DoSyscall(__NR_tgkill, getpid(), tid, sig);
  }

  static intptr_t SetNoNewPrivsTrap(ArgsRef& aArgs, void* aux) {
    if (gSetSandboxFilter == nullptr) {
      // Called after BroadcastSetThreadSandbox finished, therefore
      // not our doing and not expected.
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    // Signal that the filter is already in place.
    return -ETXTBSY;
  }

  // Trap handlers for filesystem brokering.
  // (The amount of code duplication here could be improved....)
#ifdef __NR_open
  static intptr_t OpenTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto flags = static_cast<int>(aArgs.args[1]);
    return broker->Open(path, flags);
  }

  static intptr_t AccessTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto mode = static_cast<int>(aArgs.args[1]);
    return broker->Access(path, mode);
  }

  static intptr_t StatTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto buf = reinterpret_cast<statstruct*>(aArgs.args[1]);
    return broker->Stat(path, buf);
  }

  static intptr_t LStatTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto buf = reinterpret_cast<statstruct*>(aArgs.args[1]);
    return broker->LStat(path, buf);
  }

  static intptr_t ChmodTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto mode = static_cast<mode_t>(aArgs.args[1]);
    return broker->Chmod(path, mode);
  }

  static intptr_t LinkTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto path2 = reinterpret_cast<const char*>(aArgs.args[1]);
    return broker->Link(path, path2);
  }

  static intptr_t RenameTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto path2 = reinterpret_cast<const char*>(aArgs.args[1]);
    return broker->Rename(path, path2);
  }

  static intptr_t MkdirTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto mode = static_cast<mode_t>(aArgs.args[1]);
    return broker->Mkdir(path, mode);
  }

  static intptr_t RmdirTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    return broker->Rmdir(path);
  }

  static intptr_t UnlinkTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    if (!path) {
      // A null path is a bad pointer as far as the kernel is concerned.
      return -EFAULT;
    }
    if (path[0] == '\0') {
      // If the path is empty, then just fail the call here
      return -ENOENT;
    }
    return broker->Unlink(path);
  }

  static intptr_t ReadlinkTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    auto buf = reinterpret_cast<char*>(aArgs.args[1]);
    auto size = static_cast<size_t>(aArgs.args[2]);
    return broker->Readlink(path, buf, size);
  }
#endif  // __NR_open

  static intptr_t OpenAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto flags = static_cast<int>(aArgs.args[2]);
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative openat(%d, \"%s\", 0%o)", fd, path,
                  flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Open(path, flags);
  }

  static intptr_t AccessAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto mode = static_cast<int>(aArgs.args[2]);
    // Linux's faccessat syscall has no "flags" argument.  Attempting
    // to handle the flags != 0 case is left to userspace; this is
    // impossible to do correctly in all cases, but that's not our
    // problem.
    //
    // Starting with kernel 5.8+ and glibc 2.33, there is faccessat2 that
    // supports flags, handled below.
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative faccessat(%d, \"%s\", %d)", fd, path,
                  mode);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Access(path, mode);
  }

  static intptr_t AccessAt2Trap(ArgsRef aArgs, void* aux) {
    auto* broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    const auto* path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto mode = static_cast<int>(aArgs.args[2]);
    auto flags = static_cast<int>(aArgs.args[3]);
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative faccessat2(%d, \"%s\", %d, %d)", fd,
                  path, mode, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    if ((flags & ~AT_EACCESS) == 0) {
      return broker->Access(path, mode);
    }
    return ConvertError(ENOSYS);
  }

  static intptr_t StatAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto buf = reinterpret_cast<statstruct*>(aArgs.args[2]);
    auto flags = static_cast<int>(aArgs.args[3]);

    if (fd != AT_FDCWD && (flags & AT_EMPTY_PATH) && path &&
        !strcmp(path, "")) {
#ifdef __NR_fstat64
      return DoSyscall(__NR_fstat64, fd, buf);
#else
      return DoSyscall(__NR_fstat, fd, buf);
#endif
    }

    if (!broker) {
      return BlockedSyscallTrap(aArgs, nullptr);
    }

    if (fd != AT_FDCWD && path && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative fstatat(%d, \"%s\", %p, 0x%x)", fd,
                  path, buf, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }

    int badFlags = flags & ~(AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT);
    if (badFlags != 0) {
      SANDBOX_LOG("unsupported flags 0x%x in fstatat(%d, \"%s\", %p, 0x%x)",
                  badFlags, fd, path, buf, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return (flags & AT_SYMLINK_NOFOLLOW) == 0 ? broker->Stat(path, buf)
                                              : broker->LStat(path, buf);
  }

  static intptr_t ChmodAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto mode = static_cast<mode_t>(aArgs.args[2]);
    auto flags = static_cast<int>(aArgs.args[3]);
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative chmodat(%d, \"%s\", 0%o, %d)", fd,
                  path, mode, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    if (flags != 0) {
      SANDBOX_LOG("unsupported flags in chmodat(%d, \"%s\", 0%o, %d)", fd, path,
                  mode, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Chmod(path, mode);
  }

  static intptr_t LinkAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto fd2 = static_cast<int>(aArgs.args[2]);
    auto path2 = reinterpret_cast<const char*>(aArgs.args[3]);
    auto flags = static_cast<int>(aArgs.args[4]);
    if ((fd != AT_FDCWD && path[0] != '/') ||
        (fd2 != AT_FDCWD && path2[0] != '/')) {
      SANDBOX_LOG(
          "unsupported fd-relative linkat(%d, \"%s\", %d, \"%s\", 0x%x)", fd,
          path, fd2, path2, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    if (flags != 0) {
      SANDBOX_LOG("unsupported flags in linkat(%d, \"%s\", %d, \"%s\", 0x%x)",
                  fd, path, fd2, path2, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Link(path, path2);
  }

  static intptr_t RenameAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto fd2 = static_cast<int>(aArgs.args[2]);
    auto path2 = reinterpret_cast<const char*>(aArgs.args[3]);
    if ((fd != AT_FDCWD && path[0] != '/') ||
        (fd2 != AT_FDCWD && path2[0] != '/')) {
      SANDBOX_LOG("unsupported fd-relative renameat(%d, \"%s\", %d, \"%s\")",
                  fd, path, fd2, path2);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Rename(path, path2);
  }

  static intptr_t MkdirAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto mode = static_cast<mode_t>(aArgs.args[2]);
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative mkdirat(%d, \"%s\", 0%o)", fd, path,
                  mode);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Mkdir(path, mode);
  }

  static intptr_t UnlinkAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto flags = static_cast<int>(aArgs.args[2]);
    if (!path) {
      // A null path is a bad pointer as far as the kernel is concerned.
      return -EFAULT;
    }
    if (path[0] == '\0') {
      // If the path is empty, then just fail the call here
      return -ENOENT;
    }
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative unlinkat(%d, \"%s\", 0x%x)", fd,
                  path, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    int badFlags = flags & ~AT_REMOVEDIR;
    if (badFlags != 0) {
      SANDBOX_LOG("unsupported flags 0x%x in unlinkat(%d, \"%s\", 0x%x)",
                  badFlags, fd, path, flags);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return (flags & AT_REMOVEDIR) == 0 ? broker->Unlink(path)
                                       : broker->Rmdir(path);
  }

  static intptr_t ReadlinkAtTrap(ArgsRef aArgs, void* aux) {
    auto broker = static_cast<SandboxBrokerClient*>(aux);
    auto fd = static_cast<int>(aArgs.args[0]);
    auto path = reinterpret_cast<const char*>(aArgs.args[1]);
    auto buf = reinterpret_cast<char*>(aArgs.args[2]);
    auto size = static_cast<size_t>(aArgs.args[3]);
    if (fd != AT_FDCWD && path[0] != '/') {
      SANDBOX_LOG("unsupported fd-relative readlinkat(%d, %s, %p, %d)", fd,
                  path, buf, size);
      return BlockedSyscallTrap(aArgs, nullptr);
    }
    return broker->Readlink(path, buf, size);
  }

  static intptr_t SocketpairDatagramTrap(ArgsRef aArgs, void* aux) {
    auto fds = reinterpret_cast<int*>(aArgs.args[3]);
    // Return sequential packet sockets instead of the expected
    // datagram sockets; see bug 1355274 for details.
    return ConvertError(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds));
  }

  static intptr_t SocketcallUnpackTrap(ArgsRef aArgs, void* aux) {
#ifdef __NR_socketcall
    auto argsPtr = reinterpret_cast<const unsigned long*>(aArgs.args[1]);
    int sysno = -1;

    // When Linux added separate syscalls for socket operations on the
    // old socketcall platforms, they had long since stopped adding
    // send and recv syscalls, because they can be trivially mapped
    // onto sendto and recvfrom (see also open vs. openat).
    //
    // But, socketcall itself *does* have separate calls for those.
    // So, we need to remap them; since send(to) and recv(from)
    // have basically the same types except for const, the code is
    // factored out here.
    unsigned long altArgs[6];
    auto legacySendRecvWorkaround = [&] {
      MOZ_ASSERT(argsPtr != altArgs);
      memcpy(altArgs, argsPtr, sizeof(unsigned long[4]));
      altArgs[4] = altArgs[5] = 0;
      argsPtr = altArgs;
    };

    switch (aArgs.args[0]) {
      // See also the other socketcall table in SandboxFilterUtil.cpp
#  define DISPATCH_SOCKETCALL(this_sysno, this_call) \
    case this_call:                                  \
      sysno = this_sysno;                            \
      break

      DISPATCH_SOCKETCALL(__NR_socketpair, SYS_SOCKETPAIR);
      DISPATCH_SOCKETCALL(__NR_getsockopt, SYS_GETSOCKOPT);
      DISPATCH_SOCKETCALL(__NR_sendmsg, SYS_SENDMSG);
      DISPATCH_SOCKETCALL(__NR_recvmsg, SYS_RECVMSG);
      DISPATCH_SOCKETCALL(__NR_sendto, SYS_SENDTO);
      DISPATCH_SOCKETCALL(__NR_recvfrom, SYS_RECVFROM);
      DISPATCH_SOCKETCALL(__NR_sendmmsg, SYS_SENDMMSG);
      DISPATCH_SOCKETCALL(__NR_recvmmsg, SYS_RECVMMSG);
      // __NR_recvmmsg_time64 is not available as a socketcall; a
      // Y2K38-ready userland would call it directly.
#  undef DISPATCH_SOCKETCALL

      case SYS_SEND:
        sysno = __NR_sendto;
        legacySendRecvWorkaround();
        break;
      case SYS_RECV:
        sysno = __NR_recvfrom;
        legacySendRecvWorkaround();
        break;
    }

    // This assert will fail if someone tries to map a socketcall to
    // this trap without adding it to the switch statement above.
    MOZ_RELEASE_ASSERT(sysno >= 0);

    return DoSyscall(sysno, argsPtr[0], argsPtr[1], argsPtr[2], argsPtr[3],
                     argsPtr[4], argsPtr[5]);

#else  // no socketcall
    MOZ_CRASH("unreachable?");
    return -ENOSYS;
#endif
  }

  // This just needs to return something to stand in for the
  // unconnected socket until ConnectTrap, below, and keep track of
  // the socket type somehow.  Half a socketpair *is* a socket, so it
  // should result in minimal confusion in the caller.
  static intptr_t FakeSocketTrapCommon(int domain, int type, int protocol) {
    int fds[2];
    // X11 client libs will still try to getaddrinfo() even for a
    // local connection.  Also, WebRTC still has vestigial network
    // code trying to do things in the content process.  Politely tell
    // them no.
    if (domain != AF_UNIX) {
      return -EAFNOSUPPORT;
    }
    if (socketpair(domain, type, protocol, fds) != 0) {
      return -errno;
    }
    close(fds[1]);
    return fds[0];
  }

  static intptr_t FakeSocketTrap(ArgsRef aArgs, void* aux) {
    return FakeSocketTrapCommon(static_cast<int>(aArgs.args[0]),
                                static_cast<int>(aArgs.args[1]),
                                static_cast<int>(aArgs.args[2]));
  }

  static intptr_t FakeSocketTrapLegacy(ArgsRef aArgs, void* aux) {
    const auto innerArgs = reinterpret_cast<unsigned long*>(aArgs.args[1]);

    return FakeSocketTrapCommon(static_cast<int>(innerArgs[0]),
                                static_cast<int>(innerArgs[1]),
                                static_cast<int>(innerArgs[2]));
  }

  static Maybe<int> DoGetSockOpt(int fd, int optname) {
    int optval;
    socklen_t optlen = sizeof(optval);

    if (getsockopt(fd, SOL_SOCKET, optname, &optval, &optlen) != 0) {
      return Nothing();
    }
    MOZ_RELEASE_ASSERT(static_cast<size_t>(optlen) == sizeof(optval));
    return Some(optval);
  }

  // Substitute the newly connected socket from the broker for the
  // original socket.  This is meant to be used on a fd from
  // FakeSocketTrap, above, but it should also work to simulate
  // re-connect()ing a real connected socket.
  //
  // Warning: This isn't quite right if the socket is dup()ed, because
  // other duplicates will still be the original socket, but hopefully
  // nothing we're dealing with does that.
  static intptr_t ConnectTrapCommon(SandboxBrokerClient* aBroker, int aFd,
                                    const struct sockaddr_un* aAddr,
                                    socklen_t aLen) {
    if (aFd < 0) {
      return -EBADF;
    }
    const auto maybeDomain = DoGetSockOpt(aFd, SO_DOMAIN);
    if (!maybeDomain) {
      return -errno;
    }
    if (*maybeDomain != AF_UNIX) {
      return -EAFNOSUPPORT;
    }
    const auto maybeType = DoGetSockOpt(aFd, SO_TYPE);
    if (!maybeType) {
      return -errno;
    }
    const int oldFlags = fcntl(aFd, F_GETFL);
    if (oldFlags == -1) {
      return -errno;
    }
    const int newFd = aBroker->Connect(aAddr, aLen, *maybeType);
    if (newFd < 0) {
      return newFd;
    }
    // Copy over the nonblocking flag.  The connect() won't be
    // nonblocking in that case, but that shouldn't matter for
    // AF_UNIX.  The other fcntl-settable flags are either irrelevant
    // for sockets (e.g., O_APPEND) or would be blocked by this
    // seccomp-bpf policy, so they're ignored.
    if (fcntl(newFd, F_SETFL, oldFlags & O_NONBLOCK) != 0) {
      close(newFd);
      return -errno;
    }
    if (dup2(newFd, aFd) < 0) {
      close(newFd);
      return -errno;
    }
    close(newFd);
    return 0;
  }

  static intptr_t ConnectTrap(ArgsRef aArgs, void* aux) {
    typedef const struct sockaddr_un* AddrPtr;

    return ConnectTrapCommon(static_cast<SandboxBrokerClient*>(aux),
                             static_cast<int>(aArgs.args[0]),
                             reinterpret_cast<AddrPtr>(aArgs.args[1]),
                             static_cast<socklen_t>(aArgs.args[2]));
  }

  static intptr_t ConnectTrapLegacy(ArgsRef aArgs, void* aux) {
    const auto innerArgs = reinterpret_cast<unsigned long*>(aArgs.args[1]);
    typedef const struct sockaddr_un* AddrPtr;

    return ConnectTrapCommon(static_cast<SandboxBrokerClient*>(aux),
                             static_cast<int>(innerArgs[0]),
                             reinterpret_cast<AddrPtr>(innerArgs[1]),
                             static_cast<socklen_t>(innerArgs[2]));
  }

  static intptr_t StatFsTrap(ArgsRef aArgs, void* aux) {
    // Warning: the kernel interface is not the C interface.  The
    // structs are different (<asm/statfs.h> vs. <sys/statfs.h>), and
    // the statfs64 version takes an additional size parameter.
    auto path = reinterpret_cast<const char*>(aArgs.args[0]);
    int fd = open(path, O_RDONLY | O_LARGEFILE);
    if (fd < 0) {
      return -errno;
    }

    intptr_t rv;
    switch (aArgs.nr) {
      case __NR_statfs: {
        auto buf = reinterpret_cast<void*>(aArgs.args[1]);
        rv = DoSyscall(__NR_fstatfs, fd, buf);
        break;
      }
#ifdef __NR_statfs64
      case __NR_statfs64: {
        auto sz = static_cast<size_t>(aArgs.args[1]);
        auto buf = reinterpret_cast<void*>(aArgs.args[2]);
        rv = DoSyscall(__NR_fstatfs64, fd, sz, buf);
        break;
      }
#endif
      default:
        MOZ_ASSERT(false);
        rv = -ENOSYS;
    }

    close(fd);
    return rv;
  }

 public:
  ResultExpr InvalidSyscall() const override {
    return Trap(BlockedSyscallTrap, nullptr);
  }

  virtual ResultExpr ClonePolicy(ResultExpr failPolicy) const {
    // Allow use for simple thread creation (pthread_create) only.

    // WARNING: s390 and cris pass the flags in the second arg -- see
    // CLONE_BACKWARDS2 in arch/Kconfig in the kernel source -- but we
    // don't support seccomp-bpf on those archs yet.
    Arg<int> flags(0);

    // The exact flags used can vary.  CLONE_DETACHED is used by musl
    // and by old versions of Android (<= JB 4.2), but it's been
    // ignored by the kernel since the beginning of the Git history.
    //
    // If we ever need to support Android <= KK 4.4 again, SETTLS
    // and the *TID flags will need to be made optional.
    static const int flags_required =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
        CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID |
        CLONE_CHILD_CLEARTID;
    static const int flags_optional = CLONE_DETACHED;

    return If((flags & ~flags_optional) == flags_required, Allow())
        .Else(failPolicy);
  }

  virtual ResultExpr PrctlPolicy() const {
    Arg<int> op(0);
    Arg<int> arg2(1);
    return Switch(op)
        .Case(PR_SET_VMA,  // Tagging of anonymous memory mappings
              If(arg2 == PR_SET_VMA_ANON_NAME, Allow()).Else(InvalidSyscall()))
        .Cases({PR_GET_SECCOMP,   // BroadcastSetThreadSandbox, etc.
                PR_SET_NAME,      // Thread creation
                PR_SET_DUMPABLE,  // Crash reporting
                PR_SET_PTRACER},  // Debug-mode crash handling
               Allow())
        .Case(PR_CAPBSET_READ,  // libcap.so.2 loaded by libpulse.so.0
                                // queries for capabilities
              Error(EINVAL))
#if defined(MOZ_PROFILE_GENERATE)
        .Case(PR_GET_PDEATHSIG, Allow())
#endif  // defined(MOZ_PROFILE_GENERATE)
        .Default(InvalidSyscall());
  }

  virtual BoolExpr MsgFlagsAllowed(const Arg<int>& aFlags) const {
    // MSG_DONTWAIT: used by IPC
    // MSG_NOSIGNAL: used by the sandbox (broker, reporter)
    // MSG_CMSG_CLOEXEC: should be used by anything that's passed fds
    static constexpr int kNeeded =
        MSG_DONTWAIT | MSG_NOSIGNAL | MSG_CMSG_CLOEXEC;

    // These don't appear to be used in our code at the moment, but
    // they seem low-risk enough to allow to avoid the possibility of
    // breakage.  (Necko might use MSG_PEEK, but the socket process
    // overrides this method.)
    static constexpr int kHarmless = MSG_PEEK | MSG_WAITALL | MSG_TRUNC;

    static constexpr int kAllowed = kNeeded | kHarmless;
    return (aFlags & ~kAllowed) == 0;
  }

  static ResultExpr UnpackSocketcallOrAllow() {
    // See bug 1066750.
    if (HasSeparateSocketCalls()) {
      // If this is a socketcall(2) platform, but the kernel also
      // supports separate syscalls (>= 4.3.0), we can unpack the
      // arguments and filter them.
      return Trap(SocketcallUnpackTrap, nullptr);
    }
    // Otherwise, we can't filter the args if the platform passes
    // them by pointer.
    return Allow();
  }

  Maybe<ResultExpr> EvaluateSocketCall(int aCall,
                                       bool aHasArgs) const override {
    switch (aCall) {
      case SYS_RECVMSG:
      case SYS_SENDMSG:
        if (aHasArgs) {
          Arg<int> flags(2);
          return Some(
              If(MsgFlagsAllowed(flags), Allow()).Else(InvalidSyscall()));
        }
        return Some(UnpackSocketcallOrAllow());

        // These next four weren't needed for IPC or other core
        // functionality when they were added, but they're subsets of
        // recvmsg/sendmsg so there's nothing gained by not allowing
        // them here (and simplifying subclasses).  Also, there may be
        // unknown dependencies on them now.
      case SYS_RECVFROM:
      case SYS_SENDTO:
      case SYS_RECV:
      case SYS_SEND:
        if (aHasArgs) {
          Arg<int> flags(3);
          return Some(
              If(MsgFlagsAllowed(flags), Allow()).Else(InvalidSyscall()));
        }
        return Some(UnpackSocketcallOrAllow());

      case SYS_SOCKETPAIR: {
        // We try to allow "safe" (always connected) socketpairs when using the
        // file broker, or for content processes, but we may need to fall back
        // and allow all socketpairs in some cases, see bug 1066750.
        if (!mBroker && !mAllowUnsafeSocketPair) {
          return Nothing();
        }
        if (!aHasArgs) {
          return Some(UnpackSocketcallOrAllow());
        }
        Arg<int> domain(0), type(1);
        return Some(
            If(domain == AF_UNIX,
               Switch(type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
                   .Case(SOCK_STREAM, Allow())
                   .Case(SOCK_SEQPACKET, Allow())
                   // This is used only by content (and only for
                   // direct PulseAudio, which is deprecated) but it
                   // doesn't increase attack surface:
                   .Case(SOCK_DGRAM, Trap(SocketpairDatagramTrap, nullptr))
                   .Default(InvalidSyscall()))
                .Else(InvalidSyscall()));
      }

      case SYS_GETSOCKOPT: {
        // Best-effort argument filtering as for socketpair(2), above.
        if (!aHasArgs) {
          if (HasSeparateSocketCalls()) {
            return Some(Trap(SocketcallUnpackTrap, nullptr));
          }
          return Some(Allow());
        }
        Arg<int> level(1), optname(2);
        // SO_SNDBUF is used by IPC to avoid constructing
        // unnecessarily large gather arrays for `sendmsg`.
        //
        // SO_DOMAIN and SO_TYPE are needed for connect() brokering,
        // but they're harmless even when it's not enabled.
        return Some(If(AllOf(level == SOL_SOCKET,
                             AnyOf(optname == SO_SNDBUF, optname == SO_DOMAIN,
                                   optname == SO_TYPE)),
                       Allow())
                        .Else(InvalidSyscall()));
      }

        // These two cases are for connect() brokering, if enabled.
      case SYS_SOCKET:
        if (mBrokeredConnect) {
          const auto trapFn = aHasArgs ? FakeSocketTrap : FakeSocketTrapLegacy;
          MOZ_ASSERT(mBroker);
          return Some(Trap(trapFn, mBroker));
        }
        return Nothing();

      case SYS_CONNECT:
        if (mBrokeredConnect) {
          const auto trapFn = aHasArgs ? ConnectTrap : ConnectTrapLegacy;
          MOZ_ASSERT(mBroker);
          return Some(Trap(trapFn, mBroker));
        }
        return Nothing();

      default:
        return Nothing();
    }
  }

  ResultExpr EvaluateSyscall(int sysno) const override {
    // If a file broker client was provided, route syscalls to it;
    // otherwise, fall through to the main policy, which will deny
    // them.
    if (mBroker) {
      switch (sysno) {
#ifdef __NR_open
        case __NR_open:
          return Trap(OpenTrap, mBroker);
        case __NR_access:
          return Trap(AccessTrap, mBroker);
        CASES_FOR_stat:
          return Trap(StatTrap, mBroker);
        CASES_FOR_lstat:
          return Trap(LStatTrap, mBroker);
        case __NR_chmod:
          return Trap(ChmodTrap, mBroker);
        case __NR_link:
          return Trap(LinkTrap, mBroker);
        case __NR_mkdir:
          return Trap(MkdirTrap, mBroker);
        case __NR_symlink:
          return Error(EPERM);
        case __NR_rename:
          return Trap(RenameTrap, mBroker);
        case __NR_rmdir:
          return Trap(RmdirTrap, mBroker);
        case __NR_unlink:
          return Trap(UnlinkTrap, mBroker);
        case __NR_readlink:
          return Trap(ReadlinkTrap, mBroker);
#endif
        case __NR_openat:
          return Trap(OpenAtTrap, mBroker);
        case __NR_faccessat:
          return Trap(AccessAtTrap, mBroker);
        case __NR_faccessat2:
          return Trap(AccessAt2Trap, mBroker);
        CASES_FOR_fstatat:
          return Trap(StatAtTrap, mBroker);
        // Used by new libc and Rust's stdlib, if available.
        // We don't have broker support yet so claim it does not exist.
        case __NR_statx:
          return Error(ENOSYS);
        case __NR_fchmodat:
          return Trap(ChmodAtTrap, mBroker);
        case __NR_linkat:
          return Trap(LinkAtTrap, mBroker);
        case __NR_mkdirat:
          return Trap(MkdirAtTrap, mBroker);
        case __NR_symlinkat:
          return Error(EPERM);
        case __NR_renameat:
          return Trap(RenameAtTrap, mBroker);
        case __NR_unlinkat:
          return Trap(UnlinkAtTrap, mBroker);
        case __NR_readlinkat:
          return Trap(ReadlinkAtTrap, mBroker);
      }
    } else {
      // In the absence of a broker we still need to handle the
      // fstat-equivalent subset of fstatat; see bug 1673770.
      switch (sysno) {
        // statx may be used for fstat (bug 1867673)
        case __NR_statx:
          return Error(ENOSYS);
        CASES_FOR_fstatat:
          return Trap(StatAtTrap, nullptr);
      }
    }

    switch (sysno) {
        // Timekeeping
        //
        // (Note: the switch needs to start with a literal case, not a
        // macro; otherwise clang-format gets confused.)
      case __NR_gettimeofday:
#ifdef __NR_time
      case __NR_time:
#endif
      case __NR_nanosleep:
        return Allow();

      CASES_FOR_clock_gettime:
      CASES_FOR_clock_getres:
      CASES_FOR_clock_nanosleep: {
        // clockid_t can encode a pid or tid to monitor another
        // process or thread's CPU usage (see CPUCLOCK_PID and related
        // definitions in include/linux/posix-timers.h in the kernel
        // source).  For threads, the kernel allows only tids within
        // the calling process, so it isn't a problem if we don't
        // filter those; pids do need to be restricted to the current
        // process in order to not leak information.
        Arg<clockid_t> clk_id(0);

        clockid_t this_process =
            MAKE_PROCESS_CPUCLOCK(getpid(), CPUCLOCK_SCHED);

        return If(clk_id == CLOCK_MONOTONIC, Allow())
#ifdef CLOCK_MONOTONIC_COARSE
            // Used by SandboxReporter, among other things.
            .ElseIf(clk_id == CLOCK_MONOTONIC_COARSE, Allow())
#endif
#ifdef CLOCK_MONOTONIC_RAW
            .ElseIf(clk_id == CLOCK_MONOTONIC_RAW, Allow())
#endif
            .ElseIf(clk_id == CLOCK_PROCESS_CPUTIME_ID, Allow())
            .ElseIf(clk_id == CLOCK_REALTIME, Allow())
#ifdef CLOCK_REALTIME_COARSE
            .ElseIf(clk_id == CLOCK_REALTIME_COARSE, Allow())
#endif
            .ElseIf(clk_id == CLOCK_THREAD_CPUTIME_ID, Allow())
            // Allow clock_gettime on the same process.
            .ElseIf(clk_id == this_process, Allow())
            // Allow clock_gettime on a thread.
            .ElseIf((clk_id & 7u) == (CPUCLOCK_PERTHREAD_MASK | CPUCLOCK_SCHED),
                    Allow())
#ifdef CLOCK_BOOTTIME
            .ElseIf(clk_id == CLOCK_BOOTTIME, Allow())
#endif
            .Else(InvalidSyscall());
      }

        // Thread synchronization
      CASES_FOR_futex:
        // FIXME(bug 1441993): This could be more restrictive.
        return Allow();

        // Asynchronous I/O
      CASES_FOR_epoll_create:
      CASES_FOR_epoll_wait:
      case __NR_epoll_ctl:
      CASES_FOR_poll:
        return Allow();

        // Used when requesting a crash dump.
      CASES_FOR_pipe:
        return Allow();

        // Metadata of opened files
      CASES_FOR_fstat:
        return Allow();

      CASES_FOR_fcntl: {
        Arg<int> cmd(1);
        Arg<int> flags(2);

        // Typical use of F_SETFL is to modify the flags returned by
        // F_GETFL and write them back, including some flags that
        // F_SETFL ignores.  This is a default-deny policy in case any
        // new SETFL-able flags are added.  (In particular we want to
        // forbid O_ASYNC; see bug 1328896, but also see bug 1408438.)
        static constexpr int kIgnoredFlags =
            O_ACCMODE | O_LARGEFILE_REAL | O_CLOEXEC | FMODE_NONOTIFY;
        static constexpr int kAllowedFlags =
            kIgnoredFlags | O_APPEND | O_NONBLOCK;

        // Limiting file seals may be an excess of caution; more can be added if
        // needed. (E.g., F_SEAL_WRITE would be useful so that child-to-parent
        // IPC can prevent races, but we don't have a cross-platform solution
        // for that use case.)
        static constexpr int kAllowedSeals = F_SEAL_SHRINK | F_SEAL_GROW;

        return Switch(cmd)
            // Close-on-exec is meaningless when execve isn't allowed, but
            // NSPR reads the bit and asserts that it has the expected value.
            .Case(F_GETFD, Allow())
            .Case(
                F_SETFD,
                If((flags & ~FD_CLOEXEC) == 0, Allow()).Else(InvalidSyscall()))
            // F_GETFL is also used by fdopen
            .Case(F_GETFL, Allow())
            .Case(F_SETFL, If((flags & ~kAllowedFlags) == 0, Allow())
                               .Else(InvalidSyscall()))
#if defined(MOZ_PROFILE_GENERATE)
            .Case(F_SETLKW, Allow())
#endif
            // Not much different from other forms of dup(), and commonly used.
            .Case(F_DUPFD_CLOEXEC, Allow())
            // Used by Mesa, generally useful, and harmless: tests if
            // two file descriptors refer to the same file description.
            .Case(F_DUPFD_QUERY, Allow())
            // Allow sealing size for shared memory; see above.
            .Case(F_GET_SEALS, Allow())
            .Case(F_ADD_SEALS, If((flags & ~kAllowedSeals) == 0, Allow())
                                   .Else(InvalidSyscall()))
            .Default(SandboxPolicyBase::EvaluateSyscall(sysno));
      }

        // Simple I/O
      case __NR_pread64:
      case __NR_write:
      case __NR_read:
      case __NR_readv:
      case __NR_writev:  // see SandboxLogging.cpp
      CASES_FOR_lseek:
        return Allow();

      CASES_FOR_getdents:
        return Allow();

      CASES_FOR_ftruncate:
      case __NR_fallocate:
        return mMayCreateShmem ? Allow() : InvalidSyscall();

        // Used by our fd/shm classes
      case __NR_dup:
        return Allow();

        // Memory mapping
      CASES_FOR_mmap: {
        Arg<int> flags(3);
        // Explicit huge-page mapping has a history of bugs, and
        // generally isn't used outside of server applications.
        static constexpr int kBadFlags =
            MAP_HUGETLB | (MAP_HUGE_MASK << MAP_HUGE_SHIFT);
        // ENOSYS seems to be what the kernel would return if
        // CONFIG_HUGETLBFS=n.  (This uses Error rather than
        // InvalidSyscall because the latter would crash on Nightly,
        // and I don't think those reports would be actionable.)
        return If((flags & kBadFlags) != 0, Error(ENOSYS)).Else(Allow());
      }
      case __NR_munmap:
        return Allow();

        // Shared memory
      case __NR_memfd_create: {
        Arg<unsigned> flags(1);
        // See above about mmap MAP_HUGETLB.
        static constexpr int kBadFlags =
            MFD_HUGETLB | (MFD_HUGE_MASK << MFD_HUGE_SHIFT);
        return If((flags & kBadFlags) != 0, Error(ENOSYS)).Else(Allow());
      }

        // ipc::Shmem; also, glibc when creating threads:
      case __NR_mprotect:
        return Allow();

#if !defined(MOZ_MEMORY)
        // No jemalloc means using a system allocator like glibc
        // that might use brk.
      case __NR_brk:
        return Allow();

        // Similarly, mremap (bugs: 1047620, 1286119, 1860267)
      case __NR_mremap: {
        Arg<int> flags(3);
        return If((flags & ~MREMAP_MAYMOVE) == 0, Allow())
            .Else(SandboxPolicyBase::EvaluateSyscall(sysno));
      }
#endif

        // madvise hints used by malloc; see bug 1303813 and bug 1364533
      case __NR_madvise: {
        Arg<int> advice(2);
        // The GMP specific sandbox duplicates this logic, so when adding
        // allowed values here also add them to the GMP sandbox rules.
        return If(advice == MADV_DONTNEED, Allow())
            .ElseIf(advice == MADV_FREE, Allow())
            // Used by glibc (and maybe someday mozjemalloc).
            .ElseIf(advice == MADV_GUARD_INSTALL, Allow())
            .ElseIf(advice == MADV_GUARD_REMOVE, Allow())
            // Formerly used by mozjemalloc; unclear if current use:
            .ElseIf(advice == MADV_HUGEPAGE, Allow())
            .ElseIf(advice == MADV_NOHUGEPAGE, Allow())
#ifdef MOZ_ASAN
            .ElseIf(advice == MADV_DONTDUMP, Allow())
#endif
            .ElseIf(advice == MADV_MERGEABLE, Error(EPERM))  // bug 1705045
            .Else(InvalidSyscall());
      }

        // musl libc will set this up in pthreads support.
      case __NR_membarrier:
        return Allow();

        // Signal handling
      case __NR_sigaltstack:
      CASES_FOR_sigreturn:
      CASES_FOR_sigprocmask:
      CASES_FOR_sigaction:
        return Allow();

        // Send signals within the process (raise(), profiling, etc.)
      case __NR_tgkill: {
        Arg<pid_t> tgid(0);
        return If(tgid == getpid(), Allow()).Else(InvalidSyscall());
      }

        // Polyfill with tgkill; see above.
      case __NR_tkill:
        return Trap(TKillCompatTrap, nullptr);

        // Yield
      case __NR_sched_yield:
        return Allow();

        // Thread creation.
      case __NR_clone:
        return ClonePolicy(InvalidSyscall());

      case __NR_clone3:
        return Error(ENOSYS);

        // More thread creation.
#ifdef __NR_set_robust_list
      case __NR_set_robust_list:
        return Allow();
#endif
#ifdef ANDROID
      case __NR_set_tid_address:
        return Allow();
#endif

        // prctl
      case __NR_prctl: {
        // WARNING: do not handle __NR_prctl directly in subclasses;
        // override PrctlPolicy instead.  The special handling of
        // PR_SET_NO_NEW_PRIVS is used to detect that a thread already
        // has the policy applied; see also bug 1257361.

        if (SandboxInfo::Get().Test(SandboxInfo::kHasSeccompTSync)) {
          return PrctlPolicy();
        }

        Arg<int> option(0);
        return If(option == PR_SET_NO_NEW_PRIVS,
                  Trap(SetNoNewPrivsTrap, nullptr))
            .Else(PrctlPolicy());
      }

#if defined(GP_PLAT_amd64_linux) && defined(GP_ARCH_amd64) && \
    defined(MOZ_USING_WASM_SANDBOXING)
        // arch_prctl
      case __NR_arch_prctl: {
        // Bug 1923701 - Needed for by RLBox-wasm2c: Buggy libraries are
        // sandboxed with RLBox and wasm2c (Wasm). wasm2c offers an optimization
        // for performance that uses the otherwise-unused GS register on x86.
        // The GS register is only settable using the arch_prctl platforms on
        // older x86 CPUs that don't have the wrgsbase instruction. This
        // optimization is currently only supported on linux+clang+x86_64.
        Arg<int> op(0);
        return If(op == ARCH_SET_GS, Allow())
            .Else(SandboxPolicyBase::EvaluateSyscall(sysno));
      }
#endif

        // NSPR can call this when creating a thread, but it will accept a
        // polite "no".
      case __NR_getpriority:
        // But if thread creation races with sandbox startup, that call
        // could succeed, and then we get one of these:
      case __NR_setpriority:
        return Error(EACCES);

        // Stack bounds are obtained via pthread_getattr_np, which calls
        // this but doesn't actually need it:
      case __NR_sched_getaffinity:
        return Error(ENOSYS);

        // Identifies the processor and node where this thread or process is
        // running. This is used by "Awake" profiler markers.
      case __NR_getcpu:
        return Allow();

        // Read own pid/tid.
      case __NR_getpid:
      case __NR_gettid:
        return Allow();

        // Discard capabilities
      case __NR_close:
        return Allow();

        // Machine-dependent stuff
#ifdef __arm__
      case __ARM_NR_breakpoint:
      case __ARM_NR_cacheflush:
      case __ARM_NR_usr26:  // FIXME: do we actually need this?
      case __ARM_NR_usr32:
      case __ARM_NR_set_tls:
        return Allow();
#endif

        // Needed when being debugged:
      case __NR_restart_syscall:
        return Allow();

        // Terminate threads or the process
      case __NR_exit:
      case __NR_exit_group:
        return Allow();

      case __NR_getrandom:
        return Allow();

        // Used by almost every process: GMP needs them for Clearkey
        // because of bug 1576006 (but may not need them for other
        // plugin types; see bug 1737092).  Given that fstat is
        // allowed, the uid/gid are probably available anyway.
      CASES_FOR_getuid:
      CASES_FOR_getgid:
      CASES_FOR_geteuid:
      CASES_FOR_getegid:
        return Allow();

#ifdef DESKTOP
        // Bug 1543858: glibc's qsort calls sysinfo to check the
        // memory size; it falls back to assuming there's enough RAM.
      case __NR_sysinfo:
        return Error(EPERM);
#endif

        // Bug 1651701: an API for restartable atomic sequences and
        // per-CPU data; exposing information about CPU numbers and
        // when threads are migrated or preempted isn't great but the
        // risk should be relatively low.
      case __NR_rseq:
        return Allow();

      case __NR_ioctl: {
        Arg<unsigned long> request(1);
#ifdef MOZ_ASAN
        Arg<int> fd(0);
#endif  // MOZ_ASAN
        // Make isatty() return false, because none of the terminal
        // ioctls will be allowed; libraries sometimes call this for
        // various reasons (e.g., to decide whether to emit ANSI/VT
        // color codes when logging to stderr).  glibc uses TCGETS and
        // musl uses TIOCGWINSZ.
        //
        // This is required by ffmpeg
        return If(AnyOf(request == TCGETS, request == TIOCGWINSZ,
                        request == TCGETS2),
                  Error(ENOTTY))
#ifdef MOZ_ASAN
            // ASAN's error reporter wants to know if stderr is a tty.
            .ElseIf(fd == STDERR_FILENO, Error(ENOTTY))
#endif  // MOZ_ASAN
            .Else(SandboxPolicyBase::EvaluateSyscall(sysno));
      }

      CASES_FOR_dup2:  // See ConnectTrapCommon
        if (mBrokeredConnect) {
          return Allow();
        }
        return SandboxPolicyBase::EvaluateSyscall(sysno);

#ifdef MOZ_ASAN
        // ...and before compiler-rt r209773, it will call readlink on
        // /proc/self/exe and use the cached value only if that fails:
      case __NR_readlink:
      case __NR_readlinkat:
        return Error(ENOENT);

        // ...and if it found an external symbolizer, it will try to run it:
        // (See also bug 1081242 comment #7.)
      CASES_FOR_stat:
        return Error(ENOENT);
#endif  // MOZ_ASAN

        // Replace statfs with open (which may be brokered) and
        // fstatfs (which is not allowed in this policy, but may be
        // allowed by subclasses if they wish to enable statfs).
      CASES_FOR_statfs:
        return Trap(StatFsTrap, nullptr);

        // GTK's theme parsing tries to getcwd() while sandboxed, but
        // only during Talos runs.
        // Also, Rust panics call getcwd to try to print relative paths
        // in backtraces.
      case __NR_getcwd:
        return Error(ENOENT);

        // Basically every process type ends up using this for some
        // reason (nsSystemInfo in content, Mesa in RDD, bug 1992904 for
        // utility, etc.).  Other than GMP, which overrides this (see
        // below), it's relatively safe to expose this information.
      case __NR_uname:
        return Allow();

      default:
        return SandboxPolicyBase::EvaluateSyscall(sysno);
    }
  }
};

// The process-type-specific syscall rules start here:

// The seccomp-bpf filter for content processes is not a true sandbox
// on its own; its purpose is attack surface reduction and syscall
// interception in support of a semantic sandboxing layer.  On B2G
// this is the Android process permission model; on desktop,
// namespaces and chroot() will be used.
class ContentSandboxPolicy : public SandboxPolicyCommon {
 private:
  ContentProcessSandboxParams mParams;
  bool mAllowSysV;
  bool mUsingRenderDoc;

  bool BelowLevel(int aLevel) const { return mParams.mLevel < aLevel; }
  ResultExpr AllowBelowLevel(int aLevel, ResultExpr aOrElse) const {
    return BelowLevel(aLevel) ? Allow() : std::move(aOrElse);
  }
  ResultExpr AllowBelowLevel(int aLevel) const {
    return AllowBelowLevel(aLevel, InvalidSyscall());
  }

  static intptr_t GetPPidTrap(ArgsRef aArgs, void* aux) {
    // In a pid namespace, getppid() will return 0. We will return 0 instead
    // of the real parent pid to see what breaks when we introduce the
    // pid namespace (Bug 1151624).
    return 0;
  }

 public:
  ContentSandboxPolicy(SandboxBrokerClient* aBroker,
                       ContentProcessSandboxParams&& aParams)
      : mParams(std::move(aParams)),
        mAllowSysV(PR_GetEnv("MOZ_SANDBOX_ALLOW_SYSV") != nullptr),
        mUsingRenderDoc(PR_GetEnv("RENDERDOC_CAPTUREOPTS") != nullptr) {
    mBroker = aBroker;
    mMayCreateShmem = true;
    mAllowUnsafeSocketPair = true;
    mBrokeredConnect = true;
  }

  ~ContentSandboxPolicy() override = default;

  Maybe<ResultExpr> EvaluateSocketCall(int aCall,
                                       bool aHasArgs) const override {
    switch (aCall) {
#ifdef ANDROID
      case SYS_SOCKET:
        return Some(Error(EACCES));
#else  // #ifdef DESKTOP
      case SYS_SOCKET:
      case SYS_CONNECT:
        if (BelowLevel(4)) {
          return Some(Allow());
        }
        return SandboxPolicyCommon::EvaluateSocketCall(aCall, aHasArgs);

        // FIXME (bug 1761134): sockopts should be filtered
      case SYS_GETSOCKOPT:
      case SYS_SETSOCKOPT:
        // These next 3 were needed for X11; they may not be needed
        // with X11 lockdown, but there's not much attack surface here.
      case SYS_GETSOCKNAME:
      case SYS_GETPEERNAME:
      case SYS_SHUTDOWN:
        return Some(Allow());

      case SYS_ACCEPT:
      case SYS_ACCEPT4:
        if (mUsingRenderDoc) {
          return Some(Allow());
        }
        [[fallthrough]];
#endif
      default:
        return SandboxPolicyCommon::EvaluateSocketCall(aCall, aHasArgs);
    }
  }

#ifdef DESKTOP
  Maybe<ResultExpr> EvaluateIpcCall(int aCall, int aArgShift) const override {
    switch (aCall) {
        // These are a problem: SysV IPC follows the Unix "same uid
        // policy" and can't be restricted/brokered like file access.
        // We're not using it directly, but there are some library
        // dependencies that do; see ContentNeedsSysVIPC() in
        // SandboxLaunch.cpp.  Also, Cairo as used by GTK will sometimes
        // try to use MIT-SHM, so shmget() is a non-fatal error.  See
        // also bug 1376910 and bug 1438401.
      case SHMGET:
        return Some(mAllowSysV ? Allow() : Error(EPERM));
      case SHMCTL:
      case SHMAT:
      case SHMDT:
      case SEMGET:
      case SEMCTL:
      case SEMOP:
        if (mAllowSysV) {
          return Some(Allow());
        }
        return SandboxPolicyCommon::EvaluateIpcCall(aCall, aArgShift);
      default:
        return SandboxPolicyCommon::EvaluateIpcCall(aCall, aArgShift);
    }
  }
#endif

#ifdef MOZ_PULSEAUDIO
  ResultExpr PrctlPolicy() const override {
    if (BelowLevel(4)) {
      Arg<int> op(0);
      return If(op == PR_GET_NAME, Allow())
          .Else(SandboxPolicyCommon::PrctlPolicy());
    }
    return SandboxPolicyCommon::PrctlPolicy();
  }
#endif

  ResultExpr EvaluateSyscall(int sysno) const override {
    // Straight allow for anything that got overriden via prefs
    const auto& whitelist = mParams.mSyscallWhitelist;
    if (std::find(whitelist.begin(), whitelist.end(), sysno) !=
        whitelist.end()) {
      if (SandboxInfo::Get().Test(SandboxInfo::kVerbose)) {
        SANDBOX_LOG("Allowing syscall nr %d via whitelist", sysno);
      }
      return Allow();
    }

    // Level 1 has been removed.  If seccomp-bpf is used, then we're
    // necessarily at level >= 2 and filesystem access is brokered.
    MOZ_ASSERT(!BelowLevel(2));
    MOZ_ASSERT(mBroker);

    switch (sysno) {
#ifdef DESKTOP
      case __NR_getppid:
        return Trap(GetPPidTrap, nullptr);

#  ifdef MOZ_PULSEAUDIO
      CASES_FOR_fchown:
      case __NR_fchmod:
        return AllowBelowLevel(4);
#  endif
      CASES_FOR_fstatfs:  // fontconfig, pulseaudio, GIO (see also statfs)
      case __NR_flock:    // graphics
        return Allow();

        // Bug 1354731: proprietary GL drivers try to mknod() their devices
#  ifdef __NR_mknod
      case __NR_mknod:
#  endif
      case __NR_mknodat: {
        Arg<mode_t> mode(sysno == __NR_mknodat ? 2 : 1);
        return If((mode & S_IFMT) == S_IFCHR, Error(EPERM))
            .Else(InvalidSyscall());
      }
      // Bug 1438389: ...and nvidia GL will sometimes try to chown the devices
#  ifdef __NR_chown
      case __NR_chown:
#  endif
      case __NR_fchownat:
        return Error(EPERM);
#endif

      CASES_FOR_select:
        return Allow();

      case __NR_writev:
#ifdef DESKTOP
      case __NR_pwrite64:
      case __NR_readahead:
#endif
        return Allow();

      case __NR_ioctl: {
#ifdef MOZ_ALSA
        if (BelowLevel(4)) {
          return Allow();
        }
#endif
        Arg<unsigned long> request(1);
        auto shifted_type = request & kIoctlTypeMask;

        // Rust's stdlib seems to use FIOCLEX instead of equivalent fcntls.
        return If(request == FIOCLEX, Allow())
            // Rust's stdlib also uses FIONBIO instead of equivalent fcntls.
            .ElseIf(request == FIONBIO, Allow())
            // Allow anything that isn't a tty ioctl, if level < 6
            .ElseIf(
                BelowLevel(6) ? shifted_type != kTtyIoctls : BoolConst(false),
                Allow())
            .Else(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

      CASES_FOR_fcntl: {
        Arg<int> cmd(1);
        return Switch(cmd)
            // Nvidia GL and fontconfig (newer versions) use fcntl file locking.
            .Case(F_SETLK, Allow())
#ifdef F_SETLK64
            .Case(F_SETLK64, Allow())
#endif
            // Pulseaudio uses F_SETLKW, as does fontconfig.
            .Case(F_SETLKW, Allow())
#ifdef F_SETLKW64
            .Case(F_SETLKW64, Allow())
#endif
            // Wayland client libraries use file seals
            .Case(F_ADD_SEALS, Allow())
            .Case(F_GET_SEALS, Allow())
            .Default(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

      case __NR_brk:
        // FIXME(bug 1510861) are we using any hints that aren't allowed
        // in SandboxPolicyCommon now?
      case __NR_madvise:
        return Allow();

        // wasm uses mremap (always with zero flags)
      case __NR_mremap: {
        Arg<int> flags(3);
        return If(flags == 0, Allow())
            .Else(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

        // Bug 1462640: Mesa libEGL uses mincore to test whether values
        // are pointers, for reasons.
      case __NR_mincore: {
        Arg<size_t> length(1);
        return If(length == getpagesize(), Allow())
            .Else(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

#ifdef __NR_set_thread_area
      case __NR_set_thread_area:
        return Allow();
#endif

      case __NR_getrusage:
      case __NR_times:
        return Allow();

      case __NR_fsync:
      case __NR_msync:
        return Allow();

      case __NR_getpriority:
      case __NR_setpriority:
      case __NR_sched_getattr:
      case __NR_sched_setattr:
      case __NR_sched_get_priority_min:
      case __NR_sched_get_priority_max:
      case __NR_sched_getscheduler:
      case __NR_sched_setscheduler:
      case __NR_sched_getparam:
      case __NR_sched_setparam:
#ifdef DESKTOP
      case __NR_sched_getaffinity:
#endif
        return Allow();

#ifdef DESKTOP
      case __NR_sched_setaffinity:
        return Error(EPERM);
#endif

#ifdef DESKTOP
      case __NR_pipe2: {
        // Restrict the flags; O_NOTIFICATION_PIPE in particular
        // exposes enough attack surface to be a cause for concern
        // (bug 1808320).  O_DIRECT isn't known to be used currently
        // (Try passes with it blocked), but should be low-risk, and
        // Chromium allows it.
        static constexpr int allowed_flags = O_CLOEXEC | O_NONBLOCK | O_DIRECT;
        Arg<int> flags(1);
        return If((flags & ~allowed_flags) == 0, Allow())
            .Else(InvalidSyscall());
      }

      CASES_FOR_getrlimit:
      CASES_FOR_getresuid:
      CASES_FOR_getresgid:
        return Allow();

      case __NR_prlimit64: {
        // Allow only the getrlimit() use case.  (glibc seems to use
        // only pid 0 to indicate the current process; pid == getpid()
        // is equivalent and could also be allowed if needed.)
        Arg<pid_t> pid(0);
        // This is really a const struct ::rlimit*, but Arg<> doesn't
        // work with pointers, only integer types.
        Arg<uintptr_t> new_limit(2);
        return If(AllOf(pid == 0, new_limit == 0), Allow())
            .Else(InvalidSyscall());
      }

        // PulseAudio calls umask, even though it's unsafe in
        // multithreaded applications.  But, allowing it here doesn't
        // really do anything one way or the other, now that file
        // accesses are brokered to another process.
      case __NR_umask:
        return AllowBelowLevel(4);

      case __NR_kill: {
        if (BelowLevel(4)) {
          Arg<int> sig(1);
          // PulseAudio uses kill(pid, 0) to check if purported owners of
          // shared memory files are still alive; see bug 1397753 for more
          // details.
          return If(sig == 0, Error(EPERM)).Else(InvalidSyscall());
        }
        return InvalidSyscall();
      }

      case __NR_wait4:
#  ifdef __NR_waitpid
      case __NR_waitpid:
#  endif
        // NSPR will start a thread to wait for child processes even if
        // fork() fails; see bug 227246 and bug 1299581.
        return Error(ECHILD);

      case __NR_eventfd2:
        return Allow();

#  ifdef __NR_rt_tgsigqueueinfo
        // Only allow to send signals within the process.
      case __NR_rt_tgsigqueueinfo: {
        Arg<pid_t> tgid(0);
        return If(tgid == getpid(), Allow()).Else(InvalidSyscall());
      }
#  endif

      case __NR_mlock:
      case __NR_munlock:
        return Allow();

        // We can't usefully allow fork+exec, even on a temporary basis;
        // the child would inherit the seccomp-bpf policy and almost
        // certainly die from an unexpected SIGSYS.  We also can't have
        // fork() crash, currently, because there are too many system
        // libraries/plugins that try to run commands.  But they can
        // usually do something reasonable on error.
      case __NR_clone:
        return ClonePolicy(Error(EPERM));
#  ifdef __NR_fork
      case __NR_fork:
        return Error(ENOSYS);
#  endif

#  ifdef __NR_fadvise64
      case __NR_fadvise64:
        return Allow();
#  endif

#  ifdef __NR_fadvise64_64
      case __NR_fadvise64_64:
        return Allow();
#  endif

      case __NR_fallocate:
        return Allow();

      case __NR_get_mempolicy:
        return Allow();

      // Required by libnuma for FFmpeg
      case __NR_set_mempolicy:
        return Error(ENOSYS);

      case __NR_kcmp:
        return KcmpPolicyForMesa();

#endif  // DESKTOP

#ifdef DESKTOP
      case __NR_sysinfo:
#endif
        return Allow();

      default:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
    }
  }
};

UniquePtr<sandbox::bpf_dsl::Policy> GetContentSandboxPolicy(
    SandboxBrokerClient* aMaybeBroker, ContentProcessSandboxParams&& aParams) {
  return MakeUnique<ContentSandboxPolicy>(aMaybeBroker, std::move(aParams));
}

// The GeckoMediaPlugin seccomp-bpf policy is more delicate than the others,
// because this process loads a closed-source binary and we've told users
// that we prohibit it from fingerprinting them; see:
// https://hacks.mozilla.org/2014/05/reconciling-mozillas-mission-and-w3c-eme/
//
// Be especially careful about what this policy allows.
class GMPSandboxPolicy : public SandboxPolicyCommon {
  static intptr_t OpenTrap(const arch_seccomp_data& aArgs, void* aux) {
    const auto files = static_cast<const SandboxOpenedFiles*>(aux);
    const char* path;
    int flags;

    switch (aArgs.nr) {
#ifdef __NR_open
      case __NR_open:
        path = reinterpret_cast<const char*>(aArgs.args[0]);
        flags = static_cast<int>(aArgs.args[1]);
        break;
#endif
      case __NR_openat:
        // The path has to be absolute to match the pre-opened file (see
        // assertion in ctor) so the dirfd argument is ignored.
        path = reinterpret_cast<const char*>(aArgs.args[1]);
        flags = static_cast<int>(aArgs.args[2]);
        break;
      default:
        MOZ_CRASH("unexpected syscall number");
    }

    if ((flags & O_ACCMODE) != O_RDONLY) {
      SANDBOX_LOG("non-read-only open of file %s attempted (flags=0%o)", path,
                  flags);
      return -EROFS;
    }
    int fd = files->GetDesc(path);
    if (fd < 0) {
      // SandboxOpenedFile::GetDesc already logged about this, if appropriate.
      return -ENOENT;
    }
    return fd;
  }

#if defined(__NR_stat64) || defined(__NR_stat)
  static intptr_t StatTrap(const arch_seccomp_data& aArgs, void* aux) {
    const auto* const files = static_cast<const SandboxOpenedFiles*>(aux);
    const auto* path = reinterpret_cast<const char*>(aArgs.args[0]);
    int fd = files->GetDesc(path);
    if (fd < 0) {
      // SandboxOpenedFile::GetDesc already logged about this, if appropriate.
      return -ENOENT;
    }
    auto* buf = reinterpret_cast<statstruct*>(aArgs.args[1]);
#  ifdef __NR_fstat64
    return DoSyscall(__NR_fstat64, fd, buf);
#  else
    return DoSyscall(__NR_fstat, fd, buf);
#  endif
  }
#endif

  static intptr_t UnameTrap(const arch_seccomp_data& aArgs, void* aux) {
    const auto buf = reinterpret_cast<struct utsname*>(aArgs.args[0]);
    PodZero(buf);
    // The real uname() increases fingerprinting risk for no benefit.
    // This is close enough.
    strcpy(buf->sysname, "Linux");
    strcpy(buf->version, "3");
    return 0;
  }

  const SandboxOpenedFiles* mFiles;

 public:
  explicit GMPSandboxPolicy(const SandboxOpenedFiles* aFiles) : mFiles(aFiles) {
    // Used by the profiler to send data back to the parent process;
    // we are not enabling the file broker, so this will only work if
    // memfd_create is available.
    mMayCreateShmem = true;
  }

  ~GMPSandboxPolicy() override = default;

  ResultExpr EvaluateSyscall(int sysno) const override {
    switch (sysno) {
      // Simulate opening the plugin file.
#ifdef __NR_open
      case __NR_open:
#endif
      case __NR_openat:
        return Trap(OpenTrap, mFiles);

#if defined(__NR_stat64) || defined(__NR_stat)
      CASES_FOR_stat:
        return Trap(StatTrap, mFiles);
#endif

      case __NR_brk:
        return Allow();
      case __NR_sched_get_priority_min:
      case __NR_sched_get_priority_max:
        return Allow();
      case __NR_sched_getparam:
      case __NR_sched_getscheduler:
      case __NR_sched_setscheduler: {
        Arg<pid_t> pid(0);
        return If(pid == 0, Allow()).Else(Trap(SchedTrap, nullptr));
      }

      // For clock(3) on older glibcs; bug 1304220.
      case __NR_times:
        return Allow();

      // Bug 1372428
      case __NR_uname:
        return Trap(UnameTrap, nullptr);

      // Allow the same advice values as the default policy, but return
      // Error(ENOSYS) for other values. Because the Widevine CDM may probe
      // advice arguments, including invalid values, we don't want to return
      // InvalidSyscall(), as this will crash the process. So instead just
      // indicate such calls are not available.
      case __NR_madvise: {
        Arg<int> advice(2);
        return If(advice == MADV_DONTNEED, Allow())
            .ElseIf(advice == MADV_FREE, Allow())
            .ElseIf(advice == MADV_HUGEPAGE, Allow())
            .ElseIf(advice == MADV_NOHUGEPAGE, Allow())
#ifdef MOZ_ASAN
            .ElseIf(advice == MADV_DONTDUMP, Allow())
#endif
            .ElseIf(advice == MADV_MERGEABLE, Error(EPERM))  // bug 1705045
            .Else(Error(ENOSYS));
      }

      // The profiler will try to readlink /proc/self/exe for native
      // stackwalking, but that's broken for several other reasons;
      // see discussion in bug 1770905.  (That can be emulated by
      // pre-recording the result if/when we need it.)
#ifdef __NR_readlink
      case __NR_readlink:
#endif
      case __NR_readlinkat:
        return Error(EINVAL);

      default:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
    }
  }
};

UniquePtr<sandbox::bpf_dsl::Policy> GetMediaSandboxPolicy(
    const SandboxOpenedFiles* aFiles) {
  return UniquePtr<sandbox::bpf_dsl::Policy>(new GMPSandboxPolicy(aFiles));
}

// The policy for the data decoder process is similar to the one for
// media plugins, but the codec code is all in-tree so it's better
// behaved and doesn't need special exceptions (or the ability to load
// a plugin file).  However, it does directly create shared memory
// segments, so it may need file brokering.
class RDDSandboxPolicy final : public SandboxPolicyCommon {
 public:
  explicit RDDSandboxPolicy(SandboxBrokerClient* aBroker) {
    mBroker = aBroker;
    mMayCreateShmem = true;
#ifdef MOZ_ENABLE_VULKAN_VIDEO
    if (aBroker) {
      mBrokeredConnect = true;
    }
#endif
  }

#ifndef ANDROID
  Maybe<ResultExpr> EvaluateIpcCall(int aCall, int aArgShift) const override {
    // The Intel media driver uses SysV IPC (semaphores and shared
    // memory) on newer hardware models; it always uses this fixed
    // key, so we can restrict semget and shmget.  Unfortunately, the
    // calls that operate on these resources take "identifiers", which
    // are unpredictable (by us) but guessable (by an adversary).
    static constexpr key_t kIntelKey = 'D' << 24 | 'V' << 8 | 'X' << 0;

    switch (aCall) {
      case SEMGET:
      case SHMGET: {
        Arg<key_t> key(0 + aArgShift);
        return Some(If(key == kIntelKey, Allow()).Else(InvalidSyscall()));
      }

      case SEMCTL:
      case SEMOP:
      case SEMTIMEDOP:
      case SHMCTL:
      case SHMAT:
      case SHMDT:
        return Some(Allow());

      default:
        return SandboxPolicyCommon::EvaluateIpcCall(aCall, aArgShift);
    }
  }
#endif

  Maybe<ResultExpr> EvaluateSocketCall(int aCall,
                                       bool aHasArgs) const override {
    switch (aCall) {
      // These are for X11.
      //
      // FIXME (bug 1884449): X11 is blocked now so we probably don't
      // need these, but they're relatively harmless.
      case SYS_GETSOCKNAME:
      case SYS_GETPEERNAME:
      case SYS_SHUTDOWN:
        return Some(Allow());

#ifdef MOZ_ENABLE_VULKAN_VIDEO
      // GPU drivers may call bind() while probing display sockets; this does
      // not enable any connections (no MAY_CONNECT targets in RDD policy) and
      // is not needed for Vulkan video decode — only avoids seccomp noise
      // (bug 2021722).
      case SYS_BIND:
        return Some(Error(EPERM));
      // Some Vulkan ICDs may also load CUDA, which calls setsockopt.
      case SYS_GETSOCKOPT:
      case SYS_SETSOCKOPT:
        return Some(Allow());
#endif

      case SYS_SOCKET:
        // Hardware-accelerated decode uses EGL to manage hardware surfaces.
        // When initialised it tries to connect to the Wayland server over a
        // UNIX socket. It still works fine if it can't connect to Wayland, so
        // don't let it create the socket (but don't kill the process for
        // trying).
        //
        // We also see attempts to connect to an X server on desktop
        // Linux sometimes (bug 1882598).
#ifdef MOZ_ENABLE_VULKAN_VIDEO
        // Vulkan video decode requires EGL to successfully connect to the
        // display server for EGL_MESA_image_dma_buf_export (bug 2021722).
        // With a broker, route through FakeSocketTrap so connect() is
        // restricted to the MAY_CONNECT paths in the broker policy.
        if (mBrokeredConnect) {
          return SandboxPolicyCommon::EvaluateSocketCall(aCall, aHasArgs);
        }
#endif
        return Some(Error(EACCES));

      default:
        return SandboxPolicyCommon::EvaluateSocketCall(aCall, aHasArgs);
    }
  }

  ResultExpr EvaluateSyscall(int sysno) const override {
    switch (sysno) {
      case __NR_getrusage:
        return Allow();

      // Required by libnuma for FFmpeg
      case __NR_get_mempolicy:
        return Allow();

      // Required by libnuma for FFmpeg
      case __NR_set_mempolicy:
        return Error(ENOSYS);

      case __NR_ioctl: {
        Arg<unsigned long> request(1);
        auto shifted_type = request & kIoctlTypeMask;
        static constexpr unsigned long kDrmType =
            static_cast<unsigned long>('d') << _IOC_TYPESHIFT;
        // Note: 'b' is also the Binder device on Android.
        static constexpr unsigned long kDmaBufType =
            static_cast<unsigned long>('b') << _IOC_TYPESHIFT;
#ifdef MOZ_ENABLE_V4L2
        // Type 'V' for V4L2, used for hw accelerated decode
        static constexpr unsigned long kVideoType =
            static_cast<unsigned long>('V') << _IOC_TYPESHIFT;
#endif
#ifdef MOZ_ENABLE_VULKAN_VIDEO
        static constexpr unsigned long kNvidiaRmType =
            static_cast<unsigned long>('m') << _IOC_TYPESHIFT;
        // UDMABUF_CREATE(_LIST) on /dev/udmabuf for NVIDIA Wayland DMA-BUF
        // export.
        static constexpr unsigned long kUdmabufType =
            static_cast<unsigned long>('u') << _IOC_TYPESHIFT;
#endif
        // nvidia non-tegra uses some ioctls from this range (but not actual
        // fbdev ioctls; nvidia uses values >= 200 for the NR field
        // (low 8 bits))
        static constexpr unsigned long kFbDevType =
            static_cast<unsigned long>('F') << _IOC_TYPESHIFT;

#if defined(__aarch64__)
        // NVIDIA decoder, from Linux4Tegra
        // http://lists.mplayerhq.hu/pipermail/ffmpeg-devel/2024-May/328552.html
        static constexpr unsigned long kNvidiaNvmapType =
            static_cast<unsigned long>('N') << _IOC_TYPESHIFT;
        static constexpr unsigned long kNvidiaNvhostType =
            static_cast<unsigned long>('H') << _IOC_TYPESHIFT;
#endif  // defined(__aarch64__)

        // Allow DRI and DMA-Buf for VA-API. Also allow V4L2 if enabled
        return If(shifted_type == kDrmType, Allow())
            .ElseIf(shifted_type == kDmaBufType, Allow())
#ifdef MOZ_ENABLE_VULKAN_VIDEO
            .ElseIf(shifted_type == kNvidiaRmType, Allow())
            .ElseIf(shifted_type == kUdmabufType, Allow())
#endif
#ifdef MOZ_ENABLE_V4L2
            .ElseIf(shifted_type == kVideoType, Allow())
#endif
        // NVIDIA decoder from Linux4Tegra, this is specific to Tegra ARM64 SoC
#if defined(__aarch64__)
            .ElseIf(shifted_type == kNvidiaNvmapType, Allow())
            .ElseIf(shifted_type == kNvidiaNvhostType, Allow())
#endif  // defined(__aarch64__)
        // Hack for nvidia non-tegra devices, which isn't supported yet:
#ifdef MOZ_ENABLE_VULKAN_VIDEO
            .ElseIf(shifted_type == kFbDevType, Allow())
#else
            .ElseIf(shifted_type == kFbDevType, Error(ENOTTY))
#endif
            .Else(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

        // Mesa/amdgpu
      case __NR_kcmp:
        return KcmpPolicyForMesa();

        // We use this in our DMABuf support code.
      case __NR_eventfd2:
        return Allow();

        // Allow the sched_* syscalls for the current thread only.
        // Mesa attempts to use them to optimize performance; often
        // this involves passing other threads' tids, which we can't
        // safely allow, but maybe a future Mesa version could fix that.
        // Also sched_setaffinity is required by libnuma for FFmpeg.
      case __NR_sched_getaffinity:
      case __NR_sched_setaffinity:
      case __NR_sched_getparam:
      case __NR_sched_setparam:
      case __NR_sched_getscheduler:
      case __NR_sched_setscheduler:
      case __NR_sched_getattr:
      case __NR_sched_setattr: {
        Arg<pid_t> pid(0);
        return If(pid == 0, Allow()).Else(Trap(SchedTrap, nullptr));
      }

        // The priority bounds are also used, sometimes (bug 1838675):
      case __NR_sched_get_priority_min:
      case __NR_sched_get_priority_max:
        return Allow();

        // nvidia tries to mknod(!) its devices; that won't work anyway,
        // so quietly reject it.
#ifdef __NR_mknod
      case __NR_mknod:
#endif
      case __NR_mknodat:
        return Error(EPERM);

        // Used by the nvidia GPU driver, including in multi-GPU
        // systems when we intend to use a non-nvidia GPU.  (Also used
        // by Mesa for its shader cache, but we disable that in this
        // process.)
      CASES_FOR_fstatfs:
        return Allow();

        // nvidia drivers may attempt to spawn nvidia-modprobe
      case __NR_clone:
        return ClonePolicy(Error(EPERM));
#ifdef __NR_fork
      case __NR_fork:
        return Error(ENOSYS);
#endif
#ifdef MOZ_ENABLE_VULKAN_VIDEO
      CASES_FOR_getresuid:
      CASES_FOR_getresgid:
        return Allow();
      CASES_FOR_fcntl: {
        Arg<int> cmd(1);
        // udmabuf requires the backing memfd to be sealed, and the driver may
        // check the seals it applied.
        return Switch(cmd)
            .Case(F_ADD_SEALS, Allow())
            .Case(F_GET_SEALS, Allow())
            .Default(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }
      // EGL snapshot GL context needs socket creation and display server
      // connection for EGL_MESA_image_dma_buf_export (bug 2021722).
      // With a broker, these are brokered: socket() via FakeSocketTrap
      // (AF_UNIX only), connect() via ConnectTrap (MAY_CONNECT paths only).
      case __NR_socket:
      case __NR_connect:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
#endif
        // Pass through the common policy for other syscalls
      default:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
    }
  }
};

UniquePtr<sandbox::bpf_dsl::Policy> GetDecoderSandboxPolicy(
    SandboxBrokerClient* aMaybeBroker) {
  return UniquePtr<sandbox::bpf_dsl::Policy>(
      new RDDSandboxPolicy(aMaybeBroker));
}

// Basically a clone of RDDSandboxPolicy until we know exactly what
// the SocketProcess sandbox looks like.
class SocketProcessSandboxPolicy final : public SandboxPolicyCommon {
 private:
  SocketProcessSandboxParams mParams;

  bool BelowLevel(int aLevel) const { return mParams.mLevel < aLevel; }

 public:
  explicit SocketProcessSandboxPolicy(SandboxBrokerClient* aBroker,
                                      SocketProcessSandboxParams&& aParams)
      : mParams(std::move(aParams)) {
    mBroker = aBroker;
    mMayCreateShmem = true;
  }

  static intptr_t FcntlTrap(const arch_seccomp_data& aArgs, void* aux) {
    const auto cmd = static_cast<int>(aArgs.args[1]);
    switch (cmd) {
        // This process can't exec, so the actual close-on-exec flag
        // doesn't matter; have it always read as true and ignore writes.
      case F_GETFD:
        return O_CLOEXEC;
      case F_SETFD:
        return 0;
      default:
        return -ENOSYS;
    }
  }

  BoolExpr MsgFlagsAllowed(const Arg<int>& aFlags) const override {
    // Necko might use advanced networking features, and the sandbox
    // is relatively permissive compared to content, so this is a
    // default-allow policy.
    //
    // However, `MSG_OOB` has historically been buggy, and the way it
    // maps to TCP is notoriously broken (see RFC 6093), so it should
    // be safe to block.
    return (aFlags & MSG_OOB) == 0;
  }

  Maybe<ResultExpr> EvaluateSocketCall(int aCall,
                                       bool aHasArgs) const override {
    switch (aCall) {
      case SYS_SOCKET:
      case SYS_CONNECT:
      case SYS_BIND:
        return Some(Allow());

      // sendmsg and recvmmsg needed for HTTP3/QUIC UDP IO. Note sendmsg is
      // allowed in SandboxPolicyCommon.
      case SYS_RECVMMSG:
      // Required for the DNS Resolver thread.
      case SYS_SENDMMSG:
        if (aHasArgs) {
          Arg<int> flags(3);
          return Some(
              If(MsgFlagsAllowed(flags), Allow()).Else(InvalidSyscall()));
        }
        return Some(UnpackSocketcallOrAllow());

      case SYS_GETSOCKOPT:
      case SYS_SETSOCKOPT:
      case SYS_GETSOCKNAME:
      case SYS_GETPEERNAME:
      case SYS_SHUTDOWN:
      case SYS_ACCEPT:
      case SYS_ACCEPT4:
        return Some(Allow());

      default:
        return SandboxPolicyCommon::EvaluateSocketCall(aCall, aHasArgs);
    }
  }

  ResultExpr PrctlPolicy() const override {
    Arg<int> op(0);
    Arg<int> arg2(1);
    return Switch(op)
        .Case(PR_SET_VMA,  // Tagging of anonymous memory mappings
              If(arg2 == PR_SET_VMA_ANON_NAME, Allow()).Else(InvalidSyscall()))
        .Cases({PR_SET_NAME,      // Thread creation
                PR_SET_DUMPABLE,  // Crash reporting
                PR_SET_PTRACER},  // Debug-mode crash handling
               Allow())
#if defined(MOZ_PROFILE_GENERATE)
        .Case(PR_GET_PDEATHSIG, Allow())
#endif  // defined(MOZ_PROFILE_GENERATE)
        .Default(InvalidSyscall());
  }

  ResultExpr EvaluateSyscall(int sysno) const override {
    switch (sysno) {
      case __NR_getrusage:
        return Allow();

      case __NR_ioctl: {
        Arg<unsigned long> request(1);
        auto shifted_type = request & kIoctlTypeMask;

        // Rust's stdlib seems to use FIOCLEX instead of equivalent fcntls.
        return Switch(request)
            .Case(FIOCLEX, Allow())
            // Rust's stdlib also uses FIONBIO instead of equivalent fcntls.
            .Case(FIONBIO, Allow())
            // This is used by PR_Available in nsSocketInputStream::Available.
            .Case(FIONREAD, Allow())
            // WebRTC needs interface information (bug 1975576)
            .Cases({SIOCGIFNAME, SIOCGIFFLAGS, SIOCETHTOOL, SIOCGIWRATE},
                   Allow())
            .Default(
                // Allow anything that isn't a tty ioctl (if level < 2)
                If(BelowLevel(2) ? shifted_type != kTtyIoctls
                                 : BoolConst(false),
                   Allow())
                    .Else(SandboxPolicyCommon::EvaluateSyscall(sysno)));
      }

      CASES_FOR_fcntl: {
        Arg<int> cmd(1);
        return Switch(cmd)
            .Case(F_DUPFD_CLOEXEC, Allow())
            // Nvidia GL and fontconfig (newer versions) use fcntl file locking.
            .Case(F_SETLK, Allow())
#ifdef F_SETLK64
            .Case(F_SETLK64, Allow())
#endif
            // Pulseaudio uses F_SETLKW, as does fontconfig.
            .Case(F_SETLKW, Allow())
#ifdef F_SETLKW64
            .Case(F_SETLKW64, Allow())
#endif
            .Default(SandboxPolicyCommon::EvaluateSyscall(sysno));
      }

#ifdef DESKTOP
      // This section is borrowed from ContentSandboxPolicy
      CASES_FOR_getrlimit:
      CASES_FOR_getresuid:
      CASES_FOR_getresgid:
        return Allow();

      case __NR_prlimit64: {
        // Allow only the getrlimit() use case.  (glibc seems to use
        // only pid 0 to indicate the current process; pid == getpid()
        // is equivalent and could also be allowed if needed.)
        Arg<pid_t> pid(0);
        // This is really a const struct ::rlimit*, but Arg<> doesn't
        // work with pointers, only integer types.
        Arg<uintptr_t> new_limit(2);
        return If(AllOf(pid == 0, new_limit == 0), Allow())
            .Else(InvalidSyscall());
      }
#endif  // DESKTOP

      default:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
    }
  }
};

UniquePtr<sandbox::bpf_dsl::Policy> GetSocketProcessSandboxPolicy(
    SandboxBrokerClient* aMaybeBroker, SocketProcessSandboxParams&& aParams) {
  return UniquePtr<sandbox::bpf_dsl::Policy>(
      new SocketProcessSandboxPolicy(aMaybeBroker, std::move(aParams)));
}

class UtilitySandboxPolicy : public SandboxPolicyCommon {
 public:
  explicit UtilitySandboxPolicy(SandboxBrokerClient* aBroker) {
    mBroker = aBroker;
    mMayCreateShmem = true;
  }

  ResultExpr PrctlPolicy() const override {
    Arg<int> op(0);
    Arg<int> arg2(1);
    return Switch(op)
        .Case(PR_SET_VMA,  // Tagging of anonymous memory mappings
              If(arg2 == PR_SET_VMA_ANON_NAME, Allow()).Else(InvalidSyscall()))
        .Cases({PR_SET_NAME,        // Thread creation
                PR_SET_DUMPABLE,    // Crash reporting
                PR_SET_PTRACER,     // Debug-mode crash handling
                PR_GET_PDEATHSIG},  // PGO profiling, cf
                                    // https://reviews.llvm.org/D29954
               Allow())
        .Case(PR_CAPBSET_READ,  // libcap.so.2 loaded by libpulse.so.0
                                // queries for capabilities
              Error(EINVAL))
#if defined(MOZ_PROFILE_GENERATE)
        .Case(PR_GET_PDEATHSIG, Allow())
#endif  // defined(MOZ_PROFILE_GENERATE)
        .Default(InvalidSyscall());
  }

  ResultExpr EvaluateSyscall(int sysno) const override {
    switch (sysno) {
      case __NR_getrusage:
        return Allow();

      // Required by FFmpeg
      case __NR_get_mempolicy:
        return Allow();

      // Required by libnuma for FFmpeg
      case __NR_sched_getaffinity: {
        Arg<pid_t> pid(0);
        return If(pid == 0, Allow()).Else(Trap(SchedTrap, nullptr));
      }

      // Required by libnuma for FFmpeg
      case __NR_set_mempolicy:
        return Error(ENOSYS);

      // Pass through the common policy.
      default:
        return SandboxPolicyCommon::EvaluateSyscall(sysno);
    }
  }
};

UniquePtr<sandbox::bpf_dsl::Policy> GetUtilitySandboxPolicy(
    SandboxBrokerClient* aMaybeBroker) {
  return UniquePtr<sandbox::bpf_dsl::Policy>(
      new UtilitySandboxPolicy(aMaybeBroker));
}

}  // namespace mozilla
