/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_UnderlyingSourceCallbackHelpers_h
#define mozilla_dom_UnderlyingSourceCallbackHelpers_h

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/UnderlyingSourceBinding.h"
#include "nsIAsyncInputStream.h"
#include "nsISupports.h"
#include "nsISupportsImpl.h"

/* Since the streams specification has native descriptions of some callbacks
 * (i.e. described in prose, rather than provided by user code), we need to be
 * able to pass around native callbacks. To handle this, we define polymorphic
 * classes That cover the difference between native callback and user-provided.
 *
 * The Streams specification wants us to invoke these callbacks, run through
 * WebIDL as if they were methods. So we have to preserve the underlying object
 * to use as the This value on invocation.
 */
enum class nsresult : uint32_t;

namespace mozilla::dom {

class StrongWorkerRef;
class BodyStreamHolder;
class ReadableStreamControllerBase;
class ReadableStream;

class UnderlyingSourceAlgorithmsBase : public nsISupports {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(UnderlyingSourceAlgorithmsBase)

  MOZ_CAN_RUN_SCRIPT virtual void StartCallback(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      JS::MutableHandle<JS::Value> aRetVal, ErrorResult& aRv) = 0;

  // A promise-returning algorithm that pulls data from the underlying byte
  // source
  MOZ_CAN_RUN_SCRIPT virtual already_AddRefed<Promise> PullCallback(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) = 0;

  // A promise-returning algorithm, taking one argument (the cancel reason),
  // which communicates a requested cancelation to the underlying byte source
  MOZ_CAN_RUN_SCRIPT virtual already_AddRefed<Promise> CancelCallback(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) = 0;

  // Implement this when you need to release underlying resources immediately
  // from closed(canceled)/errored streams, without waiting for GC.
  virtual void ReleaseObjects() {}

  // Can be used to read chunks directly via nsIInputStream to skip JS-related
  // overhead, if this readable stream is a wrapper of a native stream.
  // Currently used by Fetch helper functions e.g. new Response(stream).text()
  virtual nsIInputStream* MaybeGetInputStreamIfUnread() { return nullptr; }

  // Replaces the underlying input stream of an unread native stream. Used by
  // Fetch's clone() to repoint a body's ReadableStream at a freshly cloned
  // input stream without disturbing the stream or changing its identity. No-op
  // for non-native streams. Must only be called on a non-disturbed stream.
  virtual void SetInputStreamIfUnread(nsIInputStream* aInput) {}

  // https://streams.spec.whatwg.org/#other-specs-rs-create
  // By "native" we mean "instances initialized via the above set up or set up
  // with byte reading support algorithms (not, e.g., on web-developer-created
  // instances)"
  virtual bool IsNative() { return true; }

 protected:
  virtual ~UnderlyingSourceAlgorithmsBase() = default;
};

class UnderlyingSourceAlgorithms final : public UnderlyingSourceAlgorithmsBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
      UnderlyingSourceAlgorithms, UnderlyingSourceAlgorithmsBase)

  UnderlyingSourceAlgorithms(nsIGlobalObject* aGlobal,
                             JS::Handle<JSObject*> aUnderlyingSource,
                             UnderlyingSource& aUnderlyingSourceDict)
      : mGlobal(aGlobal), mUnderlyingSource(aUnderlyingSource) {
    // Step 6. (implicit Step 2.)
    if (aUnderlyingSourceDict.mStart.WasPassed()) {
      mStartCallback = aUnderlyingSourceDict.mStart.Value();
    }

    // Step 7. (implicit Step 3.)
    if (aUnderlyingSourceDict.mPull.WasPassed()) {
      mPullCallback = aUnderlyingSourceDict.mPull.Value();
    }

    // Step 8. (implicit Step 4.)
    if (aUnderlyingSourceDict.mCancel.WasPassed()) {
      mCancelCallback = aUnderlyingSourceDict.mCancel.Value();
    }

    mozilla::HoldJSObjects(this);
  };

  MOZ_CAN_RUN_SCRIPT void StartCallback(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      JS::MutableHandle<JS::Value> aRetVal, ErrorResult& aRv) override;

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> PullCallback(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) override;

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> CancelCallback(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) override;

  bool IsNative() override { return false; }

 protected:
  ~UnderlyingSourceAlgorithms() override { mozilla::DropJSObjects(this); };

 private:
  // Virtually const, but are cycle collected
  nsCOMPtr<nsIGlobalObject> mGlobal;
  JS::Heap<JSObject*> mUnderlyingSource;
  MOZ_KNOWN_LIVE RefPtr<UnderlyingSourceStartCallback> mStartCallback;
  MOZ_KNOWN_LIVE RefPtr<UnderlyingSourcePullCallback> mPullCallback;
  MOZ_KNOWN_LIVE RefPtr<UnderlyingSourceCancelCallback> mCancelCallback;
};

