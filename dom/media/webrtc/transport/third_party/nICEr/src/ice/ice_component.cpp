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

#include <string.h>
#include <assert.h>
#include <nr_api.h>
#include <registry.h>
#include <async_timer.h>
#include "ice_ctx.h"
#include "ice_codeword.h"
#include "stun.h"
#include "nr_socket_local.h"
#include "nr_socket_turn.h"
#include "nr_socket_buffered_stun.h"
#include "nr_socket_multi_tcp.h"
#include "ice_reg.h"
#include "nr_crypto.h"
#include "r_time.h"

static void nr_ice_component_refresh_consent_cb(NR_SOCKET s, int how, void *cb_arg);
static int nr_ice_component_stun_server_default_cb(void *cb_arg,nr_stun_server_ctx *stun_ctx,nr_socket *sock, nr_stun_server_request *req, int *dont_free, int *error);
static int nr_ice_pre_answer_request_destroy(nr_ice_pre_answer_request **parp);
int nr_ice_component_can_candidate_addr_pair(nr_transport_addr *local, nr_transport_addr *remote);
int nr_ice_component_can_candidate_tcptype_pair(nr_socket_tcp_type left, nr_socket_tcp_type right);
void nr_ice_component_consent_calc_consent_timer(nr_ice_component *comp);
void nr_ice_component_consent_schedule_consent_timer(nr_ice_component *comp);
int nr_ice_component_refresh_consent(nr_stun_client_ctx *ctx, NR_async_cb finished_cb, void *cb_arg);
int nr_ice_component_setup_consent(nr_ice_component *comp);
int nr_ice_pre_answer_enqueue(nr_ice_component *comp, nr_socket *sock, nr_stun_server_request *req, int *dont_free);

/* This function takes ownership of the contents of req (but not req itself) */
static int nr_ice_pre_answer_request_create(nr_transport_addr *dst, nr_stun_server_request *req, nr_ice_pre_answer_request **parp)
  {
    int r, _status;
    nr_ice_pre_answer_request *par = 0;
    nr_stun_message_attribute *attr;

    if (!(par = R_NEW(nr_ice_pre_answer_request)))
      ABORT(R_NO_MEMORY);

    par->req = *req; /* Struct assignment */
    memset(req, 0, sizeof(*req)); /* Zero contents to avoid confusion */

    if (r=nr_transport_addr_copy(&par->local_addr, dst))
      ABORT(r);
    if (!nr_stun_message_has_attribute(par->req.request, NR_STUN_ATTR_USERNAME, &attr))
      ABORT(R_INTERNAL);
    if (!(par->username = strdup(attr->u.username)))
      ABORT(R_NO_MEMORY);

    *parp=par;
    _status=0;
 abort:
    if (_status) {
      /* Erase the request so we don't free it */
      memset(&par->req, 0, sizeof(nr_stun_server_request));
      nr_ice_pre_answer_request_destroy(&par);
    }

    return(_status);
  }

static int nr_ice_pre_answer_request_destroy(nr_ice_pre_answer_request **parp)
  {
    nr_ice_pre_answer_request *par;

    if (!parp || !*parp)
      return(0);

    par = *parp;
    *parp = 0;

    nr_stun_message_destroy(&par->req.request);
    nr_stun_message_destroy(&par->req.response);

    free(par->username);
    free(par);

    return(0);
  }

int nr_ice_component_create(nr_ice_media_stream *stream, int component_id, nr_ice_component **componentp)
  {
    int _status;
    nr_ice_component *comp=0;

    if(!(comp=R_NEW(nr_ice_component)))
      ABORT(R_NO_MEMORY);

    comp->state=NR_ICE_COMPONENT_UNPAIRED;
    comp->component_id=component_id;
    comp->stream=stream;
    comp->ctx=stream->ctx;

    STAILQ_INIT(&comp->sockets);
    TAILQ_INIT(&comp->candidates);
    STAILQ_INIT(&comp->pre_answer_reqs);

    STAILQ_INSERT_TAIL(&stream->components,comp,entry);

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_component_destroy(nr_ice_component **componentp)
  {
    nr_ice_component *component;
    nr_ice_socket *s1,*s2;
    nr_ice_candidate *c1,*c2;
    nr_ice_pre_answer_request *r1,*r2;

    if(!componentp || !*componentp)
      return(0);

    component=*componentp;
    *componentp=0;

    nr_ice_component_consent_destroy(component);

    /* Detach ourselves from the sockets */
    if (component->local_component){
      nr_ice_socket *isock=STAILQ_FIRST(&component->local_component->sockets);
      while(isock){
        nr_stun_server_remove_client(isock->stun_server, component);
        isock=STAILQ_NEXT(isock, entry);
      }
    }

    /* candidates MUST be destroyed before the sockets so that
       they can deregister */
    TAILQ_FOREACH_SAFE(c1, &component->candidates, entry_comp, c2){
      TAILQ_REMOVE(&component->candidates,c1,entry_comp);
      nr_ice_candidate_destroy(&c1);
    }

    STAILQ_FOREACH_SAFE(s1, &component->sockets, entry, s2){
      STAILQ_REMOVE(&component->sockets,s1,nr_ice_socket_,entry);
      nr_ice_socket_destroy(&s1);
    }

    STAILQ_FOREACH_SAFE(r1, &component->pre_answer_reqs, entry, r2){
      STAILQ_REMOVE(&component->pre_answer_reqs,r1,nr_ice_pre_answer_request_, entry);
      nr_ice_pre_answer_request_destroy(&r1);
    }

    free(component);
    return(0);
  }

static int nr_ice_component_create_stun_server_ctx(nr_ice_component *component, nr_ice_socket *isock, nr_transport_addr *addr, char *lufrag, Data *pwd)
  {
    char label[256];
    int r,_status;

    /* Create a STUN server context for this socket */
    snprintf(label, sizeof(label), "server(%s)", addr->as_string);
    if(r=nr_stun_server_ctx_create(label,&isock->stun_server))
      ABORT(r);

   /* Add the default STUN credentials so that we can respond before
      we hear about the peer.*/
    if(r=nr_stun_server_add_default_client(isock->stun_server, lufrag, pwd, nr_ice_component_stun_server_default_cb, component))
      ABORT(r);

    /* Do this last; if this function fails, we should not be taking a reference to isock */
    if(r=nr_ice_socket_register_stun_server(isock,isock->stun_server,&isock->stun_server_handle))
      ABORT(r);

    _status = 0;
 abort:
    return(_status);
  }

static int nr_ice_component_initialize_udp(struct nr_ice_ctx_ *ctx,nr_ice_component *component, nr_local_addr *addrs, int addr_ct, char *lufrag, Data *pwd)
  {
    nr_socket *sock;
    nr_ice_socket *isock=0;
    nr_ice_candidate *cand=0;
    int i;
    int j;
    int r,_status;

    if(ctx->flags & NR_ICE_CTX_FLAGS_ONLY_PROXY) {
      /* No UDP support if we must use a proxy */
      return 0;
    }

    /* Now one ice_socket for each address */
    for(i=0;i<addr_ct;i++){
      char suppress;

      if(r=NR_reg_get2_char(NR_ICE_REG_SUPPRESS_INTERFACE_PRFX,addrs[i].addr.ifname,&suppress)){
        if(r!=R_NOT_FOUND)
          ABORT(r);
      }
      else{
        if(suppress)
          continue;
      }
      r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): host address %s",component->stream->label,addrs[i].addr.as_string);
      if((r=nr_socket_factory_create_socket(ctx->socket_factory,&addrs[i].addr,&sock))){
        r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): couldn't create socket for address %s",component->stream->label,addrs[i].addr.as_string);
        continue;
      }

      if(r=nr_ice_socket_create(ctx,component,sock,NR_ICE_SOCKET_TYPE_DGRAM,&isock))
        ABORT(r);

      /* Create a STUN server context for this socket */
      if ((r=nr_ice_component_create_stun_server_ctx(component,isock,&addrs[i].addr,lufrag,pwd)))
        ABORT(r);

      /* Make sure we don't leak this. Failures might result in it being
       * unused, but we hand off references to this in enough places below
       * that unwinding it all becomes impractical. */
      STAILQ_INSERT_TAIL(&component->sockets,isock,entry);

      if (!(component->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY)) {
        /* Create one host candidate */
        if(r=nr_ice_candidate_create(ctx,component,isock,sock,HOST,TCP_TYPE_NONE,0,
          component->component_id,&cand))
          ABORT(r);

        TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
        component->candidate_ct++;
        cand=0;

        /* And a srvrflx candidate for each STUN server */
        for(j=0;j<component->stream->stun_server_ct;j++){
          r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): Checking STUN server %s %s", component->stream->label, component->stream->stun_servers[j].addr.fqdn, component->stream->stun_servers[j].addr.as_string);
          /* Skip non-UDP */
          if (component->stream->stun_servers[j].addr.protocol != IPPROTO_UDP) continue;

          if (nr_transport_addr_check_compatibility(
                  &addrs[i].addr, &component->stream->stun_servers[j].addr)) {
            r_log(LOG_ICE,LOG_INFO,"ICE-STREAM(%s): Skipping STUN server because of address type mis-match",component->stream->label);
            continue;
          }

          /* Ensure id is set (nr_ice_ctx_set_stun_servers does not) */
          component->stream->stun_servers[j].id = j;
          if(r=nr_ice_candidate_create(ctx,component,
            isock,sock,SERVER_REFLEXIVE,TCP_TYPE_NONE,
            &component->stream->stun_servers[j],component->component_id,&cand))
            ABORT(r);
          TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
          component->candidate_ct++;
          cand=0;
        }
      }
      else{
        r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): relay only option results in no host candidate for %s",component->stream->label,addrs[i].addr.as_string);
      }

