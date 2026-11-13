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

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "linux/crash_generation/crash_generation_server.h"
#include "linux/crash_generation/crash_generation.h"
#include "linux/crash_generation/client_info.h"
#include "linux/handler/exception_handler.h"
#include "linux/handler/guid_generator.h"
#include "linux/minidump_writer/minidump_writer.h"
#include "linux/log/log.h"
#include "common/linux/eintr_wrapper.h"
#include "common/linux/safe_readlink.h"

#if defined(MOZ_OXIDIZED_BREAKPAD)
#  include "mozilla/toolkit/crashreporter/rust_minidump_writer_linux_ffi_generated.h"
#endif // defined(MOZ_OXIDIZED_BREAKPAD)

static const char kCommandQuit = 'x';

namespace google_breakpad {

using CrashReporter::UniqueFd;
using CrashReporter::GenerationClientAction;

CrashGenerationServer::CrashGenerationServer(
  const int listen_fd,
#if defined(MOZ_OXIDIZED_BREAKPAD)
  std::function<GetAuxvInfoCallback> get_auxv_info,
#endif // defined(MOZ_OXIDIZED_BREAKPAD)
  std::function<OnClientDumpRequestCallback> dump_callback,
  void* dump_context,
  const string* dump_path) :
    server_fd_(listen_fd),
#if defined(MOZ_OXIDIZED_BREAKPAD)
    get_auxv_info_(std::move(get_auxv_info)),
    force_remote_executor_(getenv("MOZ_CRASHREPORTER_FORCE_REMOTE_EXECUTOR") != nullptr),
#endif // defined(MOZ_OXIDIZED_BREAKPAD)
    dump_callback_(std::move(dump_callback)),
    dump_context_(dump_context),
    dump_dir_mutex_(PTHREAD_MUTEX_INITIALIZER),
    started_(false)
{
  if (dump_path)
    dump_dir_ = *dump_path;
  else
    dump_dir_ = "/tmp";
}

CrashGenerationServer::~CrashGenerationServer()
{
  if (started_)
    Stop();
}

bool
CrashGenerationServer::Start()
{
  if (started_ || 0 > server_fd_)
    return false;

  int control_pipe[2];
  if (pipe(control_pipe))
    return false;

  if (fcntl(control_pipe[0], F_SETFD, FD_CLOEXEC))
    return false;
  if (fcntl(control_pipe[1], F_SETFD, FD_CLOEXEC))
    return false;

  if (fcntl(control_pipe[0], F_SETFL, O_NONBLOCK))
    return false;

  control_pipe_in_ = control_pipe[0];
  control_pipe_out_ = control_pipe[1];

  if (pthread_create(&thread_, nullptr,
                    [](void* context) -> void* {
                      reinterpret_cast<CrashGenerationServer*>(context)->Run();
                      return nullptr;
                    }, this))
    return false;

  started_ = true;
  return true;
}

void
CrashGenerationServer::Stop()
{
  assert(pthread_self() != thread_);

  if (!started_)
    return;

  HANDLE_EINTR(write(control_pipe_out_, &kCommandQuit, 1));

  void* dummy;
  pthread_join(thread_, &dummy);

  close(control_pipe_in_);
  close(control_pipe_out_);

  started_ = false;
}

void
CrashGenerationServer::SetPath(const char* dump_path)
{
  pthread_mutex_lock(&dump_dir_mutex_);
  this->dump_dir_ = string(dump_path);
  pthread_mutex_unlock(&dump_dir_mutex_);
}

//static
bool
CrashGenerationServer::CreateReportChannel(int* server_fd, int* client_fd)
{
  int fds[2];

  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds))
    return false;

  static const int on = 1;
  // Enable passcred on the server end of the socket
  if (setsockopt(fds[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)))
    return false;

  if (fcntl(fds[1], F_SETFL, O_NONBLOCK))
    return false;

  *client_fd = fds[0];
  *server_fd = fds[1];
  return true;
}

// The following methods/functions execute on the server thread

void
CrashGenerationServer::Run()
{
  struct pollfd pollfds[2];
  memset(&pollfds, 0, sizeof(pollfds));

  pollfds[0].fd = server_fd_;
  pollfds[0].events = POLLIN;

  pollfds[1].fd = control_pipe_in_;
  pollfds[1].events = POLLIN;

  while (true) {
    // infinite timeout
    int nevents = poll(pollfds, sizeof(pollfds)/sizeof(pollfds[0]), -1);
    if (-1 == nevents) {
      if (EINTR == errno) {
        continue;
      } else {
        return;
      }
    }

    if (pollfds[0].revents && !ClientEvent(pollfds[0].revents))
      return;

    if (pollfds[1].revents && !ControlEvent(pollfds[1].revents))
      return;
  }
}

