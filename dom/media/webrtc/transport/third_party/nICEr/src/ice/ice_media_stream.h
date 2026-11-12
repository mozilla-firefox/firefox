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



#ifndef _ice_media_stream_h
#define _ice_media_stream_h

#include "transport_addr.h"

typedef struct nr_ice_stun_server_ {
  nr_transport_addr addr;
  int id;
} nr_ice_stun_server;

int nr_ice_stun_server_get_url(const nr_ice_stun_server *server, int is_turn, char **urlp);

typedef struct nr_ice_turn_server_ {
    nr_ice_stun_server    turn_server;
    char                 *username;
    Data                 *password;
} nr_ice_turn_server;

struct nr_ice_media_stream_ {
  char *label;
  struct nr_ice_ctx_ *ctx;
  struct nr_ice_peer_ctx_ *pctx;

  struct nr_ice_media_stream_ *local_stream; /* used when this is a peer */
  int component_ct;
  nr_ice_component_head components;

  char *ufrag;    /* ICE username */
  char *pwd;    /* ICE password */
  char *r2l_user;  /* The username for incoming requests */
  char *l2r_user;  /* The username for outgoing requests */
  Data r2l_pass;   /* The password for incoming requests */
  Data l2r_pass;   /* The password for outcoming requests */
  int ice_state;
  /* The stream is being replaced by another, so it will not continue any ICE
   * processing. If this stream is connected already, traffic can continue to
   * flow for a limited time while the new stream gets ready. */
  int obsolete;
  int actually_started_checking;
  // Hold back the gathering events (per-candidate trickle, candidate
  // errors, and the gathering/gathered stream events) instead of
  // raising them as they happen. This is used by the pool stream since
  // no consumer is listening for events yet;
  // nr_ice_media_stream_flush_gathering_events replays them once there
  // is a consumer. Since only the pool stream sets it, it is also how a
  // candidate is known to be pooled.
  bool defer_gathering_events;

#define NR_ICE_MEDIA_STREAM_UNPAIRED           1
#define NR_ICE_MEDIA_STREAM_CHECKS_FROZEN      2
#define NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE      3
#define NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED   4
#define NR_ICE_MEDIA_STREAM_CHECKS_FAILED      5

  int disconnected;

#define NR_ICE_MEDIA_STREAM_CONNECTED    0
#define NR_ICE_MEDIA_STREAM_DISCONNECTED 1

  /* Copy of flags field from nr_ice_ctx */
  int flags;

  /* Copy of STUN/TURN servers from nr_ice_ctx */
  nr_ice_stun_server *stun_servers;           /* The list of stun servers */
  int stun_server_ct;
  nr_ice_turn_server *turn_servers;           /* The list of turn servers */
  int turn_server_ct;

  nr_ice_cand_pair_head check_list;
  nr_ice_cand_pair_head trigger_check_queue;
  void *timer;  /* Check list periodic timer */

/*  nr_ice_cand_pair_head valid_list; */

  STAILQ_ENTRY(nr_ice_media_stream_) entry;
};

typedef STAILQ_HEAD(nr_ice_media_stream_head_,nr_ice_media_stream_) nr_ice_media_stream_head;

int nr_ice_media_stream_create(struct nr_ice_ctx_ *ctx,const char *label,const char *ufrag,const char *pwd,int components, nr_ice_media_stream **streamp);
int nr_ice_media_stream_destroy(nr_ice_media_stream **streamp);
int nr_ice_media_stream_initialize(struct nr_ice_ctx_ *ctx, nr_ice_media_stream *stream);
void nr_ice_media_stream_defer_gathering_events(nr_ice_media_stream *stream);
int nr_ice_media_stream_flush_gathering_events(nr_ice_media_stream *stream);
int nr_ice_media_stream_get_attributes(nr_ice_media_stream *stream, char ***attrsp,int *attrctp);
int nr_ice_media_stream_get_default_candidate(nr_ice_media_stream *stream, int component, nr_ice_candidate **candp);
int nr_ice_media_stream_pair_candidates(nr_ice_peer_ctx *pctx,nr_ice_media_stream *lstream,nr_ice_media_stream *pstream);
int nr_ice_media_stream_start_checks(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream);
int nr_ice_media_stream_service_pre_answer_requests(nr_ice_peer_ctx *pctx,nr_ice_media_stream *lstream,nr_ice_media_stream *pstream, int *serviced);
int nr_ice_media_stream_unfreeze_pairs(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream);
int nr_ice_media_stream_unfreeze_pairs_foundation(nr_ice_media_stream *stream, char *foundation);
void nr_ice_media_stream_dump_state(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, int log_level);
void nr_ice_media_stream_component_nominated(nr_ice_media_stream *stream,nr_ice_component *component);
void nr_ice_media_stream_component_failed(nr_ice_media_stream *stream,nr_ice_component *component);
void nr_ice_media_stream_refresh_consent_all(nr_ice_media_stream *stream);
void nr_ice_media_stream_disconnect_all_components(nr_ice_media_stream *stream);
void nr_ice_media_stream_set_disconnected(nr_ice_media_stream *stream, int disconnected);
int nr_ice_media_stream_check_if_connected(nr_ice_media_stream *stream);
int nr_ice_media_stream_set_state(nr_ice_media_stream *str, int state);
void nr_ice_media_stream_stop_checking(nr_ice_media_stream *str);
void nr_ice_media_stream_set_obsolete(nr_ice_media_stream *str);
int nr_ice_media_stream_is_done_gathering(nr_ice_media_stream *str);
int nr_ice_media_stream_send(nr_ice_peer_ctx *pctx, nr_ice_media_stream *str, int component, UCHAR *data, int len);
int nr_ice_media_stream_get_active(nr_ice_peer_ctx *pctx, nr_ice_media_stream *str, int component, nr_ice_candidate **local, nr_ice_candidate **remote);
int nr_ice_media_stream_find_component(nr_ice_media_stream *str, int comp_id, nr_ice_component **compp);
int nr_ice_media_stream_find_pair(nr_ice_media_stream *str, nr_ice_candidate *local, nr_ice_candidate *remote, nr_ice_cand_pair **pair);
int
nr_ice_peer_ctx_parse_media_stream_attribute(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, char *attr);
int nr_ice_media_stream_get_consent_status(nr_ice_media_stream *stream, int component_id, int *can_send, struct timeval *ts);
int nr_ice_media_stream_disable_component(nr_ice_media_stream *stream, int component_id);
int nr_ice_media_stream_pair_new_trickle_candidate(nr_ice_peer_ctx *pctx, nr_ice_media_stream *pstream, nr_ice_candidate *cand);
void nr_ice_media_stream_role_change(nr_ice_media_stream *stream);

#endif

