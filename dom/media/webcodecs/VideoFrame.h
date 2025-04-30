/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_VideoFrame_h
#define mozilla_dom_VideoFrame_h

#include "js/TypeDecls.h"
#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/NotNull.h"
#include "mozilla/Span.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/media/MediaUtils.h"
#include "nsCycleCollectionParticipant.h"
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
}  // namespace mozilla

namespace mozilla::dom {

struct VideoFrameData {
  VideoFrameData(layers::Image* aImage, const Maybe<VideoPixelFormat>& aFormat,
                 gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
                 Maybe<uint64_t> aDuration, int64_t aTimestamp,
                 const VideoColorSpaceInternal& aColorSpace);
  VideoFrameData(const VideoFrameData& aData) = default;

  const RefPtr<layers::Image> mImage;
  const Maybe<VideoPixelFormat> mFormat;
  const gfx::IntRect mVisibleRect;
  const gfx::IntSize mDisplaySize;
  const Maybe<uint64_t> mDuration;
  const int64_t mTimestamp;
  const VideoColorSpaceInternal mColorSpace;
};

struct VideoFrameSerializedData : VideoFrameData {
  VideoFrameSerializedData(const VideoFrameData& aData,
                           gfx::IntSize aCodedSize);

  const gfx::IntSize mCodedSize;
};

class VideoFrame final : public nsISupports,
                         public nsWrapperCache,
                         public media::ShutdownConsumer {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(VideoFrame)

 public:
  VideoFrame(nsIGlobalObject* aParent, const RefPtr<layers::Image>& aImage,
             const Maybe<VideoPixelFormat>& aFormat, gfx::IntSize aCodedSize,
             gfx::IntRect aVisibleRect, gfx::IntSize aDisplaySize,
             const Maybe<uint64_t>& aDuration, int64_t aTimestamp,
             const VideoColorSpaceInternal& aColorSpace);
  VideoFrame(nsIGlobalObject* aParent, const VideoFrameSerializedData& aData);
  VideoFrame(const VideoFrame& aOther);

 protected:
  ~VideoFrame();

 public:
  nsIGlobalObject* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static bool PrefEnabled(JSContext* aCx = nullptr, JSObject* aObj = nullptr);

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
  const gfx::IntSize& NativeCodedSize() const { return mCodedSize; }
  const gfx::IntSize& NativeDisplaySize() const { return mDisplaySize; }
  const gfx::IntRect& NativeVisibleRect() const { return mVisibleRect; }
  already_AddRefed<layers::Image> GetImage() const;

  nsCString ToString() const;

 public:
  // A VideoPixelFormat wrapper providing utilities for VideoFrame.
  class Format final {
   public:
    explicit Format(const VideoPixelFormat& aFormat);
    ~Format() = default;
    const VideoPixelFormat& PixelFormat() const;
    gfx::SurfaceFormat ToSurfaceFormat() const;
    void MakeOpaque();

    // TODO: Assign unique value for each plane?
    // The value indicates the order of the plane in format.
    enum class Plane : uint8_t { Y = 0, RGBA = Y, U = 1, UV = U, V = 2, A = 3 };
    nsTArray<Plane> Planes() const;
    const char* PlaneName(const Plane& aPlane) const;
    uint32_t SampleBytes(const Plane& aPlane) const;
    gfx::IntSize SampleSize(const Plane& aPlane) const;
    bool IsValidSize(const gfx::IntSize& aSize) const;
    size_t ByteCount(const gfx::IntSize& aSize) const;

   private:
    bool IsYUV() const;
    VideoPixelFormat mFormat;
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

  // A class representing the VideoFrame's data.
  class Resource final {
   public:
    Resource(const RefPtr<layers::Image>& aImage, Maybe<Format>&& aFormat);
    Resource(const Resource& aOther);
    ~Resource() = default;
    Maybe<VideoPixelFormat> TryPixelFormat() const;
    uint32_t Stride(const Format::Plane& aPlane) const;
    bool CopyTo(const Format::Plane& aPlane, const gfx::IntRect& aRect,
                Span<uint8_t>&& aPlaneDest, size_t aDestinationStride) const;

    const RefPtr<layers::Image> mImage;
    // Nothing() if mImage is not in VideoPixelFormat
    const Maybe<Format> mFormat;
  };

  nsCOMPtr<nsIGlobalObject> mParent;

  // Use Maybe instead of UniquePtr to allow copy ctor.
  // The mResource's existence is used as the [[Detached]] for [Transferable].
  Maybe<const Resource> mResource;  // Nothing() after `Close()`d

  // TODO: Replace this by mResource->mImage->GetSize()?
  gfx::IntSize mCodedSize;
  gfx::IntRect mVisibleRect;
  gfx::IntSize mDisplaySize;

  Maybe<uint64_t> mDuration;
  int64_t mTimestamp;
  VideoColorSpaceInternal mColorSpace;

  // The following are used to help monitoring mResource release.
  RefPtr<media::ShutdownWatcher> mShutdownWatcher = nullptr;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_VideoFrame_h
