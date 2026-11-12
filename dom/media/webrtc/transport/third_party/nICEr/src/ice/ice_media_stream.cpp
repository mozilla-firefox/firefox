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
#include <r_assoc.h>
#include <async_timer.h>
#include "ice_util.h"
#include "ice_ctx.h"

static const char *nr_ice_media_stream_states[]={"INVALID",
  "UNPAIRED","FROZEN","ACTIVE","CONNECTED","FAILED"
};

int nr_ice_media_stream_set_state(nr_ice_media_stream *str, int state);

int nr_ice_media_stream_create(nr_ice_ctx *ctx,const char *label,const char *ufrag,const char *pwd,int components, nr_ice_media_stream **streamp)
  {
    int r,_status;
    nr_ice_media_stream *stream=0;
    nr_ice_component *comp=0;
    int i;

    if(!(stream=R_NEW(nr_ice_media_stream)))
      ABORT(R_NO_MEMORY);

    if(!(stream->label=strdup(label)))
      ABORT(R_NO_MEMORY);

    if(!(stream->ufrag=strdup(ufrag)))
      ABORT(R_NO_MEMORY);

    if(!(stream->pwd=strdup(pwd)))
      ABORT(R_NO_MEMORY);

    stream->ctx=ctx;

    STAILQ_INIT(&stream->components);
    for(i=0;i<components;i++){
      /* component-id must be > 0, so increment by 1 */
      if(r=nr_ice_component_create(stream, i+1, &comp))
        ABORT(r);

    }

    TAILQ_INIT(&stream->check_list);
    TAILQ_INIT(&stream->trigger_check_queue);

    stream->disconnected = 0;
    stream->component_ct=components;
    stream->ice_state = NR_ICE_MEDIA_STREAM_UNPAIRED;
    stream->obsolete = 0;
    stream->actually_started_checking = 0;
    stream->defer_gathering_events = false;
    stream->r2l_user = 0;
    stream->l2r_user = 0;
    stream->flags = ctx->flags;
    if(ctx->stun_server_ct_cfg) {
      if(!(stream->stun_servers=R_NEW_CNT(nr_ice_stun_server, ctx->stun_server_ct_cfg)))
        ABORT(R_NO_MEMORY);

      memcpy(stream->stun_servers,ctx->stun_servers_cfg,sizeof(nr_ice_stun_server)*(ctx->stun_server_ct_cfg));
      stream->stun_server_ct = ctx->stun_server_ct_cfg;
    }

    if(ctx->turn_server_ct_cfg) {
      if(!(stream->turn_servers=R_NEW_CNT(nr_ice_turn_server, ctx->turn_server_ct_cfg)))
        ABORT(R_NO_MEMORY);

      for(int i = 0; i < ctx->turn_server_ct_cfg; ++i) {
        nr_ice_turn_server *dst = &stream->turn_servers[i];
        nr_ice_turn_server *src = &ctx->turn_servers_cfg[i];
        memcpy(&dst->turn_server, &src->turn_server, sizeof(nr_ice_stun_server));
        dst->username = strdup(src->username);
        r_data_create(&dst->password, src->password->data, src->password->len);
      }
      stream->turn_server_ct = ctx->turn_server_ct_cfg;
    }

    r_log(LOG_ICE,LOG_DEBUG,"ICE-STREAM(%s): flags %d",stream->label,stream->flags);
    *streamp=stream;

    _status=0;
  abort:
    if(_status){
      nr_ice_media_stream_destroy(&stream);
    }
    return(_status);
  }

int nr_ice_media_stream_destroy(nr_ice_media_stream **streamp)
  {
    nr_ice_media_stream *stream;
    nr_ice_component *c1,*c2;
    nr_ice_cand_pair *p1,*p2;
    if(!streamp || !*streamp)
      return(0);

    stream=*streamp;
    *streamp=0;

    STAILQ_FOREACH_SAFE(c1, &stream->components, entry, c2){
      STAILQ_REMOVE(&stream->components,c1,nr_ice_component_,entry);
      nr_ice_component_destroy(&c1);
    }

    /* Note: all the entries from the trigger check queue are held in here as
     *       well, so we only clean up the super set. */
    TAILQ_FOREACH_SAFE(p1, &stream->check_list, check_queue_entry, p2){
      TAILQ_REMOVE(&stream->check_list,p1,check_queue_entry);
      nr_ice_candidate_pair_destroy(&p1);
    }

    free(stream->label);

    free(stream->ufrag);
    free(stream->pwd);
    free(stream->r2l_user);
    free(stream->l2r_user);
    r_data_zfree(&stream->r2l_pass);
    r_data_zfree(&stream->l2r_pass);

    free(stream->stun_servers);
    for (int i = 0; i < stream->turn_server_ct; i++) {
        free(stream->turn_servers[i].username);
        r_data_destroy(&stream->turn_servers[i].password);
    }
    free(stream->turn_servers);

    if(stream->timer)
      NR_async_timer_cancel(stream->timer);

    free(stream);

    return(0);
  }

