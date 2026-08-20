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
#include <nr_api.h>
#include "async_timer.h"
#include "ice_ctx.h"
#include "ice_util.h"
#include "ice_codeword.h"
#include "stun.h"

static const char *nr_ice_cand_pair_states[]={"UNKNOWN","FROZEN","WAITING","IN_PROGRESS","FAILED","SUCCEEDED","CANCELLED"};

static void nr_ice_candidate_pair_restart_stun_nominated_cb(NR_SOCKET s, int how, void *cb_arg);
static void nr_ice_candidate_pair_restart_stun_role_change_cb(NR_SOCKET s, int how, void *cb_arg);
static void nr_ice_candidate_pair_compute_codeword(nr_ice_cand_pair *pair,
  nr_ice_candidate *lcand, nr_ice_candidate *rcand);

static void nr_ice_candidate_pair_set_priority(nr_ice_cand_pair *pair)
  {
    /* Priority computation S 5.7.2 */
    UINT8 controlling_priority, controlled_priority;
    if(pair->pctx->controlling)
    {
      controlling_priority=pair->local->priority;
      controlled_priority=pair->remote->priority;
    }
    else{
      controlling_priority=pair->remote->priority;
      controlled_priority=pair->local->priority;
    }
    pair->priority=(MIN(controlling_priority, controlled_priority))<<32 |
      (MAX(controlling_priority, controlled_priority))<<1 |
      (controlled_priority > controlling_priority?0:1);
  }

