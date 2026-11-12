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

#include <csi_platform.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "nr_api.h"
#include "registry.h"
#include "nr_socket.h"
#include "async_timer.h"

#include "stun_client_ctx.h"
#include "stun_server_ctx.h"
#include "turn_client_ctx.h"
#include "ice_ctx.h"
#include "ice_candidate.h"
#include "ice_codeword.h"
#include "ice_reg.h"
#include "ice_util.h"
#include "nr_socket_turn.h"
#include "nr_socket.h"
#include "nr_socket_multi_tcp.h"

static int next_automatic_preference = 127;

static int nr_ice_candidate_initialize2(nr_ice_candidate *cand);
static int nr_ice_get_foundation(nr_ice_ctx *ctx,nr_ice_candidate *cand);
static int nr_ice_srvrflx_start_stun(nr_ice_candidate *cand);
static void nr_ice_srvrflx_stun_finished_cb(NR_SOCKET sock, int how, void *cb_arg);
static int nr_ice_start_relay_turn(nr_ice_candidate *cand);
static void nr_ice_turn_allocated_cb(NR_SOCKET sock, int how, void *cb_arg);
static int nr_ice_candidate_resolved_cb(void *cb_arg, nr_transport_addr *addr);

void nr_ice_candidate_compute_codeword(nr_ice_candidate *cand)
  {
    char as_string[1024];

    snprintf(as_string,
             sizeof(as_string),
             "%s(%s)",
             cand->addr.as_string,
             cand->label);

    nr_ice_compute_codeword(as_string,strlen(as_string),cand->codeword);
  }

const char *nr_ice_candidate_type_names[]={0,"host","srflx","prflx","relay",0};
const char *nr_ice_candidate_tcp_type_names[]={0,"active","passive","so",0};

static const char *nr_ctype_name(nr_ice_candidate_type ctype) {
  assert(ctype<CTYPE_MAX && ctype>0);
  if (ctype <= 0 || ctype >= CTYPE_MAX) {
    return "ERROR";
  }
  return nr_ice_candidate_type_names[ctype];
}

static const char *nr_tcp_type_name(nr_socket_tcp_type tcp_type) {
  assert(tcp_type<TCP_TYPE_MAX && tcp_type>0);
  if (tcp_type <= 0 || tcp_type >= TCP_TYPE_MAX) {
    return "ERROR";
  }
  return nr_ice_candidate_tcp_type_names[tcp_type];
}

static int nr_ice_candidate_format_stun_label(char *label, size_t size, nr_ice_candidate *cand)
  {
  *label = 0;
  snprintf(label, size, "%s(%s|%s)", nr_ctype_name(cand->type),
           cand->base.as_string, cand->stun_server->addr.as_string);

  return (0);
  }

int nr_ice_candidate_create(nr_ice_ctx *ctx,nr_ice_component *comp,nr_ice_socket *isock, nr_socket *osock, nr_ice_candidate_type ctype, nr_socket_tcp_type tcp_type, nr_ice_stun_server *stun_server, UCHAR component_id, nr_ice_candidate **candp)
  {
    assert(!(comp->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY) || ctype == RELAYED);
    nr_ice_candidate *cand=0;
    nr_ice_candidate *tmp=0;
    int r,_status;
    char label[512];

    if(!(cand=R_NEW(nr_ice_candidate)))
      ABORT(R_NO_MEMORY);
    cand->state=NR_ICE_CAND_STATE_CREATED;
    cand->ctx=ctx;
    cand->isock=isock;
    cand->osock=osock;
    cand->type=ctype;
    cand->tcp_type=tcp_type;
    cand->stun_server=stun_server;
    cand->component_id=component_id;
    cand->component=comp;
    cand->stream=comp->stream;

    /* Extract the addr as the base */
    if(r=nr_socket_getaddr(cand->isock->sock,&cand->base))
      ABORT(r);

    /* The base might change protocol when a TURN allocation completes */
    cand->local_protocol = cand->base.protocol;

    switch(ctype) {
      case HOST:
        snprintf(label, sizeof(label), "host(%s)", cand->base.as_string);
        break;

      case SERVER_REFLEXIVE:
        if(r=nr_ice_candidate_format_stun_label(label, sizeof(label), cand))
          ABORT(r);
        break;

      case RELAYED:
        if(r=nr_ice_candidate_format_stun_label(label, sizeof(label), cand))
          ABORT(r);
        break;

      case PEER_REFLEXIVE:
        snprintf(label, sizeof(label), "prflx");
        break;

      default:
        assert(0); /* Can't happen */
        ABORT(R_BAD_ARGS);
    }

    if (tcp_type) {
      const char* ttype = nr_tcp_type_name(tcp_type);
      const int tlen = strlen(ttype)+1; /* plus space */
      const size_t llen=strlen(label);
      if (snprintf(label+llen, sizeof(label)-llen, " %s", ttype) != tlen) {
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): truncated tcp type added to buffer",
          ctx->label);
      }
    }

    if(!(cand->label=strdup(label)))
      ABORT(R_NO_MEMORY);

    if(r=nr_ice_get_foundation(ctx,cand))
      ABORT(r);
    if(r=nr_ice_candidate_compute_priority(cand))
      ABORT(r);

    TAILQ_FOREACH(tmp,&isock->candidates,entry_sock){
      if(cand->priority==tmp->priority){
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): duplicate priority %u candidate %s and candidate %s",
          ctx->label,cand->priority,cand->label,tmp->label);
      }
    }

    if(ctype==RELAYED)
      cand->u.relayed.turn_sock=osock;


    /* Add the candidate to the isock list*/
    TAILQ_INSERT_TAIL(&isock->candidates,cand,entry_sock);

    nr_ice_candidate_compute_codeword(cand);

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/CAND(%s): created candidate %s with type %s",
      ctx->label,cand->codeword,cand->label,nr_ctype_name(ctype));

    *candp=cand;

    _status=0;
  abort:
    if (_status){
      r_log(LOG_ICE,LOG_ERR,"ICE(%s): Failed to create candidate of type %s", ctx->label,nr_ctype_name(ctype));
      nr_ice_candidate_destroy(&cand);
    }
    return(_status);
  }


