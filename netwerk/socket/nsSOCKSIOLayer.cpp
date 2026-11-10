/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSOCKSIOLayer.h"

#include "mozilla/Components.h"
#include "mozilla/Logging.h"
#include "mozilla/net/DNS.h"
#include "nsCRT.h"
#include "nsDNSService2.h"
#include "nsICancelable.h"
#include "nsIDNSListener.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIFile.h"
#include "nsIFileProtocolHandler.h"
#include "nsISocketProvider.h"
#include "nsNamedPipeIOLayer.h"
#include "nsNetCID.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nspr.h"
#include "private/pprio.h"

using mozilla::LogLevel;
using namespace mozilla::net;

static PRDescIdentity nsSOCKSIOLayerIdentity;
static PRIOMethods nsSOCKSIOLayerMethods;
static bool firstTime = true;
static bool ipv6Supported = true;

static mozilla::LazyLogModule gSOCKSLog("SOCKS");
#define LOGDEBUG(args) MOZ_LOG(gSOCKSLog, mozilla::LogLevel::Debug, args)
#define LOGERROR(args) MOZ_LOG(gSOCKSLog, mozilla::LogLevel::Error, args)

class nsSOCKSSocketInfo : public nsIDNSListener {
  enum State {
    SOCKS_INITIAL,
    SOCKS_DNS_IN_PROGRESS,
    SOCKS_DNS_COMPLETE,
    SOCKS_CONNECTING_TO_PROXY,
    SOCKS4_WRITE_CONNECT_REQUEST,
    SOCKS4_READ_CONNECT_RESPONSE,
    SOCKS5_WRITE_AUTH_REQUEST,
    SOCKS5_READ_AUTH_RESPONSE,
    SOCKS5_WRITE_USERNAME_REQUEST,
    SOCKS5_READ_USERNAME_RESPONSE,
    SOCKS5_WRITE_CONNECT_REQUEST,
    SOCKS5_READ_CONNECT_RESPONSE_TOP,
    SOCKS5_READ_CONNECT_RESPONSE_BOTTOM,
    SOCKS_CONNECTED,
    SOCKS_FAILED
  };

  // A buffer of 520 bytes should be enough for any request and response
  // in case of SOCKS4 as well as SOCKS5
  static const uint32_t BUFFER_SIZE = 520;
  static const uint32_t MAX_HOSTNAME_LEN = 255;
  static const uint32_t MAX_USERNAME_LEN = 255;
  static const uint32_t MAX_PASSWORD_LEN = 255;

 public:
  nsSOCKSSocketInfo();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSLISTENER

  void Init(int32_t version, int32_t family, nsIProxyInfo* proxy,
            const char* destinationHost, uint32_t flags, uint32_t tlsFlags);

  void SetConnectTimeout(PRIntervalTime to);
  PRStatus DoHandshake(PRFileDesc* fd, int16_t oflags = -1);
  int16_t GetPollFlags() const;
  bool IsConnected() const { return mState == SOCKS_CONNECTED; }
  bool HasPendingError() const { return mPendingError != 0; }
  void ForgetFD() { mFD = nullptr; }
  void SetNamedPipeFD(PRFileDesc* fd) { mFD = fd; }

  void GetExternalProxyAddr(NetAddr& aExternalProxyAddr);
  void GetDestinationAddr(NetAddr& aDestinationAddr);
  void SetDestinationAddr(const NetAddr& aDestinationAddr);

 private:
  virtual ~nsSOCKSSocketInfo() {
    ForgetFD();
    HandshakeFinished();
  }

  void HandshakeFinished(PRErrorCode err = 0);
  PRStatus StartDNS(PRFileDesc* fd);
  PRStatus ConnectToProxy(PRFileDesc* fd);
  void FixupAddressFamily(PRFileDesc* fd, NetAddr* proxy);
  PRStatus ContinueConnectingToProxy(PRFileDesc* fd, int16_t oflags);
  PRStatus WriteV4ConnectRequest();
  PRStatus ReadV4ConnectResponse();
  PRStatus WriteV5AuthRequest();
  PRStatus ReadV5AuthResponse();
  PRStatus WriteV5UsernameRequest();
  PRStatus ReadV5UsernameResponse();
  PRStatus WriteV5ConnectRequest();
  PRStatus ReadV5AddrTypeAndLength(uint8_t* type, uint32_t* len);
  PRStatus ReadV5ConnectResponseTop();
  PRStatus ReadV5ConnectResponseBottom();

  uint8_t ReadUint8();
  uint16_t ReadUint16();
  uint32_t ReadUint32();
  void ReadNetAddr(NetAddr* addr, uint16_t fam);
  void ReadNetPort(NetAddr* addr);

  void WantRead(uint32_t sz);
  PRStatus ReadFromSocket(PRFileDesc* fd);
  PRStatus WriteToSocket(PRFileDesc* fd);

  bool IsLocalProxy() {
    nsAutoCString proxyHost;
    mProxy->GetHost(proxyHost);
    return IsHostLocalTarget(proxyHost);
  }

  nsresult SetLocalProxyPath(const nsACString& aLocalProxyPath,
                             NetAddr* aProxyAddr) {
#ifdef XP_UNIX
    nsresult rv;
    MOZ_ASSERT(aProxyAddr);

    nsCOMPtr<nsIProtocolHandler> protocolHandler(
        mozilla::components::FileProtocolHandler::Service(&rv));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsCOMPtr<nsIFileProtocolHandler> fileHandler(
        do_QueryInterface(protocolHandler, &rv));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsCOMPtr<nsIFile> socketFile;
    rv = fileHandler->GetFileFromURLSpec(aLocalProxyPath,
                                         getter_AddRefs(socketFile));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsAutoCString path;
    if (NS_WARN_IF(NS_FAILED(rv = socketFile->GetNativePath(path)))) {
      return rv;
    }

    if (sizeof(aProxyAddr->local.path) <= path.Length()) {
      NS_WARNING("domain socket path too long.");
      return NS_ERROR_FAILURE;
    }

    aProxyAddr->raw.family = AF_UNIX;
    strcpy(aProxyAddr->local.path, path.get());

    return NS_OK;
#elif defined(XP_WIN)
    MOZ_ASSERT(aProxyAddr);

    if (sizeof(aProxyAddr->local.path) <= aLocalProxyPath.Length()) {
      NS_WARNING("pipe path too long.");
      return NS_ERROR_FAILURE;
    }

    aProxyAddr->raw.family = AF_LOCAL;
    strcpy(aProxyAddr->local.path, PromiseFlatCString(aLocalProxyPath).get());
    return NS_OK;
#else
    (void)aLocalProxyPath;
    (void)aProxyAddr;
    return NS_ERROR_NOT_IMPLEMENTED;
#endif
  }

  bool SetupNamedPipeLayer(PRFileDesc* fd) {
#if defined(XP_WIN)
    if (IsLocalProxy()) {
      // nsSOCKSIOLayer handshaking only works under blocking mode
      // unfortunately. Remember named pipe's FD to switch between modes.
      SetNamedPipeFD(fd->lower);
      return true;
    }
#endif
    return false;
  }