int nr_ice_media_stream_initialize(nr_ice_ctx *ctx, nr_ice_media_stream *stream)
  {
    int r,_status;
    nr_ice_component *comp;
    bool took_from_pool = false;
    bool took = false;

    assert(!stream->obsolete);

    // Skipping the pool_stream, a stream's component that has not
    // gathered anything yet can use what the pool gathered instead of
    // making its own sockets.
    if(stream != ctx->pool_stream){
      comp = STAILQ_FIRST(&stream->components);
      while(comp){
        if(!comp->candidate_ct){
          if(r = nr_ice_component_take_from_pool(comp, &took))
            ABORT(r);

          took_from_pool |= took;
        }

        comp = STAILQ_NEXT(comp, entry);
      }
    }

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if(r=nr_ice_component_initialize(ctx,comp))
        ABORT(r);
      comp=STAILQ_NEXT(comp,entry);
    }

    if (took_from_pool) {
      // Whatever came from the pool gathered before this stream
      // existed, so no events have been raised for it. Do that now,
      // which also covers the gathering event for any component that is
      // still gathering.
      nr_ice_media_stream_flush_gathering_events(stream);
    } else if (!nr_ice_media_stream_is_done_gathering(stream) &&
               ctx->gather_handler &&
               ctx->gather_handler->vtbl->stream_gathering &&
               !stream->defer_gathering_events) {
      ctx->gather_handler->vtbl->stream_gathering(ctx->gather_handler->obj,
                                                  stream);
    }

    _status=0;
  abort:
    if (_status) {
      if (ctx->gather_handler && ctx->gather_handler->vtbl->stream_gathered &&
          !stream->defer_gathering_events) {
        ctx->gather_handler->vtbl->stream_gathered(ctx->gather_handler->obj,
                                                   stream);
      }
    }
    return(_status);
  }


void nr_ice_media_stream_defer_gathering_events(nr_ice_media_stream *stream)
  {
    stream->defer_gathering_events = true;
  }

// Raise the gathering events for candidates that this stream has but has not
// reported, because it took them from the pool, which gathered them before this
// stream existed. They are raised in the
// order a stream that gathered them itself would have raised them: gathering,
// then a trickle callback (or an error) per candidate, then gathered.
//
// Only the components that took from the pool are replayed. A component that
// gathered for itself has already reported its candidates: one that failed
// synchronously in nr_ice_component_initialize raised its error right away,
// and a component that gathered on an earlier nr_ice_gather has trickled its
// candidates already.
//
// A candidate that is still gathering is skipped; it reports itself when it
// finishes, as usual.
int nr_ice_media_stream_flush_gathering_events(nr_ice_media_stream *stream)
  {
    nr_ice_ctx *ctx = stream->ctx;
    nr_ice_component *comp;
    nr_ice_candidate *cand;
    nr_ice_gather_handler *gh = ctx->gather_handler;

    // Only the pool stream defers its events, and it never flushes them.
    assert(!stream->defer_gathering_events);

    r_log(LOG_ICE, LOG_DEBUG, "ICE(%s)/STREAM(%s): raising held-back gathering events",
          ctx->label, stream->label);

    if (gh && gh->vtbl->stream_gathering) {
      gh->vtbl->stream_gathering(gh->obj, stream);
    }

    STAILQ_FOREACH(comp, &stream->components, entry) {
      if (!comp->took_from_pool) {
        continue;
      }
      comp->took_from_pool = false;

      TAILQ_FOREACH(cand, &comp->candidates, entry_comp) {
        if (cand->state == NR_ICE_CAND_STATE_FAILED) {
          // A server reflexive candidate that piggybacks on a relay candidate
          // does not report separately; the relay candidate reports for it.
          if (cand->type == SERVER_REFLEXIVE &&
              cand->u.srvrflx.relay_candidate) {
            continue;
          }

          if (gh && gh->vtbl->candidate_error) {
            gh->vtbl->candidate_error(gh->obj, stream, cand);
          }

          continue;
        }

        if (cand->state != NR_ICE_CAND_STATE_INITIALIZED) {
          continue;
        }

        if (ctx->trickle_cb && !nr_ice_ctx_hide_candidate(ctx, cand)) {
          ctx->trickle_cb(ctx->trickle_cb_arg, ctx, stream, cand->component_id,
                          cand);
        }
      }
    }

    if (nr_ice_media_stream_is_done_gathering(stream)) {
      if (gh && gh->vtbl->stream_gathered) {
        gh->vtbl->stream_gathered(gh->obj, stream);
      }

      if (ctx->trickle_cb) {
        ctx->trickle_cb(ctx->trickle_cb_arg, ctx, stream, 1, nullptr);
      }
    }

    return 0;
  }