/* Create a peer reflexive candidate */
int nr_ice_peer_peer_rflx_candidate_create(nr_ice_ctx *ctx, const char *label, nr_ice_component *comp,nr_transport_addr *addr, nr_ice_candidate **candp)
  {
    nr_ice_candidate *cand=0;
    nr_ice_candidate_type ctype=PEER_REFLEXIVE;
    int r,_status;

    if(!(cand=R_NEW(nr_ice_candidate)))
      ABORT(R_NO_MEMORY);
    if(!(cand->label=strdup(label)))
      ABORT(R_NO_MEMORY);

    cand->state=NR_ICE_CAND_STATE_INITIALIZED;
    cand->ctx=ctx;
    cand->type=ctype;
    cand->component_id=comp->component_id;
    cand->component=comp;
    cand->stream=comp->stream;


    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/CAND(%s): creating candidate with type %s",
      ctx->label,label,nr_ctype_name(ctype));

    if(r=nr_transport_addr_copy(&cand->base,addr))
      ABORT(r);
    if(r=nr_transport_addr_copy(&cand->addr,addr))
      ABORT(r);
    /* It is impossible to determine the foundation of a remote prflx unless
     * there happens to be another trickle candidate that matches exactly. So,
     * put a placeholder here so the result is parseable. */
    if(!(cand->foundation=strdup("prflx")))
      ABORT(R_NO_MEMORY);

    nr_ice_candidate_compute_codeword(cand);

    *candp=cand;

    _status=0;
  abort:
    if (_status){
      nr_ice_candidate_destroy(&cand);
    }
    return(_status);
  }

static void nr_ice_candidate_mark_done(nr_ice_candidate *cand, int state)
  {
    if (!cand) {
      assert(0);
      return;
    }

    /* If this is a relay candidate, there's likely to be a srflx that is
     * piggybacking on it. Make sure it is marked done too. */
    if ((cand->type == RELAYED) && cand->u.relayed.srvflx_candidate) {
      nr_ice_candidate *srflx=cand->u.relayed.srvflx_candidate;
      if (state == NR_ICE_CAND_STATE_INITIALIZED &&
          nr_turn_client_get_mapped_address(cand->u.relayed.turn,
                                            &srflx->addr)) {
        r_log(LOG_ICE, LOG_WARNING, "ICE(%s)/CAND(%s): Failed to get mapped address from TURN allocate response, srflx failed.", cand->ctx->label, cand->label);
        if (!srflx->error_code) {
          srflx->error_code = cand->error_code;
        }
        nr_ice_candidate_mark_done(srflx, NR_ICE_CAND_STATE_FAILED);
      } else {
        if (state == NR_ICE_CAND_STATE_FAILED && !srflx->error_code) {
          srflx->error_code = cand->error_code;
        }
        nr_ice_candidate_mark_done(srflx, state);
      }
    }

    if (state == NR_ICE_CAND_STATE_FAILED) {
      /* Skip piggybacking srflx candidates — the relay candidate fires for them. */
      int is_piggybacking_srflx = (cand->type == SERVER_REFLEXIVE &&
                                   cand->u.srvrflx.relay_candidate != NULL);
      // Errors for pooled candidates are held back until the candidates are
      // used, like their icecandidate events. Whether that is what the spec
      // intends, and whether they should then be filtered by the ICE transport
      // policy, is https://github.com/w3c/webrtc-pc/issues/3141
      if (!is_piggybacking_srflx && !cand->stream->defer_gathering_events) {
        nr_ice_gather_handler *gh = cand->stream->ctx->gather_handler;
        if (gh && gh->vtbl->candidate_error) {
          gh->vtbl->candidate_error(gh->obj, cand->stream, cand);
        }
      }
    }

    NR_async_cb done_cb=cand->done_cb;
    cand->done_cb=0;
    cand->state=state;
    /* This might destroy cand! */
    if (done_cb) {
      done_cb(0,0,cand->cb_arg);
    }
  }

