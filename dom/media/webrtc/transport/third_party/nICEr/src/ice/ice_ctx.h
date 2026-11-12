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



#ifndef _ice_ctx_h
#define _ice_ctx_h

/* Not good practice but making includes simpler */
#include "transport_addr.h"
#include "nr_socket.h"
#include "nr_resolver.h"
#include "nr_interface_prioritizer.h"
#include "stun_client_ctx.h"
#include "stun_server_ctx.h"
#include "turn_client_ctx.h"

typedef struct nr_ice_foundation_ {
  int index;

  nr_transport_addr addr;
  int type;
  /* ICE spec says that we only compare IP address, not port */
  nr_transport_addr stun_server_addr;

  STAILQ_ENTRY(nr_ice_foundation_) entry;
} nr_ice_foundation;

typedef STAILQ_HEAD(nr_ice_foundation_head_,nr_ice_foundation_) nr_ice_foundation_head;

typedef TAILQ_HEAD(nr_ice_candidate_head_,nr_ice_candidate_) nr_ice_candidate_head;
typedef TAILQ_HEAD(nr_ice_cand_pair_head_,nr_ice_cand_pair_) nr_ice_cand_pair_head;
typedef struct nr_ice_component_ nr_ice_component;
typedef struct nr_ice_media_stream_ nr_ice_media_stream;
typedef struct nr_ice_ctx_ nr_ice_ctx;
typedef struct nr_ice_peer_ctx_ nr_ice_peer_ctx;
typedef struct nr_ice_candidate_ nr_ice_candidate;
typedef struct nr_ice_cand_pair_ nr_ice_cand_pair;
typedef void (*nr_ice_trickle_candidate_cb) (void *cb_arg,
  nr_ice_ctx *ctx, nr_ice_media_stream *stream, int component_id,
  nr_ice_candidate *candidate);

#include "ice_socket.h"
#include "ice_component.h"
#include "ice_media_stream.h"
#include "ice_candidate.h"
#include "ice_candidate_pair.h"
#include "ice_handler.h"
#include "ice_peer_ctx.h"

typedef struct nr_ice_stun_id_ {
  UCHAR id[12];

  STAILQ_ENTRY(nr_ice_stun_id_) entry;
} nr_ice_stun_id;

typedef STAILQ_HEAD(nr_ice_stun_id_head_,nr_ice_stun_id_) nr_ice_stun_id_head;

typedef struct nr_ice_stats_ {
  UINT2 stun_retransmits;
  UINT2 turn_401s;
  UINT2 turn_403s;
  UINT2 turn_438s;
} nr_ice_stats;

typedef struct nr_ice_gather_handler_vtbl_ {
  /* This media stream is gathering */
  int (*stream_gathering)(void* obj, nr_ice_media_stream* stream);

  /* This media stream has finished gathering */
  int (*stream_gathered)(void* obj, nr_ice_media_stream* stream);

  /* A candidate failed to gather (e.g. STUN/TURN error) */
  int (*candidate_error)(void* obj, nr_ice_media_stream* stream,
                         nr_ice_candidate* candidate);
} nr_ice_gather_handler_vtbl;

typedef struct nr_ice_gather_handler_ {
  void* obj;
  nr_ice_gather_handler_vtbl* vtbl;
} nr_ice_gather_handler;

struct nr_ice_ctx_ {
  UINT4 flags;
  char *label;
  nr_ice_gather_handler* gather_handler;

  UINT4 Ta;

  nr_ice_stun_server *stun_servers_cfg;           /* The list of stun servers */
  int stun_server_ct_cfg;
  nr_ice_turn_server *turn_servers_cfg;           /* The list of turn servers */
  int turn_server_ct_cfg;
  nr_local_addr *local_addrs;                 /* The list of available local addresses and corresponding interface information */
  int local_addr_ct;

  nr_resolver *resolver;                      /* The resolver to use */
  nr_interface_prioritizer *interface_prioritizer;  /* Priority decision logic */
  nr_socket_factory *socket_factory;

  nr_ice_foundation_head foundations;

  nr_ice_media_stream_head streams;           /* Media streams */
  int stream_ct;
  // The candidate pool: components that have gathered ahead of the streams
  // that will use them, held in a stream of their own so that gathering has
  // the stream context it expects. It is deliberately not in ->streams; it is
  // not an ICE stream in its own right and takes no part in ICE processing.
  nr_ice_media_stream *pool_stream;
  nr_ice_socket_head sockets;                 /* The sockets we're using */
  // Candidates of the streams, still being initialized. Answers "is this ICE
  // agent still gathering for the streams it has", so the pool is counted
  // separately: an entry nothing has taken yet is not something anyone is
  // waiting for. A candidate that moves out of the pool while it is still
  // initializing moves between the two.
  int uninitialized_candidates;
  int uninitialized_pool_candidates;

  UINT4 gather_rto;
  UINT4 stun_delay;