 private:
  State mState{SOCKS_INITIAL};
  uint8_t* mData{nullptr};
  uint8_t* mDataIoPtr{nullptr};
  uint32_t mDataLength{0};
  uint32_t mReadOffset{0};
  uint32_t mAmountToRead{0};
  nsCOMPtr<nsIDNSRecord> mDnsRec;
  nsCOMPtr<nsICancelable> mLookup;
  nsresult mLookupStatus{NS_ERROR_NOT_INITIALIZED};
  // Connect failure from OnLookupComplete, reported on the next handshake.
  PRErrorCode mPendingError{0};
  int32_t mPendingOSError{0};
  bool mProxyConnectAttempted{false};
  PRFileDesc* mFD{nullptr};

  nsCString mDestinationHost;
  nsCOMPtr<nsIProxyInfo> mProxy;
  int32_t mVersion{-1};  // SOCKS version 4 or 5
  int32_t mDestinationFamily{AF_INET};
  uint32_t mFlags{0};
  uint32_t mTlsFlags{0};
  NetAddr mInternalProxyAddr;
  NetAddr mExternalProxyAddr;
  NetAddr mDestinationAddr;
  PRIntervalTime mTimeout{PR_INTERVAL_NO_TIMEOUT};
  nsCString mProxyUsername;  // Cache, from mProxy
};

nsSOCKSSocketInfo::nsSOCKSSocketInfo() {
  mData = new uint8_t[BUFFER_SIZE];

  mInternalProxyAddr.raw.family = AF_INET;
  mInternalProxyAddr.inet.ip = htonl(INADDR_ANY);
  mInternalProxyAddr.inet.port = htons(0);

  mExternalProxyAddr.raw.family = AF_INET;
  mExternalProxyAddr.inet.ip = htonl(INADDR_ANY);
  mExternalProxyAddr.inet.port = htons(0);

  mDestinationAddr.raw.family = AF_INET;
  mDestinationAddr.inet.ip = htonl(INADDR_ANY);
  mDestinationAddr.inet.port = htons(0);
}

/* Helper template class to statically check that writes to a fixed-size
 * buffer are not going to overflow.
 *
 * Example usage:
 *   uint8_t real_buf[TOTAL_SIZE];
 *   Buffer<TOTAL_SIZE> buf(&real_buf);
 *   auto buf2 = buf.WriteUint16(1);
 *   auto buf3 = buf2.WriteUint8(2);
 *
 * It is possible to chain them, to limit the number of (error-prone)
 * intermediate variables:
 *   auto buf = Buffer<TOTAL_SIZE>(&real_buf)
 *              .WriteUint16(1)
 *              .WriteUint8(2);
 *
 * Debug builds assert when intermediate variables are reused:
 *   Buffer<TOTAL_SIZE> buf(&real_buf);
 *   auto buf2 = buf.WriteUint16(1);
 *   auto buf3 = buf.WriteUint8(2); // Asserts
 *
 * Strings can be written, given an explicit maximum length.
 *   buf.WriteString<MAX_STRING_LENGTH>(str);
 *
 * The Written() method returns how many bytes have been written so far:
 *   Buffer<TOTAL_SIZE> buf(&real_buf);
 *   auto buf2 = buf.WriteUint16(1);
 *   auto buf3 = buf2.WriteUint8(2);
 *   buf3.Written(); // returns 3.
 */
template <size_t Size>
class Buffer {
 public:
  Buffer() = default;

  explicit Buffer(uint8_t* aBuf, size_t aLength = 0)
      : mBuf(aBuf), mLength(aLength) {}

  template <size_t Size2>
  MOZ_IMPLICIT Buffer(const Buffer<Size2>& aBuf)
      : mBuf(aBuf.mBuf), mLength(aBuf.mLength) {
    static_assert(Size2 > Size, "Cannot cast buffer");
  }

  Buffer<Size - sizeof(uint8_t)> WriteUint8(uint8_t aValue) {
    return Write(aValue);
  }

  Buffer<Size - sizeof(uint16_t)> WriteUint16(uint16_t aValue) {
    return Write(aValue);
  }

  Buffer<Size - sizeof(uint32_t)> WriteUint32(uint32_t aValue) {
    return Write(aValue);
  }

  Buffer<Size - sizeof(uint16_t)> WriteNetPort(const NetAddr* aAddr) {
    return WriteUint16(aAddr->inet.port);
  }

  Buffer<Size - sizeof(IPv6Addr)> WriteNetAddr(const NetAddr* aAddr) {
    if (aAddr->raw.family == AF_INET) {
      return Write(aAddr->inet.ip);
    }
    if (aAddr->raw.family == AF_INET6) {
      return Write(aAddr->inet6.ip.u8);
    }
    MOZ_ASSERT_UNREACHABLE("Unknown address family");
    return *this;
  }

  template <size_t MaxLength>
  Buffer<Size - MaxLength> WriteString(const nsACString& aStr) {
    if (aStr.Length() > MaxLength) {
      return Buffer<Size - MaxLength>(nullptr);
    }
    return WritePtr<char, MaxLength>(aStr.Data(), aStr.Length());
  }

  size_t Written() {
    MOZ_ASSERT(mBuf);
    return mLength;
  }

  explicit operator bool() { return !!mBuf; }

 private:
  template <size_t Size2>
  friend class Buffer;

  template <typename T>
  Buffer<Size - sizeof(T)> Write(T& aValue) {
    return WritePtr<T, sizeof(T)>(&aValue, sizeof(T));
  }

  template <typename T, size_t Length>
  Buffer<Size - Length> WritePtr(const T* aValue, size_t aCopyLength) {
    static_assert(Size >= Length, "Cannot write that much");
    MOZ_ASSERT(aCopyLength <= Length);
    MOZ_ASSERT(mBuf);
    memcpy(mBuf, aValue, aCopyLength);
    Buffer<Size - Length> result(mBuf + aCopyLength, mLength + aCopyLength);
    mBuf = nullptr;
    mLength = 0;
    return result;
  }

  uint8_t* mBuf{nullptr};
  size_t mLength{0};
};

void nsSOCKSSocketInfo::Init(int32_t version, int32_t family,
                             nsIProxyInfo* proxy, const char* host,
                             uint32_t flags, uint32_t tlsFlags) {
  mVersion = version;
  mDestinationFamily = family;
  mProxy = proxy;
  mDestinationHost = host;
  mFlags = flags;
  mTlsFlags = tlsFlags;
  mProxy->GetUsername(mProxyUsername);  // cache
}

NS_IMPL_ISUPPORTS(nsSOCKSSocketInfo, nsIDNSListener)

void nsSOCKSSocketInfo::GetExternalProxyAddr(NetAddr& aExternalProxyAddr) {
  aExternalProxyAddr = mExternalProxyAddr;
}

void nsSOCKSSocketInfo::GetDestinationAddr(NetAddr& aDestinationAddr) {
  aDestinationAddr = mDestinationAddr;
}

void nsSOCKSSocketInfo::SetDestinationAddr(const NetAddr& aDestinationAddr) {
  mDestinationAddr = aDestinationAddr;
}

