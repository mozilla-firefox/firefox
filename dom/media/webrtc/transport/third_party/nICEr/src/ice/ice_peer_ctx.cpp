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
#include <registry.h>
#include <nr_api.h>
#include "ice_ctx.h"
#include "ice_peer_ctx.h"
#include "ice_media_stream.h"
#include "ice_util.h"
#include "nr_crypto.h"
#include "async_timer.h"
#include "ice_reg.h"

static void nr_ice_peer_ctx_parse_stream_attributes_int(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, nr_ice_media_stream *pstream, char **attrs, int attr_ct);
static int nr_ice_ctx_parse_candidate(nr_ice_peer_ctx *pctx, nr_ice_media_stream *pstream, char *candidate, int trickled, const char *mdns_addr);
static void nr_ice_peer_ctx_start_trickle_timer(nr_ice_peer_ctx *pctx);

int nr_ice_peer_ctx_create(nr_ice_ctx *ctx, nr_ice_handler *handler,char *label, nr_ice_peer_ctx **pctxp)
  {
    int r,_status;
    nr_ice_peer_ctx *pctx=0;

    if(!(pctx=R_NEW(nr_ice_peer_ctx)))
      ABORT(R_NO_MEMORY);

    pctx->state = NR_ICE_PEER_STATE_UNPAIRED;

    if(!(pctx->label=strdup(label)))
      ABORT(R_NO_MEMORY);

    pctx->ctx=ctx;
    pctx->handler=handler;

    /* Decide controlling vs. controlled */
    if(ctx->flags & NR_ICE_CTX_FLAGS_LITE){
      pctx->controlling=0;
    } else {
      pctx->controlling=1;
    }
    if(r=nr_crypto_random_bytes((UCHAR *)&pctx->tiebreaker,8))
      ABORT(r);

    STAILQ_INIT(&pctx->peer_streams);

    STAILQ_INSERT_TAIL(&ctx->peers,pctx,entry);

    *pctxp=pctx;

    _status = 0;
  abort:
    if(_status){
      nr_ice_peer_ctx_destroy(&pctx);
    }
    return(_status);
  }

int nr_ice_peer_ctx_aggressive_nomination(nr_ice_peer_ctx *pctx)
  {
    return (pctx->ctx->flags & NR_ICE_CTX_FLAGS_AGGRESSIVE_NOMINATION) &&
      !pctx->peer_lite;
  }



int nr_ice_peer_ctx_parse_stream_attributes(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, char **attrs, int attr_ct)
  {
    nr_ice_media_stream *pstream=0;
    nr_ice_component *comp,*comp2;
    char *lufrag,*rufrag;
    char *lpwd,*rpwd;
    int r,_status;

    /*
      Note: use component_ct from our own stream since components other
      than this offered by the other side are unusable */
    if(r=nr_ice_media_stream_create(pctx->ctx,stream->label,"","",stream->component_ct,&pstream))
      ABORT(r);

    /* Match up the local and remote components */
    comp=STAILQ_FIRST(&stream->components);
    comp2=STAILQ_FIRST(&pstream->components);
    while(comp){
      comp2->local_component=comp;

      comp=STAILQ_NEXT(comp,entry);
      comp2=STAILQ_NEXT(comp2,entry);
    }

    pstream->local_stream=stream;
    pstream->pctx=pctx;

    nr_ice_peer_ctx_parse_stream_attributes_int(pctx,stream,pstream,attrs,attr_ct);

    /* Now that we have the ufrag and password, compute all the username/password
       pairs */
    lufrag=stream->ufrag;
    lpwd=stream->pwd;
    assert(lufrag);
    assert(lpwd);
    rufrag=pstream->ufrag;
    rpwd=pstream->pwd;
    if (!rufrag || !rpwd)
      ABORT(R_BAD_DATA);

    if(r=nr_concat_strings(&pstream->r2l_user,lufrag,":",rufrag,NULL))
      ABORT(r);
    if(r=nr_concat_strings(&pstream->l2r_user,rufrag,":",lufrag,NULL))
      ABORT(r);
    if(r=r_data_make(&pstream->r2l_pass, (UCHAR *)lpwd, strlen(lpwd)))
      ABORT(r);
    if(r=r_data_make(&pstream->l2r_pass, (UCHAR *)rpwd, strlen(rpwd)))
      ABORT(r);

    STAILQ_INSERT_TAIL(&pctx->peer_streams,pstream,entry);
    pstream=0;

    _status=0;
  abort:
    if (_status) {
      nr_ice_media_stream_destroy(&pstream);
    }
    return(_status);
  }

