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



#ifndef _ice_candidate_h
#define _ice_candidate_h

typedef enum {HOST=1, SERVER_REFLEXIVE, PEER_REFLEXIVE, RELAYED, CTYPE_MAX} nr_ice_candidate_type;

/* The fields of a candidate that are derivable purely from parsing the
 * candidate attribute string, with no dependency on any nICEr runtime
 * state (ctx, sockets, streams, etc.). Hoisted out so that code outside
 * nICEr can parse a candidate attribute without setting up the rest of
 * the machinery. Owns |foundation|, |raw_addr|, and |raw_raddr|; callers
 * that allocate one of these are responsible for free()ing those. */
struct nr_ice_candidate_parsedbits {
  nr_ice_candidate_type type;
  nr_socket_tcp_type tcp_type;
  UCHAR component_id;
  nr_transport_addr addr;
  nr_transport_addr base;
  char *foundation;
  /* Raw connection-address / rel-addr strings as they appeared in the
   * candidate attribute, without any parse/normalize round-trip. In other
   * words, an uncompressed IPv6 literal stays uncompressed. |raw_raddr| is
   * null for host candidates. No spec currently requires the raw form to be
   * preserved like this, but all browsers do. */
  char *raw_addr;
  char *raw_raddr;
  UINT4 priority;
};

struct nr_ice_candidate_ : public nr_ice_candidate_parsedbits {
  char *label;
  char codeword[5];
  int state;
  int trickled;
#define NR_ICE_CAND_STATE_CREATED          1
#define NR_ICE_CAND_STATE_INITIALIZING     2
#define NR_ICE_CAND_STATE_INITIALIZED      3
#define NR_ICE_CAND_STATE_FAILED           4
#define NR_ICE_CAND_PEER_CANDIDATE_UNPAIRED 9
#define NR_ICE_CAND_PEER_CANDIDATE_PAIRED   10
  struct nr_ice_ctx_ *ctx;
  nr_ice_socket *isock;               /* The socket to read from
                                         (it contains all other candidates
                                         on this socket) */
  nr_socket *osock;                   /* The socket to write to */
  nr_ice_media_stream *stream;        /* The media stream this is associated with */
  nr_ice_component *component;        /* The component this is associated with */
  /* This is mainly to avoid forgetting tcp/tls relay protocol, since the base
   * of relay candidates is the TURN server's allocation address, which will be UDP */
  UCHAR local_protocol;    /* IPPROTO_TCP, IPPROTO_UDP. */
  nr_ice_stun_server *stun_server;
  nr_transport_addr stun_server_addr; /* Resolved STUN server address */
  void *delay_timer;
  void *resolver_handle;

  /* Holding data for STUN and TURN */
  union {
    struct {
      nr_stun_client_ctx *stun;
      void *stun_handle;
      /* If this is a srflx that is piggybacking on a relay candidate, this is
       * a back pointer to that relay candidate. */
      nr_ice_candidate *relay_candidate;
    } srvrflx;
    struct {
      nr_turn_client_ctx *turn;
      nr_ice_turn_server *server;
      nr_ice_candidate *srvflx_candidate;
      nr_socket *turn_sock;
      void *turn_handle;
    } relayed;
  } u;

  int error_code; /* STUN error code on failure, 0 if no STUN response was received */

  NR_async_cb done_cb;
  void *cb_arg;

  void *ready_cb_timer;

  TAILQ_ENTRY(nr_ice_candidate_) entry_sock;
  TAILQ_ENTRY(nr_ice_candidate_) entry_comp;
};

extern const char *nr_ice_candidate_type_names[];
extern const char *nr_ice_candidate_tcp_type_names[];


int nr_ice_candidate_create(struct nr_ice_ctx_ *ctx,nr_ice_component *component, nr_ice_socket *isock, nr_socket *osock, nr_ice_candidate_type ctype, nr_socket_tcp_type tcp_type, nr_ice_stun_server *stun_server, UCHAR component_id, nr_ice_candidate **candp);
int nr_ice_candidate_initialize(nr_ice_candidate *cand, NR_async_cb ready_cb, void *cb_arg);
void nr_ice_candidate_compute_codeword(nr_ice_candidate *cand);
int nr_ice_candidate_process_stun(nr_ice_candidate *cand, UCHAR *msg, int len, nr_transport_addr *faddr);
int nr_ice_candidate_destroy(nr_ice_candidate **candp);
void nr_ice_candidate_stop_gathering(nr_ice_candidate *cand);
int nr_ice_format_candidate_attribute(nr_ice_candidate *cand, char *attr, int maxlen, int obfuscate_raddr);
int nr_ice_peer_candidate_from_attribute(nr_ice_ctx *ctx,const char *attr,nr_ice_media_stream *stream,const char* resolved_address,nr_ice_candidate **candp);
/* Parses a candidate attribute string into the parse-only fields.
 * |bits| must be zero-initialized by the caller. On success, the caller
 * owns |bits->foundation| and is responsible for freeing it. On failure,
 * |bits| is left in an indeterminate state; the caller should still free
 * |bits->foundation| since some fields may have been populated. */
int nr_ice_parse_candidate_attribute(const char *attr, const char *resolved_address, struct nr_ice_candidate_parsedbits *bits);
int nr_ice_peer_peer_rflx_candidate_create(nr_ice_ctx *ctx, const char *label, nr_ice_component *comp,nr_transport_addr *addr, nr_ice_candidate **candp);
int nr_ice_candidate_compute_priority(nr_ice_candidate *cand);

#endif