#ifdef USE_TURN
      if ((component->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY) &&
          (component->stream->turn_server_ct == 0)) {
        r_log(LOG_ICE,LOG_ERR,"ICE-STREAM(%s): relay only option is set without any TURN server configured",component->stream->label);
      }
      /* And both a srvrflx and relayed candidate for each TURN server (unless
         we're in relay-only mode, in which case just the relayed one) */
      for(j=0;j<component->stream->turn_server_ct;j++){
        nr_socket *turn_sock;
        nr_ice_candidate *srvflx_cand=0;

        r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): Checking TURN server %s %s", component->stream->label, component->stream->turn_servers[j].turn_server.addr.fqdn, component->stream->turn_servers[j].turn_server.addr.as_string);

        /* Skip non-UDP */
        if (component->stream->turn_servers[j].turn_server.addr.protocol != IPPROTO_UDP)
          continue;

        if (nr_transport_addr_check_compatibility(
                &addrs[i].addr, &component->stream->turn_servers[j].turn_server.addr)) {
          r_log(LOG_ICE,LOG_INFO,"ICE-STREAM(%s): Skipping TURN server because of address type mis-match",component->stream->label);
          continue;
        }

        if (!(component->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY)) {
          /* Ensure id is set with a unique value */
          component->stream->turn_servers[j].turn_server.id = j + component->stream->stun_server_ct;
          /* srvrflx */
          if(r=nr_ice_candidate_create(ctx,component,
            isock,sock,SERVER_REFLEXIVE,TCP_TYPE_NONE,
            &component->stream->turn_servers[j].turn_server,component->component_id,&cand))
            ABORT(r);
          cand->state=NR_ICE_CAND_STATE_INITIALIZING; /* Don't start */
          cand->done_cb=nr_ice_gather_finished_cb;
          cand->cb_arg=cand;

          TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
          component->candidate_ct++;
          srvflx_cand=cand;
          cand=0;
        }
        /* relayed*/
        if(r=nr_socket_turn_create(&turn_sock))
          ABORT(r);
        if(r=nr_ice_candidate_create(ctx,component,
          isock,turn_sock,RELAYED,TCP_TYPE_NONE,
          &component->stream->turn_servers[j].turn_server,component->component_id,&cand))
           ABORT(r);
        if (srvflx_cand) {
          cand->u.relayed.srvflx_candidate=srvflx_cand;
          srvflx_cand->u.srvrflx.relay_candidate=cand;
        }
        cand->u.relayed.server=&component->stream->turn_servers[j];
        TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
        component->candidate_ct++;

        cand=0;
      }
#endif /* USE_TURN */
    }

    _status = 0;
 abort:
    return(_status);
  }

static int nr_ice_component_get_port_from_ephemeral_range(uint16_t *port)
  {
    int _status, r;
    void *buf = port;
    if(r=nr_crypto_random_bytes((UCHAR*)buf, 2))
      ABORT(r);
    *port|=49152; /* make it fit into IANA ephemeral port range >= 49152 */
    _status=0;
abort:
    return(_status);
  }

static int nr_ice_component_create_tcp_host_candidate(struct nr_ice_ctx_ *ctx,
  nr_ice_component *component, nr_transport_addr *interface_addr, nr_socket_tcp_type tcp_type,
  int backlog, int so_sock_ct, char *lufrag, Data *pwd, nr_ice_socket **isock)
  {
    int r,_status;
    nr_ice_candidate *cand=0;
    int tries=3;
    nr_ice_socket *isock_tmp=0;
    nr_socket *nrsock=0;
    nr_transport_addr addr;
    uint16_t local_port;

    if ((r=nr_transport_addr_copy(&addr,interface_addr)))
      ABORT(r);
    addr.protocol=IPPROTO_TCP;

    do{
      if (!tries--)
        ABORT(r);

      if((r=nr_ice_component_get_port_from_ephemeral_range(&local_port)))
        ABORT(r);

      if ((r=nr_transport_addr_set_port(&addr, local_port)))
        ABORT(r);

      if((r=nr_transport_addr_fmt_addr_string(&addr)))
        ABORT(r);

      /* It would be better to stop trying if there is error other than
         port already used, but it'd require significant work to support this. */
      r=nr_socket_multi_tcp_create(ctx,component,&addr,tcp_type,so_sock_ct,NR_STUN_MAX_MESSAGE_SIZE,&nrsock);

    } while(r);

    if((tcp_type == TCP_TYPE_PASSIVE) && (r=nr_socket_listen(nrsock,backlog)))
      ABORT(r);

    if((r=nr_ice_socket_create(ctx,component,nrsock,NR_ICE_SOCKET_TYPE_STREAM_TCP,&isock_tmp)))
      ABORT(r);

    /* nr_ice_socket took ownership of nrsock */
    nrsock=NULL;

    /* Create a STUN server context for this socket */
    if ((r=nr_ice_component_create_stun_server_ctx(component,isock_tmp,&addr,lufrag,pwd)))
      ABORT(r);

    if((r=nr_ice_candidate_create(ctx,component,isock_tmp,isock_tmp->sock,HOST,tcp_type,0,
      component->component_id,&cand)))
      ABORT(r);

    if (isock)
      *isock=isock_tmp;

    TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
    component->candidate_ct++;

    STAILQ_INSERT_TAIL(&component->sockets,isock_tmp,entry);

    _status=0;
abort:
    if (_status) {
      nr_ice_socket_destroy(&isock_tmp);
      nr_socket_destroy(&nrsock);
    }
    return(_status);
  }