int nr_ice_candidate_pair_create(nr_ice_peer_ctx *pctx, nr_ice_candidate *lcand,nr_ice_candidate *rcand,nr_ice_cand_pair **pairp)
  {
    nr_ice_cand_pair *pair=0;
    int r,_status;
    UINT4 RTO;
    nr_ice_candidate tmpcand;
    UINT8 t_priority;
    int flags = 0;

    if(!(pair=R_NEW(nr_ice_cand_pair)))
      ABORT(R_NO_MEMORY);

    pair->pctx=pctx;

    nr_ice_candidate_pair_compute_codeword(pair,lcand,rcand);

    if(r=nr_concat_strings(&pair->as_string,pair->codeword,"|",lcand->addr.as_string,"|",
        rcand->addr.as_string,"(",lcand->label,"|",rcand->label,")", NULL))
      ABORT(r);

    nr_ice_candidate_pair_set_state(pctx,pair,NR_ICE_PAIR_STATE_FROZEN);
    pair->local=lcand;
    pair->remote=rcand;

    nr_ice_candidate_pair_set_priority(pair);

    /*
       TODO(bcampen@mozilla.com): Would be nice to log why this candidate was
       created (initial pair generation, triggered check, and new trickle
       candidate seem to be the possibilities here).
    */
    r_log(LOG_ICE,LOG_INFO,"ICE(%s)/CAND-PAIR(%s): Pairing candidate %s (%x):%s (%x) priority=%llu (%llx)",pctx->ctx->label,pair->codeword,lcand->addr.as_string,lcand->priority,rcand->addr.as_string,rcand->priority,pair->priority,pair->priority);

    /* Foundation */
    if(r=nr_concat_strings(&pair->foundation,lcand->foundation,"|",
      rcand->foundation,NULL))
      ABORT(r);

    /* Compute the RTO per S 16 */
    RTO = MAX(100, (pctx->ctx->Ta * pctx->waiting_pairs));

    /* Make a bogus candidate to compute a theoretical peer reflexive
     * priority per S 7.1.1.1 */
    memcpy(&tmpcand, lcand, sizeof(tmpcand));
    /* Non-standard behavior, but that's because the standard is wrong!  If
     * this is a relay candidate, it is never appropriate to assign it the
     * priority of a prflx. One could attempt to compensate for this on the
     * other side by overriding prflx candidates (and their priority) when
     * identical trickle candidates arrive, but it is prudent to avoid creating
     * this situation in the first place.
     * TODO: It may be appropriate to assign it a higher priority than _other_
     * relay candidates. However, there are probably very few cases where it
     * would matter.
     * TODO: Note that we still have to contend with mismatched priorities for
     * srflx here; if we have two srflx, and one of them is learned by the
     * other side as a prflx, the other side is going to have a higher priority
     * for that pair unless the priority is updated. For ICE-bis nomination,
     * this won't matter a whole lot, but we do aggressive nomination, and
     * there it is crucial. The controlled side has to pick the highest
     * priority nominated pair, and all of them will get nominated. */
    if (tmpcand.type != RELAYED) {
      tmpcand.type = PEER_REFLEXIVE;
    }
    if (r=nr_ice_candidate_compute_priority(&tmpcand))
      ABORT(r);
    t_priority = tmpcand.priority;

    flags = NR_STUN_TRANSPORT_ADDR_CHECK_WILDCARD;
    if (!(pctx->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LOOPBACK;
    }
    if (!(pctx->ctx->flags & NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL)) {
      flags |= NR_STUN_TRANSPORT_ADDR_CHECK_LINK_LOCAL;
    }

    /* Our sending context */
    if (r = nr_stun_client_ctx_create(pair->as_string, lcand->osock,
                                      &rcand->addr, RTO, flags,
                                      &pair->stun_client))
      ABORT(r);
    if(!(pair->stun_client->params.ice_binding_request.username=strdup(rcand->stream->l2r_user)))
      ABORT(R_NO_MEMORY);
    if(r=r_data_copy(&pair->stun_client->params.ice_binding_request.password,
      &rcand->stream->l2r_pass))
      ABORT(r);
    /* TODO(ekr@rtfm.com): Do we need to frob this when we change role. Bug 890667 */
    pair->stun_client->params.ice_binding_request.control = pctx->controlling?
      NR_ICE_CONTROLLING:NR_ICE_CONTROLLED;
    pair->stun_client->params.ice_binding_request.priority=t_priority;

    pair->stun_client->params.ice_binding_request.tiebreaker=pctx->tiebreaker;

    *pairp=pair;

    _status=0;
  abort:
    if(_status){
      nr_ice_candidate_pair_destroy(&pair);
    }
    return(_status);
  }

int nr_ice_candidate_pair_destroy(nr_ice_cand_pair **pairp)
  {
    nr_ice_cand_pair *pair;

    if(!pairp || !*pairp)
      return(0);

    pair=*pairp;
    *pairp=0;

    // record stats back to the ice ctx on destruction
    if (pair->stun_client) {
      nr_accumulate_count(&(pair->local->ctx->stats.stun_retransmits), pair->stun_client->retransmit_ct);
    }

    free(pair->as_string);
    free(pair->foundation);
    nr_ice_socket_deregister(pair->local->isock,pair->stun_client_handle);
    if (pair->stun_client) {
      free(pair->stun_client->params.ice_binding_request.username);
      free(pair->stun_client->params.ice_binding_request.password.data);
      nr_stun_client_ctx_destroy(&pair->stun_client);
    }

    NR_async_timer_cancel(pair->stun_cb_timer);
    NR_async_timer_cancel(pair->restart_role_change_cb_timer);
    NR_async_timer_cancel(pair->restart_nominated_cb_timer);

    free(pair);
    return(0);
  }

int nr_ice_candidate_pair_unfreeze(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair)
  {
    assert(pair->state==NR_ICE_PAIR_STATE_FROZEN);

    nr_ice_candidate_pair_set_state(pctx,pair,NR_ICE_PAIR_STATE_WAITING);

    return(0);
  }

static void nr_ice_candidate_pair_stun_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    int r,_status;
    nr_ice_cand_pair *pair=(nr_ice_cand_pair*)cb_arg;
    int use_candidate=pair->stun_client->mode==NR_ICE_CLIENT_MODE_USE_CANDIDATE;
    nr_ice_cand_pair *actual_pair=0;
    nr_ice_candidate *cand=0;
    nr_stun_message *sres;
    nr_transport_addr *request_src;
    nr_transport_addr *request_dst;
    nr_transport_addr *response_src;
    nr_transport_addr response_dst;
    nr_stun_message_attribute *attr;

    pair->stun_cb_timer=0;

    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s): STUN cb on pair addr = %s",
      pair->pctx->label,pair->local->stream->label,pair->codeword,pair->as_string);

    /* This ordinarily shouldn't happen, but can if we're
       doing the second check to confirm nomination.
       Just bail out */
    if(pair->state==NR_ICE_PAIR_STATE_SUCCEEDED)
      goto done;

    switch(pair->stun_client->state){
      case NR_STUN_CLIENT_STATE_FAILED:
        sres=pair->stun_client->response;
        if(sres && nr_stun_message_has_attribute(sres,NR_STUN_ATTR_ERROR_CODE,&attr)&&attr->u.error_code.number==487){

          /*
           * Flip the controlling bit; subsequent 487s for other pairs will be
           * ignored, since we abandon their STUN transactions.
           */
          nr_ice_peer_ctx_switch_controlling_role(pair->pctx);

          return;
        }
        [[fallthrough]];
      case NR_STUN_CLIENT_STATE_TIMED_OUT:
        nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
        break;
      case NR_STUN_CLIENT_STATE_DONE:
        /* make sure the addresses match up S 7.1.2.2 */
        response_src=&pair->stun_client->peer_addr;
        request_dst=&pair->remote->addr;
        if (nr_transport_addr_cmp(response_src,request_dst,NR_TRANSPORT_ADDR_CMP_MODE_ALL)){
          r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s)/CAND-PAIR(%s): Peer address mismatch %s != %s",pair->pctx->label,pair->codeword,response_src->as_string,request_dst->as_string);
          nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
          break;
        }
        request_src=&pair->stun_client->my_addr;
        nr_socket_getaddr(pair->local->osock,&response_dst);
        if (nr_transport_addr_cmp(request_src,&response_dst,NR_TRANSPORT_ADDR_CMP_MODE_ALL)){
          r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s)/CAND-PAIR(%s): Local address mismatch %s != %s",pair->pctx->label,pair->codeword,request_src->as_string,response_dst.as_string);
          nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
          break;
        }

        // Update the rtt related fields
        if (pair->stun_client->rtt_valid) {
          nr_ice_candidate_pair_update_rtt(pair, pair->stun_client->rtt_ms);
          // clear rtt_ms so we can't double process it.
          pair->stun_client->rtt_valid = 0;
          pair->stun_client->rtt_ms = 0;
        }

        if (pair->local->type == RELAYED) {
          /* For a relay candidate pair, there may be address translation
           * between the TURN server and the remote peer, so we cannot trust
           * XOR-MAPPED-ADDRESS to match our relay address. Traffic continues
           * to flow via the TURN relay regardless, so just accept the
           * response as proof that the path works. A peer reflexive
           * candidate would be wrong here -- it would bypass the relay. */
          nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_SUCCEEDED);
        }
        else if(strlen(pair->stun_client->results.ice_binding_response.mapped_addr.as_string)==0){
          /* we're using the mapped_addr returned by the server to lookup our
           * candidate, but if the server fails to do that we can't perform
           * the lookup -- this may be a BUG because if we've gotten here
           * then the transaction ID check succeeded, and perhaps we should
           * just assume that it's the server we're talking to and that our
           * peer is ok, but I'm not sure how that'll interact with the
           * peer reflexive logic below */
          r_log(LOG_ICE,LOG_WARNING,"ICE-PEER(%s)/CAND-PAIR(%s): server failed to return mapped address on pair %s", pair->pctx->label,pair->codeword,pair->as_string);
          nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
          break;
        }
        else if(!nr_transport_addr_cmp(&pair->local->addr,&pair->stun_client->results.ice_binding_response.mapped_addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL)){
          nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_SUCCEEDED);
        }
        else if(pair->stun_client->state == NR_STUN_CLIENT_STATE_DONE) {
          /* OK, this didn't correspond to a pair on the check list, but
             it probably matches one of our candidates */

          cand=TAILQ_FIRST(&pair->local->component->candidates);
          while(cand){
            if(!nr_transport_addr_cmp(&cand->addr,&pair->stun_client->results.ice_binding_response.mapped_addr,NR_TRANSPORT_ADDR_CMP_MODE_ALL)) {
              r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): found pre-existing local candidate of type %d for mapped address %s", pair->pctx->label,cand->type,cand->addr.as_string);
              assert(cand->type != HOST);
              break;
            }

            cand=TAILQ_NEXT(cand,entry_comp);
          }

          if(!cand) {
            /* OK, nothing found, must be a new peer reflexive */
            if (pair->local->stream->flags & NR_ICE_CTX_FLAGS_RELAY_ONLY) {
              /* Any STUN response with a reflexive address in it is unwanted
                 when we'll send on relay only. Bail since cand is used below. */
              goto done;
            }
            if(r=nr_ice_candidate_create(pair->pctx->ctx,
              pair->local->component,pair->local->isock,pair->local->osock,
              PEER_REFLEXIVE,pair->local->tcp_type,0,pair->local->component->component_id,&cand))
              ABORT(r);
            if(r=nr_transport_addr_copy(&cand->addr,&pair->stun_client->results.ice_binding_response.mapped_addr))
              ABORT(r);
            cand->state=NR_ICE_CAND_STATE_INITIALIZED;
            TAILQ_INSERT_TAIL(&pair->local->component->candidates,cand,entry_comp);
          } else {
            /* Check if we have a pair for this candidate already. */
            if(r=nr_ice_media_stream_find_pair(pair->remote->stream, cand, pair->remote, &actual_pair)) {
              r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s): no pair exists for %s and %s", pair->pctx->label,cand->addr.as_string, pair->remote->addr.as_string);
            }
          }

          if(!actual_pair) {
            if(r=nr_ice_candidate_pair_create(pair->pctx,cand,pair->remote, &actual_pair))
              ABORT(r);

            if(r=nr_ice_socket_register_stun_client(actual_pair->local->isock,
              actual_pair->stun_client,&actual_pair->stun_client_handle))
              ABORT(r);

            if(r=nr_ice_component_insert_pair(actual_pair->remote->component,actual_pair))
              ABORT(r);

            /* If the original pair was nominated, make us nominated too. */
            if(pair->peer_nominated)
              actual_pair->peer_nominated=1;

            /* Now mark the orig pair failed */
            nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
          }

          assert(actual_pair);
          nr_ice_candidate_pair_set_state(actual_pair->pctx,actual_pair,NR_ICE_PAIR_STATE_SUCCEEDED);
          pair=actual_pair;

        }

        /* Should we set nominated? */
        if(pair->pctx->controlling){
          if(use_candidate)
            pair->nominated=1;
        }
        else{
          if(pair->peer_nominated)
            pair->nominated=1;
        }


        /* increment the number of valid pairs in the component */
        /* We don't bother to maintain a separate valid list */
        pair->remote->component->valid_pairs++;

        /* S 7.1.2.2: unfreeze other pairs with the same foundation*/
        if(r=nr_ice_media_stream_unfreeze_pairs_foundation(pair->remote->stream,pair->foundation))
          ABORT(r);

        /* Deal with this pair being nominated */
        if(pair->nominated){
          if(r=nr_ice_component_nominated_pair(pair->remote->component, pair))
            ABORT(r);
        }

        break;
      default:
        ABORT(R_INTERNAL);
    }

    /* If we're controlling but in regular mode, ask the handler
       if he wants to nominate something and stop... */
    if(pair->pctx->controlling && !nr_ice_peer_ctx_aggressive_nomination(pair->pctx)){
      nr_ice_component_maybe_select_pair(pair->pctx,pair->remote->component);
    }

  done:
    _status=0;
  abort:
    if (_status) {
      // cb doesn't return anything, but we should probably log that we aborted
      // This also quiets the unused variable warnings.
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s): STUN cb pair addr = %s abort with status: %d",
        pair->pctx->label,pair->local->stream->label,pair->codeword,pair->as_string, _status);
    }
    return;
  }

