/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageData.h"

#include "ErrorList.h"
#include "js/StructuredClone.h"
#include "js/Value.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CanvasRenderingContext2DBinding.h"
#include "mozilla/dom/ImageDataBinding.h"
#include "nsCycleCollectionNoteChild.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageData)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(ImageData, DropData())

NS_IMPL_CYCLE_COLLECTION_CLASS(ImageData)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageData)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(ImageData)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mData)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ImageData)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ImageData)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->DropData();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(ImageData)
  if (tmp->HasKnownLiveWrapper()) {
    tmp->mData.exposeToActiveJS();
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(ImageData)
  return tmp->HasKnownLiveWrapper() && tmp->HasNothingToTrace(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(ImageData)
  return tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

static PredefinedColorSpace GetColorSpaceFromSettings(
    const ImageDataSettings& aSettings) {
  return aSettings.mColorSpace.WasPassed() ? aSettings.mColorSpace.Value()
                                           : PredefinedColorSpace::Srgb;
}

// static
already_AddRefed<ImageData> ImageData::Constructor(
    const GlobalObject& aGlobal, const uint32_t aWidth, const uint32_t aHeight,
    const ImageDataSettings& aSettings, ErrorResult& aRv) {
  if (aWidth == 0 || aHeight == 0) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }

  // Restrict the typed array length to INT32_MAX because that's all we support.
  CheckedInt<uint32_t> length = CheckedInt<uint32_t>(aWidth) * aHeight * 4;
  if (!length.isValid() || length.value() > INT32_MAX) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }
  js::AssertSameCompartment(aGlobal.Context(), aGlobal.Get());
  JS::Rooted<JSObject*> data(
      aGlobal.Context(),
      Uint8ClampedArray::Create(aGlobal.Context(), length.value(), aRv));
  if (aRv.Failed()) {
    return nullptr;
  }

  PredefinedColorSpace colorSpace = GetColorSpaceFromSettings(aSettings);

  RefPtr<ImageData> imageData =
      new ImageData(aGlobal.GetAsSupports(), aWidth, aHeight, colorSpace, data);
  return imageData.forget();
}

// static
already_AddRefed<ImageData> ImageData::Constructor(
    const GlobalObject& aGlobal, const Uint8ClampedArray& aData,
    const uint32_t aWidth, const Optional<uint32_t>& aHeight,
    const ImageDataSettings& aSettings, ErrorResult& aRv) {
  Maybe<uint32_t> maybeLength = aData.ProcessData(
      [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&& nogc) {
        return Some(aData.Length());
      });
  uint32_t length = maybeLength.valueOr(0);
  if (length == 0 || length % 4) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }
  length /= 4;
  if (aWidth == 0) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }
  uint32_t height = length / aWidth;
  if (length != aWidth * height ||
      (aHeight.WasPassed() && aHeight.Value() != height)) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }

  PredefinedColorSpace colorSpace = GetColorSpaceFromSettings(aSettings);

  JS::Rooted<JSObject*> dataObj(aGlobal.Context(), aData.Obj());
  RefPtr<ImageData> imageData = new ImageData(aGlobal.GetAsSupports(), aWidth,
                                              height, colorSpace, dataObj);
  return imageData.forget();
}

void ImageData::HoldData() { mozilla::HoldJSObjects(this); }

void ImageData::DropData() {
  if (mData) {
    mData = nullptr;
    mozilla::DropJSObjects(this);
  }
}

JSObject* ImageData::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return ImageData_Binding::Wrap(aCx, this, aGivenProto);
}

// static
already_AddRefed<ImageData> ImageData::ReadStructuredClone(
    JSContext* aCx, nsIGlobalObject* aGlobal,
    JSStructuredCloneReader* aReader) {
  // Read the information out of the stream.

  PredefinedColorSpace colorSpace = PredefinedColorSpace::Srgb;
  uint32_t width, height;
  uint32_t cs, reserved;
  JS::Rooted<JS::Value> dataArray(aCx);

  if (!JS_ReadUint32Pair(aReader, &width, &height)) {
    return nullptr;
  }

  // As this might be serialized with older Firefox versions, be a little less
  // demanding on expecting the color space information to be present
  // as a newly added peek is used to avoid advancing the read offset
  uint32_t peekTag, peekData;
  if (!JS_PeekUint32Pair(aReader, &peekTag, &peekData)) {
    return nullptr;
  }

  const bool newFormat =
      peekTag <=
          uint32_t(MaxContiguousEnumValue<PredefinedColorSpace>::value) &&
      peekData == 0;

  if (newFormat) {
    if (!JS_ReadUint32Pair(aReader, &cs, &reserved) || reserved != 0) {
      return nullptr;
    }
    colorSpace = static_cast<PredefinedColorSpace>(cs);
  }

  if (!JS_ReadTypedArray(aReader, &dataArray)) {
    return nullptr;
  }

  JS::Rooted<JSObject*> dataObj(aCx, &dataArray.toObject());
  RootedSpiderMonkeyInterface<Uint8ClampedArray> data(aCx);
  if (!data.Init(dataObj)) {
    return nullptr;
  }

  Maybe<size_t> maybeLength = data.ProcessData(
      [&](const Span<uint8_t>& aData, JS::AutoCheckCannotGC&& nogc) {
        return Some(aData.Length());
      });
  if (maybeLength.isNothing()) {
    return nullptr;
  }

  CheckedInt<uint32_t> calculatedLength =
      CheckedInt<uint32_t>(width) * height * 4;
  if (!calculatedLength.isValid() ||
      size_t(calculatedLength.value()) != maybeLength.value()) {
    return nullptr;
  }

  RefPtr<ImageData> imageData =
      new ImageData(aGlobal, width, height, colorSpace, dataObj);
  return imageData.forget();
}

bool ImageData::WriteStructuredClone(JSContext* aCx,
                                     JSStructuredCloneWriter* aWriter) const {
  JS::Rooted<JS::Value> arrayValue(aCx, JS::ObjectValue(*GetDataObject()));
  if (!JS_WrapValue(aCx, &arrayValue)) {
    return false;
  }

  return JS_WriteUint32Pair(aWriter, Width(), Height()) &&
         JS_WriteUint32Pair(aWriter, static_cast<uint32_t>(mColorSpace), 0) &&
         JS_WriteTypedArray(aWriter, arrayValue);
}

}  // namespace mozilla::dom
