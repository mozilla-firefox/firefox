/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TransformerCallbackHelpers.h"

#include "StreamUtils.h"
#include "jsapi.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TransformStreamDefaultController.h"

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION(TransformerAlgorithmsBase)
NS_IMPL_CYCLE_COLLECTING_ADDREF(TransformerAlgorithmsBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TransformerAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TransformerAlgorithmsBase)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_INHERITED_WITH_JS_MEMBERS(
    TransformerAlgorithms, TransformerAlgorithmsBase,
    (mGlobal, mTransformCallback, mFlushCallback), (mTransformer))
NS_IMPL_ADDREF_INHERITED(TransformerAlgorithms, TransformerAlgorithmsBase)
NS_IMPL_RELEASE_INHERITED(TransformerAlgorithms, TransformerAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TransformerAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(TransformerAlgorithmsBase)

// https://streams.spec.whatwg.org/#set-up-transform-stream-default-controller-from-transformer
already_AddRefed<Promise> TransformerAlgorithms::TransformCallback(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  if (!mTransformCallback) {
    // Step 2.1. Let result be
    // TransformStreamDefaultControllerEnqueue(controller, chunk).
    aController.Enqueue(aCx, aChunk, aRv);

    // Step 2.2. If result is an abrupt completion, return a promise rejected
    // with result.[[Value]].
    if (aRv.MaybeSetPendingException(aCx)) {
      JS::Rooted<JS::Value> error(aCx);
      if (!JS_GetPendingException(aCx, &error)) {
        // Uncatchable exception; we should mark aRv and return.
        aRv.StealExceptionFromJSContext(aCx);
        return nullptr;
      }
      JS_ClearPendingException(aCx);

      return Promise::CreateRejected(aController.GetParentObject(), error, aRv);
    }

    // Step 2.3. Otherwise, return a promise resolved with undefined.
    return Promise::CreateResolvedWithUndefined(aController.GetParentObject(),
                                                aRv);
  }
  // Step 4. If transformerDict["transform"] exists, set transformAlgorithm to
  // an algorithm which takes an argument chunk and returns the result of
  // invoking transformerDict["transform"] with argument list « chunk,
  // controller » and callback this value transformer.
  JS::Rooted<JSObject*> thisObj(aCx, mTransformer);
  return MOZ_KnownLive(mTransformCallback)
      ->Call(thisObj, aChunk, aController, aRv,
             "TransformStreamDefaultController.[[transformAlgorithm]]",
             CallbackObject::eRethrowExceptions);
}

// https://streams.spec.whatwg.org/#set-up-transform-stream-default-controller-from-transformer
already_AddRefed<Promise> TransformerAlgorithms::FlushCallback(
    JSContext* aCx, TransformStreamDefaultController& aController,
    ErrorResult& aRv) {
  if (!mFlushCallback) {
    // Step 3. Let flushAlgorithm be an algorithm which returns a promise
    // resolved with undefined.
    return Promise::CreateResolvedWithUndefined(aController.GetParentObject(),
                                                aRv);
  }
  // Step 5. If transformerDict["flush"] exists, set flushAlgorithm to an
  // algorithm which returns the result of invoking transformerDict["flush"]
  // with argument list « controller » and callback this value transformer.
  JS::Rooted<JSObject*> thisObj(aCx, mTransformer);
  return MOZ_KnownLive(mFlushCallback)
      ->Call(thisObj, aController, aRv,
             "TransformStreamDefaultController.[[flushAlgorithm]]",
             CallbackObject::eRethrowExceptions);
}

already_AddRefed<Promise> TransformerAlgorithmsWrapper::TransformCallback(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  // We are in the writer stream's realm, but we want to call the transform
  // algorithm in the transform stream's realm to make sure the generated object
  // are from the right realm.
  nsCOMPtr<nsIGlobalObject> global = aController.GetParentObject();
  JSAutoRealm realm(aCx, global->GetGlobalJSObject());

  JS::Rooted<JS::Value> chunk(aCx, aChunk);
  if (!JS_WrapValue(aCx, &chunk)) {
    aRv.MightThrowJSException();
    aRv.StealExceptionFromJSContext(aCx);
    return nullptr;
  }

  return PromisifyAlgorithm(
      global,
      [this, aCx, &chunk, &aController](ErrorResult& aRv)
          MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION {
            return TransformCallbackImpl(aCx, chunk, aController, aRv);
          },
      aRv);
}

already_AddRefed<Promise> TransformerAlgorithmsWrapper::FlushCallback(
    JSContext* aCx, TransformStreamDefaultController& aController,
    ErrorResult& aRv) {
  // Same potential realm difference, see also TransformCallback above
  nsCOMPtr<nsIGlobalObject> global = aController.GetParentObject();
  JSAutoRealm realm(aCx, global->GetGlobalJSObject());

  return PromisifyAlgorithm(
      global,
      [this, aCx, &aController](ErrorResult& aRv)
          MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION {
            return FlushCallbackImpl(aCx, aController, aRv);
          },
      aRv);
}
