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

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// This is a wrapper around the nICEr ICE stack
#ifndef nricectx_h_
#define nricectx_h_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "m_cpp_utils.h"
#include "mozilla/IceServerParser.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "nricemediastream.h"
#include "nricestunaddr.h"
#include "nsIEventTarget.h"
#include "nsTArray.h"
#include "sigslot.h"

typedef struct nr_ice_ctx_ nr_ice_ctx;
typedef struct nr_ice_peer_ctx_ nr_ice_peer_ctx;
typedef struct nr_ice_media_stream_ nr_ice_media_stream;
typedef struct nr_ice_handler_ nr_ice_handler;
typedef struct nr_ice_handler_vtbl_ nr_ice_handler_vtbl;
typedef struct nr_ice_gather_handler_ nr_ice_gather_handler;
typedef struct nr_ice_gather_handler_vtbl_ nr_ice_gather_handler_vtbl;
typedef struct nr_ice_candidate_ nr_ice_candidate;
typedef struct nr_ice_cand_pair_ nr_ice_cand_pair;
typedef struct nr_ice_stun_server_ nr_ice_stun_server;
typedef struct nr_ice_turn_server_ nr_ice_turn_server;
typedef struct nr_resolver_ nr_resolver;
typedef struct nr_proxy_tunnel_config_ nr_proxy_tunnel_config;

typedef void* NR_SOCKET;

namespace mozilla {

class NrSocketProxyConfig;

class NrIceMediaStream;

extern const char kNrIceTransportUdp[];
extern const char kNrIceTransportTcp[];
extern const char kNrIceTransportTls[];

class TestNat;

class NrIceStats {
 public:
  uint16_t stun_retransmits = 0;
  uint16_t turn_401s = 0;
  uint16_t turn_403s = 0;
  uint16_t turn_438s = 0;
};

class NrIceCtx {
 public:
  enum ConnectionState {
    ICE_CTX_INIT,
    ICE_CTX_CHECKING,
    ICE_CTX_CONNECTED,
    ICE_CTX_COMPLETED,
    ICE_CTX_FAILED,
    ICE_CTX_DISCONNECTED,
    ICE_CTX_CLOSED
  };

  enum Controlling { ICE_CONTROLLING, ICE_CONTROLLED };

  enum Policy { ICE_POLICY_RELAY, ICE_POLICY_NO_HOST, ICE_POLICY_ALL };

  struct NatSimulatorConfig {
    bool mBlockTcp = false;
    bool mBlockUdp = false;
    bool mBlockTls = false;
    int mErrorCodeForDrop = 0;
    nsCString mMappingType = "ENDPOINT_INDEPENDENT"_ns;
    nsCString mFilteringType = "ENDPOINT_INDEPENDENT"_ns;
    nsCString mRedirectAddress;
    CopyableTArray<nsCString> mRedirectTargets;
    unsigned int mNetworkDelayMs = 0;
  };

  struct Config {
    NrIceCtx::Policy mPolicy = NrIceCtx::ICE_POLICY_ALL;
    bool mAllowLinkLocal = false;
    bool mAllowLoopback = false;
    Maybe<NatSimulatorConfig> mNatSimulatorConfig;
  };

  static RefPtr<NrIceCtx> Create(const std::string& aName);

  nsresult SetIceConfig(const Config& aConfig);

  RefPtr<NrIceMediaStream> CreateStream(const std::string& id,
                                        const std::string& name,
                                        int components);
  void DestroyStream(const std::string& id);
  // The flags gathering depends on, which are known late because they depend
  // on what capture permissions the window has been granted.
  void SetGatheringFlags(bool default_route_only,
                         bool obfuscate_host_addresses);

  // Make the pool hold aSize pre-gathered single-component candidate sets. The
  // components of the streams created afterwards take these instead of
  // gathering for themselves, and raise the candidates as their own. The
  // credentials are placeholders; a pooled component is re-keyed to the stream
  // that takes it.
  // Sets already in the pool are kept: growing gathers only the sets being
  // added, and shrinking releases only the sets beyond aSize. Callers that
  // need the pool re-gathered from scratch (because the ICE servers changed,
  // say) empty it first.
  nsresult ResizeCandidatePool(uint8_t aSize, const std::string& aUfrag,
                               const std::string& aPwd);
  // Release whatever is left of the pool, closing its sockets.
  void EmptyCandidatePool();
  // How many pre-gathered candidate sets are still available. Only used by
  // tests.
  uint8_t CandidatePoolSize() const;

  struct GlobalConfig {
    bool mTcpEnabled = true;
    int mStunClientMaxTransmits = 7;
    int mTrickleIceGracePeriod = 5000;
    int mIceTcpSoSockCount = 3;
    int mIceTcpListenBacklog = 10;
    nsCString mForceNetInterface;
  };

  // initialize ICE globals, crypto, and logging
  static void InitializeGlobals(const GlobalConfig& aConfig);

  void SetTargetForDefaultLocalAddressLookup(const std::string& target_ip,
                                             uint16_t target_port);

  // static GetStunAddrs for use in parent process to support
  // sandboxing restrictions
  static nsTArray<NrIceStunAddr> GetStunAddrs();
  void SetStunAddrs(const nsTArray<NrIceStunAddr>& addrs);

  bool Initialize();

  int SetNat(const RefPtr<TestNat>& aNat);

  // Deinitialize all ICE global state. Used only for testing.
  static void internal_DeinitializeGlobal();

  // Divide some timers to faster testing. Used only for testing.
  void internal_SetTimerAccelarator(int divider);

