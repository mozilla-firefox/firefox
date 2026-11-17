/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(__Userspace_os_Windows)
#  include <arpa/inet.h>
#endif
// usrsctp.h expects to have errno definitions prior to its inclusion.
#include <errno.h>

#include <memory>

#define SCTP_DEBUG 1
#define SCTP_STDINT_INCLUDE <stdint.h>

#ifdef _MSC_VER
// Disable "warning C4200: nonstandard extension used : zero-sized array in
//          struct/union"
// ...which the third-party file usrsctp.h runs afoul of.
#  pragma warning(push)
#  pragma warning(disable : 4200)
#endif

#include "usrsctp.h"

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "mozilla/media/MediaUtils.h"
#ifdef MOZ_PEERCONNECTION
#  include "transport/runnable_utils.h"
#endif

#include "DataChannelLog.h"
#include "DataChannelUsrsctp.h"

namespace mozilla {

static LazyLogModule gSCTPLog("usrsctp");

#define SCTP_LOG(args) \
  MOZ_LOG(mozilla::gSCTPLog, mozilla::LogLevel::Debug, args)

static void debug_printf(const char* format, ...) {
  va_list ap;
  char buffer[1024];

  if (MOZ_LOG_TEST(gSCTPLog, LogLevel::Debug)) {
    va_start(ap, format);
#ifdef _WIN32
    if (vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, ap) > 0) {
#else
    if (VsprintfLiteral(buffer, format, ap) > 0) {
#endif
      SCTP_LOG(("%s", buffer));
    }
    va_end(ap);
  }
}

class DataChannelRegistry {
 public:
  static uintptr_t Register(DataChannelConnectionUsrsctp* aConnection) {
    StaticMutexAutoLock lock(sInstanceMutex);
    uintptr_t result = EnsureInstance()->RegisterImpl(aConnection);
    DC_DEBUG(
        ("Registering connection %p as ulp %p", aConnection, (void*)result));
    return result;
  }

  static void Deregister(uintptr_t aId) {
    std::unique_ptr<DataChannelRegistry> maybeTrash;

    {
      StaticMutexAutoLock lock(sInstanceMutex);
      DC_DEBUG(("Deregistering connection ulp = %p", (void*)aId));
      if (NS_WARN_IF(!Instance())) {
        return;
      }
      Instance()->DeregisterImpl(aId);
      if (Instance()->Empty()) {
        // Unset singleton inside mutex lock, but don't call Shutdown until we
        // unlock, since that involves calling into libusrsctp, which invites
        // deadlock.
        maybeTrash = std::move(Instance());
      }
    }
  }

  static RefPtr<DataChannelConnectionUsrsctp> Lookup(uintptr_t aId) {
    StaticMutexAutoLock lock(sInstanceMutex);
    if (NS_WARN_IF(!Instance())) {
      return nullptr;
    }
    return Instance()->LookupImpl(aId);
  }

  // The singleton pointer has already been cleared before the destructor runs,
  // so no other thread can reach this object. Skip lock analysis.
  virtual ~DataChannelRegistry() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

    if (NS_WARN_IF(!mConnections.empty())) {
      MOZ_DIAGNOSTIC_CRASH("mConnections not empty");
      mConnections.clear();
    }

    MOZ_DIAGNOSTIC_ASSERT(!Instance());
    DeinitUsrSctp();
  }

 private:
  // This is a singleton class, so don't let just anyone create one of these
  DataChannelRegistry() {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    mShutdownBlocker = media::ShutdownBlockingTicket::Create(
        u"DataChannelRegistry::mShutdownBlocker"_ns,
        NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__);
    MOZ_DIAGNOSTIC_ASSERT(!Instance());
    InitUsrSctp();
  }

  static std::unique_ptr<DataChannelRegistry>& Instance() {
    static std::unique_ptr<DataChannelRegistry> sRegistry;
    return sRegistry;
  }

  static std::unique_ptr<DataChannelRegistry>& EnsureInstance() {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    if (!Instance()) {
      Instance().reset(new DataChannelRegistry());
    }
    return Instance();
  }

  uintptr_t RegisterImpl(DataChannelConnectionUsrsctp* aConnection)
      MOZ_REQUIRES(sInstanceMutex) {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    mConnections.emplace(mNextId, aConnection);
    return mNextId++;
  }

  void DeregisterImpl(uintptr_t aId) MOZ_REQUIRES(sInstanceMutex) {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    size_t removed = mConnections.erase(aId);
    (void)removed;
    MOZ_DIAGNOSTIC_ASSERT(removed);
  }

  bool Empty() const MOZ_REQUIRES(sInstanceMutex) {
    return mConnections.empty();
  }

  RefPtr<DataChannelConnectionUsrsctp> LookupImpl(uintptr_t aId)
      MOZ_REQUIRES(sInstanceMutex) {
    auto it = mConnections.find(aId);
    if (NS_WARN_IF(it == mConnections.end())) {
      DC_DEBUG(("Can't find connection ulp %p", (void*)aId));
      return nullptr;
    }
    return it->second;
  }

  static int SendSctpPacket(void* addr, void* buffer, size_t length,
                            uint8_t tos, uint8_t set_df) {
    uintptr_t id = reinterpret_cast<uintptr_t>(addr);
    RefPtr<DataChannelConnectionUsrsctp> connection =
        DataChannelRegistry::Lookup(id);
    if (NS_WARN_IF(!connection) || connection->InShutdown()) {
      return 0;
    }
    return connection->SendSctpPacket(static_cast<uint8_t*>(buffer), length);
  }

  void InitUsrSctp() {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
#ifndef MOZ_PEERCONNECTION
    MOZ_CRASH("Trying to use SCTP/DTLS without dom/media/webrtc/transport");
#endif

    DC_DEBUG(("Calling usrsctp_init %p", this));

    // usrsctp is a process-global singleton. If a previous DeinitUsrSctp()
    // could not tear the stack down (see the comment there), usrsctp is still
    // initialized and its timer thread is still running; re-initializing would
    // reset global locks and the callout queue out from under it and start a
    // second timer thread, corrupting memory. Reuse the live stack; the
    // callback and sysctl configuration below are still in effect.
    if (sInitted) {
      return;
    }
    usrsctp_init(0, DataChannelRegistry::SendSctpPacket, debug_printf);
    sInitted = true;

    // Set logging to SCTP:LogLevel::Debug to get SCTP debugs
    if (MOZ_LOG_TEST(gSCTPLog, LogLevel::Debug)) {
      usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
    }

    // Do not send ABORTs in response to INITs (1).
    // Do not send ABORTs for received Out of the Blue packets (2).
    usrsctp_sysctl_set_sctp_blackhole(2);

    // Disable the Explicit Congestion Notification extension (currently not
    // supported by the Firefox code)
    usrsctp_sysctl_set_sctp_ecn_enable(0);

    // Enable interleaving messages for different streams (incoming)
    // See: https://tools.ietf.org/html/rfc6458#section-8.1.20
    usrsctp_sysctl_set_sctp_default_frag_interleave(2);

    // Disabling authentication and dynamic address reconfiguration as neither
    // of them are used for data channel and only result in additional code
    // paths being used.
    usrsctp_sysctl_set_sctp_asconf_enable(0);
    usrsctp_sysctl_set_sctp_auth_enable(0);

    // Disable this redundant limit. rwnd is what ought to be used for this
    usrsctp_sysctl_set_sctp_max_chunks_on_queue(
        std::numeric_limits<uint32_t>::max());
  }

