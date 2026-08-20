/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// Some of this code is cut-and-pasted from nICEr. Copyright is:

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

#include <string>
#include <vector>

#include "ScopedNSSTypes.h"
#include "logging.h"
#include "mozilla/Preferences.h"
#include "nr_socket_proxy_config.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsIUUIDGenerator.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"
#include "pk11pub.h"
#include "runnable_utils.h"

// nICEr includes
// clang-format off
#include "nr_api.h"
#include "registry.h"
#include "addrs.h"
#include "async_timer.h"
#include "r_crc32.h"
#include "r_memory.h"
#include "ice_reg.h"
#include "transport_addr.h"
#include "nr_crypto.h"
#include "nr_socket.h"
#include "nr_socket_local.h"
#include "stun_reg.h"
#include "ice_codeword.h"
#include "ice_ctx.h"
#include "ice_candidate.h"
// clang-format on

// Local includes
#include "mozilla/Base64.h"
#include "nr_socket_prsock.h"
#include "nricectx.h"
#include "nricemediastream.h"
#include "nrinterfaceprioritizer.h"
#include "rlogconnector.h"
#include "test_nr_socket.h"

namespace mozilla {

using std::shared_ptr;
using ParsedIceServer = IceServerParser::ParsedIceServer;
using IceTransport = IceServerParser::IceTransport;

TimeStamp nr_socket_short_term_violation_time() {
  return NrSocketBase::short_term_violation_time();
}

TimeStamp nr_socket_long_term_violation_time() {
  return NrSocketBase::long_term_violation_time();
}

MOZ_MTLOG_MODULE("mtransport")

const char kNrIceTransportUdp[] = "udp";
const char kNrIceTransportTcp[] = "tcp";
const char kNrIceTransportTls[] = "tls";
static constexpr UINT4 kRegularNominationDelayMs = 20;

static bool initialized = false;

static int noop(void** obj) { return 0; }

static nr_socket_factory_vtbl ctx_socket_factory_vtbl = {nr_socket_local_create,
                                                         noop};

// Implement NSPR-based crypto algorithms
static int nr_crypto_nss_random_bytes(UCHAR* buf, size_t len) {
  UniquePK11SlotInfo slot(PK11_GetInternalSlot());
  if (!slot) return R_INTERNAL;

  SECStatus rv = PK11_GenerateRandomOnSlot(slot.get(), buf, len);
  if (rv != SECSuccess) return R_INTERNAL;

  return 0;
}

static int nr_crypto_nss_hmac(UCHAR* key, size_t keyl, UCHAR* buf, size_t bufl,
                              UCHAR* result) {
  CK_MECHANISM_TYPE mech = CKM_SHA_1_HMAC;
  PK11SlotInfo* slot = nullptr;
  MOZ_ASSERT(keyl > 0);
  SECItem keyi = {siBuffer, key, static_cast<unsigned int>(keyl)};
  PK11SymKey* skey = nullptr;
  PK11Context* hmac_ctx = nullptr;
  SECStatus status;
  unsigned int hmac_len;
  SECItem param = {siBuffer, nullptr, 0};
  int err = R_INTERNAL;

  slot = PK11_GetInternalKeySlot();
  if (!slot) goto abort;

  skey = PK11_ImportSymKey(slot, mech, PK11_OriginUnwrap, CKA_SIGN, &keyi,
                           nullptr);
  if (!skey) goto abort;

  hmac_ctx = PK11_CreateContextBySymKey(mech, CKA_SIGN, skey, &param);
  if (!hmac_ctx) goto abort;

  status = PK11_DigestBegin(hmac_ctx);
  if (status != SECSuccess) goto abort;

  status = PK11_DigestOp(hmac_ctx, buf, bufl);
  if (status != SECSuccess) goto abort;

  status = PK11_DigestFinal(hmac_ctx, result, &hmac_len, 20);
  if (status != SECSuccess) goto abort;

  MOZ_ASSERT(hmac_len == 20);

  err = 0;

abort:
  if (hmac_ctx) PK11_DestroyContext(hmac_ctx, PR_TRUE);
  if (skey) PK11_FreeSymKey(skey);
  if (slot) PK11_FreeSlot(slot);

  return err;
}

static int nr_crypto_nss_md5(UCHAR* buf, size_t bufl, UCHAR* result) {
  int err = R_INTERNAL;
  SECStatus rv;

  const SECHashObject* ho = HASH_GetHashObject(HASH_AlgMD5);
  MOZ_ASSERT(ho);
  if (!ho) goto abort;

  MOZ_ASSERT(ho->length == 16);

  rv = HASH_HashBuf(ho->type, result, buf, bufl);
  if (rv != SECSuccess) goto abort;

  err = 0;
abort:
  return err;
}

static nr_ice_crypto_vtbl nr_ice_crypto_nss_vtbl = {
    nr_crypto_nss_random_bytes, nr_crypto_nss_hmac, nr_crypto_nss_md5};

static bool ToNicerStunStruct(const char* aAddrForFqdn,
                              const ParsedIceServer& aEntry,
                              nr_ice_stun_server* aResult) {
  memset(aResult, 0, sizeof(nr_ice_stun_server));
  bool isTls = aEntry.mUri.IsTls();
  uint8_t protocol = IPPROTO_UDP;
  // TLS always uses TCP underneath
  if (isTls || (aEntry.mUri.mTransport == IceTransport::Tcp)) {
    protocol = IPPROTO_TCP;
  }

  if (isTls && !aAddrForFqdn) {
    // Note: letsencrypt has cooked up super-short-lifetime IP address based
    // certs. We may want to implement support for that.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=2019255
    MOZ_MTLOG(ML_ERROR, "TLS requires FQDN, not IP address");
    return false;
  }

  if (aEntry.mUri.mHost.Length() >= sizeof(aResult->addr.fqdn)) {
    MOZ_MTLOG(ML_ERROR, "FQDN too long: " << aEntry.mUri.mHost.get());
    return false;
  }

  const char* host = aAddrForFqdn ? aAddrForFqdn : aEntry.mUri.mHost.get();

  if (nr_str_port_to_transport_addr(host, aEntry.mUri.mPort, protocol,
                                    &(aResult->addr))) {
    MOZ_MTLOG(ML_ERROR, "Failed to init STUN server");
    return false;
  }

  if (aAddrForFqdn) {
    std::strncpy(aResult->addr.fqdn, aEntry.mUri.mHost.get(),
                 sizeof(aResult->addr.fqdn) - 1);  // Don't stomp trailing null
  }

  if (isTls) {
    aResult->addr.tls = true;
  }

  nr_transport_addr_fmt_addr_string(&(aResult->addr));
  return true;
}

static bool ToNicerTurnStruct(const char* aAddrForFqdn,
                              const ParsedIceServer& aEntry,
                              nr_ice_turn_server* aResult) {
  memset(aResult, 0, sizeof(nr_ice_turn_server));
  if (!ToNicerStunStruct(aAddrForFqdn, aEntry, &(aResult->turn_server))) {
    MOZ_MTLOG(ML_ERROR, "Failed to init STUN server");
    return false;
  }
  aResult->username = strdup(aEntry.mUsername.get());
  if (!aResult->username) {
    MOZ_MTLOG(ML_ERROR, "Couldn't allocate TURN username");
    return false;
  }

  if (aEntry.mPassword.IsEmpty()) {
    // TODO(https://bugzilla.mozilla.org/show_bug.cgi?id=2021075)
    // Once we check for this in the validation code, we might consider removing
    // this check. I'm inclined to keep it though, just in case.
    MOZ_MTLOG(ML_ERROR, "TURN password is empty");
    return false;
  }

  int r = r_data_create(&aResult->password,
                        reinterpret_cast<const UCHAR*>(aEntry.mPassword.get()),
                        aEntry.mPassword.Length());
  if (r) {
    free(aResult->username);
    MOZ_MTLOG(ML_ERROR, "Couldn't allocate TURN password");
    return false;
  }
  return true;
}

NrIceCtx::NrIceCtx(const std::string& name)
    : name_(name),
      ice_controlling_set_(false),
      ctx_(nullptr),
      peer_(nullptr),
      ice_handler_vtbl_(nullptr),
      ice_handler_(nullptr),
      ice_gather_handler_vtbl_(nullptr),
      ice_gather_handler_(nullptr),
      trickle_(true),
      config_(),
      nat_(nullptr),
      proxy_config_(nullptr) {}

/* static */
RefPtr<NrIceCtx> NrIceCtx::Create(const std::string& aName) {
  RefPtr<NrIceCtx> ctx = new NrIceCtx(aName);

  if (!ctx->Initialize()) {
    return nullptr;
  }

  return ctx;
}

nsresult NrIceCtx::SetIceConfig(const Config& aConfig) {
  config_ = aConfig;
  switch (config_.mPolicy) {
    case ICE_POLICY_RELAY:
      MOZ_MTLOG(ML_DEBUG, "SetIceConfig: relay only");
      nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES);
      nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_RELAY_ONLY);
      break;
    case ICE_POLICY_NO_HOST:
      MOZ_MTLOG(ML_DEBUG, "SetIceConfig: no host");
      nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES);
      nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_RELAY_ONLY);
      break;
    case ICE_POLICY_ALL:
      MOZ_MTLOG(ML_DEBUG, "SetIceConfig: all");
      nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_DISABLE_HOST_CANDIDATES);
      nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_RELAY_ONLY);
      break;
  }

  if (config_.mAllowLoopback) {
    nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_ALLOW_LOOPBACK);
  }

  if (config_.mAllowLinkLocal) {
    nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_ALLOW_LINK_LOCAL);
  }

  // TODO: Support re-configuring the test NAT someday?
  if (!nat_ && config_.mNatSimulatorConfig.isSome()) {
    TestNat* test_nat = new TestNat;
    test_nat->filtering_type_ = TestNat::ToNatBehavior(
        config_.mNatSimulatorConfig->mFilteringType.get());
    test_nat->mapping_type_ =
        TestNat::ToNatBehavior(config_.mNatSimulatorConfig->mMappingType.get());
    test_nat->block_udp_ = config_.mNatSimulatorConfig->mBlockUdp;
    test_nat->block_tcp_ = config_.mNatSimulatorConfig->mBlockTcp;
    test_nat->block_tls_ = config_.mNatSimulatorConfig->mBlockTls;
    test_nat->error_code_for_drop_ =
        config_.mNatSimulatorConfig->mErrorCodeForDrop;
    if (config_.mNatSimulatorConfig->mRedirectAddress.Length()) {
      test_nat
          ->stun_redirect_map_[config_.mNatSimulatorConfig->mRedirectAddress] =
          config_.mNatSimulatorConfig->mRedirectTargets;
    }
    test_nat->network_delay_ms_ = config_.mNatSimulatorConfig->mNetworkDelayMs;
    test_nat->enabled_ = true;
    SetNat(test_nat);
  }

  return NS_OK;
}

