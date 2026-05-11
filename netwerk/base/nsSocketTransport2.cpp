/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "nsSocketTransport2.h"

#include "MockNetworkLayer.h"
#include "MockNetworkLayerController.h"
#include "NSSErrorsService.h"
#include "NetworkDataCountLayer.h"
#include "QuicSocketControl.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/Telemetry.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/SSLTokensCache.h"
#include "mozilla/ProfilerBandwidthCounter.h"
#include "nsCOMPtr.h"
#include "nsICancelable.h"
#include "nsIClassInfoImpl.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIOService.h"
#include "nsIPipe.h"
#include "nsISocketProvider.h"
#include "nsITLSSocketControl.h"
#include "nsNetAddr.h"
#include "nsNetCID.h"
#include "nsNetSegmentUtils.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsProxyInfo.h"
#include "nsSocketProviderService.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"
#include "nsTransportUtils.h"
#include "nsURLHelper.h"
#include "prerr.h"
#include "sslexp.h"
#include "xpcpublic.h"

#if defined(FUZZING)
#  include "FuzzyLayer.h"
#  include "FuzzySocketControl.h"
#  include "mozilla/StaticPrefs_fuzzing.h"
#endif

#if defined(XP_WIN)
#  include "ShutdownLayer.h"
#endif

/* Following inclusions required for keepalive config not supported by NSPR. */
#include "private/pprio.h"
#if defined(XP_WIN)
#  include <winsock2.h>
#  include <mstcpip.h>
#elif defined(XP_UNIX)
#  include <errno.h>
#  include <netinet/tcp.h>
#endif
/* End keepalive config inclusions. */

#define SUCCESSFUL_CONNECTING_TO_IPV4_ADDRESS 0
#define UNSUCCESSFUL_CONNECTING_TO_IPV4_ADDRESS 1
#define SUCCESSFUL_CONNECTING_TO_IPV6_ADDRESS 2
#define UNSUCCESSFUL_CONNECTING_TO_IPV6_ADDRESS 3

//-----------------------------------------------------------------------------

static NS_DEFINE_CID(kDNSServiceCID, NS_DNSSERVICE_CID);

//-----------------------------------------------------------------------------

namespace mozilla {
namespace net {

class nsSocketEvent : public Runnable, public nsIRunnablePriority {
 public:
  nsSocketEvent(nsSocketTransport* transport, uint32_t type,
                nsresult status = NS_OK, nsISupports* param = nullptr,
                std::function<void()>&& task = nullptr)
      : Runnable("net::nsSocketEvent"),
        mTransport(transport),
        mType(type),
        mStatus(status),
        mParam(param),
        mTask(std::move(task)) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLEPRIORITY

  NS_IMETHOD Run() override {
    mTransport->OnSocketEvent(mType, mStatus, mParam, std::move(mTask));
    return NS_OK;
  }

 private:
  virtual ~nsSocketEvent() = default;

  RefPtr<nsSocketTransport> mTransport;