  void DeinitUsrSctp() {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(sInitted);
    DC_DEBUG(("Calling usrsctp_finish %p", this));
    // usrsctp_finish() is fallible: it returns -1 and tears nothing down (the
    // timer thread keeps running) while an endpoint whose destruction was
    // deferred (e.g. by an in-flight timer handler) is still on the global
    // list. Only clear sInitted on success, so InitUsrSctp() never
    // re-initializes usrsctp over a still-live stack.
    if (usrsctp_finish() == 0) {
      sInitted = false;
    }
  }

  uintptr_t mNextId MOZ_GUARDED_BY(sInstanceMutex) = 1;
  std::map<uintptr_t, RefPtr<DataChannelConnectionUsrsctp>> mConnections
      MOZ_GUARDED_BY(sInstanceMutex);
  UniquePtr<media::ShutdownBlockingTicket> mShutdownBlocker;
  static StaticMutex sInstanceMutex;
  static bool sInitted;
};

bool DataChannelRegistry::sInitted = false;

StaticMutex DataChannelRegistry::sInstanceMutex;

static int receive_cb(struct socket* sock, union sctp_sockstore addr,
                      void* data, size_t datalen, struct sctp_rcvinfo rcv,
                      int flags, void* ulp_info) {
  DC_DEBUG(("In receive_cb, ulp_info=%p", ulp_info));
  uintptr_t id = reinterpret_cast<uintptr_t>(ulp_info);
  RefPtr<DataChannelConnectionUsrsctp> connection =
      DataChannelRegistry::Lookup(id);
  if (!connection) {
    // Unfortunately, we can get callbacks after calling
    // usrsctp_close(socket), so we need to simply ignore them if we've
    // already killed the DataChannelConnection object
    DC_DEBUG((
        "Ignoring receive callback for terminated Connection ulp=%p, %zu bytes",
        ulp_info, datalen));
    return 0;
  }
  return connection->ReceiveCallback(sock, data, datalen, rcv, flags);
}

static RefPtr<DataChannelConnectionUsrsctp> GetConnectionFromSocket(
    struct socket* sock) {
  struct sockaddr* addrs = nullptr;
  int naddrs = usrsctp_getladdrs(sock, 0, &addrs);
  if (naddrs <= 0 || addrs[0].sa_family != AF_CONN) {
    return nullptr;
  }
  // usrsctp_getladdrs() returns the addresses bound to this socket, which
  // contains the SctpDataMediaChannel* as sconn_addr.  Read the pointer,
  // then free the list of addresses once we have the pointer.  We only open
  // AF_CONN sockets, and they should all have the sconn_addr set to the
  // pointer that created them, so [0] is as good as any other.
  struct sockaddr_conn* sconn =
      reinterpret_cast<struct sockaddr_conn*>(&addrs[0]);
  uintptr_t id = reinterpret_cast<uintptr_t>(sconn->sconn_addr);
  RefPtr<DataChannelConnectionUsrsctp> connection =
      DataChannelRegistry::Lookup(id);
  usrsctp_freeladdrs(addrs);

  return connection;
}

// Called when the buffer empties to the threshold value.  This is called
// from OnSctpPacketReceived() through the sctp stack.
int DataChannelConnectionUsrsctp::OnThresholdEvent(struct socket* sock,
                                                   uint32_t sb_free,
                                                   void* ulp_info) {
  RefPtr<DataChannelConnectionUsrsctp> connection =
      GetConnectionFromSocket(sock);
  if (connection) {
    connection->SendDeferredMessages();
  } else {
    DC_ERROR(("Can't find connection for socket %p", sock));
  }
  return 0;
}

DataChannelConnectionUsrsctp::~DataChannelConnectionUsrsctp() {
  MOZ_ASSERT(!mSocket);
}

void DataChannelConnectionUsrsctp::Destroy() {
  // Though it's probably ok to do this and close the sockets;
  // if we really want it to do true clean shutdowns it can
  // create a dependant Internal object that would remain around
  // until the network shut down the association or timed out.
  MOZ_ASSERT(NS_IsMainThread());
  DataChannelConnection::Destroy();

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  auto self = DataChannelRegistry::Lookup(mId);
  MOZ_DIAGNOSTIC_ASSERT(self);
  MOZ_DIAGNOSTIC_ASSERT(this == self.get());
#endif
  // Finish Destroy on STS thread to avoid bug 876167 - once that's fixed,
  // the usrsctp_close() calls can move back here (and just proxy the
  // disconnect_all())
  RUN_ON_THREAD(mSTS,
                WrapRunnable(RefPtr<DataChannelConnectionUsrsctp>(this),
                             &DataChannelConnectionUsrsctp::DestroyOnSTS),
                NS_DISPATCH_NORMAL);

  // All existing callbacks have refs to DataChannelConnection - however,
  // we need to handle their destroying the object off mainthread/STS

  // nsDOMDataChannel objects have refs to DataChannels that have refs to us
}

void DataChannelConnectionUsrsctp::DestroyOnSTS() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  if (mSocket) usrsctp_close(mSocket);
  mSocket = nullptr;

  usrsctp_deregister_address(reinterpret_cast<void*>(mId));
  DC_DEBUG(
      ("Deregistered %p from the SCTP stack.", reinterpret_cast<void*>(mId)));

  // We do this at the very last because it might tear down usrsctp, and we
  // don't want that to happen before the usrsctp_close call above
  Dispatch(NS_NewRunnableFunction(
      "DataChannelConnection::Destroy",
      [this, self = RefPtr<DataChannelConnection>(this)]() {
        DataChannelRegistry::Deregister(mId);
      }));
}

DataChannelConnectionUsrsctp::DataChannelConnectionUsrsctp(
    DataChannelConnection::DataConnectionListener* aListener,
    nsISerialEventTarget* aTarget, MediaTransportHandler* aHandler)
    : DataChannelConnection(aListener, aTarget, aHandler) {}