RefPtr<NrIceMediaStream> NrIceCtx::CreateStream(const std::string& id,
                                                const std::string& name,
                                                int components) {
  if (streams_.count(id)) {
    MOZ_ASSERT(false);
    return nullptr;
  }

  RefPtr stream = MakeRefPtr<NrIceMediaStream>(this, id, name, components);
  streams_[id] = stream;
  return stream;
}

void NrIceCtx::DestroyStream(const std::string& id) {
  auto it = streams_.find(id);
  if (it != streams_.end()) {
    auto preexisting_stream = it->second;
    SignalConnectionStateChange(preexisting_stream, ICE_CTX_CLOSED);
    streams_.erase(it);
    preexisting_stream->Close();
  }
}

// Handler callbacks
int NrIceCtx::select_pair(void* obj, nr_ice_media_stream* stream,
                          int component_id, nr_ice_cand_pair** potentials,
                          int potential_ct) {
  MOZ_MTLOG(ML_DEBUG, "select pair called: potential_ct = " << potential_ct);
  MOZ_ASSERT(stream->local_stream);
  MOZ_ASSERT(!stream->local_stream->obsolete);

  for (int i = 0; i < potential_ct; ++i) {
    nr_ice_cand_pair* pair = potentials[i];
    if (pair->state != NR_ICE_PAIR_STATE_SUCCEEDED) {
      continue;
    }

    if (pair->restart_nominated_cb_timer) {
      return 0;
    }

    return nr_ice_candidate_pair_nominate(pair, kRegularNominationDelayMs);
  }

  return 0;
}