int nr_ice_candidate_destroy(nr_ice_candidate **candp)
  {
    nr_ice_candidate *cand=0;

    if(!candp || !*candp)
      return(0);

    cand=*candp;

    nr_ice_candidate_stop_gathering(cand);

    switch(cand->type){
      case HOST:
        break;
      case RELAYED:
        // record stats back to the ice ctx on destruction
        if (cand->u.relayed.turn) {
          nr_accumulate_count(&(cand->ctx->stats.turn_401s), cand->u.relayed.turn->cnt_401s);
          nr_accumulate_count(&(cand->ctx->stats.turn_403s), cand->u.relayed.turn->cnt_403s);
          nr_accumulate_count(&(cand->ctx->stats.turn_438s), cand->u.relayed.turn->cnt_438s);

          nr_turn_stun_ctx* stun_ctx;
          stun_ctx = STAILQ_FIRST(&cand->u.relayed.turn->stun_ctxs);
          while (stun_ctx) {
            nr_accumulate_count(&(cand->ctx->stats.stun_retransmits), stun_ctx->stun->retransmit_ct);

            stun_ctx = STAILQ_NEXT(stun_ctx, entry);
          }
        }
        if (cand->u.relayed.turn_handle)
          nr_ice_socket_deregister(cand->isock, cand->u.relayed.turn_handle);
        if (cand->u.relayed.srvflx_candidate)
          cand->u.relayed.srvflx_candidate->u.srvrflx.relay_candidate=0;
        nr_turn_client_ctx_destroy(&cand->u.relayed.turn);
        nr_socket_destroy(&cand->u.relayed.turn_sock);
        break;
      case SERVER_REFLEXIVE:
        if (cand->u.srvrflx.stun_handle)
          nr_ice_socket_deregister(cand->isock, cand->u.srvrflx.stun_handle);
        if (cand->u.srvrflx.relay_candidate)
          cand->u.srvrflx.relay_candidate->u.relayed.srvflx_candidate=0;
        nr_stun_client_ctx_destroy(&cand->u.srvrflx.stun);
        break;
      default:
        break;
    }

    free(cand->foundation);
    free(cand->raw_addr);
    free(cand->raw_raddr);
    free(cand->label);
    free(cand);

    return(0);
  }

void nr_ice_candidate_stop_gathering(nr_ice_candidate *cand)
  {
    if (cand->state == NR_ICE_CAND_STATE_INITIALIZING) {
      /* Make sure the ICE ctx isn't still waiting around for this candidate
       * to init. */
      nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_FAILED);
    }

    NR_async_timer_cancel(cand->delay_timer);
    cand->delay_timer=0;
    NR_async_timer_cancel(cand->ready_cb_timer);
    cand->ready_cb_timer=0;

    if(cand->resolver_handle){
      nr_resolver_cancel(cand->ctx->resolver,cand->resolver_handle);
      cand->resolver_handle=0;
    }
  }

/* This algorithm is not super-fast, but I don't think we need a hash
   table just yet and it produces a small foundation string */
