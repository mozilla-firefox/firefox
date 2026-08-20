/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PictureInPictureWindow.h"

#include "PictureInPictureService.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "mozilla/dom/PictureInPictureWindowBinding.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(PictureInPictureWindow, DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(PictureInPictureWindow, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(PictureInPictureWindow, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PictureInPictureWindow)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

/* static */
bool PictureInPictureWindow::PictureInPictureEnabled() {
  return StaticPrefs::dom_media_pip_enabled() &&
         PictureInPictureService::IsSupported();
}

PictureInPictureWindow::PictureInPictureWindow(nsPIDOMWindowInner* aWindow,
                                               HTMLVideoElement* aVideoElement)
    : DOMEventTargetHelper(aWindow), mAssociatedVideoElement(aVideoElement) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aVideoElement);
}

PictureInPictureWindow::~PictureInPictureWindow() = default;

JSObject* PictureInPictureWindow::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return PictureInPictureWindow_Binding::Wrap(aCx, this, aGivenProto);
}

int32_t PictureInPictureWindow::Width() const {
  // The width attribute MUST return the width in CSS pixels of the
  // Picture-in-Picture window associated with pictureInPictureElement if the
  // state is opened. Otherwise, it MUST return 0.
  if (!IsStateOpened()) {
    return 0;
  }
  RefPtr<HTMLVideoElement> videoElement = mAssociatedVideoElement.get();
  if (videoElement && videoElement->OwnerDoc()->ShouldResistFingerprinting(
                          RFPTarget::ScreenRect)) {
    return VideoSizeForRFP().width;
  }
  return mWidth;
}

int32_t PictureInPictureWindow::Height() const {
  // The height attribute MUST return the height in CSS pixels of the
  // Picture-in-Picture window associated with pictureInPictureElement if the
  // state is opened. Otherwise, it MUST return 0.
  if (!IsStateOpened()) {
    return 0;
  }
  RefPtr<HTMLVideoElement> videoElement = mAssociatedVideoElement.get();
  if (videoElement && videoElement->OwnerDoc()->ShouldResistFingerprinting(
                          RFPTarget::ScreenRect)) {
    return VideoSizeForRFP().height;
  }
  return mHeight;
}

gfx::IntSize PictureInPictureWindow::VideoSizeForRFP() const {
  RefPtr<HTMLVideoElement> videoElement = mAssociatedVideoElement.get();
  if (!videoElement) {
    return {0, 0};
  }

  // From PictureInPicture.sys.mjs: "The Picture in Picture window will be a
  // maximum of a quarter of the screen height, and a third of the screen
  // width.".
  // Pretend we are maximizing the video in a 1920x1080 display.
  const uint32_t maxWidth = 1920 / 3;
  const uint32_t maxHeight = 1080 / 4;
  uint32_t width = videoElement->VideoWidth();
  uint32_t height = videoElement->VideoHeight();
  if ((height > maxHeight || width > maxWidth) && height > 0) {
    double aspectRatio = static_cast<double>(width) / height;
    if (width >= height) {
      width = maxWidth;
      height = static_cast<uint32_t>(round(maxWidth / aspectRatio));
    } else {
      height = maxHeight;
      width = static_cast<uint32_t>(round(maxHeight * aspectRatio));
    }
  }
  return {width, height};
}

void PictureInPictureWindow::NotifyDimensionsChanged(int32_t aWidth,
                                                     int32_t aHeight) {
  mWidth = aWidth;
  mHeight = aHeight;

  RefPtr<HTMLVideoElement> videoElement = mAssociatedVideoElement.get();
  if (videoElement && videoElement->OwnerDoc()->ShouldResistFingerprinting(
                          RFPTarget::ScreenRect)) {
    // With RFP, we spoof the window size to a fixed size that depends on the
    // video, therefore it does not make sense to trigger a resize event.
    return;
  }

  // When the size of a Picture-in-Picture window pipWindow changes,
  // the user agent MUST queue a task to fire an event named resize at
  // pipWindow.
  NS_DispatchToMainThread(
      NS_NewRunnableFunction(__func__, [self = RefPtr{this}]() {
        RefPtr<Event> event = NS_NewDOMEvent(self, nullptr, nullptr);
        event->InitEvent(u"resize"_ns, false, false);
        event->SetTrusted(true);
        self->DispatchEvent(*event);
      }));
}

void PictureInPictureWindow::Close() {
  if (!mOpened) {
    return;
  }
  mOpened = false;

  // mAssociatedVideoElement is a WeakPtr; the video may already have been
  // collected if its document was orphaned and released. If so, there's
  // no PiP state to clear and no listener to fire the event at.
  RefPtr<HTMLVideoElement> videoElement = mAssociatedVideoElement.get();
  mAssociatedVideoElement = nullptr;
  if (!videoElement) {
    return;
  }

  videoElement->RemoveStates(ElementState::PICTURE_IN_PICTURE);

  PictureInPictureEventInit eventInit;
  eventInit.mBubbles = true;
  eventInit.mCancelable = false;
  eventInit.mPictureInPictureWindow = this;

  RefPtr<PictureInPictureEvent> pipEvent = PictureInPictureEvent::Constructor(
      this, u"leavepictureinpicture"_ns, eventInit);
  pipEvent->SetTrusted(true);

  // https://w3c.github.io/picture-in-picture/#exit-pip
  // 3. Queue a task to fire an event named leavepictureinpicture using
  // PictureInPictureEvent at the video with its bubbles attribute
  // initialized to true and its pictureInPictureWindow attribute
  // initialized to Picture-in-Picture window associated with
  // pictureInPictureElement.
  MakeRefPtr<AsyncEventDispatcher>(videoElement, pipEvent.forget(),
                                   ChromeOnlyDispatch::eNo)
      ->PostDOMEvent();
}

}  // namespace mozilla::dom
