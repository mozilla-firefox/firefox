/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// gtests for HappyEyeballsConnectionAttempt. Two injected seams exercise the
// dispatch logic without real sockets: a fake ConnectionEstablisherFactory
// (establishers complete on demand) and a recording
// HappyEyeballsConnMgrDelegate (intercepts connection-manager/entry calls). The
// real Rust engine stays live, driven via its FFI inputs; an IP-literal origin
// avoids DNS.

#include "ConnectionEntry.h"
#include "ConnectionEstablisher.h"
#include "HappyEyeballsConnMgrDelegate.h"
#include "HappyEyeballsConnectionAttempt.h"
#include "HappyEyeballsTransaction.h"
#include "HttpConnectionBase.h"
#include "HttpTrafficAnalyzer.h"
#include "NullHttpTransaction.h"
#include "PendingTransactionInfo.h"
#include "ZeroRttHandle.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/net/ClassOfService.h"
#include "mozilla/net/DNS.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpRequestHead.h"
#include "nsHttpTransaction.h"
#include "nsIHttpProtocolHandler.h"
#include "nsISeekableStream.h"
#include "nsISocketTransportService.h"
#include "nsNetAddr.h"
#include "nsServiceManagerUtils.h"
#include "nsSocketTransportService2.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

namespace {

// Minimal HttpConnectionBase: trivial pure-virtual bodies plus a few knobs
// (UsingSpdy/UsingHttp3, Activate result) the success path reads.
class FakeHttpConnection final : public HttpConnectionBase {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit FakeHttpConnection(bool aUsingHttp3, bool aUsingSpdy)
      : mUsingHttp3(aUsingHttp3), mUsingSpdy(aUsingSpdy) {}

  bool UsingSpdy() override { return mUsingSpdy; }
  bool UsingHttp3() override { return mUsingHttp3; }

  [[nodiscard]] nsresult Activate(nsAHttpTransaction*, uint32_t,
                                  int32_t) override {
    return NS_OK;
  }
  void Close(nsresult, bool) override {}
  bool CanReuse() override { return false; }
  bool CanDirectlyActivate() override { return false; }
  void DontReuse() override {}
  nsAHttpTransaction* Transaction() override { return nullptr; }
  void CloseTransaction(nsAHttpTransaction*, nsresult, bool) override {}
  [[nodiscard]] nsresult OnHeadersAvailable(nsAHttpTransaction*,
                                            nsHttpRequestHead*,
                                            nsHttpResponseHead*,
                                            bool*) override {
    return NS_OK;
  }
  [[nodiscard]] nsresult TakeTransport(nsISocketTransport**,
                                       nsIAsyncInputStream**,
                                       nsIAsyncOutputStream**) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  void PrintDiagnostics(nsCString&) override {}
  bool TestJoinConnection(const nsACString&, int32_t) override { return false; }
  bool JoinConnection(const nsACString&, int32_t) override { return false; }
  void GetTLSSocketControl(nsITLSSocketControl** aResult) override {
    *aResult = nullptr;
  }
  [[nodiscard]] nsresult ResumeSend() override { return NS_OK; }
  [[nodiscard]] nsresult ResumeRecv() override { return NS_OK; }
  [[nodiscard]] nsresult ForceSend() override { return NS_OK; }
  [[nodiscard]] nsresult ForceRecv() override { return NS_OK; }
  HttpVersion Version() override {
    return mUsingHttp3 ? HttpVersion::v3_0
                       : (mUsingSpdy ? HttpVersion::v2_0 : HttpVersion::v1_1);
  }
  bool LastTransactionExpectedNoContent() override { return false; }
  void SetLastTransactionExpectedNoContent(bool) override {}
  int64_t BytesWritten() override { return 0; }
  bool IsPersistent() override { return true; }
  bool IsReused() override { return false; }
  [[nodiscard]] nsresult PushBack(const char*, uint32_t) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  void SetEvent(nsresult) override {}
  nsresult GetSelfAddr(NetAddr* aAddr) override {
    *aAddr = NetAddr();
    return NS_OK;
  }
  nsresult GetPeerAddr(NetAddr* aAddr) override {
    *aAddr = NetAddr();
    return NS_OK;
  }
  bool ResolvedByTRR() override { return false; }
  nsIRequest::TRRMode EffectiveTRRMode() override {
    return nsIRequest::TRR_DEFAULT_MODE;
  }
  TRRSkippedReason TRRSkipReason() override {
    return nsITRRSkipReason::TRR_UNSET;
  }
  bool GetEchConfigUsed() override { return false; }
  PRIntervalTime LastWriteTime() override { return 0; }
  [[nodiscard]] nsresult CreateTunnelStream(nsAHttpTransaction*,
                                            HttpConnectionBase**,
                                            bool) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

 private:
  ~FakeHttpConnection() = default;
  bool mUsingHttp3;
  bool mUsingSpdy;
};

NS_IMPL_ISUPPORTS(FakeHttpConnection, nsISupportsWeakReference)

// Establisher that captures the completion callback instead of opening a
// socket. The test fires the result whenever it wants.
class FakeConnectionEstablisher final : public ConnectionEstablisher {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(FakeConnectionEstablisher,
                                       ConnectionEstablisher)