int NrIceCtx::stream_ready(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_ready called");
  MOZ_ASSERT(!stream->local_stream);
  MOZ_ASSERT(!stream->obsolete);

  // Get the ICE ctx.
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);

  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  // Streams which do not exist should never be ready.
  MOZ_ASSERT(s);

  s->Ready(stream);
  ctx->SignalConnectionStateChange(s, ICE_CTX_CONNECTED);

  return 0;
}

int NrIceCtx::stream_failed(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_failed called");
  MOZ_ASSERT(!stream->local_stream);
  MOZ_ASSERT(!stream->obsolete);

  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  // Streams which do not exist should never fail.
  MOZ_ASSERT(s);

  if (!ctx->dumped_rlog_) {
    // Do this at most once per ctx
    ctx->dumped_rlog_ = true;
    MOZ_MTLOG(ML_INFO,
              "NrIceCtx(" << ctx->name_ << "): dumping r_log ringbuffer... ");
    std::deque<std::string> logs;
    RLogConnector::GetInstance()->GetAny(0, &logs);
    for (auto& log : logs) {
      MOZ_MTLOG(ML_INFO, log);
    }
  }

  s->Failed();
  ctx->SignalConnectionStateChange(s, ICE_CTX_FAILED);
  return 0;
}

int NrIceCtx::stream_checking(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_checking called");
  MOZ_ASSERT(!stream->local_stream);
  MOZ_ASSERT(!stream->obsolete);

  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  MOZ_ASSERT(s);

  if (!s->AnyGenerationIsConnected()) {
    // the checking state only applies if we aren't connected
    ctx->SignalConnectionStateChange(s, ICE_CTX_CHECKING);
  }
  return 0;
}

int NrIceCtx::stream_disconnected(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_disconnected called");
  MOZ_ASSERT(!stream->local_stream);
  MOZ_ASSERT(!stream->obsolete);

  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  MOZ_ASSERT(s);

  ctx->SignalConnectionStateChange(s, ICE_CTX_DISCONNECTED);
  return 0;
}

int NrIceCtx::stream_gathering(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_gathering called");
  MOZ_ASSERT(!stream->local_stream);
  MOZ_ASSERT(!stream->obsolete);

  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  MOZ_ASSERT(s);

  s->OnGatheringStarted(stream);
  return 0;
}

int NrIceCtx::stream_gathered(void* obj, nr_ice_media_stream* stream) {
  MOZ_MTLOG(ML_DEBUG, "stream_gathered called");
  MOZ_ASSERT(!stream->local_stream);

  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  // We get this callback for destroyed streams in some cases
  if (s) {
    s->OnGatheringComplete(stream);
  }
  return 0;
}

int NrIceCtx::candidate_error(void* obj, nr_ice_media_stream* stream,
                              nr_ice_candidate* candidate) {
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);
  if (!s) {
    return 0;
  }

  if (!candidate->stun_server) {
    return 0;
  }

  std::string address;
  uint16_t port = 0;
  if (!(ctx->ctx_->flags & NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES)) {
    nsCString host;
    if (!nr_transport_addr_get_addrstring_and_port(&candidate->base, &host,
                                                   &port)) {
      address = host.get();
    }
  }

  char* url = nullptr;
  int is_turn = (candidate->type == RELAYED);
  if (nr_ice_stun_server_get_url(candidate->stun_server, is_turn, &url)) {
    return 0;
  }

  // errorText is always empty; nICEr discards the STUN reason phrase after
  // processing the response. See bug 2018863.
  s->SignalCandidateError(s, address, port, url,
                          static_cast<uint16_t>(candidate->error_code), "");
  free(url);
  return 0;
}

