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

#include <assert.h>
#include <string.h>
#include "nr_api.h"
#include "ice_ctx.h"
#include "stun.h"
#include "nr_socket_buffered_stun.h"
#include "nr_socket_multi_tcp.h"

static void nr_ice_socket_readable_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    int r;
    nr_ice_stun_ctx *sc1,*sc2;
    nr_ice_socket *sock=(nr_ice_socket*)cb_arg;
    UCHAR buf[9216];
    char string[256];
    nr_transport_addr addr;
    int len;
    size_t len_s;
    int is_stun;
    int is_req;
    int is_ind;
    int processed_indication=0;

    nr_socket *stun_srv_sock=sock->sock;

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Socket ready to read",sock->ctx->label);

    if(r=nr_socket_recvfrom(sock->sock,buf,sizeof(buf),&len_s,0,&addr)){
      if (r != R_WOULDBLOCK && (sock->type != NR_ICE_SOCKET_TYPE_DGRAM)) {
        /* Report this error upward. Bug 946423 */
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): Error %d on reliable socket(%p). Abandoning.",sock->ctx->label, r, s);
        nr_ice_socket_failed(sock);
        return;
      }
    }

    if (sock->type != NR_ICE_SOCKET_TYPE_STREAM_TCP) {
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): rearming",sock->ctx->label);
      NR_ASYNC_WAIT(s,how,nr_ice_socket_readable_cb,cb_arg);
    }

    if (r) {
      return;
    }

    /* Deal with the fact that sizeof(int) and sizeof(size_t) may not
       be the same */
    if (len_s > (size_t)INT_MAX)
      return;

    len = (int)len_s;

  re_process:
    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Read %d bytes %sfrom %s",sock->ctx->label,len,(processed_indication ? "relayed " : ""),addr.as_string);

    /* First question: is this STUN or not? */
    is_stun=nr_is_stun_message(buf,len);

    if(is_stun){
      is_req=nr_is_stun_request_message(buf,len);
      is_ind=is_req?0:nr_is_stun_indication_message(buf,len);

      snprintf(string, sizeof(string)-1, "ICE(%s): Message is STUN (%s)",sock->ctx->label,
               is_req ? "request" : (is_ind ? "indication" : "other"));
      r_dump(NR_LOG_STUN, LOG_DEBUG, string, (char*)buf, len);


      /* We need to offer it to all of our stun contexts
         to see who bites */
      sc1=TAILQ_FIRST(&sock->stun_ctxs);
      while(sc1){
        sc2=TAILQ_NEXT(sc1,entry);

        r=-1;
        switch(sc1->type){
          /* This has been deleted, prune... */
          case NR_ICE_STUN_NONE:
            TAILQ_REMOVE(&sock->stun_ctxs,sc1,entry);
            free(sc1);
            break;

          case NR_ICE_STUN_CLIENT:
            if(!(is_req||is_ind)){
                r=nr_stun_client_process_response(sc1->u.client,buf,len,&addr);
            }
            break;

          case NR_ICE_STUN_SERVER:
            // A pooled socket has no peer yet and only placeholder
            // credentials, so it does not answer connectivity checks until a
            // component takes it and re-keys its STUN server context.
            if (is_req && sock->component->stream->defer_gathering_events) {
              r_log(LOG_ICE, LOG_DEBUG, "ICE(%s): ignoring STUN request on a pooled socket from %s",
                    sock->ctx->label, addr.as_string);
              break;
            }
            if(is_req){
              r=nr_stun_server_process_request(sc1->u.server,stun_srv_sock,(char *)buf,len,&addr,NR_STUN_AUTH_RULE_SHORT_TERM);
            }
            break;
          case NR_ICE_TURN_CLIENT:
            /* data indications are ok, so don't ignore those */
            /* Check that this is from the right TURN server address. Else
               skip */
            if (nr_transport_addr_cmp(
                    &sc1->u.turn_client.turn_client->turn_server_addr,
                    &addr, NR_TRANSPORT_ADDR_CMP_MODE_ALL))
              break;

            if(!is_req){
              if(!is_ind)
                r=nr_turn_client_process_response(sc1->u.turn_client.turn_client,buf,len,&addr);
              else{
                nr_transport_addr n_addr;
                size_t n_len;

                if (processed_indication) {
                  /* Don't allow recursively wrapped indications */
                  r_log(LOG_ICE, LOG_WARNING,
                        "ICE(%s): discarding recursively wrapped indication",
                        sock->ctx->label);
                  break;
                }
                /* This is a bit of a hack. If it's a data indication, strip
                   off the TURN framing and re-enter. This works because
                   all STUN processing is on the same physical socket.
                   We don't care about other kinds of indication */
                r=nr_turn_client_parse_data_indication(
                    sc1->u.turn_client.turn_client, &addr,
                    buf, len, buf, &n_len, len, &n_addr);
                if(!r){
                  r_log(LOG_ICE,LOG_DEBUG,"Unwrapped a data indication.");
                  len=n_len;
                  nr_transport_addr_copy(&addr,&n_addr);
                  stun_srv_sock=sc1->u.turn_client.turn_sock;
                  processed_indication=1;
                  goto re_process;
                }
              }
            }
            break;

          default:
            assert(0); /* Can't happen */
            return;
        }
        if(!r) {
          break;
        }

        sc1=sc2;
      }
      if(!sc1){
        if (nr_ice_ctx_is_known_id(sock->ctx,((nr_stun_message_header*)buf)->id.octet))
            r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Message is a retransmit",sock->ctx->label);
        else
            r_log(LOG_ICE,LOG_NOTICE,"ICE(%s): Message does not correspond to any registered stun ctx",sock->ctx->label);
      }
    }
    else{
      r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): Message is not STUN",sock->ctx->label);

      nr_ice_ctx_deliver_packet(sock->ctx, sock->component, &addr, buf, len);
    }

    return;
  }