static void nr_ice_candidate_pair_restart(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair)
  {
    int r,_status;
    UINT4 mode;

    nr_ice_candidate_pair_set_state(pctx,pair,NR_ICE_PAIR_STATE_IN_PROGRESS);

    /* Start STUN */
    if(pair->pctx->controlling && nr_ice_peer_ctx_aggressive_nomination(pair->pctx))
      mode=NR_ICE_CLIENT_MODE_USE_CANDIDATE;
    else
      mode=NR_ICE_CLIENT_MODE_BINDING_REQUEST;

    nr_stun_client_reset(pair->stun_client);

    if(r=nr_stun_client_start(pair->stun_client,mode,nr_ice_candidate_pair_stun_cb,pair))
      ABORT(r);

    if ((r=nr_ice_ctx_remember_id(pair->pctx->ctx, pair->stun_client->request))) {
      /* ignore if this fails (which it shouldn't) because it's only an
       * optimization and the cleanup routines are not going to do the right
       * thing if this fails */
      assert(0);
    }

    _status=0;
  abort:
    if(_status){
      /* Don't fire the CB, but schedule it to fire ASAP */
      assert(!pair->stun_cb_timer);
      NR_ASYNC_TIMER_SET(0,nr_ice_candidate_pair_stun_cb,pair, &pair->stun_cb_timer);
      _status=0;
    }
  }