  FakeConnectionEstablisher(nsHttpConnectionInfo* aConnInfo,
                            const NetAddr& aAddr, uint32_t aCaps, bool aIsUDP,
                            bool aAllow1918, bool aForceRefuseLocal)
      : ConnectionEstablisher(aConnInfo, aAddr, aCaps, aAllow1918),
        mIsUDP(aIsUDP),
        mForceRefuseLocal(aForceRefuseLocal) {}

  bool Start(DoneCallback&& aCallback) override {
    mCallback = std::move(aCallback);
    // Mirror the real establishers: refuse a local peer when !mAllow1918.
    // mForceRefuseLocal lets a test simulate a refusal made while speculative
    // even after Claim() flipped allow1918 to true.
    if (mForceRefuseLocal && mAddr.IsIPAddrLocal()) {
      mRefusedForLocalAddress = true;
      return false;
    }
    if (RefuseIfLocalAddress()) {
      return false;
    }
    return true;
  }
  void Close(nsresult) override { ++mCloseCount; }
  void ResetSpeculativeFlags() override {}
  bool IsUDP() const override { return mIsUDP; }

  void FireSuccess(HttpConnectionBase* aConn) {
    mResultConn = aConn;
    mCallback(RefPtr<HttpConnectionBase>(aConn));
  }
  void FireError(nsresult aError) { mCallback(Err(aError)); }

  uint32_t mCloseCount = 0;

 private:
  ~FakeConnectionEstablisher() = default;
  void Finish(nsresult) override {}
  bool mIsUDP;
  bool mForceRefuseLocal;
};

class FakeConnectionEstablisherFactory final
    : public ConnectionEstablisherFactory {
 public:
  already_AddRefed<ConnectionEstablisher> Create(
      ConnectionEstablisherType aType, nsHttpConnectionInfo* aConnInfo,
      const NetAddr& aAddr, uint32_t aCaps, bool, bool aAllow1918) override {
    bool isUDP = aType == ConnectionEstablisherType::UDP;
    RefPtr<FakeConnectionEstablisher> e = new FakeConnectionEstablisher(
        aConnInfo, aAddr, aCaps, isUDP, aAllow1918, mForceRefuseLocal);
    (isUDP ? mUDP : mTCP).AppendElement(e);
    return e.forget();
  }

  nsTArray<RefPtr<FakeConnectionEstablisher>> mTCP;
  nsTArray<RefPtr<FakeConnectionEstablisher>> mUDP;
  // When set, created establishers refuse a local peer even if allow1918 is
  // true, to simulate a refusal made while the attempt was still speculative.
  bool mForceRefuseLocal = false;
};

class RecordingConnMgrDelegate final : public HappyEyeballsConnMgrDelegate {
 public:
  already_AddRefed<PendingTransactionInfo> FindTransaction(
      bool, ConnectionEntry*, nsAHttpTransaction*) override {
    mCalls.AppendElement("FindTransaction"_ns);
    return do_AddRef(mFindResult);
  }
  nsresult DispatchTransaction(ConnectionEntry*, nsHttpTransaction* aTrans,
                               HttpConnectionBase* aConn) override {
    mCalls.AppendElement("DispatchTransaction"_ns);
    // Like the real ConnMgr, bind the transaction to the connection. Keep the
    // handle so the test can Reset it before teardown (~ConnectionHandle would
    // otherwise reclaim the fake connection and crash).
    if (mSimulateDispatchBindsConnection && aTrans && aConn) {
      RefPtr<ConnectionHandle> handle = new ConnectionHandle(aConn);
      aTrans->SetConnection(handle);
      mDispatchHandles.AppendElement(handle);
    }
    return mDispatchRv;
  }
  void AddTransaction(nsHttpTransaction*, int32_t) override {
    mCalls.AppendElement("AddTransaction"_ns);
  }
  void ReportSpdyConnection(HttpConnectionBase*, bool, bool) override {
    mCalls.AppendElement("ReportSpdyConnection"_ns);
  }
  void ReportHttp3Connection(HttpConnectionBase*, ConnectionEntry*) override {
    mCalls.AppendElement("ReportHttp3Connection"_ns);
  }
  void ReclaimConnection(HttpConnectionBase*) override {
    mCalls.AppendElement("ReclaimConnection"_ns);
  }
  void ProcessSpdyPendingQ(ConnectionEntry*) override {
    mCalls.AppendElement("ProcessSpdyPendingQ"_ns);
  }
  already_AddRefed<ConnectionEntry> HandOffHttp3OnlyConnection(
      HttpConnectionBase*, ConnectionEntry*) override {
    mCalls.AppendElement("HandOffHttp3OnlyConnection"_ns);
    return nullptr;
  }
  void InsertIntoActiveConns(ConnectionEntry*, HttpConnectionBase*) override {
    mCalls.AppendElement("InsertIntoActiveConns"_ns);
  }
  void RemoveConnectionAttempt(ConnectionEntry*, ConnectionAttempt*,
                               bool) override {
    mCalls.AppendElement("RemoveConnectionAttempt"_ns);
  }
  void RecordIPFamilyPreference(ConnectionEntry*, uint16_t) override {
    mCalls.AppendElement("RecordIPFamilyPreference"_ns);
  }
  void ResetIPFamilyPreference(ConnectionEntry*) override {
    mCalls.AppendElement("ResetIPFamilyPreference"_ns);
  }
  bool MaybeProcessCoalescingKeys(ConnectionEntry*, const nsTArray<NetAddr>&,
                                  bool) override {
    mCalls.AppendElement("MaybeProcessCoalescingKeys"_ns);
    return false;
  }
  bool RemoveTransFromPendingQ(ConnectionEntry*, nsHttpTransaction*) override {
    mCalls.AppendElement("RemoveTransFromPendingQ"_ns);
    return false;
  }
  nsresult StartRetry(ConnectionEntry*, nsHttpTransaction*, uint32_t, bool,
                      bool, bool, bool aRetryWithoutTRR) override {
    mCalls.AppendElement(aRetryWithoutTRR ? "StartRetryWithoutTRR"_ns
                                          : "StartRetryForLocalAddress"_ns);
    return mStartRetryRv;
  }