static int nr_ice_component_initialize_tcp(struct nr_ice_ctx_ *ctx,nr_ice_component *component, nr_local_addr *addrs, int addr_ct, char *lufrag, Data *pwd)
  {
    nr_ice_candidate *cand=0;
    int i;
    int j;
    int r,_status;
    int so_sock_ct=0;
    int backlog=10;
    char ice_tcp_disabled=1;

    r_log(LOG_ICE,LOG_DEBUG,"nr_ice_component_initialize_tcp");

    if(r=NR_reg_get_int4(NR_ICE_REG_ICE_TCP_SO_SOCK_COUNT,&so_sock_ct)){
      if(r!=R_NOT_FOUND)
        ABORT(r);
    }

    if(r=NR_reg_get_int4(NR_ICE_REG_ICE_TCP_LISTEN_BACKLOG,&backlog)){
      if(r!=R_NOT_FOUND)
        ABORT(r);
    }

    if ((r=NR_reg_get_char(NR_ICE_REG_ICE_TCP_DISABLE, &ice_tcp_disabled))) {
      if (r != R_NOT_FOUND)
        ABORT(r);
    }
    if ((component->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY) ||
        (component->stream->flags & NR_ICE_CTX_FLAGS_ONLY_PROXY)) {
      r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): relay/proxy only option results in ICE TCP being disabled",component->stream->label);
      ice_tcp_disabled = 1;
    }

    for(i=0;i<addr_ct;i++){
      char suppress;
      nr_ice_socket *isock_psv=0;
      nr_ice_socket *isock_so=0;

      if(r=NR_reg_get2_char(NR_ICE_REG_SUPPRESS_INTERFACE_PRFX,addrs[i].addr.ifname,&suppress)){
        if(r!=R_NOT_FOUND)
          ABORT(r);
      }
      else if(suppress) {
          continue;
      }

      if (!ice_tcp_disabled) {
        /* passive host candidate */
        if ((r=nr_ice_component_create_tcp_host_candidate(ctx, component, &addrs[i].addr,
          TCP_TYPE_PASSIVE, backlog, 0, lufrag, pwd, &isock_psv))) {
          r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): failed to create passive TCP host candidate: %d",component->stream->label,r);
        }

        /* active host candidate */
        if ((r=nr_ice_component_create_tcp_host_candidate(ctx, component, &addrs[i].addr,
          TCP_TYPE_ACTIVE, 0, 0, lufrag, pwd, NULL))) {
          r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): failed to create active TCP host candidate: %d",component->stream->label,r);
        }

        /* simultaneous-open host candidate */
        if (so_sock_ct) {
          if ((r=nr_ice_component_create_tcp_host_candidate(ctx, component, &addrs[i].addr,
            TCP_TYPE_SO, 0, so_sock_ct, lufrag, pwd, &isock_so))) {
            r_log(LOG_ICE,LOG_WARNING,"ICE-STREAM(%s): failed to create simultanous open TCP host candidate: %d",component->stream->label,r);
          }
        }

        /* And srvrflx candidates for each STUN server */
        for(j=0;j<component->stream->stun_server_ct;j++){
          if (component->stream->stun_servers[j].addr.protocol != IPPROTO_TCP) continue;

          if (isock_psv) {
            if(r=nr_ice_candidate_create(ctx,component,
              isock_psv,isock_psv->sock,SERVER_REFLEXIVE,TCP_TYPE_PASSIVE,
              &component->stream->stun_servers[j],component->component_id,&cand))
              ABORT(r);
            TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
            component->candidate_ct++;
            cand=0;
          }

          if (isock_so) {
            if(r=nr_ice_candidate_create(ctx,component,
              isock_so,isock_so->sock,SERVER_REFLEXIVE,TCP_TYPE_SO,
              &component->stream->stun_servers[j],component->component_id,&cand))
              ABORT(r);
            TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
            component->candidate_ct++;
            cand=0;
          }
        }
      }

#ifdef USE_TURN
      /* Create a new relayed candidate for each addr/TURN server pair */
      for(j=0;j<component->stream->turn_server_ct;j++){
        nr_transport_addr addr;
        nr_socket *local_sock;
        nr_socket *buffered_sock;
        nr_socket *turn_sock;
        nr_ice_socket *turn_isock;

        r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): Checking TURN server %s %s", component->stream->label, component->stream->turn_servers[j].turn_server.addr.fqdn, component->stream->turn_servers[j].turn_server.addr.as_string);

        /* Skip non-TCP */
        if (component->stream->turn_servers[j].turn_server.addr.protocol != IPPROTO_TCP)
          continue;

        /* Create relay candidate */
        if ((r=nr_transport_addr_copy(&addr, &addrs[i].addr)))
          ABORT(r);
        addr.protocol = IPPROTO_TCP;

        if (nr_transport_addr_check_compatibility(
                &addr, &component->stream->turn_servers[j].turn_server.addr)) {
          r_log(LOG_ICE,LOG_INFO,"ICE-STREAM(%s): Skipping TURN server because of address type mis-match",component->stream->label);
          continue;
        }

        if (!ice_tcp_disabled) {
          /* Use TURN server to get srflx candidates */
          if (isock_psv) {
            if(r=nr_ice_candidate_create(ctx,component,
              isock_psv,isock_psv->sock,SERVER_REFLEXIVE,TCP_TYPE_PASSIVE,
              &component->stream->turn_servers[j].turn_server,component->component_id,&cand))
              ABORT(r);
            TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
            component->candidate_ct++;
            cand=0;
          }

          if (isock_so) {
            if(r=nr_ice_candidate_create(ctx,component,
              isock_so,isock_so->sock,SERVER_REFLEXIVE,TCP_TYPE_SO,
              &component->stream->turn_servers[j].turn_server,component->component_id,&cand))
              ABORT(r);
            TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
            component->candidate_ct++;
            cand=0;
          }
        }

        if (component->stream->turn_servers[j].turn_server.addr.fqdn[0] != 0) {
          /* If we're going to use TLS, make sure that's recorded */
          addr.tls = component->stream->turn_servers[j].turn_server.addr.tls;
        }

        if ((r=nr_transport_addr_fmt_addr_string(&addr)))
          ABORT(r);

        r_log(LOG_ICE, LOG_DEBUG,
              "ICE-STREAM(%s): Creating socket for address %s (turn server %s)",
              component->stream->label, addr.as_string,
              component->stream->turn_servers[j].turn_server.addr.as_string);

        /* Create a local socket */
        if((r=nr_socket_factory_create_socket(ctx->socket_factory,&addr,&local_sock))){
          r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): couldn't create socket for address %s",component->stream->label,addr.as_string);
          continue;
        }

        r_log(LOG_ICE,LOG_DEBUG,"nr_ice_component_initialize_tcp creating TURN TCP wrappers");

        /* The TCP buffered socket */
        if((r=nr_socket_buffered_stun_create(local_sock, NR_STUN_MAX_MESSAGE_SIZE, TURN_TCP_FRAMING, &buffered_sock)))
          ABORT(r);

        /* The TURN socket */
        if(r=nr_socket_turn_create(&turn_sock))
          ABORT(r);

        /* Create an ICE socket */
        if((r=nr_ice_socket_create(ctx, component, buffered_sock, NR_ICE_SOCKET_TYPE_STREAM_TURN, &turn_isock)))
          ABORT(r);


        /* Create a STUN server context for this socket */
        if ((r=nr_ice_component_create_stun_server_ctx(component,turn_isock,&addr,lufrag,pwd)))
          ABORT(r);

        /* Make sure we don't leak this. Failures might result in it being
         * unused, but we hand off references to this in enough places below
         * that unwinding it all becomes impractical. */
        STAILQ_INSERT_TAIL(&component->sockets,turn_isock,entry);

        /* Attach ourselves to it */
        if(r=nr_ice_candidate_create(ctx,component,
          turn_isock,turn_sock,RELAYED,TCP_TYPE_NONE,
          &component->stream->turn_servers[j].turn_server,component->component_id,&cand))
          ABORT(r);
        cand->u.relayed.srvflx_candidate=NULL;
        cand->u.relayed.server=&component->stream->turn_servers[j];
        TAILQ_INSERT_TAIL(&component->candidates,cand,entry_comp);
        component->candidate_ct++;
        cand=0;
      }
#endif /* USE_TURN */
    }

    _status = 0;
 abort:
    return(_status);
  }


/* Make all the candidates we can make at the beginning */
int nr_ice_component_initialize(struct nr_ice_ctx_ *ctx,nr_ice_component *component)
  {
    int r,_status;
    nr_local_addr *addrs=ctx->local_addrs;
    int addr_ct=ctx->local_addr_ct;
    char *lufrag;
    char *lpwd;
    Data pwd;
    nr_ice_candidate *cand;

    if (component->candidate_ct) {
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): component with id %d already has candidates, probably restarting gathering because of a new stream",ctx->label,component->component_id);
      return(0);
    }

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): initializing component with id %d",ctx->label,component->component_id);

    if(addr_ct==0){
      r_log(LOG_ICE,LOG_ERR,"ICE(%s): no local addresses available",ctx->label);
      ABORT(R_NOT_FOUND);
    }

    /* Note: we need to recompute these because
       we have not yet computed the values in the peer media stream.*/
    lufrag=component->stream->ufrag;
    assert(lufrag);
    if (!lufrag)
      ABORT(R_INTERNAL);
    lpwd=component->stream->pwd;
    assert(lpwd);
    if (!lpwd)
      ABORT(R_INTERNAL);
    INIT_DATA(pwd, (UCHAR *)lpwd, strlen(lpwd));

    /* Initialize the UDP candidates */
    if (r=nr_ice_component_initialize_udp(ctx, component, addrs, addr_ct, lufrag, &pwd))
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): failed to create UDP candidates with error %d",ctx->label,r);
    /* And the TCP candidates */
    if (r=nr_ice_component_initialize_tcp(ctx, component, addrs, addr_ct, lufrag, &pwd))
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): failed to create TCP candidates with error %d",ctx->label,r);

    /* count the candidates that will be initialized */
    cand=TAILQ_FIRST(&component->candidates);
    if(!cand){
      r_log(LOG_ICE,LOG_ERR,"ICE(%s): couldn't create any valid candidates",ctx->label);
      ABORT(R_NOT_FOUND);
    }

    while(cand){
      ctx->uninitialized_candidates++;
      cand=TAILQ_NEXT(cand,entry_comp);
    }

    /* Now initialize all the candidates */
    cand=TAILQ_FIRST(&component->candidates);
    while(cand){
      if(cand->state!=NR_ICE_CAND_STATE_INITIALIZING){
        nr_ice_candidate_initialize(cand,nr_ice_gather_finished_cb,cand);
      }
      cand=TAILQ_NEXT(cand,entry_comp);
    }
    _status=0;
 abort:
    return(_status);
  }

