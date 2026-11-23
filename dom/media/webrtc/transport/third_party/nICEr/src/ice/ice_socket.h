/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#ifndef _ice_socket_h
#define _ice_socket_h

typedef struct nr_ice_stun_ctx_ {
  int type;
#define NR_ICE_STUN_NONE    0 /* Deregistered */
#define NR_ICE_STUN_CLIENT  1
#define NR_ICE_STUN_SERVER  2
#define NR_ICE_TURN_CLIENT  3

  union {
    nr_stun_client_ctx *client;
    nr_stun_server_ctx *server;
    struct {
      nr_turn_client_ctx *turn_client;
      nr_socket *turn_sock;  /* The nr_socket_turn wrapped around
                                turn_client */
    } turn_client;
  } u;

  TAILQ_ENTRY(nr_ice_stun_ctx_) entry;
} nr_ice_stun_ctx;


typedef struct nr_ice_socket_ {
  int type;
#define NR_ICE_SOCKET_TYPE_DGRAM       1
#define NR_ICE_SOCKET_TYPE_STREAM_TURN 2
#define NR_ICE_SOCKET_TYPE_STREAM_TCP  3

  nr_socket *sock;
  nr_ice_ctx *ctx;

  nr_ice_candidate_head candidates;
  nr_ice_component *component;

  TAILQ_HEAD(nr_ice_stun_ctx_head_,nr_ice_stun_ctx_) stun_ctxs;

  nr_stun_server_ctx *stun_server;
  void *stun_server_handle;

  STAILQ_ENTRY(nr_ice_socket_) entry;
} nr_ice_socket;

typedef STAILQ_HEAD(nr_ice_socket_head_,nr_ice_socket_) nr_ice_socket_head;

int nr_ice_socket_create(struct nr_ice_ctx_ *ctx, struct nr_ice_component_ *comp, nr_socket *nsock, int type, nr_ice_socket **sockp);
void nr_ice_socket_destroy(nr_ice_socket **isock);
int nr_ice_socket_close(nr_ice_socket *isock);
int nr_ice_socket_register_stun_client(nr_ice_socket *sock, nr_stun_client_ctx *srv,void **handle);
int nr_ice_socket_register_stun_server(nr_ice_socket *sock, nr_stun_server_ctx *srv,void **handle);
int nr_ice_socket_register_turn_client(nr_ice_socket *sock, nr_turn_client_ctx *srv,nr_socket *turn_socket, void **handle);
int nr_ice_socket_deregister(nr_ice_socket *sock, void *handle);
void nr_ice_socket_failed(nr_ice_socket *sock);

#endif