  int32_t Count(const char* aName) const {
    int32_t n = 0;
    for (const auto& s : mCalls) {
      if (s.EqualsASCII(aName)) {
        n++;
      }
    }
    return n;
  }

  // Clear the connection handles created by DispatchTransaction so their
  // destructors don't reclaim the fake connections.
  void ResetHandles() {
    for (auto& handle : mDispatchHandles) {
      handle->Reset();
    }
  }

  nsTArray<nsCString> mCalls;
  RefPtr<PendingTransactionInfo> mFindResult;
  nsresult mDispatchRv = NS_OK;
  nsresult mStartRetryRv = NS_OK;
  bool mSimulateDispatchBindsConnection = false;
  nsTArray<RefPtr<ConnectionHandle>> mDispatchHandles;
};

struct TestHarness {
  RefPtr<nsHttpConnectionInfo> mConnInfo;
  RefPtr<nsAHttpTransaction> mTrans;
  nsTHashSet<ConnectionEntry*> mPendingQSet;
  RefPtr<ConnectionEntry> mEntry;
  RefPtr<FakeConnectionEstablisherFactory> mFactory;
  RefPtr<RecordingConnMgrDelegate> mDelegate;
  RefPtr<HappyEyeballsConnectionAttempt> mHE;

  // aRealTransaction: use a real nsHttpTransaction (QueryHttpTransaction()
  // non-null) for paths that touch the real request; else a
  // NullHttpTransaction.
  explicit TestHarness(uint32_t aCaps = 0, bool aRealTransaction = false,
                       const nsACString& aOrigin = "127.0.0.1"_ns,
                       bool aSpeculative = false, bool aAllow1918 = true) {
    mConnInfo = new nsHttpConnectionInfo(aOrigin, 443, ""_ns, ""_ns, nullptr,
                                         OriginAttributes(),
                                         /*endToEndSSL*/ true);
    if (aRealTransaction) {
      mTrans = new nsHttpTransaction();
    } else {
      mTrans = new NullHttpTransaction(mConnInfo, nullptr, aCaps);
    }
    mEntry = new ConnectionEntry(mConnInfo, mPendingQSet);
    mFactory = new FakeConnectionEstablisherFactory();
    mDelegate = new RecordingConnMgrDelegate();
    mHE = new HappyEyeballsConnectionAttempt(mConnInfo, mTrans, aCaps,
                                             aSpeculative,
                                             /*urgentStart*/ false);
    mHE->SetAllow1918(aAllow1918);
    mHE->SetConnectionEstablisherFactoryForTesting(mFactory);
    mHE->SetConnMgrDelegateForTesting(mDelegate);
  }

  void Init() { mHE->Init(mEntry); }
};

// Ensure gHttpHandler is initialized (the HCA ctor notifies HTTP activity
// through it). Must run on the main thread.
static void EnsureHttpHandler() {
  nsCOMPtr<nsIHttpProtocolHandler> http =
      do_GetService("@mozilla.org/network/protocol;1?name=http");
  ASSERT_TRUE(http);
}

// Run `aFn` on the socket thread and block until it completes.
static void RunOnSocketThread(std::function<void()>&& aFn) {
  nsCOMPtr<nsIEventTarget> sts = gSocketTransportService;
  ASSERT_TRUE(sts);
  NS_DispatchAndSpinEventLoopUntilComplete(
      "TestHappyEyeballs"_ns, sts,
      NS_NewRunnableFunction("TestHappyEyeballs", std::move(aFn)));
}

}  // namespace

// Init() with an IP-literal origin should drive the engine to attempt a
// connection through the injected factory rather than opening a real socket.
TEST(HappyEyeballsConnectionAttempt, FactorySeamUsed)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h;
    h.Init();
    EXPECT_GE(h.mFactory->mTCP.Length(), 1u)
        << "engine should have requested a TCP establisher via the factory";
    EXPECT_EQ(h.mFactory->mUDP.Length(), 0u);
    h.mHE->Abandon();
  });
}