// https://streams.spec.whatwg.org/#readablestream-set-up
// https://streams.spec.whatwg.org/#readablestream-set-up-with-byte-reading-support
// Wrappers defined by the "Set up" methods in the spec. This helps you just
// return nullptr when an error occurred as this wrapper converts it to a
// rejected promise.
// Note that StartCallback is only for JS consumers to access
// the controller, and thus is no-op here since native consumers can call
// `EnqueueNative()` etc. without direct controller access.
class UnderlyingSourceAlgorithmsWrapper
    : public UnderlyingSourceAlgorithmsBase {
  void StartCallback(JSContext*, ReadableStreamControllerBase&,
                     JS::MutableHandle<JS::Value> aRetVal, ErrorResult&) final;

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> PullCallback(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) final;

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> CancelCallback(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) final;

  MOZ_CAN_RUN_SCRIPT virtual already_AddRefed<Promise> PullCallbackImpl(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) {
    // pullAlgorithm is optional, return null by default
    return nullptr;
  }

  MOZ_CAN_RUN_SCRIPT virtual already_AddRefed<Promise> CancelCallbackImpl(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) {
    // cancelAlgorithm is optional, return null by default
    return nullptr;
  }
};

class InputToReadableStreamAlgorithms;

// This class exists to isolate InputToReadableStreamAlgorithms from the
// nsIAsyncInputStream.  If we call AsyncWait(this,...), it holds a
// reference to 'this' which can't be cc'd, and we can leak the stream,
// causing a Worker to assert with globalScopeAlive.  By isolating
// ourselves from the inputstream, we can safely be CC'd if needed and
// will inform the inputstream to shut down.
class InputStreamHolder final : public nsIInputStreamCallback,
                                public GlobalTeardownObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINPUTSTREAMCALLBACK

  InputStreamHolder(nsIGlobalObject* aGlobal,
                    InputToReadableStreamAlgorithms* aCallback,
                    nsIAsyncInputStream* aInput);

  void Init(JSContext* aCx);

  void DisconnectFromOwner() override;

  // Used by global teardown
  void Shutdown();

  // These just proxy the calls to the nsIAsyncInputStream
  nsresult AsyncWait(uint32_t aFlags, uint32_t aRequestedCount,
                     nsIEventTarget* aEventTarget);
  nsresult Available(uint64_t* aSize) { return mInput->Available(aSize); }
  nsresult Read(char* aBuffer, uint32_t aLength, uint32_t* aWritten) {
    return mInput->Read(aBuffer, aLength, aWritten);
  }
  nsresult CloseWithStatus(nsresult aStatus) {
    return mInput->CloseWithStatus(aStatus);
  }

  nsIAsyncInputStream* GetInputStream() { return mInput; }

 private:
  ~InputStreamHolder();

  // WeakPtr to avoid cycles
  WeakPtr<InputToReadableStreamAlgorithms> mCallback;
  // To ensure the worker sticks around
  RefPtr<StrongWorkerRef> mAsyncWaitWorkerRef;
  RefPtr<StrongWorkerRef> mWorkerRef;
  nsCOMPtr<nsIAsyncInputStream> mInput;

  // To ensure the underlying source sticks around during an ongoing read
  // operation. mAlgorithms is not cycle collected on purpose, and this holder
  // is responsible to keep the underlying source algorithms until
  // nsIAsyncInputStream responds.
  //
  // This is done because otherwise the whole stream objects may be cycle
  // collected, including the promises created from read(), as our JS engine may
  // throw unsettled promises away for optimization. See bug 1849860.
  RefPtr<InputToReadableStreamAlgorithms> mAsyncWaitAlgorithms;
};