static void nr_ice_peer_ctx_parse_stream_attributes_int(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, nr_ice_media_stream *pstream, char **attrs, int attr_ct)
  {
    int r;
    int i;

    for(i=0;i<attr_ct;i++){
      if(!strncmp(attrs[i],"ice-",4)){
        if(r=nr_ice_peer_ctx_parse_media_stream_attribute(pctx,pstream,attrs[i])) {
          r_log(LOG_ICE,LOG_WARNING,"ICE(%s): peer (%s) specified bogus ICE attribute",pctx->ctx->label,pctx->label);
          continue;
        }
      }
      else if (!strncmp(attrs[i],"candidate",9)){
        if(r=nr_ice_ctx_parse_candidate(pctx,pstream,attrs[i],0,0)) {
          r_log(LOG_ICE,LOG_WARNING,"ICE(%s): peer (%s) specified bogus candidate",pctx->ctx->label,pctx->label);
          continue;
        }
      }
      else {
        r_log(LOG_ICE,LOG_WARNING,"ICE(%s): peer (%s) specified bogus attribute: %s",pctx->ctx->label,pctx->label,attrs[i]);
      }
    }

    /* Doesn't fail because we just skip errors */
  }

static int nr_ice_ctx_parse_candidate(nr_ice_peer_ctx *pctx, nr_ice_media_stream *pstream, char *candidate, int trickled, const char *mdns_addr)
  {
    nr_ice_candidate *cand=0;
    nr_ice_component *comp;
    int j;
    int r, _status;

    if(r=nr_ice_peer_candidate_from_attribute(pctx->ctx,candidate,pstream,&cand))
      ABORT(r);

    /* set the trickled flag on the candidate */
    cand->trickled = trickled;

    if (mdns_addr) {
      cand->mdns_addr = strdup(mdns_addr);
      if (!cand->mdns_addr) {
        ABORT(R_NO_MEMORY);
      }
    }

    /* Not the fastest way to find a component, but it's what we got */
    j=1;
    for(comp=STAILQ_FIRST(&pstream->components);comp;comp=STAILQ_NEXT(comp,entry)){
      if(j==cand->component_id)
        break;

      j++;
    }

    if(!comp){
      /* Very common for the answerer when it uses rtcp-mux */
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): peer (%s) no such component for candidate %s",pctx->ctx->label,pctx->label, candidate);
      ABORT(R_REJECTED);
    }

    if (comp->state == NR_ICE_COMPONENT_DISABLED) {
      r_log(LOG_ICE,LOG_WARNING,"Peer offered candidate for disabled remote component: %s", candidate);
      ABORT(R_BAD_DATA);
    }
    if (comp->local_component->state == NR_ICE_COMPONENT_DISABLED) {
      r_log(LOG_ICE,LOG_WARNING,"Peer offered candidate for disabled local component: %s", candidate);
      ABORT(R_BAD_DATA);
    }

    cand->component=comp;

    TAILQ_INSERT_TAIL(&comp->candidates,cand,entry_comp);

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND(%s): creating peer candidate",
      pctx->label,cand->label);

    _status=0;
 abort:
    if (_status) {
      nr_ice_candidate_destroy(&cand);
    }
    return(_status);
  }

