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



#ifndef _ice_candidate_pair_h
#define _ice_candidate_pair_h


struct nr_ice_cand_pair_ {
  nr_ice_peer_ctx *pctx;
  char codeword[5];
  char *as_string;
  int state;                          /* The current state (S 5.7.3) */
#define NR_ICE_PAIR_STATE_FROZEN           1
#define NR_ICE_PAIR_STATE_WAITING          2
#define NR_ICE_PAIR_STATE_IN_PROGRESS      3
#define NR_ICE_PAIR_STATE_FAILED           4
#define NR_ICE_PAIR_STATE_SUCCEEDED        5
#define NR_ICE_PAIR_STATE_CANCELLED        6

  UCHAR peer_nominated;               /* The peer sent USE-CANDIDATE
                                         on this check */
  UCHAR nominated;                    /* Is this nominated or not */

  UCHAR triggered;            /* Ignore further trigger check requests */

  UINT8 priority;                  /* The priority for this pair */
  nr_ice_candidate *local;            /* The local candidate */
  nr_ice_candidate *remote;           /* The remote candidate */
  char *foundation;                   /* The combined foundations */

  // for RTCIceCandidatePairStats
  UINT8 bytes_sent;
  UINT8 bytes_recvd;
  UINT8 packets_sent;
  UINT8 packets_recvd;
  struct timeval last_sent;
  struct timeval last_recvd;
  UINT8 responses_recvd;
  UINT8 current_rtt_ms;
  UINT8 total_rtt_ms;

  nr_stun_client_ctx *stun_client;    /* STUN context when acting as a client */
  void *stun_client_handle;

  void *stun_cb_timer;
  void *restart_role_change_cb_timer;
  void *restart_nominated_cb_timer;

  TAILQ_ENTRY(nr_ice_cand_pair_) check_queue_entry;   /* the check list */
  TAILQ_ENTRY(nr_ice_cand_pair_) triggered_check_queue_entry;  /* the trigger check queue */
};

int nr_ice_candidate_pair_create(nr_ice_peer_ctx *pctx, nr_ice_candidate *lcand,nr_ice_candidate *rcand,nr_ice_cand_pair **pairp);
int nr_ice_candidate_pair_unfreeze(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair);
int nr_ice_candidate_pair_start(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair);
int nr_ice_candidate_pair_nominate(nr_ice_cand_pair *pair, UINT4 delay);
void nr_ice_candidate_pair_set_state(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair, int state);
void nr_ice_candidate_pair_dump_state(nr_ice_cand_pair *pair, int log_level);
void nr_ice_candidate_pair_cancel(nr_ice_peer_ctx *pctx,nr_ice_cand_pair *pair, int move_to_wait_state);
int nr_ice_candidate_pair_do_triggered_check(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair, int force);
void nr_ice_candidate_pair_insert(nr_ice_cand_pair_head *head,nr_ice_cand_pair *pair);
int nr_ice_candidate_pair_trigger_check_append(nr_ice_cand_pair_head *head,nr_ice_cand_pair *pair);
int nr_ice_candidate_pair_destroy(nr_ice_cand_pair **pairp);
void nr_ice_candidate_pair_role_change(nr_ice_cand_pair *pair);
void nr_ice_candidate_pair_update_rtt(nr_ice_cand_pair *pair, UINT8 rtt_ms);

#endif