int NrIceCtx::ice_checking(void* obj, nr_ice_peer_ctx* pctx) {
  MOZ_MTLOG(ML_DEBUG, "ice_checking called");
  // We don't use this; we react to the stream-specific callbacks instead
  return 0;
}

int NrIceCtx::ice_connected(void* obj, nr_ice_peer_ctx* pctx) {
  MOZ_MTLOG(ML_DEBUG, "ice_connected called");
  // We don't use this; we react to the stream-specific callbacks instead
  return 0;
}

int NrIceCtx::ice_disconnected(void* obj, nr_ice_peer_ctx* pctx) {
  MOZ_MTLOG(ML_DEBUG, "ice_disconnected called");
  // We don't use this; we react to the stream-specific callbacks instead
  return 0;
}

int NrIceCtx::msg_recvd(void* obj, nr_ice_peer_ctx* pctx,
                        nr_ice_media_stream* stream, int component_id,
                        UCHAR* msg, int len) {
  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(obj);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  // Streams which do not exist should never have packets.
  MOZ_ASSERT(s);

  s->SignalPacketReceived(s, component_id, msg, len);

  return 0;
}

void NrIceCtx::trickle_cb(void* arg, nr_ice_ctx* ice_ctx,
                          nr_ice_media_stream* stream, int component_id,
                          nr_ice_candidate* candidate) {
  if (stream->obsolete) {
    // Stream was probably just marked obsolete, resulting in this callback
    return;
  }
  // Get the ICE ctx
  NrIceCtx* ctx = static_cast<NrIceCtx*>(arg);
  RefPtr<NrIceMediaStream> s = ctx->FindStream(stream);

  if (!s) {
    // This stream has been removed because it is inactive
    return;
  }

  if (!candidate) {
    return;
  }

  std::string actual_addr;
  std::string mdns_addr;
  ctx->GenerateObfuscatedAddress(candidate, &mdns_addr, &actual_addr);

  // Format the candidate.
  char candidate_str[NR_ICE_MAX_ATTRIBUTE_SIZE];
  int r = nr_ice_format_candidate_attribute(
      candidate, candidate_str, sizeof(candidate_str),
      (ctx->ctx()->flags & NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES) ? 1 : 0);
  MOZ_ASSERT(!r);
  if (r) return;

  MOZ_MTLOG(ML_INFO, "NrIceCtx(" << ctx->name_ << "): trickling candidate "
                                 << candidate_str);

  s->SignalCandidate(s, candidate_str, stream->ufrag, mdns_addr, actual_addr);
}

void NrIceCtx::InitializeGlobals(const GlobalConfig& aConfig) {
  RLogConnector::CreateInstance();
  // Initialize the crypto callbacks and logging stuff
  if (!initialized) {
    NR_reg_init();
    // Registers the "stun" logger for r_log.
    (void)nr_stun_startup();
    nr_crypto_vtbl = &nr_ice_crypto_nss_vtbl;
    initialized = true;

    // Set the priorites for candidate type preferences.
    // These numbers come from RFC 5245 S. 4.1.2.2
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_SRV_RFLX, 100);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_PEER_RFLX, 110);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_HOST, 126);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_RELAYED, 5);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_SRV_RFLX_TCP, 99);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_PEER_RFLX_TCP, 109);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_HOST_TCP, 125);
    NR_reg_set_uchar((char*)NR_ICE_REG_PREF_TYPE_RELAYED_TCP, 0);
    NR_reg_set_uint4((char*)"stun.client.maximum_transmits",
                     aConfig.mStunClientMaxTransmits);
    NR_reg_set_uint4((char*)NR_ICE_REG_TRICKLE_GRACE_PERIOD,
                     aConfig.mTrickleIceGracePeriod);
    NR_reg_set_int4((char*)NR_ICE_REG_ICE_TCP_SO_SOCK_COUNT,
                    aConfig.mIceTcpSoSockCount);
    NR_reg_set_int4((char*)NR_ICE_REG_ICE_TCP_LISTEN_BACKLOG,
                    aConfig.mIceTcpListenBacklog);

    NR_reg_set_char((char*)NR_ICE_REG_ICE_TCP_DISABLE, !aConfig.mTcpEnabled);

    if (!aConfig.mForceNetInterface.Length()) {
      NR_reg_set_string((char*)NR_ICE_REG_PREF_FORCE_INTERFACE_NAME,
                        const_cast<char*>(aConfig.mForceNetInterface.get()));
    }

    // For now, always use nr_resolver for UDP.
    NR_reg_set_char((char*)NR_ICE_REG_USE_NR_RESOLVER_FOR_UDP, 1);

    // Use nr_resolver for TCP only when not in e10s mode (for unit-tests)
    if (XRE_IsParentProcess()) {
      NR_reg_set_char((char*)NR_ICE_REG_USE_NR_RESOLVER_FOR_TCP, 1);
    }
  }
}