int nr_ice_candidate_pair_start(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair)
  {
    int r,_status;

    /* Register the stun ctx for when responses come in*/
    if(r=nr_ice_socket_register_stun_client(pair->local->isock,pair->stun_client,&pair->stun_client_handle))
      ABORT(r);

    nr_ice_candidate_pair_restart(pctx, pair);

    _status=0;
  abort:
    return(_status);
  }

static int nr_ice_candidate_copy_for_triggered_check(nr_ice_cand_pair *pair)
  {
    int r,_status;
    nr_ice_cand_pair *copy;

    if(r=nr_ice_candidate_pair_create(pair->pctx, pair->local, pair->remote, &copy))
      ABORT(r);

    /* Preserve nomination status */
    copy->peer_nominated= pair->peer_nominated;
    copy->nominated = pair->nominated;

    r_log(LOG_ICE,LOG_INFO,"CAND-PAIR(%s): Adding pair to check list and trigger check queue: %s",pair->codeword,pair->as_string);
    nr_ice_candidate_pair_insert(&pair->remote->stream->check_list,copy);
    nr_ice_candidate_pair_trigger_check_append(&pair->remote->stream->trigger_check_queue,copy);

    copy->triggered = 1;
    nr_ice_candidate_pair_set_state(copy->pctx,copy,NR_ICE_PAIR_STATE_WAITING);

    _status=0;
  abort:
    return(_status);
}