bool DataChannelConnectionUsrsctp::Init(const uint16_t aLocalPort,
                                        const uint16_t aNumStreams) {
  MOZ_ASSERT(NS_IsMainThread());

  struct sctp_initmsg initmsg = {};
  struct sctp_assoc_value av = {};
  struct sctp_event event = {};
  socklen_t len;

  uint16_t event_types[] = {
      SCTP_ASSOC_CHANGE,          SCTP_PEER_ADDR_CHANGE,
      SCTP_REMOTE_ERROR,          SCTP_SHUTDOWN_EVENT,
      SCTP_ADAPTATION_INDICATION, SCTP_PARTIAL_DELIVERY_EVENT,
      SCTP_SEND_FAILED_EVENT,     SCTP_STREAM_RESET_EVENT,
      SCTP_STREAM_CHANGE_EVENT};

  mId = DataChannelRegistry::Register(this);

  socklen_t buf_size = 1024 * 1024;

  // Open sctp with a callback
  if ((mSocket = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, receive_cb,
                                &DataChannelConnectionUsrsctp::OnThresholdEvent,
                                usrsctp_sysctl_get_sctp_sendspace() / 2,
                                reinterpret_cast<void*>(mId))) == nullptr) {
    goto error_cleanup;
  }

  if (usrsctp_setsockopt(mSocket, SOL_SOCKET, SO_RCVBUF, (const void*)&buf_size,
                         sizeof(buf_size)) < 0) {
    DC_ERROR(("Couldn't change receive buffer size on SCTP socket"));
    goto error_cleanup;
  }
  if (usrsctp_setsockopt(mSocket, SOL_SOCKET, SO_SNDBUF, (const void*)&buf_size,
                         sizeof(buf_size)) < 0) {
    DC_ERROR(("Couldn't change send buffer size on SCTP socket"));
    goto error_cleanup;
  }

  // Make non-blocking for bind/connect.  SCTP over UDP defaults to non-blocking
  // in associations for normal IO
  if (usrsctp_set_non_blocking(mSocket, 1) < 0) {
    DC_ERROR(("Couldn't set non_blocking on SCTP socket"));
    // We can't handle connect() safely if it will block, not that this will
    // even happen.
    goto error_cleanup;
  }

  // Make sure when we close the socket, make sure it doesn't call us back
  // again! This would cause it try to use an invalid DataChannelConnection
  // pointer
  struct linger l;
  l.l_onoff = 1;
  l.l_linger = 0;
  if (usrsctp_setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (const void*)&l,
                         (socklen_t)sizeof(struct linger)) < 0) {
    DC_ERROR(("Couldn't set SO_LINGER on SCTP socket"));
    // unsafe to allow it to continue if this fails
    goto error_cleanup;
  }

  // XXX Consider disabling this when we add proper SDP negotiation.
  // We may want to leave enabled for supporting 'cloning' of SDP offers, which
  // implies re-use of the same pseudo-port number, or forcing a renegotiation.
  {
    const int option_value = 1;
    if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_REUSE_PORT,
                           (const void*)&option_value,
                           (socklen_t)sizeof(option_value)) < 0) {
      DC_WARN(("Couldn't set SCTP_REUSE_PORT on SCTP socket"));
    }
    if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_NODELAY,
                           (const void*)&option_value,
                           (socklen_t)sizeof(option_value)) < 0) {
      DC_WARN(("Couldn't set SCTP_NODELAY on SCTP socket"));
    }
  }

  // Set explicit EOR
  {
    const int option_value = 1;
    if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_EXPLICIT_EOR,
                           (const void*)&option_value,
                           (socklen_t)sizeof(option_value)) < 0) {
      DC_ERROR(("*** failed to enable explicit EOR mode %d", errno));
      goto error_cleanup;
    }
  }

  // Enable ndata
  av.assoc_id = SCTP_FUTURE_ASSOC;
  av.assoc_value = 1;
  if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_INTERLEAVING_SUPPORTED,
                         &av, (socklen_t)sizeof(struct sctp_assoc_value)) < 0) {
    DC_ERROR(("*** failed enable ndata errno %d", errno));
    goto error_cleanup;
  }

  av.assoc_id = SCTP_ALL_ASSOC;
  av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
  if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av,
                         (socklen_t)sizeof(struct sctp_assoc_value)) < 0) {
    DC_ERROR(("*** failed enable stream reset errno %d", errno));
    goto error_cleanup;
  }

  /* Enable the events of interest. */
  event.se_assoc_id = SCTP_ALL_ASSOC;
  event.se_on = 1;
  for (unsigned short event_type : event_types) {
    event.se_type = event_type;
    if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_EVENT, &event,
                           sizeof(event)) < 0) {
      DC_ERROR(("*** failed setsockopt SCTP_EVENT errno %d", errno));
      goto error_cleanup;
    }
  }

  len = sizeof(initmsg);
  if (usrsctp_getsockopt(mSocket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, &len) <
      0) {
    DC_ERROR(("*** failed getsockopt SCTP_INITMSG"));
    goto error_cleanup;
  }
  DC_DEBUG(("Setting number of SCTP streams to %u, was %u/%u", aNumStreams,
            initmsg.sinit_num_ostreams, initmsg.sinit_max_instreams));
  initmsg.sinit_num_ostreams = aNumStreams;
  initmsg.sinit_max_instreams = MAX_NUM_STREAMS;
  if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg,
                         (socklen_t)sizeof(initmsg)) < 0) {
    DC_ERROR(("*** failed setsockopt SCTP_INITMSG, errno %d", errno));
    goto error_cleanup;
  }

  mSTS->Dispatch(
      NS_NewRunnableFunction("DataChannelConnection::Init", [id = mId]() {
        usrsctp_register_address(reinterpret_cast<void*>(id));
        DC_DEBUG(("Registered %p within the SCTP stack.",
                  reinterpret_cast<void*>(id)));
      }));

  return true;

error_cleanup:
  usrsctp_close(mSocket);
  mSocket = nullptr;
  DataChannelRegistry::Deregister(mId);
  return false;
}

void DataChannelConnectionUsrsctp::OnTransportReady() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("dtls open"));
  if (mSctpConfigured) {
    // mSocket could have been closed by an error or for some other reason,
    // don't open an opportunity to reinit.
    return;
  }

  mSctpConfigured = true;

  struct sockaddr_conn addr = {};
  addr.sconn_family = AF_CONN;
#if defined(__Userspace_os_Darwin)
  addr.sconn_len = sizeof(addr);
#endif
  addr.sconn_port = htons(mLocalPort);
  addr.sconn_addr = reinterpret_cast<void*>(mId);

  DC_DEBUG(("Calling usrsctp_bind"));
  int r = usrsctp_bind(mSocket, reinterpret_cast<struct sockaddr*>(&addr),
                       sizeof(addr));
  if (r < 0) {
    DC_ERROR(("usrsctp_bind failed: %d", r));
  } else {
    // This is the remote addr
    addr.sconn_port = htons(mRemotePort);
    DC_DEBUG(("Calling usrsctp_connect"));
    r = usrsctp_connect(mSocket, reinterpret_cast<struct sockaddr*>(&addr),
                        sizeof(addr));
    if (r >= 0 || errno == EINPROGRESS) {
      struct sctp_paddrparams paddrparams = {};
      socklen_t opt_len;

      memcpy(&paddrparams.spp_address, &addr, sizeof(struct sockaddr_conn));
      opt_len = (socklen_t)sizeof(struct sctp_paddrparams);
      r = usrsctp_getsockopt(mSocket, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS,
                             &paddrparams, &opt_len);
      if (r < 0) {
        DC_ERROR(("usrsctp_getsockopt failed: %d", r));
      } else {
        // This field is misnamed. |spp_pathmtu| represents the maximum
        // _payload_ size in libusrsctp. So:
        // 1280 (a reasonable IPV6 MTU according to RFC 8831)
        //  -12 (sctp header)
        //  -24 (GCM sipher)
        //  -13 (DTLS record header)
        //   -8 (UDP header)
        //   -4 (TURN ChannelData)
        //  -40 (IPV6 header)
        // = 1179
        // We could further restrict this, because RFC 8831 suggests a starting
        // IPV4 path MTU of 1200, which would lead to a value of 1115.
        // I suspect that in practice the path MTU for IPV4 is substantially
        // larger than 1200.
        paddrparams.spp_pathmtu = 1179;
        paddrparams.spp_flags &= ~SPP_PMTUD_ENABLE;
        paddrparams.spp_flags |= SPP_PMTUD_DISABLE;
        opt_len = (socklen_t)sizeof(struct sctp_paddrparams);
        r = usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS,
                               &paddrparams, opt_len);
        if (r < 0) {
          DC_ERROR(("usrsctp_getsockopt failed: %d", r));
        } else {
          DC_ERROR(("usrsctp: PMTUD disabled, MTU set to %u",
                    paddrparams.spp_pathmtu));
        }
      }
    }
    if (r < 0) {
      if (errno == EINPROGRESS) {
        // non-blocking
        return;
      }
      DC_ERROR(("usrsctp_connect failed: %d", errno));
      SetState(DataChannelConnectionState::Closed);
    } else {
      // We fire ON_CONNECTION via SCTP_COMM_UP when we get that
      return;
    }
  }
}

void DataChannelConnectionUsrsctp::OnSctpPacketReceived(
    const MediaPacket& packet) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (MOZ_LOG_TEST(gSCTPLog, LogLevel::Debug)) {
    char* buf;

    if ((buf = usrsctp_dumppacket((void*)packet.data(), packet.len(),
                                  SCTP_DUMP_INBOUND)) != nullptr) {
      SCTP_LOG(("%s", buf));
      usrsctp_freedumpbuffer(buf);
    }
  }
  // Pass the data to SCTP
  usrsctp_conninput(reinterpret_cast<void*>(mId), packet.data(), packet.len(),
                    0);
}