static int nr_ice_get_foundation(nr_ice_ctx *ctx,nr_ice_candidate *cand)
  {
    nr_ice_foundation *foundation;
    int i=0;
    char fnd[20];
    int _status;

    foundation=STAILQ_FIRST(&ctx->foundations);
    while(foundation){
      if(nr_transport_addr_cmp(&cand->base,&foundation->addr,NR_TRANSPORT_ADDR_CMP_MODE_ADDR))
        goto next;
      // cast necessary because there is no guarantee that enum is signed.
      // foundation->type should probably match nr_ice_candidate_type
      if((int)cand->type != foundation->type)
        goto next;
      if(cand->type == SERVER_REFLEXIVE || cand->type == RELAYED) {
        if(nr_transport_addr_cmp(&cand->stun_server->addr, &foundation->stun_server_addr, NR_TRANSPORT_ADDR_CMP_MODE_ADDR))
          goto next;
      }

      snprintf(fnd,sizeof(fnd),"%d",i);
      if(!(cand->foundation=strdup(fnd)))
        ABORT(R_NO_MEMORY);
      return(0);

    next:
      foundation=STAILQ_NEXT(foundation,entry);
      i++;
    }

    if(!(foundation=R_NEW(nr_ice_foundation)))
      ABORT(R_NO_MEMORY);
    nr_transport_addr_copy(&foundation->addr,&cand->base);
    foundation->type=cand->type;
    if(cand->type == SERVER_REFLEXIVE || cand->type == RELAYED) {
      nr_transport_addr_copy(&foundation->stun_server_addr, &cand->stun_server->addr);
    }
    STAILQ_INSERT_TAIL(&ctx->foundations,foundation,entry);

    snprintf(fnd,sizeof(fnd),"%d",i);
    if(!(cand->foundation=strdup(fnd)))
      ABORT(R_NO_MEMORY);

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_candidate_compute_priority(nr_ice_candidate *cand)
  {
    UCHAR type_preference;
    UCHAR interface_preference;
    UCHAR stun_priority;
    UCHAR direction_priority=0;
    int r,_status;

    if (cand->local_protocol != IPPROTO_UDP && cand->local_protocol != IPPROTO_TCP){
      r_log(LOG_ICE,LOG_ERR,"Unknown protocol type %u",
            (unsigned int)cand->local_protocol);
      ABORT(R_INTERNAL);
    }

    switch(cand->type){
      case HOST:
        if(cand->local_protocol == IPPROTO_UDP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_HOST,&type_preference))
            ABORT(r);
        } else if(cand->local_protocol == IPPROTO_TCP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_HOST_TCP,&type_preference))
            ABORT(r);
        }
        stun_priority=0;
        break;
      case RELAYED:
        if(cand->local_protocol == IPPROTO_UDP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_RELAYED,&type_preference))
            ABORT(r);
        } else if(cand->local_protocol == IPPROTO_TCP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_RELAYED_TCP,&type_preference))
            ABORT(r);
        }
        stun_priority=31-cand->stun_server->id;
        break;
      case SERVER_REFLEXIVE:
        if(cand->local_protocol == IPPROTO_UDP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_SRV_RFLX,&type_preference))
            ABORT(r);
        } else if(cand->local_protocol == IPPROTO_TCP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_SRV_RFLX_TCP,&type_preference))
            ABORT(r);
        }
        stun_priority=31-cand->stun_server->id;
        break;
      case PEER_REFLEXIVE:
        if(cand->local_protocol == IPPROTO_UDP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_PEER_RFLX,&type_preference))
            ABORT(r);
        } else if(cand->local_protocol == IPPROTO_TCP) {
          if(r=NR_reg_get_uchar(NR_ICE_REG_PREF_TYPE_PEER_RFLX_TCP,&type_preference))
            ABORT(r);
        }
        stun_priority=0;
        break;
      default:
        ABORT(R_INTERNAL);
    }

    if(cand->local_protocol == IPPROTO_TCP){
      switch (cand->tcp_type) {
        case TCP_TYPE_ACTIVE:
          if (cand->type == HOST)
            direction_priority=6;
          else
            direction_priority=4;
          break;
        case  TCP_TYPE_PASSIVE:
          if (cand->type == HOST)
            direction_priority=4;
          else
            direction_priority=2;
          break;
        case  TCP_TYPE_SO:
          if (cand->type == HOST)
            direction_priority=2;
          else
            direction_priority=6;
          break;
        case  TCP_TYPE_NONE:
          break;
        case TCP_TYPE_MAX:
        default:
          assert(0);
          ABORT(R_INTERNAL);
      }
    }

    if(type_preference > 126)
      r_log(LOG_ICE,LOG_ERR,"Illegal type preference %d",type_preference);

    if(!cand->ctx->interface_prioritizer) {
      /* Prioritizer is not set, read from registry */
      if(r=NR_reg_get2_uchar(NR_ICE_REG_PREF_INTERFACE_PRFX,cand->base.ifname,
        &interface_preference)) {
        if (r==R_NOT_FOUND) {
          if (next_automatic_preference == 1) {
            r_log(LOG_ICE,LOG_ERR,"Out of preference values. Can't assign one for interface %s",cand->base.ifname);
            ABORT(R_NOT_FOUND);
          }
          r_log(LOG_ICE,LOG_DEBUG,"Automatically assigning preference for interface %s->%d",cand->base.ifname,
            next_automatic_preference);
          if (r=NR_reg_set2_uchar(NR_ICE_REG_PREF_INTERFACE_PRFX,cand->base.ifname,next_automatic_preference)){
            ABORT(r);
          }
          interface_preference=next_automatic_preference << 1;
          next_automatic_preference--;
          if (cand->base.ip_version == NR_IPV6) {
            /* Prefer IPV6 over IPV4 on the same interface. */
            interface_preference += 1;
          }
        }
        else {
          ABORT(r);
        }
      }
    }
    else {
      char key_of_interface[MAXIFNAME + 41];
      nr_transport_addr addr;

      if(r=nr_socket_getaddr(cand->isock->sock, &addr))
        ABORT(r);

      if(r=nr_transport_addr_fmt_ifname_addr_string(&addr,key_of_interface,
         sizeof(key_of_interface))) {
        ABORT(r);
      }
      if(r=nr_interface_prioritizer_get_priority(cand->ctx->interface_prioritizer,
         key_of_interface,&interface_preference)) {
        ABORT(r);
      }
    }

    assert(stun_priority < 32);
    assert(direction_priority < 8);

    cand->priority=
      (type_preference << 24) |
      (interface_preference << 16) |
      (direction_priority << 13) |
      (stun_priority << 8) |
      (256 - cand->component_id);

    /* S 4.1.2 */
    assert(cand->priority>=1&&cand->priority<=2147483647);

    _status=0;
  abort:
    return(_status);
  }