// Firing a successful TCP connection runs Succeeded -> EnterSucceeded ->
// ProcessTCPConn, which dispatches the pending real transaction onto the
// winning connection through the delegate.
TEST(HappyEyeballsConnectionAttempt, TcpSuccessRunsDispatchViaDelegate)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h(/*caps*/ 0, /*realTransaction*/ true);
    nsHttpTransaction* realTrans = h.mTrans->QueryHttpTransaction();
    ASSERT_TRUE(realTrans);
    // The pending-queue lookup returns the real transaction so ProcessTCPConn
    // dispatches it rather than reclaiming the connection.
    h.mDelegate->mFindResult = new PendingTransactionInfo(realTrans);
    h.Init();
    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);

    RefPtr<FakeHttpConnection> conn =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ false);
    h.mFactory->mTCP[0]->FireSuccess(conn);

    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("RecordIPFamilyPreference"), 1);
    EXPECT_EQ(h.mDelegate->Count("InsertIntoActiveConns"), 1);
    EXPECT_EQ(h.mDelegate->Count("FindTransaction"), 1);
    EXPECT_EQ(h.mDelegate->Count("DispatchTransaction"), 1)
        << "the pending transaction should be dispatched onto the connection";
    EXPECT_TRUE(h.mHE->WasTransactionAdoptedForTesting());
    EXPECT_EQ(h.mDelegate->Count("ReportSpdyConnection"), 1);
  });
}

// A failing connection result (the only address) drives the engine to Failed
// and EnterFailed, which removes the attempt and never dispatches.
TEST(HappyEyeballsConnectionAttempt, TcpFailureDoesNotDispatch)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h;
    h.Init();
    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);

    h.mFactory->mTCP[0]->FireError(NS_ERROR_CONNECTION_REFUSED);

    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("DispatchTransaction"), 0);
    EXPECT_EQ(h.mDelegate->Count("InsertIntoActiveConns"), 0);
    EXPECT_GE(h.mDelegate->Count("RemoveConnectionAttempt"), 1);
  });
}

// A speculative attempt to a local peer refuses it synchronously instead of
// opening a socket, and -- being unclaimed -- fails without retrying.
TEST(HappyEyeballsConnectionAttempt, SpeculativeRefusesLocalAddress)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h(/*caps*/ 0, /*realTransaction*/ false, "10.0.0.2"_ns,
                  /*speculative*/ true, /*allow1918*/ false);
    h.Init();

    // The engine still asks the factory for a TCP establisher for the local
    // address, but Start() refuses it instead of connecting.
    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);
    EXPECT_TRUE(h.mFactory->mTCP[0]->RefusedForLocalAddress())
        << "a speculative establisher must refuse a local peer";

    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("DispatchTransaction"), 0);
    // Unclaimed speculative attempt: no real transaction, so no retry.
    EXPECT_EQ(h.mDelegate->Count("StartRetryForLocalAddress"), 0);
    EXPECT_GE(h.mDelegate->Count("RemoveConnectionAttempt"), 1);
  });
}

// Once a speculative attempt that refused a local peer is claimed by a real
// transaction, the terminal failure starts a fresh attempt via the delegate.
TEST(HappyEyeballsConnectionAttempt, ClaimedLocalAttemptRetries)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    // A speculative attempt (null transaction, allow1918=false) to a local
    // origin.
    TestHarness h(/*caps*/ 0, /*realTransaction*/ false, "10.0.0.2"_ns,
                  /*speculative*/ true, /*allow1918*/ false);
    // Simulate the local refusal the speculative attempt made even though the
    // claim below flips allow1918 to true.
    h.mFactory->mForceRefuseLocal = true;

    // A real transaction claims the speculative attempt.
    RefPtr<nsHttpTransaction> realTrans = new nsHttpTransaction();
    EXPECT_TRUE(h.mHE->Claim(realTrans));

    h.Init();

    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);
    EXPECT_TRUE(h.mFactory->mTCP[0]->RefusedForLocalAddress());
    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("StartRetryForLocalAddress"), 1)
        << "a claimed attempt whose only failures were local refusals must "
           "retry via the delegate";
  });
}

// 0-RTT started but no winner, then a non-0-RTT conn wins. EnterSucceeded's
// fallback (AnyStarted && !HadWinner) must re-queue the real transaction once
// and not mark it adopted. Uses the ZeroRttHandle seam to set AnyStarted.
TEST(HappyEyeballsConnectionAttempt, ZeroRttNoWinnerFallback)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h(/*caps*/ 0, /*realTransaction*/ true);
    h.Init();
    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);

    // A racer entered 0-RTT but never became the winner.
    h.mHE->ZeroRttHandleForTesting()->SetAnyStartedForTesting();

    RefPtr<FakeHttpConnection> conn =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ false);
    h.mFactory->mTCP[0]->FireSuccess(conn);

    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("AddTransaction"), 1)
        << "fallback should re-queue the real transaction once";
    EXPECT_EQ(h.mDelegate->Count("DispatchTransaction"), 0);
    EXPECT_FALSE(h.mHE->WasTransactionAdoptedForTesting());
  });
}

