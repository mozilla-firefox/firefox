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

#include "nr_api.h"
#include "stun.h"

static int nr_stun_server_destroy_client(nr_stun_server_client *clnt);
static int nr_stun_server_send_response(nr_stun_server_ctx *ctx, nr_socket *sock, nr_transport_addr *peer_addr, nr_stun_message *res, nr_stun_server_client *clnt);
static int nr_stun_server_process_request_auth_checks(nr_stun_server_ctx *ctx, nr_stun_message *req, int auth_rule, nr_stun_message *res);


int nr_stun_server_ctx_create(char *label, nr_stun_server_ctx **ctxp)
  {
    int r,_status;
    nr_stun_server_ctx *ctx=0;

    if ((r=nr_stun_startup()))
      ABORT(r);

    if(!(ctx=R_NEW(nr_stun_server_ctx)))
      ABORT(R_NO_MEMORY);

    if(!(ctx->label=strdup(label)))
      ABORT(R_NO_MEMORY);

    STAILQ_INIT(&ctx->clients);

    *ctxp=ctx;

    _status=0;
  abort:
    return(_status);
  }

int nr_stun_server_ctx_destroy(nr_stun_server_ctx **ctxp)
  {
    nr_stun_server_ctx *ctx;
    nr_stun_server_client *clnt1,*clnt2;

    if(!ctxp || !*ctxp)
      return(0);

    ctx=*ctxp;

    STAILQ_FOREACH_SAFE(clnt1, &ctx->clients, entry, clnt2) {
        nr_stun_server_destroy_client(clnt1);
    }

    nr_stun_server_destroy_client(ctx->default_client);

    free(ctx->label);
    free(ctx);

    return(0);
  }

static int nr_stun_server_client_create(nr_stun_server_ctx *ctx, const char *client_label, char *user, Data *pass, nr_stun_server_cb cb, void *cb_arg, nr_stun_server_client **clntp)
  {
    nr_stun_server_client *clnt=0;
    int r,_status;

    if(!(clnt=R_NEW(nr_stun_server_client)))
      ABORT(R_NO_MEMORY);

    if(!(clnt->label=strdup(client_label)))
      ABORT(R_NO_MEMORY);

    if(!(clnt->username=strdup(user)))
      ABORT(R_NO_MEMORY);

    if(r=r_data_copy(&clnt->password,pass))
      ABORT(r);

    r_log(NR_LOG_STUN,LOG_DEBUG,"STUN-SERVER(%s)/CLIENT(%s): Adding client for %s",ctx->label, client_label, user);
    clnt->stun_server_cb=cb;
    clnt->cb_arg=cb_arg;

    *clntp = clnt;
    _status=0;
 abort:
    if(_status){
      nr_stun_server_destroy_client(clnt);
    }
    return(_status);
  }

int nr_stun_server_add_client(nr_stun_server_ctx *ctx, char *client_label, char *user, Data *pass, nr_stun_server_cb cb, void *cb_arg)
  {
   int r,_status;
   nr_stun_server_client *clnt;

    if (r=nr_stun_server_client_create(ctx, client_label, user, pass, cb, cb_arg, &clnt))
      ABORT(r);

    STAILQ_INSERT_TAIL(&ctx->clients,clnt,entry);

    _status=0;
  abort:
    return(_status);
  }

int nr_stun_server_add_default_client(nr_stun_server_ctx *ctx, char *ufrag, Data *pass, nr_stun_server_cb cb, void *cb_arg)
  {
    int r,_status;
    nr_stun_server_client *clnt;

    assert(!ctx->default_client);
    if (ctx->default_client)
      ABORT(R_INTERNAL);

    if (r=nr_stun_server_client_create(ctx, "default_client", ufrag, pass, cb, cb_arg, &clnt))
      ABORT(r);

    ctx->default_client = clnt;

    _status=0;
  abort:
    return(_status);
  }