int nr_ice_candidate_pair_do_triggered_check(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair, int force)
  {
    int r,_status;

    if(pair->state==NR_ICE_PAIR_STATE_SUCCEEDED) {
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND_PAIR(%s): No new trigger check for succeeded pair",pctx->label,pair->codeword);
      return(0);
    } else if (pair->local->stream->obsolete) {
      r_log(LOG_ICE, LOG_DEBUG,
            "ICE-PEER(%s)/CAND_PAIR(%s): No new trigger check for pair with "
            "obsolete stream",
            pctx->label, pair->codeword);
      return (0);
    }

    /* Do not run this logic more than once on a given pair (|force| is set
     * when the check we received has USE-CANDIDATE for this pair for the first
     * time, and this pair is a higher priority than anything that has been
     * nominated so far) */
    if(!pair->triggered || force){
      r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/CAND-PAIR(%s): triggered check on %s",pctx->label,pair->codeword,pair->as_string);

      pair->triggered=1;

      switch(pair->state){
        case NR_ICE_PAIR_STATE_FAILED:
          /* OK, there was a pair, it's just invalid: According to Section
           * 7.2.1.4, we need to resurrect it */
          r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/CAND-PAIR(%s): received STUN check on failed pair, resurrecting: %s",pctx->label,pair->codeword,pair->as_string);
          [[fallthrough]];
        case NR_ICE_PAIR_STATE_FROZEN:
          nr_ice_candidate_pair_set_state(pctx,pair,NR_ICE_PAIR_STATE_WAITING);
          [[fallthrough]];
        case NR_ICE_PAIR_STATE_WAITING:
          /* Append it additionally to the trigger check queue */
          r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/CAND-PAIR(%s): Inserting pair to trigger check queue: %s",pctx->label,pair->codeword,pair->as_string);
          nr_ice_candidate_pair_trigger_check_append(&pair->remote->stream->trigger_check_queue,pair);
          break;
        case NR_ICE_PAIR_STATE_CANCELLED:
          r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/CAND-PAIR(%s): received STUN check on cancelled pair, resurrecting: %s",pctx->label,pair->codeword,pair->as_string);
          [[fallthrough]];
        case NR_ICE_PAIR_STATE_IN_PROGRESS:
          /* Instead of trying to maintain two stun contexts on the same pair,
           * and handling heterogenous responses and error conditions, we instead
           * create a second pair that is identical except that it has the
           * |triggered| bit set. We also cancel the original pair, but it can
           * still succeed on its own in the special waiting state. */
          if(r=nr_ice_candidate_copy_for_triggered_check(pair))
            ABORT(r);
          nr_ice_candidate_pair_cancel(pair->pctx,pair,1);
          break;
        default:
          /* all states are handled - a new/unknown state should not
           * automatically enter the start_checks() below */
          assert(0);
          break;
      }

      /* Ensure that the timers are running to start checks on the topmost entry
       * of the triggered check queue. */
      if(r=nr_ice_media_stream_start_checks(pair->pctx,pair->remote->stream))
        ABORT(r);
    }

    _status=0;
  abort:
    return(_status);
  }