// ReleaseRealTransaction guard (bug 2048392), real-world race: two HCAs race
// one transaction. The winner dispatches it onto its connection; the loser's
// attempt then fails and runs ReleaseRealTransaction. Since the transaction is
// already on the winner's connection, the loser must not close it or remove it
// from the pending queue.
TEST(HappyEyeballsConnectionAttempt,
     ReleaseRealTransactionSkipsCloseWhenConnected)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);
    RefPtr<nsHttpTransaction> realTrans = new nsHttpTransaction();
    nsTHashSet<ConnectionEntry*> pendingQSet;

    // --- HCA #1: wins and dispatches the shared transaction. ---
    RefPtr<ConnectionEntry> entry1 = new ConnectionEntry(ci, pendingQSet);
    RefPtr<FakeConnectionEstablisherFactory> factory1 =
        new FakeConnectionEstablisherFactory();
    RefPtr<RecordingConnMgrDelegate> delegate1 = new RecordingConnMgrDelegate();
    // Winner's dispatch binds the transaction to its connection.
    delegate1->mSimulateDispatchBindsConnection = true;
    delegate1->mFindResult = new PendingTransactionInfo(realTrans);
    RefPtr<HappyEyeballsConnectionAttempt> hca1 =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    hca1->SetConnectionEstablisherFactoryForTesting(factory1);
    hca1->SetConnMgrDelegateForTesting(delegate1);
    hca1->Init(entry1);
    ASSERT_GE(factory1->mTCP.Length(), 1u);

    RefPtr<FakeHttpConnection> conn1 =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ false);
    factory1->mTCP[0]->FireSuccess(conn1);
    ASSERT_TRUE(hca1->WasTransactionAdoptedForTesting());
    ASSERT_NE(realTrans->Connection(), nullptr)
        << "the winning attempt should have bound the transaction to its conn";

    // --- HCA #2: races the same transaction and fails. ---
    RefPtr<ConnectionEntry> entry2 = new ConnectionEntry(ci, pendingQSet);
    RefPtr<FakeConnectionEstablisherFactory> factory2 =
        new FakeConnectionEstablisherFactory();
    RefPtr<RecordingConnMgrDelegate> delegate2 = new RecordingConnMgrDelegate();
    RefPtr<HappyEyeballsConnectionAttempt> hca2 =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    hca2->SetConnectionEstablisherFactoryForTesting(factory2);
    hca2->SetConnMgrDelegateForTesting(delegate2);
    hca2->Init(entry2);
    ASSERT_GE(factory2->mTCP.Length(), 1u);

    factory2->mTCP[0]->FireError(NS_ERROR_CONNECTION_REFUSED);

    EXPECT_TRUE(hca2->IsTerminal());
    EXPECT_FALSE(realTrans->Closed())
        << "the loser must not close a transaction already on the winner conn";
    EXPECT_EQ(delegate2->Count("RemoveTransFromPendingQ"), 0)
        << "guard returns before touching the pending queue";
    EXPECT_EQ(hca2->RealHttpTransaction(), nullptr)
        << "the loser drops its reference to the real transaction";

    // Teardown: detach the connection and reset the handle the winner's
    // dispatch created so ~ConnectionHandle doesn't reclaim the fake conn.
    realTrans->SetConnection(nullptr);
    delegate1->ResetHandles();
  });
}