  UINT4 test_timer_divider;

  nr_ice_peer_ctx_head peers;
  nr_ice_stun_id_head ids;

  NR_async_cb gather_done_cb;
  void *cb_arg;

  nr_ice_trickle_candidate_cb trickle_cb;
  void *trickle_cb_arg;

  char force_net_interface[MAXIFNAME];
  nr_ice_stats stats;

  nr_transport_addr *target_for_default_local_address_lookup;
};

int nr_ice_ctx_create(char* label, UINT4 flags,
                      nr_ice_gather_handler* gather_handler, nr_ice_ctx** ctxp);
int nr_ice_ctx_create_with_credentials(char *label, UINT4 flags, char* ufrag, char* pwd, nr_ice_ctx **ctxp);
#define NR_ICE_CTX_FLAGS_AGGRESSIVE_NOMINATION             (1)
#define NR_ICE_CTX_FLAGS_LITE                              (1<<1)
#define NR_ICE_CTX_FLAGS_RELAY_ONLY                        (1<<2)
#define NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES           (1<<3)
#define NR_ICE_CTX_FLAGS_ONLY_DEFAULT_ADDRS                (1<<4)
#define NR_ICE_CTX_FLAGS_ONLY_PROXY                        (1<<5)
#define NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES          (1<<6)
#define NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK (1 << 7)
#define NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL (1 << 8)

void nr_ice_ctx_add_flags(nr_ice_ctx *ctx, UINT4 flags);
void nr_ice_ctx_remove_flags(nr_ice_ctx *ctx, UINT4 flags);
void nr_ice_ctx_destroy(nr_ice_ctx** ctxp);
int nr_ice_set_local_addresses(nr_ice_ctx *ctx, nr_local_addr* stun_addrs, int stun_addr_ct);
int nr_ice_set_target_for_default_local_address_lookup(nr_ice_ctx *ctx, const char *target_ip, UINT2 target_port);
int nr_ice_gather(nr_ice_ctx* ctx, NR_async_cb gather_done_cb, void* cb_arg);
// Change the pool to hold |size| candidate sets, keeping what it already
// gathered: only the sets added by a growth gather, and only the sets beyond
// |size| are released by a shrink. |size| is capped at
// NR_ICE_MAX_CANDIDATE_POOL_SIZE.
#define NR_ICE_MAX_CANDIDATE_POOL_SIZE 10
int nr_ice_ctx_resize_candidate_pool(nr_ice_ctx *ctx, uint8_t size,
                                     const char *ufrag, const char *pwd);
// Release whatever is left in the pool, closing its sockets.
void nr_ice_ctx_empty_candidate_pool(nr_ice_ctx *ctx);
// How many pre-gathered candidate sets are still available.
uint8_t nr_ice_ctx_candidate_pool_size(nr_ice_ctx *ctx);
struct nr_ice_component_ *nr_ice_ctx_take_pool_component(nr_ice_ctx *ctx);
int nr_ice_add_candidate(nr_ice_ctx *ctx, nr_ice_candidate *cand);
void nr_ice_gather_finished_cb(NR_SOCKET s, int h, void *cb_arg);
int nr_ice_add_media_stream(nr_ice_ctx *ctx,const char *label,const char *ufrag,const char *pwd,int components, nr_ice_media_stream **streamp);
int nr_ice_remove_media_stream(nr_ice_ctx *ctx,nr_ice_media_stream **streamp);
int nr_ice_get_global_attributes(nr_ice_ctx *ctx,char ***attrsp, int *attrctp);
int nr_ice_ctx_deliver_packet(nr_ice_ctx *ctx, nr_ice_component *comp, nr_transport_addr *source_addr, UCHAR *data, int len);
int nr_ice_ctx_is_known_id(nr_ice_ctx *ctx, UCHAR id[12]);
int nr_ice_ctx_remember_id(nr_ice_ctx *ctx, nr_stun_message *msg);
int nr_ice_ctx_set_stun_servers(nr_ice_ctx *ctx,nr_ice_stun_server *servers, int ct);
int nr_ice_ctx_set_turn_servers(nr_ice_ctx *ctx,nr_ice_turn_server *servers, int ct);
int nr_ice_ctx_set_resolver(nr_ice_ctx *ctx, nr_resolver *resolver);
int nr_ice_ctx_set_interface_prioritizer(nr_ice_ctx *ctx, nr_interface_prioritizer *prioritizer);
void nr_ice_ctx_set_socket_factory(nr_ice_ctx *ctx, nr_socket_factory *factory);
int nr_ice_ctx_set_trickle_cb(nr_ice_ctx *ctx, nr_ice_trickle_candidate_cb cb, void *cb_arg);
int nr_ice_ctx_hide_candidate(nr_ice_ctx *ctx, nr_ice_candidate *cand);

#define NR_ICE_MAX_ATTRIBUTE_SIZE 256

extern int LOG_ICE;

#endif