void nr_ice_candidate_pair_cancel(nr_ice_peer_ctx *pctx,nr_ice_cand_pair *pair, int move_to_wait_state)
  {
    if(pair->restart_nominated_cb_timer){
      NR_async_timer_cancel(pair->restart_nominated_cb_timer);
      pair->restart_nominated_cb_timer=0;
    }

    if(pair->state != NR_ICE_PAIR_STATE_FAILED){
      /* If it's already running we need to terminate the stun */
      if(pair->state==NR_ICE_PAIR_STATE_IN_PROGRESS){
        if(move_to_wait_state) {
          nr_stun_client_wait(pair->stun_client);
        } else {
          nr_stun_client_cancel(pair->stun_client);
        }
      }
      nr_ice_candidate_pair_set_state(pctx,pair,NR_ICE_PAIR_STATE_CANCELLED);
    }
  }

void nr_ice_candidate_pair_set_state(nr_ice_peer_ctx *pctx, nr_ice_cand_pair *pair, int state)
  {
    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/CAND-PAIR(%s): setting pair to state %s: %s",
      pctx->label,pair->codeword,nr_ice_cand_pair_states[state],pair->as_string);

    /* NOTE: This function used to reference pctx->state instead of
       pair->state and the assignment to pair->state was at the top
       of this function. Because pctx->state was never changed, this seems to have
       been a typo. The natural logic is "if the state changed
       decrement the counter" so this implies we should be checking
       the pair state rather than the pctx->state.

       This didn't cause big problems because waiting_pairs was only
       used for pacing, so the pacing just was kind of broken.

       This note is here as a reminder until we do more testing
       and make sure that in fact this was a typo.
    */
    if(pair->state!=NR_ICE_PAIR_STATE_WAITING){
      if(state==NR_ICE_PAIR_STATE_WAITING)
        pctx->waiting_pairs++;
    }
    else{
      if(state!=NR_ICE_PAIR_STATE_WAITING)
        pctx->waiting_pairs--;

      assert(pctx->waiting_pairs>=0);
    }
    pair->state=state;


    if(pair->state==NR_ICE_PAIR_STATE_FAILED ||
       pair->state==NR_ICE_PAIR_STATE_CANCELLED){
      nr_ice_component_failed_pair(pair->remote->component, pair);
    }
  }

