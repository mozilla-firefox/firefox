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

#ifndef CLIENT_LINUX_CRASH_GENERATION_CRASH_GENERATION_SERVER_H_
#define CLIENT_LINUX_CRASH_GENERATION_CRASH_GENERATION_SERVER_H_

#include <pthread.h>

#include <array>
#include <functional>
#include <string>

#include "common/using_std_string.h"

#if defined(MOZ_OXIDIZED_BREAKPAD)
struct DirectAuxvDumpInfo;
#endif // defined(MOZ_OXIDIZED_BREAKPAD)

namespace google_breakpad {

class ClientInfo;

class CrashGenerationServer {
public:
  // WARNING: callbacks may be invoked on a different thread
  // than that which creates the CrashGenerationServer.  They must
  // be thread safe.
  using OnClientDumpRequestCallback = void (void* dump_context,
                                            const ClientInfo& client_info,
                                            const string& file_path);
#if defined(MOZ_OXIDIZED_BREAKPAD)
  using GetAuxvInfoCallback = bool (pid_t pid, DirectAuxvDumpInfo*);
#endif // defined(MOZ_OXIDIZED_BREAKPAD)

  // Create an instance with the given parameters.
  //
  // Parameter listen_fd: The server fd created by CreateReportChannel().
  // Parameter: get_auxv_info: Callback to retrieve the stored auxiliary vector for the given PID.
  // Parameter dump_callback: Callback for a client crash dump request.
  // Parameter dump_context: Context for client crash dump request callback.
  //     Client code of this class might want to generate dumps explicitly
  //     in the crash dump request callback. In that case, false can be
  //     passed for this parameter.
  // Parameter dump_path: Path for generating dumps; required only if true is
  //     passed for generateDumps parameter; NULL can be passed otherwise.
  CrashGenerationServer(const int listen_fd,
#if defined(MOZ_OXIDIZED_BREAKPAD)
                        std::function<GetAuxvInfoCallback> get_auxv_info,
#endif // defined(MOZ_OXIDIZED_BREAKPAD)
                        std::function<OnClientDumpRequestCallback> dump_callback,
                        void* dump_context,
                        const string* dump_path);

  ~CrashGenerationServer();

  // Perform initialization steps needed to start listening to clients.
  //
  // Return true if initialization is successful; false otherwise.
  bool Start();

  // Stop the server.
  void Stop();

  // Adjust the path where minidumps are placed, this is thread-safe
  void SetPath(const char* dump_path);

  // Create a "channel" that can be used by clients to report crashes
  // to a CrashGenerationServer.  |*server_fd| should be passed to
  // this class's constructor, and |*client_fd| should be passed to
  // the ExceptionHandler constructor in the client process.
  static bool CreateReportChannel(int* server_fd, int* client_fd);

  CrashGenerationServer(CrashGenerationServer&&) = delete;
  CrashGenerationServer& operator=(CrashGenerationServer&&) = delete;

  CrashGenerationServer(const CrashGenerationServer&) = delete;
  CrashGenerationServer& operator=(const CrashGenerationServer&) = delete;

private:
  // Run the server's event loop
  void Run();

  // Invoked when an child process (client) event occurs
  // Returning true => "keep running", false => "exit loop"
  bool ClientEvent(short revents);

  // Invoked when the controlling thread (main) event occurs
  // Returning true => "keep running", false => "exit loop"
  bool ControlEvent(short revents);

  // Return a unique filename at which a minidump can be written
  bool MakeMinidumpFilename(string& outFilename);

  int server_fd_;

#if defined(MOZ_OXIDIZED_BREAKPAD)
  std::function<GetAuxvInfoCallback> get_auxv_info_;
#endif // defined(MOZ_OXIDIZED_BREAKPAD)

  std::function<OnClientDumpRequestCallback> dump_callback_;
  void* dump_context_;

  pthread_mutex_t dump_dir_mutex_;
  string dump_dir_;

  bool started_;

  pthread_t thread_;
  int control_pipe_in_;
  int control_pipe_out_;
};

} // namespace google_breakpad

#endif // CLIENT_LINUX_CRASH_GENERATION_CRASH_GENERATION_SERVER_H_
