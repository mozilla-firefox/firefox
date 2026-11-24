/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_GTEST_MEDIASINKTESTUTILS_H_
#define DOM_MEDIA_GTEST_MEDIASINKTESTUTILS_H_

#include <utility>

#include "AudioSink.h"
#include "AudioSinkWrapper.h"
#include "ImageContainer.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "MediaQueue.h"
#include "VideoFrameContainer.h"
#include "gtest/gtest.h"
#include "nsThreadUtils.h"

// A gtest body does not run inside a processed event, so
// AbstractThread::IsTailDispatcherAvailable() is false there and direct-task
// dispatch silently degrades to a plain queue dispatch, which is not how
// production orders the same work. Re-enter the function from a runnable so the
// tail dispatcher exists.
#define ENSURE_TAIL_DISPATCH(f)                                            \
  do {                                                                     \
    if (auto* t = AbstractThread::GetCurrent();                            \
        t && !t->IsTailDispatcherAvailable()) {                            \
      ASSERT_STREQ(__func__, #f)                                           \
          << "Must only use ENSURE_TAIL_DISPATCH on the current function"; \
      MOZ_ALWAYS_SUCCEEDS(                                                 \
          t->Dispatch(NS_NewRunnableFunction(__func__, [&] { f(); })));    \
      NS_ProcessPendingEvents(nullptr);                                    \
      return;                                                              \
    }                                                                      \
  } while (false)

#define ENSURE_TEST_TAIL_DISPATCH() ENSURE_TAIL_DISPATCH(TestBody)

namespace mozilla {

// Build an AudioSinkWrapper whose sink creator draws from aQueue/aInfo. The
// references must outlive the returned wrapper (callers keep them as locals or
// fixture members that do).
inline RefPtr<AudioSinkWrapper> MakeAudioSinkWrapper(
    MediaQueue<AudioData>& aQueue, MediaInfo& aInfo, double aVolume) {
  auto creator = [&aQueue, &aInfo]() {
    return UniquePtr<AudioSink>{new AudioSink(AbstractThread::GetCurrent(),
                                              aQueue, aInfo.mAudio,
                                              /*resistFingerprinting*/ false)};
  };
  return new AudioSinkWrapper(
      AbstractThread::GetCurrent(), aQueue, std::move(creator), aVolume,
      /*playbackRate*/ 1.0, /*preservesPitch*/ true, /*sinkDevice*/ nullptr);
}

inline already_AddRefed<VideoFrameContainer> MakeSinkTestVideoFrameContainer(
    MediaDecoderOwner* aOwner) {
  RefPtr container = new VideoFrameContainer(
      aOwner, MakeAndAddRef<layers::ImageContainer>(
                  layers::ImageUsageType::VideoFrameContainer,
#ifdef MOZ_WIDGET_ANDROID
                  // TODO(bug 1922144): Android should use ASYNCHRONOUS too.
                  layers::ImageContainer::SYNCHRONOUS
#else
                  layers::ImageContainer::ASYNCHRONOUS
#endif
                  ));
  return container.forget();
}

// A 1x1 image, enough for sink tests that identify frames by their timestamps
// rather than by their contents.
inline RefPtr<layers::Image> MakeSinkTest1x1Image(
    layers::ImageContainer* aContainer) {
  RefPtr image = aContainer->CreatePlanarYCbCrImage();
  static uint8_t pixel[] = {0x00};
  layers::PlanarYCbCrData data;
  data.mYChannel = data.mCbChannel = data.mCrChannel = pixel;
  data.mYStride = data.mCbCrStride = 1;
  data.mPictureRect = gfx::IntRect(0, 0, 1, 1);
  image->CopyData(data);
  return image;
}

}  // namespace mozilla

#endif  // DOM_MEDIA_GTEST_MEDIASINKTESTUTILS_H_