int nr_ice_media_stream_get_attributes(nr_ice_media_stream *stream, char ***attrsp, int *attrctp)
  {
    int attrct=2;
    nr_ice_component *comp;
    char **attrs=0;
    int index=0;
    nr_ice_candidate *cand;
    int r,_status;
    char *tmp=0;

    *attrctp=0;

    /* First find out how many attributes we need */
    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if (comp->state != NR_ICE_COMPONENT_DISABLED) {
        cand = TAILQ_FIRST(&comp->candidates);
        while(cand){
          if (!nr_ice_ctx_hide_candidate(stream->ctx, cand)) {
            ++attrct;
          }

          cand = TAILQ_NEXT(cand, entry_comp);
        }
      }
      comp=STAILQ_NEXT(comp,entry);
    }

    /* Make the array we'll need */
    if(!(attrs=R_NEW_CNT(char *, attrct)))
      ABORT(R_NO_MEMORY);
    for(index=0;index<attrct;index++){
      if(!(attrs[index]=(char*)malloc(NR_ICE_MAX_ATTRIBUTE_SIZE)))
        ABORT(R_NO_MEMORY);
    }

    index=0;
    /* Now format the attributes */
    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if (comp->state != NR_ICE_COMPONENT_DISABLED) {
        nr_ice_candidate *cand;

        cand=TAILQ_FIRST(&comp->candidates);
        while(cand){
          if (!nr_ice_ctx_hide_candidate(stream->ctx, cand)) {
            assert(index < attrct);

            if (index >= attrct)
              ABORT(R_INTERNAL);

            if(r=nr_ice_format_candidate_attribute(cand, attrs[index],NR_ICE_MAX_ATTRIBUTE_SIZE, 0))
              ABORT(r);

            index++;
          }

          cand=TAILQ_NEXT(cand,entry_comp);
        }
      }
      comp=STAILQ_NEXT(comp,entry);
    }

    /* Now, ufrag and pwd */
    if(!(tmp=(char*)malloc(100)))
      ABORT(R_NO_MEMORY);
    snprintf(tmp,100,"ice-ufrag:%s",stream->ufrag);
    attrs[index++]=tmp;

    if(!(tmp=(char*)malloc(100)))
      ABORT(R_NO_MEMORY);
    snprintf(tmp,100,"ice-pwd:%s",stream->pwd);
    attrs[index++]=tmp;

    *attrsp=attrs;
    *attrctp=attrct;

    _status=0;
  abort:
    if(_status){
      if(attrs){
        for(index=0;index<attrct;index++){
          free(attrs[index]);
        }
        free(attrs);
      }
    }
    return(_status);
  }

/* Get a default candidate per 4.1.4 */
int nr_ice_media_stream_get_default_candidate(nr_ice_media_stream *stream, int component, nr_ice_candidate **candp)
  {
    int r,_status;
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if (comp->component_id == component)
        break;

      comp=STAILQ_NEXT(comp,entry);
    }

    if (!comp)
      ABORT(R_NOT_FOUND);

    /* If there aren't any IPV4 candidates, try IPV6 */
    if((r=nr_ice_component_get_default_candidate(comp, candp, NR_IPV4)) &&
       (r=nr_ice_component_get_default_candidate(comp, candp, NR_IPV6))) {
      ABORT(r);
    }

    _status=0;
  abort:
    return(_status);
  }


