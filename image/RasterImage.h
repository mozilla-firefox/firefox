/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @file
 * This file declares the RasterImage class, which
 * handles static and animated rasterized images.
 *
 * @author  Stuart Parmenter <pavlov@netscape.com>
 * @author  Chris Saari <saari@netscape.com>
 * @author  Arron Mogge <paper@animecity.nu>
 * @author  Andrew Smith <asmith15@learn.senecac.on.ca>
 */

#ifndef mozilla_image_RasterImage_h
#define mozilla_image_RasterImage_h

#include "DecoderFactory.h"
#include "FrameAnimator.h"
#include "ISurfaceProvider.h"
#include "Image.h"
#include "ImageContainer.h"
#include "ImageMetadata.h"
#include "LookupResult.h"
#include "Orientation.h"
#include "PlaybackType.h"
#include "imgIContainer.h"
#include "mozilla/AtomicBitfields.h"
#include "mozilla/Maybe.h"
#include "mozilla/NotNull.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/image/Resolution.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#ifdef DEBUG
#  include "imgIContainerDebug.h"
#endif

class nsIInputStream;
class nsIRequest;

#define NS_RASTERIMAGE_CID                    \
  {/* 376ff2c1-9bf6-418a-b143-3340c00112f7 */ \
   0x376ff2c1,                                \
   0x9bf6,                                    \
   0x418a,                                    \
   {0xb1, 0x43, 0x33, 0x40, 0xc0, 0x01, 0x12, 0xf7}}

/**
 * Handles static and animated image containers.
 *
 *
 * @par A Quick Walk Through
 * The decoder initializes this class and calls AppendFrame() to add a frame.
 * Once RasterImage detects more than one frame, it starts the animation
 * with StartAnimation(). Note that the invalidation events for RasterImage are
 * generated automatically using nsRefreshDriver.
 *
 * @par
 * StartAnimation() initializes the animation helper object and sets the time
 * the first frame was displayed to the current clock time.
 *
 * @par
 * When the refresh driver corresponding to the imgIContainer that this image is
 * a part of notifies the RasterImage that it's time to invalidate,
 * RequestRefresh() is called with a given TimeStamp to advance to. As long as
 * the timeout of the given frame (the frame's "delay") plus the time that frame
 * was first displayed is less than or equal to the TimeStamp given,
 * RequestRefresh() calls AdvanceFrame().
 *
 * @par
 * AdvanceFrame() is responsible for advancing a single frame of the animation.
 * It can return true, meaning that the frame advanced, or false, meaning that
 * the frame failed to advance (usually because the next frame hasn't been
 * decoded yet). It is also responsible for performing the final animation stop
 * procedure if the final frame of a non-looping animation is reached.
 *
 * @par
 * Each frame can have a different method of removing itself. These are
 * listed as imgIContainer::cDispose... constants.  Notify() calls
 * DoComposite() to handle any special frame destruction.
 *
 * @par
 * The basic path through DoComposite() is:
 * 1) Calculate Area that needs updating, which is at least the area of
 *    aNextFrame.
 * 2) Dispose of previous frame.
 * 3) Draw new image onto compositingFrame.
 * See comments in DoComposite() for more information and optimizations.
 *
 * @par
 * The rest of the RasterImage specific functions are used by DoComposite to
 * destroy the old frame and build the new one.
 *
 * @note
 * <li> "Mask", "Alpha", and "Alpha Level" are interchangeable phrases in
 * respects to RasterImage.
 *
 * @par
 * <li> GIFs never have more than a 1 bit alpha.
 * <li> APNGs may have a full alpha channel.
 *
 * @par
 * <li> Background color specified in GIF is ignored by web browsers.
 *
 * @par
 * <li> If Frame 3 wants to dispose by restoring previous, what it wants is to
 * restore the composition up to and including Frame 2, as well as Frame 2s
 * disposal.  So, in the middle of DoComposite when composing Frame 3, right
 * after destroying Frame 2's area, we copy compositingFrame to
 * prevCompositingFrame.  When DoComposite gets called to do Frame 4, we
 * copy prevCompositingFrame back, and then draw Frame 4 on top.
 *
 * @par
 * The mAnim structure has members only needed for animated images, so
 * it's not allocated until the second frame is added.
 */

