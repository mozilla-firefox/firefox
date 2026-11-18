/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsProfileLock.h"
#include "nsCOMPtr.h"
#include "nsQueryObject.h"
#include "nsString.h"
#include "nsPrintfCString.h"
#include "nsDebug.h"

#if defined(XP_WIN)
#  include "ProfileUnlockerWin.h"
#endif

#if defined(XP_MACOSX)
#  include <Carbon/Carbon.h>
#  include <CoreFoundation/CoreFoundation.h>
#endif

#if defined(MOZ_WIDGET_ANDROID)
#  include "ProfileUnlockerAndroid.h"
#endif

#ifdef XP_UNIX
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <signal.h>
#  include <stdlib.h>
#  include "prnetdb.h"
#  include "prsystem.h"
#  include "prenv.h"
#  include "mozilla/Printf.h"
#endif

// **********************************************************************
// class nsProfileLock
//
// This code was moved from profile/src/nsProfileAccess.
// **********************************************************************

#if defined(XP_UNIX)
static bool sDisableSignalHandling = false;
#endif

nsProfileLock::nsProfileLock()
    : mHaveLock(false),
      mReplacedLockTime(0)
#if defined(XP_WIN)
      ,
      mLockFileHandle(INVALID_HANDLE_VALUE)
#elif defined(XP_UNIX)
      ,
      mPidLockFileName(nullptr),
      mLockFileDesc(-1)
#endif
{
#if defined(XP_UNIX)
  next = prev = this;
  sDisableSignalHandling = PR_GetEnv("MOZ_DISABLE_SIG_HANDLER") ? true : false;
#endif
}

nsProfileLock& nsProfileLock::operator=(nsProfileLock& rhs) {
  Unlock();

  mLockFile = rhs.mLockFile;
  rhs.mLockFile = nullptr;
  mHaveLock = rhs.mHaveLock;
  rhs.mHaveLock = false;
  mReplacedLockTime = rhs.mReplacedLockTime;

#if defined(XP_WIN)
  mLockFileHandle = rhs.mLockFileHandle;
  rhs.mLockFileHandle = INVALID_HANDLE_VALUE;
#elif defined(XP_UNIX)
  mLockFileDesc = rhs.mLockFileDesc;
  rhs.mLockFileDesc = -1;
  mPidLockFileName = rhs.mPidLockFileName;
  rhs.mPidLockFileName = nullptr;
  if (mPidLockFileName) {
    // rhs had a symlink lock, therefore it was on the list.
    PR_REMOVE_LINK(&rhs);
    PR_APPEND_LINK(this, &mPidLockList);
  }
#endif

  return *this;
}

nsProfileLock::~nsProfileLock() {
  Unlock();
  // Note that we don't clean up by default here so on next startup we know when
  // the profile was last used based on the modification time of the lock file.
}

#if defined(XP_UNIX)

static int setupPidLockCleanup;

PRCList nsProfileLock::mPidLockList =
    PR_INIT_STATIC_CLIST(&nsProfileLock::mPidLockList);

void nsProfileLock::RemovePidLockFiles(bool aFatalSignal) {
  while (!PR_CLIST_IS_EMPTY(&mPidLockList)) {
    nsProfileLock* lock = static_cast<nsProfileLock*>(mPidLockList.next);
    lock->Unlock(aFatalSignal);
  }
}

static struct sigaction SIGHUP_oldact;
static struct sigaction SIGINT_oldact;
static struct sigaction SIGQUIT_oldact;
static struct sigaction SIGILL_oldact;
static struct sigaction SIGABRT_oldact;
static struct sigaction SIGSEGV_oldact;
static struct sigaction SIGTERM_oldact;