void nr_ice_candidate_pair_dump_state(nr_ice_cand_pair *pair, int log_level)
  {
    r_log(LOG_ICE,log_level,"CAND-PAIR(%s): pair %s: state=%s, priority=0x%llx\n",pair->codeword,pair->as_string,nr_ice_cand_pair_states[pair->state],pair->priority);
  }


int nr_ice_candidate_pair_trigger_check_append(nr_ice_cand_pair_head *head,nr_ice_cand_pair *pair)
  {
    nr_ice_cand_pair *p1 = 0, *p2 = 0;
    TAILQ_FOREACH_SAFE(p1, head, triggered_check_queue_entry, p2) {
      if (p1 == pair) {
        return(0);
      }
    }

    TAILQ_INSERT_TAIL(head,pair,triggered_check_queue_entry);

    return(0);
  }

void nr_ice_candidate_pair_insert(nr_ice_cand_pair_head *head,nr_ice_cand_pair *pair)
  {
    nr_ice_cand_pair *c1;

    c1=TAILQ_FIRST(head);
    while(c1){
      if(c1->priority < pair->priority){
        TAILQ_INSERT_BEFORE(c1,pair,check_queue_entry);
        break;
      }

      c1=TAILQ_NEXT(c1,check_queue_entry);
    }
    if(!c1) TAILQ_INSERT_TAIL(head,pair,check_queue_entry);
  }

int nr_ice_candidate_pair_nominate(nr_ice_cand_pair *pair, UINT4 delay)
  {
    nr_ice_cand_pair *pending;
    nr_ice_component *component=pair->remote->component;
    bool already_nominated=false;

    if(pair->state != NR_ICE_PAIR_STATE_SUCCEEDED)
      return(R_BAD_ARGS);

    if(component->nominated)
      return(R_ALREADY);

    TAILQ_FOREACH(pending,&pair->remote->stream->check_list,check_queue_entry){
      if(pending->remote->component != component)
        continue;

      if(pending->nominated ||
         (pending->state == NR_ICE_PAIR_STATE_IN_PROGRESS &&
          pending->stun_client->mode == NR_ICE_CLIENT_MODE_USE_CANDIDATE))
        already_nominated=true;

      /* Replace pending nominations, or cancel them if one already started. */
      if(pending->restart_nominated_cb_timer){
        NR_async_timer_cancel(pending->restart_nominated_cb_timer);
        pending->restart_nominated_cb_timer=0;
      }
    }

    if(already_nominated)
      return(R_ALREADY);

    return(NR_ASYNC_TIMER_SET(delay,nr_ice_candidate_pair_restart_stun_nominated_cb,
      pair,&pair->restart_nominated_cb_timer));
  }

