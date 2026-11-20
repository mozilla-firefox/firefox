/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WritableStream_h
#define mozilla_dom_WritableStream_h

#include "js/TypeDecls.h"
#include "js/Value.h"
#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/QueuingStrategyBinding.h"
#include "mozilla/dom/WritableStreamDefaultController.h"
#include "mozilla/dom/WritableStreamDefaultWriter.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class Promise;
class WritableStreamDefaultController;
class WritableStreamDefaultWriter;
class UnderlyingSinkAlgorithmsBase;
class UniqueMessagePortId;
class MessagePort;

class WritableStream : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(WritableStream)

  friend class ReadableStream;

 protected:
  virtual ~WritableStream();

  virtual void LastRelease() {}

  // If one extends WritableStream with another cycle collectable class,
  // calling HoldJSObjects and DropJSObjects should happen using 'this' of
  // that extending class. And in that case Explicit should be passed to the
  // constructor of WriteableStream so that it doesn't make those calls.
  // See also https://bugzilla.mozilla.org/show_bug.cgi?id=1801214.
  enum class HoldDropJSObjectsCaller { Implicit, Explicit };

  explicit WritableStream(const GlobalObject& aGlobal,
                          HoldDropJSObjectsCaller aHoldDropCaller);
  explicit WritableStream(nsIGlobalObject* aGlobal,
                          HoldDropJSObjectsCaller aHoldDropCaller);

 public:
  // Slot Getter/Setters:
  bool Backpressure() const { return mBackpressure; }
  void SetBackpressure(bool aBackpressure) { mBackpressure = aBackpressure; }

  Promise* GetCloseRequest() { return mCloseRequest; }
  void SetCloseRequest(Promise* aRequest) { mCloseRequest = aRequest; }

  MOZ_KNOWN_LIVE WritableStreamDefaultController* Controller() {
    return mController;
  }
  void SetController(WritableStreamDefaultController& aController) {
    MOZ_ASSERT(!mController);
    mController = &aController;
  }

  Promise* GetInFlightWriteRequest() const { return mInFlightWriteRequest; }

  Promise* GetPendingAbortRequestPromise() const {
    return mPendingAbortRequestPromise;
  }

  void SetPendingAbortRequest(Promise* aPromise, JS::Handle<JS::Value> aReason,
                              bool aWasAlreadyErroring) {
    mPendingAbortRequestPromise = aPromise;
    mPendingAbortRequestReason = aReason;
    mPendingAbortRequestWasAlreadyErroring = aWasAlreadyErroring;
  }

  WritableStreamDefaultWriter* GetWriter() const { return mWriter; }
  void SetWriter(WritableStreamDefaultWriter* aWriter) { mWriter = aWriter; }

  enum class WriterState { Writable, Closed, Erroring, Errored };

  WriterState State() const { return mState; }
  void SetState(const WriterState& aState) { mState = aState; }

  void GetStoredError(JSContext* aCx, JS::MutableHandle<JS::Value> aStoredError,
                      ErrorResult& aRv) const;
  JS::Value UnsafeStoredError() const { return mStoredError; }
  void SetStoredError(JS::Handle<JS::Value> aStoredError) {
    mStoredError = aStoredError;
  }

  void AppendWriteRequest(RefPtr<Promise>& aRequest) {
    mWriteRequests.Push(RefPtr<Promise>(aRequest));
  }

  // CreateWritableStream
  MOZ_CAN_RUN_SCRIPT static already_AddRefed<WritableStream> CreateAbstract(
      JSContext* aCx, nsIGlobalObject* aGlobal,
      UnderlyingSinkAlgorithmsBase* aAlgorithms, double aHighWaterMark,
      QueuingStrategySize* aSizeAlgorithm, ErrorResult& aRv);

  // WritableStreamCloseQueuedOrInFlight
  bool CloseQueuedOrInFlight() const {
    return mCloseRequest || mInFlightCloseRequest;
  }

  // WritableStreamDealWithRejection
  MOZ_CAN_RUN_SCRIPT void DealWithRejection(JSContext* aCx,
                                            JS::Handle<JS::Value> aError,
                                            ErrorResult& aRv);

  // WritableStreamFinishErroring
  MOZ_CAN_RUN_SCRIPT void FinishErroring(JSContext* aCx, ErrorResult& aRv);

  // WritableStreamFinishInFlightClose
  void FinishInFlightClose();

  // WritableStreamFinishInFlightCloseWithError
  MOZ_CAN_RUN_SCRIPT void FinishInFlightCloseWithError(
      JSContext* aCx, JS::Handle<JS::Value> aError, ErrorResult& aRv);

  // WritableStreamFinishInFlightWrite
  void FinishInFlightWrite();

  // WritableStreamFinishInFlightWriteWithError
  MOZ_CAN_RUN_SCRIPT void FinishInFlightWriteWithError(
      JSContext* aCX, JS::Handle<JS::Value> aError, ErrorResult& aR);

  // WritableStreamHasOperationMarkedInFlight
  bool HasOperationMarkedInFlight() const {
    return mInFlightWriteRequest || mInFlightCloseRequest;
  }

  // WritableStreamMarkCloseRequestInFlight
  void MarkCloseRequestInFlight();

  // WritableStreamMarkFirstWriteRequestInFlight
  void MarkFirstWriteRequestInFlight();

  // WritableStreamRejectCloseAndClosedPromiseIfNeeded
  void RejectCloseAndClosedPromiseIfNeeded();

  // WritableStreamStartErroring
  MOZ_CAN_RUN_SCRIPT void StartErroring(JSContext* aCx,
                                        JS::Handle<JS::Value> aReason,
                                        ErrorResult& aRv);

  // WritableStreamUpdateBackpressure
  void UpdateBackpressure(bool aBackpressure);

  // [Transferable]
  // https://html.spec.whatwg.org/multipage/structured-data.html#transfer-steps
  MOZ_CAN_RUN_SCRIPT bool Transfer(JSContext* aCx,
                                   UniqueMessagePortId& aPortId);
  // https://html.spec.whatwg.org/multipage/structured-data.html#transfer-receiving-steps
  MOZ_CAN_RUN_SCRIPT static already_AddRefed<WritableStream>
  ReceiveTransferImpl(JSContext* aCx, nsIGlobalObject* aGlobal,
                      MessagePort& aPort);
  MOZ_CAN_RUN_SCRIPT static bool ReceiveTransfer(
      JSContext* aCx, nsIGlobalObject* aGlobal, MessagePort& aPort,
      JS::MutableHandle<JSObject*> aReturnObject);

  // Public functions to implement other specs
  // https://streams.spec.whatwg.org/#other-specs-ws

  // https://streams.spec.whatwg.org/#writablestream-set-up
 protected:
  // Sets up the WritableStream. Intended for subclasses.
  void SetUpNative(JSContext* aCx, UnderlyingSinkAlgorithmsWrapper& aAlgorithms,
                   Maybe<double> aHighWaterMark,
                   QueuingStrategySize* aSizeAlgorithm, ErrorResult& aRv);

 public:
  // Creates and sets up a WritableStream. Use SetUpNative for this purpose in
  // subclasses.
  static already_AddRefed<WritableStream> CreateNative(
      JSContext* aCx, nsIGlobalObject& aGlobal,
      UnderlyingSinkAlgorithmsWrapper& aAlgorithms,
      Maybe<double> aHighWaterMark, QueuingStrategySize* aSizeAlgorithm,
      ErrorResult& aRv);

  // The following definitions must only be used on WritableStream instances
  // initialized via the above set up algorithm:

  // https://streams.spec.whatwg.org/#writablestream-error
  MOZ_CAN_RUN_SCRIPT void ErrorNative(JSContext* aCx,
                                      JS::Handle<JS::Value> aError,
                                      ErrorResult& aRv);

  // https://streams.spec.whatwg.org/#writablestream-abort
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> AbortNative(
      JSContext* aCx, JS::Handle<JS::Value> aReason, ErrorResult& aRv);

  // IDL layer functions

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // IDL methods

  // TODO: Use MOZ_CAN_RUN_SCRIPT when IDL constructors can use it (bug 1749042)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static already_AddRefed<WritableStream>
  Constructor(const GlobalObject& aGlobal,
              const Optional<JS::Handle<JSObject*>>& aUnderlyingSink,
              const QueuingStrategy& aStrategy, ErrorResult& aRv);

  bool Locked() const { return !!mWriter; }

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> Abort(
      JSContext* cx, JS::Handle<JS::Value> aReason, ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> Close(JSContext* aCx,
                                                     ErrorResult& aRv);

  already_AddRefed<WritableStreamDefaultWriter> GetWriter(ErrorResult& aRv);

 protected:
  nsCOMPtr<nsIGlobalObject> mGlobal;

  // Internal Slots:
 private:
  bool mBackpressure = false;
  RefPtr<Promise> mCloseRequest;
  RefPtr<WritableStreamDefaultController> mController;
  RefPtr<Promise> mInFlightWriteRequest;
  RefPtr<Promise> mInFlightCloseRequest;

  // We inline all members of [[pendingAbortRequest]] in this class.
  // The absence (i.e. undefined) of the [[pendingAbortRequest]]
  // is indicated by mPendingAbortRequestPromise = nullptr.
  RefPtr<Promise> mPendingAbortRequestPromise;
  JS::Heap<JS::Value> mPendingAbortRequestReason;
  bool mPendingAbortRequestWasAlreadyErroring = false;

  WriterState mState = WriterState::Writable;
  JS::Heap<JS::Value> mStoredError;
  RefPtr<WritableStreamDefaultWriter> mWriter;
  mozilla::Queue<RefPtr<Promise>> mWriteRequests;

  HoldDropJSObjectsCaller mHoldDropCaller;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_WritableStream_h