int nr_ice_peer_ctx_find_pstream(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, nr_ice_media_stream **pstreamp)
  {
    int _status;
    nr_ice_media_stream *pstream;

    /* Because we don't have forward pointers, iterate through all the
       peer streams to find one that matches us */
     pstream=STAILQ_FIRST(&pctx->peer_streams);

     if(!pstream) {
       /* No peer streams at all, presumably because they do not exist yet.
        * Don't log a warning here. */
       ABORT(R_NOT_FOUND);
     }

     while(pstream) {
       if (pstream->local_stream == stream)
         break;

       pstream = STAILQ_NEXT(pstream, entry);
     }

     if (!pstream) {
       r_log(LOG_ICE,LOG_WARNING,"ICE(%s): peer (%s) has no stream matching stream %s",pctx->ctx->label,pctx->label,stream->label);
       ABORT(R_NOT_FOUND);
     }

    *pstreamp = pstream;

    _status=0;
 abort:
    return(_status);
  }

int nr_ice_peer_ctx_remove_pstream(nr_ice_peer_ctx *pctx, nr_ice_media_stream **pstreamp)
  {
    int r,_status;

    STAILQ_REMOVE(&pctx->peer_streams,*pstreamp,nr_ice_media_stream_,entry);

    if(r=nr_ice_media_stream_destroy(pstreamp)) {
      ABORT(r);
    }

    _status=0;
 abort:
    return(_status);
  }

int nr_ice_peer_ctx_parse_trickle_candidate(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, char *candidate, const char *mdns_addr)
  {
    nr_ice_media_stream *pstream;
    int r,_status;
    int needs_pairing = 0;

    if (stream->obsolete) {
      return 0;
    }

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): peer (%s) parsing trickle ICE candidate %s",pctx->ctx->label,pctx->label,candidate);
    r = nr_ice_peer_ctx_find_pstream(pctx, stream, &pstream);
    if (r)
      ABORT(r);

    switch(pstream->ice_state) {
      case NR_ICE_MEDIA_STREAM_UNPAIRED:
        break;
      case NR_ICE_MEDIA_STREAM_CHECKS_FROZEN:
      case NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE:
        needs_pairing = 1;
        break;
      default:
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s), stream(%s) tried to trickle ICE in inappropriate state %d",pctx->ctx->label,pctx->label,stream->label,pstream->ice_state);
        ABORT(R_ALREADY);
        break;
    }

    if(r=nr_ice_ctx_parse_candidate(pctx,pstream,candidate,1,mdns_addr)){
      ABORT(r);
    }

    /* If ICE is running (i.e., we are in FROZEN or ACTIVE states)
       then we need to pair this new candidate. For now we
       just re-pair the stream which is inefficient but still
       fine because we suppress duplicate pairing */
    if (needs_pairing) {
      /* Start the remote trickle grace timeout if it hasn't been started by
         another trickled candidate or from the SDP. */
      if (!pctx->trickle_grace_period_timer) {
        nr_ice_peer_ctx_start_trickle_timer(pctx);
      }

      if(r=nr_ice_media_stream_pair_candidates(pctx, stream, pstream)) {
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s), stream(%s) failed to pair trickle ICE candidates",pctx->ctx->label,pctx->label,stream->label);
        ABORT(r);
      }

      /* Start checks if this stream is not checking yet or if it has checked
         all the available candidates but not had a completed check for all
         components.

         Note that this is not compliant with RFC 5245, but consistent with
         the libjingle trickle ICE behavior. Note that we will not restart
         checks if either (a) the stream has failed or (b) all components
         have a successful pair because the switch statement above jumps
         will in both states.

         TODO(ekr@rtfm.com): restart checks.
         TODO(ekr@rtfm.com): update when the trickle ICE RFC is published
      */
      if (!pstream->timer) {
        if(r=nr_ice_media_stream_start_checks(pctx, pstream)) {
          r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s), stream(%s) failed to start checks",pctx->ctx->label,pctx->label,stream->label);
          ABORT(r);
        }
      }
    }

    _status=0;
 abort:
    return(_status);

  }


static void nr_ice_peer_ctx_trickle_wait_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_peer_ctx *pctx=(nr_ice_peer_ctx*)cb_arg;
    nr_ice_media_stream *stream;
    nr_ice_component *comp;

    pctx->trickle_grace_period_timer=0;

    r_log(LOG_ICE,LOG_INFO,"ICE(%s): peer (%s) Trickle grace period is over; marking every component with only failed pairs as failed.",pctx->ctx->label,pctx->label);

    stream=STAILQ_FIRST(&pctx->peer_streams);
    while(stream){
      comp=STAILQ_FIRST(&stream->components);
      while(comp){
        nr_ice_component_check_if_failed(comp);
        comp=STAILQ_NEXT(comp,entry);
      }
      stream=STAILQ_NEXT(stream,entry);
    }
  }

