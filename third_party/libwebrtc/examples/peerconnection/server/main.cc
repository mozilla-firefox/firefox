/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "examples/peerconnection/server/data_socket.h"
#include "examples/peerconnection/server/peer_channel.h"
#include "rtc_base/checks.h"

#if defined(WEBRTC_POSIX)
#include <sys/select.h>
#endif

// As of now, no components in peerconnection_server rely on WebRTC components
// that change its behavior based on a field trial, so this flag is currently
// unused. See peerconnection_client for example how this command line flag
// can be used and propagated.
ABSL_FLAG(
    std::string,
    force_fieldtrials,
    "",
    "Field trials control experimental features. This flag specifies the field "
    "trials in effect. E.g. running with "
    "--force_fieldtrials=WebRTC-FooFeature/Enabled/ "
    "will assign the group Enabled to field trial WebRTC-FooFeature. Multiple "
    "trials are separated by \"/\"");
ABSL_FLAG(int, port, 8888, "default: 8888");

static const size_t kMaxConnections = (FD_SETSIZE - 2);

void HandleBrowserRequest(DataSocket* ds, bool* quit) {
  RTC_DCHECK(ds && ds->valid());
  RTC_DCHECK(quit);

  const std::string& path = ds->request_path();

  *quit = (path.compare("/quit") == 0);

  if (*quit) {
    ds->Send("200 OK", true, "text/html", "",
             "<html><body>Quitting...</body></html>");
  } else if (ds->method() == DataSocket::OPTIONS) {
    // We'll get this when a browsers do cross-resource-sharing requests.
    // The headers to allow cross-origin script support will be set inside
    // Send.
    ds->Send("200 OK", true, "", "", "");
  } else {
    // Here we could write some useful output back to the browser depending on
    // the path.
    printf("Received an invalid request: %s\n", ds->request_path().c_str());
    ds->Send("500 Sorry", true, "text/html", "",
             "<html><body>Sorry, not yet implemented</body></html>");
  }
}

int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(
      "Example usage: ./peerconnection_server --port=8888\n");
  absl::ParseCommandLine(argc, argv);

  int port = absl::GetFlag(FLAGS_port);

  // Abort if the user specifies a port that is outside the allowed
  // range [1, 65535].
  if ((port < 1) || (port > 65535)) {
    printf("Error: %i is not a valid port.\n", port);
    return -1;
  }

  ListeningSocket listener;
  if (!listener.Create()) {
    printf("Failed to create server socket\n");
    return -1;
  } else if (!listener.Listen(port)) {
    printf("Failed to listen on server socket\n");
    return -1;
  }

  printf("Server listening on port %i\n", port);

  PeerChannel clients;
  typedef std::vector<DataSocket*> SocketArray;
  SocketArray sockets;
  bool quit = false;
  while (!quit) {
    fd_set socket_set;
    FD_ZERO(&socket_set);
    if (listener.valid())
      FD_SET(listener.socket(), &socket_set);

    for (SocketArray::iterator i = sockets.begin(); i != sockets.end(); ++i)
      FD_SET((*i)->socket(), &socket_set);

    struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};
    if (select(FD_SETSIZE, &socket_set, nullptr, nullptr, &timeout) ==
        SOCKET_ERROR) {
      printf("select failed\n");
      break;
    }

    for (SocketArray::iterator i = sockets.begin(); i != sockets.end(); ++i) {
      DataSocket* s = *i;
      bool socket_done = true;
      if (FD_ISSET(s->socket(), &socket_set)) {
        if (s->OnDataAvailable(&socket_done) && s->request_received()) {
          ChannelMember* member = clients.Lookup(s);
          if (member || PeerChannel::IsPeerConnection(s)) {
            if (!member) {
              if (s->PathEquals("/sign_in")) {
                clients.AddMember(s);
              } else {
                printf("No member found for: %s\n", s->request_path().c_str());
                s->Send("500 Error", true, "text/plain", "",
                        "Peer most likely gone.");
              }
            } else if (member->is_wait_request(s)) {
              // no need to do anything.
              socket_done = false;
            } else {
              ChannelMember* target = clients.IsTargetedRequest(s);
              if (target) {
                member->ForwardRequestToPeer(s, target);
              } else if (s->PathEquals("/sign_out")) {
                s->Send("200 OK", true, "text/plain", "", "");
              } else {
                printf("Couldn't find target for request: %s\n",
                       s->request_path().c_str());
                s->Send("500 Error", true, "text/plain", "",
                        "Peer most likely gone.");
              }
            }
          } else {
            HandleBrowserRequest(s, &quit);
            if (quit) {
              printf("Quitting...\n");
              FD_CLR(listener.socket(), &socket_set);
              listener.Close();
              clients.CloseAll();
            }
          }
        }
      } else {
        socket_done = false;
      }

      if (socket_done) {
        printf("Disconnecting socket\n");
        clients.OnClosing(s);
        RTC_DCHECK(s->valid());  // Close must not have been called yet.
        FD_CLR(s->socket(), &socket_set);
        delete (*i);
        i = sockets.erase(i);
        if (i == sockets.end())
          break;
      }
    }

    clients.CheckForTimeout();

    if (FD_ISSET(listener.socket(), &socket_set)) {
      DataSocket* s = listener.Accept();
      if (s) {
        if (sockets.size() >= kMaxConnections) {
          delete s;  // sorry, that's all we can take.
          printf("Connection limit reached\n");
        } else {
          sockets.push_back(s);
          printf("New connection...\n");
        }
      }
    }
  }

  for (SocketArray::iterator i = sockets.begin(); i != sockets.end(); ++i)
    delete (*i);
  sockets.clear();

  return 0;
}
