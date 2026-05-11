/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsICanvasRenderingContextInternal.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/CanvasUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/DrawTargetRecording.h"
#include "nsContentUtils.h"
#include "nsPIDOMWindow.h"
#include "nsRFPService.h"
#include "nsRefreshDriver.h"
#include "nsThreadUtils.h"

static mozilla::LazyLogModule gRenderingFingerprinterDetection(
    "FingerprinterDetection");

nsICanvasRenderingContextInternal::nsICanvasRenderingContextInternal() =
    default;

nsICanvasRenderingContextInternal::~nsICanvasRenderingContextInternal() =
    default;

mozilla::PresShell* nsICanvasRenderingContextInternal::GetPresShell() {
  if (mCanvasElement) {
    return mCanvasElement->OwnerDoc()->GetPresShell();
  }
  return nullptr;
}

nsIGlobalObject* nsICanvasRenderingContextInternal::GetParentObject() const {
  if (mCanvasElement) {
    return mCanvasElement->OwnerDoc()->GetScopeObject();
  }
  if (mOffscreenCanvas) {
    return mOffscreenCanvas->GetParentObject();
  }
  return nullptr;
}

class RecordCanvasUsageRunnable final
    : public mozilla::dom::WorkerMainThreadRunnable {
 public:
  RecordCanvasUsageRunnable(mozilla::dom::WorkerPrivate* aWorkerPrivate,
                            const mozilla::CanvasUsage& aUsage)
      : WorkerMainThreadRunnable(aWorkerPrivate,
                                 "RecordCanvasUsageRunnable"_ns),
        mUsage(aUsage) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
  }

 protected:
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool MainThreadRun() override {
    mozilla::AssertIsOnMainThread();
    RefPtr<mozilla::dom::Document> doc;
    if (!mWorkerRef) {
      MOZ_LOG(gRenderingFingerprinterDetection, mozilla::LogLevel::Error,
              ("RecordCanvasUsageRunnable::MainThreadRun - null mWorkerRef"));
      return false;
    }
    auto* priv = mWorkerRef->Private();
    if (!priv) {
      MOZ_LOG(
          gRenderingFingerprinterDetection, mozilla::LogLevel::Error,
          ("RecordCanvasUsageRunnable::MainThreadRun - null worker private"));
      return false;
    }
    doc = priv->GetDocument();
    if (!doc) {
      MOZ_LOG(gRenderingFingerprinterDetection, mozilla::LogLevel::Error,
              ("RecordCanvasUsageRunnable::MainThreadRun - null document"));
      return false;
    }
    doc->RecordCanvasUsage(mUsage);
    return true;
  }

 private:
  mozilla::CanvasUsage mUsage;
};

void nsICanvasRenderingContextInternal::RecordCanvasUsage(
    mozilla::CanvasExtractionAPI aAPI, mozilla::CSSIntSize size) const {
  mozilla::dom::CanvasContextType contextType;
  if (mCanvasElement) {
    contextType = mCanvasElement->GetCurrentContextType();
    auto usage =
        mozilla::CanvasUsage::CreateUsage(false, contextType, aAPI, size, this);
    mCanvasElement->OwnerDoc()->RecordCanvasUsage(usage);
  }
  if (mOffscreenCanvas) {
    contextType = mOffscreenCanvas->GetContextType();
    auto usage =
        mozilla::CanvasUsage::CreateUsage(true, contextType, aAPI, size, this);
    if (NS_IsMainThread()) {
      nsIGlobalObject* global = mOffscreenCanvas->GetRelevantGlobal();
      if (global) {
        if (nsPIDOMWindowInner* inner = global->GetAsInnerWindow()) {
          if (mozilla::dom::Document* doc = inner->GetExtantDoc()) {
            doc->RecordCanvasUsage(usage);
          }
        }
      }
    } else {
      mozilla::dom::WorkerPrivate* workerPrivate =
          mozilla::dom::GetCurrentThreadWorkerPrivate();
      if (workerPrivate) {
        RefPtr<RecordCanvasUsageRunnable> runnable =
            new RecordCanvasUsageRunnable(workerPrivate, usage);
        mozilla::ErrorResult rv;
        runnable->Dispatch(workerPrivate, mozilla::dom::WorkerStatus::Canceling,
                           rv);
        if (rv.Failed()) {
          rv.SuppressException();
          MOZ_LOG(gRenderingFingerprinterDetection, mozilla::LogLevel::Error,
                  ("RecordCanvasUsageRunnable dispatch failed"));
        }
      }
    }
  }
}

nsIPrincipal* nsICanvasRenderingContextInternal::PrincipalOrNull() const {
  if (mCanvasElement) {
    return mCanvasElement->NodePrincipal();
  }
  if (mOffscreenCanvas) {
    nsIGlobalObject* global = mOffscreenCanvas->GetParentObject();
    if (global) {
      return global->PrincipalOrNull();
    }
  }
  return nullptr;
}