static void nr_ice_peer_ctx_start_trickle_timer(nr_ice_peer_ctx *pctx)
  {
    UINT4 grace_period_timeout=0;

    if(pctx->trickle_grace_period_timer) {
      NR_async_timer_cancel(pctx->trickle_grace_period_timer);
      pctx->trickle_grace_period_timer=0;
    }

    NR_reg_get_uint4(NR_ICE_REG_TRICKLE_GRACE_PERIOD,&grace_period_timeout);

    if (grace_period_timeout) {
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): peer (%s) starting grace period timer for %u ms",pctx->ctx->label,pctx->label, grace_period_timeout);
      /* If we're doing trickle, we need to allow a grace period for new
       * trickle candidates to arrive in case the pairs we have fail quickly. */
       NR_ASYNC_TIMER_SET(grace_period_timeout,nr_ice_peer_ctx_trickle_wait_cb,pctx,&pctx->trickle_grace_period_timer);
    }
  }

int nr_ice_peer_ctx_pair_candidates(nr_ice_peer_ctx *pctx)
  {
    nr_ice_media_stream *stream;
    int r,_status;

    if(pctx->peer_lite && !pctx->controlling) {
      if(pctx->ctx->flags & NR_ICE_CTX_FLAGS_LITE){
        r_log(LOG_ICE,LOG_ERR,"Both sides are ICE-Lite");
        ABORT(R_BAD_DATA);
      }
      nr_ice_peer_ctx_switch_controlling_role(pctx);
    }

    r_log(LOG_ICE,LOG_DEBUG,"ICE(%s): peer (%s) pairing candidates",pctx->ctx->label,pctx->label);

    if(STAILQ_EMPTY(&pctx->peer_streams)) {
        r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s) received no media stream attributes",pctx->ctx->label,pctx->label);
        ABORT(R_FAILED);
    }

    /* Set this first; if we fail partway through, we do not want to end
     * up in UNPAIRED after creating some pairs. */
    pctx->state = NR_ICE_PEER_STATE_PAIRED;

    stream=STAILQ_FIRST(&pctx->peer_streams);
    while(stream){
      if(!stream->local_stream->obsolete) {
        if(r=nr_ice_media_stream_pair_candidates(pctx, stream->local_stream,
          stream))
          ABORT(r);
      }

      stream=STAILQ_NEXT(stream,entry);
    }


    _status=0;
  abort:
    return(_status);
  }


int nr_ice_peer_ctx_pair_new_trickle_candidate(nr_ice_ctx *ctx, nr_ice_peer_ctx *pctx, nr_ice_candidate *cand)
  {
    int r, _status;
    nr_ice_media_stream *pstream;

    r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s) pairing local trickle ICE candidate %s",pctx->ctx->label,pctx->label,cand->label);
    if ((r = nr_ice_peer_ctx_find_pstream(pctx, cand->stream, &pstream)))
      ABORT(r);

    /* Start the remote trickle grace timeout if it hasn't been started
       already. */
    if (!pctx->trickle_grace_period_timer) {
      nr_ice_peer_ctx_start_trickle_timer(pctx);
    }

    if ((r = nr_ice_media_stream_pair_new_trickle_candidate(pctx, pstream, cand)))
      ABORT(r);

    _status=0;
 abort:
    return _status;
  }

  void nr_ice_peer_ctx_destroy(nr_ice_peer_ctx** pctxp) {
    if (!pctxp || !*pctxp) return;

    nr_ice_peer_ctx* pctx = *pctxp;
    nr_ice_media_stream *str1,*str2;

    /* Stop calling the handler */
    pctx->handler = 0;

    NR_async_timer_cancel(pctx->connected_cb_timer);
    free(pctx->label);

    STAILQ_FOREACH_SAFE(str1, &pctx->peer_streams, entry, str2){
      STAILQ_REMOVE(&pctx->peer_streams,str1,nr_ice_media_stream_,entry);
      nr_ice_media_stream_destroy(&str1);
    }
    assert(pctx->ctx);
    if (pctx->ctx)
      STAILQ_REMOVE(&pctx->ctx->peers, pctx, nr_ice_peer_ctx_, entry);

    if(pctx->trickle_grace_period_timer) {
      NR_async_timer_cancel(pctx->trickle_grace_period_timer);
      pctx->trickle_grace_period_timer=0;
    }

    free(pctx);

    *pctxp=0;
  }