void nr_ice_component_stop_gathering(nr_ice_component *component)
  {
    nr_ice_candidate *c1,*c2;
    TAILQ_FOREACH_SAFE(c1, &component->candidates, entry_comp, c2){
      nr_ice_candidate_stop_gathering(c1);
    }
  }

int nr_ice_component_is_done_gathering(nr_ice_component *comp)
  {
    nr_ice_candidate *cand=TAILQ_FIRST(&comp->candidates);
    while(cand){
      if(cand->state != NR_ICE_CAND_STATE_INITIALIZED &&
         cand->state != NR_ICE_CAND_STATE_FAILED){
        return 0;
      }
      cand=TAILQ_NEXT(cand,entry_comp);
    }
    return 1;
  }


static int nr_ice_any_peer_paired(nr_ice_candidate* cand) {
  nr_ice_peer_ctx* pctx=STAILQ_FIRST(&cand->ctx->peers);
  while(pctx && pctx->state == NR_ICE_PEER_STATE_UNPAIRED){
    /* Is it worth actually looking through the check lists? Probably not. */
    pctx=STAILQ_NEXT(pctx,entry);
  }
  return pctx != NULL;
}

/*
  Compare this newly initialized candidate against the other initialized
  candidates and discard the lower-priority one if they are redundant.

   This algorithm combined with the other algorithms, favors
   host > srflx > relay
 */
int nr_ice_component_maybe_prune_candidate(nr_ice_ctx *ctx, nr_ice_component *comp, nr_ice_candidate *c1, int *was_pruned)
  {
    nr_ice_candidate *c2, *tmp = NULL;

    *was_pruned = 0;
    c2 = TAILQ_FIRST(&comp->candidates);
    while(c2){
      if((c1 != c2) &&
         (c2->state == NR_ICE_CAND_STATE_INITIALIZED) &&
         !nr_transport_addr_cmp(&c1->base,&c2->base,NR_TRANSPORT_ADDR_CMP_MODE_ALL) &&
         !nr_transport_addr_cmp(&c1->addr,&c2->addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL)){

        if((c1->type == c2->type) ||
           (!(ctx->flags & NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES) &&
            !(ctx->flags & NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES) &&
            ((c1->type==HOST && c2->type == SERVER_REFLEXIVE) ||
             (c2->type==HOST && c1->type == SERVER_REFLEXIVE)))){

          /*
             These are redundant. Remove the lower pri one, or if pairing has
             already occurred, remove the newest one.

             Since this algorithmis run whenever a new candidate
             is initialized, there should at most one duplicate.
           */
          if ((c1->priority <= c2->priority) || nr_ice_any_peer_paired(c2)) {
            tmp = c1;
            *was_pruned = 1;
          }
          else {
            tmp = c2;
          }
          break;
        }
      }

      c2=TAILQ_NEXT(c2,entry_comp);
    }

    if (tmp) {
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/CAND(%s): Removing redundant candidate",
            ctx->label,tmp->label);

      TAILQ_REMOVE(&comp->candidates,tmp,entry_comp);
      comp->candidate_ct--;
      TAILQ_REMOVE(&tmp->isock->candidates,tmp,entry_sock);

      nr_ice_candidate_destroy(&tmp);
    }

    return 0;
  }

static int nr_ice_component_pair_matches_check(nr_ice_component *comp, nr_ice_cand_pair *pair, nr_transport_addr *local_addr, nr_stun_server_request *req)
  {
    if(pair->remote->component->component_id!=comp->component_id)
      return(0);

    if(nr_transport_addr_cmp(&pair->local->base,local_addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL))
      return(0);

    if(nr_transport_addr_cmp(&pair->remote->addr,&req->src_addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL))
      return(0);

    return(1);
  }

static int nr_ice_component_handle_use_candidate(nr_ice_component *comp, nr_ice_cand_pair *pair, int *error)
  {
    int r=0,_status;

    if(comp->stream->pctx->controlling){
      r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s)/CAND_PAIR(%s): Peer sent USE-CANDIDATE but is controlled",comp->stream->pctx->label, pair->codeword);
    }
    else{
      /* If this is the first time we've noticed this is nominated...*/
      pair->peer_nominated=1;

      if(pair->state==NR_ICE_PAIR_STATE_SUCCEEDED && !pair->nominated){
        pair->nominated=1;

        if(r=nr_ice_component_nominated_pair(pair->remote->component, pair)) {
          *error=(r==R_NO_MEMORY)?500:400;
          ABORT(r);
        }
      }
    }

    _status=0;
  abort:
    return(_status);
  }

/* Section 7.2.1 */
static int nr_ice_component_process_incoming_check(nr_ice_component *comp, nr_transport_addr *local_addr, nr_stun_server_request *req, int *error)
  {
    nr_ice_cand_pair *pair;
    nr_ice_candidate *pcand=0;
    nr_stun_message *sreq=req->request;
    nr_stun_message_attribute *attr;
    int r=0,_status;
    int found_valid=0;

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): received request from %s",comp->stream->pctx->label,comp->stream->label,comp->component_id,req->src_addr.as_string);

    if (comp->state == NR_ICE_COMPONENT_DISABLED)
      ABORT(R_REJECTED);

    /* Check for role conficts (7.2.1.1) */
    if(comp->stream->pctx->controlling){
      if(nr_stun_message_has_attribute(sreq,NR_STUN_ATTR_ICE_CONTROLLING,&attr)){
        /* OK, there is a conflict. Who's right? */
        r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): role conflict, both controlling",comp->stream->pctx->label);

        if(attr->u.ice_controlling > comp->stream->pctx->tiebreaker){
          /* Update the peer ctx. This will propagate to all candidate pairs
             in the context. */
          nr_ice_peer_ctx_switch_controlling_role(comp->stream->pctx);
        }
        else {
          /* We are: throw an error */
          r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s): returning 487 role conflict",comp->stream->pctx->label);

          *error=487;
          ABORT(R_REJECTED);
        }
      }
    }
    else{
      if(nr_stun_message_has_attribute(sreq,NR_STUN_ATTR_ICE_CONTROLLED,&attr)){
        /* OK, there is a conflict. Who's right? */
        r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): role conflict, both controlled",comp->stream->pctx->label);

        if(attr->u.ice_controlled < comp->stream->pctx->tiebreaker){
          /* Update the peer ctx. This will propagate to all candidate pairs
             in the context. */
          nr_ice_peer_ctx_switch_controlling_role(comp->stream->pctx);
        }
        else {
          /* We are: throw an error */
          r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s): returning 487 role conflict",comp->stream->pctx->label);

          *error=487;
          ABORT(R_REJECTED);
        }
      }
    }

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): This STUN request appears to map to local addr %s",comp->stream->pctx->label,local_addr->as_string);

    pair=TAILQ_FIRST(&comp->stream->check_list);
    while(pair){
      /* Since triggered checks create duplicate pairs (in this implementation)
       * we are willing to handle multiple matches here. */
      if(nr_ice_component_pair_matches_check(comp, pair, local_addr, req)){
        r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND_PAIR(%s): Found a matching pair for received check: %s",comp->stream->pctx->label,pair->codeword,pair->as_string);
        int peer_nominated = pair->peer_nominated;
        if(nr_stun_message_has_attribute(req->request,NR_STUN_ATTR_USE_CANDIDATE,0)){
          if(r=nr_ice_component_handle_use_candidate(comp, pair, error)) {
            ABORT(r);
          }
        }

        int new_peer_nomination = !peer_nominated && pair->peer_nominated;
        int might_select = !comp->nominated ||
          (comp->nominated->priority < pair->priority);
        int force = new_peer_nomination && might_select;

        /* Note: the RFC says to trigger first and then nominate. But in that
         * case the canceled trigger pair would get nominated and the cloned
         * trigger pair would not get the nomination status cloned with it.*/
        if(!found_valid && (r=nr_ice_candidate_pair_do_triggered_check(comp->stream->pctx, pair, force))) {
          *error=(r==R_NO_MEMORY)?500:400;
          ABORT(r);
        }
        found_valid=1;
      }
      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    if(!found_valid){
      /* There were no matching pairs, so we need to create a new peer
       * reflexive candidate pair. */

      if(!nr_stun_message_has_attribute(sreq,NR_STUN_ATTR_PRIORITY,&attr)){
        r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s): Rejecting stun request without priority",comp->stream->pctx->label);
        *error=400;
        ABORT(R_BAD_DATA);
      }

      /* Find our local component candidate */
      nr_ice_candidate *cand;

      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): no matching pair",comp->stream->pctx->label);
      cand=TAILQ_FIRST(&comp->local_component->candidates);
      while(cand){
        if(!nr_transport_addr_cmp(&cand->addr,local_addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL))
          break;

        cand=TAILQ_NEXT(cand,entry_comp);
      }

      /* Well, this really shouldn't happen, but it's an error from the
         other side, so we just throw an error and keep going */
      if(!cand){
        r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s): stun request to unknown local address %s, discarding",comp->stream->pctx->label,local_addr->as_string);

        *error=400;
        ABORT(R_NOT_FOUND);
      }

      /* Now make a peer reflexive (remote) candidate */
      if(r=nr_ice_peer_peer_rflx_candidate_create(comp->stream->pctx->ctx,"prflx",comp,&req->src_addr,&pcand)) {
        *error=(r==R_NO_MEMORY)?500:400;
        ABORT(r);
      }
      pcand->priority=attr->u.priority;
      pcand->state=NR_ICE_CAND_PEER_CANDIDATE_PAIRED;

      /* Finally, create the candidate pair, insert into the check list, and
       * apply the incoming check to it. */
      if(r=nr_ice_candidate_pair_create(comp->stream->pctx,cand,pcand,
           &pair)) {
        *error=(r==R_NO_MEMORY)?500:400;
        ABORT(r);
      }

      nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FROZEN);
      if(r=nr_ice_component_insert_pair(comp,pair)) {
        *error=(r==R_NO_MEMORY)?500:400;
        ABORT(r);
      }

      /* Do this last, since any call to ABORT will destroy pcand */
      TAILQ_INSERT_TAIL(&comp->candidates,pcand,entry_comp);
      pcand=0;

      if(nr_stun_message_has_attribute(req->request,NR_STUN_ATTR_USE_CANDIDATE,0)){
        if(r=nr_ice_component_handle_use_candidate(comp, pair, error)) {
          ABORT(r);
        }
      }

      /* Finally start the trigger check if needed */
      if(r=nr_ice_candidate_pair_do_triggered_check(comp->stream->pctx, pair, 0)) {
        *error=(r==R_NO_MEMORY)?500:400;
        ABORT(r);
      }
    }

    _status=0;
  abort:
    if(_status){
      nr_ice_candidate_destroy(&pcand);
      assert(*error != 0);
      if(r!=R_NO_MEMORY) assert(*error != 500);
    }
    return(_status);
  }