int nr_ice_media_stream_pair_candidates(nr_ice_peer_ctx *pctx,nr_ice_media_stream *lstream,nr_ice_media_stream *pstream)
  {
    int r,_status;
    nr_ice_component *pcomp,*lcomp;

    pcomp=STAILQ_FIRST(&pstream->components);
    lcomp=STAILQ_FIRST(&lstream->components);
    while(pcomp){
      if ((lcomp->state != NR_ICE_COMPONENT_DISABLED) &&
          (pcomp->state != NR_ICE_COMPONENT_DISABLED)) {
        if(r=nr_ice_component_pair_candidates(pctx,lcomp,pcomp))
          ABORT(r);
      }

      lcomp=STAILQ_NEXT(lcomp,entry);
      pcomp=STAILQ_NEXT(pcomp,entry);
    };

    if (pstream->ice_state == NR_ICE_MEDIA_STREAM_UNPAIRED) {
      nr_ice_media_stream_set_state(pstream, NR_ICE_MEDIA_STREAM_CHECKS_FROZEN);
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_media_stream_service_pre_answer_requests(nr_ice_peer_ctx *pctx, nr_ice_media_stream *lstream, nr_ice_media_stream *pstream, int *serviced)
  {
    nr_ice_component *pcomp;
    int r,_status;
    char *user = 0;

    if (serviced)
      *serviced = 0;

    pcomp=STAILQ_FIRST(&pstream->components);
    while(pcomp){
      int serviced_inner=0;

      /* Flush all the pre-answer requests */
      if(r=nr_ice_component_service_pre_answer_requests(pctx, pcomp, pstream->r2l_user, &serviced_inner))
        ABORT(r);
      if (serviced)
        *serviced += serviced_inner;

      pcomp=STAILQ_NEXT(pcomp,entry);
    }

    _status=0;
   abort:
    free(user);
    return(_status);
  }

/* S 5.8 -- run the first pair from the triggered check queue (even after
 * checks have completed S 8.1.2) or run the highest priority WAITING pair or
 * if not available FROZEN pair from the check queue */
static void nr_ice_media_stream_check_timer_cb(NR_SOCKET s, int h, void *cb_arg)
  {
    int r,_status;
    nr_ice_media_stream *stream=(nr_ice_media_stream*)cb_arg;
    nr_ice_cand_pair *pair = 0;
    int timer_multiplier=stream->pctx->active_streams ? stream->pctx->active_streams : 1;
    int timer_val=stream->pctx->ctx->Ta*timer_multiplier;

    assert(timer_val>0);

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): check timer expired for media stream %s",stream->pctx->label,stream->label);
    stream->timer=0;

    /* The trigger check queue has the highest priority */
    pair=TAILQ_FIRST(&stream->trigger_check_queue);
    while(pair){
      if(pair->state==NR_ICE_PAIR_STATE_WAITING){
        /* Remove the pair from he trigger check queue */
        r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): Removing pair from trigger check queue %s",stream->pctx->label,pair->as_string);
        TAILQ_REMOVE(&stream->trigger_check_queue,pair,triggered_check_queue_entry);
        break;
      }
      pair=TAILQ_NEXT(pair,triggered_check_queue_entry);
    }

    if (stream->ice_state != NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED) {
      if(!pair){
        /* Find the highest priority WAITING check and move it to RUNNING */
        pair=TAILQ_FIRST(&stream->check_list);
        while(pair){
          if(pair->state==NR_ICE_PAIR_STATE_WAITING)
            break;
          pair=TAILQ_NEXT(pair,check_queue_entry);
        }
      }

      /* Hmmm... No WAITING. Let's look for FROZEN */
      if(!pair){
        pair=TAILQ_FIRST(&stream->check_list);

        while(pair){
          if(pair->state==NR_ICE_PAIR_STATE_FROZEN){
            if(r=nr_ice_candidate_pair_unfreeze(stream->pctx,pair))
              ABORT(r);
            break;
          }
          pair=TAILQ_NEXT(pair,check_queue_entry);
        }
      }
    }

    if(pair){
      nr_ice_candidate_pair_start(pair->pctx,pair); /* Ignore failures */

      /* stream->ice_state goes to checking when we decide that it is ok to
       * start checking, which can happen before we get remote candidates. We
       * want to fire this event when we _actually_ start sending checks. */
      if (!stream->actually_started_checking) {
        stream->actually_started_checking = 1;
        if (stream->pctx->handler &&
            stream->pctx->handler->vtbl->stream_checking) {
          stream->pctx->handler->vtbl->stream_checking(
              stream->pctx->handler->obj, stream->local_stream);
        }
      }

      NR_ASYNC_TIMER_SET(timer_val,nr_ice_media_stream_check_timer_cb,cb_arg,&stream->timer);
    }
    else {
      r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s): no FROZEN/WAITING pairs for %s",stream->pctx->label,stream->label);
    }

    _status=0;
  abort:
    if (_status) {
      // cb doesn't return anything, but we should probably log that we aborted
      // This also quiets the unused variable warnings.
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): check timer cb for media stream %s abort with status: %d",
        stream->pctx->label,stream->label, _status);
    }
    return;
  }

/* Start checks for this media stream (aka check list) */
int nr_ice_media_stream_start_checks(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream)
  {
    int r,_status;

    /* Don't start the check timer if the stream is failed */
    if (stream->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_FAILED) {
      assert(0);
      ABORT(R_INTERNAL);
    }

    if (stream->local_stream->obsolete) {
      assert(0);
      ABORT(R_INTERNAL);
    }

    /* Even if the stream is completed already remote can still create a new
     * triggered check request which needs to fire, but not change our stream
     * state. */
    if (stream->ice_state != NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED) {
      if(r=nr_ice_media_stream_set_state(stream,NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE)) {
        ABORT(r);
      }
    }

    if (!stream->timer) {
      r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/ICE-STREAM(%s): Starting check timer for stream.",pctx->label,stream->label);
      nr_ice_media_stream_check_timer_cb(0,0,stream);
    }

    nr_ice_peer_ctx_stream_started_checks(pctx, stream);

    _status=0;
  abort:
    return(_status);
  }