int nr_stun_server_ctx_update_component(nr_stun_server_ctx *ctx, char *ufrag,
                                        Data *pass, nr_stun_server_cb cb,
                                        void *cb_arg)
  {
    int r, _status;
    nr_stun_server_client *clnt = ctx->default_client;
    char *username = nullptr;
    Data password = {};

    assert(clnt);
    if (!clnt) {
      ABORT(R_INTERNAL);
    }

    if (!(username = strdup(ufrag))) {
      ABORT(R_NO_MEMORY);
    }

    if ((r = r_data_copy(&password, pass))) {
      ABORT(r);
    }

    r_log(NR_LOG_STUN, LOG_DEBUG, "STUN-SERVER(%s)/CLIENT(%s): Updating default client from %s to %s",
          ctx->label, clnt->label, clnt->username, username);

    // Nothing below can fail, so the old credentials can go.
    free(clnt->username);
    clnt->username = username;
    r_data_zfree(&clnt->password);
    clnt->password = password;
    clnt->stun_server_cb = cb;
    clnt->cb_arg = cb_arg;

    return 0;

 abort:
    free(username);
    r_data_zfree(&password);
    return _status;
  }

int nr_stun_server_remove_client(nr_stun_server_ctx *ctx, void *cb_arg)
  {
    nr_stun_server_client *clnt1,*clnt2;
    int found = 0;

    STAILQ_FOREACH_SAFE(clnt1, &ctx->clients, entry, clnt2) {
      if(clnt1->cb_arg == cb_arg) {
        STAILQ_REMOVE(&ctx->clients, clnt1, nr_stun_server_client_, entry);
        nr_stun_server_destroy_client(clnt1);
        found++;
      }
    }

    if (!found)
      ERETURN(R_NOT_FOUND);

    return 0;
  }

static int nr_stun_server_get_password(void *arg, nr_stun_message *msg, Data **password)
  {
    int _status;
    nr_stun_server_ctx *ctx = (nr_stun_server_ctx*)arg;
    nr_stun_server_client *clnt = 0;
    nr_stun_message_attribute *username_attribute;

    if ((nr_stun_get_message_client(ctx, msg, &clnt))) {
        if (! nr_stun_message_has_attribute(msg, NR_STUN_ATTR_USERNAME, &username_attribute)) {
           r_log(NR_LOG_STUN,LOG_WARNING,"STUN-SERVER(%s): Missing Username",ctx->label);
           ABORT(R_NOT_FOUND);
        }

        /* Although this is an exceptional condition, we'll already have seen a
         * NOTICE-level log message about the unknown user, so additional log
         * messages at any level higher than DEBUG are unnecessary. */

        r_log(NR_LOG_STUN,LOG_DEBUG,"STUN-SERVER(%s): Unable to find password for unknown user: %s",ctx->label,username_attribute->u.username);
        ABORT(R_NOT_FOUND);
    }

    *password = &clnt->password;

    _status=0;
  abort:
    return(_status);
  }

