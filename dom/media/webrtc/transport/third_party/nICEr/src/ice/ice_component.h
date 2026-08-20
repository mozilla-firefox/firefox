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



#ifndef _ice_component_h
#define _ice_component_h

typedef struct nr_ice_pre_answer_request_ {
  nr_stun_server_request req;
  char *username;
  nr_transport_addr local_addr;

  STAILQ_ENTRY(nr_ice_pre_answer_request_) entry;
} nr_ice_pre_answer_request;

typedef STAILQ_HEAD(nr_ice_pre_answer_request_head_, nr_ice_pre_answer_request_) nr_ice_pre_answer_request_head;

struct nr_ice_component_ {
  int state;
#define NR_ICE_COMPONENT_UNPAIRED           0
#define NR_ICE_COMPONENT_RUNNING            1
#define NR_ICE_COMPONENT_NOMINATED          2
#define NR_ICE_COMPONENT_FAILED             3
#define NR_ICE_COMPONENT_DISABLED           4
  struct nr_ice_ctx_ *ctx;
  struct nr_ice_media_stream_ *stream;
  nr_ice_component *local_component;

  int component_id;
  nr_ice_socket_head sockets;
  nr_ice_candidate_head candidates;
  int candidate_ct;
  nr_ice_pre_answer_request_head pre_answer_reqs;

  int valid_pairs;
  struct nr_ice_cand_pair_ *nominated; /* Highest priority nomninated pair */
  struct nr_ice_cand_pair_ *active;

  nr_stun_client_ctx *consent_ctx;
  void *consent_timer;
  void *consent_timeout;
  void *consent_handle;
  int can_send;
  int disconnected;
  struct timeval consent_last_seen;

  STAILQ_ENTRY(nr_ice_component_)entry;
};

typedef STAILQ_HEAD(nr_ice_component_head_,nr_ice_component_) nr_ice_component_head;

int nr_ice_component_create(struct nr_ice_media_stream_ *stream, int component_id, nr_ice_component **componentp);
int nr_ice_component_destroy(nr_ice_component **componentp);
int nr_ice_component_initialize(struct nr_ice_ctx_ *ctx,nr_ice_component *component);
void nr_ice_component_stop_gathering(nr_ice_component *component);
int nr_ice_component_is_done_gathering(nr_ice_component *comp);
int nr_ice_component_maybe_prune_candidate(nr_ice_ctx *ctx, nr_ice_component *comp, nr_ice_candidate *c1, int *was_pruned);
int nr_ice_component_pair_candidate(nr_ice_peer_ctx *pctx, nr_ice_component *pcomp, nr_ice_candidate *lcand, int pair_all_remote);
int nr_ice_component_pair_candidates(nr_ice_peer_ctx *pctx, nr_ice_component *lcomp, nr_ice_component *pcomp);
int nr_ice_component_service_pre_answer_requests(nr_ice_peer_ctx *pctx, nr_ice_component *pcomp, char *username, int *serviced);
int nr_ice_component_nominated_pair(nr_ice_component *comp, nr_ice_cand_pair *pair);
void nr_ice_component_failed_pair(nr_ice_component *comp, nr_ice_cand_pair *pair);
void nr_ice_component_check_if_failed(nr_ice_component *comp);
void nr_ice_component_maybe_select_pair(nr_ice_peer_ctx *pctx, nr_ice_component *comp);
int nr_ice_component_set_failed(nr_ice_component *comp);
int nr_ice_component_finalize(nr_ice_component *lcomp, nr_ice_component *rcomp);
int nr_ice_component_insert_pair(nr_ice_component *pcomp, nr_ice_cand_pair *pair);
int nr_ice_component_get_default_candidate(nr_ice_component *comp, nr_ice_candidate **candp, int ip_version);
void nr_ice_component_consent_destroy(nr_ice_component *comp);
void nr_ice_component_refresh_consent_now(nr_ice_component *comp);
void nr_ice_component_disconnected(nr_ice_component *comp);
void nr_ice_component_dump_state(nr_ice_component *comp, int log_level);

#endif