/* Start checks for this media stream (aka check list) S 5.7 */
int nr_ice_media_stream_unfreeze_pairs(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream)
  {
    int r,_status;
    r_assoc *assoc=0;
    nr_ice_cand_pair *pair=0;

    /* Already seen assoc */
    if(r=r_assoc_create(&assoc,r_assoc_crc32_hash_compute,5))
      ABORT(r);

    /* S 5.7.4. Set the highest priority pairs in each foundation to WAITING */
    pair=TAILQ_FIRST(&stream->check_list);
    while(pair){
      void *v;

      if(r=r_assoc_fetch(assoc,pair->foundation,strlen(pair->foundation),&v)){
        if(r!=R_NOT_FOUND)
          ABORT(r);
        if(r=nr_ice_candidate_pair_unfreeze(pctx,pair))
          ABORT(r);

        if(r=r_assoc_insert(assoc,pair->foundation,strlen(pair->foundation),
          0,0,0,R_ASSOC_NEW))
          ABORT(r);
      }

      /* Already exists... fall through */
      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    _status=0;
  abort:
    r_assoc_destroy(&assoc);
    return(_status);
  }

static int nr_ice_media_stream_unfreeze_pairs_match(nr_ice_media_stream *stream, char *foundation)
  {
    nr_ice_cand_pair *pair;
    int r,_status;
    int unfroze=0;

    pair=TAILQ_FIRST(&stream->check_list);
    while(pair){
      if(pair->state==NR_ICE_PAIR_STATE_FROZEN &&
        !strcmp(foundation,pair->foundation)){
        if(r=nr_ice_candidate_pair_unfreeze(stream->pctx,pair))
          ABORT(r);
        unfroze++;
      }
      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    if(!unfroze)
      return(R_NOT_FOUND);

    _status=0;
  abort:
    return(_status);
  }

/* S 7.1.2.2 */
int nr_ice_media_stream_unfreeze_pairs_foundation(nr_ice_media_stream *stream, char *foundation)
  {
    int r,_status;
    nr_ice_media_stream *str;

    /* 1. Unfreeze all frozen pairs with the same foundation
       in this stream */
    if(r=nr_ice_media_stream_unfreeze_pairs_match(stream,foundation)){
      if(r!=R_NOT_FOUND)
        ABORT(r);
    }

    /* Now go through the check lists for the other streams */
    str=STAILQ_FIRST(&stream->pctx->peer_streams);
    while(str){
      if(str!=stream && !str->local_stream->obsolete){
        switch(str->ice_state){
          case NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE:
            /* Unfreeze matching pairs */
            if(r=nr_ice_media_stream_unfreeze_pairs_match(str,foundation)){
              if(r!=R_NOT_FOUND)
                ABORT(r);
            }
            break;
          case NR_ICE_MEDIA_STREAM_CHECKS_FROZEN:
            /* Unfreeze matching pairs if any */
            r=nr_ice_media_stream_unfreeze_pairs_match(str,foundation);
            if(r){
              if(r!=R_NOT_FOUND)
                ABORT(r);

              /* OK, no matching pairs: execute the algorithm from 5.7
                 for this stream */
              if(r=nr_ice_media_stream_unfreeze_pairs(str->pctx,str))
                ABORT(r);
            }
            if(r=nr_ice_media_stream_start_checks(str->pctx,str))
              ABORT(r);

            break;
          default:
            break;
        }
      }

      str=STAILQ_NEXT(str,entry);
    }

    _status=0;
  abort:
    return(_status);
  }


void nr_ice_media_stream_dump_state(nr_ice_peer_ctx *pctx, nr_ice_media_stream *stream, int log_level)
  {
    nr_ice_cand_pair *pair;
    nr_ice_component *comp;

    if(stream->local_stream){
      /* stream has a corresponding local_stream */
      nr_ice_media_stream_dump_state(stream->local_stream->pctx,stream->local_stream, log_level);
      r_log(LOG_ICE,log_level,"ICE-PEER(%s)/STREAM(%s): state dump", stream->pctx->label, stream->label);
    } else {
      r_log(LOG_ICE,log_level,"ICE(%s)/STREAM(%s): state dump", stream->ctx->label, stream->label);
    }

    pair=TAILQ_FIRST(&stream->check_list);
    while(pair){
      nr_ice_candidate_pair_dump_state(pair, log_level);
      pair=TAILQ_NEXT(pair,check_queue_entry);
    }

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      nr_ice_component_dump_state(comp, log_level);
      comp=STAILQ_NEXT(comp,entry);
    }
  }

int nr_ice_media_stream_set_state(nr_ice_media_stream *str, int state)
  {
    /* Make no-change a no-op */
    if (state == str->ice_state)
      return 0;

    assert((size_t)state < sizeof(nr_ice_media_stream_states)/sizeof(char *));
    assert((size_t)str->ice_state < sizeof(nr_ice_media_stream_states)/sizeof(char *));

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): stream %s state %s->%s",
      str->pctx->label,str->label,
      nr_ice_media_stream_states[str->ice_state],
      nr_ice_media_stream_states[state]);

    if(state == NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE)
      str->pctx->active_streams++;
    if(str->ice_state == NR_ICE_MEDIA_STREAM_CHECKS_ACTIVE)
      str->pctx->active_streams--;

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): %d active streams",
      str->pctx->label, str->pctx->active_streams);

    str->ice_state=state;
    if (state == NR_ICE_MEDIA_STREAM_CHECKS_FAILED) {
      nr_ice_media_stream_dump_state(str->pctx,str,LOG_ERR);
    }

    return(0);
  }

