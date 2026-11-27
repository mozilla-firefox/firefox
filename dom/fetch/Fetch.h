/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Fetch_h
#define mozilla_dom_Fetch_h

#include "mozilla/Attributes.h"
#include "mozilla/dom/AbortSignal.h"
#include "mozilla/dom/BodyConsumer.h"
#include "mozilla/dom/FetchBindingFwd.h"
#include "mozilla/dom/FetchStreamReader.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/ReadableStreamDefaultReaderBinding.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/workerinternals/RuntimeService.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsProxyRelease.h"
#include "nsString.h"

class nsIGlobalObject;
class nsIEventTarget;

namespace mozilla {
class ErrorResult;

namespace ipc {
class PrincipalInfo;
}  // namespace ipc

namespace dom {

class BlobImpl;
class InternalRequest;

class ReadableStreamDefaultReader;
class RequestOrUTF8String;
class WorkerPrivate;

enum class CallerType : uint32_t;

already_AddRefed<Promise> FetchRequest(nsIGlobalObject* aGlobal,
                                       const RequestOrUTF8String& aInput,
                                       const RequestInit& aInit,
                                       CallerType aCallerType,
                                       ErrorResult& aRv);

nsresult UpdateRequestReferrer(nsIGlobalObject* aGlobal,
                               InternalRequest* aRequest);

namespace fetch {
using BodyInit = dom::BodyInit;
// ResponseBodyInit is now the same as BodyInit since both include
// ReadableStream
using ResponseBodyInit = BodyInit;
using OwningBodyInit = dom::OwningBodyInit;
};  // namespace fetch

/*
 * Creates an nsIInputStream based on the fetch specifications 'extract a byte
 * stream algorithm' - http://fetch.spec.whatwg.org/#concept-bodyinit-extract.
 * Stores content type in out param aContentType.
 */
nsresult ExtractByteStreamFromBody(const fetch::OwningBodyInit& aBodyInit,
                                   nsIInputStream** aStream,
                                   nsCString& aContentType,
                                   uint64_t& aContentLength);

/*
 * Non-owning version.
 */
nsresult ExtractByteStreamFromBody(const fetch::BodyInit& aBodyInit,
                                   nsIInputStream** aStream,
                                   nsCString& aContentType,
                                   uint64_t& aContentLength);

/*
 * FetchBody's body consumption uses nsIInputStreamPump to read from the
 * underlying stream to a block of memory, which is then adopted by
 * ContinueConsumeBody() and converted to the right type based on the JS
 * function called.
 *
 * Use of the nsIInputStreamPump complicates things on the worker thread.
 * The solution used here is similar to WebSockets.
 * The difference is that we are only interested in completion and not data
 * events, and nsIInputStreamPump can only deliver completion on the main
 * thread.
 *
 * Before starting the pump on the main thread, we addref the FetchBody to keep
 * it alive. Then we add a feature, to track the status of the worker.
 *
 * ContinueConsumeBody() is the function that cleans things up in both success
 * and error conditions and so all callers call it with the appropriate status.
 *
 * Once the read is initiated on the main thread there are two possibilities.
 *
 * 1) Pump finishes before worker has finished Running.
 *    In this case we adopt the data and dispatch a runnable to the worker,
 *    which derefs FetchBody and removes the feature and resolves the Promise.
 *
 * 2) Pump still working while worker has stopped Running.
 *    The feature is Notify()ed and ContinueConsumeBody() is called with
 *    NS_BINDING_ABORTED. We first Cancel() the pump using a sync runnable to
 *    ensure that mFetchBody remains alive (since mConsumeBodyPump is strongly
 *    held by it) until pump->Cancel() is called. OnStreamComplete() will not
 *    do anything if the error code is NS_BINDING_ABORTED, so we don't have to
 *    worry about keeping anything alive.
 *
 * The pump is always released on the main thread.
 */

class FetchBodyBase : public nsISupports {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(FetchBodyBase)

 protected:
  virtual ~FetchBodyBase() = default;

  RefPtr<ReadableStream> mReadableStreamBody;
};

template <class Derived>
class FetchBody : public FetchBodyBase, public AbortFollower {
 public:
  using FetchBodyBase::QueryInterface;

  NS_INLINE_DECL_REFCOUNTING_INHERITED(FetchBody, FetchBodyBase)

  bool BodyUsed() const;

  // https://fetch.spec.whatwg.org/#body-unusable: the body's stream is
  // disturbed or locked.
  bool IsBodyUnusable() const;

  // Cancels the body, per the Streams "cancel" operation, and
  // marks it used so later consumers and clone() see it as unusable. Closes
  // the underlying native stream when no ReadableStream reflector exists yet.
  //
  // This is marked as a script boundary to minimize changes required for
  // annotation while we work out how to correctly annotate this code.
  // Tracked in Bug 1750650.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void CancelBody(JSContext* aCx, ErrorResult& aRv,
                  JS::Handle<JS::Value> aReason = JS::UndefinedHandleValue);

  already_AddRefed<Promise> ArrayBuffer(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::ArrayBuffer, aRv);
  }

