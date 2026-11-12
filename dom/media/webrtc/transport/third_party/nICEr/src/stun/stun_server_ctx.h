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



#ifndef _stun_server_ctx_h
#define _stun_server_ctx_h

typedef struct nr_stun_server_ctx_ nr_stun_server_ctx;
typedef struct nr_stun_server_client_  nr_stun_server_client;

#include "stun_util.h"


typedef struct nr_stun_server_request_{
  nr_transport_addr src_addr;
  nr_stun_message *request;
  nr_stun_message *response;
} nr_stun_server_request;

typedef int (*nr_stun_server_cb)(void *cb_arg, struct nr_stun_server_ctx_ *ctx,nr_socket *sock,nr_stun_server_request *req, int *dont_free, int *error);

struct nr_stun_server_client_ {
  char *label;
  char *username;
  Data password;
  nr_stun_server_cb stun_server_cb;
  void *cb_arg;
  char nonce[NR_STUN_MAX_NONCE_BYTES+1];  /* +1 for \0 */
  STAILQ_ENTRY(nr_stun_server_client_) entry;
};

typedef STAILQ_HEAD(nr_stun_server_client_head_, nr_stun_server_client_) nr_stun_server_client_head;

struct nr_stun_server_ctx_ {
  char *label;
  nr_stun_server_client_head clients;
  nr_stun_server_client *default_client;
};


int nr_stun_server_ctx_create(char *label, nr_stun_server_ctx **ctxp);
int nr_stun_server_ctx_destroy(nr_stun_server_ctx **ctxp);
int nr_stun_server_add_client(nr_stun_server_ctx *ctx, char *client_label, char *user, Data *pass, nr_stun_server_cb cb, void *cb_arg);
int nr_stun_server_remove_client(nr_stun_server_ctx *ctx, void *cb_arg);
int nr_stun_server_add_default_client(nr_stun_server_ctx *ctx, char *ufrag, Data *pass, nr_stun_server_cb cb, void *cb_arg);
// Updates the credentials and callback of the default client, which is how an
// already-gathered socket is pointed at different local ICE credentials, or at
// a different component.
int nr_stun_server_ctx_update_component(nr_stun_server_ctx *ctx, char *ufrag,
                                        Data *pass, nr_stun_server_cb cb,
                                        void *cb_arg);
int nr_stun_server_process_request(nr_stun_server_ctx *ctx, nr_socket *sock, char *msg, int len, nr_transport_addr *peer_addr, int auth_rule);
int nr_stun_get_message_client(nr_stun_server_ctx *ctx, nr_stun_message *req, nr_stun_server_client **clnt);

#endif

