/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ReadableStreamPipeTo.h"

#include "ReadableStreamAbstract.h"
#include "ReadableStreamDefaultReaderAbstract.h"
#include "WritableStreamAbstract.h"
#include "WritableStreamDefaultWriterAbstract.h"
#include "js/Exception.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/AbortFollower.h"
#include "mozilla/dom/AbortSignal.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupportsImpl.h"

namespace mozilla::dom {

using namespace streams_abstract;

struct PipeToReadRequest;
class WriteFinishedPromiseHandler;
class ShutdownActionFinishedPromiseHandler;

// https://streams.spec.whatwg.org/#readable-stream-pipe-to (Steps 14-15.)
//
// This class implements everything that is required to read all chunks from
// the reader (source) and write them to writer (destination), while
// following the constraints given in the spec using our implementation-defined
// behavior.
//
// The cycle-collected references look roughly like this:
// clang-format off
//
// Closed promise <-- ReadableStreamDefaultReader <--> ReadableStream
//         |                  ^              |
//         |(PromiseHandler)  |(mReader)     |(ReadRequest)
//         |                  |              |
//         |-------------> PipeToPump <-------
//                         ^  |   |
//         |---------------|  |   |
//         |                  |   |-------(mLastWrite) -------->
//         |(PromiseHandler)  |   |< ---- (PromiseHandler) ---- Promise
//         |                  |                                   ^
//         |                  |(mWriter)                          |(mWriteRequests)
//         |                  v                                   |
// Closed promise <-- WritableStreamDefaultWriter <--------> WritableStream
//
// clang-format on
class PipeToPump final : public AbortFollower {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(PipeToPump)

  friend struct PipeToReadRequest;
  friend class WriteFinishedPromiseHandler;
  friend class ShutdownActionFinishedPromiseHandler;

  PipeToPump(Promise* aPromise, ReadableStreamDefaultReader* aReader,
             WritableStreamDefaultWriter* aWriter, bool aPreventClose,
             bool aPreventAbort, bool aPreventCancel)
      : mPromise(aPromise),
        mReader(aReader),
        mWriter(aWriter),
        mPreventClose(aPreventClose),
        mPreventAbort(aPreventAbort),
        mPreventCancel(aPreventCancel) {}

  MOZ_CAN_RUN_SCRIPT void Start(JSContext* aCx, AbortSignal* aSignal);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void RunAbortAlgorithm() override;

 private:
  ~PipeToPump() override = default;

  MOZ_CAN_RUN_SCRIPT void PerformAbortAlgorithm(JSContext* aCx,
                                                AbortSignalImpl* aSignal);

  MOZ_CAN_RUN_SCRIPT bool SourceOrDestErroredOrClosed(JSContext* aCx);

  using ShutdownAction = already_AddRefed<Promise> (*)(
      JSContext*, PipeToPump*, JS::Handle<mozilla::Maybe<JS::Value>>,
      ErrorResult&);

  MOZ_CAN_RUN_SCRIPT void ShutdownWithAction(
      JSContext* aCx, ShutdownAction aAction,
      JS::Handle<mozilla::Maybe<JS::Value>> aError);
  MOZ_CAN_RUN_SCRIPT void ShutdownWithActionAfterFinishedWrite(
      JSContext* aCx, ShutdownAction aAction,
      JS::Handle<mozilla::Maybe<JS::Value>> aError);

  MOZ_CAN_RUN_SCRIPT void Shutdown(
      JSContext* aCx, JS::Handle<mozilla::Maybe<JS::Value>> aError);

  void Finalize(JSContext* aCx, JS::Handle<mozilla::Maybe<JS::Value>> aError);

  MOZ_CAN_RUN_SCRIPT void OnReadFulfilled(JSContext* aCx,
                                          JS::Handle<JS::Value> aChunk,
                                          ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void OnWriterReady(JSContext* aCx, JS::Handle<JS::Value>);
  MOZ_CAN_RUN_SCRIPT void Read(JSContext* aCx);

  MOZ_CAN_RUN_SCRIPT void OnSourceClosed(JSContext* aCx, JS::Handle<JS::Value>);
  MOZ_CAN_RUN_SCRIPT void OnSourceErrored(
      JSContext* aCx, JS::Handle<JS::Value> aSourceStoredError);

  MOZ_CAN_RUN_SCRIPT void OnDestClosed(JSContext* aCx, JS::Handle<JS::Value>);
  MOZ_CAN_RUN_SCRIPT void OnDestErrored(JSContext* aCx,
                                        JS::Handle<JS::Value> aDestStoredError);

  RefPtr<Promise> mPromise;
  RefPtr<ReadableStreamDefaultReader> mReader;
  RefPtr<WritableStreamDefaultWriter> mWriter;
  RefPtr<Promise> mLastWritePromise;
  const bool mPreventClose;
  const bool mPreventAbort;
  const bool mPreventCancel;
  bool mShuttingDown = false;
#ifdef DEBUG
  bool mReadChunk = false;
#endif
};

// This is a helper class for PipeToPump that allows it to attach
// member functions as promise handlers.
class PipeToPumpHandler final : public PromiseNativeHandler {
  virtual ~PipeToPumpHandler() = default;