void NrIceCtx::SetTargetForDefaultLocalAddressLookup(
    const std::string& target_ip, uint16_t target_port) {
  nr_ice_set_target_for_default_local_address_lookup(ctx_, target_ip.c_str(),
                                                     target_port);
}

#define MAXADDRS 100  // mirrors setting in ice_ctx.c

/* static */
nsTArray<NrIceStunAddr> NrIceCtx::GetStunAddrs() {
  nsTArray<NrIceStunAddr> addrs;

  nr_local_addr local_addrs[MAXADDRS];
  int addr_ct = 0;

  // most likely running on parent process and need crypto vtbl
  // initialized on Windows (Linux and OSX don't seem to care)
  if (!initialized) {
    nr_crypto_vtbl = &nr_ice_crypto_nss_vtbl;
  }

  MOZ_MTLOG(ML_INFO, "NrIceCtx static call to find local stun addresses");
  if (nr_stun_get_addrs(local_addrs, MAXADDRS, &addr_ct)) {
    MOZ_MTLOG(ML_INFO, "Error finding local stun addresses");
  } else {
    for (int i = 0; i < addr_ct; ++i) {
      NrIceStunAddr addr(&local_addrs[i]);
      addrs.AppendElement(addr);
    }
  }

  return addrs;
}

void NrIceCtx::SetStunAddrs(const nsTArray<NrIceStunAddr>& addrs) {
  nr_local_addr* local_addrs;
  local_addrs = new nr_local_addr[addrs.Length()];

  for (size_t i = 0; i < addrs.Length(); ++i) {
    addrs[i].toNrLocalAddr(local_addrs[i]);
  }
  nr_ice_set_local_addresses(ctx_, local_addrs, addrs.Length());

  delete[] local_addrs;
}

bool NrIceCtx::Initialize() {
  // Create the gather handler objects
  ice_gather_handler_vtbl_ = new nr_ice_gather_handler_vtbl();
  ice_gather_handler_vtbl_->stream_gathering = &NrIceCtx::stream_gathering;
  ice_gather_handler_vtbl_->stream_gathered = &NrIceCtx::stream_gathered;
  ice_gather_handler_vtbl_->candidate_error = &NrIceCtx::candidate_error;
  ice_gather_handler_ = new nr_ice_gather_handler();
  ice_gather_handler_->vtbl = ice_gather_handler_vtbl_;
  ice_gather_handler_->obj = this;

  // Create the ICE context
  int r;

  UINT4 flags = NR_ICE_CTX_FLAGS_AGGRESSIVE_NOMINATION;

  r = nr_ice_ctx_create(const_cast<char*>(name_.c_str()), flags,
                        ice_gather_handler_, &ctx_);

  if (r) {
    MOZ_MTLOG(ML_ERROR, "Couldn't create ICE ctx for '" << name_ << "'");
    return false;
  }

  // override default factory to capture optional proxy config when creating
  // sockets.
  nr_socket_factory* factory;
  r = nr_socket_factory_create_int(this, &ctx_socket_factory_vtbl, &factory);

  if (r) {
    MOZ_MTLOG(LogLevel::Error, "Couldn't create ctx socket factory.");
    return false;
  }
  nr_ice_ctx_set_socket_factory(ctx_, factory);

  nr_interface_prioritizer* prioritizer = CreateInterfacePrioritizer();
  if (!prioritizer) {
    MOZ_MTLOG(LogLevel::Error, "Couldn't create interface prioritizer.");
    return false;
  }

  r = nr_ice_ctx_set_interface_prioritizer(ctx_, prioritizer);
  if (r) {
    MOZ_MTLOG(LogLevel::Error, "Couldn't set interface prioritizer.");
    return false;
  }

  if (generating_trickle()) {
    r = nr_ice_ctx_set_trickle_cb(ctx_, &NrIceCtx::trickle_cb, this);
    if (r) {
      MOZ_MTLOG(ML_ERROR, "Couldn't set trickle cb for '" << name_ << "'");
      return false;
    }
  }

  // Create the handler objects
  ice_handler_vtbl_ = new nr_ice_handler_vtbl();
  ice_handler_vtbl_->select_pair = &NrIceCtx::select_pair;
  ice_handler_vtbl_->stream_ready = &NrIceCtx::stream_ready;
  ice_handler_vtbl_->stream_failed = &NrIceCtx::stream_failed;
  ice_handler_vtbl_->stream_checking = &NrIceCtx::stream_checking;
  ice_handler_vtbl_->stream_disconnected = &NrIceCtx::stream_disconnected;
  // stream_gathering and stream_gathered do not go here, since those are tied
  // to the _local_ nr_ice_media_stream in nICEr. nICEr allows a local
  // nr_ice_media_stream (which has a single set of candidates, and therefore a
  // single gathering state) to be associated with multiple remote
  // nr_ice_media_streams (which each have their own ICE connection state)
  // because it allows forking. We never encounter forking, so these will be
  // one-to-one in practice, but the architecture in nICEr means we have to set
  // up these callbacks on the nr_ice_ctx, not the nr_ice_peer_ctx.
  ice_handler_vtbl_->ice_connected = &NrIceCtx::ice_connected;
  ice_handler_vtbl_->msg_recvd = &NrIceCtx::msg_recvd;
  ice_handler_vtbl_->ice_checking = &NrIceCtx::ice_checking;
  ice_handler_vtbl_->ice_disconnected = &NrIceCtx::ice_disconnected;

  ice_handler_ = new nr_ice_handler();
  ice_handler_->vtbl = ice_handler_vtbl_;
  ice_handler_->obj = this;

  // Create the peer ctx. Because we do not support parallel forking, we
  // only have one peer ctx.
  std::string peer_name = name_ + ":default";
  r = nr_ice_peer_ctx_create(ctx_, ice_handler_,
                             const_cast<char*>(peer_name.c_str()), &peer_);
  if (r) {
    MOZ_MTLOG(ML_ERROR, "Couldn't create ICE peer ctx for '" << name_ << "'");
    return false;
  }

  nsresult rv;
  sts_target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);

  if (!NS_SUCCEEDED(rv)) return false;

  return true;
}

