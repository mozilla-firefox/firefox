/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HWInference.h"

#include "HWInferenceParent.h"
#include "HWInferenceProcess.h"
#include "mozilla/dom/HWInferenceBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

using hwinference::DeviceDescriptor;
using hwinference::HWInferenceParent;
using hwinference::HWInferenceProcess;
using ListDevicesPromise = HWInferenceParent::ListDevicesPromise;

static HWInferenceDevice ToJSDevice(const DeviceDescriptor& aDescriptor) {
  HWInferenceDevice device;
  device.mId = aDescriptor.id();
  device.mDescription = aDescriptor.description();
  device.mKind = aDescriptor.kind();
  for (HWInferenceBackend backend : aDescriptor.backends()) {
    MOZ_RELEASE_ASSERT(device.mBackends.AppendElement(backend, fallible));
  }
  return device;
}

/* static */
already_AddRefed<Promise> HWInference::ListDevices(const GlobalObject& aGlobal,
                                                   ErrorResult& aRv) {
  AssertIsOnMainThread();
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (!promise) {
    return nullptr;
  }

  RefPtr<mozilla::ipc::UtilityProcessKeepAlive> keepAlive =
      HWInferenceProcess::Browser().Acquire();
  if (!keepAlive) {
    promise->MaybeRejectWithOperationError(
        "HWInference.listDevices: failed to start the inference process");
    return promise.forget();
  }
  RefPtr<HWInferenceParent> process = HWInferenceProcess::Browser().Actor();
  // The keep-alive lives in the callbacks: the process may go as soon as the
  // reply is in.
  process->WhenReady()
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [process]() { return process->SendListDevices(); },
          []() {
            return ListDevicesPromise::CreateAndReject(
                mozilla::ipc::ResponseRejectReason::SendError, __func__);
          })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [promise, keepAlive](const nsTArray<DeviceDescriptor>& aDevices) {
            nsTArray<HWInferenceDevice> devices;
            for (const auto& descriptor : aDevices) {
              devices.AppendElement(ToJSDevice(descriptor));
            }
            promise->MaybeResolve(devices);
          },
          [promise, keepAlive](mozilla::ipc::ResponseRejectReason) {
            promise->MaybeRejectWithAbortError(
                "HWInference.listDevices: the inference process went away");
          });
  return promise.forget();
}

}  // namespace mozilla::dom