static bool SendActionToClient(
    int server_endpoint,
    pid_t crashing_pid,
    GenerationClientAction action
);

bool
CrashGenerationServer::ClientEvent(short revents)
{
  if (POLLHUP & revents)
    return false;
  assert(POLLIN & revents);

  // A process has crashed and has signaled us by writing a datagram
  // to the death signal socket. The datagram contains the crash context needed
  // for writing the minidump as well as a file descriptor and a credentials
  // block so that they can't lie about their pid.

  // The length of the control message:
  static const unsigned kControlMsgSize =
      CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct ucred));
  // The length of the regular payload:
  static const unsigned kCrashContextSize =
      sizeof(google_breakpad::ExceptionHandler::CrashContext);

  struct msghdr msg = {0};
  struct iovec iov[1];
  alignas(16) char crash_context[kCrashContextSize];
  char control[kControlMsgSize];
  const ssize_t expected_msg_size = sizeof(crash_context);

  iov[0].iov_base = crash_context;
  iov[0].iov_len = sizeof(crash_context);
  msg.msg_iov = iov;
  msg.msg_iovlen = sizeof(iov)/sizeof(iov[0]);
  msg.msg_control = control;
  msg.msg_controllen = kControlMsgSize;

  const ssize_t msg_size = HANDLE_EINTR(recvmsg(server_fd_, &msg, 0));
  if (msg_size != expected_msg_size)
    return true;

  if (msg.msg_controllen != kControlMsgSize ||
      msg.msg_flags & ~MSG_TRUNC)
    return true;

  // Walk the control payload and extract the file descriptor and validated pid.
  pid_t crashing_pid = -1;
  [[maybe_unused]] uid_t crashing_uid = 0;
  UniqueFd server_endpoint;
  for (struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg); hdr;
       hdr = CMSG_NXTHDR(&msg, hdr)) {
    if (hdr->cmsg_level != SOL_SOCKET)
      continue;
    if (hdr->cmsg_type == SCM_RIGHTS) {
      const unsigned len = hdr->cmsg_len -
          (((uint8_t*)CMSG_DATA(hdr)) - (uint8_t*)hdr);
      assert(len % sizeof(int) == 0u);
      const unsigned num_fds = len / sizeof(int);
      if (num_fds > 1 || num_fds == 0) {
        // A nasty process could try and send us too many descriptors and
        // force a leak.
        for (unsigned i = 0; i < num_fds; ++i)
          close(reinterpret_cast<int*>(CMSG_DATA(hdr))[i]);
        return true;
      } else {
        server_endpoint.reset(*reinterpret_cast<int*>(CMSG_DATA(hdr)));
      }
    } else if (hdr->cmsg_type == SCM_CREDENTIALS) {
      const struct ucred *cred =
          reinterpret_cast<struct ucred*>(CMSG_DATA(hdr));
      crashing_pid = cred->pid;
      crashing_uid = cred->uid;
    }
  }

  if (crashing_pid == -1 || !server_endpoint) {
    return true;
  }

  string minidump_filename;
  if (!MakeMinidumpFilename(minidump_filename))
    return true;

