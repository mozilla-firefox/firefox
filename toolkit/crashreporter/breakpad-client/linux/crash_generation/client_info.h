// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CLIENT_LINUX_CRASH_GENERATION_CLIENT_INFO_H_
#define CLIENT_LINUX_CRASH_GENERATION_CLIENT_INFO_H_

#include <sys/types.h>
#if defined(MOZ_OXIDIZED_BREAKPAD)
#include "mozilla/toolkit/crashreporter/rust_minidump_writer_linux_ffi_generated.h"
#else
struct ExtraCrashData;
#endif

namespace google_breakpad {

class CrashGenerationServer;

class ClientInfo {
 public:
  ClientInfo(pid_t pid, CrashGenerationServer* crash_server, ExtraCrashData* extra_data)
    : crash_server_(crash_server),
      pid_(pid),
      extra_data_(extra_data) {}

#if defined(MOZ_OXIDIZED_BREAKPAD)
  ~ClientInfo() {
    if (extra_data_) {
      free_minidump_extra_data(extra_data_);
    }
  }
  ClientInfo(const ClientInfo&) = delete;
  ClientInfo& operator=(const ClientInfo& other) = delete;
#endif

  CrashGenerationServer* crash_server() const { return crash_server_; }
  pid_t pid() const { return pid_; }
  void set_extra_data(ExtraCrashData* extra_data) {
      extra_data_ = extra_data;
  }

  // Internal mutation is allowed so that extra data can be collected through
  // the callbacks. This pointer *cannot* outlive the CrashInfo object.
  ExtraCrashData* extra_data() const {
      return extra_data_;
  }

  // Owned by ClientEvent, don't assume its validity after the callback returns.
  // -1 if the crashed process is dumped directly.
  void set_remote_executor_fd(int fd) { remote_executor_fd_ = fd; }
  int remote_executor_fd() const { return remote_executor_fd_; }

 private:
  CrashGenerationServer* crash_server_;
  pid_t pid_;
  ExtraCrashData* extra_data_ = nullptr; // Possible extra crash data, notably for error reporting.
  int remote_executor_fd_ = -1;
};

}

#endif // CLIENT_LINUX_CRASH_GENERATION_CLIENT_INFO_H_