static int nr_ice_candidate_use_nr_resolver(nr_transport_addr *addr)
  {
    if(addr->fqdn[0] == 0) {
      return 0;
    }

    char use = 0;
    if(addr->protocol == IPPROTO_UDP) {
      NR_reg_get_char(NR_ICE_REG_USE_NR_RESOLVER_FOR_UDP, &use);
    } else if(addr->protocol == IPPROTO_TCP) {
      NR_reg_get_char(NR_ICE_REG_USE_NR_RESOLVER_FOR_TCP, &use);
    } else {
      assert(0);
    }

    return use;
  }

  static int nr_ice_candidate_check_stun_turn_address(
      const nr_ice_candidate* cand, const nr_transport_addr* addr) {
    if (!(cand->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK) &&
        nr_transport_addr_is_loopback(addr)) {
      r_log(LOG_ICE, LOG_WARNING,
            "ICE(%s): Skipping STUN server because it is on a loopback address "
            "%s",
            cand->ctx->label, cand->label);
      return 0;
    }

    if (!(cand->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL) &&
        nr_transport_addr_is_link_local(addr)) {
      r_log(LOG_ICE, LOG_WARNING,
            "ICE(%s): Skipping STUN server because it is on a link-local "
            "address %s",
            cand->ctx->label, cand->label);
      return 0;
    }

    if (nr_transport_addr_check_compatibility(addr, &cand->base)) {
      r_log(LOG_ICE, LOG_WARNING,
            "ICE(%s): Skipping STUN server because of link local mis-match for "
            "candidate %s",
            cand->ctx->label, cand->label);
      return 0;
    }

    return 1;
  }

int nr_ice_candidate_initialize(nr_ice_candidate *cand, NR_async_cb ready_cb, void *cb_arg)
  {
    int r,_status;
    int protocol=NR_RESOLVE_PROTOCOL_STUN;
    cand->done_cb=ready_cb;
    cand->cb_arg=cb_arg;
    cand->state=NR_ICE_CAND_STATE_INITIALIZING;

    switch(cand->type){
      case HOST:
        if(r=nr_socket_getaddr(cand->isock->sock,&cand->addr))
          ABORT(r);
        cand->osock=cand->isock->sock;
        // Post this so that it doesn't happen in-line. The timer handle is
        // cleared first, since nr_ice_candidate_stop_gathering cancels it.
        NR_ASYNC_TIMER_SET(0, [](NR_SOCKET, int, void *cb_arg) {
            nr_ice_candidate *cand = static_cast<nr_ice_candidate*>(cb_arg);
            cand->ready_cb_timer = 0;
            nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_INITIALIZED);
          }, (void *)cand, &cand->ready_cb_timer);
        break;
      case RELAYED:
        protocol=NR_RESOLVE_PROTOCOL_TURN;
        [[fallthrough]];
      case SERVER_REFLEXIVE:
        if (nr_transport_addr_cmp(&cand->base, &cand->stun_server->addr,
                                  NR_TRANSPORT_ADDR_CMP_MODE_PROTOCOL)) {
          r_log(LOG_ICE, LOG_INFO,
                "ICE-CANDIDATE(%s): Skipping srflx/relayed candidate because "
                "of IP version/transport mis-match with STUN/TURN server "
                "(%u/%s - %u/%s).",
                cand->label, cand->base.ip_version,
                cand->base.protocol == IPPROTO_UDP ? "UDP" : "TCP",
                cand->stun_server->addr.ip_version,
                cand->stun_server->addr.protocol == IPPROTO_UDP ? "UDP"
                                                                : "TCP");
          ABORT(R_NOT_FOUND); /* Same error code when DNS lookup fails */
        }

        if(nr_ice_candidate_use_nr_resolver(&cand->stun_server->addr)) {
          r_log(
              LOG_ICE, LOG_DEBUG,
              "ICE-CANDIDATE(%s): Starting DNS resolution (%u/%s - %u/%s).",
              cand->label, cand->base.ip_version,
              cand->base.protocol == IPPROTO_UDP ? "UDP" : "TCP",
              cand->stun_server->addr.ip_version,
              cand->stun_server->addr.protocol == IPPROTO_UDP ? "UDP" : "TCP");
          nr_resolver_resource resource;
          uint16_t port;
          resource.domain_name = cand->stun_server->addr.fqdn;
          if (r = nr_transport_addr_get_port(&cand->stun_server->addr, &port)) {
            ABORT(r);
          }
          resource.port = port;
          resource.stun_turn=protocol;
          resource.transport_protocol = cand->stun_server->addr.protocol;

          switch (cand->base.ip_version) {
            case NR_IPV4:
              resource.address_family=AF_INET;
              break;
            case NR_IPV6:
              resource.address_family=AF_INET6;
              break;
            default:
              assert(0);
              ABORT(R_BAD_ARGS);
          }

          /* Try to resolve */
          if(!cand->ctx->resolver) {
            r_log(LOG_ICE, LOG_ERR, "ICE-CANDIDATE(%s): Can't use DNS names without a resolver", cand->label);
            ABORT(R_BAD_ARGS);
          }

          if(r=nr_resolver_resolve(cand->ctx->resolver,
                                   &resource,
                                   nr_ice_candidate_resolved_cb,
                                   (void *)cand,
                                   &cand->resolver_handle)){
            r_log(LOG_ICE,LOG_ERR,"ICE-CANDIDATE(%s): Could not invoke DNS resolver",cand->label);
            ABORT(r);
          }
        } else {
          if (!nr_ice_candidate_check_stun_turn_address(
                  cand, &cand->stun_server->addr)) {
            ABORT(R_NOT_FOUND);
          }

          /* No nr_resolver for this, just copy the address and finish init */
          if (r = nr_transport_addr_copy(&cand->stun_server_addr,
                                         &cand->stun_server->addr)) {
            r_log(LOG_ICE,LOG_ERR,"ICE-CANDIDATE(%s): Could not copy STUN server addr", cand->label);
            ABORT(r);
          }

          if(r=nr_ice_candidate_initialize2(cand))
            ABORT(r);
        }
        break;
      default:
        ABORT(R_INTERNAL);
    }

    nr_ice_candidate_compute_codeword(cand);

    _status=0;
  abort:
    if(_status && _status!=R_WOULDBLOCK) {
      nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_FAILED);
    }
    return(_status);
  }

