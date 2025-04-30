/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ImageTrack.h"
#include "ImageContainer.h"
#include "mozilla/dom/ImageTrackList.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/image/ImageUtils.h"

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ImageTrack, mParent, mTrackList,
                                      mDecodedFrames)
NS_IMPL_CYCLE_COLLECTING_ADDREF(ImageTrack)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ImageTrack)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ImageTrack)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ImageTrack::ImageTrack(ImageTrackList* aTrackList, int32_t aIndex,
                       bool aSelected, bool aAnimated, uint32_t aFrameCount,
                       bool aFrameCountComplete, float aRepetitionCount)
    : mParent(aTrackList->GetParentObject()),
      mTrackList(aTrackList),
      mFramesTimestamp(image::FrameTimeout::Zero()),
      mIndex(aIndex),
      mRepetitionCount(aRepetitionCount),
      mFrameCount(aFrameCount),
      mFrameCountComplete(aFrameCountComplete),
      mAnimated(aAnimated),
      mSelected(aSelected) {}

ImageTrack::~ImageTrack() = default;

void ImageTrack::Destroy() { mTrackList = nullptr; }

JSObject* ImageTrack::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();
  return ImageTrack_Binding::Wrap(aCx, this, aGivenProto);
}

void ImageTrack::SetSelected(bool aSelected) {
  if (mTrackList) {
    mTrackList->SetSelectedIndex(mIndex, aSelected);
  }
}

void ImageTrack::OnFrameCountSuccess(
    const image::DecodeFrameCountResult& aResult) {
  MOZ_ASSERT_IF(mFrameCountComplete, mFrameCount == aResult.mFrameCount);
  MOZ_ASSERT_IF(!aResult.mFinished, !mFrameCountComplete);
  MOZ_ASSERT_IF(!mAnimated, aResult.mFrameCount <= 1);
  MOZ_ASSERT(aResult.mFrameCount >= mFrameCount);
  mFrameCount = aResult.mFrameCount;
  mFrameCountComplete = aResult.mFinished;
}

void ImageTrack::OnDecodeFramesSuccess(
    const image::DecodeFramesResult& aResult) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug,
          ("ImageTrack %p OnDecodeFramesSuccess -- decoded %zu frames "
           "(finished %d), already had %zu frames (finished %d)",
           this, aResult.mFrames.Length(), aResult.mFinished,
           mDecodedFrames.Length(), mDecodedFramesComplete));

  mDecodedFramesComplete = aResult.mFinished;
  mDecodedFrames.SetCapacity(mDecodedFrames.Length() +
                             aResult.mFrames.Length());

  for (const auto& f : aResult.mFrames) {
    VideoColorSpaceInternal colorSpace;
    gfx::IntSize size = f.mSurface->GetSize();
    gfx::IntRect rect(gfx::IntPoint(0, 0), size);

    Maybe<VideoPixelFormat> format =
        SurfaceFormatToVideoPixelFormat(f.mSurface->GetFormat());
    MOZ_ASSERT(format, "Unexpected format for image!");

    Maybe<uint64_t> duration;
    if (f.mTimeout != image::FrameTimeout::Forever()) {
      duration =
          Some(static_cast<uint64_t>(f.mTimeout.AsMilliseconds()) * 1000);
    }

    uint64_t timestamp = UINT64_MAX;
    if (mFramesTimestamp != image::FrameTimeout::Forever()) {
      timestamp =
          static_cast<uint64_t>(mFramesTimestamp.AsMilliseconds()) * 1000;
    }

    mFramesTimestamp += f.mTimeout;

    auto image = MakeRefPtr<layers::SourceSurfaceImage>(size, f.mSurface);
    auto frame = MakeRefPtr<VideoFrame>(mParent, image, format, size, rect,
                                        size, duration, timestamp, colorSpace);
    mDecodedFrames.AppendElement(std::move(frame));
  }
}

}  // namespace mozilla::dom