static void nr_ice_candidate_pair_restart_stun_nominated_cb(NR_SOCKET s, int how, void *cb_arg)
  {
    nr_ice_cand_pair *pair=(nr_ice_cand_pair*)cb_arg;
    int r,_status;

    pair->restart_nominated_cb_timer=0;

    if(pair->state != NR_ICE_PAIR_STATE_SUCCEEDED ||
       !pair->pctx->controlling || pair->local->stream->obsolete)
      return;

    nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_IN_PROGRESS);

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s)/COMP(%d): Restarting pair as nominated: %s",pair->pctx->label,pair->local->stream->label,pair->codeword,pair->remote->component->component_id,pair->as_string);

    nr_stun_client_reset(pair->stun_client);

    if(r=nr_stun_client_start(pair->stun_client,NR_ICE_CLIENT_MODE_USE_CANDIDATE,nr_ice_candidate_pair_stun_cb,pair))
      ABORT(r);

    if((r=nr_ice_ctx_remember_id(pair->pctx->ctx, pair->stun_client->request)))
      assert(0);

    _status=0;
  abort:
    if (_status) {
      // cb doesn't return anything, but we should probably log that we aborted
      // This also quiets the unused variable warnings.
      r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s)/COMP(%d): STUN nominated cb pair as nominated: %s abort with status: %d",
        pair->pctx->label,pair->local->stream->label,pair->codeword,pair->remote->component->component_id,pair->as_string, _status);
      if(pair->state==NR_ICE_PAIR_STATE_IN_PROGRESS){
        nr_ice_candidate_pair_set_state(pair->pctx,pair,NR_ICE_PAIR_STATE_FAILED);
        if(pair->pctx->controlling &&
           !nr_ice_peer_ctx_aggressive_nomination(pair->pctx)){
          r_log(LOG_ICE,LOG_ERR,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s)/COMP(%d): Nomination failed to start; asking the application to select another pair",pair->pctx->label,pair->local->stream->label,pair->codeword,pair->remote->component->component_id);
          nr_ice_component_maybe_select_pair(pair->pctx,pair->remote->component);
        }
      }
    }
    return;
  }

static void nr_ice_candidate_pair_restart_stun_role_change_cb(NR_SOCKET s, int how, void *cb_arg)
 {
    nr_ice_cand_pair *pair=(nr_ice_cand_pair*)cb_arg;

    pair->restart_role_change_cb_timer=0;

    r_log(LOG_ICE,LOG_INFO,"ICE-PEER(%s)/STREAM(%s)/CAND-PAIR(%s):COMP(%d): Restarting pair as %s: %s",pair->pctx->label,pair->local->stream->label,pair->codeword,pair->remote->component->component_id,pair->pctx->controlling ? "CONTROLLING" : "CONTROLLED",pair->as_string);

    nr_ice_candidate_pair_restart(pair->pctx, pair);
  }

void nr_ice_candidate_pair_role_change(nr_ice_cand_pair *pair)
  {
    pair->stun_client->params.ice_binding_request.control = pair->pctx->controlling ? NR_ICE_CONTROLLING : NR_ICE_CONTROLLED;
    nr_ice_candidate_pair_set_priority(pair);

    if(pair->state == NR_ICE_PAIR_STATE_IN_PROGRESS) {
      /* We could try only restarting in-progress pairs when they receive their
       * 487, but this ends up being simpler, because any extra 487 are dropped.
       */
      if(!pair->restart_role_change_cb_timer)
        NR_ASYNC_TIMER_SET(0,nr_ice_candidate_pair_restart_stun_role_change_cb,pair,&pair->restart_role_change_cb_timer);
    }
  }

void nr_ice_candidate_pair_update_rtt(nr_ice_cand_pair *pair, UINT8 rtt_ms)
  {
    assert(pair);

    pair->responses_recvd++;
    pair->current_rtt_ms = rtt_ms;
    pair->total_rtt_ms += rtt_ms;
    r_log(LOG_ICE,LOG_DEBUG,"ICE-PEER(%s)/CAND-PAIR(%s): responses_recvd %llu, current_rtt_ms %llu ms, total_rtt_ms %llu ms",
          pair->pctx->label,pair->codeword,pair->responses_recvd,pair->current_rtt_ms,pair->total_rtt_ms);
  }

static void nr_ice_candidate_pair_compute_codeword(nr_ice_cand_pair *pair,
  nr_ice_candidate *lcand, nr_ice_candidate *rcand)
  {
    char as_string[2048];

    snprintf(as_string,
             sizeof(as_string),
             "%s|%s(%s|%s)",
             lcand->addr.as_string,
             rcand->addr.as_string,
             lcand->label,
             rcand->label);

    nr_ice_compute_codeword(as_string,strlen(as_string),pair->codeword);
  }