// ProcessTCPConn guard (bug 2048294), real-world race: a WebSocket/WebTransport
// upgrade is raced by two HCAs. The first wins and dispatches it (Connection()
// set). The second wins an H2 connection, whose ProcessTCPConn would normally
// re-queue the upgrade for the extended CONNECT tunnel; but since it's already
// on a connection it must NOT re-queue (re-dispatch would hit a transport-less
// connection and crash in Activate).
TEST(HappyEyeballsConnectionAttempt, ProcessTCPConnSkipsRequeueWhenConnected)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);
    RefPtr<nsHttpTransaction> realTrans = new nsHttpTransaction();
    // Makes deferExtendedConnect fire on an H2 connection.
    realTrans->SetIsForWebTransport(true);
    nsTHashSet<ConnectionEntry*> pendingQSet;

    // --- HCA #1: wins on H1 and dispatches the shared transaction. ---
    RefPtr<ConnectionEntry> entry1 = new ConnectionEntry(ci, pendingQSet);
    RefPtr<FakeConnectionEstablisherFactory> factory1 =
        new FakeConnectionEstablisherFactory();
    RefPtr<RecordingConnMgrDelegate> delegate1 = new RecordingConnMgrDelegate();
    delegate1->mSimulateDispatchBindsConnection = true;
    delegate1->mFindResult = new PendingTransactionInfo(realTrans);
    RefPtr<HappyEyeballsConnectionAttempt> hca1 =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    hca1->SetConnectionEstablisherFactoryForTesting(factory1);
    hca1->SetConnMgrDelegateForTesting(delegate1);
    hca1->Init(entry1);
    ASSERT_GE(factory1->mTCP.Length(), 1u);
    RefPtr<FakeHttpConnection> conn1 =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ false);
    factory1->mTCP[0]->FireSuccess(conn1);
    ASSERT_NE(realTrans->Connection(), nullptr);

    // --- HCA #2: wins on H2; deferExtendedConnect must skip the re-queue. ---
    RefPtr<ConnectionEntry> entry2 = new ConnectionEntry(ci, pendingQSet);
    RefPtr<FakeConnectionEstablisherFactory> factory2 =
        new FakeConnectionEstablisherFactory();
    RefPtr<RecordingConnMgrDelegate> delegate2 = new RecordingConnMgrDelegate();
    RefPtr<HappyEyeballsConnectionAttempt> hca2 =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    hca2->SetConnectionEstablisherFactoryForTesting(factory2);
    hca2->SetConnMgrDelegateForTesting(delegate2);
    hca2->Init(entry2);
    ASSERT_GE(factory2->mTCP.Length(), 1u);
    RefPtr<FakeHttpConnection> conn2 =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ true);
    factory2->mTCP[0]->FireSuccess(conn2);

    EXPECT_TRUE(hca2->IsTerminal());
    EXPECT_EQ(delegate2->Count("AddTransaction"), 0)
        << "must not re-queue a transaction already on a connection";
    EXPECT_EQ(delegate2->Count("FindTransaction"), 0)
        << "guard returns before touching the pending queue";
    EXPECT_FALSE(realTrans->Closed());
    EXPECT_EQ(hca2->RealHttpTransaction(), nullptr)
        << "deferExtendedConnect clears the loser's transaction reference";

    realTrans->SetConnection(nullptr);
    delegate1->ResetHandles();
  });
}

// Bug 2051415: on H2->H1 fallback the connection manager abandons the stale
// in-flight attempt. Verifies the property the fix relies on: Abandon() closes
// the establisher, so a late success can't fire and re-queue the transaction.
TEST(HappyEyeballsConnectionAttempt, AbandonClosesInFlightEstablisher)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);
    RefPtr<nsHttpTransaction> realTrans = new nsHttpTransaction();
    // Stand-in for the WebSocket upgrade.
    realTrans->SetIsForWebTransport(true);
    nsTHashSet<ConnectionEntry*> pendingQSet;

    RefPtr<ConnectionEntry> entry = new ConnectionEntry(ci, pendingQSet);
    RefPtr<FakeConnectionEstablisherFactory> factory =
        new FakeConnectionEstablisherFactory();
    RefPtr<RecordingConnMgrDelegate> delegate = new RecordingConnMgrDelegate();
    RefPtr<HappyEyeballsConnectionAttempt> hca =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    hca->SetConnectionEstablisherFactoryForTesting(factory);
    hca->SetConnMgrDelegateForTesting(delegate);
    hca->Init(entry);
    ASSERT_GE(factory->mTCP.Length(), 1u);
    EXPECT_FALSE(hca->IsTerminal());
    EXPECT_EQ(factory->mTCP[0]->mCloseCount, 0u)
        << "the establisher is still in flight before abandonment";

    // The connection manager abandons the stale attempt on H2->H1 fallback.
    hca->Abandon();

    EXPECT_TRUE(hca->IsTerminal())
        << "an abandoned attempt is terminal and cannot reach ProcessTCPConn";
    EXPECT_EQ(factory->mTCP[0]->mCloseCount, 1u)
        << "abandoning closes the in-flight establisher, so a late success "
           "can't fire and re-queue the restarted transaction (bug 2051415)";
    EXPECT_FALSE(realTrans->Closed())
        << "abandoning the attempt must not close the restarted transaction";
  });
}

// Bug 2051415, 0-RTT corner. The manager detaches the transaction
// (ForgetRealTransaction) before abandoning, so EnterDone's "0-RTT started, no
// winner" path can't re-queue it (cf. ZeroRttNoWinnerFallback, which does).
TEST(HappyEyeballsConnectionAttempt,
     ForgetRealTransactionPreventsAbandonRequeue)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    TestHarness h(/*caps*/ 0, /*realTransaction*/ true);
    h.Init();
    ASSERT_GE(h.mFactory->mTCP.Length(), 1u);

    // A racer entered 0-RTT but never won -- the case EnterDone re-queues.
    h.mHE->ZeroRttHandleForTesting()->SetAnyStartedForTesting();

    // Manager takes over the transaction, detaching it before abandoning.
    h.mHE->ForgetRealTransaction();
    h.mHE->Abandon();

    EXPECT_TRUE(h.mHE->IsTerminal());
    EXPECT_EQ(h.mDelegate->Count("AddTransaction"), 0)
        << "a detached, abandoned attempt must not re-queue the transaction "
           "the connection manager is restarting (bug 2051415)";
    EXPECT_EQ(h.mFactory->mTCP[0]->mCloseCount, 1u)
        << "abandoning still closes the in-flight establisher";
  });
}