int nr_stun_server_process_request_auth_checks(nr_stun_server_ctx *ctx, nr_stun_message *req, int auth_rule, nr_stun_message *res)
  {
    int r,_status;

    if (nr_stun_message_has_attribute(req, NR_STUN_ATTR_MESSAGE_INTEGRITY, 0)
     || !(auth_rule & NR_STUN_AUTH_RULE_OPTIONAL)) {
        /* favor long term credentials over short term, if both are supported */

        if (auth_rule & NR_STUN_AUTH_RULE_LONG_TERM) {
            if ((r=nr_stun_receive_request_long_term_auth(req, ctx, res)))
                ABORT(r);
        }
        else if (auth_rule & NR_STUN_AUTH_RULE_SHORT_TERM) {
            if ((r=nr_stun_receive_request_or_indication_short_term_auth(req, res)))
                ABORT(r);
        }
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_stun_server_process_request(nr_stun_server_ctx *ctx, nr_socket *sock, char *msg, int len, nr_transport_addr *peer_addr, int auth_rule)
  {
    int r,_status;
    char string[256];
    nr_stun_message *req = 0;
    nr_stun_message *res = 0;
    nr_stun_server_client *clnt = 0;
    nr_stun_server_request info;
    int error;
    int dont_free = 0;
    nr_transport_addr my_addr;

    if ((r=nr_socket_getaddr(sock, &my_addr))) {
      ABORT(r);
    }

    r_log(NR_LOG_STUN,LOG_DEBUG,"STUN-SERVER(%s): Received(my_addr=%s,peer_addr=%s)",ctx->label,my_addr.as_string,peer_addr->as_string);

    snprintf(string, sizeof(string)-1, "STUN-SERVER(%s): Received ", ctx->label);
    r_dump(NR_LOG_STUN, LOG_DEBUG, string, (char*)msg, len);

    memset(&info,0,sizeof(info));

    if ((r=nr_stun_message_create2(&req, (UCHAR*)msg, len)))
        ABORT(r);

    if ((r=nr_stun_message_create(&res)))
        ABORT(r);

    if ((r=nr_stun_decode_message(req, nr_stun_server_get_password, ctx))) {
        /* RFC5389 S 7.3 says "If any errors are detected, the message is
         * silently discarded."  */
#ifndef USE_STUN_PEDANTIC
        /* ... but that seems like a bad idea, at least return a 400 so
         * that the server isn't a black hole to the client */
        nr_stun_form_error_response(req, res, 400, "Bad Request - Failed to decode request");
        ABORT(R_ALREADY);
#endif /* USE_STUN_PEDANTIC */
        ABORT(R_REJECTED);
    }

    if ((r=nr_stun_receive_message(0, req))) {
        /* RFC5389 S 7.3 says "If any errors are detected, the message is
         * silently discarded."  */
#ifndef USE_STUN_PEDANTIC
        /* ... but that seems like a bad idea, at least return a 400 so
         * that the server isn't a black hole to the client */
        nr_stun_form_error_response(req, res, 400, "Bad Request - Section 7.3 check failed");
        ABORT(R_ALREADY);
#endif /* USE_STUN_PEDANTIC */
        ABORT(R_REJECTED);
    }

    if (NR_STUN_GET_TYPE_CLASS(req->header.type) != NR_CLASS_REQUEST
     && NR_STUN_GET_TYPE_CLASS(req->header.type) != NR_CLASS_INDICATION) {
         r_log(NR_LOG_STUN,LOG_WARNING,"STUN-SERVER(%s): Illegal message type: %04x",ctx->label,req->header.type);
        /* RFC5389 S 7.3 says "If any errors are detected, the message is
         * silently discarded."  */
#ifndef USE_STUN_PEDANTIC
        /* ... but that seems like a bad idea, at least return a 400 so
         * that the server isn't a black hole to the client */
        nr_stun_form_error_response(req, res, 400, "Bad Request - Unsupported message type");
        ABORT(R_ALREADY);
#endif /* USE_STUN_PEDANTIC */
        ABORT(R_REJECTED);
    }

    /* "The STUN agent then does any checks that are required by a
     * authentication mechanism that the usage has specified" */
    if ((r=nr_stun_server_process_request_auth_checks(ctx, req, auth_rule, res)))
        ABORT(r);

    if (NR_STUN_GET_TYPE_CLASS(req->header.type) == NR_CLASS_INDICATION) {
        if ((r=nr_stun_process_indication(req)))
            ABORT(r);
    }
    else {
        if ((r=nr_stun_process_request(req, res)))
            ABORT(r);
    }

    assert(res->header.type == 0);

    clnt = 0;
    if (NR_STUN_GET_TYPE_CLASS(req->header.type) == NR_CLASS_REQUEST) {
        if ((nr_stun_get_message_client(ctx, req, &clnt))) {
            if ((r=nr_stun_form_success_response(req, peer_addr, 0, res)))
                ABORT(r);
        }
        else {
            if ((r=nr_stun_form_success_response(req, peer_addr, &clnt->password, res)))
                ABORT(r);
        }
    }

    if(clnt && clnt->stun_server_cb){
        r_log(NR_LOG_STUN,LOG_DEBUG,"Entering STUN server callback");

        /* Set up the info */
        if(r=nr_transport_addr_copy(&info.src_addr,peer_addr))
            ABORT(r);

        info.request = req;
        info.response = res;

        error = 0;
        dont_free = 0;
        if (clnt->stun_server_cb(clnt->cb_arg,ctx,sock,&info,&dont_free,&error)) {
            if (error == 0)
                error = 500;

            nr_stun_form_error_response(req, res, error, "ICE Failure");
            ABORT(R_ALREADY);
        }
    }

    _status=0;
  abort:
    if (!res)
        goto skip_response;

    if (NR_STUN_GET_TYPE_CLASS(req->header.type) == NR_CLASS_INDICATION)
        goto skip_response;

    /* Now respond */

    if (_status != 0 && ! nr_stun_message_has_attribute(res, NR_STUN_ATTR_ERROR_CODE, 0))
         nr_stun_form_error_response(req, res, 500, "Failed to specify error");

    if ((r=nr_stun_server_send_response(ctx, sock, peer_addr, res, clnt))) {
        r_log(NR_LOG_STUN,LOG_ERR,"STUN-SERVER(label=%s): Failed sending response (my_addr=%s,peer_addr=%s)",ctx->label,my_addr.as_string,peer_addr->as_string);
        _status = R_FAILED;
    }

    if (0) {
  skip_response:
        _status = 0;
    }

    if (!dont_free) {
      nr_stun_message_destroy(&res);
      nr_stun_message_destroy(&req);
    }

    return(_status);
  }

static int nr_stun_server_send_response(nr_stun_server_ctx *ctx, nr_socket *sock, nr_transport_addr *peer_addr, nr_stun_message *res, nr_stun_server_client *clnt)
  {
    int r,_status;
    char string[256];
    nr_transport_addr my_addr;

    if ((r=nr_socket_getaddr(sock, &my_addr))) {
      ABORT(r);
    }

    r_log(NR_LOG_STUN,LOG_DEBUG,"STUN-SERVER(label=%s): Sending(my_addr=%s,peer_addr=%s)",ctx->label,my_addr.as_string,peer_addr->as_string);

    if ((r=nr_stun_encode_message(res))) {
        /* should never happen */
        r_log(NR_LOG_STUN,LOG_ERR,"STUN-SERVER(label=%s): Unable to encode message", ctx->label);
    }
    else {
        snprintf(string, sizeof(string)-1, "STUN(%s): Sending to %s ", ctx->label, peer_addr->as_string);
        r_dump(NR_LOG_STUN, LOG_DEBUG, string, (char*)res->buffer, res->length);

        if(r=nr_socket_sendto(sock,res->buffer,res->length,0,peer_addr))
          ABORT(r);
    }

    _status=0;
  abort:
    return(_status);
  }

static int nr_stun_server_destroy_client(nr_stun_server_client *clnt)
  {
    if (!clnt)
      return 0;

    free(clnt->label);
    free(clnt->username);
    r_data_zfree(&clnt->password);

    free(clnt);
    return(0);
  }

int nr_stun_get_message_client(nr_stun_server_ctx *ctx, nr_stun_message *req, nr_stun_server_client **out)
  {
    int _status;
    nr_stun_message_attribute *attr;
    nr_stun_server_client *clnt=0;

    if (! nr_stun_message_has_attribute(req, NR_STUN_ATTR_USERNAME, &attr)) {
       r_log(NR_LOG_STUN,LOG_WARNING,"STUN-SERVER(%s): Missing Username",ctx->label);
       ABORT(R_NOT_FOUND);
    }

    STAILQ_FOREACH(clnt, &ctx->clients, entry) {
        if (!strncmp(clnt->username, attr->u.username,
                     sizeof(attr->u.username)))
            break;
    }

    if (!clnt && ctx->default_client) {
      /* If we can't find a specific client see if this matches the default,
         which means that the username starts with our ufrag.
       */
      char *colon = strchr(attr->u.username, ':');
      if (colon && !strncmp(ctx->default_client->username,
                            attr->u.username,
                            colon - attr->u.username)) {
        clnt = ctx->default_client;
        r_log(NR_LOG_STUN,LOG_NOTICE,"STUN-SERVER(%s): Falling back to default client, username=: %s",ctx->label,attr->u.username);
      }
    }

    if (!clnt) {
        r_log(NR_LOG_STUN,LOG_WARNING,"STUN-SERVER(%s): Request from unknown user: %s",ctx->label,attr->u.username);
        ABORT(R_NOT_FOUND);
    }

    *out = clnt;

    _status=0;
  abort:
    return(_status);
  }