  uint32_t mType;
  nsresult mStatus;
  nsCOMPtr<nsISupports> mParam;
  std::function<void()> mTask;
};

NS_IMPL_ISUPPORTS_INHERITED(nsSocketEvent, Runnable, nsIRunnablePriority)

NS_IMETHODIMP
nsSocketEvent::GetPriority(uint32_t* aPriority) {
  if (mTransport->IsTRRConnection()) {
    *aPriority = nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
  } else {
    *aPriority = nsIRunnablePriority::PRIORITY_NORMAL;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------

// #define TEST_CONNECT_ERRORS
#ifdef TEST_CONNECT_ERRORS
#  include <stdlib.h>
static PRErrorCode RandomizeConnectError(PRErrorCode code) {
  //
  // To test out these errors, load http://www.yahoo.com/.  It should load
  // correctly despite the random occurrence of these errors.
  //
  int n = rand();
  if (n > RAND_MAX / 2) {
    struct {
      PRErrorCode err_code;
      const char* err_name;
    } errors[] = {
        //
        // These errors should be recoverable provided there is another
        // IP address in mDNSRecord.
        //
        {PR_CONNECT_REFUSED_ERROR, "PR_CONNECT_REFUSED_ERROR"},
        {PR_CONNECT_TIMEOUT_ERROR, "PR_CONNECT_TIMEOUT_ERROR"},
        //
        // This error will cause this socket transport to error out;
        // however, if the consumer is HTTP, then the HTTP transaction
        // should be restarted when this error occurs.
        //
        {PR_CONNECT_RESET_ERROR, "PR_CONNECT_RESET_ERROR"},
    };
    n = n % (sizeof(errors) / sizeof(errors[0]));
    code = errors[n].err_code;
    SOCKET_LOG(("simulating NSPR error %d [%s]\n", code, errors[n].err_name));
  }
  return code;
}
#endif

//-----------------------------------------------------------------------------

nsresult ErrorAccordingToNSPR(PRErrorCode errorCode) {
  nsresult rv = NS_ERROR_FAILURE;
  switch (errorCode) {
    case PR_WOULD_BLOCK_ERROR:
      rv = NS_BASE_STREAM_WOULD_BLOCK;
      break;
    case PR_CONNECT_ABORTED_ERROR:
    case PR_CONNECT_RESET_ERROR:
    case PR_END_OF_FILE_ERROR:  // unexpected EOF is treated the same as reset
      rv = NS_ERROR_NET_RESET;
      break;
    case PR_CONNECT_REFUSED_ERROR:
    // We lump the following NSPR codes in with PR_CONNECT_REFUSED_ERROR. We
    // could get better diagnostics by adding distinct XPCOM error codes for
    // each of these, but there are a lot of places in Gecko that check
    // specifically for NS_ERROR_CONNECTION_REFUSED, all of which would need to
    // be checked.
    case PR_NETWORK_UNREACHABLE_ERROR:
    case PR_HOST_UNREACHABLE_ERROR:
    case PR_ADDRESS_NOT_AVAILABLE_ERROR:
    // Treat EACCES as a soft error since (at least on Linux) connect() returns
    // EACCES when an IPv6 connection is blocked by a firewall. See bug 270784.
    case PR_NO_ACCESS_RIGHTS_ERROR:
      rv = NS_ERROR_CONNECTION_REFUSED;
      break;
    case PR_ADDRESS_NOT_SUPPORTED_ERROR:
      rv = NS_ERROR_SOCKET_ADDRESS_NOT_SUPPORTED;
      break;
    case PR_IO_TIMEOUT_ERROR:
    case PR_CONNECT_TIMEOUT_ERROR:
      rv = NS_ERROR_NET_TIMEOUT;
      break;
    case PR_OUT_OF_MEMORY_ERROR:
    // These really indicate that the descriptor table filled up, or that the
    // kernel ran out of network buffers - but nobody really cares which part of
    // the system ran out of memory.
    case PR_PROC_DESC_TABLE_FULL_ERROR:
    case PR_SYS_DESC_TABLE_FULL_ERROR:
    case PR_INSUFFICIENT_RESOURCES_ERROR:
      rv = NS_ERROR_OUT_OF_MEMORY;
      break;
    case PR_ADDRESS_IN_USE_ERROR:
      rv = NS_ERROR_SOCKET_ADDRESS_IN_USE;
      break;
    // These filename-related errors can arise when using Unix-domain sockets.
    case PR_FILE_NOT_FOUND_ERROR:
      rv = NS_ERROR_FILE_NOT_FOUND;
      break;
    case PR_IS_DIRECTORY_ERROR:
      rv = NS_ERROR_FILE_IS_DIRECTORY;
      break;
    case PR_LOOP_ERROR:
      rv = NS_ERROR_FILE_UNRESOLVABLE_SYMLINK;
      break;
    case PR_NAME_TOO_LONG_ERROR:
      rv = NS_ERROR_FILE_NAME_TOO_LONG;
      break;
    case PR_NO_DEVICE_SPACE_ERROR:
      rv = NS_ERROR_FILE_NO_DEVICE_SPACE;
      break;
    case PR_NOT_DIRECTORY_ERROR:
      rv = NS_ERROR_FILE_NOT_DIRECTORY;
      break;
    case PR_READ_ONLY_FILESYSTEM_ERROR:
      rv = NS_ERROR_FILE_READ_ONLY;
      break;
    case PR_BAD_ADDRESS_ERROR:
      rv = NS_ERROR_UNKNOWN_HOST;
      break;
    default:
      if (psm::IsNSSErrorCode(errorCode)) {
        rv = psm::GetXPCOMFromNSSError(errorCode);
      }
      break;

      // NSPR's socket code can return these, but they're not worth breaking out
      // into their own error codes, distinct from NS_ERROR_FAILURE:
      //
      // PR_BAD_DESCRIPTOR_ERROR
      // PR_INVALID_ARGUMENT_ERROR
      // PR_NOT_SOCKET_ERROR
      // PR_NOT_TCP_SOCKET_ERROR
      //   These would indicate a bug internal to the component.
      //
      // PR_PROTOCOL_NOT_SUPPORTED_ERROR
      //   This means that we can't use the given "protocol" (like
      //   IPPROTO_TCP or IPPROTO_UDP) with a socket of the given type. As
      //   above, this indicates an internal bug.
      //
      // PR_IS_CONNECTED_ERROR
      //   This indicates that we've applied a system call like 'bind' or
      //   'connect' to a socket that is already connected. The socket
      //   components manage each file descriptor's state, and in some cases
      //   handle this error result internally. We shouldn't be returning
      //   this to our callers.
      //
      // PR_IO_ERROR
      //   This is so vague that NS_ERROR_FAILURE is just as good.
  }
  SOCKET_LOG(("ErrorAccordingToNSPR [in=%d out=%" PRIx32 "]\n", errorCode,
              static_cast<uint32_t>(rv)));
  return rv;
}

//-----------------------------------------------------------------------------
// socket input stream impl
//-----------------------------------------------------------------------------

nsSocketInputStream::nsSocketInputStream(nsSocketTransport* trans)
    : mTransport(trans) {}

// called on the socket transport thread...
//
//   condition : failure code if socket has been closed
//
void nsSocketInputStream::OnSocketReady(nsresult condition) {
  SOCKET_LOG(("nsSocketInputStream::OnSocketReady [this=%p cond=%" PRIx32 "]\n",
              this, static_cast<uint32_t>(condition)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsCOMPtr<nsIInputStreamCallback> callback;
  {
    MutexAutoLock lock(mTransport->mLock);

    // update condition, but be careful not to erase an already
    // existing error condition.
    if (NS_SUCCEEDED(mCondition)) mCondition = condition;

    // ignore event if only waiting for closure and not closed.
    if (NS_FAILED(mCondition) || !(mCallbackFlags & WAIT_CLOSURE_ONLY)) {
      callback = std::move(mCallback);
      mCallbackFlags = 0;
    }
  }

  if (callback) callback->OnInputStreamReady(this);
}

NS_IMPL_QUERY_INTERFACE(nsSocketInputStream, nsIInputStream,
                        nsIAsyncInputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
nsSocketInputStream::AddRef() {
  ++mReaderRefCnt;
  return mTransport->AddRef();
}

NS_IMETHODIMP_(MozExternalRefCountType)
nsSocketInputStream::Release() {
  if (--mReaderRefCnt == 0) Close();
  return mTransport->Release();
}

NS_IMETHODIMP
nsSocketInputStream::Close() { return CloseWithStatus(NS_BASE_STREAM_CLOSED); }

NS_IMETHODIMP
nsSocketInputStream::Available(uint64_t* avail) {
  SOCKET_LOG(("nsSocketInputStream::Available [this=%p]\n", this));

  *avail = 0;

  PRFileDesc* fd;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (NS_FAILED(mCondition)) return mCondition;

    fd = mTransport->GetFD_Locked();
    if (!fd) return NS_OK;
  }

  // cannot hold lock while calling NSPR.  (worried about the fact that PSM
  // synchronously proxies notifications over to the UI thread, which could
  // mistakenly try to re-enter this code.)
  int32_t n = PR_Available(fd);

  // PSM does not implement PR_Available() so do a best approximation of it
  // with MSG_PEEK
  if ((n == -1) && (PR_GetError() == PR_NOT_IMPLEMENTED_ERROR)) {
    char c;

    n = PR_Recv(fd, &c, 1, PR_MSG_PEEK, 0);
    SOCKET_LOG(
        ("nsSocketInputStream::Available [this=%p] "
         "using PEEK backup n=%d]\n",
         this, n));
  }

  nsresult rv;
  {
    MutexAutoLock lock(mTransport->mLock);

    mTransport->ReleaseFD_Locked(fd);

    if (n >= 0) {
      *avail = n;
    } else {
      PRErrorCode code = PR_GetError();
      if (code == PR_WOULD_BLOCK_ERROR) return NS_OK;
      mCondition = ErrorAccordingToNSPR(code);
    }
    rv = mCondition;
  }
  if (NS_FAILED(rv)) mTransport->OnInputClosed(rv);
  return rv;
}

NS_IMETHODIMP
nsSocketInputStream::StreamStatus() {
  SOCKET_LOG(("nsSocketInputStream::StreamStatus [this=%p]\n", this));

  MutexAutoLock lock(mTransport->mLock);
  return mCondition;
}

NS_IMETHODIMP
nsSocketInputStream::Read(char* buf, uint32_t count, uint32_t* countRead) {
  SOCKET_LOG(("nsSocketInputStream::Read [this=%p count=%u]\n", this, count));

  *countRead = 0;

  PRFileDesc* fd = nullptr;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (NS_FAILED(mCondition)) {
      return (mCondition == NS_BASE_STREAM_CLOSED) ? NS_OK : mCondition;
    }

    fd = mTransport->GetFD_Locked();
    if (!fd) return NS_BASE_STREAM_WOULD_BLOCK;
  }

  SOCKET_LOG(("  calling PR_Read [count=%u]\n", count));

  // cannot hold lock while calling NSPR.  (worried about the fact that PSM
  // synchronously proxies notifications over to the UI thread, which could
  // mistakenly try to re-enter this code.)
  int32_t n = PR_Read(fd, buf, count);

  SOCKET_LOG(("  PR_Read returned [n=%d]\n", n));

  nsresult rv = NS_OK;
  {
    MutexAutoLock lock(mTransport->mLock);

#ifdef ENABLE_SOCKET_TRACING
    if (n > 0) mTransport->TraceInBuf(buf, n);
#endif

    mTransport->ReleaseFD_Locked(fd);

    if (n > 0) {
      mByteCount += (*countRead = n);
      profiler_count_bandwidth_read_bytes(n);
    } else if (n < 0) {
      PRErrorCode code = PR_GetError();
      if (code == PR_WOULD_BLOCK_ERROR) return NS_BASE_STREAM_WOULD_BLOCK;
      mCondition = ErrorAccordingToNSPR(code);
    }
    rv = mCondition;
  }
  if (NS_FAILED(rv)) mTransport->OnInputClosed(rv);

  // only send this notification if we have indeed read some data.
  // see bug 196827 for an example of why this is important.
  if (n > 0) mTransport->SendStatus(NS_NET_STATUS_RECEIVING_FROM);
  return rv;
}

NS_IMETHODIMP
nsSocketInputStream::ReadSegments(nsWriteSegmentFun writer, void* closure,
                                  uint32_t count, uint32_t* countRead) {
  // socket stream is unbuffered
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsSocketInputStream::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketInputStream::CloseWithStatus(nsresult reason) {
  SOCKET_LOG(("nsSocketInputStream::CloseWithStatus [this=%p reason=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(reason)));

  // may be called from any thread

  nsresult rv;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (NS_SUCCEEDED(mCondition)) {
      rv = mCondition = reason;
    } else {
      rv = NS_OK;
    }
  }
  if (NS_FAILED(rv)) mTransport->OnInputClosed(rv);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketInputStream::AsyncWait(nsIInputStreamCallback* callback, uint32_t flags,
                               uint32_t amount, nsIEventTarget* target) {
  SOCKET_LOG(("nsSocketInputStream::AsyncWait [this=%p]\n", this));

  bool hasError = false;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (callback && target) {
      //
      // build event proxy
      //
      mCallback = NS_NewInputStreamReadyEvent("nsSocketInputStream::AsyncWait",
                                              callback, target);
    } else {
      mCallback = callback;
    }
    mCallbackFlags = flags;

    hasError = NS_FAILED(mCondition);
  }  // unlock mTransport->mLock

  if (hasError) {
    // OnSocketEvent will call OnInputStreamReady with an error code after
    // going through the event loop. We do this because most socket callers
    // do not expect AsyncWait() to synchronously execute the OnInputStreamReady
    // callback.
    mTransport->PostEvent(nsSocketTransport::MSG_INPUT_PENDING);
  } else {
    mTransport->OnInputPending();
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// socket output stream impl
//-----------------------------------------------------------------------------

nsSocketOutputStream::nsSocketOutputStream(nsSocketTransport* trans)
    : mTransport(trans) {}

// called on the socket transport thread...
//
//   condition : failure code if socket has been closed
//
void nsSocketOutputStream::OnSocketReady(nsresult condition) {
  SOCKET_LOG(("nsSocketOutputStream::OnSocketReady [this=%p cond=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(condition)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsCOMPtr<nsIOutputStreamCallback> callback;
  {
    MutexAutoLock lock(mTransport->mLock);

    // update condition, but be careful not to erase an already
    // existing error condition.
    if (NS_SUCCEEDED(mCondition)) mCondition = condition;

    // ignore event if only waiting for closure and not closed.
    if (NS_FAILED(mCondition) || !(mCallbackFlags & WAIT_CLOSURE_ONLY)) {
      callback = std::move(mCallback);
      mCallbackFlags = 0;
    }
  }

  if (callback) callback->OnOutputStreamReady(this);
}

NS_IMPL_QUERY_INTERFACE(nsSocketOutputStream, nsIOutputStream,
                        nsIAsyncOutputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
nsSocketOutputStream::AddRef() {
  ++mWriterRefCnt;
  return mTransport->AddRef();
}

NS_IMETHODIMP_(MozExternalRefCountType)
nsSocketOutputStream::Release() {
  if (--mWriterRefCnt == 0) Close();
  return mTransport->Release();
}

NS_IMETHODIMP
nsSocketOutputStream::Close() { return CloseWithStatus(NS_BASE_STREAM_CLOSED); }

NS_IMETHODIMP
nsSocketOutputStream::Flush() { return NS_OK; }

NS_IMETHODIMP
nsSocketOutputStream::StreamStatus() {
  MutexAutoLock lock(mTransport->mLock);
  return mCondition;
}

NS_IMETHODIMP
nsSocketOutputStream::Write(const char* buf, uint32_t count,
                            uint32_t* countWritten) {
  SOCKET_LOG(("nsSocketOutputStream::Write [this=%p count=%u]\n", this, count));

  *countWritten = 0;

  // A write of 0 bytes can be used to force the initial SSL handshake, so do
  // not reject that.

  PRFileDesc* fd = nullptr;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (NS_FAILED(mCondition)) return mCondition;

    fd = mTransport->GetFD_Locked();
    if (!fd) return NS_BASE_STREAM_WOULD_BLOCK;
  }

  SOCKET_LOG(("  calling PR_Write [count=%u]\n", count));

  // cannot hold lock while calling NSPR.  (worried about the fact that PSM
  // synchronously proxies notifications over to the UI thread, which could
  // mistakenly try to re-enter this code.)
  int32_t n = PR_Write(fd, buf, count);

  SOCKET_LOG(("  PR_Write returned [n=%d]\n", n));

  nsresult rv = NS_OK;
  {
    MutexAutoLock lock(mTransport->mLock);

#ifdef ENABLE_SOCKET_TRACING
    if (n > 0) mTransport->TraceOutBuf(buf, n);
#endif

    mTransport->ReleaseFD_Locked(fd);

    if (n > 0) {
      mByteCount += (*countWritten = n);
      profiler_count_bandwidth_written_bytes(n);
    } else if (n < 0) {
      PRErrorCode code = PR_GetError();
      if (code == PR_WOULD_BLOCK_ERROR) return NS_BASE_STREAM_WOULD_BLOCK;
      mCondition = ErrorAccordingToNSPR(code);
    }
    rv = mCondition;
  }
  if (NS_FAILED(rv)) mTransport->OnOutputClosed(rv);

  // only send this notification if we have indeed written some data.
  // see bug 196827 for an example of why this is important.
  if ((n > 0)) {
    mTransport->SendStatus(NS_NET_STATUS_SENDING_TO);
  }

  return rv;
}

NS_IMETHODIMP
nsSocketOutputStream::WriteSegments(nsReadSegmentFun reader, void* closure,
                                    uint32_t count, uint32_t* countRead) {
  // socket stream is unbuffered
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsSocketOutputStream::WriteFromSegments(
    nsIInputStream* input, void* closure, const char* fromSegment,
    uint32_t offset, uint32_t count, uint32_t* countRead) {
  nsSocketOutputStream* self = (nsSocketOutputStream*)closure;
  return self->Write(fromSegment, count, countRead);
}

NS_IMETHODIMP
nsSocketOutputStream::WriteFrom(nsIInputStream* stream, uint32_t count,
                                uint32_t* countRead) {
  return stream->ReadSegments(WriteFromSegments, this, count, countRead);
}

NS_IMETHODIMP
nsSocketOutputStream::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketOutputStream::CloseWithStatus(nsresult reason) {
  SOCKET_LOG(("nsSocketOutputStream::CloseWithStatus [this=%p reason=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(reason)));

  // may be called from any thread

  nsresult rv;
  {
    MutexAutoLock lock(mTransport->mLock);

    if (NS_SUCCEEDED(mCondition)) {
      rv = mCondition = reason;
    } else {
      rv = NS_OK;
    }
  }
  if (NS_FAILED(rv)) mTransport->OnOutputClosed(rv);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketOutputStream::AsyncWait(nsIOutputStreamCallback* callback,
                                uint32_t flags, uint32_t amount,
                                nsIEventTarget* target) {
  SOCKET_LOG(("nsSocketOutputStream::AsyncWait [this=%p]\n", this));

  {
    MutexAutoLock lock(mTransport->mLock);

    if (callback && target) {
      //
      // build event proxy
      //
      mCallback = NS_NewOutputStreamReadyEvent(callback, target);
    } else {
      mCallback = callback;
    }

    mCallbackFlags = flags;
  }
  mTransport->OnOutputPending();
  return NS_OK;
}

//-----------------------------------------------------------------------------
// socket transport impl
//-----------------------------------------------------------------------------

nsSocketTransport::nsSocketTransport()
    : mFD(this),
      mSocketTransportService(gSocketTransportService),
      mInput(new nsSocketInputStream(this)),
      mOutput(new nsSocketOutputStream(this)) {
  SOCKET_LOG(("creating nsSocketTransport @%p\n", this));

  mTimeouts[TIMEOUT_CONNECT] = UINT16_MAX;     // no timeout
  mTimeouts[TIMEOUT_READ_WRITE] = UINT16_MAX;  // no timeout
}

nsSocketTransport::~nsSocketTransport() {
  MOZ_RELEASE_ASSERT(!mAttached);
  SOCKET_LOG(("destroying nsSocketTransport @%p\n", this));
}

nsresult nsSocketTransport::Init(const nsTArray<nsCString>& types,
                                 const nsACString& host, uint16_t port,
                                 const nsACString& hostRoute,
                                 uint16_t portRoute,
                                 nsIProxyInfo* givenProxyInfo,
                                 nsIDNSRecord* dnsRecord) {
  nsCOMPtr<nsProxyInfo> proxyInfo;
  if (givenProxyInfo) {
    proxyInfo = do_QueryInterface(givenProxyInfo);
    NS_ENSURE_ARG(proxyInfo);
  }

  if (dnsRecord) {
    mExternalDNSResolution = true;
    mDNSRecord = do_QueryInterface(dnsRecord);
    bool resolvedByTRR;
    mDNSRecord->IsTRR(&resolvedByTRR);
    mResolvedByTRR = resolvedByTRR;
    nsIRequest::TRRMode effectiveTRRMode;
    mDNSRecord->GetEffectiveTRRMode(&effectiveTRRMode);
    mEffectiveTRRMode = effectiveTRRMode;
    nsITRRSkipReason::value trrSkipReason;
    mDNSRecord->GetTrrSkipReason(&trrSkipReason);
    mTRRSkipReason = trrSkipReason;
  }

  // init socket type info

  mOriginHost = host;
  mOriginPort = port;
  if (!hostRoute.IsEmpty()) {
    mHost = hostRoute;
    mPort = portRoute;
  } else {
    mHost = host;
    mPort = port;
  }

  // A subtle check we don't enter this method more than once for the socket
  // transport lifetime.  Disable on TSan builds to prevent race checking, we
  // don't want an atomic here for perf reasons!
#ifndef MOZ_TSAN
  MOZ_ASSERT(!mPortRemappingApplied);
#endif  // !MOZ_TSAN

  if (proxyInfo) {
    mHttpsProxy = proxyInfo->IsHTTPS();
  }

  const char* proxyType = nullptr;
  mProxyInfo = proxyInfo;
  if (proxyInfo) {
    mProxyPort = proxyInfo->Port();
    mProxyHost = proxyInfo->Host();
    // grab proxy type (looking for "socks" for example)
    proxyType = proxyInfo->Type();
    if (proxyType && (proxyInfo->IsHTTP() || proxyInfo->IsHTTPS() ||
                      proxyInfo->IsDirect() || !strcmp(proxyType, "unknown"))) {
      proxyType = nullptr;
    }
  }

  SOCKET_LOG1(
      ("nsSocketTransport::Init [this=%p host=%s:%hu origin=%s:%d "
       "proxy=%s:%hu]\n",
       this, mHost.get(), mPort, mOriginHost.get(), mOriginPort,
       mProxyHost.get(), mProxyPort));

  // include proxy type as a socket type if proxy type is not "http"
  uint32_t typeCount = types.Length() + (proxyType != nullptr);
  if (!typeCount) return NS_OK;

  // if we have socket types, then the socket provider service had
  // better exist!
  nsresult rv;
  nsCOMPtr<nsISocketProviderService> spserv =
      nsSocketProviderService::GetOrCreate();

  if (!mTypes.SetCapacity(typeCount, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // now verify that each socket type has a registered socket provider.
  for (uint32_t i = 0, type = 0; i < typeCount; ++i) {
    // store socket types
    if (i == 0 && proxyType) {
      mTypes.AppendElement(proxyType);
    } else {
      mTypes.AppendElement(types[type++]);
    }

    nsCOMPtr<nsISocketProvider> provider;
    rv = spserv->GetSocketProvider(mTypes[i].get(), getter_AddRefs(provider));
    if (NS_FAILED(rv)) {
      NS_WARNING("no registered socket provider");
      return rv;
    }

    // note if socket type corresponds to a transparent proxy
    // XXX don't hardcode SOCKS here (use proxy info's flags instead).
    if (mTypes[i].EqualsLiteral("socks") || mTypes[i].EqualsLiteral("socks4")) {
      mProxyTransparent = true;

      if (proxyInfo->Flags() & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST) {
        // we want the SOCKS layer to send the hostname
        // and port to the proxy and let it do the DNS.
        mProxyTransparentResolvesHost = true;
      }
    }
  }

  return NS_OK;
}

#if defined(XP_UNIX)
nsresult nsSocketTransport::InitWithFilename(const char* filename) {
  return InitWithName(filename, strlen(filename));
}

nsresult nsSocketTransport::InitWithName(const char* name, size_t length) {
  if (length > sizeof(mNetAddr.local.path) - 1) {
    return NS_ERROR_FILE_NAME_TOO_LONG;
  }

  if (!name[0] && length > 1) {
    // name is abstract address name that is supported on Linux only
#  if defined(XP_LINUX)
    mHost.Assign(name + 1, length - 1);
#  else
    return NS_ERROR_SOCKET_ADDRESS_NOT_SUPPORTED;
#  endif
  } else {
    // The name isn't abstract socket address.  So this is Unix domain
    // socket that has file path.
    mHost.Assign(name, length);
  }
  mPort = 0;

  mNetAddr.local.family = AF_LOCAL;
  memcpy(mNetAddr.local.path, name, length);
  mNetAddr.local.path[length] = '\0';
  mNetAddrIsSet = true;

  return NS_OK;
}
#endif

nsresult nsSocketTransport::InitWithConnectedSocket(PRFileDesc* fd,
                                                    const NetAddr* addr) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  addr->ToString(mHost);

  uint16_t port;
  if (addr->raw.family == AF_INET) {
    port = addr->inet.port;
  } else if (addr->raw.family == AF_INET6) {
    port = addr->inet6.port;
  } else {
    port = 0;
  }
  mPort = ntohs(port);

  mNetAddr = *addr;

  mPollFlags = (PR_POLL_READ | PR_POLL_WRITE | PR_POLL_EXCEPT);
  mState = STATE_TRANSFERRING;
  SetSocketName(fd);
  mNetAddrIsSet = true;

  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(!mFD.IsInitialized(), "already initialized");
    mFD = fd;
    mFDref = 1;
    mFDconnected = true;
    mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
  }

  // make sure new socket is non-blocking
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  PR_SetSocketOption(fd, &opt);

  SOCKET_LOG(
      ("nsSocketTransport::InitWithConnectedSocket [this=%p addr=%s:%hu]\n",
       this, mHost.get(), mPort));

  // jump to InitiateSocket to get ourselves attached to the STS poll list.
  return PostEvent(MSG_RETRY_INIT_SOCKET);
}

nsresult nsSocketTransport::InitWithConnectedSocket(
    PRFileDesc* aFD, const NetAddr* aAddr, nsIInterfaceRequestor* aCallbacks) {
  {
    MutexAutoLock lock(mLock);
    mCallbacks = aCallbacks;
  }
  return InitWithConnectedSocket(aFD, aAddr);
}

nsresult nsSocketTransport::PostEvent(uint32_t type, nsresult status,
                                      nsISupports* param,
                                      std::function<void()>&& task) {
  SOCKET_LOG(("nsSocketTransport::PostEvent [this=%p type=%u status=%" PRIx32
              " param=%p]\n",
              this, type, static_cast<uint32_t>(status), param));

  nsCOMPtr<nsIRunnable> event =
      new nsSocketEvent(this, type, status, param, std::move(task));
  if (!event) return NS_ERROR_OUT_OF_MEMORY;

  return mSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
}

void nsSocketTransport::SendStatus(nsresult status) {
  SOCKET_LOG1(("nsSocketTransport::SendStatus [this=%p status=%" PRIx32 "]\n",
               this, static_cast<uint32_t>(status)));

  nsCOMPtr<nsITransportEventSink> sink;
  uint64_t progress;
  {
    MutexAutoLock lock(mLock);
    sink = mEventSink;
    switch (status) {
      case NS_NET_STATUS_SENDING_TO:
        progress = mOutput->ByteCount(lock);
        break;
      case NS_NET_STATUS_RECEIVING_FROM:
        progress = mInput->ByteCount(lock);
        break;
      default:
        progress = 0;
        break;
    }
  }
  if (sink) {
    sink->OnTransportStatus(this, status, progress, -1);
  }
}

nsresult nsSocketTransport::ResolveHost() {
  SOCKET_LOG((
      "nsSocketTransport::ResolveHost [this=%p %s:%d%s] "
      "mProxyTransparentResolvesHost=%d\n",
      this, SocketHost().get(), SocketPort(),
      mConnectionFlags & nsSocketTransport::BYPASS_CACHE ? " bypass cache" : "",
      mProxyTransparentResolvesHost));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsresult rv;

  if (!mProxyHost.IsEmpty()) {
    if (!mProxyTransparent || mProxyTransparentResolvesHost) {
#if defined(XP_UNIX)
      MOZ_ASSERT(!mNetAddrIsSet || mNetAddr.raw.family != AF_LOCAL,
                 "Unix domain sockets can't be used with proxies");
#endif
      // When not resolving mHost locally, we still want to ensure that
      // it only contains valid characters.  See bug 304904 for details.
      // Sometimes the end host is not yet known and mHost is *
      if (!net_IsValidDNSHost(mHost) && !mHost.EqualsLiteral("*")) {
        SOCKET_LOG(("  invalid hostname %s\n", mHost.get()));
        return NS_ERROR_UNKNOWN_HOST;
      }
    }
    if (mProxyTransparentResolvesHost) {
      // Name resolution is done on the server side.  Just pretend
      // client resolution is complete, this will get picked up later.
      // since we don't need to do DNS now, we bypass the resolving
      // step by initializing mNetAddr to an empty address, but we
      // must keep the port. The SOCKS IO layer will use the hostname
      // we send it when it's created, rather than the empty address
      // we send with the connect call.
      mState = STATE_RESOLVING;
      mNetAddr.raw.family = AF_INET;
      mNetAddr.inet.port = htons(SocketPort());
      mNetAddr.inet.ip = htonl(INADDR_ANY);
      return PostEvent(MSG_DNS_LOOKUP_COMPLETE, NS_OK, nullptr);
    }
  }

  if (mExternalDNSResolution) {
    MOZ_ASSERT(mDNSRecord);
    mState = STATE_RESOLVING;
    return PostEvent(MSG_DNS_LOOKUP_COMPLETE, NS_OK, nullptr);
  }

  nsCOMPtr<nsIDNSService> dns = nullptr;
  auto initTask = [&dns]() { dns = do_GetService(kDNSServiceCID); };
  if (!NS_IsMainThread()) {
    // Forward to the main thread synchronously.
    RefPtr<nsIThread> mainThread = do_GetMainThread();
    if (!mainThread) {
      return NS_ERROR_FAILURE;
    }

    SyncRunnable::DispatchToThread(
        mainThread,
        NS_NewRunnableFunction("nsSocketTransport::ResolveHost->GetDNSService",
                               initTask));
  } else {
    initTask();
  }
  if (!dns) {
    return NS_ERROR_FAILURE;
  }

  mResolving = true;

  nsIDNSService::DNSFlags dnsFlags = nsIDNSService::RESOLVE_DEFAULT_FLAGS;
  if (mConnectionFlags & nsSocketTransport::BYPASS_CACHE) {
    dnsFlags = nsIDNSService::RESOLVE_BYPASS_CACHE;
  }
  if (mConnectionFlags & nsSocketTransport::REFRESH_CACHE) {
    dnsFlags = nsIDNSService::RESOLVE_REFRESH_CACHE;
  }
  if (mConnectionFlags & nsSocketTransport::DISABLE_IPV6) {
    dnsFlags |= nsIDNSService::RESOLVE_DISABLE_IPV6;
  }
  if (mConnectionFlags & nsSocketTransport::DISABLE_IPV4) {
    dnsFlags |= nsIDNSService::RESOLVE_DISABLE_IPV4;
  }
  if (mConnectionFlags & nsSocketTransport::DISABLE_TRR) {
    dnsFlags |= nsIDNSService::RESOLVE_DISABLE_TRR;
  }

  if (mConnectionFlags & nsSocketTransport::USE_IP_HINT_ADDRESS) {
    dnsFlags |= nsIDNSService::RESOLVE_IP_HINT;
  }

  dnsFlags |= nsIDNSService::GetFlagsFromTRRMode(
      nsISocketTransport::GetTRRModeFromFlags(mConnectionFlags));

  // When we get here, we are not resolving using any configured proxy likely
  // because of individual proxy setting on the request or because the host is
  // excluded from proxying.  Hence, force resolution despite global proxy-DNS
  // configuration.
  dnsFlags |= nsIDNSService::RESOLVE_IGNORE_SOCKS_DNS;

  NS_ASSERTION(!(dnsFlags & nsIDNSService::RESOLVE_DISABLE_IPV6) ||
                   !(dnsFlags & nsIDNSService::RESOLVE_DISABLE_IPV4),
               "Setting both RESOLVE_DISABLE_IPV6 and RESOLVE_DISABLE_IPV4");

  SendStatus(NS_NET_STATUS_RESOLVING_HOST);

  if (!SocketHost().Equals(mOriginHost)) {
    SOCKET_LOG(("nsSocketTransport %p origin %s doing dns for %s\n", this,
                mOriginHost.get(), SocketHost().get()));
  }
  rv =
      dns->AsyncResolveNative(SocketHost(), nsIDNSService::RESOLVE_TYPE_DEFAULT,
                              dnsFlags, nullptr, this, mSocketTransportService,
                              mOriginAttributes, getter_AddRefs(mDNSRequest));

  if (NS_SUCCEEDED(rv)) {
    SOCKET_LOG(("  advancing to STATE_RESOLVING\n"));
    mState = STATE_RESOLVING;
  }
  return rv;
}

nsresult nsSocketTransport::BuildSocket(PRFileDesc*& fd, bool& proxyTransparent,
                                        bool& usingSSL) {
  SOCKET_LOG(("nsSocketTransport::BuildSocket [this=%p]\n", this));

  nsresult rv = NS_OK;

  proxyTransparent = false;
  usingSSL = false;

  if (mTypes.IsEmpty()) {
    fd = PR_OpenTCPSocket(mNetAddr.raw.family);
    if (!fd) {
      SOCKET_LOG(("  error creating TCP nspr socket [rv=%" PRIx32 "]\n",
                  static_cast<uint32_t>(rv)));
      return NS_ERROR_OUT_OF_MEMORY;
    }
    return NS_OK;
  }

#if defined(XP_UNIX)
  MOZ_ASSERT(!mNetAddrIsSet || mNetAddr.raw.family != AF_LOCAL,
             "Unix domain sockets can't be used with socket types");
#endif

  fd = nullptr;

  uint32_t controlFlags = 0;
  if (mProxyTransparentResolvesHost) {
    controlFlags |= nsISocketProvider::PROXY_RESOLVES_HOST;
  }

  if (mConnectionFlags & nsISocketTransport::ANONYMOUS_CONNECT) {
    controlFlags |= nsISocketProvider::ANONYMOUS_CONNECT;
  }

  if (mConnectionFlags & nsISocketTransport::NO_PERMANENT_STORAGE) {
    controlFlags |= nsISocketProvider::NO_PERMANENT_STORAGE;
  }

  if (mConnectionFlags & nsISocketTransport::BE_CONSERVATIVE) {
    controlFlags |= nsISocketProvider::BE_CONSERVATIVE;
  }

  if (mConnectionFlags & nsISocketTransport::DONT_TRY_ECH) {
    controlFlags |= nsISocketProvider::DONT_TRY_ECH;
  }

  if (mConnectionFlags & nsISocketTransport::IS_RETRY) {
    controlFlags |= nsISocketProvider::IS_RETRY;
  }

  if (mConnectionFlags &
      nsISocketTransport::ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT) {
    controlFlags |= nsISocketProvider::ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT;
  }

  if (mConnectionFlags & nsISocketTransport::IS_SPECULATIVE_CONNECTION) {
    controlFlags |= nsISocketProvider::IS_SPECULATIVE_CONNECTION;
  }

  if (mResolvedByTRR) {
    controlFlags |= nsISocketProvider::USED_PRIVATE_DNS;
  }

  // by setting host to mOriginHost, instead of mHost we send the
  // SocketProvider (e.g. PSM) the origin hostname but can still do DNS
  // on an explicit alternate service host name
  const char* host = mOriginHost.get();
  int32_t port = (int32_t)mOriginPort;

  nsCOMPtr<nsISocketProviderService> spserv =
      nsSocketProviderService::GetOrCreate();
  nsCOMPtr<nsIProxyInfo> proxyInfo = mProxyInfo;

  uint32_t i;
  for (i = 0; i < mTypes.Length(); ++i) {
    nsCOMPtr<nsISocketProvider> provider;

    SOCKET_LOG(("  pushing io layer [%u:%s]\n", i, mTypes[i].get()));

    rv = spserv->GetSocketProvider(mTypes[i].get(), getter_AddRefs(provider));
    if (NS_FAILED(rv)) break;

    nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
    if (i == 0) {
      // if this is the first type, we'll want the
      // service to allocate a new socket

      // Most layers _ESPECIALLY_ PSM want the origin name here as they
      // will use it for secure checks, etc.. and any connection management
      // differences between the origin name and the routed name can be
      // taken care of via DNS. However, SOCKS is a special case as there is
      // no DNS. in the case of SOCKS and PSM the PSM is a separate layer
      // and receives the origin name.
      const char* socketProviderHost = host;
      int32_t socketProviderPort = port;
      if (mProxyTransparentResolvesHost &&
          (mTypes[0].EqualsLiteral("socks") ||
           mTypes[0].EqualsLiteral("socks4"))) {
        SOCKET_LOG(("SOCKS %d Host/Route override: %s:%d -> %s:%d\n",
                    mHttpsProxy, socketProviderHost, socketProviderPort,
                    mHost.get(), mPort));
        socketProviderHost = mHost.get();
        socketProviderPort = mPort;
      }

      // when https proxying we want to just connect to the proxy as if
      // it were the end host (i.e. expect the proxy's cert)

      rv = provider->NewSocket(
          mNetAddr.raw.family,
          mHttpsProxy ? mProxyHost.get() : socketProviderHost,
          mHttpsProxy ? mProxyPort : socketProviderPort, proxyInfo,
          mOriginAttributes, controlFlags, mTlsFlags, &fd,
          getter_AddRefs(tlsSocketControl));

      if (NS_SUCCEEDED(rv) && !fd) {
        MOZ_ASSERT_UNREACHABLE(
            "NewSocket succeeded but failed to "
            "create a PRFileDesc");
        rv = NS_ERROR_UNEXPECTED;
      }
    } else {
      // the socket has already been allocated,
      // so we just want the service to add itself
      // to the stack (such as pushing an io layer)
      rv = provider->AddToSocket(mNetAddr.raw.family, host, port, proxyInfo,
                                 mOriginAttributes, controlFlags, mTlsFlags, fd,
                                 getter_AddRefs(tlsSocketControl));
    }

    // controlFlags = 0; not used below this point...
    if (NS_FAILED(rv)) break;

    // if the service was ssl or starttls, we want to hold onto the socket
    // info
    bool isSSL = mTypes[i].EqualsLiteral("ssl");
    if (isSSL || mTypes[i].EqualsLiteral("starttls")) {
      // remember security info
      {
        MutexAutoLock lock(mLock);
        mTLSSocketControl = tlsSocketControl;
        SOCKET_LOG(("  [tlsSocketControl=%p callbacks=%p]\n",
                    mTLSSocketControl.get(), mCallbacks.get()));
      }
      // remember if socket type is SSL so we can ProxyStartSSL if need be.
      usingSSL = isSSL;
    } else if (mTypes[i].EqualsLiteral("socks") ||
               mTypes[i].EqualsLiteral("socks4")) {
      // since socks is transparent, any layers above
      // it do not have to worry about proxy stuff
      proxyInfo = nullptr;
      proxyTransparent = true;
    }
  }

  if (NS_FAILED(rv)) {
    SOCKET_LOG(("  error pushing io layer [%u:%s rv=%" PRIx32 "]\n", i,
                mTypes[i].get(), static_cast<uint32_t>(rv)));
    if (fd) {
      CloseSocket(fd);
    }
  }
  return rv;
}

static bool ShouldBlockAddress(const NetAddr& aAddr, const nsCString& aHost) {
  if (!xpc::AreNonLocalConnectionsDisabled()) {
    return false;
  }

  NetAddr overrideAddr;
  bool hasOverride = FindNetAddrOverride(aAddr, overrideAddr);
  const NetAddr& addrToCheck = hasOverride ? overrideAddr : aAddr;

  if (addrToCheck.IsIPAddrAny() || addrToCheck.IsIPAddrLocal() ||
      addrToCheck.IsIPAddrShared() || addrToCheck.IsLoopbackAddr()) {
    return false;
  }

  nsAutoCString allowlist;
  {
    const auto prefLock =
        mozilla::StaticPrefs::network_socket_allowed_nonlocal_domains();
    allowlist = *prefLock;
  }

  for (const nsACString& host :
       nsCCharSeparatedTokenizer(allowlist, ',').ToRange()) {
    if (aHost == host) {
      return false;
    }
  }

  return true;
}

nsresult nsSocketTransport::InitiateSocket() {
  SOCKET_LOG(("nsSocketTransport::InitiateSocket [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsresult rv;
  bool isLocal;
  IsLocal(&isLocal);

  if (gIOService->IsNetTearingDown()) {
    return NS_ERROR_ABORT;
  }

  // Since https://github.com/whatwg/fetch/pull/1763,
  // we need to disable access to 0.0.0.0 for non-test purposes
  if (mNetAddr.IsIPAddrAny() && !mProxyTransparentResolvesHost) {
    if (StaticPrefs::network_socket_ip_addr_any_disabled()) {
      SOCKET_LOG(("connection refused NS_ERROR_CONNECTION_REFUSED\n"));
      return NS_ERROR_CONNECTION_REFUSED;
    }
  }

  if (gIOService->IsOffline()) {
    if (StaticPrefs::network_disable_localhost_when_offline() || !isLocal) {
      return NS_ERROR_OFFLINE;
    }
  } else if (!isLocal) {
#ifdef DEBUG
    // all IP networking has to be done from the parent
    if (NS_SUCCEEDED(mCondition) && ((mNetAddr.raw.family == AF_INET) ||
                                     (mNetAddr.raw.family == AF_INET6))) {
      MOZ_ASSERT(!IsNeckoChild());
    }
#endif

    if (NS_SUCCEEDED(mCondition) && ShouldBlockAddress(mNetAddr, mHost)) {
      nsAutoCString ipaddr;
      RefPtr<nsNetAddr> netaddr = new nsNetAddr(&mNetAddr);
      netaddr->GetAddress(ipaddr);
      fprintf_stderr(
          stderr,
          "FATAL ERROR: Non-local network connections are disabled and a "
          "connection "
          "attempt to %s (%s) was made.\nYou should only access hostnames "
          "available via the test networking proxy (if running mochitests) "
          "or from a test-specific httpd.js server (if running xpcshell "
          "tests). "
          "Browser services should be disabled or redirected to a local "
          "server.\n",
          mHost.get(), ipaddr.get());
      return NS_ERROR_NON_LOCAL_CONNECTION_REFUSED;
    }
  }

  // Hosts/Proxy Hosts that are Local IP Literals should not be speculatively
  // connected - Bug 853423.
  if (mConnectionFlags & nsISocketTransport::DISABLE_RFC1918 &&
      mNetAddr.IsIPAddrLocal()) {
    SOCKET_LOG(
        ("nsSocketTransport::InitiateSocket skipping "
         "speculative connection for host [%s:%d] proxy "
         "[%s:%d] with Local IP address [%s]",
         mHost.get(), mPort, mProxyHost.get(), mProxyPort,
         mNetAddr.ToString().get()));
    mCondition = NS_ERROR_CONNECTION_REFUSED;
    OnSocketDetached(nullptr);
    return mCondition;
  }

  //
  // find out if it is going to be ok to attach another socket to the STS.
  // if not then we have to wait for the STS to tell us that it is ok.
  // the notification is asynchronous, which means that when we could be
  // in a race to call AttachSocket once notified.  for this reason, when
  // we get notified, we just re-enter this function.  as a result, we are
  // sure to ask again before calling AttachSocket.  in this way we deal
  // with the race condition.  though it isn't the most elegant solution,
  // it is far simpler than trying to build a system that would guarantee
  // FIFO ordering (which wouldn't even be that valuable IMO).  see bug
  // 194402 for more info.
  //
  if (!mSocketTransportService->CanAttachSocket()) {
    nsCOMPtr<nsIRunnable> event =
        new nsSocketEvent(this, MSG_RETRY_INIT_SOCKET);
    if (!event) return NS_ERROR_OUT_OF_MEMORY;
    return mSocketTransportService->NotifyWhenCanAttachSocket(event);
  }

  //
  // if we already have a connected socket, then just attach and return.
  //
  {
    MutexAutoLock lock(mLock);
    if (mFD.IsInitialized()) {
      rv = mSocketTransportService->AttachSocket(mFD, this);
      if (NS_SUCCEEDED(rv)) mAttached = true;
      return rv;
    }
  }

  //
  // create new socket fd, push io layers, etc.
  //
  PRFileDesc* fd;
  bool proxyTransparent;
  bool usingSSL;

  rv = BuildSocket(fd, proxyTransparent, usingSSL);
  if (NS_FAILED(rv)) {
    SOCKET_LOG(
        ("  BuildSocket failed [rv=%" PRIx32 "]\n", static_cast<uint32_t>(rv)));
    return rv;
  }

#ifdef FUZZING
  if (StaticPrefs::fuzzing_necko_enabled()) {
    rv = AttachFuzzyIOLayer(fd);
    if (NS_FAILED(rv)) {
      SOCKET_LOG(("Failed to attach fuzzing IOLayer [rv=%" PRIx32 "].\n",
                  static_cast<uint32_t>(rv)));
      return rv;
    }
    SOCKET_LOG(("Successfully attached fuzzing IOLayer.\n"));

    if (usingSSL) {
      mTLSSocketControl = new FuzzySocketControl();
    }
  }
#endif

  PRStatus status;

  // Make the socket non-blocking...
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_Nonblocking;
  opt.value.non_blocking = true;
  status = PR_SetSocketOption(fd, &opt);
  NS_ASSERTION(status == PR_SUCCESS, "unable to make socket non-blocking");

  if (mReuseAddrPort) {
    SOCKET_LOG(("  Setting port/addr reuse socket options\n"));

    // Set ReuseAddr for TCP sockets to enable having several
    // sockets bound to same local IP and port
    PRSocketOptionData opt_reuseaddr;
    opt_reuseaddr.option = PR_SockOpt_Reuseaddr;
    opt_reuseaddr.value.reuse_addr = PR_TRUE;
    status = PR_SetSocketOption(fd, &opt_reuseaddr);
    if (status != PR_SUCCESS) {
      SOCKET_LOG(("  Couldn't set reuse addr socket option: %d\n", status));
    }

    // And also set ReusePort for platforms supporting this socket option
    PRSocketOptionData opt_reuseport;
    opt_reuseport.option = PR_SockOpt_Reuseport;
    opt_reuseport.value.reuse_port = PR_TRUE;
    status = PR_SetSocketOption(fd, &opt_reuseport);
    if (status != PR_SUCCESS &&
        PR_GetError() != PR_OPERATION_NOT_SUPPORTED_ERROR) {
      SOCKET_LOG(("  Couldn't set reuse port socket option: %d\n", status));
    }
  }

  // disable the nagle algorithm - if we rely on it to coalesce writes into
  // full packets the final packet of a multi segment POST/PUT or pipeline
  // sequence is delayed a full rtt
  opt.option = PR_SockOpt_NoDelay;
  opt.value.no_delay = true;
  PR_SetSocketOption(fd, &opt);

  // if the network.tcp.sendbuffer preference is set, use it to size SO_SNDBUF
  // The Windows default of 8KB is too small and as of vista sp1, autotuning
  // only applies to receive window
  int32_t sndBufferSize;
  mSocketTransportService->GetSendBufferSize(&sndBufferSize);
  if (sndBufferSize > 0) {
    opt.option = PR_SockOpt_SendBufferSize;
    opt.value.send_buffer_size = sndBufferSize;
    PR_SetSocketOption(fd, &opt);
  }

  if (mQoSBits) {
    opt.option = PR_SockOpt_IpTypeOfService;
    opt.value.tos = mQoSBits;
    PR_SetSocketOption(fd, &opt);
  }

#if defined(XP_WIN)
  // The linger is turned off by default. This is not a hard close, but
  // closesocket should return immediately and operating system tries to send
  // remaining data for certain, implementation specific, amount of time.
  // https://msdn.microsoft.com/en-us/library/ms739165.aspx
  //
  // Turn the linger option on an set the interval to 0. This will cause hard
  // close of the socket.
  opt.option = PR_SockOpt_Linger;
  opt.value.linger.polarity = 1;
  opt.value.linger.linger = 0;
  PR_SetSocketOption(fd, &opt);
#endif

#if defined(TCP_USER_TIMEOUT)
  {
    int32_t tcpUserTimeoutS = StaticPrefs::network_tcp_user_timeout();
    if (tcpUserTimeoutS > 0) {
      PRFileDesc* bottom = PR_GetIdentitiesLayer(fd, PR_NSPR_IO_LAYER);
      if (bottom) {
        PROsfd sock = PR_FileDesc2NativeHandle(bottom);
        if (sock != -1) {
          int tcpUserTimeoutMs = tcpUserTimeoutS * 1000;
          if (setsockopt(sock, IPPROTO_TCP, TCP_USER_TIMEOUT, &tcpUserTimeoutMs,
                         sizeof(tcpUserTimeoutMs)) != 0) {
            NS_WARNING("nsSocketTransport: Failed setting TCP_USER_TIMEOUT");
          }
        }
      }
    }
  }
#endif

#if defined(TCP_NOTSENT_LOWAT)
  {
    int32_t notsentLowat = StaticPrefs::network_tcp_notsent_lowat();
    if (notsentLowat > 0) {
      PRFileDesc* bottom = PR_GetIdentitiesLayer(fd, PR_NSPR_IO_LAYER);
      if (bottom) {
        PROsfd sock = PR_FileDesc2NativeHandle(bottom);
        if (sock != -1) {
          if (setsockopt(sock, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &notsentLowat,
                         sizeof(notsentLowat)) != 0) {
            NS_WARNING("nsSocketTransport: Failed setting TCP_NOTSENT_LOWAT");
          }
        }
      }
    }
  }
#endif

  // up to here, mFD will only be accessed by us

  // assign mFD so that we can properly handle OnSocketDetached before we've
  // established a connection.
  {
    MutexAutoLock lock(mLock);
    // inform socket transport about this newly created socket...
    rv = mSocketTransportService->AttachSocket(fd, this);
    if (NS_FAILED(rv)) {
      CloseSocket(fd);
      return rv;
    }
    mAttached = true;

    mFD = fd;
    mFDref = 1;
    mFDconnected = false;
    mPollTimeout = mTimeouts[TIMEOUT_CONNECT];
  }

  SOCKET_LOG(("  advancing to STATE_CONNECTING\n"));
  mState = STATE_CONNECTING;
  SendStatus(NS_NET_STATUS_CONNECTING_TO);

  SOCKET_LOG(("  trying address: %s\n", mNetAddr.ToString().get()));

  //
  // Initiate the connect() to the host...
  //
  PRNetAddr prAddr;
  memset(&prAddr, 0, sizeof(prAddr));
  if (mBindAddr) {
    NetAddrToPRNetAddr(mBindAddr.get(), &prAddr);
    status = PR_Bind(fd, &prAddr);
    if (status != PR_SUCCESS) {
      return NS_ERROR_FAILURE;
    }
    mBindAddr = nullptr;
  }

  NetAddrToPRNetAddr(&mNetAddr, &prAddr);

#ifdef XP_WIN
  // Find the real tcp socket and set non-blocking once again!
  // Bug 1158189.
  PRFileDesc* bottom = PR_GetIdentitiesLayer(fd, PR_NSPR_IO_LAYER);
  if (bottom) {
    PROsfd osfd = PR_FileDesc2NativeHandle(bottom);
    u_long nonblocking = 1;
    if (ioctlsocket(osfd, FIONBIO, &nonblocking) != 0) {
      NS_WARNING("Socket could not be set non-blocking!");
      return NS_ERROR_FAILURE;
    }
  }
#endif

  if (mTLSSocketControl) {
    nsCString echConfig;
    {
      MutexAutoLock lock(mLock);
      echConfig = mEchConfig;
    }
    if (!echConfig.IsEmpty() &&
        !(mConnectionFlags & (DONT_TRY_ECH | BE_CONSERVATIVE))) {
      SOCKET_LOG(("nsSocketTransport::InitiateSocket set echconfig."));
      rv = mTLSSocketControl->SetEchConfig(echConfig);
      if (NS_FAILED(rv)) {
        return rv;
      }
      mEchConfigUsed = true;
    }
  }

  if (Telemetry::CanRecordPrereleaseData() ||
      Telemetry::CanRecordReleaseData()) {
    if (NS_FAILED(AttachNetworkDataCountLayer(fd))) {
      SOCKET_LOG(
          ("nsSocketTransport::InitiateSocket "
           "AttachNetworkDataCountLayer failed [this=%p]\n",
           this));
    }
  }
  if (StaticPrefs::network_socket_attach_mock_network_layer() &&
      xpc::AreNonLocalConnectionsDisabled()) {
    if (NS_FAILED(AttachMockNetworkLayer(fd))) {
      SOCKET_LOG(
          ("nsSocketTransport::InitiateSocket "
           "AttachMockNetworkLayer failed [this=%p]\n",
           this));
    }
  }

  status = PR_Connect(fd, &prAddr, NS_SOCKET_CONNECT_TIMEOUT);
  PRErrorCode code = PR_GetError();
  if (status == PR_SUCCESS) {
    PR_SetFDInheritable(fd, false);
  }

  if (status == PR_SUCCESS) {
    //
    // we are connected!
    //
    OnSocketConnected();
  } else {
#if defined(TEST_CONNECT_ERRORS)
    code = RandomizeConnectError(code);
#endif
    //
    // If the PR_Connect(...) would block, then poll for a connection.
    //
    if ((PR_WOULD_BLOCK_ERROR == code) || (PR_IN_PROGRESS_ERROR == code)) {
      mPollFlags = (PR_POLL_EXCEPT | PR_POLL_WRITE);
      //
      // If the socket is already connected, then return success...
      //
    } else if (PR_IS_CONNECTED_ERROR == code) {
      //
      // we are connected!
      //
      OnSocketConnected();

      if (mTLSSocketControl && !mProxyHost.IsEmpty() && proxyTransparent &&
          usingSSL) {
        // if the connection phase is finished, and the ssl layer has
        // been pushed, and we were proxying (transparently; ie. nothing
        // has to happen in the protocol layer above us), it's time for
        // the ssl to start doing it's thing.
        SOCKET_LOG(("  calling ProxyStartSSL()\n"));
        mTLSSocketControl->ProxyStartSSL();
        // XXX what if we were forced to poll on the socket for a successful
        // connection... wouldn't we need to call ProxyStartSSL after a call
        // to PR_ConnectContinue indicates that we are connected?
        //
        // XXX this appears to be what the old socket transport did.  why
        // isn't this broken?
      }
    }
    //
    // A SOCKS request was rejected; get the actual error code from
    // the OS error
    //
    else if (PR_UNKNOWN_ERROR == code && mProxyTransparent &&
             !mProxyHost.IsEmpty()) {
      code = PR_GetOSError();
      rv = ErrorAccordingToNSPR(code);
    }
    //
    // The connection was refused...
    //
    else {
      rv = ErrorAccordingToNSPR(code);
      if ((rv == NS_ERROR_CONNECTION_REFUSED) && !mProxyHost.IsEmpty()) {
        rv = NS_ERROR_PROXY_CONNECTION_REFUSED;
      }
    }
  }
  return rv;
}

bool nsSocketTransport::RecoverFromError() {
  NS_ASSERTION(NS_FAILED(mCondition), "there should be something wrong");

  SOCKET_LOG(
      ("nsSocketTransport::RecoverFromError [this=%p state=%x cond=%" PRIx32
       "]\n",
       this, mState, static_cast<uint32_t>(mCondition)));

  if (mDoNotRetryToConnect) {
    SOCKET_LOG(
        ("nsSocketTransport::RecoverFromError do not retry because "
         "mDoNotRetryToConnect is set [this=%p]\n",
         this));
    return false;
  }

#if defined(XP_UNIX)
  // Unix domain connections don't have multiple addresses to try,
  // so the recovery techniques here don't apply.
  if (mNetAddrIsSet && mNetAddr.raw.family == AF_LOCAL) return false;
#endif

  if ((mConnectionFlags & nsSocketTransport::USE_IP_HINT_ADDRESS) &&
      mCondition == NS_ERROR_UNKNOWN_HOST &&
      (mState == MSG_DNS_LOOKUP_COMPLETE || mState == MSG_ENSURE_CONNECT)) {
    SOCKET_LOG(("  try again without USE_IP_HINT_ADDRESS"));
    mConnectionFlags &= ~nsSocketTransport::USE_IP_HINT_ADDRESS;
    mState = STATE_CLOSED;
    return NS_SUCCEEDED(PostEvent(MSG_ENSURE_CONNECT, NS_OK));
  }

  // can only recover from errors in these states
  if (mState != STATE_RESOLVING && mState != STATE_CONNECTING) {
    SOCKET_LOG(("  not in a recoverable state"));
    return false;
  }

  nsresult rv;

#ifdef DEBUG
  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(!mFDconnected, "socket should not be connected");
  }
#endif

  // all connection failures need to be reported to DNS so that the next
  // time we will use a different address if available.
  // NS_BASE_STREAM_CLOSED is not an actual connection failure, so don't report
  // to DNS.
  if (mState == STATE_CONNECTING && mDNSRecord &&
      mCondition != NS_BASE_STREAM_CLOSED) {
    mDNSRecord->ReportUnusable(SocketPort());
  }

  if (mCondition != NS_ERROR_CONNECTION_REFUSED &&
      mCondition != NS_ERROR_PROXY_CONNECTION_REFUSED &&
      mCondition != NS_ERROR_NET_TIMEOUT &&
      mCondition != NS_ERROR_UNKNOWN_HOST &&
      mCondition != NS_ERROR_UNKNOWN_PROXY_HOST) {
    SOCKET_LOG(("  not a recoverable error %" PRIx32,
                static_cast<uint32_t>(mCondition)));
    return false;
  }

  bool tryAgain = false;

  if ((mState == STATE_CONNECTING) && mDNSRecord) {
    if (mNetAddr.raw.family == AF_INET) {
      if (mSocketTransportService->IsTelemetryEnabledAndNotSleepPhase()) {
        glean::network::ipv4_and_ipv6_address_connectivity
            .AccumulateSingleSample(UNSUCCESSFUL_CONNECTING_TO_IPV4_ADDRESS);
      }
    } else if (mNetAddr.raw.family == AF_INET6) {
      if (mSocketTransportService->IsTelemetryEnabledAndNotSleepPhase()) {
        glean::network::ipv4_and_ipv6_address_connectivity
            .AccumulateSingleSample(UNSUCCESSFUL_CONNECTING_TO_IPV6_ADDRESS);
      }
    }
  }

  if (mConnectionFlags & RETRY_WITH_DIFFERENT_IP_FAMILY &&
      mCondition == NS_ERROR_UNKNOWN_HOST && mState == STATE_RESOLVING &&
      !mProxyTransparentResolvesHost) {
    SOCKET_LOG(("  trying lookup again with opposite ip family\n"));
    mConnectionFlags ^= (DISABLE_IPV6 | DISABLE_IPV4);
    mConnectionFlags &= ~RETRY_WITH_DIFFERENT_IP_FAMILY;
    // This will tell the consuming half-open to reset preference on the
    // connection entry
    mResetFamilyPreference = true;
    tryAgain = true;
  }

  // try next ip address only if past the resolver stage...
  if (mState == STATE_CONNECTING && mDNSRecord) {
    nsresult rv = mDNSRecord->GetNextAddr(SocketPort(), &mNetAddr);
    {
      bool resolvedByTRR;
      mDNSRecord->IsTRR(&resolvedByTRR);
      mResolvedByTRR = resolvedByTRR;
      nsIRequest::TRRMode effectiveTRRMode;
      mDNSRecord->GetEffectiveTRRMode(&effectiveTRRMode);
      mEffectiveTRRMode = effectiveTRRMode;
      nsITRRSkipReason::value trrSkipReason;
      mDNSRecord->GetTrrSkipReason(&trrSkipReason);
      mTRRSkipReason = trrSkipReason;
    }
    if (NS_SUCCEEDED(rv)) {
      SOCKET_LOG(("  trying again with next ip address\n"));
      tryAgain = true;
    } else if (mExternalDNSResolution) {
      mRetryDnsIfPossible = true;
      bool trrEnabled;
      mDNSRecord->IsTRR(&trrEnabled);
      // Bug 1648147 - If the server responded with `0.0.0.0` or `::` then we
      // should intentionally not fallback to regular DNS.
      if (trrEnabled && !StaticPrefs::network_trr_fallback_on_zero_response() &&
          ((mNetAddr.raw.family == AF_INET && mNetAddr.inet.ip == 0) ||
           (mNetAddr.raw.family == AF_INET6 && mNetAddr.inet6.ip.u64[0] == 0 &&
            mNetAddr.inet6.ip.u64[1] == 0))) {
        SOCKET_LOG(("  TRR returned 0.0.0.0 and there are no other IPs"));
        mRetryDnsIfPossible = false;
      }
    } else if (mConnectionFlags & RETRY_WITH_DIFFERENT_IP_FAMILY) {
      SOCKET_LOG(("  failed to connect, trying with opposite ip family\n"));
      // Drop state to closed.  This will trigger new round of DNS
      // resolving bellow.
      mState = STATE_CLOSED;
      mConnectionFlags ^= (DISABLE_IPV6 | DISABLE_IPV4);
      mConnectionFlags &= ~RETRY_WITH_DIFFERENT_IP_FAMILY;
      // This will tell the consuming half-open to reset preference on the
      // connection entry
      mResetFamilyPreference = true;
      tryAgain = true;
    } else if (!(mConnectionFlags & DISABLE_TRR)) {
      bool trrEnabled;
      mDNSRecord->IsTRR(&trrEnabled);

      // Bug 1648147 - If the server responded with `0.0.0.0` or `::` then we
      // should intentionally not fallback to regular DNS.
      if (!StaticPrefs::network_trr_fallback_on_zero_response() &&
          ((mNetAddr.raw.family == AF_INET && mNetAddr.inet.ip == 0) ||
           (mNetAddr.raw.family == AF_INET6 && mNetAddr.inet6.ip.u64[0] == 0 &&
            mNetAddr.inet6.ip.u64[1] == 0))) {
        SOCKET_LOG(("  TRR returned 0.0.0.0 and there are no other IPs"));
      } else if (trrEnabled) {
        nsIRequest::TRRMode trrMode = nsIRequest::TRR_DEFAULT_MODE;
        mDNSRecord->GetEffectiveTRRMode(&trrMode);
        // If current trr mode is trr only, we should not retry.
        if (trrMode != nsIRequest::TRR_ONLY_MODE) {
          // Drop state to closed.  This will trigger a new round of
          // DNS resolving. Bypass the cache this time since the
          // cached data came from TRR and failed already!
          SOCKET_LOG(("  failed to connect with TRR enabled, try w/o\n"));
          mState = STATE_CLOSED;
          mConnectionFlags |= DISABLE_TRR | BYPASS_CACHE | REFRESH_CACHE;
          tryAgain = true;
        }
      }
    }
  }

  // prepare to try again.
  if (tryAgain) {
    uint32_t msg;

    if (mState == STATE_CONNECTING) {
      mState = STATE_RESOLVING;
      msg = MSG_DNS_LOOKUP_COMPLETE;
    } else {
      mState = STATE_CLOSED;
      msg = MSG_ENSURE_CONNECT;
    }

    rv = PostEvent(msg, NS_OK);
    if (NS_FAILED(rv)) tryAgain = false;
  }

  return tryAgain;
}

// called on the socket thread only
void nsSocketTransport::OnMsgInputClosed(nsresult reason) {
  SOCKET_LOG(("nsSocketTransport::OnMsgInputClosed [this=%p reason=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(reason)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mInputClosed = true;
  // check if event should affect entire transport
  if (NS_FAILED(reason) && (reason != NS_BASE_STREAM_CLOSED)) {
    mCondition = reason;  // XXX except if NS_FAILED(mCondition), right??
  } else if (mOutputClosed) {
    mCondition =
        NS_BASE_STREAM_CLOSED;  // XXX except if NS_FAILED(mCondition), right??
  } else {
    if (mState == STATE_TRANSFERRING) mPollFlags &= ~PR_POLL_READ;
    mInput->OnSocketReady(reason);
  }
}

// called on the socket thread only
void nsSocketTransport::OnMsgOutputClosed(nsresult reason) {
  SOCKET_LOG(("nsSocketTransport::OnMsgOutputClosed [this=%p reason=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(reason)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mOutputClosed = true;
  // check if event should affect entire transport
  if (NS_FAILED(reason) && (reason != NS_BASE_STREAM_CLOSED)) {
    mCondition = reason;  // XXX except if NS_FAILED(mCondition), right??
  } else if (mInputClosed) {
    mCondition =
        NS_BASE_STREAM_CLOSED;  // XXX except if NS_FAILED(mCondition), right??
  } else {
    if (mState == STATE_TRANSFERRING) mPollFlags &= ~PR_POLL_WRITE;
    mOutput->OnSocketReady(reason);
  }
}

void nsSocketTransport::OnSocketConnected() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("  advancing to STATE_TRANSFERRING\n"));

  mPollFlags = (PR_POLL_READ | PR_POLL_WRITE | PR_POLL_EXCEPT);
  mState = STATE_TRANSFERRING;

  // Set the m*AddrIsSet flags only when state has reached TRANSFERRING
  // because we need to make sure its value does not change due to failover
  mNetAddrIsSet = true;

  // assign mFD (must do this within the transport lock), but take care not
  // to trample over mFDref if mFD is already set.
  {
    MutexAutoLock lock(mLock);
    NS_ASSERTION(mFD.IsInitialized(), "no socket");
    NS_ASSERTION(mFDref == 1, "wrong socket ref count");
    SetSocketName(mFD);
    mFDconnected = true;
    mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
  }

  // Ensure keepalive is configured correctly if previously enabled.
  if (mKeepaliveEnabled) {
    nsresult rv = SetKeepaliveEnabledInternal(true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      SOCKET_LOG(("  SetKeepaliveEnabledInternal failed rv[0x%" PRIx32 "]",
                  static_cast<uint32_t>(rv)));
    }
  }

  SendStatus(NS_NET_STATUS_CONNECTED_TO);
}

void nsSocketTransport::SetSocketName(PRFileDesc* fd) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mSelfAddrIsSet) {
    return;
  }

  PRNetAddr prAddr;
  memset(&prAddr, 0, sizeof(prAddr));
  if (PR_GetSockName(fd, &prAddr) == PR_SUCCESS) {
    PRNetAddrToNetAddr(&prAddr, &mSelfAddr);
    mSelfAddrIsSet = true;
  }
}

PRFileDesc* nsSocketTransport::GetFD_Locked() {
  mLock.AssertCurrentThreadOwns();

  // mFD is not available to the streams while disconnected.
  if (!mFDconnected) return nullptr;

  if (mFD.IsInitialized()) mFDref++;

  return mFD;
}

class ThunkPRClose : public Runnable {
 public:
  explicit ThunkPRClose(PRFileDesc* fd)
      : Runnable("net::ThunkPRClose"), mFD(fd) {}

  NS_IMETHOD Run() override {
    nsSocketTransport::CloseSocket(mFD);
    return NS_OK;
  }

 private:
  PRFileDesc* mFD;
};

void STS_PRCloseOnSocketTransport(PRFileDesc* fd, bool lingerPolarity,
                                  int16_t lingerTimeout) {
  if (gSocketTransportService) {
    // Can't PR_Close() a socket off STS thread. Thunk it to STS to die
    gSocketTransportService->Dispatch(new ThunkPRClose(fd), NS_DISPATCH_NORMAL);
  } else {
    // something horrible has happened
    NS_ASSERTION(gSocketTransportService, "No STS service");
  }
}

void nsSocketTransport::ReleaseFD_Locked(PRFileDesc* fd) {
  mLock.AssertCurrentThreadOwns();

  NS_ASSERTION(mFD == fd, "wrong fd");

  if (--mFDref == 0) {
    if (gIOService->IsNetTearingDown() &&
        ((PR_IntervalNow() - gIOService->NetTearingDownStarted()) >
         gSocketTransportService->MaxTimeForPrClosePref())) {
      // If shutdown last to long, let the socket leak and do not close it.
      SOCKET_LOG(("Intentional leak"));
    } else {
      if (mLingerPolarity || mLingerTimeout) {
        PRSocketOptionData socket_linger;
        socket_linger.option = PR_SockOpt_Linger;
        socket_linger.value.linger.polarity = mLingerPolarity;
        socket_linger.value.linger.linger = mLingerTimeout;
        PR_SetSocketOption(mFD, &socket_linger);
      }
      if (OnSocketThread()) {
        SOCKET_LOG(("nsSocketTransport: calling PR_Close [this=%p]\n", this));
        CloseSocket(mFD);
      } else {
        // Can't PR_Close() a socket off STS thread. Thunk it to STS to die
        STS_PRCloseOnSocketTransport(mFD, mLingerPolarity, mLingerTimeout);
      }
    }
    mFD = nullptr;
  }
}

//-----------------------------------------------------------------------------
// socket event handler impl

void nsSocketTransport::OnSocketEvent(uint32_t type, nsresult status,
                                      nsISupports* param,
                                      std::function<void()>&& task) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(
      ("nsSocketTransport::OnSocketEvent [this=%p type=%u status=%" PRIx32
       " param=%p]\n",
       this, type, static_cast<uint32_t>(status), param));

  if (NS_FAILED(mCondition)) {
    // block event since we're apparently already dead.
    SOCKET_LOG(("  blocking event [condition=%" PRIx32 "]\n",
                static_cast<uint32_t>(mCondition)));
    //
    // notify input/output streams in case either has a pending notify.
    //
    mInput->OnSocketReady(mCondition);
    mOutput->OnSocketReady(mCondition);
    return;
  }

  switch (type) {
    case MSG_ENSURE_CONNECT:
      SOCKET_LOG(("  MSG_ENSURE_CONNECT\n"));
      if (task) {
        task();
      }

      // Apply port remapping here so that we do it on the socket thread and
      // before we process the resolved DNS name or create the socket the first
      // time.
      if (!mPortRemappingApplied) {
        mPortRemappingApplied = true;

        mSocketTransportService->ApplyPortRemap(&mPort);
        mSocketTransportService->ApplyPortRemap(&mOriginPort);
      }

      //
      // ensure that we have created a socket, attached it, and have a
      // connection.
      //
      if (mState == STATE_CLOSED) {
        // Unix domain sockets are ready to connect; mNetAddr is all we
        // need. Internet address families require a DNS lookup (or possibly
        // several) before we can connect.
#if defined(XP_UNIX)
        if (mNetAddrIsSet && mNetAddr.raw.family == AF_LOCAL) {
          mCondition = InitiateSocket();
        } else {
#else
        {
#endif
          mCondition = ResolveHost();
        }

      } else {
        SOCKET_LOG(("  ignoring redundant event\n"));
      }
      break;

    case MSG_DNS_LOOKUP_COMPLETE:
      if (mDNSRequest) {  // only send this if we actually resolved anything
        SendStatus(NS_NET_STATUS_RESOLVED_HOST);
      }

      SOCKET_LOG(("  MSG_DNS_LOOKUP_COMPLETE\n"));
      mDNSRequest = nullptr;

      if (mDNSRecord) {
        mDNSRecord->GetNextAddr(SocketPort(), &mNetAddr);
        bool resolvedByTRR;
        mDNSRecord->IsTRR(&resolvedByTRR);
        mResolvedByTRR = resolvedByTRR;
        nsIRequest::TRRMode effectiveTRRMode;
        mDNSRecord->GetEffectiveTRRMode(&effectiveTRRMode);
        mEffectiveTRRMode = effectiveTRRMode;
        nsITRRSkipReason::value trrSkipReason;
        mDNSRecord->GetTrrSkipReason(&trrSkipReason);
        mTRRSkipReason = trrSkipReason;
      }
      // status contains DNS lookup status
      if (NS_FAILED(status)) {
        // When using a HTTP proxy, NS_ERROR_UNKNOWN_HOST means the HTTP
        // proxy host is not found, so we fixup the error code.
        // For SOCKS proxies (mProxyTransparent == true), the socket
        // transport resolves the real host here, so there's no fixup
        // (see bug 226943).
        if ((status == NS_ERROR_UNKNOWN_HOST) && !mProxyTransparent &&
            !mProxyHost.IsEmpty()) {
          mCondition = NS_ERROR_UNKNOWN_PROXY_HOST;
        } else {
          mCondition = status;
        }
      } else if (mState == STATE_RESOLVING) {
        mCondition = InitiateSocket();
      }
      break;

    case MSG_RETRY_INIT_SOCKET:
      mCondition = InitiateSocket();
      break;

    case MSG_INPUT_CLOSED:
      SOCKET_LOG(("  MSG_INPUT_CLOSED\n"));
      OnMsgInputClosed(status);
      break;

    case MSG_INPUT_PENDING:
      SOCKET_LOG(("  MSG_INPUT_PENDING\n"));
      OnMsgInputPending();
      break;

    case MSG_OUTPUT_CLOSED:
      SOCKET_LOG(("  MSG_OUTPUT_CLOSED\n"));
      OnMsgOutputClosed(status);
      break;

    case MSG_OUTPUT_PENDING:
      SOCKET_LOG(("  MSG_OUTPUT_PENDING\n"));
      OnMsgOutputPending();
      break;
    case MSG_TIMEOUT_CHANGED:
      SOCKET_LOG(("  MSG_TIMEOUT_CHANGED\n"));
      {
        MutexAutoLock lock(mLock);
        mPollTimeout =
            mTimeouts[(mState == STATE_TRANSFERRING) ? TIMEOUT_READ_WRITE
                                                     : TIMEOUT_CONNECT];
      }
      break;
    default:
      SOCKET_LOG(("  unhandled event!\n"));
  }

  if (NS_FAILED(mCondition)) {
    SOCKET_LOG(("  after event [this=%p cond=%" PRIx32 "]\n", this,
                static_cast<uint32_t>(mCondition)));
    if (!mAttached) {  // need to process this error ourselves...
      OnSocketDetached(nullptr);
    }
  } else if (mPollFlags == PR_POLL_EXCEPT) {
    mPollFlags = 0;  // make idle
  }
}

uint64_t nsSocketTransport::ByteCountReceived() { return mInput->ByteCount(); }

uint64_t nsSocketTransport::ByteCountSent() { return mOutput->ByteCount(); }

bool nsSocketTransport::IsTRRConnection() { return mIsTRRConnection; }

//-----------------------------------------------------------------------------
// socket handler impl

void nsSocketTransport::OnSocketReady(PRFileDesc* fd, int16_t outFlags) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG1(("nsSocketTransport::OnSocketReady [this=%p outFlags=%hd]\n",
               this, outFlags));

  if (outFlags == -1) {
    SOCKET_LOG(("socket timeout expired\n"));
    mCondition = NS_ERROR_NET_TIMEOUT;
    return;
  }

  if (mState == STATE_TRANSFERRING) {
    // if waiting to write and socket is writable or hit an exception.
    if ((mPollFlags & PR_POLL_WRITE) && (outFlags & ~PR_POLL_READ)) {
      // assume that we won't need to poll any longer (the stream will
      // request that we poll again if it is still pending).
      mPollFlags &= ~PR_POLL_WRITE;
      mOutput->OnSocketReady(NS_OK);
    }
    // if waiting to read and socket is readable or hit an exception.
    if ((mPollFlags & PR_POLL_READ) && (outFlags & ~PR_POLL_WRITE)) {
      // assume that we won't need to poll any longer (the stream will
      // request that we poll again if it is still pending).
      mPollFlags &= ~PR_POLL_READ;
      mInput->OnSocketReady(NS_OK);
    }
    // Update poll timeout in case it was changed
    {
      MutexAutoLock lock(mLock);
      mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
    }
  } else if ((mState == STATE_CONNECTING) && !gIOService->IsNetTearingDown()) {
    // We do not need to do PR_ConnectContinue when we are already
    // shutting down.

    PRStatus status = PR_ConnectContinue(fd, outFlags);

    if (status == PR_SUCCESS) {
      //
      // we are connected!
      //
      OnSocketConnected();

      if (mNetAddr.raw.family == AF_INET) {
        if (mSocketTransportService->IsTelemetryEnabledAndNotSleepPhase()) {
          glean::network::ipv4_and_ipv6_address_connectivity
              .AccumulateSingleSample(SUCCESSFUL_CONNECTING_TO_IPV4_ADDRESS);
        }
      } else if (mNetAddr.raw.family == AF_INET6) {
        if (mSocketTransportService->IsTelemetryEnabledAndNotSleepPhase()) {
          glean::network::ipv4_and_ipv6_address_connectivity
              .AccumulateSingleSample(SUCCESSFUL_CONNECTING_TO_IPV6_ADDRESS);
        }
      }
    } else {
      PRErrorCode code = PR_GetError();
#if defined(TEST_CONNECT_ERRORS)
      code = RandomizeConnectError(code);
#endif
      //
      // If the connect is still not ready, then continue polling...
      //
      if ((PR_WOULD_BLOCK_ERROR == code) || (PR_IN_PROGRESS_ERROR == code)) {
        // Set up the select flags for connect...
        mPollFlags = (PR_POLL_EXCEPT | PR_POLL_WRITE);
        // Update poll timeout in case it was changed
        {
          MutexAutoLock lock(mLock);
          mPollTimeout = mTimeouts[TIMEOUT_CONNECT];
        }
      }
      //
      // The SOCKS proxy rejected our request. Find out why.
      //
      else if (PR_UNKNOWN_ERROR == code && mProxyTransparent &&
               !mProxyHost.IsEmpty()) {
        code = PR_GetOSError();
        mCondition = ErrorAccordingToNSPR(code);
      } else {
        //
        // else, the connection failed...
        //
        mCondition = ErrorAccordingToNSPR(code);
        if ((mCondition == NS_ERROR_CONNECTION_REFUSED) &&
            !mProxyHost.IsEmpty()) {
          mCondition = NS_ERROR_PROXY_CONNECTION_REFUSED;
        }
        SOCKET_LOG(("  connection failed! [reason=%" PRIx32 "]\n",
                    static_cast<uint32_t>(mCondition)));
      }
    }
  } else if ((mState == STATE_CONNECTING) && gIOService->IsNetTearingDown()) {
    // We do not need to do PR_ConnectContinue when we are already
    // shutting down.
    SOCKET_LOG(
        ("We are in shutdown so skip PR_ConnectContinue and set "
         "and error.\n"));
    mCondition = NS_ERROR_ABORT;
  } else {
    NS_ERROR("unexpected socket state");
    mCondition = NS_ERROR_UNEXPECTED;
  }

  if (mPollFlags == PR_POLL_EXCEPT) mPollFlags = 0;  // make idle
}

// called on the socket thread only
void nsSocketTransport::OnSocketDetached(PRFileDesc* fd) {
  SOCKET_LOG(("nsSocketTransport::OnSocketDetached [this=%p cond=%" PRIx32
              "]\n",
              this, static_cast<uint32_t>(mCondition)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mAttached = false;

  // if we didn't initiate this detach, then be sure to pass an error
  // condition up to our consumers.  (e.g., STS is shutting down.)
  if (NS_SUCCEEDED(mCondition)) {
    if (gIOService->IsOffline()) {
      mCondition = NS_ERROR_OFFLINE;
    } else {
      mCondition = NS_ERROR_ABORT;
    }
  }

  // If we are not shutting down try again.
  if (!gIOService->IsNetTearingDown() && RecoverFromError()) {
    mCondition = NS_OK;
  } else {
    mState = STATE_CLOSED;

    // make sure there isn't any pending DNS request
    if (mDNSRequest) {
      mDNSRequest->Cancel(NS_ERROR_ABORT);
      mDNSRequest = nullptr;
    }

    //
    // notify input/output streams
    //
    mInput->OnSocketReady(mCondition);
    mOutput->OnSocketReady(mCondition);
    if (gIOService->IsNetTearingDown()) {
      if (mInputCopyContext) {
        NS_CancelAsyncCopy(mInputCopyContext, mCondition);
      }
      if (mOutputCopyContext) {
        NS_CancelAsyncCopy(mOutputCopyContext, mCondition);
      }
    }
  }

  if (mCondition == NS_ERROR_NET_RESET && mDNSRecord &&
      mOutput->ByteCount() == 0) {
    // If we are here, it's likely that we are retrying a transaction. Blocking
    // the already used address could increase the successful rate of the retry.
    mDNSRecord->ReportUnusable(SocketPort());
  }

  // finally, release our reference to the socket (must do this within
  // the transport lock) possibly closing the socket. Also release our
  // listeners to break potential refcount cycles.

  // We should be careful not to release mEventSink and mCallbacks while
  // we're locked, because releasing it might require acquiring the lock
  // again, so we just null out mEventSink and mCallbacks while we're
  // holding the lock, and let the stack based objects' destuctors take
  // care of destroying it if needed.
  nsCOMPtr<nsIInterfaceRequestor> ourCallbacks;
  nsCOMPtr<nsITransportEventSink> ourEventSink;
  {
    MutexAutoLock lock(mLock);
    if (mFD.IsInitialized()) {
      auto callback = std::move(mFDDetachCallback);
      if (callback) {
        callback(mFD);
      }
      ReleaseFD_Locked(mFD);
      // flag mFD as unusable; this prevents other consumers from
      // acquiring a reference to mFD.
      mFDconnected = false;
    }

    // We must release mCallbacks and mEventSink to avoid memory leak
    // but only when RecoverFromError() above failed. Otherwise we lose
    // link with UI and security callbacks on next connection attempt
    // round. That would lead e.g. to a broken certificate exception page.
    if (NS_FAILED(mCondition)) {
      mCallbacks.swap(ourCallbacks);
      mEventSink.swap(ourEventSink);
    }
  }
}

void nsSocketTransport::IsLocal(bool* aIsLocal) {
  {
    MutexAutoLock lock(mLock);

#if defined(XP_UNIX)
    // Unix-domain sockets are always local.
    if (mNetAddr.raw.family == PR_AF_LOCAL) {
      *aIsLocal = true;
      return;
    }
#endif

    *aIsLocal = mNetAddr.IsLoopbackAddr();
  }
}

//-----------------------------------------------------------------------------
// xpcom api

NS_IMPL_ISUPPORTS(nsSocketTransport, nsISocketTransport, nsITransport,
                  nsIDNSListener, nsIClassInfo, nsIInterfaceRequestor)
NS_IMPL_CI_INTERFACE_GETTER(nsSocketTransport, nsISocketTransport, nsITransport,
                            nsIDNSListener, nsIInterfaceRequestor)

NS_IMETHODIMP
nsSocketTransport::OpenInputStream(uint32_t flags, uint32_t segsize,
                                   uint32_t segcount,
                                   nsIInputStream** aResult) {
  SOCKET_LOG(
      ("nsSocketTransport::OpenInputStream [this=%p flags=%x]\n", this, flags));

  NS_ENSURE_TRUE(!mInput->IsReferenced(), NS_ERROR_UNEXPECTED);

  nsresult rv;
  nsCOMPtr<nsIAsyncInputStream> pipeIn;
  nsCOMPtr<nsIInputStream> result;
  nsCOMPtr<nsISupports> inputCopyContext;

  if (!(flags & OPEN_UNBUFFERED) || (flags & OPEN_BLOCKING)) {
    // XXX if the caller wants blocking, then the caller also gets buffered!
    // bool openBuffered = !(flags & OPEN_UNBUFFERED);
    bool openBlocking = (flags & OPEN_BLOCKING);

    net_ResolveSegmentParams(segsize, segcount);

    // create a pipe
    nsCOMPtr<nsIAsyncOutputStream> pipeOut;
    NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut), !openBlocking,
                true, segsize, segcount);

    // async copy from socket to pipe
    rv = NS_AsyncCopy(mInput.get(), pipeOut, mSocketTransportService,
                      NS_ASYNCCOPY_VIA_WRITESEGMENTS, segsize, nullptr, nullptr,
                      true, true, getter_AddRefs(inputCopyContext));
    if (NS_FAILED(rv)) return rv;

    result = pipeIn;
  } else {
    result = mInput.get();
  }

  // flag input stream as open
  mInputClosed = false;
  // mInputCopyContext can be only touched on socket thread
  auto task = [self = RefPtr{this}, inputCopyContext(inputCopyContext)]() {
    MOZ_ASSERT(OnSocketThread());
    self->mInputCopyContext = inputCopyContext;
  };
  rv = PostEvent(MSG_ENSURE_CONNECT, NS_OK, nullptr, std::move(task));
  if (NS_FAILED(rv)) {
    return rv;
  }

  result.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::OpenOutputStream(uint32_t flags, uint32_t segsize,
                                    uint32_t segcount,
                                    nsIOutputStream** aResult) {
  SOCKET_LOG(("nsSocketTransport::OpenOutputStream [this=%p flags=%x]\n", this,
              flags));

  NS_ENSURE_TRUE(!mOutput->IsReferenced(), NS_ERROR_UNEXPECTED);

  nsresult rv;
  nsCOMPtr<nsIAsyncOutputStream> pipeOut;
  nsCOMPtr<nsIOutputStream> result;
  nsCOMPtr<nsISupports> outputCopyContext;
  if (!(flags & OPEN_UNBUFFERED) || (flags & OPEN_BLOCKING)) {
    // XXX if the caller wants blocking, then the caller also gets buffered!
    // bool openBuffered = !(flags & OPEN_UNBUFFERED);
    bool openBlocking = (flags & OPEN_BLOCKING);

    net_ResolveSegmentParams(segsize, segcount);

    // create a pipe
    nsCOMPtr<nsIAsyncInputStream> pipeIn;
    NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut), true,
                !openBlocking, segsize, segcount);

    // async copy from socket to pipe
    rv = NS_AsyncCopy(pipeIn, mOutput.get(), mSocketTransportService,
                      NS_ASYNCCOPY_VIA_READSEGMENTS, segsize, nullptr, nullptr,
                      true, true, getter_AddRefs(outputCopyContext));
    if (NS_FAILED(rv)) return rv;

    result = pipeOut;
  } else {
    result = mOutput.get();
  }

  // flag output stream as open
  mOutputClosed = false;

  // mOutputCopyContext can be only touched on socket thread
  auto task = [self = RefPtr{this}, outputCopyContext(outputCopyContext)]() {
    MOZ_ASSERT(OnSocketThread());
    self->mOutputCopyContext = outputCopyContext;
  };
  rv = PostEvent(MSG_ENSURE_CONNECT, NS_OK, nullptr, std::move(task));
  if (NS_FAILED(rv)) return rv;

  result.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::Close(nsresult reason) {
  SOCKET_LOG(("nsSocketTransport::Close %p reason=%" PRIx32, this,
              static_cast<uint32_t>(reason)));

  if (NS_SUCCEEDED(reason)) reason = NS_BASE_STREAM_CLOSED;

  mDoNotRetryToConnect = true;

  mInput->CloseWithStatus(reason);
  mOutput->CloseWithStatus(reason);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetTlsSocketControl(nsITLSSocketControl** tlsSocketControl) {
  MutexAutoLock lock(mLock);
  *tlsSocketControl = do_AddRef(mTLSSocketControl).take();
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetSecurityCallbacks(nsIInterfaceRequestor** callbacks) {
  MutexAutoLock lock(mLock);
  *callbacks = do_AddRef(mCallbacks).take();
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetSecurityCallbacks(nsIInterfaceRequestor* callbacks) {
  nsCOMPtr<nsIInterfaceRequestor> threadsafeCallbacks;
  NS_NewNotificationCallbacksAggregation(callbacks, nullptr,
                                         GetCurrentSerialEventTarget(),
                                         getter_AddRefs(threadsafeCallbacks));
  MutexAutoLock lock(mLock);
  mCallbacks = threadsafeCallbacks;
  SOCKET_LOG(("Reset callbacks for tlsSocketInfo=%p callbacks=%p\n",
              mTLSSocketControl.get(), mCallbacks.get()));
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetEventSink(nsITransportEventSink* sink,
                                nsIEventTarget* target) {
  nsCOMPtr<nsITransportEventSink> temp;
  if (target) {
    nsresult rv =
        net_NewTransportEventSinkProxy(getter_AddRefs(temp), sink, target);
    if (NS_FAILED(rv)) return rv;
    sink = temp.get();
  }

  MutexAutoLock lock(mLock);
  mEventSink = sink;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::IsAlive(bool* result) {
  *result = false;

  nsresult conditionWhileLocked = NS_OK;
  PRFileDescAutoLock fd(this, &conditionWhileLocked);
  if (NS_FAILED(conditionWhileLocked) || !fd.IsInitialized()) {
    return NS_OK;
  }

  // XXX do some idle-time based checks??

  char c;
  int32_t rval = PR_Recv(fd, &c, 1, PR_MSG_PEEK, 0);

  if ((rval > 0) || (rval < 0 && PR_GetError() == PR_WOULD_BLOCK_ERROR)) {
    *result = true;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetHost(nsACString& host) {
  host = SocketHost();
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetPort(int32_t* port) {
  *port = (int32_t)SocketPort();
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aOriginAttributes) {
  if (NS_WARN_IF(!ToJSValue(aCx, mOriginAttributes, aOriginAttributes))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetScriptableOriginAttributes(
    JSContext* aCx, JS::Handle<JS::Value> aOriginAttributes) {
  MutexAutoLock lock(mLock);
  NS_ENSURE_FALSE(mFD.IsInitialized(), NS_ERROR_FAILURE);

  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  mOriginAttributes = std::move(attrs);
  return NS_OK;
}

nsresult nsSocketTransport::GetOriginAttributes(
    OriginAttributes* aOriginAttributes) {
  NS_ENSURE_ARG(aOriginAttributes);
  *aOriginAttributes = mOriginAttributes;
  return NS_OK;
}

nsresult nsSocketTransport::SetOriginAttributes(
    const OriginAttributes& aOriginAttributes) {
  MutexAutoLock lock(mLock);
  NS_ENSURE_FALSE(mFD.IsInitialized(), NS_ERROR_FAILURE);

  mOriginAttributes = aOriginAttributes;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetPeerAddr(NetAddr* addr) {
  // once we are in the connected state, mNetAddr will not change.
  // so if we can verify that we are in the connected state, then
  // we can freely access mNetAddr from any thread without being
  // inside a critical section.

  if (!mNetAddrIsSet) {
    SOCKET_LOG(
        ("nsSocketTransport::GetPeerAddr [this=%p state=%d] "
         "NOT_AVAILABLE because not yet connected.",
         this, mState));
    return NS_ERROR_NOT_AVAILABLE;
  }

  *addr = mNetAddr;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetSelfAddr(NetAddr* addr) {
  // once we are in the connected state, mSelfAddr will not change.
  // so if we can verify that we are in the connected state, then
  // we can freely access mSelfAddr from any thread without being
  // inside a critical section.

  if (!mSelfAddrIsSet) {
    SOCKET_LOG(
        ("nsSocketTransport::GetSelfAddr [this=%p state=%d] "
         "NOT_AVAILABLE because not yet connected.",
         this, mState));
    return NS_ERROR_NOT_AVAILABLE;
  }

  *addr = mSelfAddr;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::Bind(NetAddr* aLocalAddr) {
  NS_ENSURE_ARG(aLocalAddr);

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mAttached) {
    return NS_ERROR_FAILURE;
  }

  mBindAddr = MakeUnique<NetAddr>(*aLocalAddr);

  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetScriptablePeerAddr(nsINetAddr** addr) {
  NetAddr rawAddr;

  nsresult rv;
  rv = GetPeerAddr(&rawAddr);
  if (NS_FAILED(rv)) return rv;

  RefPtr<nsNetAddr> netaddr = new nsNetAddr(&rawAddr);
  netaddr.forget(addr);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetScriptableSelfAddr(nsINetAddr** addr) {
  NetAddr rawAddr;

  nsresult rv;
  rv = GetSelfAddr(&rawAddr);
  if (NS_FAILED(rv)) return rv;

  RefPtr<nsNetAddr> netaddr = new nsNetAddr(&rawAddr);
  netaddr.forget(addr);

  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetTimeout(uint32_t type, uint32_t* value) {
  NS_ENSURE_ARG_MAX(type, nsISocketTransport::TIMEOUT_READ_WRITE);
  MutexAutoLock lock(mLock);
  *value = (uint32_t)mTimeouts[type];
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetTimeout(uint32_t type, uint32_t value) {
  NS_ENSURE_ARG_MAX(type, nsISocketTransport::TIMEOUT_READ_WRITE);

  SOCKET_LOG(("nsSocketTransport::SetTimeout %p type=%u, value=%u", this, type,
              value));

  // truncate overly large timeout values.
  {
    MutexAutoLock lock(mLock);
    mTimeouts[type] = (uint16_t)std::min<uint32_t>(value, UINT16_MAX);
  }
  PostEvent(MSG_TIMEOUT_CHANGED);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetReuseAddrPort(bool reuseAddrPort) {
  mReuseAddrPort = reuseAddrPort;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetLinger(bool aPolarity, int16_t aTimeout) {
  MutexAutoLock lock(mLock);

  mLingerPolarity = aPolarity;
  mLingerTimeout = aTimeout;

  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetQoSBits(uint8_t aQoSBits) {
  // Don't do any checking here of bits.  Why?  Because as of RFC-4594
  // several different Class Selector and Assured Forwarding values
  // have been defined, but that isn't to say more won't be added later.
  // In that case, any checking would be an impediment to interoperating
  // with newer QoS definitions.

  mQoSBits = aQoSBits;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetQoSBits(uint8_t* aQoSBits) {
  *aQoSBits = static_cast<uint8_t>(mQoSBits);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetRecvBufferSize(uint32_t* aSize) {
  PRFileDescAutoLock fd(this);
  if (!fd.IsInitialized()) return NS_ERROR_NOT_CONNECTED;

  nsresult rv = NS_OK;
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_RecvBufferSize;
  if (PR_GetSocketOption(fd, &opt) == PR_SUCCESS) {
    *aSize = opt.value.recv_buffer_size;
  } else {
    rv = NS_ERROR_FAILURE;
  }

  return rv;
}

NS_IMETHODIMP
nsSocketTransport::GetSendBufferSize(uint32_t* aSize) {
  PRFileDescAutoLock fd(this);
  if (!fd.IsInitialized()) return NS_ERROR_NOT_CONNECTED;

  nsresult rv = NS_OK;
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_SendBufferSize;
  if (PR_GetSocketOption(fd, &opt) == PR_SUCCESS) {
    *aSize = opt.value.send_buffer_size;
  } else {
    rv = NS_ERROR_FAILURE;
  }

  return rv;
}

NS_IMETHODIMP
nsSocketTransport::SetRecvBufferSize(uint32_t aSize) {
  PRFileDescAutoLock fd(this);
  if (!fd.IsInitialized()) return NS_ERROR_NOT_CONNECTED;

  nsresult rv = NS_OK;
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_RecvBufferSize;
  opt.value.recv_buffer_size = aSize;
  if (PR_SetSocketOption(fd, &opt) != PR_SUCCESS) rv = NS_ERROR_FAILURE;

  return rv;
}

NS_IMETHODIMP
nsSocketTransport::SetSendBufferSize(uint32_t aSize) {
  PRFileDescAutoLock fd(this);
  if (!fd.IsInitialized()) return NS_ERROR_NOT_CONNECTED;

  nsresult rv = NS_OK;
  PRSocketOptionData opt;
  opt.option = PR_SockOpt_SendBufferSize;
  opt.value.send_buffer_size = aSize;
  if (PR_SetSocketOption(fd, &opt) != PR_SUCCESS) rv = NS_ERROR_FAILURE;

  return rv;
}

NS_IMETHODIMP
nsSocketTransport::OnLookupComplete(nsICancelable* request, nsIDNSRecord* rec,
                                    nsresult status) {
  SOCKET_LOG(("nsSocketTransport::OnLookupComplete: this=%p status %" PRIx32
              ".",
              this, static_cast<uint32_t>(status)));

  if (NS_SUCCEEDED(status)) {
    mDNSRecord = do_QueryInterface(rec);
    MOZ_ASSERT(mDNSRecord);
  }

  if (nsCOMPtr<nsIDNSAddrRecord> addrRecord = do_QueryInterface(rec)) {
    bool resolvedByTRR;
    addrRecord->IsTRR(&resolvedByTRR);
    mResolvedByTRR = resolvedByTRR;
    nsIRequest::TRRMode effectiveTRRMode;
    addrRecord->GetEffectiveTRRMode(&effectiveTRRMode);
    mEffectiveTRRMode = effectiveTRRMode;
    nsITRRSkipReason::value trrSkipReason;
    addrRecord->GetTrrSkipReason(&trrSkipReason);
    mTRRSkipReason = trrSkipReason;
  }

  // flag host lookup complete for the benefit of the ResolveHost method.
  mResolving = false;
  nsresult rv = PostEvent(MSG_DNS_LOOKUP_COMPLETE, status, nullptr);

  // if posting a message fails, then we should assume that the socket
  // transport has been shutdown.  this should never happen!  if it does
  // it means that the socket transport service was shutdown before the
  // DNS service.
  if (NS_FAILED(rv)) {
    NS_WARNING("unable to post DNS lookup complete message");
  }

  return NS_OK;
}

// nsIInterfaceRequestor
NS_IMETHODIMP
nsSocketTransport::GetInterface(const nsIID& iid, void** result) {
  if (iid.Equals(NS_GET_IID(nsIDNSRecord)) ||
      iid.Equals(NS_GET_IID(nsIDNSAddrRecord))) {
    return mDNSRecord ? mDNSRecord->QueryInterface(iid, result)
                      : NS_ERROR_NO_INTERFACE;
  }
  return this->QueryInterface(iid, result);
}

NS_IMETHODIMP
nsSocketTransport::GetInterfaces(nsTArray<nsIID>& array) {
  return NS_CI_INTERFACE_GETTER_NAME(nsSocketTransport)(array);
}

NS_IMETHODIMP
nsSocketTransport::GetScriptableHelper(nsIXPCScriptable** _retval) {
  *_retval = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetContractID(nsACString& aContractID) {
  aContractID.SetIsVoid(true);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassDescription(nsACString& aClassDescription) {
  aClassDescription.SetIsVoid(true);
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassID(nsCID** aClassID) {
  *aClassID = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetFlags(uint32_t* aFlags) {
  *aFlags = nsIClassInfo::THREADSAFE;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassIDNoAlloc(nsCID* aClassIDNoAlloc) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsSocketTransport::GetConnectionFlags(uint32_t* value) {
  *value = mConnectionFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetConnectionFlags(uint32_t value) {
  SOCKET_LOG(
      ("nsSocketTransport::SetConnectionFlags %p flags=%u", this, value));

  mConnectionFlags = value;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetIsPrivate(bool aIsPrivate) {
  mIsPrivate = aIsPrivate;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetIsTRRConnection(bool aIsTRRConnection) {
  mIsTRRConnection = aIsTRRConnection;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetTlsFlags(uint32_t* value) {
  *value = mTlsFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetTlsFlags(uint32_t value) {
  mTlsFlags = value;
  return NS_OK;
}

void nsSocketTransport::OnKeepaliveEnabledPrefChange(bool aEnabled) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // The global pref toggles keepalive as a system feature; it only affects
  // an individual socket if keepalive has been specifically enabled for it.
  // So, ensure keepalive is configured correctly if previously enabled.
  if (mKeepaliveEnabled) {
    nsresult rv = SetKeepaliveEnabledInternal(aEnabled);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      SOCKET_LOG(("  SetKeepaliveEnabledInternal [%s] failed rv[0x%" PRIx32 "]",
                  aEnabled ? "enable" : "disable", static_cast<uint32_t>(rv)));
    }
  }
}

nsresult nsSocketTransport::SetKeepaliveEnabledInternal(bool aEnable) {
  MOZ_ASSERT(mKeepaliveIdleTimeS > 0 && mKeepaliveIdleTimeS <= kMaxTCPKeepIdle);
  MOZ_ASSERT(mKeepaliveRetryIntervalS > 0 &&
             mKeepaliveRetryIntervalS <= kMaxTCPKeepIntvl);
  MOZ_ASSERT(mKeepaliveProbeCount > 0 &&
             mKeepaliveProbeCount <= kMaxTCPKeepCount);

  PRFileDescAutoLock fd(this);
  if (NS_WARN_IF(!fd.IsInitialized())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // Only enable if keepalives are globally enabled, but ensure other
  // options are set correctly on the fd.
  bool enable = aEnable && mSocketTransportService->IsKeepaliveEnabled();
  nsresult rv =
      fd.SetKeepaliveVals(enable, mKeepaliveIdleTimeS, mKeepaliveRetryIntervalS,
                          mKeepaliveProbeCount);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    SOCKET_LOG(("  SetKeepaliveVals failed rv[0x%" PRIx32 "]",
                static_cast<uint32_t>(rv)));
    return rv;
  }
  rv = fd.SetKeepaliveEnabled(enable);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    SOCKET_LOG(("  SetKeepaliveEnabled failed rv[0x%" PRIx32 "]",
                static_cast<uint32_t>(rv)));
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetKeepaliveEnabled(bool* aResult) {
  MOZ_ASSERT(aResult);

  *aResult = mKeepaliveEnabled;
  return NS_OK;
}

nsresult nsSocketTransport::EnsureKeepaliveValsAreInitialized() {
  nsresult rv = NS_OK;
  int32_t val = -1;
  if (mKeepaliveIdleTimeS == -1) {
    rv = mSocketTransportService->GetKeepaliveIdleTime(&val);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    mKeepaliveIdleTimeS = val;
  }
  if (mKeepaliveRetryIntervalS == -1) {
    rv = mSocketTransportService->GetKeepaliveRetryInterval(&val);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    mKeepaliveRetryIntervalS = val;
  }
  if (mKeepaliveProbeCount == -1) {
    rv = mSocketTransportService->GetKeepaliveProbeCount(&val);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    mKeepaliveProbeCount = val;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetKeepaliveEnabled(bool aEnable) {
#if defined(XP_WIN) || defined(XP_UNIX) || defined(XP_MACOSX)
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (aEnable == mKeepaliveEnabled) {
    SOCKET_LOG(("nsSocketTransport::SetKeepaliveEnabled [%p] already %s.", this,
                aEnable ? "enabled" : "disabled"));
    return NS_OK;
  }

  nsresult rv = NS_OK;
  if (aEnable) {
    rv = EnsureKeepaliveValsAreInitialized();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      SOCKET_LOG(
          ("  SetKeepaliveEnabled [%p] "
           "error [0x%" PRIx32 "] initializing keepalive vals",
           this, static_cast<uint32_t>(rv)));
      return rv;
    }
  }
  SOCKET_LOG(
      ("nsSocketTransport::SetKeepaliveEnabled [%p] "
       "%s, idle time[%ds] retry interval[%ds] packet count[%d]: "
       "globally %s.",
       this, aEnable ? "enabled" : "disabled", mKeepaliveIdleTimeS,
       mKeepaliveRetryIntervalS, mKeepaliveProbeCount,
       mSocketTransportService->IsKeepaliveEnabled() ? "enabled" : "disabled"));

  // Set mKeepaliveEnabled here so that state is maintained; it is possible
  // that we're in between fds, e.g. the 1st IP address failed, so we're about
  // to retry on a 2nd from the DNS record.
  mKeepaliveEnabled = aEnable;

  rv = SetKeepaliveEnabledInternal(aEnable);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    SOCKET_LOG(("  SetKeepaliveEnabledInternal failed rv[0x%" PRIx32 "]",
                static_cast<uint32_t>(rv)));
    return rv;
  }

  return NS_OK;
#else /* !(defined(XP_WIN) || defined(XP_UNIX) || defined(XP_MACOSX)) */
  SOCKET_LOG(("nsSocketTransport::SetKeepaliveEnabled unsupported platform"));
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
nsSocketTransport::SetKeepaliveVals(int32_t aIdleTime, int32_t aRetryInterval) {
#if defined(XP_WIN) || defined(XP_UNIX) || defined(XP_MACOSX)
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (NS_WARN_IF(aIdleTime <= 0 || kMaxTCPKeepIdle < aIdleTime)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (NS_WARN_IF(aRetryInterval <= 0 || kMaxTCPKeepIntvl < aRetryInterval)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aIdleTime == mKeepaliveIdleTimeS &&
      aRetryInterval == mKeepaliveRetryIntervalS) {
    SOCKET_LOG(
        ("nsSocketTransport::SetKeepaliveVals [%p] idle time "
         "already %ds and retry interval already %ds.",
         this, mKeepaliveIdleTimeS, mKeepaliveRetryIntervalS));
    return NS_OK;
  }
  mKeepaliveIdleTimeS = aIdleTime;
  mKeepaliveRetryIntervalS = aRetryInterval;

  nsresult rv = NS_OK;
  if (mKeepaliveProbeCount == -1) {
    int32_t val = -1;
    nsresult rv = mSocketTransportService->GetKeepaliveProbeCount(&val);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    mKeepaliveProbeCount = val;
  }

  SOCKET_LOG(
      ("nsSocketTransport::SetKeepaliveVals [%p] "
       "keepalive %s, idle time[%ds] retry interval[%ds] "
       "packet count[%d]",
       this, mKeepaliveEnabled ? "enabled" : "disabled", mKeepaliveIdleTimeS,
       mKeepaliveRetryIntervalS, mKeepaliveProbeCount));

  PRFileDescAutoLock fd(this);
  if (NS_WARN_IF(!fd.IsInitialized())) {
    return NS_ERROR_NULL_POINTER;
  }

  rv = fd.SetKeepaliveVals(mKeepaliveEnabled, mKeepaliveIdleTimeS,
                           mKeepaliveRetryIntervalS, mKeepaliveProbeCount);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
#else
  SOCKET_LOG(("nsSocketTransport::SetKeepaliveVals unsupported platform"));
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

#ifdef ENABLE_SOCKET_TRACING

#  include <stdio.h>
#  include <ctype.h>
#  include "prenv.h"

static void DumpBytesToFile(const char* path, const char* header,
                            const char* buf, int32_t n) {
  FILE* fp = fopen(path, "a");

  fprintf(fp, "\n%s [%d bytes]\n", header, n);

  const unsigned char* p;
  while (n) {
    p = (const unsigned char*)buf;

    int32_t i, row_max = std::min(16, n);

    for (i = 0; i < row_max; ++i) fprintf(fp, "%02x  ", *p++);
    for (i = row_max; i < 16; ++i) fprintf(fp, "    ");

    p = (const unsigned char*)buf;
    for (i = 0; i < row_max; ++i, ++p) {
      if (isprint(*p))
        fprintf(fp, "%c", *p);
      else
        fprintf(fp, ".");
    }

    fprintf(fp, "\n");
    buf += row_max;
    n -= row_max;
  }

  fprintf(fp, "\n");
  fclose(fp);
}

void nsSocketTransport::TraceInBuf(const char* buf, int32_t n) {
  char* val = PR_GetEnv("NECKO_SOCKET_TRACE_LOG");
  if (!val || !*val) return;

  nsAutoCString header;
  header.AssignLiteral("Reading from: ");
  header.Append(mHost);
  header.Append(':');
  header.AppendInt(mPort);

  DumpBytesToFile(val, header.get(), buf, n);
}

void nsSocketTransport::TraceOutBuf(const char* buf, int32_t n) {
  char* val = PR_GetEnv("NECKO_SOCKET_TRACE_LOG");
  if (!val || !*val) return;

  nsAutoCString header;
  header.AssignLiteral("Writing to: ");
  header.Append(mHost);
  header.Append(':');
  header.AppendInt(mPort);

  DumpBytesToFile(val, header.get(), buf, n);
}

#endif

static void LogNSPRError(const char* aPrefix, const void* aObjPtr) {
#if defined(DEBUG)
  PRErrorCode errCode = PR_GetError();
  int errLen = PR_GetErrorTextLength();
  nsAutoCString errStr;
  if (errLen > 0) {
    errStr.SetLength(errLen);
    PR_GetErrorText(errStr.BeginWriting());
  }
  NS_WARNING(nsPrintfCString("%s [%p] NSPR error[0x%x] %s.",
                             aPrefix ? aPrefix : "nsSocketTransport", aObjPtr,
                             errCode,
                             errLen > 0 ? errStr.get() : "<no error text>")
                 .get());
#endif
}

nsresult nsSocketTransport::PRFileDescAutoLock::SetKeepaliveEnabled(
    bool aEnable) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!(aEnable && !gSocketTransportService->IsKeepaliveEnabled()),
             "Cannot enable keepalive if global pref is disabled!");
  if (aEnable && !gSocketTransportService->IsKeepaliveEnabled()) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRSocketOptionData opt;

  opt.option = PR_SockOpt_Keepalive;
  opt.value.keep_alive = aEnable;
  PRStatus status = PR_SetSocketOption(mFd, &opt);
  if (NS_WARN_IF(status != PR_SUCCESS)) {
    LogNSPRError("nsSocketTransport::PRFileDescAutoLock::SetKeepaliveEnabled",
                 mSocketTransport);
    return ErrorAccordingToNSPR(PR_GetError());
  }
  return NS_OK;
}

static void LogOSError(const char* aPrefix, const void* aObjPtr) {
#if defined(DEBUG)
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

#  ifdef XP_WIN
  DWORD errCode = WSAGetLastError();
  char* errMessage;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 NULL, errCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 (LPSTR)&errMessage, 0, NULL);
  NS_WARNING(nsPrintfCString("%s [%p] OS error[0x%lx] %s",
                             aPrefix ? aPrefix : "nsSocketTransport", aObjPtr,
                             errCode,
                             errMessage ? errMessage : "<no error text>")
                 .get());
  LocalFree(errMessage);
#  else
  int errCode = errno;
  char* errMessage = strerror(errno);
  NS_WARNING(nsPrintfCString("%s [%p] OS error[0x%x] %s",
                             aPrefix ? aPrefix : "nsSocketTransport", aObjPtr,
                             errCode,
                             errMessage ? errMessage : "<no error text>")
                 .get());
#  endif
#endif
}

/* XXX PR_SetSockOpt does not support setting keepalive values, so native
 * handles and platform specific apis (setsockopt, WSAIOCtl) are used in this
 * file. Requires inclusion of NSPR private/pprio.h, and platform headers.
 */

nsresult nsSocketTransport::PRFileDescAutoLock::SetKeepaliveVals(
    bool aEnabled, int aIdleTime, int aRetryInterval, int aProbeCount) {
#if defined(XP_WIN) || defined(XP_UNIX) || defined(XP_MACOSX)
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (NS_WARN_IF(aIdleTime <= 0 || kMaxTCPKeepIdle < aIdleTime)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (NS_WARN_IF(aRetryInterval <= 0 || kMaxTCPKeepIntvl < aRetryInterval)) {
    return NS_ERROR_INVALID_ARG;
  }
  if (NS_WARN_IF(aProbeCount <= 0 || kMaxTCPKeepCount < aProbeCount)) {
    return NS_ERROR_INVALID_ARG;
  }

  PROsfd sock = PR_FileDesc2NativeHandle(mFd);
  if (NS_WARN_IF(sock == -1)) {
    LogNSPRError("nsSocketTransport::PRFileDescAutoLock::SetKeepaliveVals",
                 mSocketTransport);
    return ErrorAccordingToNSPR(PR_GetError());
  }
#endif

#if defined(XP_WIN)
  // Windows allows idle time and retry interval to be set; NOT ping count.
  struct tcp_keepalive keepalive_vals = {(u_long)aEnabled,
                                         // Windows uses msec.
                                         (u_long)(aIdleTime * 1000UL),
                                         (u_long)(aRetryInterval * 1000UL)};
  DWORD bytes_returned;
  int err =
      WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepalive_vals,
               sizeof(keepalive_vals), NULL, 0, &bytes_returned, NULL, NULL);
  if (NS_WARN_IF(err)) {
    LogOSError("nsSocketTransport WSAIoctl failed", mSocketTransport);
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;

#elif defined(XP_DARWIN)
  // Darwin uses sec; only supports idle time being set.
  int err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE, &aIdleTime,
                       sizeof(aIdleTime));
  if (NS_WARN_IF(err)) {
    LogOSError("nsSocketTransport Failed setting TCP_KEEPALIVE",
               mSocketTransport);
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;

#elif defined(XP_UNIX)
  // Not all *nix OSes support the following setsockopt() options
  // ... but we assume they are supported in the Android kernel;
  // build errors will tell us if they are not.
#  if defined(ANDROID) || defined(TCP_KEEPIDLE)
  // Idle time until first keepalive probe; interval between ack'd probes;
  // seconds.
  int err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &aIdleTime,
                       sizeof(aIdleTime));
  if (NS_WARN_IF(err)) {
    LogOSError("nsSocketTransport Failed setting TCP_KEEPIDLE",
               mSocketTransport);
    return NS_ERROR_UNEXPECTED;
  }

#  endif
#  if defined(ANDROID) || defined(TCP_KEEPINTVL)
  // Interval between unack'd keepalive probes; seconds.
  err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &aRetryInterval,
                   sizeof(aRetryInterval));
  if (NS_WARN_IF(err)) {
    LogOSError("nsSocketTransport Failed setting TCP_KEEPINTVL",
               mSocketTransport);
    return NS_ERROR_UNEXPECTED;
  }

#  endif
#  if defined(ANDROID) || defined(TCP_KEEPCNT)
  // Number of unack'd keepalive probes before connection times out.
  err = setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &aProbeCount,
                   sizeof(aProbeCount));
  if (NS_WARN_IF(err)) {
    LogOSError("nsSocketTransport Failed setting TCP_KEEPCNT",
               mSocketTransport);
    return NS_ERROR_UNEXPECTED;
  }

#  endif
  return NS_OK;
#else
  MOZ_ASSERT(false,
             "nsSocketTransport::PRFileDescAutoLock::SetKeepaliveVals "
             "called on unsupported platform!");
  return NS_ERROR_UNEXPECTED;
#endif
}

void nsSocketTransport::CloseSocket(PRFileDesc* aFd) {
#if defined(XP_WIN)
  AttachShutdownLayer(aFd);
#endif

  PR_Close(aFd);
}

NS_IMETHODIMP
nsSocketTransport::GetResetIPFamilyPreference(bool* aReset) {
  *aReset = mResetFamilyPreference;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetEchConfigUsed(bool* aEchConfigUsed) {
  *aEchConfigUsed = mEchConfigUsed;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetEchConfig(const nsACString& aEchConfig) {
  MutexAutoLock lock(mLock);
  mEchConfig = aEchConfig;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::ResolvedByTRR(bool* aResolvedByTRR) {
  *aResolvedByTRR = mResolvedByTRR;
  return NS_OK;
}

NS_IMETHODIMP nsSocketTransport::GetEffectiveTRRMode(
    nsIRequest::TRRMode* aEffectiveTRRMode) {
  *aEffectiveTRRMode = mEffectiveTRRMode;
  return NS_OK;
}

NS_IMETHODIMP nsSocketTransport::GetTrrSkipReason(
    nsITRRSkipReason::value* aSkipReason) {
  *aSkipReason = mTRRSkipReason;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetRetryDnsIfPossible(bool* aRetryDns) {
  *aRetryDns = mRetryDnsIfPossible;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetStatus(nsresult* aStatus) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  *aStatus = mCondition;
  return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetIsTRRConnection(bool* aIsTRRConnection) {
  *aIsTRRConnection = mIsTRRConnection;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