static int nr_ice_candidate_resolved_cb(void *cb_arg, nr_transport_addr *addr)
  {
    nr_ice_candidate *cand=(nr_ice_candidate*)cb_arg;
    int r,_status;

    cand->resolver_handle=0;

    if(addr){
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): resolved candidate %s. addr=%s",
            cand->ctx->label,cand->label,addr->as_string);
    }
    else {
      r_log(LOG_ICE,LOG_WARNING,"ICE(%s): failed to resolve candidate %s.",
            cand->ctx->label,cand->label);
      ABORT(R_NOT_FOUND);
    }

    if (!nr_ice_candidate_check_stun_turn_address(cand, addr)) {
      ABORT(R_NOT_FOUND);
    }

    /* Copy the address */
    if(r=nr_transport_addr_copy(&cand->stun_server_addr,addr))
      ABORT(r);

    if (cand->tcp_type == TCP_TYPE_PASSIVE || cand->tcp_type == TCP_TYPE_SO){
      if (r=nr_socket_multi_tcp_stun_server_connect(cand->osock, addr))
        ABORT(r);
    }

    /* Now start initializing */
    if(r=nr_ice_candidate_initialize2(cand))
      ABORT(r);

    _status=0;
  abort:
    if(_status && _status!=R_WOULDBLOCK) {
      nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_FAILED);
    }
    return(_status);
  }

static int nr_ice_candidate_initialize2(nr_ice_candidate *cand)
  {
    int r,_status;

    switch(cand->type){
      case HOST:
        assert(0); /* Can't happen */
        ABORT(R_INTERNAL);
        break;
      case RELAYED:
        if(r=nr_ice_start_relay_turn(cand))
          ABORT(r);
        ABORT(R_WOULDBLOCK);
        break;
      case SERVER_REFLEXIVE:
        /* Need to start stun */
        if(r=nr_ice_srvrflx_start_stun(cand))
          ABORT(r);
        cand->osock=cand->isock->sock;
        ABORT(R_WOULDBLOCK);
        break;
      default:
        ABORT(R_INTERNAL);
    }

    _status=0;
  abort:
    return(_status);
  }

static void nr_ice_srvrflx_start_stun_timer_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_candidate *cand=(nr_ice_candidate*)cb_arg;
    int r,_status;

    cand->delay_timer=0;