// There needs to be a means of distinguishing between connection errors
// that the SOCKS server reports when it rejects a connection request, and
// connection errors that happen while attempting to connect to the SOCKS
// server. Otherwise, Firefox will report incorrectly that the proxy server
// is refusing connections when a SOCKS request is rejected by the proxy.
// When a SOCKS handshake failure occurs, the PR error is set to
// PR_UNKNOWN_ERROR, and the real error code is returned via the OS error.
void nsSOCKSSocketInfo::HandshakeFinished(PRErrorCode err) {
  if (err == 0) {
    mState = SOCKS_CONNECTED;
#if defined(XP_WIN)
    // Switch back to nonblocking mode after finishing handshaking.
    if (IsLocalProxy() && mFD) {
      PRSocketOptionData opt_nonblock;
      opt_nonblock.option = PR_SockOpt_Nonblocking;
      opt_nonblock.value.non_blocking = PR_TRUE;
      PR_SetSocketOption(mFD, &opt_nonblock);
      mFD = nullptr;
    }
#endif
  } else {
    mState = SOCKS_FAILED;
    PR_SetError(PR_UNKNOWN_ERROR, err);
  }

  // We don't need the buffer any longer, so free it.
  delete[] mData;
  mData = nullptr;
  mDataIoPtr = nullptr;
  mDataLength = 0;
  mReadOffset = 0;
  mAmountToRead = 0;
  if (mLookup) {
    mLookup->Cancel(NS_ERROR_FAILURE);
    mLookup = nullptr;
  }
}

PRStatus nsSOCKSSocketInfo::StartDNS(PRFileDesc* fd) {
  MOZ_ASSERT(!mDnsRec && mState == SOCKS_INITIAL,
             "Must be in initial state to make DNS Lookup");

  // We are on the socket thread, where the DNS service can't be created
  // directly, so make sure it gets created on the main thread if needed.
  nsCOMPtr<nsIDNSService> dns = GetOrInitDNSService();
  if (!dns) {
    LOGERROR(("socks: DNS service unavailable"));
    PR_SetError(PR_BAD_ADDRESS_ERROR, 0);
    return PR_FAILURE;
  }

  nsCString proxyHost;
  mProxy->GetHost(proxyHost);

  mozilla::OriginAttributes attrs;

  mFD = fd;
  nsresult rv = dns->AsyncResolveNative(
      proxyHost, nsIDNSService::RESOLVE_TYPE_DEFAULT,
      nsIDNSService::RESOLVE_IGNORE_SOCKS_DNS, nullptr, this,
      mozilla::GetCurrentSerialEventTarget(), attrs, getter_AddRefs(mLookup));

  if (NS_FAILED(rv)) {
    LOGERROR(("socks: DNS lookup for SOCKS proxy %s failed", proxyHost.get()));
    PR_SetError(PR_BAD_ADDRESS_ERROR, 0);
    return PR_FAILURE;
  }
  mState = SOCKS_DNS_IN_PROGRESS;
  PR_SetError(PR_IN_PROGRESS_ERROR, 0);
  return PR_FAILURE;
}

static bool IsInProgress(PRErrorCode aError) {
  return aError == PR_WOULD_BLOCK_ERROR || aError == PR_IN_PROGRESS_ERROR;
}

NS_IMETHODIMP
nsSOCKSSocketInfo::OnLookupComplete(nsICancelable* aRequest,
                                    nsIDNSRecord* aRecord, nsresult aStatus) {
  MOZ_ASSERT(aRequest == mLookup, "wrong DNS query");
  mLookup = nullptr;
  mLookupStatus = aStatus;
  mDnsRec = aRecord;
  mState = SOCKS_DNS_COMPLETE;
  if (mFD) {
    if (ConnectToProxy(mFD) != PR_SUCCESS && !IsInProgress(PR_GetError())) {
      mPendingError = PR_GetError();
      mPendingOSError = PR_GetOSError();
    }
    ForgetFD();
  }
  return NS_OK;
}

PRStatus nsSOCKSSocketInfo::ConnectToProxy(PRFileDesc* fd) {
  PRStatus status;
  nsresult rv;

  MOZ_ASSERT(mState == SOCKS_DNS_COMPLETE, "Must have DNS to make connection!");

  if (NS_FAILED(mLookupStatus)) {
    PR_SetError(PR_BAD_ADDRESS_ERROR, 0);
    return PR_FAILURE;
  }

  // Try socks5 if the destination addrress is IPv6
  if (mVersion == 4 && mDestinationAddr.raw.family == AF_INET6) {
    mVersion = 5;
  }

  nsAutoCString proxyHost;
  mProxy->GetHost(proxyHost);

  int32_t proxyPort;
  mProxy->GetPort(&proxyPort);

  int32_t addresses = 0;
  do {
    if (IsLocalProxy()) {
      rv = SetLocalProxyPath(proxyHost, &mInternalProxyAddr);
      if (NS_FAILED(rv)) {
        LOGERROR(
            ("socks: unable to connect to SOCKS proxy, %s", proxyHost.get()));
        PR_SetError(PR_BAD_ADDRESS_ERROR, 0);
        return PR_FAILURE;
      }
    } else {
      nsCOMPtr<nsIDNSAddrRecord> record = do_QueryInterface(mDnsRec);
      MOZ_ASSERT(record);
      if (addresses++) {
        record->ReportUnusable(proxyPort);
      }

      rv = record->GetNextAddr(proxyPort, &mInternalProxyAddr);
      // No more addresses to try? If so, we'll need to bail
      if (NS_FAILED(rv)) {
        LOGERROR(
            ("socks: unable to connect to SOCKS proxy, %s", proxyHost.get()));
        // Otherwise the error from the last failed connect is still set.
        if (!mProxyConnectAttempted) {
          PR_SetError(PR_BAD_ADDRESS_ERROR, 0);
        }
        return PR_FAILURE;
      }

      LOGDEBUG(("socks: trying proxy server, %s:%hu",
                mInternalProxyAddr.ToString().get(),
                ntohs(mInternalProxyAddr.inet.port)));
    }

    NetAddr proxy = mInternalProxyAddr;
    FixupAddressFamily(fd, &proxy);
    PRNetAddr prProxy;
    NetAddrToPRNetAddr(&proxy, &prProxy);
    mProxyConnectAttempted = true;
    status = fd->lower->methods->connect(fd->lower, &prProxy, mTimeout);
    if (status != PR_SUCCESS) {
      PRErrorCode c = PR_GetError();

      // If EINPROGRESS, return now and check back later after polling
      if (IsInProgress(c)) {
        mState = SOCKS_CONNECTING_TO_PROXY;
        return status;
      }
      if (IsLocalProxy()) {
        LOGERROR(("socks: connect to domain socket failed (%d)", c));
        PR_SetError(PR_CONNECT_REFUSED_ERROR, 0);
        mState = SOCKS_FAILED;
        return status;
      }
    }
  } while (status != PR_SUCCESS);

#if defined(XP_WIN)
  // Switch to blocking mode during handshaking
  if (IsLocalProxy() && mFD) {
    PRSocketOptionData opt_nonblock;
    opt_nonblock.option = PR_SockOpt_Nonblocking;
    opt_nonblock.value.non_blocking = PR_FALSE;
    PR_SetSocketOption(mFD, &opt_nonblock);
  }
#endif

  // Connected now, start SOCKS
  if (mVersion == 4) return WriteV4ConnectRequest();
  return WriteV5AuthRequest();
}

