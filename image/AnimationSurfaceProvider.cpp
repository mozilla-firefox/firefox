/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AnimationSurfaceProvider.h"

#include "mozilla/StaticPrefs_image.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "nsProxyRelease.h"

#include "DecodePool.h"
#include "Decoder.h"

#ifdef MOZ_JXL
#  include "decoders/nsJXLDecoder.h"
#endif

using namespace mozilla::gfx;
using namespace mozilla::layers;

namespace mozilla {
namespace image {

AnimationSurfaceProvider::AnimationSurfaceProvider(
    NotNull<RasterImage*> aImage, const SurfaceKey& aSurfaceKey,
    NotNull<Decoder*> aDecoder, size_t aCurrentFrame)
    : ISurfaceProvider(ImageKey(aImage.get()), aSurfaceKey,
                       AvailabilityState::StartAsPlaceholder()),
      mImage(aImage.get()),
      mDecodingMutex("AnimationSurfaceProvider::mDecoder"),
      mDecoder(aDecoder.get()),
      mFramesMutex("AnimationSurfaceProvider::mFrames"),
      mCompositedFrameRequested(false),
      mSharedAnimation(MakeRefPtr<SharedSurfacesAnimation>()) {
  MOZ_ASSERT(!mDecoder->IsMetadataDecode(),
             "Use MetadataDecodingTask for metadata decodes");
  MOZ_ASSERT(!mDecoder->IsFirstFrameDecode(),
             "Use DecodedSurfaceProvider for single-frame image decodes");

  // Calculate how many frames we need to decode in this animation before we
  // enter decode-on-demand mode.
  IntSize frameSize = aSurfaceKey.Size();
  size_t threshold =
      (size_t(StaticPrefs::image_animated_decode_on_demand_threshold_kb()) *
       1024) /
      (sizeof(uint32_t) * frameSize.width * frameSize.height);
  size_t batch = StaticPrefs::image_animated_decode_on_demand_batch_size();

  mFrames.reset(
      new AnimationFrameRetainedBuffer(threshold, batch, aCurrentFrame));
}

AnimationSurfaceProvider::~AnimationSurfaceProvider() {
  DropImageReference();

  mSharedAnimation->Destroy();
  if (mDecoder) {
    mDecoder->SetFrameRecycler(nullptr);
  }
}

void AnimationSurfaceProvider::DropImageReference() {
  if (!mImage) {
    return;  // Nothing to do.
  }

  // RasterImage objects need to be destroyed on the main thread.
  SurfaceCache::ReleaseImageOnMainThread(mImage.forget());
}

void AnimationSurfaceProvider::Reset() {
  // We want to go back to the beginning.
  bool mayDiscard;
  bool restartDecoder = false;

  {
    MutexAutoLock lock(mFramesMutex);

    // If we have not crossed the threshold, we know we haven't discarded any
    // frames, and thus we know it is safe move our display index back to the
    // very beginning. It would be cleaner to let the frame buffer make this
    // decision inside the AnimationFrameBuffer::Reset method, but if we have
    // crossed the threshold, we need to hold onto the decoding mutex too. We
    // should avoid blocking the main thread on the decoder threads.
    mayDiscard = mFrames->MayDiscard();
    if (!mayDiscard) {
      restartDecoder = mFrames->Reset();
    }
  }

  if (mayDiscard) {
    // We are over the threshold and have started discarding old frames. In
    // that case we need to seize the decoding mutex. Thankfully we know that
    // we are in the process of decoding at most the batch size frames, so
    // this should not take too long to acquire.
    MutexAutoLock lock(mDecodingMutex);

    // We may have hit an error while redecoding. Because FrameAnimator is
    // tightly coupled to our own state, that means we would need to go through
    // some heroics to resume animating in those cases. The typical reason for
    // a redecode to fail is out of memory, and recycling should prevent most of
    // those errors. When image.animated.generate-full-frames has shipped
    // enabled on a release or two, we can simply remove the old FrameAnimator
    // blending code and simplify this quite a bit -- just always pop the next
    // full frame and timeout off the stack.
    if (mDecoder) {
#ifdef MOZ_JXL
      Vector<JxlFrameInfo> scannedFrames;
      bool isJXL = mDecoder->GetType() == DecoderType::JXL;
      if (isJXL) {
        auto* jxlDecoder = static_cast<nsJXLDecoder*>(mDecoder.get());
        // OOM here is acceptable: the decoder will re-scan from scratch.
        (void)scannedFrames.appendAll(jxlDecoder->ScannedFrames());
      }
#endif

      mDecoder = DecoderFactory::CloneAnimationDecoder(mDecoder);
      MOZ_ASSERT(mDecoder);

#ifdef MOZ_JXL
      if (isJXL && !scannedFrames.empty()) {
        auto* jxlDecoder = static_cast<nsJXLDecoder*>(mDecoder.get());
        jxlDecoder->TransferScannedFrames(std::move(scannedFrames));
      }
#endif

      MutexAutoLock lock2(mFramesMutex);
      restartDecoder = mFrames->Reset();
    } else {
      MOZ_ASSERT(mFrames->HasRedecodeError());
    }
  }

  if (restartDecoder) {
    DecodePool::Singleton()->AsyncRun(this);
  }
}

void AnimationSurfaceProvider::Advance(size_t aFrame) {
  bool restartDecoder;

  RefPtr<SourceSurface> surface;
  IntRect dirtyRect;
  {
    // Typical advancement of a frame.
    MutexAutoLock lock(mFramesMutex);
    restartDecoder = mFrames->AdvanceTo(aFrame);

    imgFrame* frame = mFrames->Get(aFrame, /* aForDisplay */ true);
    MOZ_ASSERT(frame);
    if (aFrame != 0) {
      dirtyRect = frame->GetDirtyRect();
    } else {
      MOZ_ASSERT(mFrames->SizeKnown());
      dirtyRect = mFrames->FirstFrameRefreshArea();
    }
    surface = frame->GetSourceSurface();
    MOZ_ASSERT(surface);
  }

  if (restartDecoder) {
    DecodePool::Singleton()->AsyncRun(this);
  }

  mCompositedFrameRequested = false;
  auto* sharedSurface = static_cast<SourceSurfaceSharedData*>(surface.get());
  mSharedAnimation->SetCurrentFrame(sharedSurface, dirtyRect);
}

DrawableFrameRef AnimationSurfaceProvider::DrawableRef(size_t aFrame) {
  MutexAutoLock lock(mFramesMutex);

  if (Availability().IsPlaceholder()) {
    MOZ_ASSERT_UNREACHABLE("Calling DrawableRef() on a placeholder");
    return DrawableFrameRef();
  }

  imgFrame* frame = mFrames->Get(aFrame, /* aForDisplay */ true);
  if (!frame) {
    return DrawableFrameRef();
  }

  return frame->DrawableRef();
}

already_AddRefed<imgFrame> AnimationSurfaceProvider::GetFrame(size_t aFrame) {
  MutexAutoLock lock(mFramesMutex);

  if (Availability().IsPlaceholder()) {
    MOZ_ASSERT_UNREACHABLE("Calling GetFrame() on a placeholder");
    return nullptr;
  }

  RefPtr<imgFrame> frame = mFrames->Get(aFrame, /* aForDisplay */ false);
  MOZ_ASSERT_IF(frame, frame->IsFinished());
  return frame.forget();
}

bool AnimationSurfaceProvider::IsFinished() const {
  MutexAutoLock lock(mFramesMutex);

  if (Availability().IsPlaceholder()) {
    MOZ_ASSERT_UNREACHABLE("Calling IsFinished() on a placeholder");
    return false;
  }

  return mFrames->IsFirstFrameFinished();
}

bool AnimationSurfaceProvider::IsFullyDecoded() const {
  MutexAutoLock lock(mFramesMutex);
  return mFrames->SizeKnown() && !mFrames->MayDiscard();
}

size_t AnimationSurfaceProvider::LogicalSizeInBytes() const {
  // When decoding animated images, we need at most three live surfaces: the
  // composited surface, the previous composited surface for
  // DisposalMethod::RESTORE_PREVIOUS, and the surface we're currently decoding
  // into. The composited surfaces are always BGRA. Although the surface we're
  // decoding into may be paletted, and may be smaller than the real size of the
  // image, we assume the worst case here.
  // XXX(seth): Note that this is actually not accurate yet; we're storing the
  // full sequence of frames, not just the three live surfaces mentioned above.
  // Unfortunately there's no way to know in advance how many frames an
  // animation has, so we really can't do better here. This will become correct
  // once bug 1289954 is complete.
  IntSize size = GetSurfaceKey().Size();
  return 3 * size.width * size.height * sizeof(uint32_t);
}

void AnimationSurfaceProvider::AddSizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf, const AddSizeOfCb& aCallback) {
  // Note that the surface cache lock is already held here, and then we acquire
  // mFramesMutex. For this method, this ordering is unavoidable, which means
  // that we must be careful to always use the same ordering elsewhere.
  MutexAutoLock lock(mFramesMutex);
  mFrames->AddSizeOfExcludingThis(aMallocSizeOf, aCallback);
}

void AnimationSurfaceProvider::Run() {
  MutexAutoLock lock(mDecodingMutex);

  if (!mDecoder) {
    MOZ_ASSERT_UNREACHABLE("Running after decoding finished?");
    return;
  }

  while (true) {
    // Run the decoder.
    LexerResult result = mDecoder->Decode(WrapNotNull(this));

    if (result.is<TerminalState>()) {
      // We may have a new frame now, but it's not guaranteed - a decoding
      // failure or truncated data may mean that no new frame got produced.
      // Since we're not sure, rather than call CheckForNewFrameAtYield() here
      // we call CheckForNewFrameAtTerminalState(), which handles both of these
      // possibilities.
      bool continueDecoding = CheckForNewFrameAtTerminalState();
      FinishDecoding();

      // Even if it is the last frame, we may not have enough frames buffered
      // ahead of the current. If we are shutting down, we want to ensure we
      // release the thread as soon as possible. The animation may advance even
      // during shutdown, which keeps us decoding, and thus blocking the decode
      // pool during teardown.
      if (!mDecoder || !continueDecoding || DecodePool::IsShuttingDown()) {
        return;
      }

      // Restart from the very beginning because the decoder was recreated.
      continue;
    }

    // If there is output available we want to change the entry in the surface
    // cache from a placeholder to an actual surface now before NotifyProgress
    // call below so that when consumers get the frame complete notification
    // from the NotifyProgress they can actually get a surface from the surface
    // cache.
    bool checkForNewFrameAtYieldResult = false;
    if (result == LexerResult(Yield::OUTPUT_AVAILABLE)) {
      checkForNewFrameAtYieldResult = CheckForNewFrameAtYield();
    }

    // Notify for the progress we've made so far.
    if (mImage && mDecoder->HasProgress()) {
      NotifyProgress(WrapNotNull(mImage), WrapNotNull(mDecoder));
    }

    if (result == LexerResult(Yield::NEED_MORE_DATA)) {
      // We can't make any more progress right now. The decoder itself will
      // ensure that we get reenqueued when more data is available; just return
      // for now.
      return;
    }

    // There's new output available - a new frame! Grab it. If we don't need any
    // more for the moment we can break out of the loop. If we are shutting
    // down, we want to ensure we release the thread as soon as possible. The
    // animation may advance even during shutdown, which keeps us decoding, and
    // thus blocking the decode pool during teardown.
    MOZ_ASSERT(result == LexerResult(Yield::OUTPUT_AVAILABLE));
    if (!checkForNewFrameAtYieldResult || DecodePool::IsShuttingDown()) {
      return;
    }
  }
}

bool AnimationSurfaceProvider::CheckForNewFrameAtYield() {
  mDecodingMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(mDecoder);

  bool justGotFirstFrame = false;
  bool continueDecoding = false;

  {
    MutexAutoLock lock(mFramesMutex);

    // Try to get the new frame from the decoder.
    RefPtr<imgFrame> frame = mDecoder->GetCurrentFrame();
    MOZ_ASSERT(mDecoder->HasFrameToTake());
    mDecoder->ClearHasFrameToTake();

    if (!frame) {
      MOZ_ASSERT_UNREACHABLE("Decoder yielded but didn't produce a frame?");
      return true;
    }

    // We should've gotten a different frame than last time.
    MOZ_ASSERT(!mFrames->IsLastInsertedFrame(frame));

    // Append the new frame to the list.
    AnimationFrameBuffer::InsertStatus status =
        mFrames->Insert(std::move(frame));

    // If we hit a redecode error, then we actually want to stop. This happens
    // when we tried to insert more frames than we originally had (e.g. the
    // original decoder attempt hit an OOM error sooner than we did). Better to
    // stop the animation than to get out of sync with FrameAnimator.
    if (mFrames->HasRedecodeError()) {
      mDecoder = nullptr;
      return false;
    }

    switch (status) {
      case AnimationFrameBuffer::InsertStatus::DISCARD_CONTINUE:
        continueDecoding = true;
        [[fallthrough]];
      case AnimationFrameBuffer::InsertStatus::DISCARD_YIELD:
        RequestFrameDiscarding();
        break;
      case AnimationFrameBuffer::InsertStatus::CONTINUE:
        continueDecoding = true;
        break;
      case AnimationFrameBuffer::InsertStatus::YIELD:
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled insert status!");
        break;
    }

    // We only want to handle the first frame if it is the first pass for the
    // animation decoder. The owning image will be cleared after that.
    size_t frameCount = mFrames->Size();
    if (frameCount == 1 && mImage) {
      justGotFirstFrame = true;
    }
  }

  if (justGotFirstFrame) {
    AnnounceSurfaceAvailable();
  }

  return continueDecoding;
}

bool AnimationSurfaceProvider::CheckForNewFrameAtTerminalState() {
  mDecodingMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(mDecoder);

  bool justGotFirstFrame = false;
  bool continueDecoding;

  {
    MutexAutoLock lock(mFramesMutex);

    // The decoder may or may not have a new frame for us at this point. Avoid
    // reinserting the same frame again.
    RefPtr<imgFrame> frame = mDecoder->GetCurrentFrame();

    // If the decoder didn't finish a new frame (ie if, after starting the
    // frame, it got an error and aborted the frame and the rest of the decode)
    // that means it won't be reporting it to the image or FrameAnimator so we
    // should ignore it too, that's what HasFrameToTake tracks basically.
    if (!mDecoder->HasFrameToTake()) {
      frame = nullptr;
    } else {
      MOZ_ASSERT(frame);
      mDecoder->ClearHasFrameToTake();
    }

    if (!frame || mFrames->IsLastInsertedFrame(frame)) {
      return mFrames->MarkComplete(mDecoder->GetFirstFrameRefreshArea());
    }

    // Append the new frame to the list.
    AnimationFrameBuffer::InsertStatus status =
        mFrames->Insert(std::move(frame));

    // If we hit a redecode error, then we actually want to stop. This will be
    // fully handled in FinishDecoding.
    if (mFrames->HasRedecodeError()) {
      return false;
    }

    switch (status) {
      case AnimationFrameBuffer::InsertStatus::DISCARD_CONTINUE:
      case AnimationFrameBuffer::InsertStatus::DISCARD_YIELD:
        RequestFrameDiscarding();
        break;
      case AnimationFrameBuffer::InsertStatus::CONTINUE:
      case AnimationFrameBuffer::InsertStatus::YIELD:
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled insert status!");
        break;
    }

    continueDecoding =
        mFrames->MarkComplete(mDecoder->GetFirstFrameRefreshArea());

    // We only want to handle the first frame if it is the first pass for the
    // animation decoder. The owning image will be cleared after that.
    if (mFrames->Size() == 1 && mImage) {
      justGotFirstFrame = true;
    }
  }

  if (justGotFirstFrame) {
    AnnounceSurfaceAvailable();
  }

  return continueDecoding;
}

void AnimationSurfaceProvider::RequestFrameDiscarding() {
  mDecodingMutex.AssertCurrentThreadOwns();
  mFramesMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(mDecoder);

  if (mFrames->MayDiscard() || mFrames->IsRecycling()) {
    MOZ_ASSERT_UNREACHABLE("Already replaced frame queue!");
    return;
  }

  auto oldFrameQueue =
      static_cast<AnimationFrameRetainedBuffer*>(mFrames.get());

  MOZ_ASSERT(!mDecoder->GetFrameRecycler());
  if (StaticPrefs::image_animated_decode_on_demand_recycle_AtStartup()) {
    mFrames.reset(new AnimationFrameRecyclingQueue(std::move(*oldFrameQueue)));
    mDecoder->SetFrameRecycler(this);
  } else {
    mFrames.reset(new AnimationFrameDiscardingQueue(std::move(*oldFrameQueue)));
  }
}

void AnimationSurfaceProvider::AnnounceSurfaceAvailable() {
  mFramesMutex.AssertNotCurrentThreadOwns();
  MOZ_ASSERT(mImage);

  // We just got the first frame; let the surface cache know. We deliberately do
  // this outside of mFramesMutex to avoid a potential deadlock with
  // AddSizeOfExcludingThis(), since otherwise we'd be acquiring mFramesMutex
  // and then the surface cache lock, while the memory reporting code would
  // acquire the surface cache lock and then mFramesMutex.
  SurfaceCache::SurfaceAvailable(WrapNotNull(this));
}

void AnimationSurfaceProvider::FinishDecoding() {
  mDecodingMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(mDecoder);

  if (mImage) {
    // Send notifications.
    NotifyDecodeComplete(WrapNotNull(mImage), WrapNotNull(mDecoder));
  }

  // Determine if we need to recreate the decoder, in case we are discarding
  // frames and need to loop back to the beginning.
  bool recreateDecoder;
  {
    MutexAutoLock lock(mFramesMutex);
    recreateDecoder = !mFrames->HasRedecodeError() && mFrames->MayDiscard();
  }

  if (recreateDecoder) {
#ifdef MOZ_JXL
    Vector<JxlFrameInfo> scannedFrames;
    uint32_t seekTarget = 0;
    bool isJXL = mDecoder->GetType() == DecoderType::JXL;
    if (isJXL) {
      auto* jxlDecoder = static_cast<nsJXLDecoder*>(mDecoder.get());
      // OOM here is acceptable: the decoder will re-scan from scratch.
      (void)scannedFrames.appendAll(jxlDecoder->ScannedFrames());

      MutexAutoLock lock(mFramesMutex);
      MOZ_ASSERT(mFrames->MayDiscard());
      auto* discardingQueue =
          static_cast<AnimationFrameDiscardingQueue*>(mFrames.get());
      seekTarget = static_cast<uint32_t>(discardingQueue->PendingInsert());
    }
#endif

    mDecoder = DecoderFactory::CloneAnimationDecoder(mDecoder);
    MOZ_ASSERT(mDecoder);

#ifdef MOZ_JXL
    if (isJXL && !scannedFrames.empty()) {
      auto* jxlDecoder = static_cast<nsJXLDecoder*>(mDecoder.get());
      jxlDecoder->TransferScannedFrames(std::move(scannedFrames));
      jxlDecoder->SetSeekTargetFrame(seekTarget);
    }
#endif
  } else {
    mDecoder = nullptr;
  }

  // We don't need a reference to our image anymore, either, and we don't want
  // one. We may be stored in the surface cache for a long time after decoding
  // finishes. If we don't drop our reference to the image, we'll end up
  // keeping it alive as long as we remain in the surface cache, which could
  // greatly extend the image's lifetime - in fact, if the image isn't
  // discardable, it'd result in a leak!
  DropImageReference();
}

bool AnimationSurfaceProvider::ShouldPreferSyncRun() const {
  MutexAutoLock lock(mDecodingMutex);
  MOZ_ASSERT(mDecoder);

  return mDecoder->ShouldSyncDecode(
      StaticPrefs::image_mem_decode_bytes_at_a_time_AtStartup());
}

RawAccessFrameRef AnimationSurfaceProvider::RecycleFrame(
    gfx::IntRect& aRecycleRect) {
  MutexAutoLock lock(mFramesMutex);
  MOZ_ASSERT(mFrames->IsRecycling());
  return mFrames->RecycleFrame(aRecycleRect);
}

nsresult AnimationSurfaceProvider::UpdateKey(
    layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources, wr::ImageKey& aKey) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<SourceSurface> surface;
  {
    MutexAutoLock lock(mFramesMutex);
    imgFrame* frame =
        mFrames->Get(mFrames->Displayed(), /* aForDisplay */ true);
    if (!frame) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    surface = frame->GetSourceSurface();
  }

  mCompositedFrameRequested = true;
  auto* sharedSurface = static_cast<SourceSurfaceSharedData*>(surface.get());
  return mSharedAnimation->UpdateKey(sharedSurface, aManager, aResources, aKey);
}

}  // namespace image
}  // namespace mozilla