void nsProfileLock::FatalSignalHandler(int signo
#  ifdef SA_SIGINFO
                                       ,
                                       siginfo_t* info, void* context
#  endif
) {
  // Remove any locks still held.
  RemovePidLockFiles(true);

  // Chain to the old handler, which may exit.
  struct sigaction* oldact = nullptr;

  switch (signo) {
    case SIGHUP:
      oldact = &SIGHUP_oldact;
      break;
    case SIGINT:
      oldact = &SIGINT_oldact;
      break;
    case SIGQUIT:
      oldact = &SIGQUIT_oldact;
      break;
    case SIGILL:
      oldact = &SIGILL_oldact;
      break;
    case SIGABRT:
      oldact = &SIGABRT_oldact;
      break;
    case SIGSEGV:
      oldact = &SIGSEGV_oldact;
      break;
    case SIGTERM:
      oldact = &SIGTERM_oldact;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("bad signo");
      break;
  }

  if (oldact) {
    if (oldact->sa_handler == SIG_DFL) {
      // Make sure the default sig handler is executed
      // We need it to get Mozilla to dump core.
      sigaction(signo, oldact, nullptr);

      // Now that we've restored the default handler, unmask the
      // signal and invoke it.

      sigset_t unblock_sigs;
      sigemptyset(&unblock_sigs);
      sigaddset(&unblock_sigs, signo);

      sigprocmask(SIG_UNBLOCK, &unblock_sigs, nullptr);

      raise(signo);
    }
#  ifdef SA_SIGINFO
    else if (oldact->sa_sigaction &&
             (oldact->sa_flags & SA_SIGINFO) == SA_SIGINFO) {
      oldact->sa_sigaction(signo, info, context);
    }
#  endif
    else if (oldact->sa_handler && oldact->sa_handler != SIG_IGN) {
      oldact->sa_handler(signo);
    }
  }

  // Backstop exit call, just in case.
  _exit(signo);
}