void nr_ice_media_stream_stop_checking(nr_ice_media_stream *str)
  {
    nr_ice_cand_pair *p;
    nr_ice_component *comp;

    /* Cancel candidate pairs */
    p=TAILQ_FIRST(&str->check_list);
    while(p){
      nr_ice_candidate_pair_cancel(p->pctx,p,0);
      p=TAILQ_NEXT(p,check_queue_entry);
    }

    if(str->timer) {
      NR_async_timer_cancel(str->timer);
      str->timer = 0;
    }

    /* Cancel consent timers in case it is running already */
    comp=STAILQ_FIRST(&str->components);
    while(comp){
      nr_ice_component_consent_destroy(comp);
      comp=STAILQ_NEXT(comp,entry);
    }
  }

void nr_ice_media_stream_set_obsolete(nr_ice_media_stream *str)
  {
    nr_ice_component *c1,*c2;
    str->obsolete = 1;

    STAILQ_FOREACH_SAFE(c1, &str->components, entry, c2){
      nr_ice_component_stop_gathering(c1);
    }

    nr_ice_media_stream_stop_checking(str);
  }

int nr_ice_media_stream_is_done_gathering(nr_ice_media_stream *str)
  {
    nr_ice_component *comp;
    comp=STAILQ_FIRST(&str->components);
    while(comp){
      if(!nr_ice_component_is_done_gathering(comp)) {
        return 0;
      }
      comp=STAILQ_NEXT(comp,entry);
    }
    return 1;
  }

void nr_ice_media_stream_refresh_consent_all(nr_ice_media_stream *stream)
  {
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if(comp->disconnected) {
        nr_ice_component_refresh_consent_now(comp);
      }

      comp=STAILQ_NEXT(comp,entry);
    }
  }

void nr_ice_media_stream_disconnect_all_components(nr_ice_media_stream *stream)
  {
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      comp->disconnected = 1;

      comp=STAILQ_NEXT(comp,entry);
    }
  }

void nr_ice_media_stream_set_disconnected(nr_ice_media_stream *stream, int disconnected)
  {
    if (stream->disconnected == disconnected) {
      return;
    }

    if (stream->ice_state != NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED) {
      return;
    }
    stream->disconnected = disconnected;

    if (disconnected == NR_ICE_MEDIA_STREAM_DISCONNECTED) {
      if (!stream->local_stream->obsolete) {
        if (stream->pctx->handler &&
            stream->pctx->handler->vtbl->stream_disconnected) {
          stream->pctx->handler->vtbl->stream_disconnected(
              stream->pctx->handler->obj, stream->local_stream);
        }
        nr_ice_peer_ctx_disconnected(stream->pctx);
      }
    } else {
      if (!stream->local_stream->obsolete) {
        if (stream->pctx->handler &&
            stream->pctx->handler->vtbl->stream_ready) {
          stream->pctx->handler->vtbl->stream_ready(stream->pctx->handler->obj,
                                                    stream->local_stream);
        }
      }
      nr_ice_peer_ctx_react_to_stream_change(stream->pctx);
    }
  }

int nr_ice_media_stream_check_if_connected(nr_ice_media_stream *stream)
  {
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if((comp->state != NR_ICE_COMPONENT_DISABLED) &&
         (comp->local_component->state != NR_ICE_COMPONENT_DISABLED) &&
         comp->disconnected)
        break;

      comp=STAILQ_NEXT(comp,entry);
    }

    /* At least one disconnected component */
    if(comp)
      goto done;

    nr_ice_media_stream_set_disconnected(stream, NR_ICE_MEDIA_STREAM_CONNECTED);

  done:
    return(0);
  }