static int nr_ice_component_stun_server_cb(void *cb_arg,nr_stun_server_ctx *stun_ctx,nr_socket *sock, nr_stun_server_request *req, int *dont_free, int *error)
  {
    nr_ice_component *pcomp=(nr_ice_component*)cb_arg;
    nr_transport_addr local_addr;
    int r,_status;

    if(pcomp->state==NR_ICE_COMPONENT_FAILED) {
      *error=400;
      ABORT(R_REJECTED);
    }

    if (pcomp->local_component->stream->obsolete) {
      /* Don't do any triggered check stuff in thiis case. */
      return 0;
    }

    /* Find the candidate pair that this maps to */
    if(r=nr_socket_getaddr(sock,&local_addr)) {
      *error=500;
      ABORT(r);
    }

    if (r=nr_ice_component_process_incoming_check(pcomp, &local_addr, req, error))
      ABORT(r);

    _status=0;
 abort:
    return(_status);
  }

int nr_ice_component_service_pre_answer_requests(nr_ice_peer_ctx *pctx, nr_ice_component *pcomp, char *username, int *serviced)
  {
    nr_ice_pre_answer_request *r1,*r2;
    nr_ice_component *comp = pcomp->local_component;
    int r,_status;

    if (serviced)
      *serviced = 0;

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): looking for pre-answer requests",pctx->label,comp->stream->label,comp->component_id);

    STAILQ_FOREACH_SAFE(r1, &comp->pre_answer_reqs, entry, r2) {
      if (!strcmp(r1->username, username)) {
        int error = 0;

        r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): found pre-answer request",pctx->label,comp->stream->label,comp->component_id);
        r = nr_ice_component_process_incoming_check(pcomp, &r1->local_addr, &r1->req, &error);
        if (r) {
          r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): error processing pre-answer request. Would have returned %d",pctx->label,comp->stream->label,comp->component_id, error);
        }
        (*serviced)++;
        STAILQ_REMOVE(&comp->pre_answer_reqs,r1,nr_ice_pre_answer_request_, entry);
        nr_ice_pre_answer_request_destroy(&r1);
      }
    }

    _status=0;
     return(_status);
  }

int nr_ice_component_can_candidate_tcptype_pair(nr_socket_tcp_type left, nr_socket_tcp_type right)
  {
    if (left && !right)
      return(0);
    if (!left && right)
      return(0);
    if (left == TCP_TYPE_ACTIVE && right != TCP_TYPE_PASSIVE)
      return(0);
    if (left == TCP_TYPE_SO && right != TCP_TYPE_SO)
      return(0);
    if (left == TCP_TYPE_PASSIVE)
      return(0);

    return(1);
  }

/* filter out pairings which won't work. */
int nr_ice_component_can_candidate_addr_pair(nr_transport_addr *local, nr_transport_addr *remote)
  {
    if(local->ip_version != remote->ip_version)
      return(0);
    if(local->protocol != remote->protocol)
      return(0);
    if(nr_transport_addr_is_link_local(local) !=
       nr_transport_addr_is_link_local(remote))
      return(0);
    /* This prevents our ice_unittest (or broken clients) from pairing a
     * loopback with a host candidate. */
    if(nr_transport_addr_is_loopback(local) !=
       nr_transport_addr_is_loopback(remote))
      return(0);

    return(1);
  }

int nr_ice_component_pair_candidate(nr_ice_peer_ctx *pctx, nr_ice_component *pcomp, nr_ice_candidate *lcand, int pair_all_remote)
  {
    int r, _status;
    nr_ice_candidate *pcand;
    nr_ice_cand_pair *pair=0;
    char codeword[5];

    nr_ice_compute_codeword(lcand->label,strlen(lcand->label),codeword);
    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND(%s): Pairing local candidate %s",pctx->label,codeword,lcand->label);

    switch(lcand->type){
      case HOST:
        break;
      case SERVER_REFLEXIVE:
      case PEER_REFLEXIVE:
        /* Don't actually pair these candidates */
        goto done;
        break;
      case RELAYED:
        break;
      default:
        assert(0);
        ABORT(R_INTERNAL);
        break;
    }

    TAILQ_FOREACH(pcand, &pcomp->candidates, entry_comp){
      if(!nr_ice_component_can_candidate_addr_pair(&lcand->addr, &pcand->addr))
        continue;
      if(!nr_ice_component_can_candidate_tcptype_pair(lcand->tcp_type, pcand->tcp_type))
        continue;

      /* https://tools.ietf.org/html/draft-ietf-rtcweb-mdns-ice-candidates-03#section-3.3.2 */
      if(lcand->type == RELAYED && pcand->mdns_addr && strlen(pcand->mdns_addr)) {
        continue;
      }

      /*
        Two modes, depending on |pair_all_remote|

        1. Pair remote candidates which have not been paired
           (used in initial pairing or in processing the other side's
           trickle candidates).
        2. Pair any remote candidate (used when processing our own
           trickle candidates).
      */
      if (pair_all_remote || (pcand->state == NR_ICE_CAND_PEER_CANDIDATE_UNPAIRED)) {
        if (pair_all_remote) {
          /* When a remote candidate arrives after the start of checking, but
           * before the gathering of local candidates, it can be in UNPAIRED */
          pcand->state = NR_ICE_CAND_PEER_CANDIDATE_PAIRED;
        }

        nr_ice_compute_codeword(pcand->label,strlen(pcand->label),codeword);
        r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND(%s): Pairing with peer candidate %s", pctx->label, codeword, pcand->label);

        if(r=nr_ice_candidate_pair_create(pctx,lcand,pcand,&pair))
          ABORT(r);

        if(r=nr_ice_component_insert_pair(pcomp, pair))
          ABORT(r);
      }
    }

   done:
    _status = 0;
   abort:
    return(_status);
  }