  using FunPtr = void (PipeToPump::*)(JSContext*, JS::Handle<JS::Value>);

  RefPtr<PipeToPump> mPipeToPump;
  FunPtr mResolved;
  FunPtr mRejected;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS(PipeToPumpHandler)

  explicit PipeToPumpHandler(PipeToPump* aPipeToPump, FunPtr aResolved,
                             FunPtr aRejected)
      : mPipeToPump(aPipeToPump), mResolved(aResolved), mRejected(aRejected) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult&) override {
    if (mResolved) {
      (mPipeToPump->*mResolved)(aCx, aValue);
    }
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aReason,
                        ErrorResult&) override {
    if (mRejected) {
      (mPipeToPump->*mRejected)(aCx, aReason);
    }
  }
};

NS_IMPL_CYCLE_COLLECTION(PipeToPumpHandler, mPipeToPump)
NS_IMPL_CYCLE_COLLECTING_ADDREF(PipeToPumpHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PipeToPumpHandler)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PipeToPumpHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

void PipeToPump::RunAbortAlgorithm() {
  AutoJSAPI jsapi;
  if (!jsapi.Init(mReader->GetStream()->GetParentObject())) {
    NS_WARNING(
        "Failed to initialize AutoJSAPI in PipeToPump::RunAbortAlgorithm");
    return;
  }
  JSContext* cx = jsapi.cx();

  RefPtr<AbortSignalImpl> signal = Signal();
  PerformAbortAlgorithm(cx, signal);
}

void PipeToPump::PerformAbortAlgorithm(JSContext* aCx,
                                       AbortSignalImpl* aSignal) {
  MOZ_ASSERT(aSignal->Aborted());

  // https://streams.spec.whatwg.org/#readable-stream-pipe-to
  // Step 14.1. Let abortAlgorithm be the following steps:
  // Note: All the following steps are 14.1.xx

  // Step 1. Let error be signal’s abort reason.
  JS::Rooted<JS::Value> error(aCx);
  aSignal->GetReason(aCx, &error);

  auto action = [](JSContext* aCx, PipeToPump* aPipeToPump,
                   JS::Handle<mozilla::Maybe<JS::Value>> aError,
                   ErrorResult& aRv) MOZ_CAN_RUN_SCRIPT {
    JS::Rooted<JS::Value> error(aCx, *aError);

    // Step 2. Let actions be an empty ordered set.
    nsTArray<RefPtr<Promise>> actions;

    // Step 3. If preventAbort is false, append the following action to actions:
    if (!aPipeToPump->mPreventAbort) {
      RefPtr<WritableStream> dest = aPipeToPump->mWriter->GetStream();

      // Step 3.1. If dest.[[state]] is "writable", return !
      // WritableStreamAbort(dest, error).
      if (dest->State() == WritableStream::WriterState::Writable) {
        RefPtr<Promise> p = WritableStreamAbort(aCx, dest, error, aRv);
        if (aRv.Failed()) {
          return already_AddRefed<Promise>();
        }
        actions.AppendElement(p);
      }

      // Step 3.2. Otherwise, return a promise resolved with undefined.
      // Note: This is basically a no-op.
    }

    // Step 4. If preventCancel is false, append the following action action to
    // actions:
    if (!aPipeToPump->mPreventCancel) {
      RefPtr<ReadableStream> source = aPipeToPump->mReader->GetStream();

      // Step 4.1. If source.[[state]] is "readable", return !
      // ReadableStreamCancel(source, error).
      if (source->State() == ReadableStream::ReaderState::Readable) {
        RefPtr<Promise> p = ReadableStreamCancel(aCx, source, error, aRv);
        if (aRv.Failed()) {
          return already_AddRefed<Promise>();
        }
        actions.AppendElement(p);
      }

      // Step 4.2. Otherwise, return a promise resolved with undefined.
      // No-op again.
    }

    // Step 5. .. action consisting of getting a promise to wait for
    // all of the actions in actions ...
    return Promise::All(aCx, actions, aRv);
  };

  // Step 5. Shutdown with an action consisting of getting a promise to wait for
  // all of the actions in actions, and with error.
  JS::Rooted<Maybe<JS::Value>> someError(aCx, Some(error.get()));
  ShutdownWithAction(aCx, action, someError);
}

bool PipeToPump::SourceOrDestErroredOrClosed(JSContext* aCx) {
  // (Constraint) Error and close states must be propagated:
  // the following conditions must be applied in order.
  RefPtr<ReadableStream> source = mReader->GetStream();
  RefPtr<WritableStream> dest = mWriter->GetStream();

  // Step 1. Errors must be propagated forward: if source.[[state]] is or
  // becomes "errored", then
  if (source->State() == ReadableStream::ReaderState::Errored) {
    JS::Rooted<JS::Value> storedError(aCx);
    source->GetStoredError(aCx, &storedError, IgnoredErrorResult());
    OnSourceErrored(aCx, storedError);
    return true;
  }

  // Step 2. Errors must be propagated backward: if dest.[[state]] is or becomes
  // "errored", then
  if (dest->State() == WritableStream::WriterState::Errored) {
    JS::Rooted<JS::Value> storedError(aCx);
    dest->GetStoredError(aCx, &storedError, IgnoredErrorResult());
    OnDestErrored(aCx, storedError);
    return true;
  }

  // Step 3. Closing must be propagated forward: if source.[[state]] is or
  // becomes "closed", then
  if (source->State() == ReadableStream::ReaderState::Closed) {
    OnSourceClosed(aCx, JS::UndefinedHandleValue);
    return true;
  }

  // Step 4. Closing must be propagated backward:
  // if ! WritableStreamCloseQueuedOrInFlight(dest) is true
  // or dest.[[state]] is "closed", then
  if (dest->CloseQueuedOrInFlight() ||
      dest->State() == WritableStream::WriterState::Closed) {
    OnDestClosed(aCx, JS::UndefinedHandleValue);
    return true;
  }

  return false;
}

// https://streams.spec.whatwg.org/#readable-stream-pipe-to
// Steps 14-15.
void PipeToPump::Start(JSContext* aCx, AbortSignal* aSignal) {
  // Step 14. If signal is not undefined,
  if (aSignal) {
    // Step 14.1. Let abortAlgorithm be the following steps:
    // ... This is implemented by RunAbortAlgorithm.

    // Step 14.2. If signal is aborted, perform abortAlgorithm and
    // return promise.
    if (aSignal->Aborted()) {
      PerformAbortAlgorithm(aCx, aSignal);
      return;
    }

    // Step 14.3. Add abortAlgorithm to signal.
    Follow(aSignal);
  }

  // Step 15. In parallel but not really; see #905, using reader and writer,
  // read all chunks from source and write them to dest.
  // Due to the locking provided by the reader and writer,
  // the exact manner in which this happens is not observable to author code,
  // and so there is flexibility in how this is done.

  // (Constraint) Error and close states must be propagated

  // Before piping has started, we have to check for source/destination being
  // errored/closed manually.
  if (SourceOrDestErroredOrClosed(aCx)) {
    return;
  }

  // We use the following two promises to propagate error and close states
  // during piping.
  RefPtr<Promise> readerClosed = mReader->ClosedPromise();
  readerClosed->AppendNativeHandler(new PipeToPumpHandler(
      this, &PipeToPump::OnSourceClosed, &PipeToPump::OnSourceErrored));

  // Note: Because we control the destination/writer it should never be closed
  // after we did the initial check above with SourceOrDestErroredOrClosed.
  RefPtr<Promise> writerClosed = mWriter->ClosedPromise();
  writerClosed->AppendNativeHandler(new PipeToPumpHandler(
      this, &PipeToPump::OnDestClosed, &PipeToPump::OnDestErrored));

  Read(aCx);
}

class WriteFinishedPromiseHandler final : public PromiseNativeHandler {
  RefPtr<PipeToPump> mPipeToPump;
  PipeToPump::ShutdownAction mAction;
  bool mHasError;
  JS::Heap<JS::Value> mError;