int NrIceCtx::SetNat(const RefPtr<TestNat>& aNat) {
  nat_ = aNat;
  nr_socket_factory* fac;
  int r = nat_->create_socket_factory(&fac);
  if (r) {
    return r;
  }
  nr_ice_ctx_set_socket_factory(ctx_, fac);
  return 0;
}

// ONLY USE THIS FOR TESTING. Will cause totally unpredictable and possibly very
// bad effects if ICE is still live.
void NrIceCtx::internal_DeinitializeGlobal() {
  NR_reg_del((char*)"stun");
  NR_reg_del((char*)"ice");
  RLogConnector::DestroyInstance();
  nr_crypto_vtbl = nullptr;
  initialized = false;
}

void NrIceCtx::internal_SetTimerAccelarator(int divider) {
  ctx_->test_timer_divider = divider;
}

void NrIceCtx::AccumulateStats(const NrIceStats& stats) {
  nr_accumulate_count(&(ctx_->stats.stun_retransmits), stats.stun_retransmits);
  nr_accumulate_count(&(ctx_->stats.turn_401s), stats.turn_401s);
  nr_accumulate_count(&(ctx_->stats.turn_403s), stats.turn_403s);
  nr_accumulate_count(&(ctx_->stats.turn_438s), stats.turn_438s);
}

NrIceStats NrIceCtx::Destroy() {
  // designed to be called more than once so if stats are desired, this can be
  // called just prior to the destructor
  MOZ_MTLOG(ML_NOTICE, "NrIceCtx(" << name_ << "): " << __func__);

  for (auto& idAndStream : streams_) {
    idAndStream.second->Close();
  }

  NrIceStats stats;
  if (ctx_) {
    stats.stun_retransmits = ctx_->stats.stun_retransmits;
    stats.turn_401s = ctx_->stats.turn_401s;
    stats.turn_403s = ctx_->stats.turn_403s;
    stats.turn_438s = ctx_->stats.turn_438s;
  }

  if (peer_) {
    nr_ice_peer_ctx_destroy(&peer_);
  }
  if (ctx_) {
    nr_ice_ctx_destroy(&ctx_);
  }

  delete ice_handler_vtbl_;
  delete ice_handler_;

  delete ice_gather_handler_vtbl_;
  delete ice_gather_handler_;

  ice_handler_vtbl_ = nullptr;
  ice_handler_ = nullptr;
  ice_gather_handler_vtbl_ = nullptr;
  ice_gather_handler_ = nullptr;
  proxy_config_ = nullptr;
  streams_.clear();

  return stats;
}

NrIceCtx::~NrIceCtx() = default;

void NrIceCtx::destroy_peer_ctx() { nr_ice_peer_ctx_destroy(&peer_); }

nsresult NrIceCtx::SetControlling(Controlling controlling) {
  if (!ice_controlling_set_) {
    peer_->controlling = (controlling == ICE_CONTROLLING) ? 1 : 0;
    ice_controlling_set_ = true;

    MOZ_MTLOG(ML_DEBUG,
              "ICE ctx " << name_ << " setting controlling to" << controlling);
  }
  return NS_OK;
}

NrIceCtx::Controlling NrIceCtx::GetControlling() {
  return (peer_->controlling) ? ICE_CONTROLLING : ICE_CONTROLLED;
}

