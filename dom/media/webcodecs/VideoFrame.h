/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_VideoFrame_h
#define mozilla_dom_VideoFrame_h

#include "MediaResult.h"
#include "js/TypeDecls.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Span.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BufferSourceBindingFwd.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/media/MediaUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsTArrayForwardDeclare.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;
class nsIURI;

namespace mozilla {

namespace layers {
class Image;
}  // namespace layers

namespace dom {

class DOMRectReadOnly;
class HTMLCanvasElement;
class HTMLImageElement;
class HTMLVideoElement;
class ImageBitmap;
class OffscreenCanvas;
class Promise;
class SVGImageElement;
class StructuredCloneHolder;
class VideoColorSpace;
class VideoFrame;
enum class PredefinedColorSpace : uint8_t;
enum class VideoPixelFormat : uint8_t;
struct VideoFrameBufferInit;
struct VideoFrameCopyToOptions;
struct VideoFrameInit;

}  // namespace dom

namespace webgpu {
class ExternalTexture;
}  // namespace webgpu

}  // namespace mozilla

namespace mozilla::dom {

struct VideoFrameData;
struct VideoFrameSerializedData;

class VideoFrame final : public nsISupports,
                         public nsWrapperCache,
                         public media::ShutdownConsumer {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(VideoFrame)

 public:
  class Format;
  class Resource;

  // The only way to obtain a Resource.
  static Result<Resource, MediaResult> ValidateImage(
      const RefPtr<layers::Image>& aImage, Maybe<Format> aFormat);

  static Result<RefPtr<VideoFrame>, MediaResult> CreateFromImage(
      nsIGlobalObject* aParent, const RefPtr<layers::Image>& aImage,
      const Maybe<VideoPixelFormat>& aFormat, gfx::IntRect aVisibleRect,
      gfx::IntSize aDisplaySize, const Maybe<uint64_t>& aDuration,
      int64_t aTimestamp, const VideoColorSpaceInternal& aColorSpace);

  VideoFrame(nsIGlobalObject* aParent, const Resource& aResource,
             gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
             const Maybe<uint64_t>& aDuration, int64_t aTimestamp,
             const VideoColorSpaceInternal& aColorSpace);
  VideoFrame(nsIGlobalObject* aParent, const VideoFrameData& aData);
  VideoFrame(const VideoFrame& aOther);

 protected:
  ~VideoFrame();

 public:
  nsIGlobalObject* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static bool PrefEnabled(JSContext* aCx, JSObject* aObj = nullptr);

  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, HTMLImageElement& aImageElement,
      const VideoFrameInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, SVGImageElement& aSVGImageElement,
      const VideoFrameInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, HTMLCanvasElement& aCanvasElement,
      const VideoFrameInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, HTMLVideoElement& aVideoElement,
      const VideoFrameInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, OffscreenCanvas& aOffscreenCanvas,
      const VideoFrameInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& aGlobal,
                                                  ImageBitmap& aImageBitmap,
                                                  const VideoFrameInit& aInit,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(const GlobalObject& aGlobal,
                                                  VideoFrame& aVideoFrame,
                                                  const VideoFrameInit& aInit,
                                                  ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, const ArrayBufferView& aBufferView,
      const VideoFrameBufferInit& aInit, ErrorResult& aRv);
  static already_AddRefed<VideoFrame> Constructor(
      const GlobalObject& aGlobal, const ArrayBuffer& aBuffer,
      const VideoFrameBufferInit& aInit, ErrorResult& aRv);

  Nullable<VideoPixelFormat> GetFormat() const;

  uint32_t CodedWidth() const;

  uint32_t CodedHeight() const;

  already_AddRefed<DOMRectReadOnly> GetCodedRect() const;

  already_AddRefed<DOMRectReadOnly> GetVisibleRect() const;

  uint32_t DisplayWidth() const;

  uint32_t DisplayHeight() const;

  Nullable<uint64_t> GetDuration() const;

  int64_t Timestamp() const;

  already_AddRefed<VideoColorSpace> ColorSpace() const;

  uint32_t AllocationSize(const VideoFrameCopyToOptions& aOptions,
                          ErrorResult& aRv);

  already_AddRefed<Promise> CopyTo(const AllowSharedBufferSource& aDestination,
                                   const VideoFrameCopyToOptions& aOptions,
                                   ErrorResult& aRv);

  already_AddRefed<VideoFrame> Clone(ErrorResult& aRv) const;

  void Close();
  bool IsClosed() const;
  void OnShutdown() override;

  // [Serializable] implementations: {Read, Write}StructuredClone
  static JSObject* ReadStructuredClone(JSContext* aCx, nsIGlobalObject* aGlobal,
                                       JSStructuredCloneReader* aReader,
                                       const VideoFrameSerializedData& aData);

  bool WriteStructuredClone(JSStructuredCloneWriter* aWriter,
                            StructuredCloneHolder* aHolder) const;

  // [Transferable] implementations: Transfer, FromTransferred
  using TransferredData = VideoFrameSerializedData;

  UniquePtr<TransferredData> Transfer();

  static already_AddRefed<VideoFrame> FromTransferred(nsIGlobalObject* aGlobal,
                                                      TransferredData* aData);

  // Native only methods.
  gfx::IntSize NativeCodedSize() const {
    return mResource ? mResource->mCodedSize : gfx::IntSize();
  }
  const gfx::IntSize& NativeDisplaySize() const { return mDisplaySize; }
  const gfx::IntRect& NativeVisibleRect() const { return mVisibleRect; }
  already_AddRefed<layers::Image> GetImage() const;

  // Track a WebGPU ExternalTexture as being imported from this video frame.
  // This ensures it will be correctly expired when the video frame is closed.
  void TrackWebGPUExternalTexture(
      WeakPtr<webgpu::ExternalTexture> aExternalTexture);

  nsCString ToString() const;

 public:
  // A VideoPixelFormat wrapper providing utilities for VideoFrame.
  class Format final {
   public:
    explicit Format(const VideoPixelFormat& aFormat);
    ~Format() = default;
    const VideoPixelFormat& PixelFormat() const;
    gfx::SurfaceFormat ToSurfaceFormat() const;

    // TODO: Assign unique value for each plane?
    // The value indicates the order of the plane in format.
    enum class Plane : uint8_t { Y = 0, RGBA = Y, U = 1, UV = U, V = 2, A = 3 };
    nsTArray<Plane> Planes() const;
    const char* PlaneName(const Plane& aPlane) const;
    uint32_t SampleBytes(const Plane& aPlane) const;
    gfx::IntSize SampleSize(const Plane& aPlane) const;
    bool IsValidSize(const gfx::IntSize& aSize) const;
    Result<size_t, MediaResult> ByteCount(const gfx::IntSize& aSize) const;

   private:
    friend class Resource;
    bool IsYUV() const;
    void MakeOpaque();
    VideoPixelFormat mFormat;
  };

  // A validated layers::Image with cached properties.
  class Resource final {
   public:
    Resource(const Resource& aOther) = default;
    ~Resource() = default;
    Maybe<VideoPixelFormat> TryPixelFormat() const;
    Resource CreateOpaque() const;
    bool CopyPlaneInto(const Format::Plane& aPlane, const gfx::IntRect& aRect,
                       Span<uint8_t> aPlaneDest,
                       size_t aDestinationStride) const;

    const RefPtr<layers::Image> mImage;
    const Maybe<Format> mFormat;
    const gfx::IntSize mCodedSize;
    const gfx::IntRect mPictureRect;

   private:
    friend class VideoFrame;
    Resource(const RefPtr<layers::Image>& aImage, Maybe<Format> aFormat,
             gfx::IntSize aCodedSize, gfx::IntRect aPictureRect);
  };

 private:
  // VideoFrame can run on either main thread or worker thread.
  void AssertIsOnOwningThread() const { NS_ASSERT_OWNINGTHREAD(VideoFrame); }

  already_AddRefed<VideoFrame> ConvertToRGBFrame(
      const VideoPixelFormat& aFormat, const PredefinedColorSpace& aColorSpace);

  VideoFrameData GetVideoFrameData() const;

  // Below helpers are used to automatically release the holding Resource if
  // VideoFrame is never Close()d by the users.
  void StartAutoClose();
  void StopAutoClose();
  void CloseIfNeeded();

  nsCOMPtr<nsIGlobalObject> mParent;

  // Use Maybe instead of UniquePtr to allow copy ctor.
  // The mResource's existence is used as the [[Detached]] for [Transferable].
  Maybe<const Resource> mResource;  // Nothing() after `Close()`d

  gfx::IntRect mVisibleRect;
  gfx::IntSize mDisplaySize;

  Maybe<uint64_t> mDuration;
  int64_t mTimestamp;
  VideoColorSpaceInternal mColorSpace;

  // The following are used to help monitoring mResource release.
  RefPtr<media::ShutdownWatcher> mShutdownWatcher = nullptr;

  // WebGPU external textures that were imported from this video frame. We must
  // call `Expire()` on them when the video frame is closed.
  nsTArray<WeakPtr<webgpu::ExternalTexture>> mWebGPUExternalTextures;
};

struct VideoFrameData {
  VideoFrameData(const VideoFrameData& aData) = default;

  const VideoFrame::Resource mResource;
  const gfx::IntRect mVisibleRect;
  const gfx::IntSize mDisplaySize;
  const Maybe<uint64_t> mDuration;
  const int64_t mTimestamp;
  const VideoColorSpaceInternal mColorSpace;

 private:
  // Only a VideoFrame can create one, so mVisibleRect was validated against
  // mResource.mCodedSize.
  friend class VideoFrame;
  VideoFrameData(const VideoFrame::Resource& aResource,
                 gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
                 Maybe<uint64_t> aDuration, int64_t aTimestamp,
                 const VideoColorSpaceInternal& aColorSpace);
};

struct VideoFrameSerializedData : VideoFrameData {
  explicit VideoFrameSerializedData(const VideoFrameData& aData);
  VideoFrameSerializedData(const VideoFrameSerializedData& aData) = default;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_VideoFrame_h