// 0-RTT winner-offset handling: several racers each advance the real
// transaction's request stream to a different Request0RttStreamOffset (the
// bytes that racer sent as early data). When one wins via Finish0RTT(accept),
// ZeroRttHandle must seek the real request stream to the *winner's* offset (so
// the real transaction resumes exactly where the winner left off) — not the
// loser's.
TEST(HappyEyeballsConnectionAttempt, ZeroRttWinnerUsesItsOwnStreamOffset)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);

    // The request head is held by weak reference inside the transaction, so it
    // must outlive realTrans (declared first).
    nsHttpRequestHead reqHead;
    reqHead.SetMethod("GET"_ns);
    reqHead.SetVersion(HttpVersion::v1_1);
    reqHead.SetRequestURI("/"_ns);

    RefPtr<nsHttpTransaction> realTrans = new nsHttpTransaction();
    nsresult rv = realTrans->Init(
        NS_HTTP_USE_HAPPY_EYEBALLS, ci, &reqHead, /*reqBody*/ nullptr,
        /*reqContentLength*/ 0,
        /*target*/ gSocketTransportService, /*callbacks*/ nullptr,
        /*eventsink*/ nullptr, /*browserId*/ 0, HttpTrafficCategory::eInvalid,
        /*requestContext*/ nullptr, ClassOfService(), /*initialRwin*/ 0,
        /*responseTimeoutEnabled*/ false, /*channelId*/ 0,
        nsILoadInfo::IPAddressSpace::Unknown, LNAPerms{});
    ASSERT_TRUE(NS_SUCCEEDED(rv));

    // The serialized GET request is the (seekable) request stream; pick two
    // distinct in-range offsets for the winning and losing racers.
    nsCOMPtr<nsISeekableStream> seekable =
        do_QueryInterface(realTrans->RequestStream());
    ASSERT_TRUE(seekable);
    ASSERT_TRUE(
        NS_SUCCEEDED(seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0)));
    uint64_t streamLen = 0;
    ASSERT_TRUE(
        NS_SUCCEEDED(realTrans->RequestStream()->Available(&streamLen)));
    ASSERT_GE(streamLen, 4u);
    uint64_t winnerOffset = streamLen / 4;
    uint64_t loserOffset = streamLen / 2;
    ASSERT_NE(winnerOffset, loserOffset);

    RefPtr<HappyEyeballsConnectionAttempt> hca =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    ZeroRttHandle* zrh = hca->ZeroRttHandleForTesting();
    ASSERT_TRUE(zrh);

    auto noStatus = [](nsITransport*, nsresult, int64_t) {};
    auto noAuth = []() {};
    RefPtr<HappyEyeballsTransaction> hetLoser =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);
    RefPtr<HappyEyeballsTransaction> hetWinner =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);

    // Each racer sent a different amount of early data.
    hetLoser->Request0RttStreamOffset() = Some(loserOffset);
    hetWinner->Request0RttStreamOffset() = Some(winnerOffset);

    // Finish0RTT -> AdoptWinner -> Adopt requires the winner to be on a
    // connection; an H2 fake makes Adopt reuse our ConnectionHandle (the
    // SwapTransaction is skipped because the fake isn't a real Http2Session).
    RefPtr<FakeHttpConnection> winnerConn =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ true);
    RefPtr<ConnectionHandle> winnerHandle = new ConnectionHandle(winnerConn);
    hetWinner->SetConnection(winnerHandle);
    hetWinner->SetConnectedCallback([](nsresult) {});

    // Winner accepts 0-RTT (restart=false).
    hetWinner->Finish0RTT(/*aRestart*/ false, /*aAlpnChanged*/ false);

    int64_t pos = -1;
    ASSERT_TRUE(NS_SUCCEEDED(seekable->Tell(&pos)));
    EXPECT_EQ(static_cast<uint64_t>(pos), winnerOffset)
        << "request stream must be seeked to the winner's 0-RTT offset, not "
           "the "
           "loser's";

    // Teardown: detach + reset the handle so ~ConnectionHandle doesn't reclaim
    // the fake connection.
    realTrans->SetConnection(nullptr);
    winnerHandle->Reset();
  });
}

// Helper: a real, 0-RTT-capable transaction for the ZeroRttHandle tests.
static already_AddRefed<nsHttpTransaction> MakeRealTransaction(
    nsHttpConnectionInfo* aCi, nsHttpRequestHead* aReqHead) {
  aReqHead->SetMethod("GET"_ns);
  aReqHead->SetVersion(HttpVersion::v1_1);
  aReqHead->SetRequestURI("/"_ns);
  RefPtr<nsHttpTransaction> trans = new nsHttpTransaction();
  nsresult rv = trans->Init(
      NS_HTTP_USE_HAPPY_EYEBALLS, aCi, aReqHead, /*reqBody*/ nullptr,
      /*reqContentLength*/ 0, /*target*/ gSocketTransportService,
      /*callbacks*/ nullptr,
      /*eventsink*/ nullptr, /*browserId*/ 0, HttpTrafficCategory::eInvalid,
      /*requestContext*/ nullptr, ClassOfService(), /*initialRwin*/ 0,
      /*responseTimeoutEnabled*/ false, /*channelId*/ 0,
      nsILoadInfo::IPAddressSpace::Unknown, LNAPerms{});
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  return trans.forget();
}