  virtual ~WriteFinishedPromiseHandler() { mozilla::DropJSObjects(this); };

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(WriteFinishedPromiseHandler)

  explicit WriteFinishedPromiseHandler(
      JSContext* aCx, PipeToPump* aPipeToPump,
      PipeToPump::ShutdownAction aAction,
      JS::Handle<mozilla::Maybe<JS::Value>> aError)
      : mPipeToPump(aPipeToPump), mAction(aAction) {
    mHasError = aError.isSome();
    if (mHasError) {
      mError = *aError;
    }
    mozilla::HoldJSObjects(this);
  }

  MOZ_CAN_RUN_SCRIPT void WriteFinished(JSContext* aCx) {
    RefPtr<PipeToPump> pipeToPump = mPipeToPump;  // XXX known-live?
    JS::Rooted<Maybe<JS::Value>> error(aCx);
    if (mHasError) {
      error = Some(mError);
    }
    pipeToPump->ShutdownWithActionAfterFinishedWrite(aCx, mAction, error);
  }

  MOZ_CAN_RUN_SCRIPT void ResolvedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aValue,
                                           ErrorResult&) override {
    WriteFinished(aCx);
  }

  MOZ_CAN_RUN_SCRIPT void RejectedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aReason,
                                           ErrorResult&) override {
    WriteFinished(aCx);
  }
};