int DataChannelConnectionUsrsctp::SendSctpPacket(const uint8_t* buffer,
                                                 size_t length) {
  if (MOZ_LOG_TEST(gSCTPLog, LogLevel::Debug)) {
    char* buf;

    if ((buf = usrsctp_dumppacket(buffer, length, SCTP_DUMP_OUTBOUND)) !=
        nullptr) {
      SCTP_LOG(("%s", buf));
      usrsctp_freedumpbuffer(buf);
    }
  }

  auto packet = std::make_unique<MediaPacket>();
  packet->SetType(MediaPacket::SCTP);
  packet->Copy(static_cast<const uint8_t*>(buffer), length);

  SendPacket(std::move(packet));
  return 0;  // cheat!  Packets can always be dropped later anyways
}

uint32_t DataChannelConnectionUsrsctp::UpdateCurrentStreamIndex() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  RefPtr<DataChannel> channel = mChannels.GetNextChannel(mCurrentStream);
  if (!channel) {
    mCurrentStream = 0;
  } else {
    mCurrentStream = channel->mStream;
  }
  return mCurrentStream;
}

uint32_t DataChannelConnectionUsrsctp::GetCurrentStreamIndex() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (!mChannels.Get(mCurrentStream)) {
    // The stream muse have been removed, reset
    DC_DEBUG(("Reset mCurrentChannel"));
    mCurrentStream = 0;
  }
  return mCurrentStream;
}

bool DataChannelConnectionUsrsctp::RaiseStreamLimitTo(uint16_t aNewLimit) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (GetState() == DataChannelConnectionState::Closed) {
    // Smile and nod, could end up here via a dispatch
    return true;
  }

  if (mNegotiatedIdLimit == MAX_NUM_STREAMS) {
    // We're already maxed out!
    return false;
  }

  if (aNewLimit <= mNegotiatedIdLimit) {
    // We already have enough
    return true;
  }

  if (aNewLimit > MAX_NUM_STREAMS) {
    // Hard cap: if someone calls again asking for this much, we'll return
    // false above
    aNewLimit = MAX_NUM_STREAMS;
  }

  struct sctp_status status = {};
  socklen_t len = (socklen_t)sizeof(struct sctp_status);
  if (usrsctp_getsockopt(mSocket, IPPROTO_SCTP, SCTP_STATUS, &status, &len) <
      0) {
    DC_ERROR(("***failed: getsockopt SCTP_STATUS"));
    return false;
  }
  const uint16_t outStreamsNeeded =
      aNewLimit - mNegotiatedIdLimit;  // number to add

  // Note: if multiple channel opens happen when we don't have enough space,
  // we'll call RaiseStreamLimitTo() multiple times
  struct sctp_add_streams sas = {};
  sas.sas_instrms = 0;
  sas.sas_outstrms = outStreamsNeeded; /* XXX error handling */
  // Doesn't block, we get an event when it succeeds or fails
  if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_ADD_STREAMS, &sas,
                         (socklen_t)sizeof(struct sctp_add_streams)) < 0) {
    if (errno == EALREADY) {
      // Uhhhh, ok?
      DC_DEBUG(("Already have %u output streams", outStreamsNeeded));
      return true;
    }

    DC_ERROR(("***failed: setsockopt ADD errno=%d", errno));
    return false;
  }
  DC_DEBUG(("Requested %u more streams", outStreamsNeeded));
  // We add to mNegotiatedIdLimit when we get a SCTP_STREAM_CHANGE_EVENT and the
  // values are larger than mNegotiatedIdLimit
  return true;
}

void DataChannelConnectionUsrsctp::SendDeferredMessages() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  RefPtr<DataChannel> channel;  // we may null out the refs to this

  DC_DEBUG(("SendDeferredMessages called, pending type: %s",
            ToString(mPendingType)));
  if (mPendingType == PendingType::None) {
    return;
  }

  // Send pending control messages
  // Note: If ndata is not active, check if DCEP messages are currently
  // outstanding. These need to
  //       be sent first before other streams can be used for sending.
  if (!mBufferedControl.IsEmpty() &&
      (mSendInterleaved || mPendingType == PendingType::Dcep)) {
    if (SendBufferedMessages(mBufferedControl, nullptr)) {
      return;
    }

    // Note: There may or may not be pending data messages
    mPendingType = PendingType::Data;
  }

  bool blocked = false;
  uint32_t i = GetCurrentStreamIndex();
  uint32_t end = i;
  do {
    channel = mChannels.Get(i);
    if (!channel) {
      continue;
    }

    // Should already be cleared if closing/closed
    if (channel->mBufferedData.IsEmpty()) {
      i = UpdateCurrentStreamIndex();
      continue;
    }

    // Send buffered data messages
    // Warning: This will fail in case ndata is inactive and a previously
    //          deallocated data channel has not been closed properly. If you
    //          ever see that no messages can be sent on any channel, this is
    //          likely the cause (an explicit EOR message partially sent whose
    //          remaining chunks are still being waited for).
    size_t written = 0;
    blocked = SendBufferedMessages(channel->mBufferedData, &written);
    if (written) {
      channel->DecrementBufferedAmount(written);
    }

    // Update current stream index
    // Note: If ndata is not active, the outstanding data messages on this
    //       stream need to be sent first before other streams can be used for
    //       sending.
    if (mSendInterleaved || !blocked) {
      i = UpdateCurrentStreamIndex();
    }
  } while (!blocked && i != end);

  if (!blocked) {
    mPendingType =
        mBufferedControl.IsEmpty() ? PendingType::None : PendingType::Dcep;
  }
}

// buffer MUST have at least one item!
// returns if we're still blocked (true)
bool DataChannelConnectionUsrsctp::SendBufferedMessages(
    nsTArray<OutgoingMsg>& buffer, size_t* aWritten) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  do {
    // Re-send message
    const int error = SendMsgInternal(buffer[0], aWritten);
    switch (error) {
      case 0:
        buffer.RemoveElementAt(0);
        break;
      case EAGAIN:
#if (EAGAIN != EWOULDBLOCK)
      case EWOULDBLOCK:
#endif
        return true;
      default:
        buffer.RemoveElementAt(0);
        DC_ERROR(("error on sending: %d", error));
        break;
    }
  } while (!buffer.IsEmpty());

  return false;
}

// NOTE: the updated spec from the IETF says we should set in-order until we
// receive an ACK. That would make this code moot.  Keep it for now for
// backwards compatibility.
void DataChannelConnectionUsrsctp::OnStreamOpen(uint16_t stream) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  nsTArray<UniquePtr<QueuedDataMessage>> temp;
  std::swap(temp, mQueuedData);

  temp.RemoveElementsBy([stream, this](const auto& dataItem) {
    const bool match = dataItem->mStream == stream;
    if (match) {
      DC_DEBUG(("Delivering queued data for stream %u, length %zu", stream,
                dataItem->mData.Length()));
      // Deliver the queued data
      HandleDataMessageChunk(
          dataItem->mData.Elements(), dataItem->mData.Length(), dataItem->mPpid,
          dataItem->mStream, dataItem->mMessageId, dataItem->mFlags);
    }
    return match;
  });

  std::swap(temp, mQueuedData);
}

bool DataChannelConnectionUsrsctp::HasQueuedData(uint16_t aStream) const {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  for (const auto& data : mQueuedData) {
    if (data->mStream == aStream) {
      return true;
    }
  }
  return false;
}