// Regression for bug 2051776 (winner-then-loser). Once a winner is declared,
// ZeroRttHandle::Cleanup() nulls mWinner and leaves mState == CleanedUp, so a
// late Finish0RTT from a losing racer must be a no-op -- not trip the
// MOZ_ASSERT(mState == State::Open) in ZeroRttHandle::Finish0RTT.
TEST(HappyEyeballsConnectionAttempt, ZeroRttLateLoserFinish0RTTAfterWinner)
{
  EnsureHttpHandler();
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);
    // The request head is held by weak reference; it must outlive realTrans.
    nsHttpRequestHead reqHead;
    RefPtr<nsHttpTransaction> realTrans = MakeRealTransaction(ci, &reqHead);

    RefPtr<HappyEyeballsConnectionAttempt> hca =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    ZeroRttHandle* zrh = hca->ZeroRttHandleForTesting();
    ASSERT_TRUE(zrh);

    auto noStatus = [](nsITransport*, nsresult, int64_t) {};
    auto noAuth = []() {};
    RefPtr<HappyEyeballsTransaction> hetWinner =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);
    RefPtr<HappyEyeballsTransaction> hetLoser =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);
    hetWinner->Request0RttStreamOffset() = Some<uint64_t>(0);
    hetLoser->Request0RttStreamOffset() = Some<uint64_t>(0);

    RefPtr<FakeHttpConnection> winnerConn =
        new FakeHttpConnection(/*usingHttp3*/ false, /*usingSpdy*/ true);
    RefPtr<ConnectionHandle> winnerHandle = new ConnectionHandle(winnerConn);
    hetWinner->SetConnection(winnerHandle);
    hetWinner->SetConnectedCallback([](nsresult) {});

    // Declare the winner -> Cleanup() nulls mWinner, mState = CleanedUp.
    hetWinner->Finish0RTT(/*aRestart*/ false, /*aAlpnChanged*/ false);

    // Late loser Finish0RTT after cleanup: must be a no-op (asserts before
    // fix).
    hetLoser->Finish0RTT(/*aRestart*/ false, /*aAlpnChanged*/ false);

    EXPECT_FALSE(realTrans->Closed());

    realTrans->SetConnection(nullptr);
    winnerHandle->Reset();
  });
}

// Regression for bug 2051776 (real-txn-gone path). The first Finish0RTT takes
// the !realTxn branch and calls Cleanup() without ever declaring a winner
// (mHadWinner stays false), so a second Finish0RTT must still be a no-op. Only
// a state-based guard covers this path.
TEST(HappyEyeballsConnectionAttempt, ZeroRttLateLoserFinish0RTTAfterRealTxnGone)
{
  EnsureHttpHandler();
  Preferences::SetBool("network.http.0rtt_force_txn_gone_for_testing", true);
  RunOnSocketThread([]() {
    RefPtr<nsHttpConnectionInfo> ci =
        new nsHttpConnectionInfo("127.0.0.1"_ns, 443, ""_ns, ""_ns, nullptr,
                                 OriginAttributes(), /*endToEndSSL*/ true);
    nsHttpRequestHead reqHead;
    RefPtr<nsHttpTransaction> realTrans = MakeRealTransaction(ci, &reqHead);

    RefPtr<HappyEyeballsConnectionAttempt> hca =
        new HappyEyeballsConnectionAttempt(ci, realTrans, /*caps*/ 0,
                                           /*speculative*/ false,
                                           /*urgentStart*/ false);
    ZeroRttHandle* zrh = hca->ZeroRttHandleForTesting();
    ASSERT_TRUE(zrh);

    auto noStatus = [](nsITransport*, nsresult, int64_t) {};
    auto noAuth = []() {};
    RefPtr<HappyEyeballsTransaction> het1 =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);
    RefPtr<HappyEyeballsTransaction> het2 =
        new HappyEyeballsTransaction(ci, nullptr, /*caps*/ 0, /*browserId*/ 0,
                                     noStatus, noAuth, noAuth, zrh);
    het1->Request0RttStreamOffset() = Some<uint64_t>(0);
    het2->Request0RttStreamOffset() = Some<uint64_t>(0);

    // First Finish0RTT: the force pref makes the real txn "gone", taking the
    // !realTxn branch -> Cleanup() without declaring a winner.
    het1->Finish0RTT(/*aRestart*/ false, /*aAlpnChanged*/ false);

    // Second Finish0RTT after that cleanup: must be a no-op (asserts before
    // fix).
    het2->Finish0RTT(/*aRestart*/ false, /*aAlpnChanged*/ false);

    realTrans->SetConnection(nullptr);
  });
  Preferences::ClearUser("network.http.0rtt_force_txn_gone_for_testing");
}

}  // namespace net
}  // namespace mozilla