NS_IMPL_CYCLE_COLLECTION_WITH_JS_MEMBERS(WriteFinishedPromiseHandler,
                                         (mPipeToPump), (mError))
NS_IMPL_CYCLE_COLLECTING_ADDREF(WriteFinishedPromiseHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WriteFinishedPromiseHandler)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WriteFinishedPromiseHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// https://streams.spec.whatwg.org/#rs-pipeTo-shutdown-with-action
// Shutdown with an action: if any of the above requirements ask to shutdown
// with an action action, optionally with an error originalError, then:
void PipeToPump::ShutdownWithAction(
    JSContext* aCx, ShutdownAction aAction,
    JS::Handle<mozilla::Maybe<JS::Value>> aError) {
  // Step 1. If shuttingDown is true, abort these substeps.
  if (mShuttingDown) {
    return;
  }

  // Step 2. Set shuttingDown to true.
  mShuttingDown = true;

  // Step 3. If dest.[[state]] is "writable" and !
  // WritableStreamCloseQueuedOrInFlight(dest) is false,
  RefPtr<WritableStream> dest = mWriter->GetStream();
  if (dest->State() == WritableStream::WriterState::Writable &&
      !dest->CloseQueuedOrInFlight()) {
    // Step 3.1. If any chunks have been read but not yet written, write them to
    // dest.
    // Step 3.2. Wait until every chunk that has been read has been
    // written (i.e. the corresponding promises have settled).
    //
    // Note: Write requests are processed in order, so when the promise
    // for the last written chunk is settled all previous chunks have been
    // written as well.
    if (mLastWritePromise) {
      mLastWritePromise->AppendNativeHandler(
          new WriteFinishedPromiseHandler(aCx, this, aAction, aError));
      return;
    }
  }

  // Don't have to wait for last write, immediately continue.
  ShutdownWithActionAfterFinishedWrite(aCx, aAction, aError);
}

class ShutdownActionFinishedPromiseHandler final : public PromiseNativeHandler {
  RefPtr<PipeToPump> mPipeToPump;
  bool mHasError;
  JS::Heap<JS::Value> mError;

  virtual ~ShutdownActionFinishedPromiseHandler() {
    mozilla::DropJSObjects(this);
  }

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(
      ShutdownActionFinishedPromiseHandler)

  explicit ShutdownActionFinishedPromiseHandler(
      JSContext* aCx, PipeToPump* aPipeToPump,
      JS::Handle<mozilla::Maybe<JS::Value>> aError)
      : mPipeToPump(aPipeToPump) {
    mHasError = aError.isSome();
    if (mHasError) {
      mError = *aError;
    }
    mozilla::HoldJSObjects(this);
  }

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    // https://streams.spec.whatwg.org/#rs-pipeTo-shutdown-with-action
    // Step 5. Upon fulfillment of p, finalize, passing along originalError if
    // it was given.
    JS::Rooted<Maybe<JS::Value>> maybeError(aCx);
    if (mHasError) {
      JS::Rooted<JS::Value> error(aCx, mError);
      if (!JS_WrapValue(aCx, &error)) {
        aRv.StealExceptionFromJSContext(aCx);
        return;
      }
      maybeError = Some(error.get());
    }
    mPipeToPump->Finalize(aCx, maybeError);
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aReason,
                        ErrorResult&) override {
    // https://streams.spec.whatwg.org/#rs-pipeTo-shutdown-with-action
    // Step 6. Upon rejection of p with reason newError, finalize with
    // newError.
    JS::Rooted<Maybe<JS::Value>> error(aCx, Some(aReason));
    mPipeToPump->Finalize(aCx, error);
  }
};

NS_IMPL_CYCLE_COLLECTION_WITH_JS_MEMBERS(ShutdownActionFinishedPromiseHandler,
                                         (mPipeToPump), (mError))
NS_IMPL_CYCLE_COLLECTING_ADDREF(ShutdownActionFinishedPromiseHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ShutdownActionFinishedPromiseHandler)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ShutdownActionFinishedPromiseHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// https://streams.spec.whatwg.org/#rs-pipeTo-shutdown-with-action
// Continuation after Step 3. triggered a promise resolution.
void PipeToPump::ShutdownWithActionAfterFinishedWrite(
    JSContext* aCx, ShutdownAction aAction,
    JS::Handle<mozilla::Maybe<JS::Value>> aError) {
  if (!aAction) {
    // Used to implement shutdown without action. Finalize immediately.
    Finalize(aCx, aError);
    return;
  }

  // Step 4. Let p be the result of performing action.
  RefPtr<PipeToPump> thisRefPtr = this;
  ErrorResult rv;
  RefPtr<Promise> p = aAction(aCx, thisRefPtr, aError, rv);

  // Error while calling actions above, continue immediately with finalization.
  if (rv.MaybeSetPendingException(aCx)) {
    JS::Rooted<Maybe<JS::Value>> someError(aCx);

    JS::Rooted<JS::Value> error(aCx);
    if (JS_GetPendingException(aCx, &error)) {
      someError = Some(error.get());
    }

    JS_ClearPendingException(aCx);

    Finalize(aCx, someError);
    return;
  }

  // Steps 5-6.
  p->AppendNativeHandler(
      new ShutdownActionFinishedPromiseHandler(aCx, this, aError));
}