int nr_ice_component_pair_candidates(nr_ice_peer_ctx *pctx, nr_ice_component *lcomp,nr_ice_component *pcomp)
  {
    nr_ice_candidate *lcand, *pcand;
    nr_ice_socket *isock;
    int r,_status;

    r_log(LOG_ICE,LOG_DEBUG,"Pairing candidates======");

    /* Create the candidate pairs */
    lcand=TAILQ_FIRST(&lcomp->candidates);

    if (!lcand) {
      /* No local candidates, initialized or not! */
      ABORT(R_FAILED);
    }

    while(lcand){
      if (lcand->state == NR_ICE_CAND_STATE_INITIALIZED) {
        if ((r = nr_ice_component_pair_candidate(pctx, pcomp, lcand, 0)))
          ABORT(r);
      }

      lcand=TAILQ_NEXT(lcand,entry_comp);
    }

    /* Mark all peer candidates as paired */
    pcand=TAILQ_FIRST(&pcomp->candidates);
    while(pcand){
      pcand->state = NR_ICE_CAND_PEER_CANDIDATE_PAIRED;

      pcand=TAILQ_NEXT(pcand,entry_comp);

    }

    /* Now register the STUN server callback for this component.
       Note that this is a per-component CB so we only need to
       do this once.
    */
    if (pcomp->state != NR_ICE_COMPONENT_RUNNING) {
      isock=STAILQ_FIRST(&lcomp->sockets);
      while(isock){
        if(r=nr_stun_server_add_client(isock->stun_server,pctx->label,
          pcomp->stream->r2l_user,&pcomp->stream->r2l_pass,nr_ice_component_stun_server_cb,pcomp)) {
            ABORT(r);
        }
        isock=STAILQ_NEXT(isock,entry);
      }
    }

    pcomp->state = NR_ICE_COMPONENT_RUNNING;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_pre_answer_enqueue(nr_ice_component *comp, nr_socket *sock, nr_stun_server_request *req, int *dont_free)
  {
    int r = 0;
    int _status;
    nr_ice_pre_answer_request *r1, *r2;
    nr_transport_addr dst_addr;
    nr_ice_pre_answer_request *par = 0;

    if (r=nr_socket_getaddr(sock, &dst_addr))
      ABORT(r);

    STAILQ_FOREACH_SAFE(r1, &comp->pre_answer_reqs, entry, r2) {
      if (!nr_transport_addr_cmp(&r1->local_addr, &dst_addr,
                                 NR_TRANSPORT_ADDR_CMP_MODE_ALL) &&
          !nr_transport_addr_cmp(&r1->req.src_addr, &req->src_addr,
                                 NR_TRANSPORT_ADDR_CMP_MODE_ALL)) {
        return(0);
      }
    }

    if (r=nr_ice_pre_answer_request_create(&dst_addr, req, &par))
      ABORT(r);

    r_log(LOG_ICE,LOG_DEBUG, "ICE(%s)/STREAM(%s)/COMP(%d): Enqueuing STUN request pre-answer from %s",
          comp->ctx->label, comp->stream->label, comp->component_id,
          req->src_addr.as_string);

    *dont_free = 1;
    STAILQ_INSERT_TAIL(&comp->pre_answer_reqs, par, entry);

    _status=0;
abort:
    return(_status);
  }

/* Fires when we have an incoming candidate that doesn't correspond to an existing
   remote peer. This is either pre-answer or just spurious. Store it in the
   component for use when we see the actual answer, at which point we need
   to do the procedures from S 7.2.1 in nr_ice_component_stun_server_cb.
 */
static int nr_ice_component_stun_server_default_cb(void *cb_arg,nr_stun_server_ctx *stun_ctx,nr_socket *sock, nr_stun_server_request *req, int *dont_free, int *error)
  {
    int r, _status;
    nr_ice_component *comp = (nr_ice_component *)cb_arg;

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/STREAM(%s)/COMP(%d): Received STUN request pre-answer from %s",
          comp->ctx->label, comp->stream->label, comp->component_id,
          req->src_addr.as_string);

    if (r=nr_ice_pre_answer_enqueue(comp, sock, req, dont_free)) {
      r_log(LOG_ICE,LOG_ERR,"ICE(%s)/STREAM(%s)/COMP(%d): Failed (%d) to enque pre-answer request from %s",
          comp->ctx->label, comp->stream->label, comp->component_id, r,
          req->src_addr.as_string);
      ABORT(r);
    }

    _status=0;
 abort:
    return(_status);
  }

#define NR_ICE_CONSENT_TIMER_DEFAULT 5000
#define NR_ICE_CONSENT_TIMEOUT_DEFAULT 30000

static void nr_ice_component_consent_failed(nr_ice_component *comp)
  {
    if (!comp->can_send) {
      return;
    }

    r_log(LOG_ICE,LOG_INFO,"ICE(%s)/STREAM(%s)/COMP(%d): Consent refresh failed",
          comp->ctx->label, comp->stream->label, comp->component_id);
    comp->can_send = 0;

    if (comp->consent_timeout) {
      NR_async_timer_cancel(comp->consent_timeout);
      comp->consent_timeout = 0;
    }
    if (comp->consent_timer) {
      NR_async_timer_cancel(comp->consent_timer);
      comp->consent_timer = 0;
    }
    /* We are turning the consent failure into a ICE component failure to
     * alert the browser via ICE connection state change about this event. */
    nr_ice_media_stream_component_failed(comp->stream, comp);
  }

static void nr_ice_component_consent_timeout_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_component *comp=(nr_ice_component*)cb_arg;

    comp->consent_timeout = 0;

    r_log(LOG_ICE,LOG_WARNING,"ICE(%s)/STREAM(%s)/COMP(%d): Consent refresh final time out",
          comp->ctx->label, comp->stream->label, comp->component_id);
    nr_ice_component_consent_failed(comp);
  }


void nr_ice_component_disconnected(nr_ice_component *comp)
  {
    if (!comp->can_send) {
      return;
    }

    if (comp->disconnected) {
      return;
    }

    r_log(LOG_ICE,LOG_WARNING,"ICE(%s)/STREAM(%s)/COMP(%d): component disconnected",
          comp->ctx->label, comp->stream->label, comp->component_id);
    comp->disconnected = 1;

    /* a single disconnected component disconnects the stream */
    nr_ice_media_stream_set_disconnected(comp->stream, NR_ICE_MEDIA_STREAM_DISCONNECTED);
  }

static void nr_ice_component_consent_refreshed(nr_ice_component *comp)
  {
    uint16_t tval;

    if (!comp->can_send) {
      return;
    }

    gettimeofday(&comp->consent_last_seen, 0);
    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/STREAM(%s)/COMP(%d): consent_last_seen is now %lu",
        comp->ctx->label, comp->stream->label, comp->component_id,
        comp->consent_last_seen.tv_sec);

    comp->disconnected = 0;

    nr_ice_media_stream_check_if_connected(comp->stream);

    if (comp->consent_timeout)
      NR_async_timer_cancel(comp->consent_timeout);

    tval = NR_ICE_CONSENT_TIMEOUT_DEFAULT;
    if (comp->ctx->test_timer_divider)
      tval = tval / comp->ctx->test_timer_divider;

    NR_ASYNC_TIMER_SET(tval, nr_ice_component_consent_timeout_cb, comp,
                       &comp->consent_timeout);
  }