/* S OK, this component has a nominated. If every component has a nominated,
   the stream is ready */
void nr_ice_media_stream_component_nominated(nr_ice_media_stream *stream,nr_ice_component *component)
  {
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&stream->components);
    while(comp){
      if((comp->state != NR_ICE_COMPONENT_DISABLED) &&
         (comp->local_component->state != NR_ICE_COMPONENT_DISABLED) &&
         !comp->nominated)
        break;

      comp=STAILQ_NEXT(comp,entry);
    }

    /* At least one un-nominated component */
    if(comp)
      return;

    /* All done... */
    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/ICE-STREAM(%s): all active components have nominated candidate pairs",stream->pctx->label,stream->label);
    nr_ice_media_stream_set_state(stream,NR_ICE_MEDIA_STREAM_CHECKS_CONNECTED);

    /* Cancel our timer */
    if(stream->timer){
      NR_async_timer_cancel(stream->timer);
      stream->timer=0;
    }

    if (stream->pctx->handler && !stream->local_stream->obsolete) {
      stream->pctx->handler->vtbl->stream_ready(stream->pctx->handler->obj,stream->local_stream);
    }

    /* Now tell the peer_ctx that we're connected */
    nr_ice_peer_ctx_react_to_stream_change(stream->pctx);
  }

void nr_ice_media_stream_component_failed(nr_ice_media_stream *stream,nr_ice_component *component)
  {
    component->state=NR_ICE_COMPONENT_FAILED;

    /* at least one component failed in this media stream, so the entire
     * media stream is marked failed */

    nr_ice_media_stream_set_state(stream,NR_ICE_MEDIA_STREAM_CHECKS_FAILED);

    nr_ice_media_stream_stop_checking(stream);

    if (stream->pctx->handler && !stream->local_stream->obsolete) {
      stream->pctx->handler->vtbl->stream_failed(stream->pctx->handler->obj,stream->local_stream);
    }

    /* Now tell the peer_ctx that we've failed */
    nr_ice_peer_ctx_react_to_stream_change(stream->pctx);
  }


/* OK, we have the stream the user created, but that reflects the base
   ICE ctx, not the peer_ctx. So, find the related stream in the pctx,
   and then find the component */