// https://streams.spec.whatwg.org/#rs-pipeTo-shutdown
// Shutdown: if any of the above requirements or steps ask to shutdown,
// optionally with an error error, then:
void PipeToPump::Shutdown(JSContext* aCx,
                          JS::Handle<mozilla::Maybe<JS::Value>> aError) {
  // Note: We implement "shutdown" in terms of "shutdown with action".
  // We can observe that when passing along an action that always succeeds
  // shutdown with action and shutdown have the same behavior, when
  // Ignoring the potential micro task for the promise that we skip anyway.
  ShutdownWithAction(aCx, nullptr, aError);
}

// https://streams.spec.whatwg.org/#rs-pipeTo-finalize
// Finalize: both forms of shutdown will eventually ask to finalize,
// optionally with an error error, which means to perform the following steps:
void PipeToPump::Finalize(JSContext* aCx,
                          JS::Handle<mozilla::Maybe<JS::Value>> aError) {
  IgnoredErrorResult rv;
  // Step 1. Perform ! WritableStreamDefaultWriterRelease(writer).
  WritableStreamDefaultWriterRelease(aCx, mWriter);

  // Step 2. If reader implements ReadableStreamBYOBReader,
  // perform ! ReadableStreamBYOBReaderRelease(reader).
  // Note: We always use a default reader.
  MOZ_ASSERT(!mReader->IsBYOB());

  // Step 3. Otherwise, perform ! ReadableStreamDefaultReaderRelease(reader).
  ReadableStreamDefaultReaderRelease(aCx, mReader, rv);
  NS_WARNING_ASSERTION(!rv.Failed(),
                       "ReadableStreamReaderGenericRelease should not fail.");

  // Step 3. If signal is not undefined, remove abortAlgorithm from signal.
  if (IsFollowing()) {
    Unfollow();
  }

  // Step 4. If error was given, reject promise with error.
  if (aError.isSome()) {
    JS::Rooted<JS::Value> error(aCx, *aError);
    mPromise->MaybeReject(error);
  } else {
    // Step 5. Otherwise, resolve promise with undefined.
    mPromise->MaybeResolveWithUndefined();
  }

  // Remove all references.
  mPromise = nullptr;
  mReader = nullptr;
  mWriter = nullptr;
  mLastWritePromise = nullptr;
  Unfollow();
}

void PipeToPump::OnReadFulfilled(JSContext* aCx, JS::Handle<JS::Value> aChunk,
                                 ErrorResult& aRv) {
  // (Constraint) Shutdown must stop activity:
  // if shuttingDown becomes true, the user agent must not initiate further
  // reads from reader, and must only perform writes of already-read chunks ...
  //
  // We may reach this point after |On{Source,Dest}{Clos,Error}ed| has responded
  // to an out-of-band change.  Per the comment in |OnSourceErrored|, we want to
  // allow the implicated shutdown to proceed, and we don't want to interfere
  // with or additionally alter its operation.  Particularly, we don't want to
  // queue up the successfully-read chunk (if there was one, and this isn't just
  // reporting "done") to be written: it wasn't "already-read" when that
  // error/closure happened.
  //
  // All specified reactions to a closure/error invoke either the shutdown, or
  // shutdown with an action, algorithms.  Those algorithms each abort if either
  // shutdown algorithm has already been invoked.  So we check for shutdown here
  // in case of asynchronous closure/error and abort if shutdown has already
  // started (and possibly finished).
  //
  // TODO: Implement the eventual resolution from
  // https://github.com/whatwg/streams/issues/1207
  if (mShuttingDown) {
    return;
  }

  // Write asynchronously. Roughly this is like:
  // `Promise.resolve().then(() => stream.write(chunk));`
  // XXX: The spec currently does not require asynchronicity, but this still
  // matches other engines' behavior. See
  // https://github.com/whatwg/streams/issues/1243.
  RefPtr<Promise> promise =
      Promise::CreateInfallible(xpc::CurrentNativeGlobal(aCx));
  promise->MaybeResolveWithUndefined();
  auto result = promise->ThenWithCycleCollectedArgsJS(
      [](JSContext* aCx, JS::Handle<JS::Value>, ErrorResult& aRv,
         const RefPtr<PipeToPump>& aSelf,
         const RefPtr<WritableStreamDefaultWriter>& aWriter,
         JS::Handle<JS::Value> aChunk)
          MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION -> already_AddRefed<Promise> {
            // Shutdown may have cleaned up the writer.
            // (Can't just check the mShuttingDown because
            // ShutdownWithActionAfterFinishedWrite may be still waiting.)
            if (!aWriter->GetStream()) {
              return nullptr;
            }

            RefPtr<Promise> promise =
                WritableStreamDefaultWriterWrite(aCx, aWriter, aChunk, aRv);

            // Last read has finished, so it's time to start reading again.
            aSelf->Read(aCx);

            return promise.forget();
          },
      std::make_tuple(RefPtr{this}, mWriter), std::make_tuple(aChunk));
  if (result.isErr()) {
    mLastWritePromise = nullptr;
    return;
  }
  mLastWritePromise = result.unwrap();

  mLastWritePromise->AppendNativeHandler(
      new PipeToPumpHandler(this, nullptr, &PipeToPump::OnDestErrored));
}