/* Start the checks for the first media stream (S 5.7)
   The rest remain FROZEN */
int nr_ice_peer_ctx_start_checks(nr_ice_peer_ctx *pctx)
  {
    return nr_ice_peer_ctx_start_checks2(pctx, 0);
  }

/* Start checks for some media stream.

   If allow_non_first == 0, then we only look at the first stream,
   which is 5245-complaint.

   If allow_non_first == 1 then we find the first non-empty stream
   This is not compliant with RFC 5245 but is necessary to make trickle ICE
   work plausibly
*/
int nr_ice_peer_ctx_start_checks2(nr_ice_peer_ctx *pctx, int allow_non_first)
  {
    int r,_status;
    nr_ice_media_stream *stream;
    int started = 0;

    /* Ensure that grace period timer is running. We might cancel this if we
     * didn't actually start any pairs. */
    nr_ice_peer_ctx_start_trickle_timer(pctx);

    /* Might have added some streams */
    pctx->reported_connected = 0;
    NR_async_timer_cancel(pctx->connected_cb_timer);
    pctx->connected_cb_timer = 0;
    pctx->checks_started = 0;

    nr_ice_peer_ctx_check_if_connected(pctx);

    if (pctx->reported_connected) {
      r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s) in %s all streams were done",pctx->ctx->label,pctx->label,__FUNCTION__);
      return (0);
    }

    stream=STAILQ_FIRST(&pctx->peer_streams);
    if(!stream)
      ABORT(R_FAILED);

    while (stream) {
      if(!stream->local_stream->obsolete) {
        assert(stream->ice_state != NR_ICE_MEDIA_STREAM_UNPAIRED);

        if (stream->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_FROZEN) {
          if(!TAILQ_EMPTY(&stream->check_list))
            break;

          if(!allow_non_first){
            /* This test applies if:

               1. allow_non_first is 0 (i.e., non-trickle ICE)
               2. the first stream has an empty check list.

               But in the non-trickle ICE case, the other side should have provided
               some candidates or ICE is pretty much not going to work and we're
               just going to fail. Hence R_FAILED as opposed to R_NOT_FOUND and
               immediate termination here.
            */
            r_log(LOG_ICE,LOG_ERR,"ICE(%s): peer (%s) first stream has empty check list",pctx->ctx->label,pctx->label);
            ABORT(R_FAILED);
          }
        }
      }

      stream=STAILQ_NEXT(stream, entry);
    }

    if (!stream) {
      /*
         We fail above if we aren't doing trickle, and this is not all that
         unusual in the trickle case.
       */
      r_log(LOG_ICE,LOG_NOTICE,"ICE(%s): peer (%s) no streams with non-empty check lists",pctx->ctx->label,pctx->label);
    }
    else if (stream->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_FROZEN) {
      if(r=nr_ice_media_stream_unfreeze_pairs(pctx,stream))
        ABORT(r);
      if(r=nr_ice_media_stream_start_checks(pctx,stream))
        ABORT(r);
      ++started;
    }

    stream=STAILQ_FIRST(&pctx->peer_streams);
    while (stream) {
      int serviced = 0;
      if (r=nr_ice_media_stream_service_pre_answer_requests(pctx, stream->local_stream, stream, &serviced))
        ABORT(r);

      if (serviced) {
        ++started;
      }
      else {
        r_log(LOG_ICE,LOG_NOTICE,"ICE(%s): peer (%s) no streams with pre-answer requests",pctx->ctx->label,pctx->label);
      }


      stream=STAILQ_NEXT(stream, entry);
    }

    if (!started && pctx->ctx->uninitialized_candidates) {
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): peer (%s) no checks to start, but gathering is not done yet, cancelling grace period timer",pctx->ctx->label,pctx->label);
      /* Never mind on the grace period timer */
      NR_async_timer_cancel(pctx->trickle_grace_period_timer);
      pctx->trickle_grace_period_timer=0;
      ABORT(R_NOT_FOUND);
    }

    _status=0;
  abort:
    return(_status);
  }