int nr_ice_socket_create(nr_ice_ctx *ctx,nr_ice_component *comp, nr_socket *nsock, int type, nr_ice_socket **sockp)
  {
    nr_ice_socket *sock=0;
    NR_SOCKET fd;
    nr_transport_addr addr;
    int r,_status;

    if(!(sock=R_NEW(nr_ice_socket)))
      ABORT(R_NO_MEMORY);

    sock->sock=nsock;
    sock->ctx=ctx;
    sock->component=comp;

    if(r=nr_socket_getaddr(nsock, &addr))
      ABORT(r);

    if (type == NR_ICE_SOCKET_TYPE_DGRAM) {
      assert(addr.protocol == IPPROTO_UDP);
    }
    else {
      assert(addr.protocol == IPPROTO_TCP);
    }
    sock->type=type;

    TAILQ_INIT(&sock->candidates);
    TAILQ_INIT(&sock->stun_ctxs);

    if (sock->type == NR_ICE_SOCKET_TYPE_DGRAM){
      if((r=nr_socket_getfd(nsock,&fd)))
        ABORT(r);
      NR_ASYNC_WAIT(fd,NR_ASYNC_WAIT_READ,nr_ice_socket_readable_cb,sock);
    }
    else if (sock->type == NR_ICE_SOCKET_TYPE_STREAM_TURN) {
      /* some OS's (e.g. Linux) don't like to see un-connected TCP sockets in
       * the poll socket set. */
      nr_socket_buffered_stun_set_readable_cb(nsock,nr_ice_socket_readable_cb,sock);
    }
    else if (sock->type == NR_ICE_SOCKET_TYPE_STREAM_TCP) {
      /* in this case we can't hook up using NR_ASYNC_WAIT, because nr_socket_multi_tcp
         consists of multiple nr_sockets and file descriptors. */
      if((r=nr_socket_multi_tcp_set_readable_cb(nsock,nr_ice_socket_readable_cb,sock)))
        ABORT(r);
    }

    *sockp=sock;

    _status=0;
  abort:
    if(_status) free(sock);
    return(_status);
  }