  already_AddRefed<Promise> Blob(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::Blob, aRv);
  }

  already_AddRefed<Promise> Bytes(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::Bytes, aRv);
  }

  already_AddRefed<Promise> FormData(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::FormData, aRv);
  }

  already_AddRefed<Promise> Json(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::JSON, aRv);
  }

  already_AddRefed<Promise> Text(JSContext* aCx, ErrorResult& aRv) {
    return ConsumeBody(aCx, BodyConsumer::ConsumeType::Text, aRv);
  }

  already_AddRefed<ReadableStream> GetBody(JSContext* aCx, ErrorResult& aRv);
  void GetMimeType(nsACString& aMimeType, nsACString& aMixedCaseMimeType);

  BlobImpl* BodyBlobImpl() const;

  const nsAString& BodyLocalPath() const;

  // If the body contains a ReadableStream body object, this method produces a
  // tee() of it.
  //
  // This is marked as a script boundary minimize changes required for
  // annotation while we work out how to correctly annotate this code.
  // Tracked in Bug 1750650.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void MaybeTeeReadableStreamBody(JSContext* aCx, ReadableStream** aBodyOut,
                                  FetchStreamReader** aStreamReader,
                                  nsIInputStream** aInputStream,
                                  ErrorResult& aRv);

  // After clone() clones the underlying nsIInputStream, an unread native
  // ReadableStream reflector may still point at the original stream, which
  // clone() can have replaced (for non-cloneable bodies it is now consumed by
  // the cloning copy). Repoint such a reflector at the current body stream so
  // the original stream is not read from two places. No-op when there is no
  // reflector or it is not a native unread stream.
  void MaybeRebindReadableStreamBody();

  // Utility public methods accessed by various runnables.

  // This method _must_ be called in order to set the body as used. If the body
  // is a ReadableStream, this method will start reading the stream.
  // More in details, this method does:
  // 1) It uses an internal flag to track if the body is used.  This is tracked
  // separately from the ReadableStream disturbed state due to purely native
  // streams.
  // 2) If there is a ReadableStream reflector for the native stream it is
  // Locked.
  // 3) If there is a JS ReadableStream then we begin pumping it into the native
  // body stream.  This effectively locks and disturbs the stream.
  //
  // Note that JSContext is used only if there is a ReadableStream (this can
  // happen because the body is a ReadableStream or because attribute body has
  // already been used by content). If something goes wrong using
  // ReadableStream, errors will be reported via ErrorResult and not as JS
  // exceptions in JSContext. This is done in order to have a centralized error
  // reporting way.
  //
  // Exceptions generated when reading from the ReadableStream are directly sent
  // to the Console.
  void SetBodyUsed(JSContext* aCx, ErrorResult& aRv);

  virtual AbortSignalImpl* GetSignalImpl() const = 0;

  virtual AbortSignalImpl* GetSignalImplToConsumeBody() const = 0;

  // AbortFollower
  void RunAbortAlgorithm() override;

  already_AddRefed<Promise> ConsumeBody(JSContext* aCx,
                                        BodyConsumer::ConsumeType aType,
                                        ErrorResult& aRv);

 protected:
  nsCOMPtr<nsIGlobalObject> mGlobal;

  // This is the Reader used to retrieve data from the body. This needs to be
  // traversed by subclasses.
  RefPtr<FetchStreamReader> mFetchStreamReader;

  explicit FetchBody(nsIGlobalObject* aGlobal);

  virtual ~FetchBody();

  void SetReadableStreamBody(JSContext* aCx, ReadableStream* aBody);

 private:
  Derived* DerivedClass() const {
    return static_cast<Derived*>(const_cast<FetchBody*>(this));
  }

  void LockStream(JSContext* aCx, ReadableStream* aStream, ErrorResult& aRv);

  void AssertIsOnTargetThread() {
    MOZ_ASSERT(NS_IsMainThread() == !GetCurrentThreadWorkerPrivate());
  }

  // Only ever set once, always on target thread.
  bool mBodyUsed;

  // The main-thread event target for runnable dispatching.
  nsCOMPtr<nsISerialEventTarget> mMainThreadEventTarget;
};

class EmptyBody final : public FetchBody<EmptyBody> {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(EmptyBody,
                                                         FetchBody<EmptyBody>)

 public:
  static already_AddRefed<EmptyBody> Create(
      nsIGlobalObject* aGlobal, mozilla::ipc::PrincipalInfo* aPrincipalInfo,
      AbortSignalImpl* aAbortSignalImpl, const nsACString& aMimeType,
      const nsACString& aMixedCaseMimeType, ErrorResult& aRv);

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  AbortSignalImpl* GetSignalImpl() const override { return mAbortSignalImpl; }
  AbortSignalImpl* GetSignalImplToConsumeBody() const final { return nullptr; }

  const UniquePtr<mozilla::ipc::PrincipalInfo>& GetPrincipalInfo() const {
    return mPrincipalInfo;
  }

  void GetMimeType(nsACString& aMimeType, nsACString& aMixedCaseMimeType) {
    aMimeType = mMimeType;
    aMixedCaseMimeType = mMixedCaseMimeType;
  }

  void GetBody(nsIInputStream** aStream, int64_t* aBodyLength = nullptr);

  using FetchBody::BodyBlobImpl;

  BlobImpl* BodyBlobImpl() const { return nullptr; }

  using FetchBody::BodyLocalPath;

  const nsAString& BodyLocalPath() const { return EmptyString(); }

 private:
  EmptyBody(nsIGlobalObject* aGlobal,
            mozilla::ipc::PrincipalInfo* aPrincipalInfo,
            AbortSignalImpl* aAbortSignalImpl, const nsACString& aMimeType,
            const nsACString& aMixedCaseMimeType,
            already_AddRefed<nsIInputStream> aBodyStream);

  ~EmptyBody();

  UniquePtr<mozilla::ipc::PrincipalInfo> mPrincipalInfo;
  RefPtr<AbortSignalImpl> mAbortSignalImpl;
  nsCString mMimeType;
  nsCString mMixedCaseMimeType;
  nsCOMPtr<nsIInputStream> mBodyStream;
};
}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_Fetch_h