void DataChannelConnectionUsrsctp::HandleDataMessageChunk(
    const void* data, size_t length, uint32_t ppid, uint16_t stream,
    uint16_t messageId, int flags) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: stream %u, length %zu, ppid %u, message-id %u", __func__,
            stream, length, ppid, messageId));

  RefPtr<DataChannel> channel = FindChannelByStream(stream);

  // XXX A closed channel may trip this... check
  // NOTE: the updated spec from the IETF says we should set in-order until we
  // receive an ACK. That would make this code moot.  Keep it for now for
  // backwards compatibility.
  if (!channel || HasQueuedData(stream)) {
    // In the updated 0-RTT open case, the sender can send data immediately
    // after Open, and doesn't set the in-order bit (since we don't have a
    // response or ack).  Also, with external negotiation, data can come in
    // before we're told about the external negotiation.  We need to buffer
    // data until either a) Open comes in, if the ordering get messed up,
    // or b) the app tells us this channel was externally negotiated.  When
    // these occur, we deliver the data.

    // Since this is rare and non-performance, keep a single list of queued
    // data messages to deliver once the channel opens.
    DC_DEBUG(("Queuing data for stream %u, length %zu", stream, length));
    // Copies data
    mQueuedData.AppendElement(
        new QueuedDataMessage(stream, ppid, messageId, flags,
                              static_cast<const uint8_t*>(data), length));
    return;
  }

  const char* type = (ppid == DATA_CHANNEL_PPID_DOMSTRING_PARTIAL ||
                      ppid == DATA_CHANNEL_PPID_DOMSTRING ||
                      ppid == DATA_CHANNEL_PPID_DOMSTRING_EMPTY)
                         ? "string"
                         : "binary";

  auto it = channel->mRecvBuffers.find(messageId);
  if (it != channel->mRecvBuffers.end()) {
    IncomingMsg& msg(it->second);
    if (!ReassembleMessageChunk(msg, data, length, ppid, stream)) {
      FinishClose_s(channel);
      return;
    }

    if (flags & MSG_EOR) {
      DC_DEBUG(
          ("%s: last chunk of multi-chunk %s message, id %u, "
           "stream %u, length %zu",
           __func__, type, messageId, stream, length));
      HandleDataMessage(std::move(msg));
      channel->mRecvBuffers.erase(messageId);
    } else {
      DC_DEBUG(
          ("%s: middle chunk of multi-chunk %s message, id %u, "
           "stream %u, length %zu",
           __func__, type, messageId, stream, length));
    }
    return;
  }

  IncomingMsg msg(ppid, stream);
  if (!ReassembleMessageChunk(msg, data, length, ppid, stream)) {
    FinishClose_s(channel);
    return;
  }

  if (flags & MSG_EOR) {
    DC_DEBUG(
        ("%s: single-chunk %s message, id %u, stream %u, "
         "length %zu",
         __func__, type, messageId, stream, length));
    HandleDataMessage(std::move(msg));
  } else {
    DC_DEBUG(
        ("%s: first chunk of multi-chunk %s message, id %u, "
         "stream %u, length %zu",
         __func__, type, messageId, stream, length));
    channel->mRecvBuffers.insert({messageId, std::move(msg)});
  }
}

// A sane endpoint should not be fragmenting DCEP, but I think it is allowed
// technically? Use the same chunk reassembly logic that we use for DATA.
void DataChannelConnectionUsrsctp::HandleDCEPMessageChunk(const void* buffer,
                                                          size_t length,
                                                          uint32_t ppid,
                                                          uint16_t stream,
                                                          int flags) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  if (!mRecvBuffer.isSome()) {
    mRecvBuffer = Some(IncomingMsg(ppid, stream));
  }

  if (!ReassembleMessageChunk(*mRecvBuffer, buffer, length, ppid, stream)) {
    CloseAll_s();
    return;
  }

  if (!(flags & MSG_EOR)) {
    DC_DEBUG(("%s: No EOR, waiting for more chunks", __func__));
    return;
  }

  DC_DEBUG(("%s: EOR, handling", __func__));
  // Last chunk, ready to go.
  HandleDCEPMessage(std::move(*mRecvBuffer));
  mRecvBuffer = Nothing();
}

void DataChannelConnectionUsrsctp::HandleMessageChunk(
    const void* buffer, size_t length, uint32_t ppid, uint16_t stream,
    uint16_t messageId, int flags) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  switch (ppid) {
    case DATA_CHANNEL_PPID_CONTROL:
      DC_DEBUG(("%s: Got DCEP message size %zu", __func__, length));
      HandleDCEPMessageChunk(buffer, length, ppid, stream, flags);
      break;
    case DATA_CHANNEL_PPID_DOMSTRING_PARTIAL:
    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_DOMSTRING_EMPTY:
    case DATA_CHANNEL_PPID_BINARY_PARTIAL:
    case DATA_CHANNEL_PPID_BINARY:
    case DATA_CHANNEL_PPID_BINARY_EMPTY:
      HandleDataMessageChunk(buffer, length, ppid, stream, messageId, flags);
      break;
    default:
      DC_ERROR((
          "Unhandled message of length %zu PPID %u on stream %u received (%s).",
          length, ppid, stream, (flags & MSG_EOR) ? "complete" : "partial"));
      break;
  }
}

void DataChannelConnectionUsrsctp::HandleAssociationChangeEvent(
    const struct sctp_assoc_change* sac) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  uint32_t i, n;
  DataChannelConnectionState state = GetState();
  switch (sac->sac_state) {
    case SCTP_COMM_UP:
      DC_DEBUG(("Association change: SCTP_COMM_UP"));
      if (state == DataChannelConnectionState::Connecting) {
        SetState(DataChannelConnectionState::Open);

        DC_DEBUG(("Negotiated number of incoming streams: %" PRIu16,
                  sac->sac_inbound_streams));
        DC_DEBUG(("Negotiated number of outgoing streams: %" PRIu16,
                  sac->sac_outbound_streams));
        mNegotiatedIdLimit = std::max(
            mNegotiatedIdLimit,
            std::max(sac->sac_outbound_streams, sac->sac_inbound_streams));

        DC_DEBUG(("DTLS connect() succeeded!  Entering connected mode"));

        // Open any streams pending...
        ProcessQueuedOpens();

      } else if (state == DataChannelConnectionState::Open) {
        DC_DEBUG(("DataConnection Already OPEN"));
      } else {
        DC_ERROR(("Unexpected state: %s", ToString(state)));
      }
      break;
    case SCTP_COMM_LOST:
      DC_DEBUG(("Association change: SCTP_COMM_LOST"));
      // This association is toast, so also close all the channels
      CloseAll_s();
      break;
    case SCTP_RESTART:
      DC_DEBUG(("Association change: SCTP_RESTART"));
      break;
    case SCTP_SHUTDOWN_COMP:
      DC_DEBUG(("Association change: SCTP_SHUTDOWN_COMP"));
      CloseAll_s();
      break;
    case SCTP_CANT_STR_ASSOC:
      DC_DEBUG(("Association change: SCTP_CANT_STR_ASSOC"));
      break;
    default:
      DC_DEBUG(("Association change: UNKNOWN"));
      break;
  }
  DC_DEBUG(("Association change: streams (in/out) = (%u/%u)",
            sac->sac_inbound_streams, sac->sac_outbound_streams));

  if (NS_WARN_IF(!sac)) {
    return;
  }

  n = sac->sac_length - sizeof(*sac);
  if ((sac->sac_state == SCTP_COMM_UP) || (sac->sac_state == SCTP_RESTART)) {
    if (n > 0) {
      for (i = 0; i < n; ++i) {
        switch (sac->sac_info[i]) {
          case SCTP_ASSOC_SUPPORTS_PR:
            DC_DEBUG(("Supports: PR"));
            break;
          case SCTP_ASSOC_SUPPORTS_AUTH:
            DC_DEBUG(("Supports: AUTH"));
            break;
          case SCTP_ASSOC_SUPPORTS_ASCONF:
            DC_DEBUG(("Supports: ASCONF"));
            break;
          case SCTP_ASSOC_SUPPORTS_MULTIBUF:
            DC_DEBUG(("Supports: MULTIBUF"));
            break;
          case SCTP_ASSOC_SUPPORTS_RE_CONFIG:
            DC_DEBUG(("Supports: RE-CONFIG"));
            break;
#if defined(SCTP_ASSOC_SUPPORTS_INTERLEAVING)
          case SCTP_ASSOC_SUPPORTS_INTERLEAVING:
            DC_DEBUG(("Supports: NDATA"));
            // TODO: This should probably be set earlier above in 'case
            //       SCTP_COMM_UP' but we also need this for 'SCTP_RESTART'.
            mSendInterleaved = true;
            break;
#endif
          default:
            DC_ERROR(("Supports: UNKNOWN(0x%02x)", sac->sac_info[i]));
            break;
        }
      }
    }
  } else if (((sac->sac_state == SCTP_COMM_LOST) ||
              (sac->sac_state == SCTP_CANT_STR_ASSOC)) &&
             (n > 0)) {
    DC_DEBUG(("Association: ABORT ="));
    for (i = 0; i < n; ++i) {
      DC_DEBUG((" 0x%02x", sac->sac_info[i]));
    }
  }
  if ((sac->sac_state == SCTP_CANT_STR_ASSOC) ||
      (sac->sac_state == SCTP_SHUTDOWN_COMP) ||
      (sac->sac_state == SCTP_COMM_LOST)) {
    return;
  }
}