int nr_ice_socket_destroy(nr_ice_socket **isockp)
  {
    nr_ice_stun_ctx *s1,*s2;
    nr_ice_socket *isock;

    if(!isockp || !*isockp)
      return(0);

    isock=*isockp;
    *isockp=0;

    /* Close the socket */
    nr_ice_socket_close(isock);

    /* The STUN server */
    nr_stun_server_ctx_destroy(&isock->stun_server);

    /* Now clean up the STUN ctxs */
    TAILQ_FOREACH_SAFE(s1, &isock->stun_ctxs, entry, s2){
      TAILQ_REMOVE(&isock->stun_ctxs, s1, entry);
      free(s1);
    }

    free(isock);

    return(0);
  }

int nr_ice_socket_close(nr_ice_socket *isock)
  {
#ifdef NR_SOCKET_IS_VOID_PTR
    NR_SOCKET fd=NULL;
    NR_SOCKET no_socket = NULL;
#else
    NR_SOCKET fd=-1;
    NR_SOCKET no_socket = -1;
#endif

    if (!isock||!isock->sock)
      return(0);

    if (isock->type != NR_ICE_SOCKET_TYPE_STREAM_TCP){
      nr_socket_getfd(isock->sock,&fd);
      assert(isock->sock!=0);
      if(fd != no_socket){
        NR_ASYNC_CANCEL(fd,NR_ASYNC_WAIT_READ);
        NR_ASYNC_CANCEL(fd,NR_ASYNC_WAIT_WRITE);
      }
    }
    nr_socket_destroy(&isock->sock);

    return(0);
  }

int nr_ice_socket_register_stun_client(nr_ice_socket *sock, nr_stun_client_ctx *srv,void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=R_NEW(nr_ice_stun_ctx)))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_STUN_CLIENT;
    sc->u.client=srv;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);

    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_socket_register_stun_server(nr_ice_socket *sock, nr_stun_server_ctx *srv,void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=R_NEW(nr_ice_stun_ctx)))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_STUN_SERVER;
    sc->u.server=srv;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);

    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_socket_register_turn_client(nr_ice_socket *sock, nr_turn_client_ctx *srv,
                                       nr_socket *turn_socket, void **handle)
  {
    nr_ice_stun_ctx *sc=0;
    int _status;

    if(!(sc=R_NEW(nr_ice_stun_ctx)))
      ABORT(R_NO_MEMORY);

    sc->type=NR_ICE_TURN_CLIENT;
    sc->u.turn_client.turn_client=srv;
    sc->u.turn_client.turn_sock=turn_socket;

    TAILQ_INSERT_TAIL(&sock->stun_ctxs,sc,entry);

    *handle=sc;

    _status=0;
  abort:
    return(_status);
  }

/* Just mark it deregistered. Don't delete it now because it's not safe
   in the CB, which is where this is likely to be called */
int nr_ice_socket_deregister(nr_ice_socket *sock, void *handle)
  {
    nr_ice_stun_ctx *sc=(nr_ice_stun_ctx*)handle;

    if(!sc)
      return(0);

    sc->type=NR_ICE_STUN_NONE;

    return(0);
  }

void nr_ice_socket_failed(nr_ice_socket *sock)
  {
    nr_ice_stun_ctx *s1,*s2;
    TAILQ_FOREACH_SAFE(s1, &sock->stun_ctxs, entry, s2){
      switch (s1->type) {
        case NR_ICE_STUN_NONE:
          break;
        case NR_ICE_STUN_CLIENT:
          nr_stun_client_failed(s1->u.client);
          break;
        case NR_ICE_STUN_SERVER:
          /* Nothing to do here? */
          break;
        case NR_ICE_TURN_CLIENT:
          nr_turn_client_failed(s1->u.turn_client.turn_client);
          break;
        default:
          assert(0);
      }
    }
  }