/* TODO: if the response is a BINDING-ERROR-RESPONSE, then restart
 * TODO: using NR_STUN_CLIENT_MODE_BINDING_REQUEST because the
 * TODO: server may not have understood the 0.96-style request */
    if(r=nr_stun_client_start(cand->u.srvrflx.stun, NR_STUN_CLIENT_MODE_BINDING_REQUEST_NO_AUTH, nr_ice_srvrflx_stun_finished_cb, cand))
      ABORT(r);

    if(r=nr_ice_ctx_remember_id(cand->ctx, cand->u.srvrflx.stun->request))
      ABORT(r);

    if(r=nr_ice_socket_register_stun_client(cand->isock,cand->u.srvrflx.stun,&cand->u.srvrflx.stun_handle))
      ABORT(r);

    _status=0;
  abort:
    if (_status && (cand->u.srvrflx.stun->state==NR_STUN_CLIENT_STATE_RUNNING)) {
      nr_stun_client_failed(cand->u.srvrflx.stun);
    }
    return;
  }

static int nr_ice_srvrflx_start_stun(nr_ice_candidate *cand)
  {
    int r,_status;
    int flags = NR_STUN_TRANSPORT_ADDR_CHECK_WILDCARD;
    if (!(cand->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LOOPBACK;
    }
    if (!(cand->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LINK_LOCAL;
    }

    assert(!cand->delay_timer);
    if (r = nr_stun_client_ctx_create(
            cand->label, cand->isock->sock, &cand->stun_server_addr,
            cand->stream->ctx->gather_rto, flags, &cand->u.srvrflx.stun))
      ABORT(r);

    NR_ASYNC_TIMER_SET(cand->stream->ctx->stun_delay,nr_ice_srvrflx_start_stun_timer_cb,cand,&cand->delay_timer);
    cand->stream->ctx->stun_delay += cand->stream->ctx->Ta;

    _status=0;
  abort:
    return(_status);
  }

static void nr_ice_start_relay_turn_timer_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_candidate *cand=(nr_ice_candidate*)cb_arg;
    int r,_status;

    cand->delay_timer=0;

    if(r=nr_turn_client_allocate(cand->u.relayed.turn, nr_ice_turn_allocated_cb, cb_arg))
      ABORT(r);

    if(r=nr_ice_socket_register_turn_client(cand->isock, cand->u.relayed.turn,
                                            cand->osock, &cand->u.relayed.turn_handle))
      ABORT(r);

    _status=0;
  abort:
    if(_status && (cand->u.relayed.turn->state==NR_TURN_CLIENT_STATE_ALLOCATING)){
      nr_turn_client_failed(cand->u.relayed.turn);
    }
    return;
  }

static int nr_ice_start_relay_turn(nr_ice_candidate *cand)
  {
    int r,_status;
    assert(!cand->delay_timer);
    if(r=nr_turn_client_ctx_create(cand->label, cand->isock->sock,
                                   cand->u.relayed.server->username,
                                   cand->u.relayed.server->password,
                                   &cand->stun_server_addr,
                                   cand->component->ctx,
                                   &cand->u.relayed.turn))
      ABORT(r);

    if(r=nr_socket_turn_set_ctx(cand->osock, cand->u.relayed.turn))
      ABORT(r);

    NR_ASYNC_TIMER_SET(cand->stream->ctx->stun_delay,nr_ice_start_relay_turn_timer_cb,cand,&cand->delay_timer);
    cand->stream->ctx->stun_delay += cand->stream->ctx->Ta;

    _status=0;
  abort:
    return(_status);
  }

static void nr_ice_srvrflx_stun_finished_cb(NR_SOCKET sock, int how, void *cb_arg)
  {
    int _status;
    nr_ice_candidate *cand=(nr_ice_candidate*)cb_arg;

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/CAND(%s): %s",cand->ctx->label,cand->label,__FUNCTION__);

    /* Deregister to suppress duplicates */
    if(cand->u.srvrflx.stun_handle){ /* This test because we might have failed before CB registered */
      nr_ice_socket_deregister(cand->isock,cand->u.srvrflx.stun_handle);
      cand->u.srvrflx.stun_handle=0;
    }

    switch(cand->u.srvrflx.stun->state){
      /* OK, we should have a mapped address */
      case NR_STUN_CLIENT_STATE_DONE:
        /* Copy the address */
        nr_transport_addr_copy(&cand->addr, &cand->u.srvrflx.stun->results.stun_binding_response.mapped_addr);
        cand->addr.protocol=cand->base.protocol;
        nr_transport_addr_fmt_addr_string(&cand->addr);
        nr_stun_client_ctx_destroy(&cand->u.srvrflx.stun);
        nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_INITIALIZED);
        cand=0;
        break;

      /* This failed, so go to the next STUN server if there is one */
      case NR_STUN_CLIENT_STATE_FAILED:
        cand->error_code = cand->u.srvrflx.stun->error_code;
        ABORT(R_NOT_FOUND);
        break;
      default:
        ABORT(R_INTERNAL);
    }
    _status = 0;
  abort:
    if(_status){
      nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_FAILED);
    }
  }