static void nr_ice_component_refresh_consent_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_cand_pair *pair=(nr_ice_cand_pair*)cb_arg;
    assert(pair && pair->remote && pair->remote->component);
    nr_ice_component *comp=pair->remote->component;

    switch (comp->consent_ctx->state) {
      case NR_STUN_CLIENT_STATE_FAILED:
        if (comp->consent_ctx->error_code == 403) {
          r_log(LOG_ICE, LOG_INFO, "ICE(%s)/STREAM(%s)/COMP(%d): Consent revoked by peer",
                comp->ctx->label, comp->stream->label, comp->component_id);
          nr_ice_component_consent_failed(comp);
        }
        break;
      case NR_STUN_CLIENT_STATE_DONE:
        r_log(LOG_ICE, LOG_INFO, "ICE(%s)/STREAM(%s)/COMP(%d): Consent refreshed",
              comp->ctx->label, comp->stream->label, comp->component_id);
        if (comp->consent_ctx->rtt_valid) {
          nr_ice_candidate_pair_update_rtt(pair, comp->consent_ctx->rtt_ms);
          // clear rtt_ms so we can't double process it.
          comp->consent_ctx->rtt_valid = 0;
          comp->consent_ctx->rtt_ms = 0;
        }
        nr_ice_component_consent_refreshed(comp);
        break;
      case NR_STUN_CLIENT_STATE_TIMED_OUT:
        r_log(LOG_ICE, LOG_INFO, "ICE(%s)/STREAM(%s)/COMP(%d): A single consent refresh request timed out",
              comp->ctx->label, comp->stream->label, comp->component_id);
        nr_ice_component_disconnected(comp);
        break;
      default:
        break;
    }
  }

int nr_ice_component_refresh_consent(nr_stun_client_ctx *ctx, NR_async_cb finished_cb, void *cb_arg)
  {
    int r,_status;

    nr_stun_client_reset(ctx);

    if (r=nr_stun_client_start(ctx, NR_ICE_CLIENT_MODE_BINDING_REQUEST, finished_cb, cb_arg))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }

void nr_ice_component_consent_calc_consent_timer(nr_ice_component *comp)
  {
    uint16_t trange, trand, tval;

    trange = NR_ICE_CONSENT_TIMER_DEFAULT * 20 / 100;
    tval = NR_ICE_CONSENT_TIMER_DEFAULT - trange;
    if (!nr_crypto_random_bytes((UCHAR*)&trand, sizeof(trand)))
      tval += (trand % (trange * 2));

    if (comp->ctx->test_timer_divider)
      tval = tval / comp->ctx->test_timer_divider;

    /* The timeout of the transaction is the maximum time until we send the
     * next consent request. */
    comp->consent_ctx->maximum_transmits_timeout_ms = tval;
  }

static void nr_ice_component_consent_timer_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_component *comp=(nr_ice_component*)cb_arg;
    int r;

    if (!comp->consent_ctx) {
      return;
    }

    if (comp->consent_timer) {
      NR_async_timer_cancel(comp->consent_timer);
    }
    comp->consent_timer = 0;

    comp->consent_ctx->params.ice_binding_request.username =
      comp->stream->l2r_user;
    comp->consent_ctx->params.ice_binding_request.password =
      comp->stream->l2r_pass;
    comp->consent_ctx->params.ice_binding_request.control =
      comp->stream->pctx->controlling?
      NR_ICE_CONTROLLING:NR_ICE_CONTROLLED;
    comp->consent_ctx->params.ice_binding_request.tiebreaker =
      comp->stream->pctx->tiebreaker;
    comp->consent_ctx->params.ice_binding_request.priority =
      comp->active->local->priority;

    nr_ice_component_consent_calc_consent_timer(comp);

    if (r=nr_ice_component_refresh_consent(comp->consent_ctx,
                                           nr_ice_component_refresh_consent_cb,
                                           comp->active)) {
      r_log(LOG_ICE,LOG_ERR,"ICE(%s)/STREAM(%s)/COMP(%d): Refresh consent failed with %d",
            comp->ctx->label, comp->stream->label, comp->component_id, r);
    }

    nr_ice_component_consent_schedule_consent_timer(comp);

  }

void nr_ice_component_consent_schedule_consent_timer(nr_ice_component *comp)
  {
    if (!comp->can_send) {
      return;
    }

    NR_ASYNC_TIMER_SET(comp->consent_ctx->maximum_transmits_timeout_ms,
                       nr_ice_component_consent_timer_cb, comp,
                       &comp->consent_timer);
  }

void nr_ice_component_refresh_consent_now(nr_ice_component *comp)
  {
    nr_ice_component_consent_timer_cb(0, 0, comp);
  }

void nr_ice_component_consent_destroy(nr_ice_component *comp)
  {
    if (comp->consent_timer) {
      NR_async_timer_cancel(comp->consent_timer);
      comp->consent_timer = 0;
    }
    if (comp->consent_timeout) {
      NR_async_timer_cancel(comp->consent_timeout);
      comp->consent_timeout = 0;
    }
    if (comp->consent_handle) {
      nr_ice_socket_deregister(comp->active->local->isock,
                               comp->consent_handle);
      comp->consent_handle = 0;
    }
    if (comp->consent_ctx) {
      nr_stun_client_ctx_destroy(&comp->consent_ctx);
      comp->consent_ctx = 0;
    }
  }

int nr_ice_component_setup_consent(nr_ice_component *comp)
  {
    int r,_status;

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s)/STREAM(%s)/COMP(%d): Setting up refresh consent",
          comp->ctx->label, comp->stream->label, comp->component_id);

    nr_ice_component_consent_destroy(comp);

    int flags = NR_STUN_TRANSPORT_ADDR_CHECK_WILDCARD;
    if (!(comp->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LOOPBACK;
    }
    if (!(comp->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LINK_LOCAL;
    }

    if (r = nr_stun_client_ctx_create("consent", comp->active->local->osock,
                                      &comp->active->remote->addr, 0, flags,
                                      &comp->consent_ctx))
      ABORT(r);
    /* Consent request get send only once. */
    comp->consent_ctx->maximum_transmits = 1;

    if (r=nr_ice_socket_register_stun_client(comp->active->local->isock,
            comp->consent_ctx, &comp->consent_handle))
      ABORT(r);

    comp->can_send = 1;
    comp->disconnected = 0;
    nr_ice_component_consent_refreshed(comp);

    nr_ice_component_consent_calc_consent_timer(comp);
    nr_ice_component_consent_schedule_consent_timer(comp);

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_component_nominated_pair(nr_ice_component *comp, nr_ice_cand_pair *pair)
  {
    int r,_status;
    nr_ice_cand_pair *p2;

    /* Are we changing what the nominated pair is? */
    if(comp->nominated){
      if(comp->nominated->priority >= pair->priority)
        return(0);
      r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d)/CAND-PAIR(%s): replacing pair %s with CAND-PAIR(%s)",comp->stream->pctx->label,comp->stream->label,comp->component_id,comp->nominated->codeword,comp->nominated->as_string,pair->codeword);
      /* As consent doesn't hold a reference to its isock this needs to happen
       * before making the new pair the active one. */
      nr_ice_component_consent_destroy(comp);
    }

    /* Set the new nominated pair */
    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d)/CAND-PAIR(%s): nominated pair is %s",comp->stream->pctx->label,comp->stream->label,comp->component_id,pair->codeword,pair->as_string);
    comp->state=NR_ICE_COMPONENT_NOMINATED;
    comp->nominated=pair;
    comp->active=pair;

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d)/CAND-PAIR(%s): cancelling all pairs but %s",comp->stream->pctx->label,comp->stream->label,comp->component_id,pair->codeword,pair->as_string);

    /* Cancel checks in WAITING and FROZEN per ICE S 8.1.2 */
    /* DO NOT CANCEL HIGHER PRIORITY PEER NOMINATED PAIRS!!! If a pair has been
     * peer nominated, we _must_ pursue it to completion, because if this is
     * the highest priority working pair from the peer's perspective, this is
     * the one it will use! This is a spec bug. */
    p2=TAILQ_FIRST(&comp->stream->trigger_check_queue);
    while(p2){
      if((p2 != pair) &&
         (p2->remote->component->component_id == comp->component_id) &&
         !(p2->peer_nominated && (p2->priority > pair->priority))) {
        assert(p2->state == NR_ICE_PAIR_STATE_WAITING ||
               p2->state == NR_ICE_PAIR_STATE_CANCELLED);
        r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d)/CAND-PAIR(%s): cancelling FROZEN/WAITING pair %s in trigger check queue because CAND-PAIR(%s) was nominated.",comp->stream->pctx->label,comp->stream->label,comp->component_id,p2->codeword,p2->as_string,pair->codeword);

        nr_ice_candidate_pair_cancel(pair->pctx,p2,0);
      }

      p2=TAILQ_NEXT(p2,triggered_check_queue_entry);
    }
    p2=TAILQ_FIRST(&comp->stream->check_list);
    while(p2){
      if((p2 != pair) &&
         (p2->remote->component->component_id == comp->component_id) &&
         ((p2->state == NR_ICE_PAIR_STATE_FROZEN) ||
          (p2->state == NR_ICE_PAIR_STATE_WAITING)) &&
         !(p2->peer_nominated && (p2->priority > pair->priority))) {
        r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d)/CAND-PAIR(%s): cancelling FROZEN/WAITING pair %s because CAND-PAIR(%s) was nominated.",comp->stream->pctx->label,comp->stream->label,comp->component_id,p2->codeword,p2->as_string,pair->codeword);

        nr_ice_candidate_pair_cancel(pair->pctx,p2,0);
      }

      p2=TAILQ_NEXT(p2,check_queue_entry);
    }
    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): cancelling done",comp->stream->pctx->label,comp->stream->label,comp->component_id);

    if(r=nr_ice_component_setup_consent(comp))
      ABORT(r);

    nr_ice_media_stream_component_nominated(comp->stream,comp);

    _status=0;
  abort:
    return(_status);
  }

