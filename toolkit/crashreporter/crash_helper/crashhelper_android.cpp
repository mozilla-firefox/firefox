/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <jni.h>

#include <android/log.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

// For DirectAuxvDumpInfo
#include "mozilla/toolkit/crashreporter/rust_minidump_writer_linux_ffi_generated.h"
#include "mozilla/crash_helper_ffi_generated.h"

#define CRASH_HELPER_LOGTAG "GeckoCrashHelper"

extern "C" JNIEXPORT jboolean JNICALL
Java_org_mozilla_gecko_crashhelper_CrashHelper_set_1breakpad_1opts(
    JNIEnv* jenv, jclass, jint breakpad_fd) {
  // Enable passing credentials on the Breakpad server socket. We'd love to do
  // it inside CrashHelper.java but the Java methods require an Android API
  // version that's too recent for us.
  const int val = 1;
  int res = setsockopt(breakpad_fd, SOL_SOCKET, SO_PASSCRED, &val, sizeof(val));
  if (res < 0) {
    return false;
  }

  return true;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_mozilla_gecko_crashhelper_CrashHelper_bind_1and_1listen(
    JNIEnv* jenv, jclass, jint listen_fd) {
  struct sockaddr_un addr = {
      .sun_family = AF_UNIX,
      .sun_path = {},
  };

  // The address' path deliberately starts with a null byte to inform the
  // kernel that this is an abstract address and not an actual file path.
  snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 2,
           "gecko-crash-helper-pipe.%d", getpid());

  int res = bind(listen_fd, (const struct sockaddr*)&addr, sizeof(addr));
  if (res < 0) {
    return false;
  }

  res = listen(listen_fd, 1);
  if (res < 0) {
    return false;
  }

  return true;
}

extern "C" JNIEXPORT void JNICALL
Java_org_mozilla_gecko_crashhelper_CrashHelper_crash_1generator(
    JNIEnv* jenv, jclass, jint client_pid, jint breakpad_fd,
    jstring minidump_path, jint listen_fd, jint server_fd) {
  // The breakpad server socket needs to be put in non-blocking mode, we do it
  // here as the Rust code that picks it up won't touch it anymore and just
  // pass it along to Breakpad.
  int flags = fcntl(breakpad_fd, F_GETFL);
  if (flags == -1) {
    __android_log_print(ANDROID_LOG_FATAL, CRASH_HELPER_LOGTAG,
                        "Unable to get the Breakpad pipe file options");
    return;
  }

  int res = fcntl(breakpad_fd, F_SETFL, flags | O_NONBLOCK);
  if (res == -1) {
    __android_log_print(ANDROID_LOG_FATAL, CRASH_HELPER_LOGTAG,
                        "Unable to set the Breakpad pipe in non-blocking mode");
    return;
  }

  const char* minidump_path_str =
      jenv->GetStringUTFChars(minidump_path, nullptr);
  crash_generator_logic_android(client_pid, breakpad_fd, minidump_path_str,
                                listen_fd, server_fd);
  jenv->ReleaseStringUTFChars(minidump_path, minidump_path_str);
}