void nr_ice_peer_ctx_stream_started_checks(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream)
  {
    if (!pctx->checks_started) {
      r_log(LOG_ICE,LOG_NOTICE,"ICE(%s): peer (%s) is now checking",pctx->ctx->label,pctx->label);
      pctx->checks_started = 1;
      if (pctx->handler && pctx->handler->vtbl->ice_checking) {
        pctx->handler->vtbl->ice_checking(pctx->handler->obj, pctx);
      }
    }
  }

void nr_ice_peer_ctx_refresh_consent_all_streams(nr_ice_peer_ctx *pctx)
  {
    nr_ice_media_stream *str;

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): refreshing consent on all streams",pctx->label);

    str=STAILQ_FIRST(&pctx->peer_streams);
    while(str) {
      nr_ice_media_stream_refresh_consent_all(str);
      str=STAILQ_NEXT(str,entry);
    }
  }

void nr_ice_peer_ctx_disconnected(nr_ice_peer_ctx *pctx)
  {
    if (pctx->connected_cb_timer) {
      /* Whoops, never mind */
      NR_async_timer_cancel(pctx->connected_cb_timer);
      pctx->connected_cb_timer = 0;
    }

    if (pctx->reported_connected &&
        pctx->handler &&
        pctx->handler->vtbl->ice_disconnected) {
      pctx->handler->vtbl->ice_disconnected(pctx->handler->obj, pctx);

      pctx->reported_connected = 0;
    }
  }

void nr_ice_peer_ctx_disconnect_all_streams(nr_ice_peer_ctx *pctx)
  {
    nr_ice_media_stream *str;

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): disconnecting all streams",pctx->label);

    str=STAILQ_FIRST(&pctx->peer_streams);
    while(str) {
      nr_ice_media_stream_disconnect_all_components(str);

      /* The first stream to be disconnected will cause the peer ctx to signal
         the disconnect up. */
      nr_ice_media_stream_set_disconnected(str, NR_ICE_MEDIA_STREAM_DISCONNECTED);

      str=STAILQ_NEXT(str,entry);
    }
  }

void nr_ice_peer_ctx_connected(nr_ice_peer_ctx *pctx)
  {
    /* Fire the handler callback to say we're done */
    if (pctx->reported_connected &&
        pctx->handler &&
        pctx->handler->vtbl->ice_connected) {
      pctx->handler->vtbl->ice_connected(pctx->handler->obj, pctx);
    }
  }

static void nr_ice_peer_ctx_fire_connected(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_peer_ctx *pctx=(nr_ice_peer_ctx*)cb_arg;

    pctx->connected_cb_timer=0;

    nr_ice_peer_ctx_connected(pctx);
  }

/* Examine all the streams to see if we're
   maybe miraculously connected */
void nr_ice_peer_ctx_check_if_connected(nr_ice_peer_ctx *pctx)
  {
    nr_ice_media_stream *str;
    int failed=0;
    int succeeded=0;

    str=STAILQ_FIRST(&pctx->peer_streams);
    while(str){
      if (!str->local_stream->obsolete){
        if(str->ice_state==NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED){
          succeeded++;
        }
        else if(str->ice_state==NR_ICE_MEDIA_STREAM_CHECKS_FAILED){
          failed++;
        }
        else{
          break;
        }
      }
      str=STAILQ_NEXT(str,entry);
    }

    if(str)
      return;  /* Something isn't done */

    /* OK, we're finished, one way or another */
    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): all checks completed success=%d fail=%d",pctx->label,succeeded,failed);

    /* Make sure grace period timer is cancelled */
    if(pctx->trickle_grace_period_timer) {
      r_log(LOG_ICE,LOG_INFO,"ICE(%s): peer (%s) cancelling grace period timer",pctx->ctx->label,pctx->label);
      NR_async_timer_cancel(pctx->trickle_grace_period_timer);
      pctx->trickle_grace_period_timer=0;
    }

    /* Schedule a connected notification for the first connected event.
       IMPORTANT: This is done in a callback because we expect destructors
       of various kinds to be fired from here */
    if (!pctx->reported_connected) {
      pctx->reported_connected = 1;
      assert(!pctx->connected_cb_timer);
      NR_ASYNC_TIMER_SET(0,nr_ice_peer_ctx_fire_connected,pctx,&pctx->connected_cb_timer);
    }
  }


