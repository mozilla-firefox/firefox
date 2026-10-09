/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TLSTransportLayer_h_
#define TLSTransportLayer_h_

#include "nsSocketTransportService2.h"
#include "nsIInterfaceRequestor.h"
#include "nsISocketTransport.h"
#include "nsIAsyncInputStream.h"
#include "nsIAsyncOutputStream.h"
#include "prio.h"

namespace mozilla::net {

// TLSTransportLayer will provide a secondary TLS layer. It will be added as a
// layer between nsHttpConnection and nsSocketTransport.
// The mSocketTransport, mSocketIn, and mSocketOut of nsHttpConnection will be
// replaced by TLSTransportLayer.
//
// The input path of reading data from a socket is shown below.
// nsHttpConnection::OnSocketReadable
// nsHttpConnection::OnWriteSegment
// nsHttpConnection::mSocketIn->Read
// TLSTransportLayer::InputStreamWrapper::Read
// TLSTransportLayer::InputInternal
// TLSTransportLayer::InputStreamWrapper::ReadDirectly
// nsSocketInputStream::Read
//
// The output path of writing data to a socket is shown below.
// nsHttpConnection::OnSocketWritable
// nsHttpConnection::OnReadSegment
// TLSTransportLayer::OutputStreamWrapper::Write
// TLSTransportLayer::OutputInternal
// TLSTransportLayer::OutputStreamWrapper::WriteDirectly
// nsSocketOutputStream::Write

// 9d6a3bc6-1f90-41d0-9b02-33ccd169052b
#define NS_TLSTRANSPORTLAYER_IID \
  {0x9d6a3bc6, 0x1f90, 0x41d0, {0x9b, 0x02, 0x33, 0xcc, 0xd1, 0x69, 0x05, 0x2b}}

class TLSTransportLayer final : public nsISocketTransport,
                                public nsIInputStreamCallback,
                                public nsIOutputStreamCallback {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_TLSTRANSPORTLAYER_IID)
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITRANSPORT
  NS_DECL_NSISOCKETTRANSPORT
  NS_DECL_NSIINPUTSTREAMCALLBACK
  NS_DECL_NSIOUTPUTSTREAMCALLBACK

  explicit TLSTransportLayer(nsISocketTransport* aTransport,
                             nsIAsyncInputStream* aInputStream,
                             nsIAsyncOutputStream* aOutputStream,
                             nsIInputStreamCallback* aOwner);
  bool Init(const char* aTLSHost, int32_t aTLSPort);
  already_AddRefed<nsIAsyncInputStream> GetInputStreamWrapper() {
    nsCOMPtr<nsIAsyncInputStream> stream = &mSocketInWrapper;
    return stream.forget();
  }
  already_AddRefed<nsIAsyncOutputStream> GetOutputStreamWrapper() {
    nsCOMPtr<nsIAsyncOutputStream> stream = &mSocketOutWrapper;
    return stream.forget();
  }

  bool HasDataToRecv();

  void ReleaseOwner() { mOwner = nullptr; }

 private:
  class InputStreamWrapper : public nsIAsyncInputStream {
   public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIINPUTSTREAM
    NS_DECL_NSIASYNCINPUTSTREAM

    explicit InputStreamWrapper(nsIAsyncInputStream* aInputStream,
                                TLSTransportLayer* aTransport);

    nsresult ReadDirectly(char* buf, uint32_t count, uint32_t* countRead);
    nsresult Status() { return mStatus; }
    void SetStatus(nsresult aStatus) { mStatus = aStatus; }

   private:
    friend class TLSTransportLayer;
    virtual ~InputStreamWrapper() = default;
    nsresult ReturnDataFromBuffer(char* buf, uint32_t count,
                                  uint32_t* countRead);

    nsCOMPtr<nsIAsyncInputStream> mSocketIn;

    nsresult mStatus{NS_OK};
    // The lifetime of InputStreamWrapper and OutputStreamWrapper are bound to
    // TLSTransportLayer, so using |mTransport| as a raw pointer should be safe.
    TLSTransportLayer* MOZ_OWNING_REF mTransport;
  };

  class OutputStreamWrapper : public nsIAsyncOutputStream {
   public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIOUTPUTSTREAM
    NS_DECL_NSIASYNCOUTPUTSTREAM

    explicit OutputStreamWrapper(nsIAsyncOutputStream* aOutputStream,
                                 TLSTransportLayer* aTransport);

    nsresult WriteDirectly(const char* buf, uint32_t count,
                           uint32_t* countWritten);
    nsresult Status() { return mStatus; }
    void SetStatus(nsresult aStatus) { mStatus = aStatus; }

   private:
    friend class TLSTransportLayer;
    virtual ~OutputStreamWrapper() = default;
    static nsresult WriteFromSegments(nsIInputStream*, void*, const char*,
                                      uint32_t offset, uint32_t count,
                                      uint32_t* countRead);

    nsCOMPtr<nsIAsyncOutputStream> mSocketOut;

    nsresult mStatus{NS_OK};
    TLSTransportLayer* MOZ_OWNING_REF mTransport;
  };

  virtual ~TLSTransportLayer();
  bool DispatchRelease();

  nsISocketTransport* Transport() { return mSocketTransport; }

  void MaybeWakeWaiter(int32_t aPollResult, int16_t aOutFlags, bool aInput);

  int32_t OutputInternal(const char* aBuf, int32_t aAmount);
  int32_t InputInternal(char* aBuf, int32_t aAmount);

  static PRStatus GetPeerName(PRFileDesc* fd, PRNetAddr* addr);
  static PRStatus GetSocketOption(PRFileDesc* fd, PRSocketOptionData* aOpt);
  static PRStatus SetSocketOption(PRFileDesc* fd,
                                  const PRSocketOptionData* data);
  static int32_t Write(PRFileDesc* fd, const void* buf, int32_t amount);
  static int32_t Read(PRFileDesc* fd, void* buf, int32_t amount);
  static int32_t Send(PRFileDesc* fd, const void* buf, int32_t amount,
                      int flags, PRIntervalTime timeout);
  static int32_t Recv(PRFileDesc* fd, void* buf, int32_t amount, int flags,
                      PRIntervalTime timeout);
  static PRStatus Close(PRFileDesc* fd);
  static int16_t Poll(PRFileDesc* fd, int16_t in_flags, int16_t* out_flags);

  nsCOMPtr<nsISocketTransport> mSocketTransport;
  InputStreamWrapper mSocketInWrapper;
  OutputStreamWrapper mSocketOutWrapper;
  nsCOMPtr<nsITLSSocketControl> mTLSSocketControl;
  // Whether the last PR_Poll reached this layer's Poll(). See MaybeWakeWaiter.
  bool mPollCalled{false};

  nsCOMPtr<nsIInputStreamCallback> mInputCallback;
  nsCOMPtr<nsIOutputStreamCallback> mOutputCallback;
  PRFileDesc* mFD{nullptr};
  nsCOMPtr<nsIInputStreamCallback> mOwner;
  nsresult mOutputStatus{NS_OK};
  nsresult mInputStatus{NS_OK};
};

}  // namespace mozilla::net

inline nsISupports* ToSupports(mozilla::net::TLSTransportLayer* aTransport) {
  return static_cast<nsISocketTransport*>(aTransport);
}

#endif