static void nr_ice_turn_allocated_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    int r,_status;
    nr_ice_candidate *cand=(nr_ice_candidate*)cb_arg;
    nr_turn_client_ctx *turn=cand->u.relayed.turn;
    char *label;
    nr_transport_addr relay_addr;

    switch(turn->state){
      /* OK, we should have a mapped address */
      case NR_TURN_CLIENT_STATE_ALLOCATED:
        if (r=nr_turn_client_get_relayed_address(turn, &relay_addr))
          ABORT(r);

        if(r=nr_concat_strings(&label,"turn-relay(",cand->base.as_string,"|",
                               relay_addr.as_string,")",NULL))
          ABORT(r);

        r_log(LOG_ICE,LOG_DEBUG,"TURN-CLIENT(%s)/CAND(%s): Switching from TURN to RELAY (%s)",cand->u.relayed.turn->label,cand->label,label);

        /* Copy the relayed address into the candidate addr and
           into the candidate base. Note that we need to keep the
           ifname in the base. */
        if (r=nr_transport_addr_copy(&cand->addr, &relay_addr))
          ABORT(r);
        if (r=nr_transport_addr_copy_keep_ifname(&cand->base, &relay_addr))  /* Need to keep interface for priority calculation */
          ABORT(r);

        r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/CAND(%s): new relay base=%s addr=%s", cand->ctx->label, cand->label, cand->base.as_string, cand->addr.as_string);

        free(cand->label);
        cand->label=label;
        nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_INITIALIZED);
        cand = 0;

        break;

    case NR_TURN_CLIENT_STATE_FAILED:
    case NR_TURN_CLIENT_STATE_CANCELLED:
      r_log(NR_LOG_TURN, LOG_WARNING,
            "ICE-CANDIDATE(%s): nr_turn_allocated_cb called with state %d",
            cand->label, turn->state);
      /* This failed, so go to the next TURN server if there is one */
      ABORT(R_NOT_FOUND);
      break;
    default:
      assert(0); /* should never happen */
      ABORT(R_INTERNAL);
    }

    _status=0;
  abort:
    if(_status){
      if (cand) {
        r_log(NR_LOG_TURN, LOG_WARNING,
              "ICE-CANDIDATE(%s): nr_turn_allocated_cb failed", cand->label);
        if (turn->state == NR_TURN_CLIENT_STATE_FAILED) {
          cand->error_code = turn->last_error_code;
        }
        nr_ice_candidate_mark_done(cand, NR_ICE_CAND_STATE_FAILED);
      }
    }
  }

/* Format the candidate attribute as per ICE S 15.1 */
int nr_ice_format_candidate_attribute(nr_ice_candidate *cand, char *attr, int maxlen, int obfuscate_raddr)
  {
    int r,_status;
    char addr[256];
    uint16_t port;
    int len;
    nr_transport_addr *raddr;

    assert(!strcmp(nr_ice_candidate_type_names[HOST], "host"));
    assert(!strcmp(nr_ice_candidate_type_names[RELAYED], "relay"));

    if(r=nr_transport_addr_get_addrstring(&cand->addr,addr,sizeof(addr)))
      ABORT(r);

    if(r=nr_transport_addr_get_port(&cand->addr,&port))
      ABORT(r);
    /* https://tools.ietf.org/html/rfc6544#section-4.5 */
    if (cand->base.protocol==IPPROTO_TCP && cand->tcp_type==TCP_TYPE_ACTIVE)
      port=9;
    snprintf(attr,maxlen,"candidate:%s %d %s %u %s %u typ %s",
      cand->foundation, cand->component_id, cand->addr.protocol==IPPROTO_UDP?"UDP":"TCP",cand->priority, addr, port,
      nr_ctype_name(cand->type));

    len=strlen(attr); attr+=len; maxlen-=len;

    /* raddr, rport are determined in the same way for prflx/srflx/relay. */
    if (cand->type != HOST) {
      if (obfuscate_raddr) {
        snprintf(attr,maxlen," raddr 0.0.0.0 rport 0");
      } else {
        raddr = (cand->stream->flags &
                 (NR_ICE_CTX_FLAGS_RELAY_ONLY |
                  NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES)) ?
          &cand->addr : &cand->base;
        if(r=nr_transport_addr_get_addrstring(raddr,addr,sizeof(addr)))
          ABORT(r);
        if(r=nr_transport_addr_get_port(raddr,&port))
          ABORT(r);
        snprintf(attr,maxlen," raddr %s rport %u",addr,port);
      }
    }

    if (cand->base.protocol==IPPROTO_TCP && cand->tcp_type){
      len=strlen(attr);
      attr+=len;
      maxlen-=len;
      snprintf(attr,maxlen," tcptype %s",nr_tcp_type_name(cand->tcp_type));
    }

    _status=0;
  abort:
    return(_status);
  }