nsresult NrIceCtx::SetIceServers(const nsTArray<ParsedIceServer>& aServers,
                                 bool aTurnDisabled) {
  MOZ_MTLOG(ML_NOTICE, "NrIceCtx(" << name_ << "): " << __func__);

  std::vector<nr_ice_stun_server> stunServers;
  std::vector<nr_ice_turn_server> turnServers;

  for (const auto& entry : aServers) {
    bool isTurn = entry.mUri.IsTurn();

    if (isTurn && aTurnDisabled) {
      continue;
    }

    // Test whether this uses an IP addr, or is an FQDN.
    // We could use PR_StringToNetAddr instead, but that pulls in an extra
    // dependency.
    nr_transport_addr parsed;
    bool isFqdn = false;
    if (!nr_str_port_to_transport_addr(entry.mUri.mHost.get(), entry.mUri.mPort,
                                       IPPROTO_UDP, &parsed)) {
      isFqdn = parsed.fqdn[0] != '\0';
    }

    if (isFqdn) {
      // Not a parseable IP address -- treat as FQDN.
      // Create both IPv4 and IPv6 versions with a placeholder address
      if (isTurn) {
        nr_ice_turn_server turnServer;
        if (ToNicerTurnStruct("0.0.0.0", entry, &turnServer)) {
          turnServers.push_back(turnServer);
        }
        if (ToNicerTurnStruct("::", entry, &turnServer)) {
          turnServers.push_back(turnServer);
        }
      } else {
        nr_ice_stun_server stunServer;
        if (ToNicerStunStruct("0.0.0.0", entry, &stunServer)) {
          stunServers.push_back(stunServer);
        }
        if (ToNicerStunStruct("::", entry, &stunServer)) {
          stunServers.push_back(stunServer);
        }
      }
    } else {
      // Parsed as IP address.
      if (isTurn) {
        nr_ice_turn_server turnServer;
        if (ToNicerTurnStruct(nullptr, entry, &turnServer)) {
          turnServers.push_back(turnServer);
        }
      } else {
        nr_ice_stun_server stunServer;
        if (ToNicerStunStruct(nullptr, entry, &stunServer)) {
          stunServers.push_back(stunServer);
        }
      }
    }
  }

  int r = nr_ice_ctx_set_stun_servers(ctx_, stunServers.data(),
                                      static_cast<int>(stunServers.size()));
  if (!r) {
    r = nr_ice_ctx_set_turn_servers(ctx_, turnServers.data(),
                                    static_cast<int>(turnServers.size()));
    if (!r) {
      // nICEr has taken ownership of the username/credential in these.
      turnServers.clear();
    } else {
      MOZ_MTLOG(ML_ERROR, "Couldn't set TURN servers for '" << name_ << "'");
    }
  } else {
    MOZ_MTLOG(ML_ERROR, "Couldn't set STUN servers for '" << name_ << "'");
  }

  for (auto& turn : turnServers) {
    free(turn.username);
    r_data_destroy(&turn.password);
  }

  return r ? NS_ERROR_FAILURE : NS_OK;
}

nsresult NrIceCtx::SetResolver(nr_resolver* resolver) {
  int r = nr_ice_ctx_set_resolver(ctx_, resolver);

  if (r) {
    MOZ_MTLOG(ML_ERROR, "Couldn't set resolver for '" << name_ << "'");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult NrIceCtx::SetProxyConfig(NrSocketProxyConfig&& config) {
  proxy_config_.reset(new NrSocketProxyConfig(std::move(config)));
  if (nat_) {
    nat_->set_proxy_config(proxy_config_);
  }

  if (proxy_config_->GetForceProxy()) {
    nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_ONLY_PROXY);
  } else {
    nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_ONLY_PROXY);
  }

  return NS_OK;
}

void NrIceCtx::SetCtxFlags(bool default_route_only) {
  ASSERT_ON_THREAD(sts_target_);

  if (default_route_only) {
    nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_ONLY_DEFAULT_ADDRS);
  } else {
    nr_ice_ctx_remove_flags(ctx_, NR_ICE_CTX_FLAGS_ONLY_DEFAULT_ADDRS);
  }
}