/* Given a component in the main ICE ctx, find the relevant component in
   the peer_ctx */
int nr_ice_peer_ctx_find_component(nr_ice_peer_ctx *pctx, nr_ice_media_stream *str, int component_id, nr_ice_component **compp)
  {
    nr_ice_media_stream *pstr;
    int r,_status;

    pstr=STAILQ_FIRST(&pctx->peer_streams);
    while(pstr){
      if(pstr->local_stream==str)
        break;

      pstr=STAILQ_NEXT(pstr,entry);
    }
    if(!pstr)
      ABORT(R_BAD_ARGS);

    if(r=nr_ice_media_stream_find_component(pstr,component_id,compp))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }

/*
   This packet may be for us.

   1. Find the matching peer component
   2. Examine the packet source address to see if it matches
   one of the peer candidates.
   3. Fire the relevant callback handler if there is a match

   Return 0 if match, R_REJECTED if no match, other errors
   if we can't even find the component or something like that.
*/

int nr_ice_peer_ctx_deliver_packet_maybe(nr_ice_peer_ctx *pctx, nr_ice_component *comp, nr_transport_addr *source_addr, UCHAR *data, int len)
  {
    nr_ice_component *peer_comp;
    nr_ice_candidate *cand;
    int r,_status;

    if(r=nr_ice_peer_ctx_find_component(pctx, comp->stream, comp->component_id,
      &peer_comp))
      ABORT(r);

    /* OK, we've found the component, now look for matches */
    cand=TAILQ_FIRST(&peer_comp->candidates);
    while(cand){
      if(!nr_transport_addr_cmp(source_addr,&cand->addr,
        NR_TRANSPORT_ADDR_CMP_MODE_ALL))
        break;

      cand=TAILQ_NEXT(cand,entry_comp);
    }

    if(!cand)
      ABORT(R_REJECTED);

    // accumulate the received bytes and packets for the active candidate pair
    if (peer_comp->active) {
      peer_comp->active->bytes_recvd += len;
      peer_comp->active->packets_recvd += 1;
      gettimeofday(&peer_comp->active->last_recvd, 0);
    }

    /* OK, there's a match. Call the handler */

    if (pctx->handler) {
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): Delivering data", pctx->label);

      pctx->handler->vtbl->msg_recvd(pctx->handler->obj,
        pctx,comp->stream,comp->component_id,data,len);
    }

    _status=0;
  abort:
    return(_status);
  }

void nr_ice_peer_ctx_switch_controlling_role(nr_ice_peer_ctx *pctx)
  {
    int controlling = !(pctx->controlling);
    if(pctx->controlling_conflict_resolved) {
      r_log(LOG_ICE,LOG_WARNING,"ICE(%s): peer (%s) %s called more than once; "
            "this probably means the peer is confused. Not switching roles.",
            pctx->ctx->label,pctx->label,__FUNCTION__);
      return;
    }

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): detected "
          "role conflict. Switching to %s",
          pctx->label,
          controlling ? "controlling" : "controlled");

    pctx->controlling = controlling;
    pctx->controlling_conflict_resolved = 1;

    if(pctx->state == NR_ICE_PEER_STATE_PAIRED) {
      /*  We have formed candidate pairs. We need to inform them. */
      nr_ice_media_stream *pstream=STAILQ_FIRST(&pctx->peer_streams);
      while(pstream) {
        nr_ice_media_stream_role_change(pstream);
        pstream = STAILQ_NEXT(pstream, entry);
      }
    }
  }

