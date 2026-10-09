/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "jsapi/RTCEncodedFrameBase.h"

#include <cstddef>
#include <utility>

#include "api/frame_transformer_interface.h"
#include "js/ArrayBuffer.h"
#include "js/GCAPI.h"
#include "js/StructuredClone.h"
#include "js/Wrapper.h"
#include "js/experimental/TypedData.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/RTCRtpScriptTransformer.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/fallible.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(RTCEncodedFrameBase)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RTCEncodedFrameBase)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner, mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mData)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RTCEncodedFrameBase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner, mGlobal)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(RTCEncodedFrameBase)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBERS(mData)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(RTCEncodedFrameBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RTCEncodedFrameBase)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RTCEncodedFrameBase)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

RTCEncodedFrameBase::RTCEncodedFrameBase(
    nsIGlobalObject* aGlobal,
    std::unique_ptr<webrtc::TransformableFrameInterface> aFrame,
    uint64_t aCounter, RTCRtpScriptTransformer* aOwner)
    : mGlobal(aGlobal),
      mOwner(aOwner),
      mFrame(std::move(aFrame)),
      mCounter(aCounter) {
  MOZ_ASSERT(mFrame);
  mozilla::HoldJSObjects(this);

  // Leaves mData null if the buffer cannot be made, which is what HasData
  // reports. A frame with no data will be dropped by RTCRtpScriptTransformer.
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(mGlobal))) {
    return;
  }

  const auto& data = mFrame->GetData();

  if (data.empty()) {
    mData = JS::NewArrayBuffer(jsapi.cx(), 0);
    return;
  }

  UniquePtr<void, JS::FreePolicy> jsdata(
      js_pod_arena_malloc<uint8_t>(js::ArrayBufferContentsArena, data.size()));
  if (NS_WARN_IF(!jsdata)) {
    return;
  }

  memcpy(jsdata.get(), data.data(), data.size());
  mData = JS::NewArrayBufferWithContents(jsapi.cx(), data.size(),
                                         std::move(jsdata));
}

RTCEncodedFrameBase::RTCEncodedFrameBase(nsIGlobalObject* aGlobal,
                                         JS::Handle<JSObject*> aData)
    : mGlobal(aGlobal) {
  mozilla::HoldJSObjects(this);
  mData = aData;
}

RTCEncodedFrameBase::~RTCEncodedFrameBase() {
  DetachData();
  mozilla::DropJSObjects(this);
}

void RTCEncodedFrameBase::DetachData() {
  // We might have handled this in unlink already
  if (mGlobal && mData) {
    AutoJSAPI jsapi;
    if (NS_WARN_IF(!jsapi.Init(mGlobal))) {
      return;
    }

    JS::Rooted<JSObject*> rootedData(jsapi.cx(), mData);
    if (rootedData) {
      JS::DetachArrayBuffer(jsapi.cx(), rootedData);
    }
  }
}

nsIGlobalObject* RTCEncodedFrameBase::GetParentObject() const {
  return mGlobal;
}

void RTCEncodedFrameBase::SetData(const ArrayBuffer& aData, ErrorResult& aRv) {
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(mGlobal))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> data(cx, aData.Obj());
  if (!JS_WrapObject(cx, &data)) {
    aRv.StealExceptionFromJSContext(cx);
    return;
  }
  mData = data;
}

void RTCEncodedFrameBase::GetData(JSContext* aCx,
                                  JS::Rooted<JSObject*>* aObj) const {
  aObj->set(mData);
}

bool RTCEncodedFrameBase::CopyData(JSContext* aCx,
                                   JS::MutableHandle<JSObject*> aData) const {
  if (!mData || JS::IsDetachedArrayBufferObject(mData)) {
    // The spec has a hole here. We use DataCloneError for now.
    // If the spec settles on TypeError, JS:CopyArrayBuffer will do that for us.
    // see https://github.com/w3c/webrtc-encoded-transform/issues/315
    ErrorResult rv;
    rv.ThrowDataCloneError("The frame's data has been detached");
    (void)rv.MaybeSetPendingException(aCx);
    return false;
  }

  JS::Rooted<JSObject*> data(aCx, mData);
  if (!JS_WrapObject(aCx, &data)) {
    return false;
  }
  aData.set(JS::CopyArrayBuffer(aCx, data));
  return !NS_WARN_IF(!aData);
}

bool RTCEncodedFrameBase::WriteData(JSContext* aCx,
                                    JSStructuredCloneWriter* aWriter) const {
  if (!mData || JS::IsDetachedArrayBufferObject(mData)) {
    // Returning false causes a DataCloneError, which is what we want here.
    return false;
  }

  // JS_WriteTypedArray does not take a bare ArrayBuffer, so hand it a view.
  // TODO: Update this once bug 2067921 is fixed.
  JS::Rooted<JSObject*> buffer(aCx, mData);
  // Put this in the right compartment
  if (NS_WARN_IF(!JS_WrapObject(aCx, &buffer))) {
    return false;
  }
  JS::Rooted<JSObject*> view(aCx,
                             JS_NewUint8ArrayWithBuffer(aCx, buffer, 0, -1));
  if (NS_WARN_IF(!view)) {
    return false;
  }

  JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*view));
  // JS_WrapValue isn't necessary here, but if bug 2067921 is fixed and we use
  // JS_WriteArrayBuffer above instead of the "wrap in a typed array" hack, we
  // are likely to need it.
  return !NS_WARN_IF(!JS_WrapValue(aCx, &value)) &&
         !NS_WARN_IF(!JS_WriteTypedArray(aWriter, value));
}

/* static */
bool RTCEncodedFrameBase::ReadData(JSContext* aCx,
                                   JSStructuredCloneReader* aReader,
                                   JS::MutableHandle<JSObject*> aData) {
  JS::Rooted<JS::Value> value(aCx);
  if (NS_WARN_IF(!JS_ReadTypedArray(aReader, &value)) ||
      NS_WARN_IF(!value.isObject())) {
    return false;
  }

  JS::Rooted<JSObject*> view(aCx, &value.toObject());
  bool isShared = false;
  aData.set(JS_GetArrayBufferViewBuffer(aCx, view, &isShared));
  return !NS_WARN_IF(!aData);
}

uint64_t RTCEncodedFrameBase::GetCounter() const { return mCounter; }

std::unique_ptr<webrtc::TransformableFrameInterface>
RTCEncodedFrameBase::TakeFrame() {
  if (mFrame) {
    JS::AutoCheckCannotGC nogc;
    bool isShared;
    size_t length = JS::GetArrayBufferByteLength(mData);
    uint8_t* data = JS::GetArrayBufferData(mData, &isShared, nogc);
    if (data && length) {
      // This makes a copy
      mFrame->SetData({data, length});
    } else {
      mFrame->SetData({});
    }
  }
  DetachData();
  return std::move(mFrame);
}

size_t RTCEncodedFrameBase::Size() const {
  return GetArrayBufferByteLength(mData);
}

}  // namespace mozilla::dom