nsresult NrIceCtx::StartGathering(bool default_route_only,
                                  bool obfuscate_host_addresses) {
  ASSERT_ON_THREAD(sts_target_);
  MOZ_MTLOG(ML_NOTICE, "NrIceCtx(" << name_ << "): " << __func__);

  if (obfuscate_host_addresses) {
    nr_ice_ctx_add_flags(ctx_, NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES);
  }

  SetCtxFlags(default_route_only);

  // This might start gathering for the first time, or again after
  // renegotiation, or might do nothing at all if gathering has already
  // finished.
  int r = nr_ice_gather(ctx_, &NrIceCtx::gather_cb, this);

  if (r && r != R_WOULDBLOCK) {
    MOZ_MTLOG(ML_ERROR, "ICE FAILED: Couldn't gather ICE candidates for '"
                            << name_ << "', error=" << r);
    SignalAllStreamsFailed();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

RefPtr<NrIceMediaStream> NrIceCtx::FindStream(nr_ice_media_stream* stream) {
  for (auto& idAndStream : streams_) {
    if (idAndStream.second->HasStream(stream)) {
      return idAndStream.second;
    }
  }

  return nullptr;
}

std::vector<std::string> NrIceCtx::GetGlobalAttributes() {
  char** attrs = nullptr;
  int attrct;
  int r;
  std::vector<std::string> ret;

  r = nr_ice_get_global_attributes(ctx_, &attrs, &attrct);
  if (r) {
    MOZ_MTLOG(ML_ERROR,
              "Couldn't get ufrag and password for '" << name_ << "'");
    return ret;
  }

  for (int i = 0; i < attrct; i++) {
    ret.push_back(std::string(attrs[i]));
    free(attrs[i]);
  }
  free(attrs);

  return ret;
}

nsresult NrIceCtx::ParseGlobalAttributes(std::vector<std::string> attrs) {
  std::vector<char*> attrs_in;
  attrs_in.reserve(attrs.size());
  for (auto& attr : attrs) {
    attrs_in.push_back(const_cast<char*>(attr.c_str()));
  }

  int r = nr_ice_peer_ctx_parse_global_attributes(
      peer_, attrs_in.empty() ? nullptr : &attrs_in[0], attrs_in.size());
  if (r) {
    MOZ_MTLOG(ML_ERROR,
              "Couldn't parse global attributes for " << name_ << "'");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool NrIceCtx::HasStreamsToConnect() const {
  for (auto& idAndStream : streams_) {
    if (idAndStream.second->state() != NrIceMediaStream::ICE_CLOSED) {
      return true;
    }
  }
  return false;
}

nsresult NrIceCtx::StartChecks() {
  int r;
  MOZ_MTLOG(ML_NOTICE, "NrIceCtx(" << name_ << "): " << __func__);

  if (!HasStreamsToConnect()) {
    MOZ_MTLOG(ML_NOTICE, "In StartChecks, nothing to do on " << name_);
    return NS_OK;
  }

  r = nr_ice_peer_ctx_pair_candidates(peer_);
  if (r) {
    MOZ_MTLOG(ML_ERROR, "ICE FAILED: Couldn't pair candidates on " << name_);
    SignalAllStreamsFailed();
    return NS_ERROR_FAILURE;
  }

  r = nr_ice_peer_ctx_start_checks2(peer_, 1);
  if (r) {
    if (r == R_NOT_FOUND) {
      MOZ_MTLOG(ML_INFO, "Couldn't start peer checks on "
                             << name_ << ", assuming trickle ICE");
    } else {
      MOZ_MTLOG(ML_ERROR,
                "ICE FAILED: Couldn't start peer checks on " << name_);
      SignalAllStreamsFailed();
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}

void NrIceCtx::gather_cb(NR_SOCKET s, int h, void* arg) {
  MOZ_MTLOG(ML_DEBUG, "gather_cb called");
  // We don't use this; we react to the stream-specific callbacks instead
}

void NrIceCtx::SignalAllStreamsFailed() {
  for (auto& [id, stream] : streams_) {
    (void)id;
    stream->Failed();
    SignalConnectionStateChange(stream, ICE_CTX_FAILED);
  }
}

void NrIceCtx::UpdateNetworkState(bool online) {
  MOZ_MTLOG(ML_NOTICE, "NrIceCtx(" << name_ << "): updating network state to "
                                   << (online ? "online" : "offline"));
  if (online) {
    nr_ice_peer_ctx_refresh_consent_all_streams(peer_);
  } else {
    nr_ice_peer_ctx_disconnect_all_streams(peer_);
  }
}

void NrIceCtx::GenerateObfuscatedAddress(nr_ice_candidate* candidate,
                                         std::string* mdns_address,
                                         std::string* actual_address) {
  if (candidate->type == HOST &&
      (ctx_->flags & NR_ICE_CTX_FLAGS_OBFUSCATE_HOST_ADDRESSES)) {
    char addr[64];
    if (nr_transport_addr_get_addrstring(&candidate->addr, addr,
                                         sizeof(addr))) {
      return;
    }

    *actual_address = addr;

    const auto& iter = obfuscated_host_addresses_.find(*actual_address);
    if (iter != obfuscated_host_addresses_.end()) {
      *mdns_address = iter->second;
    } else {
      nsresult rv;
      nsCOMPtr<nsIUUIDGenerator> uuidgen =
          do_GetService("@mozilla.org/uuid-generator;1", &rv);
      // If this fails, we'll return a zero UUID rather than something
      // unexpected.
      nsID id = {};
      id.Clear();
      if (NS_SUCCEEDED(rv)) {
        rv = uuidgen->GenerateUUIDInPlace(&id);
        if (NS_FAILED(rv)) {
          id.Clear();
        }
      }

      char chars[NSID_LENGTH];
      id.ToProvidedString(chars);
      // The string will look like {64888863-a253-424a-9b30-1ed285d20142},
      // we want to trim off the braces.
      const char* ptr_to_id = chars;
      ++ptr_to_id;
      chars[NSID_LENGTH - 2] = 0;

      std::ostringstream o;
      o << ptr_to_id << ".local";
      *mdns_address = o.str();

      obfuscated_host_addresses_[*actual_address] = *mdns_address;
    }
    candidate->mdns_addr = strdup(mdns_address->c_str());
  }
}

}  // namespace mozilla

// Reimplement nr_ice_compute_codeword to avoid copyright issues
void nr_ice_compute_codeword(char* buf, int len, char* codeword) {
  UINT4 c;

  r_crc32(buf, len, &c);
  [[maybe_unused]] nsresult nr = mozilla::Base64Encode(
      reinterpret_cast<char*>(&c), 3, mozilla::Span(codeword, 5));
  MOZ_ASSERT(NS_SUCCEEDED(nr));
}

int nr_socket_local_create(void* obj, nr_transport_addr* addr,
                           nr_socket** sockp) {
  using namespace mozilla;

  RefPtr<NrSocketBase> sock;
  int r, _status;
  shared_ptr<NrSocketProxyConfig> config = nullptr;

  if (obj) {
    config = static_cast<NrIceCtx*>(obj)->GetProxyConfig();
  }

  r = NrSocketBase::CreateSocket(addr, &sock, config);
  if (r) {
    ABORT(r);
  }

  r = nr_socket_create_int(static_cast<void*>(sock), sock->vtbl(), sockp);
  if (r) ABORT(r);

  _status = 0;

  {
    // We will release this reference in destroy(), not exactly the normal
    // ownership model, but it is what it is.
    NrSocketBase* dummy = sock.forget().take();
    (void)dummy;
  }

abort:
  return _status;
}