  nr_ice_ctx* ctx() { return ctx_; }
  nr_ice_peer_ctx* peer() { return peer_; }

  // Testing only.
  void destroy_peer_ctx();

  RefPtr<NrIceMediaStream> GetStream(const std::string& id) {
    auto it = streams_.find(id);
    if (it != streams_.end()) {
      return it->second;
    }
    return nullptr;
  }

  std::vector<RefPtr<NrIceMediaStream>> GetStreams() const {
    std::vector<RefPtr<NrIceMediaStream>> result;
    result.reserve(streams_.size());
    for (auto& idAndStream : streams_) {
      result.push_back(idAndStream.second);
    }
    return result;
  }

  bool HasStreamsToConnect() const;

  // The name of the ctx
  const std::string& name() const { return name_; }

  // Get the global attributes
  std::vector<std::string> GetGlobalAttributes();

  // Set the other side's global attributes
  nsresult ParseGlobalAttributes(std::vector<std::string> attrs);

  // Set whether we are controlling or not.
  nsresult SetControlling(Controlling controlling);

  Controlling GetControlling();

  using ParsedIceServer = IceServerParser::ParsedIceServer;
  // Set ICE servers from ParsedIceServer structs. Converts directly to nICEr
  // C structs, with FQDN fanout (IPv4 + IPv6 entries for hostnames).
  // Must be called before StartGathering.
  nsresult SetIceServers(const nsTArray<ParsedIceServer>& aServers,
                         bool aTurnDisabled);

  // Provide the resolution provider. Must be called before
  // StartGathering.
  nsresult SetResolver(nr_resolver* resolver);

  // Provide the proxy address. Must be called before
  // StartGathering.
  nsresult SetProxyConfig(NrSocketProxyConfig&& config);

  const std::shared_ptr<NrSocketProxyConfig>& GetProxyConfig() {
    return proxy_config_;
  }

  void SetCtxFlags(bool default_route_only);

  // Start ICE gathering
  nsresult StartGathering(bool default_route_only,
                          bool obfuscate_host_addresses);

  // Start checking
  nsresult StartChecks();

  // Notify that the network has gone online/offline
  void UpdateNetworkState(bool online);

  void AccumulateStats(const NrIceStats& stats);
  NrIceStats Destroy();

  // Are we trickling?
  bool generating_trickle() const { return trickle_; }

  // Signals to indicate events. API users can (and should)
  // register for these.
  sigslot::signal2<NrIceMediaStream*, NrIceCtx::ConnectionState>
      SignalConnectionStateChange;

  // The thread to direct method calls to
  nsCOMPtr<nsIEventTarget> thread() { return sts_target_; }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NrIceCtx)

 private:
  explicit NrIceCtx(const std::string& name);

  virtual ~NrIceCtx();

  DISALLOW_COPY_ASSIGN(NrIceCtx);

  // Callbacks for nICEr
  static void gather_cb(NR_SOCKET s, int h, void* arg);  // ICE gather complete

  // Handler implementation
  static int select_pair(void* obj, nr_ice_media_stream* stream,
                         int component_id, nr_ice_cand_pair** potentials,
                         int potential_ct);
  static int stream_gathering(void* obj, nr_ice_media_stream* stream);
  static int stream_gathered(void* obj, nr_ice_media_stream* stream);
  static int candidate_error(void* obj, nr_ice_media_stream* stream,
                             nr_ice_candidate* candidate);
  static int stream_checking(void* obj, nr_ice_media_stream* stream);
  static int stream_ready(void* obj, nr_ice_media_stream* stream);
  static int stream_disconnected(void* obj, nr_ice_media_stream* stream);
  static int stream_failed(void* obj, nr_ice_media_stream* stream);
  static int ice_checking(void* obj, nr_ice_peer_ctx* pctx);
  static int ice_connected(void* obj, nr_ice_peer_ctx* pctx);
  static int ice_disconnected(void* obj, nr_ice_peer_ctx* pctx);
  static int msg_recvd(void* obj, nr_ice_peer_ctx* pctx,
                       nr_ice_media_stream* stream, int component_id,
                       unsigned char* msg, int len);
  static void trickle_cb(void* arg, nr_ice_ctx* ctx,
                         nr_ice_media_stream* stream, int component_id,
                         nr_ice_candidate* candidate);

  void SignalAllStreamsFailed();

  // Find a media stream by stream ptr. Gross
  RefPtr<NrIceMediaStream> FindStream(nr_ice_media_stream* stream);

  void GenerateObfuscatedAddress(nr_ice_candidate* candidate,
                                 std::string* mdns_address,
                                 std::string* actual_address);

  bool dumped_rlog_ = false;
  const std::string name_;
  bool ice_controlling_set_;
  std::map<std::string, RefPtr<NrIceMediaStream>> streams_;
  nr_ice_ctx* ctx_;
  nr_ice_peer_ctx* peer_;
  nr_ice_handler_vtbl* ice_handler_vtbl_;                // Must be pointer
  nr_ice_handler* ice_handler_;                          // Must be pointer
  nr_ice_gather_handler_vtbl* ice_gather_handler_vtbl_;  // Must be pointer
  nr_ice_gather_handler* ice_gather_handler_;            // Must be pointer
  bool trickle_;
  nsCOMPtr<nsIEventTarget> sts_target_;  // The thread to run on
  Config config_;
  RefPtr<TestNat> nat_;
  std::shared_ptr<NrSocketProxyConfig> proxy_config_;
  std::map<std::string, std::string> obfuscated_host_addresses_;
};

}  // namespace mozilla
#endif