nsICookieJarSettings* nsICanvasRenderingContextInternal::GetCookieJarSettings()
    const {
  if (mCanvasElement) {
    return mCanvasElement->OwnerDoc()->CookieJarSettings();
  }

  // If there is an offscreen canvas, attempt to retrieve its owner window
  // and return the cookieJarSettings for the window's document, if available.
  if (mOffscreenCanvas) {
    nsCOMPtr<nsPIDOMWindowInner> win =
        do_QueryInterface(mOffscreenCanvas->GetRelevantGlobal());

    if (win) {
      return win->GetExtantDoc()->CookieJarSettings();
    }

    // If the owner window cannot be retrieved, check if there is a current
    // worker and return its cookie jar settings if available.
    mozilla::dom::WorkerPrivate* worker =
        mozilla::dom::GetCurrentThreadWorkerPrivate();

    if (worker) {
      return worker->CookieJarSettings();
    }
  }

  return nullptr;
}

void nsICanvasRenderingContextInternal::RemovePostRefreshObserver() {
  if (mRefreshDriver) {
    mRefreshDriver->RemovePostRefreshObserver(this);
    mRefreshDriver = nullptr;
  }
}

void nsICanvasRenderingContextInternal::AddPostRefreshObserverIfNecessary() {
  if (!GetPresShell() || !GetPresShell()->GetPresContext() ||
      !GetPresShell()->GetPresContext()->RefreshDriver()) {
    return;
  }
  mRefreshDriver = GetPresShell()->GetPresContext()->RefreshDriver();
  mRefreshDriver->AddPostRefreshObserver(this);
}

void nsICanvasRenderingContextInternal::DoSecurityCheck(
    nsIPrincipal* aPrincipal, bool aForceWriteOnly, bool aCORSUsed) {
  if (mCanvasElement) {
    mozilla::CanvasUtils::DoDrawImageSecurityCheck(mCanvasElement, aPrincipal,
                                                   aForceWriteOnly, aCORSUsed);
  } else if (mOffscreenCanvas) {
    mozilla::CanvasUtils::DoDrawImageSecurityCheck(mOffscreenCanvas, aPrincipal,
                                                   aForceWriteOnly, aCORSUsed);
  }
}

bool nsICanvasRenderingContextInternal::ShouldResistFingerprinting(
    mozilla::RFPTarget aTarget) const {
  if (mCanvasElement) {
    return mCanvasElement->OwnerDoc()->ShouldResistFingerprinting(aTarget);
  }
  if (mOffscreenCanvas) {
    return mOffscreenCanvas->ShouldResistFingerprinting(aTarget);
  }
  // Last resort, just check the global preference
  return nsContentUtils::ShouldResistFingerprinting("Fallback", aTarget);
}

bool nsICanvasRenderingContextInternal::DispatchEvent(
    const nsAString& eventName, mozilla::CanBubble aCanBubble,
    mozilla::Cancelable aIsCancelable) const {
  bool useDefaultHandler = true;

  if (mCanvasElement) {
    nsContentUtils::DispatchTrustedEvent(mCanvasElement->OwnerDoc(),
                                         mCanvasElement, eventName, aCanBubble,
                                         aIsCancelable, &useDefaultHandler);
  } else if (mOffscreenCanvas) {
    // OffscreenCanvas case
    auto event = mozilla::MakeRefPtr<mozilla::dom::Event>(mOffscreenCanvas,
                                                          nullptr, nullptr);
    event->InitEvent(eventName, aCanBubble, aIsCancelable);
    event->SetTrusted(true);
    useDefaultHandler = mOffscreenCanvas->DispatchEvent(
        *event, mozilla::dom::CallerType::System, mozilla::IgnoreErrors());
  }
  return useDefaultHandler;
}

already_AddRefed<mozilla::gfx::SourceSurface>
nsICanvasRenderingContextInternal::GetOptimizedSnapshot(
    mozilla::gfx::DrawTarget* aTarget, gfxAlphaType* out_alphaType) {
  if (aTarget &&
      aTarget->GetBackendType() == mozilla::gfx::BackendType::RECORDING) {
    if (auto* actor = SupportsSnapshotExternalCanvas()) {
      // If this snapshot is for a recording target, then try to avoid reading
      // back any data by using SnapshotExternalCanvas instead. This avoids
      // having sync interactions between GPU and content process.
      if (RefPtr<mozilla::gfx::SourceSurface> surf =
              static_cast<mozilla::gfx::DrawTargetRecording*>(aTarget)
                  ->SnapshotExternalCanvas(this, actor)) {
        if (out_alphaType) {
          *out_alphaType =
              GetIsOpaque() ? gfxAlphaType::Opaque : gfxAlphaType::Premult;
        }
        return surf.forget();
      }
    }
  }

  return GetSurfaceSnapshot(out_alphaType);
}
