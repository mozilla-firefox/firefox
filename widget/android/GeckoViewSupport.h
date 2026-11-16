/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_GeckoViewSupport_h
#define mozilla_widget_GeckoViewSupport_h

#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/java/GeckoResultWrappers.h"
#include "mozilla/java/GeckoSessionNatives.h"
#include "mozilla/java/WebResponseWrappers.h"
#include "mozilla/widget/WindowEvent.h"

class nsPIDOMWindowOuter;
class nsWindow;

namespace mozilla {
namespace widget {

class GeckoViewSupport final
    : public java::GeckoSession::Window::Natives<GeckoViewSupport> {
  RefPtr<nsWindow> mWindow;

  // We hold a WeakRef because we want to allow the
  // GeckoSession.Window to be garbage collected.
  // Callers need to create a LocalRef from this
  // before calling methods.
  java::GeckoSession::Window::WeakRef mGeckoViewWindow;

 public:
  typedef java::GeckoSession::Window::Natives<GeckoViewSupport> Base;

  template <typename Functor>
  static void OnNativeCall(Functor&& aCall) {
    NS_DispatchToMainThread(
        new WindowEvent<Functor>(std::forward<Functor>(aCall)));
  }

  GeckoViewSupport(nsWindow* aWindow,
                   const java::GeckoSession::Window::LocalRef& aInstance,
                   nsPIDOMWindowOuter* aDOMWindow);

  ~GeckoViewSupport();

  nsWindow* GetNsWindow() const { return mWindow; }

  using Base::DisposeNative;

  /**
   * GeckoView methods
   */
 private:
  nsCOMPtr<nsPIDOMWindowOuter> mDOMWindow;
  bool mIsReady{false};
  RefPtr<dom::CanonicalBrowsingContext> GetContentCanonicalBrowsingContext();
  MOZ_CAN_RUN_SCRIPT void CreatePdf(
      jni::LocalRef<mozilla::java::GeckoResult> aGeckoResult,
      RefPtr<dom::CanonicalBrowsingContext> aCbc);

 public:
  // Create and attach a window.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void Open(
      const jni::Class::LocalRef& aCls,
      java::GeckoSession::Window::Param aWindow, jni::Object::Param aQueue,
      jni::Object::Param aCompositor, jni::Object::Param aDispatcher,
      jni::Object::Param aSessionAccessibility, jni::Object::Param aInitData,
      jni::String::Param aId, jni::String::Param aChromeURI, bool aPrivateMode);

  // Close and destroy the nsWindow.
  MOZ_CAN_RUN_SCRIPT void Close();

  // Transfer this nsWindow to new GeckoSession objects.
  void Transfer(const java::GeckoSession::Window::LocalRef& inst,
                jni::Object::Param aQueue, jni::Object::Param aCompositor,
                jni::Object::Param aDispatcher,
                jni::Object::Param aSessionAccessibility,
                jni::Object::Param aInitData);

  void AttachEditable(const java::GeckoSession::Window::LocalRef& inst,
                      jni::Object::Param aEditableParent);

  void AttachAccessibility(const java::GeckoSession::Window::LocalRef& inst,
                           jni::Object::Param aSessionAccessibility);

  void OnReady(jni::Object::Param aQueue = nullptr);

  auto OnLoadRequest(mozilla::jni::String::Param aUri, int32_t aWindowType,
                     int32_t aFlags, mozilla::jni::String::Param aTriggeringUri,
                     bool aHasUserGesture, bool aIsTopLevel) const
      -> java::GeckoResult::LocalRef;

  void OnShowDynamicToolbar() const;
  void OnHideDynamicToolbar() const;

  void PassExternalResponse(java::WebResponse::Param aResponse);

  void AttachMediaSessionController(
      const java::GeckoSession::Window::LocalRef& inst,
      jni::Object::Param aController, const int64_t aId);

  void DetachMediaSessionController(
      const java::GeckoSession::Window::LocalRef& inst,
      jni::Object::Param aController);

  void OnWeakNonIntrusiveDetach(already_AddRefed<Runnable> aDisposer) {
    RefPtr<Runnable> disposer(aDisposer);
    disposer->Run();
  }

  MOZ_CAN_RUN_SCRIPT void PrintToPdf(
      const java::GeckoSession::Window::LocalRef& inst,
      jni::Object::Param aStream, int64_t aBcId);

  MOZ_CAN_RUN_SCRIPT void PrintToPdf(
      const java::GeckoSession::Window::LocalRef& inst,
      jni::Object::Param aStream);

  void RequestFullScreenshot(const java::GeckoSession::Window::LocalRef& inst,
                             jni::Object::Param aResult,
                             jni::Object::Param aTarget, int32_t aX, int32_t aY,
                             int32_t aWidth, int32_t aHeight,
                             float aRenderingScale);

  // Capture a content region (CSS pixels, scaled by aScale) directly into an
  // Android Surface. Used by the scroll capture (long screenshot) path.
  void RequestRegionToSurface(const java::GeckoSession::Window::LocalRef& inst,
                              jni::Object::Param aResult,
                              jni::Object::Param aSurface, int32_t aX,
                              int32_t aY, int32_t aWidth, int32_t aHeight,
                              float aScale);

  // Query content for the top document's scroll size (in CSS pixels)
  // and device pixel ratio (for calculating the size in device pixels)
  void RequestContentMetrics(const java::GeckoSession::Window::LocalRef& inst,
                             jni::Object::Param aResult,
                             jni::Object::Param aMetrics);

  // Record the outcome of a scroll capture image request as the
  // geckoview.scroll_capture_tile_result labeled counter. aOutcome is one of
  // the values defined in GeckoViewScrollCaptureCallback.
  void RecordScrollCaptureTileResult(
      const java::GeckoSession::Window::LocalRef& inst, int32_t aOutcome);

  void RecordScrollCaptureOutcome(
      const java::GeckoSession::Window::LocalRef& inst, int32_t aOutcome);

  // See nsIHapticFeedback::HapticFeedbackType for available effects.
  void PerformHapticFeedback(int32_t aEffect);
};

}  // namespace widget
}  // namespace mozilla

#endif  // mozilla_widget_GeckoViewSupport_h