namespace mozilla {
namespace layers {
class ImageContainer;
class Image;
class LayersManager;
}  // namespace layers

namespace image {

class Decoder;
struct DecoderFinalStatus;
struct DecoderTelemetry;
class ImageMetadata;
class SourceBuffer;

class RasterImage final : public ImageResource,
                          public SupportsWeakPtr
#ifdef DEBUG
    ,
                          public imgIContainerDebug
#endif
{
  // (no public constructor - use ImageFactory)
  virtual ~RasterImage();

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_IMGICONTAINER
#ifdef DEBUG
  NS_DECL_IMGICONTAINERDEBUG
#endif

  virtual nsresult StartAnimation() override;
  virtual nsresult StopAnimation() override;

  // Methods inherited from Image
  virtual void OnSurfaceDiscarded(const SurfaceKey& aSurfaceKey) override;

  virtual size_t SizeOfSourceWithComputedFallback(
      SizeOfState& aState) const override;

  /* Triggers discarding. */
  void Discard();

  //////////////////////////////////////////////////////////////////////////////
  // Decoder callbacks.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * Sends the provided progress notifications to ProgressTracker.
   *
   * Main-thread only.
   *
   * @param aProgress    The progress notifications to send.
   * @param aInvalidRect An invalidation rect to send.
   * @param aFrameCount  If Some(), an updated count of the number of frames of
   *                     animation the decoder has finished decoding so far.
   *                     This is a lower bound for the total number of
   *                     animation frames this image has.
   * @param aDecoderFlags The decoder flags used by the decoder that generated
   *                      these notifications, or DefaultDecoderFlags() if the
   *                      notifications don't come from a decoder.
   * @param aSurfaceFlags The surface flags used by the decoder that generated
   *                      these notifications, or DefaultSurfaceFlags() if the
   *                      notifications don't come from a decoder.
   */
  void NotifyProgress(Progress aProgress,
                      const OrientedIntRect& aInvalidRect = OrientedIntRect(),
                      const Maybe<uint32_t>& aFrameCount = Nothing(),
                      DecoderFlags aDecoderFlags = DefaultDecoderFlags(),
                      SurfaceFlags aSurfaceFlags = DefaultSurfaceFlags());

  /**
   * Records decoding results, sends out any final notifications, updates the
   * state of this image, and records telemetry.
   *
   * Main-thread only.
   *
   * @param aStatus       Final status information about the decoder. (Whether
   * it encountered an error, etc.)
   * @param aMetadata     Metadata about this image that the decoder gathered.
   * @param aTelemetry    Telemetry data about the decoder.
   * @param aProgress     Any final progress notifications to send.
   * @param aInvalidRect  Any final invalidation rect to send.
   * @param aFrameCount   If Some(), a final updated count of the number of
   * frames of animation the decoder has finished decoding so far. This is a
   * lower bound for the total number of animation frames this image has.
   * @param aDecoderFlags The decoder flags used by the decoder.
   * @param aSurfaceFlags The surface flags used by the decoder.
   */
  void NotifyDecodeComplete(
      const DecoderFinalStatus& aStatus, const ImageMetadata& aMetadata,
      const DecoderTelemetry& aTelemetry, Progress aProgress,
      const OrientedIntRect& aInvalidRect, const Maybe<uint32_t>& aFrameCount,
      DecoderFlags aDecoderFlags, SurfaceFlags aSurfaceFlags);

  // Helper method for NotifyDecodeComplete.
  void ReportDecoderError();

  //////////////////////////////////////////////////////////////////////////////
  // Network callbacks.
  //////////////////////////////////////////////////////////////////////////////

  virtual nsresult OnImageDataAvailable(nsIRequest* aRequest,
                                        nsIInputStream* aInStr,
                                        uint64_t aSourceOffset,
                                        uint32_t aCount) override;
  virtual nsresult OnImageDataComplete(nsIRequest* aRequest, nsresult aStatus,
                                       bool aLastPart) override;

  void NotifyForLoadEvent(Progress aProgress);

  /**
   * A hint of the number of bytes of source data that the image contains. If
   * called early on, this can help reduce copying and reallocations by
   * appropriately preallocating the source data buffer.
   *
   * We take this approach rather than having the source data management code do
   * something more complicated (like chunklisting) because HTTP is by far the
   * dominant source of images, and the Content-Length header is quite reliable.
   * Thus, pre-allocation simplifies code and reduces the total number of
   * allocations.
   */
  nsresult SetSourceSizeHint(uint32_t aSizeHint);

 private:
  nsresult Init(const char* aMimeType, uint32_t aFlags);

  /**
   * Tries to retrieve a surface for this image with size @aSize, surface flags
   * matching @aFlags, and a playback type of @aPlaybackType.
   *
   * If @aFlags specifies FLAG_SYNC_DECODE and we already have all the image
   * data, we'll attempt a sync decode if no matching surface is found. If
   * FLAG_SYNC_DECODE was not specified and no matching surface was found, we'll
   * kick off an async decode so that the surface is (hopefully) available next
   * time it's requested. aMarkUsed determines if we mark the surface used in
   * the surface cache or not.
   *
   * @return a drawable surface, which may be empty if the requested surface
   *         could not be found.
   */
  LookupResult LookupFrame(const OrientedIntSize& aSize, uint32_t aFlags,
                           PlaybackType aPlaybackType, bool aMarkUsed);

  /// Helper method for LookupFrame().
  LookupResult LookupFrameInternal(const OrientedIntSize& aSize,
                                   uint32_t aFlags, PlaybackType aPlaybackType,
                                   bool aMarkUsed);

  ImgDrawResult DrawInternal(DrawableSurface&& aFrameRef, gfxContext* aContext,
                             const OrientedIntSize& aSize,
                             const ImageRegion& aRegion,
                             gfx::SamplingFilter aSamplingFilter,
                             uint32_t aFlags, float aOpacity);

  //////////////////////////////////////////////////////////////////////////////
  // Decoding.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * Creates and runs a decoder, either synchronously or asynchronously
   * according to @aFlags. Decodes at the provided target size @aSize, using
   * decode flags @aFlags. Performs a single-frame decode of this image unless
   * we know the image is animated *and* @aPlaybackType is
   * PlaybackType::eAnimated.
   *
   * It's an error to call Decode() before this image's intrinsic size is
   * available. A metadata decode must successfully complete first.
   *
   * aOutRanSync is set to true if the decode was run synchronously.
   * aOutFailed is set to true if failed to start a decode.
   */
  void Decode(const OrientedIntSize& aSize, uint32_t aFlags,
              PlaybackType aPlaybackType, bool& aOutRanSync, bool& aOutFailed);

  /**
   * Creates and runs a metadata decoder, either synchronously or
   * asynchronously according to @aFlags.
   */
  NS_IMETHOD DecodeMetadata(uint32_t aFlags);

  /**
   * Sets the size, inherent orientation, animation metadata, and other
   * information about the image gathered during decoding.
   *
   * This function may be called multiple times, but will throw an error if
   * subsequent calls do not match the first.
   *
   * @param aMetadata The metadata to set on this image.
   * @param aFromMetadataDecode True if this metadata came from a metadata
   *                            decode; false if it came from a full decode.
   * @return |true| unless a catastrophic failure was discovered. If |false| is
   * returned, it indicates that the image is corrupt in a way that requires all
   * surfaces to be discarded to recover.
   */
  bool SetMetadata(const ImageMetadata& aMetadata, bool aFromMetadataDecode);

  /**
   * In catastrophic circumstances like a GPU driver crash, the contents of our
   * frames may become invalid.  If the information we gathered during the
   * metadata decode proves to be wrong due to image corruption, the frames we
   * have may violate this class's invariants. Either way, we need to
   * immediately discard the invalid frames and redecode so that callers don't
   * perceive that we've entered an invalid state.
   *
   * RecoverFromInvalidFrames discards all existing frames and redecodes using
   * the provided @aSize and @aFlags.
   */
  void RecoverFromInvalidFrames(const OrientedIntSize& aSize, uint32_t aFlags);

  void OnSurfaceDiscardedInternal(bool aAnimatedFramesDiscarded);

 private:  // data
  OrientedIntSize mSize;
  nsTArray<OrientedIntSize> mNativeSizes;

  // The orientation required to correctly orient the image, from the image's
  // metadata. RasterImage will handle and apply this orientation itself.
  Orientation mOrientation;

  // The resolution as specified in the image metadata, in dppx.
  Resolution mResolution;

  /// If this has a value, we're waiting for SetSize() to send the load event.
  Maybe<Progress> mLoadProgress;

  // Hotspot of this image, or (0, 0) if there is no hotspot data.
  //
  // We assume (and assert) that no image has both orientation metadata and a
  // hotspot, so we store this as an untyped point.
  gfx::IntPoint mHotspot;

  /// If this image is animated, a FrameAnimator which manages its animation.
  UniquePtr<FrameAnimator> mFrameAnimator;

  /// Animation timeline and other state for animation images.
  Maybe<AnimationState> mAnimationState;

  // Image locking.
  uint32_t mLockCount;

  // The type of decoder this image needs. Computed from the MIME type in
  // Init().
  DecoderType mDecoderType;

  // How many times we've decoded this image.
  // This is currently only used for statistics
  int32_t mDecodeCount;

#ifdef DEBUG
  uint32_t mFramesNotified;
#endif

  // The source data for this image.
  NotNull<RefPtr<SourceBuffer>> mSourceBuffer;

  // Boolean flags (clustered together to conserve space):
  MOZ_ATOMIC_BITFIELDS(
      mAtomicBitfields, 16,
      ((bool, HasSize, 1),         // Has SetSize() been called?
       (bool, Transient, 1),       // Is the image short-lived?
       (bool, SyncLoad, 1),        // Are we loading synchronously?
       (bool, Discardable, 1),     // Is container discardable?
       (bool, SomeSourceData, 1),  // Do we have some source data?
       (bool, AllSourceData, 1),   // Do we have all the source data?
       (bool, HasBeenDecoded, 1),  // Decoded at least once?

       // Whether we're waiting to start animation. If we get a StartAnimation()
       // call but we don't yet have more than one frame, mPendingAnimation is
       // set so that we know to start animation later if/when we have more
       // frames.
       (bool, PendingAnimation, 1),

       // Whether the animation can stop, due to running out
       // of frames, or no more owning request
       (bool, AnimationFinished, 1),

       // Whether, once we are done doing a metadata decode, we should
       // immediately kick off a full decode.
       (bool, WantFullDecode, 1)))

  TimeStamp mDrawStartTime;

  //////////////////////////////////////////////////////////////////////////////
  // Scaling.
  //////////////////////////////////////////////////////////////////////////////

  // Determines whether we can downscale during decode with the given
  // parameters.
  bool CanDownscaleDuringDecode(const OrientedIntSize& aSize, uint32_t aFlags);

  // Error handling.
  void DoError();

  class HandleErrorWorker : public Runnable {
   public:
    /**
     * Called from decoder threads when DoError() is called, since errors can't
     * be handled safely off-main-thread. Dispatches an event which reinvokes
     * DoError on the main thread if there isn't one already pending.
     */
    static void DispatchIfNeeded(RasterImage* aImage);

    NS_IMETHOD Run() override;

   private:
    explicit HandleErrorWorker(RasterImage* aImage);

    RefPtr<RasterImage> mImage;
  };

  // Helpers
  bool CanDiscard();

  bool IsOpaque();

  LookupResult RequestDecodeForSizeInternal(const OrientedIntSize& aSize,
                                            uint32_t aFlags,
                                            uint32_t aWhichFrame);

 protected:
  explicit RasterImage(nsIURI* aURI = nullptr);

  bool ShouldAnimate() override;

  friend class ImageFactory;
};

inline NS_IMETHODIMP RasterImage::GetAnimationMode(uint16_t* aAnimationMode) {
  return GetAnimationModeInternal(aAnimationMode);
}

}  // namespace image
}  // namespace mozilla

/**
 * Casting RasterImage to nsISupports is ambiguous. This method handles that.
 */
inline nsISupports* ToSupports(mozilla::image::RasterImage* p) {
  return NS_ISUPPORTS_CAST(mozilla::image::ImageResource*, p);
}

#endif /* mozilla_image_RasterImage_h */