#if defined(MOZ_OXIDIZED_BREAKPAD)
  ExceptionHandler::CrashContext* breakpad_cc =
      reinterpret_cast<ExceptionHandler::CrashContext*>(crash_context);
  ExtraCrashData* extra_data = nullptr;
  siginfo_t& si = breakpad_cc->siginfo;
  signalfd_siginfo signalfd_si = {};
  signalfd_si.ssi_signo = si.si_signo;
  signalfd_si.ssi_errno = si.si_errno;
  signalfd_si.ssi_code = si.si_code;
  signalfd_si.ssi_pid = si.si_pid;
  signalfd_si.ssi_uid = si.si_uid;

  switch (si.si_signo) {
    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
    case SIGBUS:
    case SIGSYS:
      signalfd_si.ssi_addr = reinterpret_cast<size_t>(si.si_addr);
      break;
  }

  UniqueFd remote_minidump_writer_endpoint;

  // Different UID usually prevents direct ptrace()
  if (force_remote_executor_ || crashing_uid != getuid()) {
    if (!SendActionToClient(
        server_endpoint.get(),
        crashing_pid,
        GenerationClientAction::ForkAndLaunchExecutor)) {
      return true;
    }

    // From this point forward, the server endpoint is done with its job.
    // It is now the endpoint for talking to the remote executor.
    remote_minidump_writer_endpoint = std::move(server_endpoint);
  }

  bool res = false;

  MinidumpWriterContext* writer = minidump_writer_create(
    minidump_filename.c_str(),
    crashing_pid,
    breakpad_cc->tid,
    &extra_data
  );
  DirectAuxvDumpInfo auxvInfo = {};
  if (writer && get_auxv_info_ && get_auxv_info_(crashing_pid, &auxvInfo)) {
    minidump_writer_set_direct_auxv_dump_info(writer, &auxvInfo);
  }
  if (writer) {
    const fpregset_t *float_state = nullptr;

#  ifndef __arm__
    float_state = reinterpret_cast<const fpregset_t *>(&breakpad_cc->float_state);
#  endif

    minidump_writer_set_crash_context(writer, &breakpad_cc->context, float_state, &signalfd_si);

    if (remote_minidump_writer_endpoint) {
      // We need to keep a copy around for the annotation reader.
      int writer_endpoint =
          fcntl(remote_minidump_writer_endpoint.get(), F_DUPFD_CLOEXEC, 0);
      if (writer_endpoint == -1) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg),
                 "dup() of the remote executor endpoint failed, fetching client"
                 "annotation will likely error out. Errno %d.\n",
                 errno);
        logger::write(err_msg, strlen(err_msg));
        writer_endpoint = remote_minidump_writer_endpoint.release();
      }
      minidump_writer_set_remote_unix_stream(writer, writer_endpoint);
    }

    res = minidump_writer_dump(writer, extra_data);
  }
#else
  if (!google_breakpad::WriteMinidump(minidump_filename.c_str(),
                                      crashing_pid, crash_context,
                                      kCrashContextSize)) {
    return true;
  }
#endif

  ClientInfo info(crashing_pid, this, extra_data);
  extra_data = nullptr; // ownership transferred to the info object.
#if defined(MOZ_OXIDIZED_BREAKPAD)
  info.set_remote_executor_fd(remote_minidump_writer_endpoint.get());
#endif

  if (dump_callback_) {
    dump_callback_(dump_context_, info, minidump_filename);
  }

  // Close the endpoint, which will release a direct client and do nothing in the case of
  // a remote client.
  server_endpoint.reset();

  return true;
}

bool
CrashGenerationServer::ControlEvent(short revents)
{
  if (POLLHUP & revents)
    return false;
  assert(POLLIN & revents);

  char command;
  if (read(control_pipe_in_, &command, 1))
    return false;

  switch (command) {
  case kCommandQuit:
    return false;
  default:
    assert(0);
  }

  return true;
}

bool
CrashGenerationServer::MakeMinidumpFilename(string& outFilename)
{
  GUID guid;
  char guidString[kGUIDStringLength+1];

  if (!(CreateGUID(&guid)
        && GUIDToString(&guid, guidString, sizeof(guidString))))
    return false;

  char path[PATH_MAX];
  pthread_mutex_lock(&dump_dir_mutex_);
  snprintf(path, sizeof(path), "%s/%s.dmp", dump_dir_.c_str(), guidString);
  pthread_mutex_unlock(&dump_dir_mutex_);

  outFilename = path;
  return true;
}

static bool SendActionToClient(
    int server_endpoint,
    pid_t crashing_pid,
    GenerationClientAction action
) {
  const uint8_t action_byte = static_cast<uint8_t>(action);

  // MSG_DONTWAIT in case the client maliciously tries to freeze us by filling their buffer
  const ssize_t rv = HANDLE_EINTR(send(
    server_endpoint, &action_byte, sizeof(action_byte), MSG_NOSIGNAL | MSG_DONTWAIT));
  if (rv == static_cast<ssize_t>(sizeof(action_byte))) {
    return true;
  }

  char err_msg[256];
  if (rv == -1) {
    const int error_code = errno;
    snprintf(err_msg, sizeof(err_msg),
      "CrashGenerationServer failed to send action %hhu to process %d. Errno %d.\n",
      action_byte,
      static_cast<int>(crashing_pid), error_code);
  } else {
    snprintf(err_msg, sizeof(err_msg),
      "CrashGenerationServer failed to send action %hhu to process %d with no error.\n",
      action_byte,
      static_cast<int>(crashing_pid));
  }

  logger::write(err_msg, strlen(err_msg));
  return false;
}

}  // namespace google_breakpad