void nsSOCKSSocketInfo::FixupAddressFamily(PRFileDesc* fd, NetAddr* proxy) {
  int32_t proxyFamily = mInternalProxyAddr.raw.family;
  // Do nothing if the address family is already matched
  if (proxyFamily == mDestinationFamily) {
    return;
  }
  // If the system does not support IPv6 and the proxy address is IPv6,
  // We can do nothing here.
  if (proxyFamily == AF_INET6 && !ipv6Supported) {
    return;
  }
  // If the system does not support IPv6 and the destination address is
  // IPv6, convert IPv4 address to IPv4-mapped IPv6 address to satisfy
  // the emulation layer
  if (mDestinationFamily == AF_INET6 && !ipv6Supported) {
    proxy->inet6.family = AF_INET6;
    proxy->inet6.port = mInternalProxyAddr.inet.port;
    uint8_t* proxyp = proxy->inet6.ip.u8;
    memset(proxyp, 0, 10);
    memset(proxyp + 10, 0xff, 2);
    memcpy(proxyp + 12, (char*)&mInternalProxyAddr.inet.ip, 4);
    // mDestinationFamily should not be updated
    return;
  }
  // There's no PR_NSPR_IO_LAYER required when using named pipe,
  // we simply ignore the TCP family here.
  if (SetupNamedPipeLayer(fd)) {
    return;
  }

  // Get an OS native handle from a specified FileDesc
  PROsfd osfd = PR_FileDesc2NativeHandle(fd);
  if (osfd == -1) {
    return;
  }

  // Create a new FileDesc with a specified family
  PRFileDesc* tmpfd = PR_OpenTCPSocket(proxyFamily);
  if (!tmpfd) {
    return;
  }
  PROsfd newsd = PR_FileDesc2NativeHandle(tmpfd);
  if (newsd == -1) {
    PR_Close(tmpfd);
    return;
  }
  // Must succeed because PR_FileDesc2NativeHandle succeeded
  fd = PR_GetIdentitiesLayer(fd, PR_NSPR_IO_LAYER);
  MOZ_ASSERT(fd);
  // Swap OS native handles
  PR_ChangeFileDescNativeHandle(fd, newsd);
  PR_ChangeFileDescNativeHandle(tmpfd, osfd);
  // Close temporary FileDesc which is now associated with
  // old OS native handle
  PR_Close(tmpfd);
  mDestinationFamily = proxyFamily;
}

PRStatus nsSOCKSSocketInfo::ContinueConnectingToProxy(PRFileDesc* fd,
                                                      int16_t oflags) {
  PRStatus status;

  MOZ_ASSERT(mState == SOCKS_CONNECTING_TO_PROXY,
             "Continuing connection in wrong state!");

  LOGDEBUG(("socks: continuing connection to proxy"));

  status = fd->lower->methods->connectcontinue(fd->lower, oflags);
  if (status != PR_SUCCESS) {
    PRErrorCode c = PR_GetError();
    if (!IsInProgress(c)) {
      // A connection failure occured, try another address
      mState = SOCKS_DNS_COMPLETE;
      return ConnectToProxy(fd);
    }

    // We're still connecting
    return PR_FAILURE;
  }

  // Connected now, start SOCKS
  if (mVersion == 4) return WriteV4ConnectRequest();
  return WriteV5AuthRequest();
}