void DataChannelConnectionUsrsctp::HandlePeerAddressChangeEvent(
    const struct sctp_paddr_change* spc) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  const char* addr = "";
#if !defined(__Userspace_os_Windows)
  char addr_buf[INET6_ADDRSTRLEN];
  struct sockaddr_in* sin;
  struct sockaddr_in6* sin6;
#endif

  switch (spc->spc_aaddr.ss_family) {
    case AF_INET:
#if !defined(__Userspace_os_Windows)
      sin = (struct sockaddr_in*)&spc->spc_aaddr;
      addr = inet_ntop(AF_INET, &sin->sin_addr, addr_buf, INET6_ADDRSTRLEN);
#endif
      break;
    case AF_INET6:
#if !defined(__Userspace_os_Windows)
      sin6 = (struct sockaddr_in6*)&spc->spc_aaddr;
      addr = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, INET6_ADDRSTRLEN);
#endif
      break;
    case AF_CONN:
      addr = "DTLS connection";
      break;
    default:
      break;
  }
  DC_DEBUG(("Peer address %s is now ", addr));
  switch (spc->spc_state) {
    case SCTP_ADDR_AVAILABLE:
      DC_DEBUG(("SCTP_ADDR_AVAILABLE"));
      break;
    case SCTP_ADDR_UNREACHABLE:
      DC_DEBUG(("SCTP_ADDR_UNREACHABLE"));
      break;
    case SCTP_ADDR_REMOVED:
      DC_DEBUG(("SCTP_ADDR_REMOVED"));
      break;
    case SCTP_ADDR_ADDED:
      DC_DEBUG(("SCTP_ADDR_ADDED"));
      break;
    case SCTP_ADDR_MADE_PRIM:
      DC_DEBUG(("SCTP_ADDR_MADE_PRIM"));
      break;
    case SCTP_ADDR_CONFIRMED:
      DC_DEBUG(("SCTP_ADDR_CONFIRMED"));
      break;
    default:
      DC_ERROR(("UNKNOWN SCP STATE"));
      break;
  }
  if (spc->spc_error) {
    DC_ERROR((" (error = 0x%08x).\n", spc->spc_error));
  }
}

void DataChannelConnectionUsrsctp::HandleRemoteErrorEvent(
    const struct sctp_remote_error* sre) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  size_t i, n;

  n = sre->sre_length - sizeof(struct sctp_remote_error);
  DC_WARN(("Remote Error (error = 0x%04x): ", sre->sre_error));
  for (i = 0; i < n; ++i) {
    DC_WARN((" 0x%02x", sre->sre_data[i]));
  }
}

void DataChannelConnectionUsrsctp::HandleShutdownEvent(
    const struct sctp_shutdown_event* sse) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("Shutdown event."));
  /* XXX: notify all channels. */
  // Attempts to actually send anything will fail
}

void DataChannelConnectionUsrsctp::HandleAdaptationIndication(
    const struct sctp_adaptation_event* sai) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("Adaptation indication: %x.", sai->sai_adaptation_ind));
}

void DataChannelConnectionUsrsctp::HandlePartialDeliveryEvent(
    const struct sctp_pdapi_event* spde) {
  // Note: Be aware that stream and sequence number being u32 instead of u16 is
  //       a bug in the SCTP API. This may change in the future.

  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("Partial delivery event: "));
  switch (spde->pdapi_indication) {
    case SCTP_PARTIAL_DELIVERY_ABORTED:
      DC_DEBUG(("delivery aborted "));
      break;
    default:
      DC_ERROR(("??? "));
      break;
  }
  DC_DEBUG(("(flags = %x), stream = %" PRIu32 ", sn = %" PRIu32,
            spde->pdapi_flags, spde->pdapi_stream, spde->pdapi_seq));

  // Validate stream ID
  if (spde->pdapi_stream >= UINT16_MAX) {
    DC_ERROR(("Invalid stream id in partial delivery event: %" PRIu32 "\n",
              spde->pdapi_stream));
    return;
  }

  // Find channel and reset buffer
  RefPtr<DataChannel> channel =
      FindChannelByStream((uint16_t)spde->pdapi_stream);
  if (channel) {
    auto it = channel->mRecvBuffers.find(spde->pdapi_seq);
    if (it != channel->mRecvBuffers.end()) {
      DC_WARN(("Abort partially delivered message of %zu bytes\n",
               it->second.GetLength()));
      channel->mRecvBuffers.erase(it);
    } else {
      // Uhhh, ok?
      DC_WARN(
          ("Abort partially delivered message that we've never seen any "
           "of? What?"));
    }
  }
}

void DataChannelConnectionUsrsctp::HandleSendFailedEvent(
    const struct sctp_send_failed_event* ssfe) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  size_t i, n;

  if (ssfe->ssfe_flags & SCTP_DATA_UNSENT) {
    DC_DEBUG(("Unsent "));
  }
  if (ssfe->ssfe_flags & SCTP_DATA_SENT) {
    DC_DEBUG(("Sent "));
  }
  if (ssfe->ssfe_flags & ~(SCTP_DATA_SENT | SCTP_DATA_UNSENT)) {
    DC_DEBUG(("(flags = %x) ", ssfe->ssfe_flags));
  }
#ifdef XP_WIN
#  define PRIPPID "lu"
#else
#  define PRIPPID "u"
#endif
  DC_DEBUG(("message with PPID = %" PRIPPID
            ", SID = %d, flags: 0x%04x due to error = 0x%08x",
            ntohl(ssfe->ssfe_info.snd_ppid), ssfe->ssfe_info.snd_sid,
            ssfe->ssfe_info.snd_flags, ssfe->ssfe_error));