// Using this class means you are also passing the lifetime control of your
// nsIAsyncInputStream, as it will be closed when this class tears down.
class InputToReadableStreamAlgorithms
    : public UnderlyingSourceAlgorithmsWrapper,
      public nsIInputStreamCallback,
      public SupportsWeakPtr {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIINPUTSTREAMCALLBACK
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(InputToReadableStreamAlgorithms,
                                           UnderlyingSourceAlgorithmsWrapper)

  InputToReadableStreamAlgorithms(JSContext* aCx, nsIAsyncInputStream* aInput,
                                  ReadableStream* aStream);

  // Streams algorithms

  already_AddRefed<Promise> PullCallbackImpl(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) override;

  void ReleaseObjects() override;

  nsIInputStream* MaybeGetInputStreamIfUnread() override;

 protected:
  ~InputToReadableStreamAlgorithms();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void CloseAndReleaseObjects(
      JSContext* aCx, ReadableStream* aStream);

  void WriteIntoReadRequestBuffer(JSContext* aCx, ReadableStream* aStream,
                                  JS::Handle<JSObject*> aBuffer,
                                  uint32_t aLength, uint32_t* aByteWritten,
                                  ErrorResult& aRv);

  // https://streams.spec.whatwg.org/#readablestream-pull-from-bytes
  // (Uses InputStreamHolder for the "byte sequence" in the spec)
  MOZ_CAN_RUN_SCRIPT void PullFromInputStream(JSContext* aCx,
                                              uint64_t aAvailable,
                                              ErrorResult& aRv);

  // Closes or errors aStream according to the failure status of the backing
  // nsIInputStream. Subclasses may override to run extra steps after the
  // stream is errored; note that erroring the stream calls ReleaseObjects().
  virtual void ErrorPropagation(JSContext* aCx, ReadableStream* aStream,
                                nsresult aError);

  // Decides whether a failure of the backing nsIInputStream (other than
  // NS_BASE_STREAM_CLOSED) errors the ReadableStream.  Override to return
  // false when the stream will be errored through another channel.  Keep the
  // condition strict: a status that is not propagated here, but never arrives
  // on the other channel, leaves the stream hanging.
  virtual bool ShouldPropagateError(nsresult aError) { return true; }

  // Builds the JS error value used to error the stream when the input stream
  // closes with a failure status other than NS_BASE_STREAM_CLOSED. The default
  // is a generic TypeError; subclasses may override to map specific nsresults
  // to more meaningful errors (e.g. a named DOMException).
  virtual void BuildErrorValue(JSContext* aCx, nsresult aError,
                               JS::MutableHandle<JS::Value> aErrorValue);

  // Common methods

  bool IsClosed() { return !mInput; }

  nsCOMPtr<nsIEventTarget> mOwningEventTarget;

  // This promise is created by PullCallback and resolved when
  // OnInputStreamReady succeeds. No need to try hard to settle it though, see
  // also ReleaseObjects() for the reason.
  RefPtr<Promise> mPullPromise;

  RefPtr<InputStreamHolder> mInput;

  // mStream never changes after construction and before CC
  MOZ_KNOWN_LIVE RefPtr<ReadableStream> mStream;
};

class NonAsyncInputToReadableStreamAlgorithms
    : public UnderlyingSourceAlgorithmsWrapper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(
      NonAsyncInputToReadableStreamAlgorithms,
      UnderlyingSourceAlgorithmsWrapper)

  explicit NonAsyncInputToReadableStreamAlgorithms(nsIInputStream& aInput)
      : mInput(&aInput) {}

  already_AddRefed<Promise> PullCallbackImpl(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) override;

  void ReleaseObjects() override {
    if (RefPtr<InputToReadableStreamAlgorithms> algorithms =
            mAsyncAlgorithms.forget()) {
      algorithms->ReleaseObjects();
    }
    if (nsCOMPtr<nsIInputStream> input = mInput.forget()) {
      input->Close();
    }
  }

  nsIInputStream* MaybeGetInputStreamIfUnread() override {
    MOZ_ASSERT(mInput, "Should be only called on non-disturbed streams");
    return mInput;
  }

  void SetInputStreamIfUnread(nsIInputStream* aInput) override {
    MOZ_ASSERT(mInput, "Should be only called on non-disturbed streams");
    MOZ_ASSERT(!mAsyncAlgorithms,
               "Should be only called before the stream is read");
    mInput = aInput;
  }

 private:
  ~NonAsyncInputToReadableStreamAlgorithms() = default;

  nsCOMPtr<nsIInputStream> mInput;
  RefPtr<InputToReadableStreamAlgorithms> mAsyncAlgorithms;
};

}  // namespace mozilla::dom

#endif