void PipeToPump::OnWriterReady(JSContext* aCx, JS::Handle<JS::Value>) {
  // Writer is ready again (i.e. backpressure was resolved), so read.
  Read(aCx);
}

struct PipeToReadRequest : public ReadRequest {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(PipeToReadRequest, ReadRequest)

  RefPtr<PipeToPump> mPipeToPump;

  explicit PipeToReadRequest(PipeToPump* aPipeToPump)
      : mPipeToPump(aPipeToPump) {}

  MOZ_CAN_RUN_SCRIPT void ChunkSteps(JSContext* aCx,
                                     JS::Handle<JS::Value> aChunk,
                                     ErrorResult& aRv) override {
    RefPtr<PipeToPump> pipeToPump = mPipeToPump;  // XXX known live?
    pipeToPump->OnReadFulfilled(aCx, aChunk, aRv);
  }

  // The reader's closed promise handlers will already call OnSourceClosed/
  // OnSourceErrored, so these steps can just be ignored.
  void CloseSteps(JSContext* aCx, ErrorResult& aRv) override {}
  void ErrorSteps(JSContext* aCx, JS::Handle<JS::Value> aError,
                  ErrorResult& aRv) override {}

 protected:
  virtual ~PipeToReadRequest() = default;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(PipeToReadRequest, ReadRequest, mPipeToPump)

NS_IMPL_ADDREF_INHERITED(PipeToReadRequest, ReadRequest)
NS_IMPL_RELEASE_INHERITED(PipeToReadRequest, ReadRequest)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PipeToReadRequest)
NS_INTERFACE_MAP_END_INHERITING(ReadRequest)

void PipeToPump::Read(JSContext* aCx) {
#ifdef DEBUG
  mReadChunk = true;
#endif

  // (Constraint) Shutdown must stop activity:
  // If shuttingDown becomes true, the user agent must not initiate
  // further reads from reader
  if (mShuttingDown) {
    return;
  }

  // (Constraint) Backpressure must be enforced:
  // While WritableStreamDefaultWriterGetDesiredSize(writer) is ≤ 0 or is null,
  // the user agent must not read from reader.
  Nullable<double> desiredSize =
      WritableStreamDefaultWriterGetDesiredSize(mWriter);
  if (desiredSize.IsNull()) {
    // This means the writer has errored. This is going to be handled
    // by the writer closed promise.
    return;
  }

  if (desiredSize.Value() <= 0) {
    // Wait for the writer to become ready before reading more data from
    // the reader. We don't care about rejections here, because those are
    // already handled by the writer closed promise.
    RefPtr<Promise> readyPromise = mWriter->Ready();
    readyPromise->AppendNativeHandler(
        new PipeToPumpHandler(this, &PipeToPump::OnWriterReady, nullptr));
    return;
  }

  RefPtr<ReadableStreamDefaultReader> reader = mReader;
  RefPtr<ReadRequest> request = new PipeToReadRequest(this);
  ErrorResult rv;
  ReadableStreamDefaultReaderRead(aCx, reader, request, rv);
  if (rv.MaybeSetPendingException(aCx)) {
    // XXX It's actually not quite obvious what we should do here.
    // We've got an error during reading, so on the surface it seems logical
    // to invoke `OnSourceErrored`. However in certain cases the required
    // condition > source.[[state]] is or becomes "errored" < won't actually
    // happen i.e. when `WritableStreamDefaultWriterWrite` called from
    // `OnReadFulfilled` (via PipeToReadRequest::ChunkSteps) fails in
    // a synchronous fashion.
    JS::Rooted<JS::Value> error(aCx);
    JS::Rooted<Maybe<JS::Value>> someError(aCx);

    // The error was moved to the JSContext by MaybeSetPendingException.
    if (JS_GetPendingException(aCx, &error)) {
      someError = Some(error.get());
    }

    JS_ClearPendingException(aCx);

    Shutdown(aCx, someError);
  }
}