#undef PRIPPID
  n = ssfe->ssfe_length - sizeof(struct sctp_send_failed_event);
  for (i = 0; i < n; ++i) {
    DC_DEBUG((" 0x%02x", ssfe->ssfe_data[i]));
  }
}

bool DataChannelConnectionUsrsctp::ResetStreams(nsTArray<uint16_t>& aStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  DC_DEBUG(("%s %p: Sending outgoing stream reset for %zu streams", __func__,
            this, aStreams.Length()));
  if (aStreams.IsEmpty()) {
    DC_DEBUG(("No streams to reset"));
    return false;
  }
  const size_t len =
      sizeof(sctp_reset_streams) + (aStreams.Length()) * sizeof(uint16_t);
  struct sctp_reset_streams* srs = static_cast<struct sctp_reset_streams*>(
      moz_xmalloc(len));  // infallible malloc
  memset(srs, 0, len);
  srs->srs_flags = SCTP_STREAM_RESET_OUTGOING;
  srs->srs_number_streams = aStreams.Length();
  for (size_t i = 0; i < aStreams.Length(); ++i) {
    srs->srs_stream_list[i] = aStreams[i];
  }
  if (usrsctp_setsockopt(mSocket, IPPROTO_SCTP, SCTP_RESET_STREAMS, srs,
                         (socklen_t)len) < 0) {
    DC_ERROR(("***failed: setsockopt RESET, errno %d", errno));
    // if errno == EALREADY, this is normal - we can't send another reset
    // with one pending.
    // When we get an incoming reset (which may be a response to our
    // outstanding one), see if we have any pending outgoing resets and
    // send them
  } else {
    aStreams.Clear();
  }
  free(srs);
  return aStreams.Length() == 0;
}

void DataChannelConnectionUsrsctp::HandleStreamResetEvent(
    const struct sctp_stream_reset_event* strrst) {
  std::vector<uint16_t> streamsReset;

  if (!(strrst->strreset_flags & SCTP_STREAM_RESET_DENIED) &&
      !(strrst->strreset_flags & SCTP_STREAM_RESET_FAILED)) {
    size_t n =
        (strrst->strreset_length - sizeof(struct sctp_stream_reset_event)) /
        sizeof(uint16_t);
    for (size_t i = 0; i < n; ++i) {
      streamsReset.push_back(strrst->strreset_stream_list[i]);
    }
  }

  if (strrst->strreset_flags & SCTP_STREAM_RESET_INCOMING_SSN) {
    OnStreamsReset(std::move(streamsReset));
  } else if (strrst->strreset_flags & SCTP_STREAM_RESET_OUTGOING_SSN) {
    OnStreamsResetComplete(std::move(streamsReset));
  }
}

void DataChannelConnectionUsrsctp::HandleStreamChangeEvent(
    const struct sctp_stream_change_event* strchg) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (strchg->strchange_flags == SCTP_STREAM_CHANGE_DENIED) {
    DC_ERROR(("*** Failed increasing number of streams from %u (%u/%u)",
              mNegotiatedIdLimit, strchg->strchange_instrms,
              strchg->strchange_outstrms));
    // XXX FIX! notify pending opens of failure
    return;
  }
  if (strchg->strchange_instrms > mNegotiatedIdLimit) {
    DC_DEBUG(("Other side increased streams from %u to %u", mNegotiatedIdLimit,
              strchg->strchange_instrms));
  }
  uint16_t old_limit = mNegotiatedIdLimit;
  uint16_t new_limit =
      std::min((uint16_t)MAX_NUM_STREAMS,
               std::max(strchg->strchange_outstrms, strchg->strchange_instrms));
  if (new_limit > mNegotiatedIdLimit) {
    DC_DEBUG(("Increasing number of streams from %u to %u - adding %u (in: %u)",
              old_limit, new_limit, new_limit - old_limit,
              strchg->strchange_instrms));
    // make sure both are the same length
    mNegotiatedIdLimit = new_limit;
    DC_DEBUG(("New length = %u (was %u)", mNegotiatedIdLimit, old_limit));
    // Re-process any channels waiting for streams.
    // Linear search, but we don't increase channels often and
    // the array would only get long in case of an app error normally

    // Make sure we request enough streams if there's a big jump in streams
    // Could make a more complex API for OpenXxxFinish() and avoid this loop
    auto channels = mChannels.GetAll();
    size_t num_needed =
        channels.Length() ? (channels.LastElement()->mStream + 1) : 0;
    Maybe<uint16_t> num_desired;
    MOZ_ASSERT(num_needed != INVALID_STREAM);
    if (num_needed > new_limit) {
      // Round up to a multiple of 16, or cap out
      num_desired =
          Some(std::min(16 * (num_needed / 16 + 1), (size_t)MAX_NUM_STREAMS));
      DC_DEBUG(("Not enough new streams, asking for %u", *num_desired));
    } else if (strchg->strchange_outstrms < strchg->strchange_instrms) {
      num_desired = Some(strchg->strchange_instrms);
      DC_DEBUG(("Requesting %u output streams to match partner", *num_desired));
    }

    if (num_desired.isSome()) {
      RaiseStreamLimitTo(*num_desired);
    }

    ProcessQueuedOpens();
  }
  // else probably not a change in # of streams

  if ((strchg->strchange_flags & SCTP_STREAM_CHANGE_DENIED) ||
      (strchg->strchange_flags & SCTP_STREAM_CHANGE_FAILED)) {
    // Other side denied our request. Need to AnnounceClosed some stuff.
    for (auto& channel : mChannels.GetAll()) {
      if (channel->mStream >= mNegotiatedIdLimit) {
        /* XXX: Signal to the other end. */
        FinishClose_s(channel);
        // maybe fire onError (bug 843625)
      }
    }
  }
}

void DataChannelConnectionUsrsctp::HandleNotification(
    const union sctp_notification* notif, size_t n) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (notif->sn_header.sn_length != (uint32_t)n) {
    return;
  }
  switch (notif->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:
      HandleAssociationChangeEvent(&(notif->sn_assoc_change));
      break;
    case SCTP_PEER_ADDR_CHANGE:
      HandlePeerAddressChangeEvent(&(notif->sn_paddr_change));
      break;
    case SCTP_REMOTE_ERROR:
      HandleRemoteErrorEvent(&(notif->sn_remote_error));
      break;
    case SCTP_SHUTDOWN_EVENT:
      HandleShutdownEvent(&(notif->sn_shutdown_event));
      break;
    case SCTP_ADAPTATION_INDICATION:
      HandleAdaptationIndication(&(notif->sn_adaptation_event));
      break;
    case SCTP_AUTHENTICATION_EVENT:
      DC_DEBUG(("SCTP_AUTHENTICATION_EVENT"));
      break;
    case SCTP_SENDER_DRY_EVENT:
      // DC_DEBUG(("SCTP_SENDER_DRY_EVENT"));
      break;
    case SCTP_NOTIFICATIONS_STOPPED_EVENT:
      DC_DEBUG(("SCTP_NOTIFICATIONS_STOPPED_EVENT"));
      break;
    case SCTP_PARTIAL_DELIVERY_EVENT:
      HandlePartialDeliveryEvent(&(notif->sn_pdapi_event));
      break;
    case SCTP_SEND_FAILED_EVENT:
      HandleSendFailedEvent(&(notif->sn_send_failed_event));
      break;
    case SCTP_STREAM_RESET_EVENT:
      HandleStreamResetEvent(&(notif->sn_strreset_event));
      break;
    case SCTP_ASSOC_RESET_EVENT:
      DC_DEBUG(("SCTP_ASSOC_RESET_EVENT"));
      break;
    case SCTP_STREAM_CHANGE_EVENT:
      HandleStreamChangeEvent(&(notif->sn_strchange_event));
      break;
    default:
      DC_ERROR(("unknown SCTP event: %u", (uint32_t)notif->sn_header.sn_type));
      break;
  }
}