PRStatus nsSOCKSSocketInfo::WriteV4ConnectRequest() {
  if (mProxyUsername.Length() > MAX_USERNAME_LEN) {
    LOGERROR(("socks username is too long"));
    HandshakeFinished(PR_UNKNOWN_ERROR);
    return PR_FAILURE;
  }

  NetAddr* addr = &mDestinationAddr;
  int32_t proxy_resolve;

  MOZ_ASSERT(mState == SOCKS_CONNECTING_TO_PROXY, "Invalid state!");

  proxy_resolve = mFlags & nsISocketProvider::PROXY_RESOLVES_HOST;

  mDataLength = 0;
  mState = SOCKS4_WRITE_CONNECT_REQUEST;

  LOGDEBUG(("socks4: sending connection request (socks4a resolve? %s)",
            proxy_resolve ? "yes" : "no"));

  // Send a SOCKS 4 connect request.
  auto buf = Buffer<BUFFER_SIZE>(mData)
                 .WriteUint8(0x04)  // version -- 4
                 .WriteUint8(0x01)  // command -- connect
                 .WriteNetPort(addr);

  // We don't have anything more to write after the if, so we can
  // use a buffer with no further writes allowed.
  Buffer<0> buf3;
  if (proxy_resolve) {
    // Add the full name, null-terminated, to the request
    // according to SOCKS 4a. A fake IP address, with the first
    // four bytes set to 0 and the last byte set to something other
    // than 0, is used to notify the proxy that this is a SOCKS 4a
    // request. This request type works for Tor and perhaps others.
    // Passwords not supported by V4.
    auto buf2 =
        buf.WriteUint32(htonl(0x00000001))  // Fake IP
            .WriteString<MAX_USERNAME_LEN>(mProxyUsername)
            .WriteUint8(0x00)  // Null-terminate username
            .WriteString<MAX_HOSTNAME_LEN>(mDestinationHost);  // Hostname
    if (!buf2) {
      LOGERROR(("socks4: destination host name is too long!"));
      HandshakeFinished(PR_BAD_ADDRESS_ERROR);
      return PR_FAILURE;
    }
    buf3 = buf2.WriteUint8(0x00);
  } else if (addr->raw.family == AF_INET) {
    // Passwords not supported by V4.
    buf3 = buf.WriteNetAddr(addr)  // Add the IPv4 address
               .WriteString<MAX_USERNAME_LEN>(mProxyUsername)
               .WriteUint8(0x00);  // Null-terminate username
  } else {
    LOGERROR(("socks: SOCKS 4 can only handle IPv4 addresses!"));
    HandshakeFinished(PR_BAD_ADDRESS_ERROR);
    return PR_FAILURE;
  }

  mDataLength = buf3.Written();
  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV4ConnectResponse() {
  MOZ_ASSERT(mState == SOCKS4_READ_CONNECT_RESPONSE,
             "Handling SOCKS 4 connection reply in wrong state!");
  MOZ_ASSERT(mDataLength == 8, "SOCKS 4 connection reply must be 8 bytes!");

  LOGDEBUG(("socks4: checking connection reply"));

  if (ReadUint8() != 0x00) {
    LOGERROR(("socks4: wrong connection reply"));
    HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
    return PR_FAILURE;
  }

  // See if our connection request was granted
  if (ReadUint8() == 90) {
    LOGDEBUG(("socks4: connection successful!"));
    HandshakeFinished();
    return PR_SUCCESS;
  }

  LOGERROR(("socks4: unable to connect"));
  HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
  return PR_FAILURE;
}

PRStatus nsSOCKSSocketInfo::WriteV5AuthRequest() {
  MOZ_ASSERT(mVersion == 5, "SOCKS version must be 5!");

  mDataLength = 0;
  mState = SOCKS5_WRITE_AUTH_REQUEST;

  // Send an initial SOCKS 5 greeting
  LOGDEBUG(("socks5: sending auth methods"));
  mDataLength = Buffer<BUFFER_SIZE>(mData)
                    .WriteUint8(0x05)  // version -- 5
                    .WriteUint8(0x01)  // # of auth methods -- 1
                    // Use authenticate iff we have a proxy username.
                    .WriteUint8(mProxyUsername.IsEmpty() ? 0x00 : 0x02)
                    .Written();

  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV5AuthResponse() {
  MOZ_ASSERT(mState == SOCKS5_READ_AUTH_RESPONSE,
             "Handling SOCKS 5 auth method reply in wrong state!");
  MOZ_ASSERT(mDataLength == 2, "SOCKS 5 auth method reply must be 2 bytes!");

  LOGDEBUG(("socks5: checking auth method reply"));

  // Check version number
  if (ReadUint8() != 0x05) {
    LOGERROR(("socks5: unexpected version in the reply"));
    HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
    return PR_FAILURE;
  }

  // Make sure our authentication choice was accepted,
  // and continue accordingly
  uint8_t authMethod = ReadUint8();
  if (mProxyUsername.IsEmpty() && authMethod == 0x00) {  // no auth
    LOGDEBUG(("socks5: server allows connection without authentication"));
    return WriteV5ConnectRequest();
  }
  if (!mProxyUsername.IsEmpty() && authMethod == 0x02) {  // username/pw
    LOGDEBUG(("socks5: auth method accepted by server"));
    return WriteV5UsernameRequest();
  }  // 0xFF signals error
  LOGERROR(("socks5: server did not accept our authentication method"));
  HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
  return PR_FAILURE;
}

PRStatus nsSOCKSSocketInfo::WriteV5UsernameRequest() {
  MOZ_ASSERT(mVersion == 5, "SOCKS version must be 5!");

  if (mProxyUsername.Length() > MAX_USERNAME_LEN) {
    LOGERROR(("socks username is too long"));
    HandshakeFinished(PR_UNKNOWN_ERROR);
    return PR_FAILURE;
  }

  nsCString password;
  mProxy->GetPassword(password);
  if (password.Length() > MAX_PASSWORD_LEN) {
    LOGERROR(("socks password is too long"));
    HandshakeFinished(PR_UNKNOWN_ERROR);
    return PR_FAILURE;
  }

  mDataLength = 0;
  mState = SOCKS5_WRITE_USERNAME_REQUEST;

  // RFC 1929 Username/password auth for SOCKS 5
  LOGDEBUG(("socks5: sending username and password"));
  mDataLength = Buffer<BUFFER_SIZE>(mData)
                    .WriteUint8(0x01)                     // version 1 (not 5)
                    .WriteUint8(mProxyUsername.Length())  // username length
                    .WriteString<MAX_USERNAME_LEN>(mProxyUsername)  // username
                    .WriteUint8(password.Length())  // password length
                    .WriteString<MAX_PASSWORD_LEN>(
                        password)  // password. WARNING: Sent unencrypted!
                    .Written();

  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV5UsernameResponse() {
  MOZ_ASSERT(mState == SOCKS5_READ_USERNAME_RESPONSE,
             "Handling SOCKS 5 username/password reply in wrong state!");

  MOZ_ASSERT(mDataLength == 2, "SOCKS 5 username reply must be 2 bytes");

  // Check version number, must be 1 (not 5)
  if (ReadUint8() != 0x01) {
    LOGERROR(("socks5: unexpected version in the reply"));
    HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
    return PR_FAILURE;
  }

  // Check whether username/password were accepted
  if (ReadUint8() != 0x00) {  // 0 = success
    LOGERROR(("socks5: username/password not accepted"));
    HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
    return PR_FAILURE;
  }

  LOGDEBUG(("socks5: username/password accepted by server"));

  return WriteV5ConnectRequest();
}

PRStatus nsSOCKSSocketInfo::WriteV5ConnectRequest() {
  // Send SOCKS 5 connect request
  NetAddr* addr = &mDestinationAddr;
  int32_t proxy_resolve;
  proxy_resolve = mFlags & nsISocketProvider::PROXY_RESOLVES_HOST;

  LOGDEBUG(("socks5: sending connection request (socks5 resolve? %s)",
            proxy_resolve ? "yes" : "no"));

  mDataLength = 0;
  mState = SOCKS5_WRITE_CONNECT_REQUEST;

  auto buf = Buffer<BUFFER_SIZE>(mData)
                 .WriteUint8(0x05)   // version -- 5
                 .WriteUint8(0x01)   // command -- connect
                 .WriteUint8(0x00);  // reserved

  // We're writing a net port after the if, so we need a buffer allowing
  // to write that much.
  Buffer<sizeof(uint16_t)> buf2;
  // Add the address to the SOCKS 5 request. SOCKS 5 supports several
  // address types, so we pick the one that works best for us.
  if (proxy_resolve) {
    // Add the host name. Only a single byte is used to store the length,
    // so we must prevent long names from being used.
    buf2 = buf.WriteUint8(0x03)  // addr type -- domainname
               .WriteUint8(mDestinationHost.Length())             // name length
               .WriteString<MAX_HOSTNAME_LEN>(mDestinationHost);  // Hostname
    if (!buf2) {
      LOGERROR(("socks5: destination host name is too long!"));
      HandshakeFinished(PR_BAD_ADDRESS_ERROR);
      return PR_FAILURE;
    }
  } else if (addr->raw.family == AF_INET) {
    buf2 = buf.WriteUint8(0x01)  // addr type -- IPv4
               .WriteNetAddr(addr);
  } else if (addr->raw.family == AF_INET6) {
    buf2 = buf.WriteUint8(0x04)  // addr type -- IPv6
               .WriteNetAddr(addr);
  } else {
    LOGERROR(("socks5: destination address of unknown type!"));
    HandshakeFinished(PR_BAD_ADDRESS_ERROR);
    return PR_FAILURE;
  }

  auto buf3 = buf2.WriteNetPort(addr);  // port
  mDataLength = buf3.Written();

  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV5AddrTypeAndLength(uint8_t* type,
                                                    uint32_t* len) {
  MOZ_ASSERT(mState == SOCKS5_READ_CONNECT_RESPONSE_TOP ||
                 mState == SOCKS5_READ_CONNECT_RESPONSE_BOTTOM,
             "Invalid state!");
  MOZ_ASSERT(mDataLength >= 5,
             "SOCKS 5 connection reply must be at least 5 bytes!");

  // Seek to the address location
  mReadOffset = 3;

  *type = ReadUint8();

  switch (*type) {
    case 0x01:  // ipv4
      *len = 4 - 1;
      break;
    case 0x04:  // ipv6
      *len = 16 - 1;
      break;
    case 0x03:  // fqdn
      *len = ReadUint8();
      break;
    default:  // wrong address type
      LOGERROR(("socks5: wrong address type in connection reply!"));
      return PR_FAILURE;
  }

  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV5ConnectResponseTop() {
  uint8_t res;
  uint32_t len;

  MOZ_ASSERT(mState == SOCKS5_READ_CONNECT_RESPONSE_TOP, "Invalid state!");
  MOZ_ASSERT(mDataLength == 5,
             "SOCKS 5 connection reply must be exactly 5 bytes!");

  LOGDEBUG(("socks5: checking connection reply"));

  // Check version number
  if (ReadUint8() != 0x05) {
    LOGERROR(("socks5: unexpected version in the reply"));
    HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
    return PR_FAILURE;
  }

  // Check response
  res = ReadUint8();
  if (res != 0x00) {
    PRErrorCode c = PR_CONNECT_REFUSED_ERROR;

    switch (res) {
      case 0x01:
        LOGERROR(
            ("socks5: connect failed: "
             "01, General SOCKS server failure."));
        break;
      case 0x02:
        LOGERROR(
            ("socks5: connect failed: "
             "02, Connection not allowed by ruleset."));
        break;
      case 0x03:
        LOGERROR(("socks5: connect failed: 03, Network unreachable."));
        c = PR_NETWORK_UNREACHABLE_ERROR;
        break;
      case 0x04:
        LOGERROR(("socks5: connect failed: 04, Host unreachable."));
        c = PR_BAD_ADDRESS_ERROR;
        break;
      case 0x05:
        LOGERROR(("socks5: connect failed: 05, Connection refused."));
        break;
      case 0x06:
        LOGERROR(("socks5: connect failed: 06, TTL expired."));
        c = PR_CONNECT_TIMEOUT_ERROR;
        break;
      case 0x07:
        LOGERROR(
            ("socks5: connect failed: "
             "07, Command not supported."));
        break;
      case 0x08:
        LOGERROR(
            ("socks5: connect failed: "
             "08, Address type not supported."));
        c = PR_BAD_ADDRESS_ERROR;
        break;
      default:
        LOGERROR(("socks5: connect failed."));
        break;
    }

    HandshakeFinished(c);
    return PR_FAILURE;
  }

  if (ReadV5AddrTypeAndLength(&res, &len) != PR_SUCCESS) {
    HandshakeFinished(PR_BAD_ADDRESS_ERROR);
    return PR_FAILURE;
  }

  mState = SOCKS5_READ_CONNECT_RESPONSE_BOTTOM;
  WantRead(len + 2);

  return PR_SUCCESS;
}

PRStatus nsSOCKSSocketInfo::ReadV5ConnectResponseBottom() {
  uint8_t type;
  uint32_t len;

  MOZ_ASSERT(mState == SOCKS5_READ_CONNECT_RESPONSE_BOTTOM, "Invalid state!");

  if (ReadV5AddrTypeAndLength(&type, &len) != PR_SUCCESS) {
    HandshakeFinished(PR_BAD_ADDRESS_ERROR);
    return PR_FAILURE;
  }

  MOZ_ASSERT(mDataLength == 7 + len,
             "SOCKS 5 unexpected length of connection reply!");

  LOGDEBUG(("socks5: loading source addr and port"));
  // Read what the proxy says is our source address
  switch (type) {
    case 0x01:  // ipv4
      ReadNetAddr(&mExternalProxyAddr, AF_INET);
      break;
    case 0x04:  // ipv6
      ReadNetAddr(&mExternalProxyAddr, AF_INET6);
      break;
    case 0x03:  // fqdn (skip)
      mReadOffset += len;
      mExternalProxyAddr.raw.family = AF_INET;
      break;
  }

  ReadNetPort(&mExternalProxyAddr);

  LOGDEBUG(("socks5: connected!"));
  HandshakeFinished();

  return PR_SUCCESS;
}

void nsSOCKSSocketInfo::SetConnectTimeout(PRIntervalTime to) { mTimeout = to; }

PRStatus nsSOCKSSocketInfo::DoHandshake(PRFileDesc* fd, int16_t oflags) {
  LOGDEBUG(("socks: DoHandshake(), state = %d", mState));

  if (mPendingError) {
    PR_SetError(mPendingError, mPendingOSError);
    return PR_FAILURE;
  }

  switch (mState) {
    case SOCKS_INITIAL:
      if (IsLocalProxy()) {
        mState = SOCKS_DNS_COMPLETE;
        mLookupStatus = NS_OK;
        return ConnectToProxy(fd);
      }

      return StartDNS(fd);
    case SOCKS_DNS_IN_PROGRESS:
      PR_SetError(PR_IN_PROGRESS_ERROR, 0);
      return PR_FAILURE;
    case SOCKS_DNS_COMPLETE:
      return ConnectToProxy(fd);
    case SOCKS_CONNECTING_TO_PROXY:
      return ContinueConnectingToProxy(fd, oflags);
    case SOCKS4_WRITE_CONNECT_REQUEST:
      if (WriteToSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      WantRead(8);
      mState = SOCKS4_READ_CONNECT_RESPONSE;
      return PR_SUCCESS;
    case SOCKS4_READ_CONNECT_RESPONSE:
      if (ReadFromSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      return ReadV4ConnectResponse();

    case SOCKS5_WRITE_AUTH_REQUEST:
      if (WriteToSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      WantRead(2);
      mState = SOCKS5_READ_AUTH_RESPONSE;
      return PR_SUCCESS;
    case SOCKS5_READ_AUTH_RESPONSE:
      if (ReadFromSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      return ReadV5AuthResponse();
    case SOCKS5_WRITE_USERNAME_REQUEST:
      if (WriteToSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      WantRead(2);
      mState = SOCKS5_READ_USERNAME_RESPONSE;
      return PR_SUCCESS;
    case SOCKS5_READ_USERNAME_RESPONSE:
      if (ReadFromSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      return ReadV5UsernameResponse();
    case SOCKS5_WRITE_CONNECT_REQUEST:
      if (WriteToSocket(fd) != PR_SUCCESS) return PR_FAILURE;

      // The SOCKS 5 response to the connection request is variable
      // length. First, we'll read enough to tell how long the response
      // is, and will read the rest later.
      WantRead(5);
      mState = SOCKS5_READ_CONNECT_RESPONSE_TOP;
      return PR_SUCCESS;
    case SOCKS5_READ_CONNECT_RESPONSE_TOP:
      if (ReadFromSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      return ReadV5ConnectResponseTop();
    case SOCKS5_READ_CONNECT_RESPONSE_BOTTOM:
      if (ReadFromSocket(fd) != PR_SUCCESS) return PR_FAILURE;
      return ReadV5ConnectResponseBottom();

    case SOCKS_CONNECTED:
      LOGERROR(("socks: already connected"));
      HandshakeFinished(PR_IS_CONNECTED_ERROR);
      return PR_FAILURE;
    case SOCKS_FAILED:
      LOGERROR(("socks: already failed"));
      return PR_FAILURE;
  }

  LOGERROR(("socks: executing handshake in invalid state, %d", mState));
  HandshakeFinished(PR_INVALID_STATE_ERROR);

  return PR_FAILURE;
}

int16_t nsSOCKSSocketInfo::GetPollFlags() const {
  switch (mState) {
    case SOCKS_DNS_IN_PROGRESS:
    case SOCKS_DNS_COMPLETE:
    case SOCKS_CONNECTING_TO_PROXY:
      return PR_POLL_EXCEPT | PR_POLL_WRITE;
    case SOCKS4_WRITE_CONNECT_REQUEST:
    case SOCKS5_WRITE_AUTH_REQUEST:
    case SOCKS5_WRITE_USERNAME_REQUEST:
    case SOCKS5_WRITE_CONNECT_REQUEST:
      return PR_POLL_WRITE;
    case SOCKS4_READ_CONNECT_RESPONSE:
    case SOCKS5_READ_AUTH_RESPONSE:
    case SOCKS5_READ_USERNAME_RESPONSE:
    case SOCKS5_READ_CONNECT_RESPONSE_TOP:
    case SOCKS5_READ_CONNECT_RESPONSE_BOTTOM:
      return PR_POLL_READ;
    default:
      break;
  }

  return 0;
}

inline uint8_t nsSOCKSSocketInfo::ReadUint8() {
  uint8_t rv;
  MOZ_ASSERT(mReadOffset + sizeof(rv) <= mDataLength,
             "Not enough space to pop a uint8_t!");
  rv = mData[mReadOffset];
  mReadOffset += sizeof(rv);
  return rv;
}

inline uint16_t nsSOCKSSocketInfo::ReadUint16() {
  uint16_t rv;
  MOZ_ASSERT(mReadOffset + sizeof(rv) <= mDataLength,
             "Not enough space to pop a uint16_t!");
  memcpy(&rv, mData + mReadOffset, sizeof(rv));
  mReadOffset += sizeof(rv);
  return rv;
}

inline uint32_t nsSOCKSSocketInfo::ReadUint32() {
  uint32_t rv;
  MOZ_ASSERT(mReadOffset + sizeof(rv) <= mDataLength,
             "Not enough space to pop a uint32_t!");
  memcpy(&rv, mData + mReadOffset, sizeof(rv));
  mReadOffset += sizeof(rv);
  return rv;
}

void nsSOCKSSocketInfo::ReadNetAddr(NetAddr* addr, uint16_t fam) {
  uint32_t amt = 0;
  const uint8_t* ip = mData + mReadOffset;

  addr->raw.family = fam;
  if (fam == AF_INET) {
    amt = sizeof(addr->inet.ip);
    MOZ_ASSERT(mReadOffset + amt <= mDataLength,
               "Not enough space to pop an ipv4 addr!");
    memcpy(&addr->inet.ip, ip, amt);
  } else if (fam == AF_INET6) {
    amt = sizeof(addr->inet6.ip.u8);
    MOZ_ASSERT(mReadOffset + amt <= mDataLength,
               "Not enough space to pop an ipv6 addr!");
    memcpy(addr->inet6.ip.u8, ip, amt);
  }

  mReadOffset += amt;
}

void nsSOCKSSocketInfo::ReadNetPort(NetAddr* addr) {
  addr->inet.port = ReadUint16();
}

void nsSOCKSSocketInfo::WantRead(uint32_t sz) {
  MOZ_ASSERT(mDataIoPtr == nullptr,
             "WantRead() called while I/O already in progress!");
  MOZ_ASSERT(mDataLength + sz <= BUFFER_SIZE, "Can't read that much data!");
  mAmountToRead = sz;
}

PRStatus nsSOCKSSocketInfo::ReadFromSocket(PRFileDesc* fd) {
  int32_t rc;
  const uint8_t* end;

  if (!mAmountToRead) {
    LOGDEBUG(("socks: ReadFromSocket(), nothing to do"));
    return PR_SUCCESS;
  }

  if (!mDataIoPtr) {
    mDataIoPtr = mData + mDataLength;
    mDataLength += mAmountToRead;
  }

  end = mData + mDataLength;

  while (mDataIoPtr < end) {
    rc = PR_Read(fd, mDataIoPtr, end - mDataIoPtr);
    if (rc <= 0) {
      if (rc == 0) {
        LOGERROR(("socks: proxy server closed connection"));
        HandshakeFinished(PR_CONNECT_REFUSED_ERROR);
        return PR_FAILURE;
      }
      if (PR_GetError() == PR_WOULD_BLOCK_ERROR) {
        LOGDEBUG(("socks: ReadFromSocket(), want read"));
      }
      break;
    }

    mDataIoPtr += rc;
  }

  LOGDEBUG(("socks: ReadFromSocket(), have %u bytes total",
            unsigned(mDataIoPtr - mData)));
  if (mDataIoPtr == end) {
    mDataIoPtr = nullptr;
    mAmountToRead = 0;
    mReadOffset = 0;
    return PR_SUCCESS;
  }

  return PR_FAILURE;
}

PRStatus nsSOCKSSocketInfo::WriteToSocket(PRFileDesc* fd) {
  int32_t rc;
  const uint8_t* end;

  if (!mDataLength) {
    LOGDEBUG(("socks: WriteToSocket(), nothing to do"));
    return PR_SUCCESS;
  }

  if (!mDataIoPtr) mDataIoPtr = mData;

  end = mData + mDataLength;

  while (mDataIoPtr < end) {
    rc = PR_Write(fd, mDataIoPtr, end - mDataIoPtr);
    if (rc < 0) {
      if (PR_GetError() == PR_WOULD_BLOCK_ERROR) {
        LOGDEBUG(("socks: WriteToSocket(), want write"));
      }
      break;
    }

    mDataIoPtr += rc;
  }

  if (mDataIoPtr == end) {
    mDataIoPtr = nullptr;
    mDataLength = 0;
    mReadOffset = 0;
    return PR_SUCCESS;
  }

  return PR_FAILURE;
}

static PRStatus nsSOCKSIOLayerConnect(PRFileDesc* fd, const PRNetAddr* addr,
                                      PRIntervalTime to) {
  PRStatus status;
  NetAddr dst;

  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;
  if (info == nullptr) return PR_FAILURE;

  if (addr->raw.family == PR_AF_INET6 &&
      PR_IsNetAddrType(addr, PR_IpAddrV4Mapped)) {
    const uint8_t* srcp;

    LOGDEBUG(("socks: converting ipv4-mapped ipv6 address to ipv4"));

    // copied from _PR_ConvertToIpv4NetAddr()
    dst.raw.family = AF_INET;
    dst.inet.ip = htonl(INADDR_ANY);
    dst.inet.port = htons(0);
    srcp = addr->ipv6.ip.pr_s6_addr;
    memcpy(&dst.inet.ip, srcp + 12, 4);
    dst.inet.family = AF_INET;
    dst.inet.port = addr->ipv6.port;
  } else {
    dst = NetAddr(addr);
  }

  info->SetDestinationAddr(dst);
  info->SetConnectTimeout(to);

  do {
    status = info->DoHandshake(fd, -1);
  } while (status == PR_SUCCESS && !info->IsConnected());

  return status;
}

static PRStatus nsSOCKSIOLayerConnectContinue(PRFileDesc* fd, int16_t oflags) {
  PRStatus status;

  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;
  if (info == nullptr) return PR_FAILURE;

  do {
    status = info->DoHandshake(fd, oflags);
  } while (status == PR_SUCCESS && !info->IsConnected());

  return status;
}

static int16_t nsSOCKSIOLayerPoll(PRFileDesc* fd, int16_t in_flags,
                                  int16_t* out_flags) {
  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;
  if (info == nullptr) return PR_FAILURE;

  if (!info->IsConnected()) {
    // No readiness follows a pending error, so report ready to surface it.
    if (info->HasPendingError()) {
      *out_flags = in_flags;
      return in_flags;
    }
    *out_flags = 0;
    return info->GetPollFlags();
  }

  return fd->lower->methods->poll(fd->lower, in_flags, out_flags);
}

static PRStatus nsSOCKSIOLayerClose(PRFileDesc* fd) {
  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;
  PRDescIdentity id = PR_GetLayersIdentity(fd);

  if (info && id == nsSOCKSIOLayerIdentity) {
    info->ForgetFD();
    NS_RELEASE(info);
    fd->identity = PR_INVALID_IO_LAYER;
  }

  return fd->lower->methods->close(fd->lower);
}

static PRFileDesc* nsSOCKSIOLayerAccept(PRFileDesc* fd, PRNetAddr* addr,
                                        PRIntervalTime timeout) {
  // TODO: implement SOCKS support for accept
  return fd->lower->methods->accept(fd->lower, addr, timeout);
}

static int32_t nsSOCKSIOLayerAcceptRead(PRFileDesc* sd, PRFileDesc** nd,
                                        PRNetAddr** raddr, void* buf,
                                        int32_t amount,
                                        PRIntervalTime timeout) {
  // TODO: implement SOCKS support for accept, then read from it
  return sd->lower->methods->acceptread(sd->lower, nd, raddr, buf, amount,
                                        timeout);
}

static PRStatus nsSOCKSIOLayerBind(PRFileDesc* fd, const PRNetAddr* addr) {
  // TODO: implement SOCKS support for bind (very similar to connect)
  return fd->lower->methods->bind(fd->lower, addr);
}

static PRStatus nsSOCKSIOLayerGetName(PRFileDesc* fd, PRNetAddr* addr) {
  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;

  if (info != nullptr && addr != nullptr) {
    NetAddr temp;
    info->GetExternalProxyAddr(temp);
    NetAddrToPRNetAddr(&temp, addr);
    return PR_SUCCESS;
  }

  return PR_FAILURE;
}

static PRStatus nsSOCKSIOLayerGetPeerName(PRFileDesc* fd, PRNetAddr* addr) {
  nsSOCKSSocketInfo* info = (nsSOCKSSocketInfo*)fd->secret;

  if (info != nullptr && addr != nullptr) {
    NetAddr temp;
    info->GetDestinationAddr(temp);
    NetAddrToPRNetAddr(&temp, addr);
    return PR_SUCCESS;
  }

  return PR_FAILURE;
}

static PRStatus nsSOCKSIOLayerListen(PRFileDesc* fd, int backlog) {
  // TODO: implement SOCKS support for listen
  return fd->lower->methods->listen(fd->lower, backlog);
}

// add SOCKS IO layer to an existing socket
nsresult nsSOCKSIOLayerAddToSocket(int32_t family, const char* host,
                                   int32_t port, nsIProxyInfo* proxy,
                                   int32_t socksVersion, uint32_t flags,
                                   uint32_t tlsFlags, PRFileDesc* fd) {
  NS_ENSURE_TRUE((socksVersion == 4) || (socksVersion == 5),
                 NS_ERROR_NOT_INITIALIZED);

  if (firstTime) {
    // XXX hack until NSPR provides an official way to detect system IPv6
    // support (bug 388519)
    PRFileDesc* tmpfd = PR_OpenTCPSocket(PR_AF_INET6);
    if (!tmpfd) {
      ipv6Supported = false;
    } else {
      // If the system does not support IPv6, NSPR will push
      // IPv6-to-IPv4 emulation layer onto the native layer
      ipv6Supported = PR_GetIdentitiesLayer(tmpfd, PR_NSPR_IO_LAYER) == tmpfd;
      PR_Close(tmpfd);
    }

    nsSOCKSIOLayerIdentity = PR_GetUniqueIdentity("SOCKS layer");
    nsSOCKSIOLayerMethods = *PR_GetDefaultIOMethods();

    nsSOCKSIOLayerMethods.connect = nsSOCKSIOLayerConnect;
    nsSOCKSIOLayerMethods.connectcontinue = nsSOCKSIOLayerConnectContinue;
    nsSOCKSIOLayerMethods.poll = nsSOCKSIOLayerPoll;
    nsSOCKSIOLayerMethods.bind = nsSOCKSIOLayerBind;
    nsSOCKSIOLayerMethods.acceptread = nsSOCKSIOLayerAcceptRead;
    nsSOCKSIOLayerMethods.getsockname = nsSOCKSIOLayerGetName;
    nsSOCKSIOLayerMethods.getpeername = nsSOCKSIOLayerGetPeerName;
    nsSOCKSIOLayerMethods.accept = nsSOCKSIOLayerAccept;
    nsSOCKSIOLayerMethods.listen = nsSOCKSIOLayerListen;
    nsSOCKSIOLayerMethods.close = nsSOCKSIOLayerClose;

    firstTime = false;
  }

  LOGDEBUG(("Entering nsSOCKSIOLayerAddToSocket()."));

  PRFileDesc* layer;
  PRStatus rv;

  layer = PR_CreateIOLayerStub(nsSOCKSIOLayerIdentity, &nsSOCKSIOLayerMethods);
  if (!layer) {
    LOGERROR(("PR_CreateIOLayerStub() failed."));
    return NS_ERROR_FAILURE;
  }

  nsSOCKSSocketInfo* infoObject = new nsSOCKSSocketInfo();
  if (!infoObject) {
    // clean up IOLayerStub
    LOGERROR(("Failed to create nsSOCKSSocketInfo()."));
    PR_Free(layer);  // PR_CreateIOLayerStub() uses PR_Malloc().
    return NS_ERROR_FAILURE;
  }

  NS_ADDREF(infoObject);
  infoObject->Init(socksVersion, family, proxy, host, flags, tlsFlags);
  layer->secret = (PRFilePrivate*)infoObject;

  PRDescIdentity fdIdentity = PR_GetLayersIdentity(fd);
#if defined(XP_WIN)
  if (fdIdentity == mozilla::net::nsNamedPipeLayerIdentity) {
    // remember named pipe fd on the info object so that we can switch
    // blocking and non-blocking mode on the pipe later.
    infoObject->SetNamedPipeFD(fd);
  }
#endif
  rv = PR_PushIOLayer(fd, fdIdentity, layer);

  if (rv == PR_FAILURE) {
    LOGERROR(("PR_PushIOLayer() failed. rv = %x.", rv));
    NS_RELEASE(infoObject);
    PR_Free(layer);  // PR_CreateIOLayerStub() uses PR_Malloc().
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool IsHostLocalTarget(const nsACString& aHost) {
#if defined(XP_UNIX)
  return StringBeginsWith(aHost, "file:"_ns);
#elif defined(XP_WIN)
  return IsNamedPipePath(aHost);
#else
  return false;
#endif  // XP_UNIX
}