int nr_ice_media_stream_find_component(nr_ice_media_stream *str, int comp_id, nr_ice_component **compp)
  {
    int _status;
    nr_ice_component *comp;

    comp=STAILQ_FIRST(&str->components);
    while(comp){
      if(comp->component_id==comp_id)
        break;

      comp=STAILQ_NEXT(comp,entry);
    }
    if(!comp)
      ABORT(R_NOT_FOUND);

    *compp=comp;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_media_stream_send(nr_ice_peer_ctx *pctx, nr_ice_media_stream *str, int component, UCHAR *data, int len)
  {
    int r,_status;
    nr_ice_component *comp;

    /* First find the peer component */
    if(r=nr_ice_peer_ctx_find_component(pctx, str, component, &comp))
      ABORT(r);

    /* Do we have an active pair yet? We should... */
    if(!comp->active)
      ABORT(R_NOT_FOUND);

    /* Does fresh ICE consent exist? */
    if(!comp->can_send)
      ABORT(R_FAILED);

    /* OK, write to that pair, which means:
       1. Use the socket on our local side.
       2. Use the address on the remote side
    */
    if(r=nr_socket_sendto(comp->active->local->osock,data,len,0,
                          &comp->active->remote->addr)) {
      if ((r==R_IO_ERROR) || (r==R_EOD)) {
        nr_ice_component_disconnected(comp);
      }
      ABORT(r);
    }

    // accumulate the sent bytes and packets for the active candidate pair
    comp->active->bytes_sent += len;
    comp->active->packets_sent += 1;
    gettimeofday(&comp->active->last_sent, 0);

    _status=0;
  abort:
    return(_status);
  }

/* Returns R_REJECTED if the component is unpaired or has been disabled. */
int nr_ice_media_stream_get_active(nr_ice_peer_ctx *pctx, nr_ice_media_stream *str, int component, nr_ice_candidate **local, nr_ice_candidate **remote)
  {
    int r,_status;
    nr_ice_component *comp;

    /* First find the peer component */
    if(r=nr_ice_peer_ctx_find_component(pctx, str, component, &comp))
      ABORT(r);

    if (comp->state == NR_ICE_COMPONENT_UNPAIRED ||
        comp->state == NR_ICE_COMPONENT_DISABLED)
      ABORT(R_REJECTED);

    if(!comp->active)
      ABORT(R_NOT_FOUND);

    if (local) *local = comp->active->local;
    if (remote) *remote = comp->active->remote;

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_media_stream_pair_new_trickle_candidate(nr_ice_peer_ctx *pctx, nr_ice_media_stream *pstream, nr_ice_candidate *cand)
  {
    int r,_status;
    nr_ice_component *comp;

    if ((r=nr_ice_media_stream_find_component(pstream, cand->component_id, &comp)))
      ABORT(R_NOT_FOUND);

    if (r=nr_ice_component_pair_candidate(pctx, comp, cand, 1))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }

int nr_ice_media_stream_get_consent_status(nr_ice_media_stream *stream, int
component_id, int *can_send, struct timeval *ts)
  {
    int r,_status;
    nr_ice_component *comp;

    if ((r=nr_ice_media_stream_find_component(stream, component_id, &comp)))
      ABORT(r);

    *can_send = comp->can_send;
    ts->tv_sec = comp->consent_last_seen.tv_sec;
    ts->tv_usec = comp->consent_last_seen.tv_usec;
    _status=0;
  abort:
    return(_status);
  }

int nr_ice_media_stream_disable_component(nr_ice_media_stream *stream, int component_id)
  {
    int r,_status;
    nr_ice_component *comp;

    if (stream->ice_state != NR_ICE_MEDIA_STREAM_UNPAIRED)
      ABORT(R_FAILED);

    if ((r=nr_ice_media_stream_find_component(stream, component_id, &comp)))
      ABORT(r);

    /* Can only disable before pairing */
    if (comp->state != NR_ICE_COMPONENT_UNPAIRED &&
        comp->state != NR_ICE_COMPONENT_DISABLED)
      ABORT(R_FAILED);

    comp->state = NR_ICE_COMPONENT_DISABLED;

    _status=0;
  abort:
    return(_status);
  }

void nr_ice_media_stream_role_change(nr_ice_media_stream *stream)
  {
    nr_ice_cand_pair *pair,*temp_pair;
    /* Changing role causes candidate pair priority to change, which requires
     * re-sorting the check list. */
    nr_ice_cand_pair_head old_checklist;

    /* Move check_list to old_checklist (not POD, have to do the hard way) */
    TAILQ_INIT(&old_checklist);
    TAILQ_FOREACH_SAFE(pair,&stream->check_list,check_queue_entry,temp_pair) {
      TAILQ_REMOVE(&stream->check_list,pair,check_queue_entry);
      TAILQ_INSERT_TAIL(&old_checklist,pair,check_queue_entry);
    }

    /* Re-insert into the check list */
    TAILQ_FOREACH_SAFE(pair,&old_checklist,check_queue_entry,temp_pair) {
      TAILQ_REMOVE(&old_checklist,pair,check_queue_entry);
      nr_ice_candidate_pair_role_change(pair);
      nr_ice_candidate_pair_insert(&stream->check_list,pair);
    }
  }

int nr_ice_media_stream_find_pair(nr_ice_media_stream *str, nr_ice_candidate *lcand, nr_ice_candidate *rcand, nr_ice_cand_pair **pair)
  {
    nr_ice_cand_pair_head *head = &str->check_list;
    nr_ice_cand_pair *c1;

    c1=TAILQ_FIRST(head);
    while(c1){
      if(c1->local == lcand &&
         c1->remote == rcand) {
        *pair=c1;
        return(0);
      }

      c1=TAILQ_NEXT(c1,check_queue_entry);
    }

    return(R_NOT_FOUND);
  }

int nr_ice_stun_server_get_url(const nr_ice_stun_server *server, int is_turn, char **urlp)
  {
    int _status;
    char buf[320]; /* scheme + host + port + transport param */
    const char *scheme;
    const char *query;
    char host_buf[256];
    uint16_t port;

    if (nr_transport_addr_get_port(&server->addr, &port))
      ABORT(R_FAILED);

    if (nr_transport_addr_get_addrstring(&server->addr, host_buf, sizeof(host_buf)))
      ABORT(R_FAILED);

    if (is_turn) {
      scheme = server->addr.tls ? "turns" : "turn";
    } else {
      scheme = server->addr.tls ? "stuns" : "stun";
    }

    query = "";
    if (server->addr.tls || server->addr.protocol == IPPROTO_TCP) {
      query = "?transport=tcp";
    }

    if (!server->addr.fqdn[0] && server->addr.ip_version == NR_IPV6) {
      snprintf(buf, sizeof(buf), "%s:[%s]:%u%s", scheme, host_buf, port, query);
    } else {
      snprintf(buf, sizeof(buf), "%s:%s:%u%s", scheme, host_buf, port, query);
    }

    if (!(*urlp = strdup(buf)))
      ABORT(R_NO_MEMORY);

    _status = 0;
  abort:
    return(_status);
  }