int DataChannelConnectionUsrsctp::ReceiveCallback(struct socket* sock,
                                                  void* data, size_t datalen,
                                                  struct sctp_rcvinfo rcv,
                                                  int flags) {
  MOZ_ASSERT(!NS_IsMainThread());
  DC_DEBUG(("In ReceiveCallback"));

  mSTS->Dispatch(NS_NewRunnableFunction(
      "DataChannelConnection::ReceiveCallback",
      [data, datalen, rcv, flags, this,
       self = RefPtr<DataChannelConnection>(this)]() mutable {
        if (!data) {
          DC_DEBUG(("ReceiveCallback: SCTP has finished shutting down"));
        } else {
          if (flags & MSG_NOTIFICATION) {
            HandleNotification(static_cast<union sctp_notification*>(data),
                               datalen);
          } else {
            // NOTE: When interleaved mode is in use, rcv.rcv_ssn holds the
            // message id instead of the stream sequence number, based on a read
            // of the usrsctp code.
            HandleMessageChunk(data, datalen, ntohl(rcv.rcv_ppid), rcv.rcv_sid,
                               rcv.rcv_ssn, flags);
          }
          // sctp allocates 'data' with malloc(), and expects the receiver to
          // free it.
          // It would be nice if it were possible to eliminate a copy by passing
          // ownership here, but because DATA messages end up in an nsCString,
          // and ncCString requires null termination (which usrsctp does not
          // do), we _have_ to make a copy somewhere. That might as well be
          // here. The downstream code can avoid further copies in whatever way
          // makes sense.
          free(data);
        }
      }));

  // usrsctp defines the callback as returning an int, but doesn't use it
  return 1;
}

// Returns a POSIX error code directly instead of setting errno.
int DataChannelConnectionUsrsctp::SendMsgInternal(OutgoingMsg& msg,
                                                  size_t* aWritten) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  struct sctp_sendv_spa info = {};
  // General flags
  info.sendv_flags = SCTP_SEND_SNDINFO_VALID;

  // Set stream identifier and protocol identifier
  info.sendv_sndinfo.snd_sid = msg.GetMetadata().mStreamId;
  info.sendv_sndinfo.snd_ppid = htonl(msg.GetMetadata().mPpid);

  if (msg.GetMetadata().mUnordered) {
    info.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
  }

  // Partial reliability policy, lifetime and rtx are mutually exclusive
  msg.GetMetadata().mMaxLifetimeMs.apply([&](auto value) {
    info.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
    info.sendv_prinfo.pr_value = value;
    info.sendv_flags |= SCTP_SEND_PRINFO_VALID;
  });

  msg.GetMetadata().mMaxRetransmissions.apply([&](auto value) {
    info.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
    info.sendv_prinfo.pr_value = value;
    info.sendv_flags |= SCTP_SEND_PRINFO_VALID;
  });

  // Send until buffer is empty
  Span<const uint8_t> chunk = msg.GetRemainingData();
  do {
    if (chunk.Length() <= DATA_CHANNEL_MAX_BINARY_FRAGMENT) {
      // Last chunk!
      info.sendv_sndinfo.snd_flags |= SCTP_EOR;
    } else {
      chunk = chunk.To(DATA_CHANNEL_MAX_BINARY_FRAGMENT);
    }

    // Send (or try at least)
    // SCTP will return EMSGSIZE if the message is bigger than the buffer
    // size (or EAGAIN if there isn't space). However, we can avoid EMSGSIZE
    // by carefully crafting small enough message chunks.
    const ssize_t writtenOrError = usrsctp_sendv(
        mSocket, chunk.Elements(), chunk.Length(), nullptr, 0, (void*)&info,
        (socklen_t)sizeof(struct sctp_sendv_spa), SCTP_SENDV_SPA, 0);

    if (writtenOrError < 0) {
      return errno;
    }

    const size_t written = writtenOrError;

    if (aWritten &&
        msg.GetMetadata().mPpid != DATA_CHANNEL_PPID_DOMSTRING_EMPTY &&
        msg.GetMetadata().mPpid != DATA_CHANNEL_PPID_BINARY_EMPTY) {
      *aWritten += written;
    }
    DC_DEBUG(("Sent buffer (written=%zu, len=%zu, left=%zu)", written,
              chunk.Length(), msg.GetRemainingData().Length() - written));

    // TODO: Remove once resolved
    // (https://github.com/sctplab/usrsctp/issues/132)
    if (written == 0) {
      DC_ERROR(("@tuexen: usrsctp_sendv returned 0"));
      return EAGAIN;
    }

    // Update buffer position
    msg.Advance(written);

    // If not all bytes have been written, this obviously means that usrsctp's
    // buffer is full and we need to try again later.
    if (written < chunk.Length()) {
      return EAGAIN;
    }

    chunk = msg.GetRemainingData();
  } while (chunk.Length() > 0);

  return 0;
}

// Returns a POSIX error code directly instead of setting errno.
int DataChannelConnectionUsrsctp::SendMsgInternalOrBuffer(
    nsTArray<OutgoingMsg>& buffer, OutgoingMsg&& msg, bool* buffered,
    size_t* aWritten) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  NS_WARNING_ASSERTION(msg.GetLength() > 0, "Length is 0?!");

  int error = 0;
  bool need_buffering = false;

  if (buffer.IsEmpty() &&
      (mSendInterleaved || mPendingType == PendingType::None)) {
    error = SendMsgInternal(msg, aWritten);
    switch (error) {
      case 0:
        break;
      case EAGAIN:
#if (EAGAIN != EWOULDBLOCK)
      case EWOULDBLOCK:
#endif
        need_buffering = true;
        break;
      default:
        DC_ERROR(("error %d on sending", error));
        break;
    }
  } else {
    need_buffering = true;
  }

  if (need_buffering) {
    // queue data for resend!  And queue any further data for the stream until
    // it is...
    buffer.EmplaceBack(std::move(msg));
    DC_DEBUG(("Queued %zu buffers (left=%zu, total=%zu)", buffer.Length(),
              buffer.LastElement().GetLength(), msg.GetLength()));
    if (buffered) {
      *buffered = true;
    }
    return 0;
  }

  if (buffered) {
    *buffered = false;
  }
  return error;
}

int DataChannelConnectionUsrsctp::SendMessage(DataChannel& aChannel,
                                              OutgoingMsg&& aMsg) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  bool buffered;
  if (aMsg.GetMetadata().mPpid == DATA_CHANNEL_PPID_CONTROL) {
    int error = SendMsgInternalOrBuffer(mBufferedControl, std::move(aMsg),
                                        &buffered, nullptr);
    // Set pending type (if buffered)
    if (!error && buffered && mPendingType == PendingType::None) {
      mPendingType = PendingType::Dcep;
    }
    return error;
  }

  size_t written = 0;
  if (const int error = SendMsgInternalOrBuffer(
          aChannel.mBufferedData, std::move(aMsg), &buffered, &written);
      error) {
    return error;
  }

  if (written &&
      aMsg.GetMetadata().mPpid != DATA_CHANNEL_PPID_DOMSTRING_EMPTY &&
      aMsg.GetMetadata().mPpid != DATA_CHANNEL_PPID_BINARY_EMPTY) {
    aChannel.DecrementBufferedAmount(written);
  }

  // Set pending type and stream index (if buffered)
  if (buffered && mPendingType == PendingType::None) {
    mPendingType = PendingType::Data;
    mCurrentStream = aChannel.mStream;
  }

  return 0;
}

}  // namespace mozilla