// Step 3. Closing must be propagated forward: if source.[[state]] is or
// becomes "closed", then
void PipeToPump::OnSourceClosed(JSContext* aCx, JS::Handle<JS::Value>) {
  // Step 3.1. If preventClose is false, shutdown with an action of
  // ! WritableStreamDefaultWriterCloseWithErrorPropagation(writer).
  if (!mPreventClose) {
    ShutdownWithAction(
        aCx,
        [](JSContext* aCx, PipeToPump* aPipeToPump,
           JS::Handle<mozilla::Maybe<JS::Value>> aError, ErrorResult& aRv)
            MOZ_CAN_RUN_SCRIPT {
              RefPtr<WritableStreamDefaultWriter> writer = aPipeToPump->mWriter;
              return WritableStreamDefaultWriterCloseWithErrorPropagation(
                  aCx, writer, aRv);
            },
        JS::NothingHandleValue);
  } else {
    // Step 3.2 Otherwise, shutdown.
    Shutdown(aCx, JS::NothingHandleValue);
  }
}

// Step 1. Errors must be propagated forward: if source.[[state]] is or
// becomes "errored", then
void PipeToPump::OnSourceErrored(JSContext* aCx,
                                 JS::Handle<JS::Value> aSourceStoredError) {
  // If |source| becomes errored not during a pending read, it's clear we must
  // react immediately.
  //
  // But what if |source| becomes errored *during* a pending read?  Should this
  // first error, or the pending-read second error, predominate?  Two semantics
  // are possible when |source|/|dest| become closed or errored while there's a
  // pending read:
  //
  //   1. Wait until the read fulfills or rejects, then respond to the
  //      closure/error without regard to the read having fulfilled or rejected.
  //      (This will simply not react to the read being rejected, or it will
  //      queue up the read chunk to be written during shutdown.)
  //   2. React to the closure/error immediately per "Error and close states
  //      must be propagated".  Then when the read fulfills or rejects later, do
  //      nothing.
  //
  // The spec doesn't clearly require either semantics.  It requires that
  // *already-read* chunks be written (at least if |dest| didn't become errored
  // or closed such that no further writes can occur).  But it's silent as to
  // not-fully-read chunks.  (These semantic differences may only be observable
  // with very carefully constructed readable/writable streams.)
  //
  // It seems best, generally, to react to the temporally-earliest problem that
  // arises, so we implement option #2.  (Blink, in contrast, currently
  // implements option #1.)
  //
  // All specified reactions to a closure/error invoke either the shutdown, or
  // shutdown with an action, algorithms.  Those algorithms each abort if either
  // shutdown algorithm has already been invoked.  So we don't need to do
  // anything special here to deal with a pending read.
  //
  // TODO: Implement the eventual resolution from
  // https://github.com/whatwg/streams/issues/1207

  // Step 1.1 If preventAbort is false, shutdown with an action of
  // ! WritableStreamAbort(dest, source.[[storedError]])
  // and with source.[[storedError]].
  JS::Rooted<Maybe<JS::Value>> error(aCx, Some(aSourceStoredError));
  if (!mPreventAbort) {
    ShutdownWithAction(
        aCx,
        [](JSContext* aCx, PipeToPump* aPipeToPump,
           JS::Handle<mozilla::Maybe<JS::Value>> aError, ErrorResult& aRv)
            MOZ_CAN_RUN_SCRIPT {
              JS::Rooted<JS::Value> error(aCx, *aError);
              RefPtr<WritableStream> dest = aPipeToPump->mWriter->GetStream();
              return WritableStreamAbort(aCx, dest, error, aRv);
            },
        error);
  } else {
    // Step 1.1. Otherwise, shutdown with source.[[storedError]].
    Shutdown(aCx, error);
  }
}

// Step 4. Closing must be propagated backward:
// if ! WritableStreamCloseQueuedOrInFlight(dest) is true
// or dest.[[state]] is "closed", then
void PipeToPump::OnDestClosed(JSContext* aCx, JS::Handle<JS::Value>) {
  // Step 4.1. Assert: no chunks have been read or written.
  // Note: No reading automatically implies no writing.
  // In a perfect world OnDestClosed would only be called before we start
  // piping, because afterwards the writer has an exclusive lock on the stream.
  // In reality the closed promise can still be resolved after we release
  // the lock on the writer in Finalize.
  if (mShuttingDown) {
    return;
  }
  MOZ_ASSERT(!mReadChunk);

  // Step 4.2. Let destClosed be a new TypeError.
  JS::Rooted<Maybe<JS::Value>> destClosed(aCx, Nothing());
  {
    ErrorResult rv;
    rv.ThrowTypeError("Cannot pipe to closed stream");
    JS::Rooted<JS::Value> error(aCx);
    bool ok = ToJSValue(aCx, std::move(rv), &error);
    MOZ_RELEASE_ASSERT(ok, "must be ok");
    destClosed = Some(error.get());
  }

  // Step 4.3. If preventCancel is false, shutdown with an action of
  // ! ReadableStreamCancel(source, destClosed) and with destClosed.
  if (!mPreventCancel) {
    ShutdownWithAction(
        aCx,
        [](JSContext* aCx, PipeToPump* aPipeToPump,
           JS::Handle<mozilla::Maybe<JS::Value>> aError, ErrorResult& aRv)
            MOZ_CAN_RUN_SCRIPT {
              JS::Rooted<JS::Value> error(aCx, *aError);
              RefPtr<ReadableStream> dest = aPipeToPump->mReader->GetStream();
              return ReadableStreamCancel(aCx, dest, error, aRv);
            },
        destClosed);
  } else {
    // Step 4.4. Otherwise, shutdown with destClosed.
    Shutdown(aCx, destClosed);
  }
}

