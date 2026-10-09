/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_MEDIA_WEBRTC_JSAPI_RTCENCODEDFRAMEBASE_H_
#define MOZILLA_DOM_MEDIA_WEBRTC_JSAPI_RTCENCODEDFRAMEBASE_H_

#include <memory>

#include "js/TypeDecls.h"
#include "mozilla/dom/TypedArray.h"  // ArrayBuffer

class nsIGlobalObject;
struct JSStructuredCloneReader;
struct JSStructuredCloneWriter;

namespace webrtc {
class TransformableFrameInterface;
}

namespace mozilla {

class ErrorResult;

namespace dom {

class RTCRtpScriptTransformer;

class RTCEncodedFrameBase : public nsISupports, public nsWrapperCache {
 public:
  // A live frame; owns the libwebrtc frame it came from, and can be enqueued
  // back into the transformer that owns it (see TakeFrame).
  RTCEncodedFrameBase(
      nsIGlobalObject* aGlobal,
      std::unique_ptr<webrtc::TransformableFrameInterface> aFrame,
      uint64_t aCounter, RTCRtpScriptTransformer* aOwner);

  // A copy (structured clone, or copy construction from script), adopting a
  // buffer the JS engine made for us (see ReadData and CopyData). Does not have
  // an owner, meaning it cannot be enqueued, which means it doesn't need a
  // counter or libwebrtc frame either.
  RTCEncodedFrameBase(nsIGlobalObject* aGlobal, JS::Handle<JSObject*> aData);

  // There's no situation where creating an exact copy is valid right now, even
  // if the members were copyable. No need for move either.
  RTCEncodedFrameBase(const RTCEncodedFrameBase&) = delete;
  RTCEncodedFrameBase& operator=(const RTCEncodedFrameBase&) = delete;
  RTCEncodedFrameBase(RTCEncodedFrameBase&&) = delete;
  RTCEncodedFrameBase& operator=(RTCEncodedFrameBase&&) = delete;

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(RTCEncodedFrameBase)

  nsIGlobalObject* GetParentObject() const;

  void SetData(const ArrayBuffer& aData, ErrorResult& aRv);

  void GetData(JSContext* aCx, JS::Rooted<JSObject*>* aObj) const;

  // False if we could not make a buffer for the frame's data. Such a frame is
  // not fit to hand to script, and will be dropped.
  bool HasData() const { return mData; }

  uint64_t GetCounter() const;

  size_t Size() const;

  bool CheckOwner(RTCRtpScriptTransformer* aOwner) const {
    return aOwner == mOwner;
  }

  std::unique_ptr<webrtc::TransformableFrameInterface> TakeFrame();

 protected:
  virtual ~RTCEncodedFrameBase();
  void DetachData();

  // Deep copies the data buffer, as copy construction needs. Leaves an
  // exception pending on aCx and returns false on failure.
  [[nodiscard]] bool CopyData(JSContext* aCx,
                              JS::MutableHandle<JSObject*> aData) const;

  // Writes the data buffer into the clone stream, which gets us its
  // ArrayBuffer serialization semantics (transfer, internal backreferences)
  // instead of reimplementing them. Leaves an exception pending on aCx on
  // failure, as the structured clone callbacks are expected to do.
  [[nodiscard]] bool WriteData(JSContext* aCx,
                               JSStructuredCloneWriter* aWriter) const;

  // Reads back what WriteData wrote.
  [[nodiscard]] static bool ReadData(JSContext* aCx,
                                     JSStructuredCloneReader* aReader,
                                     JS::MutableHandle<JSObject*> aData);

  RefPtr<nsIGlobalObject> mGlobal;
  JS::Heap<JSObject*> mData;

  // These are only set on originals, not copies/clones.
  RefPtr<RTCRtpScriptTransformer> mOwner;
  std::unique_ptr<webrtc::TransformableFrameInterface> mFrame;
  uint64_t mCounter = 0;
};

}  // namespace dom
}  // namespace mozilla
#endif  // MOZILLA_DOM_MEDIA_WEBRTC_JSAPI_RTCENCODEDFRAMEBASE_H_