nsresult nsProfileLock::LockWithFcntl(nsIFile* aLockFile,
                                      nsIProfileUnlocker** aUnlocker) {
  nsresult rv = NS_OK;

  nsAutoCString lockFilePath;
  rv = aLockFile->GetNativePath(lockFilePath);
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not get native path");
    return rv;
  }

  aLockFile->GetLastModifiedTime(&mReplacedLockTime);

  mLockFileDesc = open(lockFilePath.get(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (mLockFileDesc != -1) {
    struct flock lock;
    lock.l_start = 0;
    lock.l_len = 0;  // len = 0 means entire file
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    // If fcntl(F_GETLK) fails then the server does not support/allow fcntl(),
    // return failure rather than access denied in this case so we fallback
    // to using a symlink lock, bug 303633.
    struct flock testlock = lock;
    if (fcntl(mLockFileDesc, F_GETLK, &testlock) == -1) {
      close(mLockFileDesc);
      mLockFileDesc = -1;
      rv = NS_ERROR_FAILURE;
    } else if (fcntl(mLockFileDesc, F_SETLK, &lock) == -1) {
#  ifdef MOZ_WIDGET_ANDROID
      MOZ_ASSERT(aUnlocker);
      RefPtr<mozilla::ProfileUnlockerAndroid> unlocker(
          new mozilla::ProfileUnlockerAndroid(testlock.l_pid));
      nsCOMPtr<nsIProfileUnlocker> unlockerInterface(do_QueryObject(unlocker));
      unlockerInterface.forget(aUnlocker);
#  endif

      close(mLockFileDesc);
      mLockFileDesc = -1;

      // With OS X, on NFS, errno == ENOTSUP
      // XXX Check for that and return specific rv for it?
#  ifdef DEBUG
      printf("fcntl(F_SETLK) failed. errno = %d\n", errno);
#  endif
      if (errno == EAGAIN || errno == EACCES)
        rv = NS_ERROR_FILE_ACCESS_DENIED;
      else
        rv = NS_ERROR_FAILURE;
    }
  } else {
    NS_WARNING("Failed to open lock file.");
    rv = NS_ERROR_FAILURE;
  }
  return rv;
}

static bool IsSymlinkStaleLock(struct in_addr* aAddr, const char* aFileName,
                               bool aHaveFcntlLock) {
  // the link exists; see if it's from this machine, and if
  // so if the process is still active
  char buf[1024];
  int len = readlink(aFileName, buf, sizeof buf - 1);
  if (len > 0) {
    buf[len] = '\0';
    char* colon = strchr(buf, ':');
    if (colon) {
      *colon++ = '\0';
      unsigned long addr = inet_addr(buf);
      if (addr != (unsigned long)-1) {
        if (colon[0] == '+' && aHaveFcntlLock) {
          // This lock was placed by a Firefox build which would have
          // taken the fnctl lock, and we've already taken the fcntl lock,
          // so the process that created this obsolete lock must be gone
          return true;
        }

        char* after = nullptr;
        pid_t pid = strtol(colon, &after, 0);
        if (pid != 0 && *after == '\0') {
          if (addr != aAddr->s_addr) {
            // Remote lock: give up even if stuck.
            return false;
          }

          // kill(pid,0) is a neat trick to check if a
          // process exists
          if (kill(pid, 0) == 0 || errno != ESRCH) {
            // Local process appears to be alive, ass-u-me it
            // is another Mozilla instance, or a compatible
            // derivative, that's currently using the profile.
            // XXX need an "are you Mozilla?" protocol
            return false;
          }
        }
      }
    }
  }
  return true;
}

nsresult nsProfileLock::LockWithSymlink(nsIFile* aLockFile,
                                        bool aHaveFcntlLock) {
  nsresult rv;
  nsAutoCString lockFilePath;
  rv = aLockFile->GetNativePath(lockFilePath);
  if (NS_FAILED(rv)) {
    NS_ERROR("Could not get native path");
    return rv;
  }

  // don't replace an existing lock time if fcntl already got one
  if (!mReplacedLockTime)
    aLockFile->GetLastModifiedTimeOfLink(&mReplacedLockTime);

  struct in_addr inaddr;
  inaddr.s_addr = htonl(INADDR_LOOPBACK);

  // We still have not loaded the profile, so we may not have proxy information.
  // Avoiding a DNS lookup in this stage makes sure any proxy is not bypassed.
  // By default, the lookup is enabled, but when it is not, we use 127.0.0.1
  // for the IP address portion of the lock signature.
  // However, this may cause the browser to refuse to start in the rare case
  // that all of the following conditions are met:
  //   1. The browser profile is on a network file system.
  //   2. The file system does not support fcntl() locking.
  //   3. The browser is run from two different computers at the same time.
#  ifndef MOZ_PROXY_BYPASS_PROTECTION
  char hostname[256];
  PRStatus status = PR_GetSystemInfo(PR_SI_HOSTNAME, hostname, sizeof hostname);
  if (status == PR_SUCCESS) {
    char netdbbuf[PR_NETDB_BUF_SIZE];
    PRHostEnt hostent;
    status = PR_GetHostByName(hostname, netdbbuf, sizeof netdbbuf, &hostent);
    if (status == PR_SUCCESS) memcpy(&inaddr, hostent.h_addr, sizeof inaddr);
  }
#  endif

  mozilla::SmprintfPointer signature =
      mozilla::Smprintf("%s:%s%lu", inet_ntoa(inaddr),
                        aHaveFcntlLock ? "+" : "", (unsigned long)getpid());
  const char* fileName = lockFilePath.get();
  int symlink_rv, symlink_errno = 0, tries = 0;

  // use ns4.x-compatible symlinks if the FS supports them
  while ((symlink_rv = symlink(signature.get(), fileName)) < 0) {
    symlink_errno = errno;
    if (symlink_errno != EEXIST) break;

    if (!IsSymlinkStaleLock(&inaddr, fileName, aHaveFcntlLock)) break;

    // Lock seems to be bogus: try to claim it.  Give up after a large
    // number of attempts (100 comes from the 4.x codebase).
    (void)unlink(fileName);
    if (++tries > 100) break;
  }

  if (symlink_rv == 0) {
    // We exclusively created the symlink: record its name for eventual
    // unlock-via-unlink.
    rv = NS_OK;
    mPidLockFileName = strdup(fileName);
    if (mPidLockFileName) {
      PR_APPEND_LINK(this, &mPidLockList);
      if (!setupPidLockCleanup++) {
        // Clean up on normal termination.
        // This instanciates a dummy class, and will trigger the class
        // destructor when libxul is unloaded. This is equivalent to atexit(),
        // but gracefully handles dlclose().
        static RemovePidLockFilesExiting r;

        // Clean up on abnormal termination, using POSIX sigaction.
        // Don't arm a handler if the signal is being ignored, e.g.,
        // because mozilla is run via nohup.
        if (!sDisableSignalHandling) {
          struct sigaction act, oldact;
#  ifdef SA_SIGINFO
          act.sa_sigaction = FatalSignalHandler;
          act.sa_flags = SA_SIGINFO | SA_ONSTACK;
#  else
          act.sa_handler = FatalSignalHandler;
#  endif
          sigfillset(&act.sa_mask);

#  define CATCH_SIGNAL(signame)                      \
    PR_BEGIN_MACRO                                   \
    if (sigaction(signame, nullptr, &oldact) == 0 && \
        oldact.sa_handler != SIG_IGN) {              \
      sigaction(signame, &act, &signame##_oldact);   \
    }                                                \
    PR_END_MACRO

          CATCH_SIGNAL(SIGHUP);
          CATCH_SIGNAL(SIGINT);
          CATCH_SIGNAL(SIGQUIT);
          CATCH_SIGNAL(SIGILL);
          CATCH_SIGNAL(SIGABRT);
          CATCH_SIGNAL(SIGSEGV);
          CATCH_SIGNAL(SIGTERM);

#  undef CATCH_SIGNAL
        }
      }
    }
  } else if (symlink_errno == EEXIST)
    rv = NS_ERROR_FILE_ACCESS_DENIED;
  else {
#  ifdef DEBUG
    printf("symlink() failed. errno = %d\n", errno);
#  endif
    rv = NS_ERROR_FAILURE;
  }
  return rv;
}
#endif /* XP_UNIX */

nsresult nsProfileLock::GetReplacedLockTime(PRTime* aResult) {
  *aResult = mReplacedLockTime;
  return NS_OK;
}

#if defined(XP_MACOSX)
constexpr auto LOCKFILE_NAME = u".parentlock"_ns;
constexpr auto OLD_LOCKFILE_NAME = u"parent.lock"_ns;
#elif defined(XP_UNIX)
constexpr auto OLD_LOCKFILE_NAME = u"lock"_ns;
constexpr auto LOCKFILE_NAME = u".parentlock"_ns;
#else
constexpr auto LOCKFILE_NAME = u"parent.lock"_ns;
#endif

bool nsProfileLock::IsMaybeLockFile(nsIFile* aFile) {
  nsAutoString tmp;
  if (NS_SUCCEEDED(aFile->GetLeafName(tmp))) {
    if (tmp.Equals(LOCKFILE_NAME)) return true;
#if (defined(XP_MACOSX) || defined(XP_UNIX))
    if (tmp.Equals(OLD_LOCKFILE_NAME)) return true;
#endif
  }
  return false;
}

nsresult nsProfileLock::Lock(nsIFile* aProfileDir,
                             nsIProfileUnlocker** aUnlocker) {
  nsresult rv;
  if (aUnlocker) *aUnlocker = nullptr;

  NS_ENSURE_STATE(!mHaveLock);

  bool isDir;
  rv = aProfileDir->IsDirectory(&isDir);
  if (NS_FAILED(rv)) return rv;
  if (!isDir) return NS_ERROR_FILE_NOT_DIRECTORY;

  nsCOMPtr<nsIFile> lockFile;
  rv = aProfileDir->Clone(getter_AddRefs(lockFile));
  if (NS_FAILED(rv)) return rv;

  rv = lockFile->Append(LOCKFILE_NAME);
  if (NS_FAILED(rv)) return rv;

  // Remember the name we're using so we can clean up
  rv = lockFile->Clone(getter_AddRefs(mLockFile));
  if (NS_FAILED(rv)) return rv;

#if defined(XP_DARWIN)
  // First, try locking using fcntl. It is more reliable on
  // a local machine, but may not be supported by an NFS server.

  rv = LockWithFcntl(lockFile);
  if (NS_FAILED(rv) && (rv != NS_ERROR_FILE_ACCESS_DENIED)) {
    // If that failed for any reason other than NS_ERROR_FILE_ACCESS_DENIED,
    // assume we tried an NFS that does not support it. Now, try with symlink.
    rv = LockWithSymlink(lockFile, false);
  }
#elif defined(XP_UNIX)
  // Get the old lockfile name
  nsCOMPtr<nsIFile> oldLockFile;
  rv = aProfileDir->Clone(getter_AddRefs(oldLockFile));
  if (NS_FAILED(rv)) return rv;
  rv = oldLockFile->Append(OLD_LOCKFILE_NAME);
  if (NS_FAILED(rv)) return rv;

  // First, try locking using fcntl. It is more reliable on
  // a local machine, but may not be supported by an NFS server.
  rv = LockWithFcntl(lockFile, aUnlocker);
  if (NS_SUCCEEDED(rv)) {
    // Check to see whether there is a symlink lock held by an older
    // Firefox build, and also place our own symlink lock --- but
    // mark it "obsolete" so that other newer builds can break the lock
    // if they obtain the fcntl lock
    rv = LockWithSymlink(oldLockFile, true);

    // If the symlink failed for some reason other than it already
    // exists, then something went wrong e.g. the file system
    // doesn't support symlinks, or we don't have permission to
    // create a symlink there.  In such cases we should just
    // continue because it's unlikely there is an old build
    // running with a symlink there and we've already successfully
    // placed a fcntl lock.
    if (rv != NS_ERROR_FILE_ACCESS_DENIED) rv = NS_OK;
  } else if (rv != NS_ERROR_FILE_ACCESS_DENIED) {
    // If that failed for any reason other than NS_ERROR_FILE_ACCESS_DENIED,
    // assume we tried an NFS that does not support it. Now, try with symlink
    // using the old symlink path
    rv = LockWithSymlink(oldLockFile, false);
  }

#elif defined(XP_WIN)
  nsAutoString filePath;
  rv = lockFile->GetPath(filePath);
  if (NS_FAILED(rv)) return rv;

  lockFile->GetLastModifiedTime(&mReplacedLockTime);

  // always create the profile lock and never delete it so we can use its
  // modification timestamp to detect startup crashes
  mLockFileHandle = CreateFileW(filePath.get(), GENERIC_READ | GENERIC_WRITE,
                                0,  // no sharing - of course
                                nullptr, CREATE_ALWAYS, 0, nullptr);
  if (mLockFileHandle == INVALID_HANDLE_VALUE) {
    if (aUnlocker) {
      RefPtr<mozilla::ProfileUnlockerWin> unlocker(
          new mozilla::ProfileUnlockerWin(filePath));
      if (NS_SUCCEEDED(unlocker->Init())) {
        nsCOMPtr<nsIProfileUnlocker> unlockerInterface(
            do_QueryObject(unlocker));
        unlockerInterface.forget(aUnlocker);
      }
    }
    return NS_ERROR_FILE_ACCESS_DENIED;
  }
#endif

  if (NS_SUCCEEDED(rv)) mHaveLock = true;

  return rv;
}

nsresult nsProfileLock::Unlock(bool aFatalSignal) {
  nsresult rv = NS_OK;

#pragma GCC diagnostic push
/* Bug 1952241: Ignore uninitialized warning for mHaveLock.
   Seems to be a false positive with gcc */
#pragma GCC diagnostic ignored "-Wuninitialized"
  if (mHaveLock) {
#pragma GCC diagnostic pop
#if defined(XP_WIN)
    if (mLockFileHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(mLockFileHandle);
      mLockFileHandle = INVALID_HANDLE_VALUE;
    }
#elif defined(XP_UNIX)
    if (mPidLockFileName) {
      PR_REMOVE_LINK(this);
      (void)unlink(mPidLockFileName);

      // Only free mPidLockFileName if we're not in the fatal signal
      // handler.  The problem is that a call to free() might be the
      // cause of this fatal signal.  If so, calling free() might cause
      // us to wait on the malloc implementation's lock.  We're already
      // holding this lock, so we'll deadlock. See bug 522332.
      if (!aFatalSignal) free(mPidLockFileName);
      mPidLockFileName = nullptr;
    }
    if (mLockFileDesc != -1) {
      close(mLockFileDesc);
      mLockFileDesc = -1;
      // Don't remove it
    }
#endif

    mHaveLock = false;
  }

  return rv;
}

nsresult nsProfileLock::Cleanup() {
  if (mHaveLock) {
    return NS_ERROR_FILE_IS_LOCKED;
  }

  if (mLockFile) {
    nsresult rv = mLockFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
    mLockFile = nullptr;
  }

  return NS_OK;
}