// Step 2. Errors must be propagated backward: if dest.[[state]] is or becomes
// "errored", then
void PipeToPump::OnDestErrored(JSContext* aCx,
                               JS::Handle<JS::Value> aDestStoredError) {
  // Step 2.1. If preventCancel is false, shutdown with an action of
  // ! ReadableStreamCancel(source, dest.[[storedError]])
  // and with dest.[[storedError]].
  JS::Rooted<Maybe<JS::Value>> error(aCx, Some(aDestStoredError));
  if (!mPreventCancel) {
    ShutdownWithAction(
        aCx,
        [](JSContext* aCx, PipeToPump* aPipeToPump,
           JS::Handle<mozilla::Maybe<JS::Value>> aError, ErrorResult& aRv)
            MOZ_CAN_RUN_SCRIPT {
              JS::Rooted<JS::Value> error(aCx, *aError);
              RefPtr<ReadableStream> dest = aPipeToPump->mReader->GetStream();
              return ReadableStreamCancel(aCx, dest, error, aRv);
            },
        error);
  } else {
    // Step 2.1. Otherwise, shutdown with dest.[[storedError]].
    Shutdown(aCx, error);
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(PipeToPump)
NS_IMPL_CYCLE_COLLECTING_ADDREF(PipeToPump)
NS_IMPL_CYCLE_COLLECTING_RELEASE(PipeToPump)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PipeToPump)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(PipeToPump)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWriter)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLastWritePromise)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(PipeToPump)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWriter)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLastWritePromise)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

namespace streams_abstract {
// https://streams.spec.whatwg.org/#readable-stream-pipe-to
already_AddRefed<Promise> ReadableStreamPipeTo(
    ReadableStream* aSource, WritableStream* aDest, bool aPreventClose,
    bool aPreventAbort, bool aPreventCancel, AbortSignal* aSignal,
    mozilla::ErrorResult& aRv) {
  // Step 1. Assert: source implements ReadableStream. (Implicit)
  // Step 2. Assert: dest implements WritableStream. (Implicit)
  // Step 3. Assert: preventClose, preventAbort, and preventCancel are all
  //         booleans (Implicit)
  // Step 4. If signal was not given, let signal be
  //         undefined. (Implicit)
  // Step 5. Assert: either signal is undefined, or signal
  //         implements AbortSignal. (Implicit)
  // Step 6. Assert: !IsReadableStreamLocked(source) is false.
  MOZ_ASSERT(!IsReadableStreamLocked(aSource));

  // Step 7. Assert: !IsWritableStreamLocked(dest) is false.
  MOZ_ASSERT(!IsWritableStreamLocked(aDest));

  AutoJSAPI jsapi;
  if (!jsapi.Init(aSource->GetParentObject())) {
    aRv.ThrowUnknownError("Internal error");
    return nullptr;
  }
  JSContext* cx = jsapi.cx();

  // Step 8. If source.[[controller]] implements ReadableByteStreamController,
  //         let reader be either !AcquireReadableStreamBYOBReader(source) or
  //         !AcquireReadableStreamDefaultReader(source), at the user agent’s
  //         discretion.
  // Step 9. Otherwise, let reader be
  //         !AcquireReadableStreamDefaultReader(source).

  // Note: In the interests of simplicity, we choose here to always acquire
  // a default reader.
  RefPtr<ReadableStreamDefaultReader> reader =
      AcquireReadableStreamDefaultReader(aSource, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 10. Let writer be ! AcquireWritableStreamDefaultWriter(dest).
  RefPtr<WritableStreamDefaultWriter> writer =
      AcquireWritableStreamDefaultWriter(aDest, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 11. Set source.[[disturbed]] to true.
  aSource->SetDisturbed(true);

  // Step 12. Let shuttingDown be false.
  // Note: PipeToPump ensures this by construction.

  // Step 13. Let promise be a new promise.
  RefPtr<Promise> promise =
      Promise::CreateInfallible(aSource->GetParentObject());

  // Steps 14-15.
  RefPtr<PipeToPump> pump = new PipeToPump(
      promise, reader, writer, aPreventClose, aPreventAbort, aPreventCancel);
  pump->Start(cx, aSignal);

  // Step 16. Return promise.
  return promise.forget();
}
}  // namespace streams_abstract

}  // namespace mozilla::dom