static int nr_ice_component_have_all_pairs_failed(nr_ice_component *comp)
  {
    nr_ice_cand_pair *p2;

    p2=TAILQ_FIRST(&comp->stream->check_list);
    while(p2){
      if(comp->component_id==p2->local->component_id){
        switch(p2->state){
        case NR_ICE_PAIR_STATE_FROZEN:
        case NR_ICE_PAIR_STATE_WAITING:
        case NR_ICE_PAIR_STATE_IN_PROGRESS:
        case NR_ICE_PAIR_STATE_SUCCEEDED:
            return(0);
        case NR_ICE_PAIR_STATE_FAILED:
        case NR_ICE_PAIR_STATE_CANCELLED:
            /* states that will never be recovered from */
            break;
        default:
            assert(0);
            break;
        }
      }

      p2=TAILQ_NEXT(p2,check_queue_entry);
    }

    return(1);
  }

void nr_ice_component_failed_pair(nr_ice_component *comp, nr_ice_cand_pair *pair)
  {
    nr_ice_component_check_if_failed(comp);
  }

void nr_ice_component_check_if_failed(nr_ice_component *comp)
  {
    if (comp->state == NR_ICE_COMPONENT_RUNNING) {
      /* Don't do anything to streams that aren't currently running */
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): Checking whether component needs to be marked failed.",comp->stream->pctx->label,comp->stream->label,comp->component_id);

      if (!comp->stream->pctx->trickle_grace_period_timer &&
          nr_ice_component_have_all_pairs_failed(comp)) {
        r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): All pairs are failed, and grace period has elapsed. Marking component as failed.",comp->stream->pctx->label,comp->stream->label,comp->component_id);
        nr_ice_media_stream_component_failed(comp->stream,comp);
      }
    }
  }

void nr_ice_component_maybe_select_pair(nr_ice_peer_ctx *pctx, nr_ice_component *comp)
  {
    nr_ice_cand_pair **pairs=0;
    int ct=0;
    nr_ice_cand_pair *pair;
    int r;

    if(comp->nominated)
      return;

    assert(pctx->controlling);
    assert(!nr_ice_peer_ctx_aggressive_nomination(pctx));

    /* Size the array */
    pair=TAILQ_FIRST(&comp->stream->check_list);
    while(pair){
      if (comp->component_id == pair->local->component_id){
        if(pair->nominated ||
           (pair->state == NR_ICE_PAIR_STATE_IN_PROGRESS &&
            pair->stun_client->mode == NR_ICE_CLIENT_MODE_USE_CANDIDATE))
          return;

        ct++;
      }

      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    /* Make and fill the array */
    if(!(pairs=R_NEW_CNT(nr_ice_cand_pair*, ct))){
      r_log(LOG_ICE,LOG_ERR,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): Failed to allocate candidate pairs",pctx->label,comp->stream->label,comp->component_id);
      return;
    }

    ct=0;
    pair=TAILQ_FIRST(&comp->stream->check_list);
    while(pair){
      if (comp->component_id == pair->local->component_id)
          pairs[ct++]=pair;

      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    if (pctx->handler) {
      if(r=pctx->handler->vtbl->select_pair(pctx->handler->obj,
        comp->stream,comp->component_id,pairs,ct))
        r_log(LOG_ICE,LOG_ERR,"ICE-PEER(%s)/STREAM(%s)/COMP(%d): Pair selection callback failed with %d",pctx->label,comp->stream->label,comp->component_id,r);
    }

    free(pairs);
  }


/* Close the underlying sockets for everything but the nominated candidate */
int nr_ice_component_finalize(nr_ice_component *lcomp, nr_ice_component *rcomp)
  {
    nr_ice_socket *isock=0;
    nr_ice_socket *s1,*s2;

    if(rcomp->state==NR_ICE_COMPONENT_NOMINATED){
      assert(rcomp->active == rcomp->nominated);
      isock=rcomp->nominated->local->isock;
    }

    STAILQ_FOREACH_SAFE(s1, &lcomp->sockets, entry, s2){
      if(s1!=isock){
        STAILQ_REMOVE(&lcomp->sockets,s1,nr_ice_socket_,entry);
        nr_ice_socket_destroy(&s1);
      }
    }

    return(0);
  }


int nr_ice_component_insert_pair(nr_ice_component *pcomp, nr_ice_cand_pair *pair)
  {
    int _status;

    /* Pairs for peer reflexive are marked SUCCEEDED immediately */
    if (pair->state != NR_ICE_PAIR_STATE_FROZEN &&
        pair->state != NR_ICE_PAIR_STATE_SUCCEEDED){
      assert(0);
      ABORT(R_BAD_ARGS);
    }

    /* We do not throw an error after this, because we've inserted the pair. */
    nr_ice_candidate_pair_insert(&pair->remote->stream->check_list,pair);

    /* Make sure the check timer is running, if the stream was previously
     * started. We will not start streams just because a pair was created,
     * unless it is the first pair to be created across all streams. */
    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND-PAIR(%s): Ensure that check timer is running for new pair %s.",pair->remote->stream->pctx->label, pair->codeword, pair->as_string);

    if(pair->remote->stream->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE ||
       (pair->remote->stream->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_FROZEN &&
        !pair->remote->stream->pctx->checks_started)){
      if(nr_ice_media_stream_start_checks(pair->remote->stream->pctx, pair->remote->stream)) {
        r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s)/CAND-PAIR(%s): Could not restart checks for new pair %s.",pair->remote->stream->pctx->label, pair->codeword, pair->as_string);
      }
    }

    _status=0;
  abort:
    if (_status) {
      nr_ice_candidate_pair_destroy(&pair);
    }
    return(_status);
  }

int nr_ice_component_get_default_candidate(nr_ice_component *comp, nr_ice_candidate **candp, int ip_version)
  {
    int _status;
    nr_ice_candidate *cand;
    nr_ice_candidate *best_cand = NULL;

    /* We have the component. Now find the "best" candidate, making
       use of the fact that more "reliable" candidate types have
       higher numbers. So, we sort by type and then priority within
       type
    */
    cand=TAILQ_FIRST(&comp->candidates);
    while(cand){
      if (!nr_ice_ctx_hide_candidate(comp->ctx, cand) &&
          cand->addr.ip_version == ip_version) {
        if (!best_cand) {
          best_cand = cand;
        }
        else if (best_cand->type < cand->type) {
          best_cand = cand;
        } else if (best_cand->type == cand->type &&
                   best_cand->priority < cand->priority) {
          best_cand = cand;
        }
      }

      cand=TAILQ_NEXT(cand,entry_comp);
    }

    /* No candidates */
    if (!best_cand)
      ABORT(R_NOT_FOUND);

    *candp = best_cand;

    _status=0;
  abort:
    return(_status);

  }


void nr_ice_component_dump_state(nr_ice_component *comp, int log_level)
  {
    nr_ice_candidate *cand;

    if (comp->local_component) {
      r_log(LOG_ICE,log_level,"ICE(%s)/ICE-STREAM(%s): Remote component %d in state %d - dumping candidates",comp->ctx->label,comp->stream->label,comp->component_id,comp->state);
    } else {
      r_log(LOG_ICE,log_level,"ICE(%s)/ICE-STREAM(%s): Local component %d - dumping candidates",comp->ctx->label,comp->stream->label,comp->component_id);
    }

    cand=TAILQ_FIRST(&comp->candidates);
    while(cand){
      r_log(LOG_ICE,log_level,"ICE(%s)/ICE-STREAM(%s)/CAND(%s): %s",comp->ctx->label,comp->stream->label,cand->codeword,cand->label);
      cand=TAILQ_NEXT(cand,entry_comp);
    }
  }

